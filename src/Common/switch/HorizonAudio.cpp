// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonAudio.h"
#include "HorizonDiagnostics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include <switch.h>

namespace
{
	constexpr AudioRendererConfig RENDERER_CONFIG = {
		.output_rate = AudioRendererOutputRate_48kHz,
		.num_voices = 24,
		.num_effects = 0,
		.num_sinks = 1,
		.num_mix_objs = 1,
		.num_mix_buffers = 2,
	};
	constexpr int OUTPUT_CHANNELS = 2;
	constexpr int DETACH_ATTEMPTS = 20;
	constexpr uint64_t FRAME_NS = 5'000'000;

	struct StereoFactors
	{
		float left;
		float right;
	};

	constexpr float DOWNMIX = 0.7071f;
	constexpr std::array<StereoFactors, 1> MONO_FACTORS{{{1.0f, 1.0f}}};
	constexpr std::array<StereoFactors, 2> STEREO_FACTORS{{{1.0f, 0.0f}, {0.0f, 1.0f}}};
	constexpr std::array<StereoFactors, 4> QUAD_FACTORS{{{1.0f, 0.0f}, {0.0f, 1.0f}, {DOWNMIX, 0.0f}, {0.0f, DOWNMIX}}};
	// FL FR FC LFE BL BR I think is correct
	constexpr std::array<StereoFactors, 6> SURROUND_FACTORS{
		{{1.0f, 0.0f}, {0.0f, 1.0f}, {DOWNMIX, DOWNMIX}, {0.0f, 0.0f}, {DOWNMIX, 0.0f}, {0.0f, DOWNMIX}}};

	const StereoFactors* GetDownmix(uint32_t channels)
	{
		switch (channels)
		{
		case 1:
			return MONO_FACTORS.data();
		case 2:
			return STEREO_FACTORS.data();
		case 4:
			return QUAD_FACTORS.data();
		case 6:
			return SURROUND_FACTORS.data();
		default:
			return nullptr;
		}
	}

	size_t AlignUp(size_t value, size_t alignment)
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}

	AudioDriver s_driver;
	std::mutex s_mutex;
	std::vector<HorizonAudio::Stream*> s_streams;
	bool s_running = false;
	uint32_t s_updateFailures = 0;
}

struct HorizonAudio::Stream
{
	int voice = -1;
	int memPool = -1;
	uint32_t blockBytes = 0;
	uint32_t blockCount = 0;
	uint8_t* memory = nullptr;
	std::unique_ptr<AudioDriverWaveBuf[]> waveBufs;

	std::mutex producerMutex;
	alignas(64) std::atomic<uint32_t> written = 0;
	alignas(64) std::atomic<uint32_t> released = 0;

	alignas(64) uint32_t submitted = 0;
	std::atomic<bool> playing = false;
	std::atomic<float> volume = 1.0f;
	bool appliedPlaying = false;
	float appliedVolume = 1.0f;
	uint32_t underruns = 0;
};

namespace
{
	void SubmitBlocks(HorizonAudio::Stream& stream)
	{
		const uint32_t written = stream.written.load(std::memory_order_acquire);
		for (; stream.submitted != written; stream.submitted++)
			audrvVoiceAddWaveBuf(&s_driver, stream.voice, &stream.waveBufs[stream.submitted % stream.blockCount]);

		const bool playing = stream.playing.load(std::memory_order_relaxed);
		if (playing != stream.appliedPlaying)
		{
			audrvVoiceSetPaused(&s_driver, stream.voice, !playing);
			stream.appliedPlaying = playing;
		}
		const float volume = stream.volume.load(std::memory_order_relaxed);
		if (volume != stream.appliedVolume)
		{
			audrvVoiceSetVolume(&s_driver, stream.voice, volume);
			stream.appliedVolume = volume;
		}
	}

	void ReleaseBlocks(HorizonAudio::Stream& stream)
	{
		uint32_t released = stream.released.load(std::memory_order_relaxed);
		if (released == stream.submitted)
			return;
		while (released != stream.submitted &&
			   stream.waveBufs[released % stream.blockCount].state == AudioDriverWaveBufState_Done)
			released++;
		if (released == stream.submitted && stream.appliedPlaying)
			stream.underruns++;
		stream.released.store(released, std::memory_order_release);
	}

	int FindFreeVoice()
	{
		for (int i = 0; i < RENDERER_CONFIG.num_voices; i++)
		{
			if (!s_driver.in_voices[i].is_used)
				return i;
		}
		return -1;
	}

	bool ReleaseMemPool(int memPool)
	{
		if (s_driver.in_mempools[memPool].state == AudioRendererMemPoolState_New)
			return audrvMemPoolRemove(&s_driver, memPool);
		audrvMemPoolDetach(&s_driver, memPool);
		for (int i = 0; i < DETACH_ATTEMPTS; i++)
		{
			audrvUpdate(&s_driver);
			if (s_driver.in_mempools[memPool].state == AudioRendererMemPoolState_Detached)
				return audrvMemPoolRemove(&s_driver, memPool);
			svcSleepThread(FRAME_NS);
		}
		return false;
	}

	void DestroyStream(HorizonAudio::Stream* stream)
	{
		if (stream->voice >= 0)
			audrvVoiceDrop(&s_driver, stream->voice);
		if (stream->memPool >= 0 && !ReleaseMemPool(stream->memPool))
		{
			HorizonDiagnostics::Log("audio: memory pool %d did not detach", stream->memPool);
			stream->memory = nullptr;
		}
		std::free(stream->memory);
		delete stream;
	}
}

namespace HorizonAudio
{
	bool Init()
	{
		std::scoped_lock lock(s_mutex);
		if (s_running)
			return true;

		Result rc = audrenInitialize(&RENDERER_CONFIG);
		if (R_FAILED(rc))
		{
			HorizonDiagnostics::Log("audio: audrenInitialize failed (%#x)", rc);
			return false;
		}

		rc = audrvCreate(&s_driver, &RENDERER_CONFIG, OUTPUT_CHANNELS);
		if (R_FAILED(rc))
		{
			HorizonDiagnostics::Log("audio: audrvCreate failed (%#x)", rc);
			audrenExit();
			return false;
		}

		static constexpr std::array<u8, OUTPUT_CHANNELS> SINK_CHANNELS{0, 1};
		if (audrvDeviceSinkAdd(&s_driver, AUDREN_DEFAULT_DEVICE_NAME, OUTPUT_CHANNELS, SINK_CHANNELS.data()) < 0)
			HorizonDiagnostics::Log("audio: could not add the output sink");
		rc = audrvUpdate(&s_driver);
		if (R_SUCCEEDED(rc))
			rc = audrenStartAudioRenderer();
		if (R_FAILED(rc))
		{
			HorizonDiagnostics::Log("audio: could not start the renderer (%#x)", rc);
			audrvClose(&s_driver);
			audrenExit();
			return false;
		}
		s_running = true;
		HorizonDiagnostics::Log("audio: renderer started, %d voice channels", RENDERER_CONFIG.num_voices);
		return true;
	}

	void Shutdown()
	{
		std::scoped_lock lock(s_mutex);
		if (!s_running)
			return;
		if (!s_streams.empty())
			HorizonDiagnostics::Log("audio: %zu streams still open at shutdown", s_streams.size());
		for (Stream* stream : s_streams)
			DestroyStream(stream);
		s_streams.clear();
		if (s_updateFailures != 0)
			HorizonDiagnostics::Log("audio: %u renderer updates failed", s_updateFailures);
		audrenStopAudioRenderer();
		audrvClose(&s_driver);
		audrenExit();
		s_running = false;
		s_updateFailures = 0;
	}

	void Update()
	{
		{
			std::scoped_lock lock(s_mutex);
			if (!s_running)
			{
				svcSleepThread(FRAME_NS);
				return;
			}
			for (Stream* stream : s_streams)
				SubmitBlocks(*stream);
			if (R_FAILED(audrvUpdate(&s_driver)))
				s_updateFailures++;
			for (Stream* stream : s_streams)
				ReleaseBlocks(*stream);
		}
		audrenWaitFrame();
	}

	Stream* OpenStream(uint32_t sampleRate, uint32_t channels, uint32_t framesPerBlock, uint32_t blockCount)
	{
		const StereoFactors* downmix = GetDownmix(channels);
		if (!downmix || framesPerBlock == 0 || blockCount == 0)
		{
			HorizonDiagnostics::Log("audio: unsupported stream of %u channels, %u frames", channels, framesPerBlock);
			return nullptr;
		}

		std::scoped_lock lock(s_mutex);
		if (!s_running)
			return nullptr;

		auto* stream = new Stream();
		stream->blockBytes = framesPerBlock * channels * sizeof(int16_t);
		stream->blockCount = blockCount;
		const size_t slotBytes = AlignUp(stream->blockBytes, AUDREN_BUFFER_ALIGNMENT);
		const size_t memorySize = AlignUp(slotBytes * blockCount, AUDREN_MEMPOOL_ALIGNMENT);
		stream->memory = static_cast<uint8_t*>(std::aligned_alloc(AUDREN_MEMPOOL_ALIGNMENT, memorySize));
		stream->waveBufs = std::make_unique<AudioDriverWaveBuf[]>(blockCount);
		if (!stream->memory)
		{
			HorizonDiagnostics::Log("audio: could not allocate %zu bytes", memorySize);
			DestroyStream(stream);
			return nullptr;
		}
		std::memset(stream->memory, 0, memorySize);
		armDCacheFlush(stream->memory, memorySize);
		for (uint32_t i = 0; i < blockCount; i++)
		{
			AudioDriverWaveBuf& waveBuf = stream->waveBufs[i];
			waveBuf = {};
			waveBuf.data_raw = stream->memory + i * slotBytes;
			waveBuf.size = stream->blockBytes;
			waveBuf.end_sample_offset = static_cast<s32>(framesPerBlock);
		}

		stream->memPool = audrvMemPoolAdd(&s_driver, stream->memory, memorySize);
		if (stream->memPool < 0 || !audrvMemPoolAttach(&s_driver, stream->memPool))
		{
			HorizonDiagnostics::Log("audio: could not add a memory pool");
			DestroyStream(stream);
			return nullptr;
		}

		const int voice = FindFreeVoice();
		if (voice < 0 || !audrvVoiceInit(&s_driver, voice, static_cast<int>(channels), PcmFormat_Int16, static_cast<int>(sampleRate)))
		{
			HorizonDiagnostics::Log("audio: no voice left for %u channels", channels);
			DestroyStream(stream);
			return nullptr;
		}
		stream->voice = voice;
		audrvVoiceSetDestinationMix(&s_driver, voice, AUDREN_FINAL_MIX_ID);
		for (uint32_t channel = 0; channel < channels; channel++)
		{
			audrvVoiceSetMixFactor(&s_driver, voice, downmix[channel].left, static_cast<int>(channel), 0);
			audrvVoiceSetMixFactor(&s_driver, voice, downmix[channel].right, static_cast<int>(channel), 1);
		}
		audrvVoiceSetPaused(&s_driver, voice, true);
		audrvUpdate(&s_driver);

		s_streams.push_back(stream);
		HorizonDiagnostics::Log("audio: voice %d opened, %u Hz, %u channels, %u blocks of %u frames", voice, sampleRate,
			channels, blockCount, framesPerBlock);
		return stream;
	}

	void CloseStream(Stream* stream)
	{
		if (!stream)
			return;
		std::scoped_lock lock(s_mutex);
		std::erase(s_streams, stream);
		HorizonDiagnostics::Log("audio: voice %d closed, ran dry %u times", stream->voice, stream->underruns);
		DestroyStream(stream);
	}

	bool QueueBlock(Stream* stream, const int16_t* samples)
	{
		std::scoped_lock lock(stream->producerMutex);
		const uint32_t written = stream->written.load(std::memory_order_relaxed);
		if (written - stream->released.load(std::memory_order_acquire) >= stream->blockCount)
			return false;
		void* const destination = const_cast<void*>(stream->waveBufs[written % stream->blockCount].data_raw);
		std::memcpy(destination, samples, stream->blockBytes);
		armDCacheFlush(destination, stream->blockBytes);
		stream->written.store(written + 1, std::memory_order_release);
		return true;
	}

	uint32_t GetQueuedBlocks(const Stream* stream)
	{
		return stream->written.load(std::memory_order_acquire) - stream->released.load(std::memory_order_acquire);
	}

	void SetPlaying(Stream* stream, bool playing)
	{
		stream->playing.store(playing, std::memory_order_relaxed);
	}

	void SetVolume(Stream* stream, float volume)
	{
		stream->volume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
	}
}
