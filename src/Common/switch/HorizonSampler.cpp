// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonSampler.h"
#include "HorizonDiagnostics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <thread>
#include <unordered_map>
#include <utility>

#include <switch.h>

extern "C" void _start();
extern "C" char __end__[];

namespace
{
	constexpr uint64_t SAMPLE_PERIOD_NS = 2'000'000;
	constexpr uint64_t BUCKET_NS = 250'000'000;
	constexpr int PAUSE_RETRIES = 8;
	constexpr uint64_t PAUSE_RETRY_NS = 1'000;

#if defined(KANKOHI_FRAME_POINTERS)
	constexpr bool FRAME_POINTERS = true;
#else
	constexpr bool FRAME_POINTERS = false;
#endif

	constexpr size_t MAX_THREADS = 64;
	constexpr size_t MAX_FRAMES = FRAME_POINTERS ? 16 : 1;
	constexpr size_t MAX_SAMPLES = 512 * 1024;
	constexpr size_t MIN_SAMPLES = 4 * 1024;
	constexpr size_t MAX_STACK_SIZE = 2 * 1024 * 1024;
	constexpr size_t MAX_LISTED_FUNCTIONS = 60;
	constexpr size_t MAX_DISASSEMBLED_FUNCTIONS = 24;
	constexpr size_t MAX_DISASSEMBLED_INSTRUCTIONS = 2048;
	constexpr uint64_t NO_TICKS = UINT64_MAX;

	struct Sample
	{
		uint64_t pc;
		uint64_t lr;
		std::array<uint64_t, MAX_FRAMES> frames;
		uint16_t svc;
		uint8_t thread;
		uint8_t depth;
		bool blocked;
	};

	struct ThreadRecord
	{
		HorizonDiagnostics::ThreadInfo info{};
		bool gone = false;
		uint64_t failures = 0;
		uint64_t firstTicks = NO_TICKS;
		uint64_t firstWall = 0;
		uint64_t lastTicks = NO_TICKS;
		uint64_t lastWall = 0;
	};

	struct Bucket
	{
		uint64_t wallTicks = 0;
		HorizonSampler::Counters counters;
		std::array<uint64_t, MAX_THREADS> threadTicks{};
	};

	struct HotFunction
	{
		HorizonSampler::GuestFunction function{};
		uint64_t self = 0;
		uint64_t inclusive = 0;
		std::map<uint32_t, uint64_t> instructions;
	};

	struct Capture
	{
		HorizonSampler::Options options;
		uintptr_t moduleStart = 0;
		uintptr_t moduleEnd = 0;
		std::array<ThreadRecord, MAX_THREADS> threads{};
		size_t threadCount = 0;
		Handle samplerHandle = INVALID_HANDLE;
		std::unique_ptr<Sample[]> samples;
		size_t sampleCapacity = 0;
		size_t sampleCount = 0;
		uint64_t droppedSamples = 0;
		uint64_t rounds = 0;
		std::vector<Bucket> buckets;
		uint64_t samplerTicks = NO_TICKS;
		Result firstError = 0;
		double elapsed = 0.0;
		std::vector<HorizonSampler::GuestFunction> functions;
		std::vector<HotFunction> hotFunctions;
		std::unordered_map<uintptr_t, size_t> hotIndices;
	};

	std::atomic_bool s_running = false;
	std::atomic_bool s_stopRequested = false;
	std::mutex s_lifecycleMutex;
	std::thread s_thread;
	Capture s_capture;

	__attribute__((format(printf, 2, 3))) void Append(std::string& text, const char* format, ...)
	{
		char line[512];
		va_list args;
		va_start(args, format);
		const int length = std::vsnprintf(line, sizeof(line), format, args);
		va_end(args);
		if (length > 0)
			text.append(line, std::min<size_t>(length, sizeof(line) - 1));
	}

	bool InModule(uint64_t address)
	{
		return address >= s_capture.moduleStart && address < s_capture.moduleEnd;
	}

	bool InCodeArena(uint64_t address)
	{
		return address >= s_capture.options.codeStart && address < s_capture.options.codeEnd;
	}

	bool IsInSyscall(uint64_t pc, uint16_t& svc)
	{
		if (pc % 4 != 0 || !InModule(pc))
			return false;
		uint32_t instruction;
		std::memcpy(&instruction, (const void*)pc, sizeof(instruction));
		if ((instruction & 0xffe0001f) != 0xd4000001)
			return false;
		svc = (uint16_t)((instruction >> 5) & 0xffff);
		return true;
	}

	uint8_t WalkFrames(const ThreadContext& context, std::array<uint64_t, MAX_FRAMES>& frames)
	{
		if constexpr (!FRAME_POINTERS)
			return 0;

		MemoryInfo info{};
		u32 pageInfo;
		if (R_FAILED(svcQueryMemory(&info, &pageInfo, context.sp)) || (info.perm & Perm_R) == 0)
			return 0;
		const uint64_t stackEnd = std::min<uint64_t>(info.addr + info.size, context.sp + MAX_STACK_SIZE);

		uint64_t framePointer = context.fp;
		uint64_t lowest = context.sp;
		uint8_t depth = 0;
		while (depth < MAX_FRAMES)
		{
			if (framePointer % 16 != 0 || framePointer < lowest || framePointer > stackEnd - 16)
				break;
			uint64_t record[2];
			std::memcpy(record, (const void*)framePointer, sizeof(record));
			const bool module = InModule(record[1]);
			if (!module && !InCodeArena(record[1]))
				break;
			frames[depth++] = record[1];
			if (!module)
				break;
			lowest = framePointer + 1;
			framePointer = record[0];
		}
		return depth;
	}

	void NoteFailure(Result result)
	{
		if (s_capture.firstError == 0)
			s_capture.firstError = result;
	}

	void TakeSample(size_t index)
	{
		ThreadRecord& thread = s_capture.threads[index];
		const Handle handle = thread.info.handle;

		const Result pauseResult = svcSetThreadActivity(handle, ThreadActivity_Paused);
		if (R_FAILED(pauseResult))
		{
			thread.failures++;
			if (R_VALUE(pauseResult) == KERNELRESULT(InvalidHandle) || R_SUCCEEDED(svcWaitSynchronizationSingle(handle, 0)))
				thread.gone = true;
			else
				NoteFailure(pauseResult);
			return;
		}

		ThreadContext context;
		Result result = 0;
		for (int attempt = 0; attempt < PAUSE_RETRIES; attempt++)
		{
			result = svcGetThreadContext3(&context, handle);
			if (R_SUCCEEDED(result))
				break;
			svcSleepThread(PAUSE_RETRY_NS);
		}

		Sample* const sample =
			s_capture.sampleCount < s_capture.sampleCapacity ? &s_capture.samples[s_capture.sampleCount] : nullptr;
		if (R_SUCCEEDED(result) && sample)
		{
			sample->pc = context.pc.x;
			sample->lr = context.lr;
			sample->thread = (uint8_t)index;
			sample->svc = 0;
			sample->blocked = IsInSyscall(context.pc.x, sample->svc);
			sample->depth = WalkFrames(context, sample->frames);
		}

		svcSetThreadActivity(handle, ThreadActivity_Runnable);

		if (R_FAILED(result))
		{
			thread.failures++;
			NoteFailure(result);
			return;
		}
		if (sample)
			s_capture.sampleCount++;
		else
			s_capture.droppedSamples++;
	}

	void RefreshThreads()
	{
		for (const HorizonDiagnostics::ThreadInfo& info : HorizonDiagnostics::GetThreads())
		{
			if (info.handle == s_capture.samplerHandle)
				continue;
			const auto end = s_capture.threads.begin() + s_capture.threadCount;
			auto record = std::find_if(s_capture.threads.begin(), end,
									   [&](const ThreadRecord& r) { return r.info.handle == info.handle; });
			if (record == end)
			{
				if (s_capture.threadCount == s_capture.threads.size())
					continue;
				s_capture.threadCount++;
			}
			record->info = info;
		}
	}

	void RecordBucket()
	{
		if (s_capture.buckets.size() == s_capture.buckets.capacity())
			return;
		Bucket& bucket = s_capture.buckets.emplace_back();
		bucket.wallTicks = armGetSystemTick();
		if (s_capture.options.readCounters)
			bucket.counters = s_capture.options.readCounters();
		bucket.threadTicks.fill(NO_TICKS);
		for (size_t i = 0; i < s_capture.threadCount; i++)
		{
			ThreadRecord& thread = s_capture.threads[i];
			const uint64_t ticks = HorizonDiagnostics::GetThreadTicks(thread.info.handle);
			bucket.threadTicks[i] = ticks;
			if (ticks == NO_TICKS)
				continue;
			if (thread.firstTicks == NO_TICKS)
			{
				thread.firstTicks = ticks;
				thread.firstWall = bucket.wallTicks;
			}
			thread.lastTicks = ticks;
			thread.lastWall = bucket.wallTicks;
		}
	}

	bool KeepLinkRegister(const Sample& sample)
	{
		return !InCodeArena(sample.pc) && (InModule(sample.lr) || InCodeArena(sample.lr));
	}

	const HorizonSampler::GuestFunction* FindFunction(uint64_t address)
	{
		const auto after =
			std::upper_bound(s_capture.functions.begin(), s_capture.functions.end(), address,
							 [](uint64_t a, const HorizonSampler::GuestFunction& f) { return a < f.hostAddress; });
		if (after == s_capture.functions.begin())
			return nullptr;
		const HorizonSampler::GuestFunction& function = *std::prev(after);
		return address < function.hostAddress + function.hostSize ? &function : nullptr;
	}

	void ResolveFunctions()
	{
		if (!s_capture.options.listFunctions)
			return;
		s_capture.functions = s_capture.options.listFunctions();
		std::sort(s_capture.functions.begin(), s_capture.functions.end(),
				  [](const auto& a, const auto& b) { return a.hostAddress < b.hostAddress; });

		for (size_t i = 0; i < s_capture.sampleCount; i++)
		{
			const Sample& sample = s_capture.samples[i];
			std::set<size_t> onStack;
			const auto note = [&](uint64_t address, bool self) {
				if (!InCodeArena(address))
					return;
				const HorizonSampler::GuestFunction* function = FindFunction(address);
				if (!function)
					return;
				const auto [it, inserted] =
					s_capture.hotIndices.emplace(function->hostAddress, s_capture.hotFunctions.size());
				if (inserted)
					s_capture.hotFunctions.push_back({*function});
				HotFunction& hot = s_capture.hotFunctions[it->second];
				if (self)
				{
					hot.self++;
					hot.instructions[(uint32_t)(address - function->hostAddress) / 4]++;
				}
				onStack.insert(it->second);
			};
			note(sample.pc, true);
			if (KeepLinkRegister(sample))
				note(sample.lr, false);
			for (uint8_t frame = 0; frame < sample.depth; frame++)
				note(sample.frames[frame], false);
			for (const size_t index : onStack)
				s_capture.hotFunctions[index].inclusive++;
		}
	}

	std::string Label(uint64_t address)
	{
		std::string label;
		if (InModule(address))
			Append(label, "m:%lx", address - s_capture.moduleStart);
		else if (!InCodeArena(address))
			Append(label, "?:%lx", address);
		else if (const HorizonSampler::GuestFunction* function = FindFunction(address))
			Append(label, "b:%08x", function->guestAddress);
		else
			Append(label, "j:%lx", address - s_capture.options.codeStart);
		return label;
	}

	std::string FunctionName(uint32_t guestAddress)
	{
		return s_capture.options.nameFunction ? s_capture.options.nameFunction(guestAddress) : std::string();
	}

	double Percent(uint64_t part, uint64_t whole)
	{
		return whole ? 100.0 * (double)part / (double)whole : 0.0;
	}

	void AppendSummary(std::string& text)
	{
		const HorizonDiagnostics::Clocks clocks = HorizonDiagnostics::ReadClocks();
		Append(text, "Kankohi profile\nbuild id %s\n", HorizonDiagnostics::GetBuildId());
		Append(text, "module base 0x%lx size 0x%lx\n", s_capture.moduleStart, s_capture.moduleEnd - s_capture.moduleStart);
		Append(text, "clocks: CPU %u MHz, GPU %u MHz, RAM %u MHz\n", clocks.cpuMHz, clocks.gpuMHz, clocks.memoryMHz);
		text += s_capture.options.description;
		Append(text, "%.2f s, %zu samples in %lu rounds, %lu dropped, frame pointers %s\n", s_capture.elapsed,
			   s_capture.sampleCount, s_capture.rounds, s_capture.droppedSamples, FRAME_POINTERS ? "on" : "off");
		if (s_capture.firstError != 0)
			Append(text, "first sampling error %#x\n", s_capture.firstError);
		if (s_capture.buckets.size() >= 2)
		{
			const Bucket& first = s_capture.buckets.front();
			const Bucket& last = s_capture.buckets.back();
			const uint64_t wallNs = armTicksToNs(last.wallTicks - first.wallTicks);
			const uint64_t frames = last.counters.frames - first.counters.frames;
			Append(text, "frames/s %.2f, GPU busy %.1f%%\n", wallNs ? (double)frames * 1e9 / (double)wallNs : 0.0,
				   Percent(last.counters.gpuBusyNs - first.counters.gpuBusyNs, wallNs));
		}
	}

	void AppendThreads(std::string& text)
	{
		std::array<uint64_t, MAX_THREADS> total{}, blocked{};
		for (size_t i = 0; i < s_capture.sampleCount; i++)
		{
			total[s_capture.samples[i].thread]++;
			blocked[s_capture.samples[i].thread] += s_capture.samples[i].blocked;
		}
		text += "\nThreads\n  id     CPU%  blocked%  samples  failed  name\n";
		for (size_t i = 0; i < s_capture.threadCount; i++)
		{
			const ThreadRecord& thread = s_capture.threads[i];
			const double cpu = thread.firstTicks == NO_TICKS ? 0.0 : Percent(thread.lastTicks - thread.firstTicks, thread.lastWall - thread.firstWall);
			Append(text, "  T%-3zu  %5.1f     %5.1f  %7lu  %6lu  %s%s\n", i, cpu, Percent(blocked[i], total[i]), total[i],
				   thread.failures, thread.info.name, thread.gone ? " (exited)" : "");
		}
		if (s_capture.samplerTicks != NO_TICKS && s_capture.elapsed > 0.0)
			Append(text, "  profiler overhead %.1f%% of one core\n",
				   (double)armTicksToNs(s_capture.samplerTicks) / 1e7 / s_capture.elapsed);
	}

	void AppendTimeline(std::string& text)
	{
		std::vector<size_t> busyThreads;
		for (size_t i = 0; i < s_capture.threadCount; i++)
		{
			const ThreadRecord& thread = s_capture.threads[i];
			if (thread.firstTicks != NO_TICKS && Percent(thread.lastTicks - thread.firstTicks, thread.lastWall - thread.firstWall) >= 2.0)
				busyThreads.push_back(i);
		}
		text += "\nTimeline (CPU% of each thread above 2%)\n     s    fps   GPU%";
		for (const size_t i : busyThreads)
			Append(text, "  T%-4zu", i);
		text += '\n';
		for (size_t b = 1; b < s_capture.buckets.size(); b++)
		{
			const Bucket& previous = s_capture.buckets[b - 1];
			const Bucket& bucket = s_capture.buckets[b];
			const uint64_t wall = bucket.wallTicks - previous.wallTicks;
			const uint64_t wallNs = armTicksToNs(wall);
			if (wallNs == 0)
				continue;
			Append(text, "%6.2f  %5.1f  %5.1f", (double)armTicksToNs(bucket.wallTicks - s_capture.buckets[0].wallTicks) / 1e9,
				   (double)(bucket.counters.frames - previous.counters.frames) * 1e9 / (double)wallNs,
				   Percent(bucket.counters.gpuBusyNs - previous.counters.gpuBusyNs, wallNs));
			for (const size_t i : busyThreads)
			{
				if (previous.threadTicks[i] == NO_TICKS || bucket.threadTicks[i] == NO_TICKS)
					text += "      -";
				else
					Append(text, "  %5.1f", Percent(bucket.threadTicks[i] - previous.threadTicks[i], wall));
			}
			text += '\n';
		}
	}

	void AppendFunctions(std::string& text)
	{
		if (s_capture.hotFunctions.empty())
			return;
		std::sort(s_capture.hotFunctions.begin(), s_capture.hotFunctions.end(),
				  [](const HotFunction& a, const HotFunction& b) { return std::pair(a.self, a.inclusive) > std::pair(b.self, b.inclusive); });
		text += "\nHot guest functions\n     self  inclusive  address   name\n";
		for (size_t i = 0; i < std::min(s_capture.hotFunctions.size(), MAX_LISTED_FUNCTIONS); i++)
		{
			const HotFunction& hot = s_capture.hotFunctions[i];
			Append(text, "  %7lu  %9lu  %08x  %s\n", hot.self, hot.inclusive, hot.function.guestAddress,
				   FunctionName(hot.function.guestAddress).c_str());
		}
		for (size_t i = 0; i < std::min(s_capture.hotFunctions.size(), MAX_DISASSEMBLED_FUNCTIONS); i++)
		{
			const HotFunction& hot = s_capture.hotFunctions[i];
			if (hot.self == 0)
				break;
			const size_t count = std::min(hot.function.hostSize / 4, MAX_DISASSEMBLED_INSTRUCTIONS);
			Append(text, "\nBlock %08x, %zu host instructions\n", hot.function.guestAddress, count);
			for (size_t instruction = 0; instruction < count; instruction++)
			{
				uint32_t encoding;
				std::memcpy(&encoding, (const void*)(hot.function.hostAddress + instruction * 4), sizeof(encoding));
				const auto samples = hot.instructions.find((uint32_t)instruction);
				Append(text, "  +0x%04zx %6lu %08x\n", instruction * 4,
					   samples != hot.instructions.end() ? samples->second : 0, encoding);
			}
		}
	}

	void AppendStacks(std::string& text, size_t thread)
	{
		std::map<std::string, uint64_t> stacks;
		uint64_t total = 0;
		for (size_t i = 0; i < s_capture.sampleCount; i++)
		{
			const Sample& sample = s_capture.samples[i];
			if (sample.thread != thread)
				continue;
			std::string key;
			if (sample.blocked)
				Append(key, "svc:%x ", sample.svc);
			else
				key = "run ";
			key += Label(sample.pc);
			if (KeepLinkRegister(sample))
				key += " L" + Label(sample.lr);
			for (uint8_t frame = 0; frame < sample.depth; frame++)
				key += ' ' + Label(sample.frames[frame]);
			stacks[key]++;
			total++;
		}
		if (total == 0)
			return;
		std::vector<std::pair<std::string, uint64_t>> ordered(stacks.begin(), stacks.end());
		std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		Append(text, "\n== Thread T%zu: %s ==\n", thread, s_capture.threads[thread].info.name);
		for (const auto& [stack, count] : ordered)
		{
			Append(text, "  %lu ", count);
			text += stack;
			text += '\n';
		}
	}

	void WriteReport()
	{
		ResolveFunctions();
		std::string text;
		AppendSummary(text);
		AppendThreads(text);
		AppendTimeline(text);
		AppendFunctions(text);
		for (size_t i = 0; i < s_capture.threadCount; i++)
			AppendStacks(text, i);

		FILE* file = std::fopen(s_capture.options.path.c_str(), "wb");
		if (!file)
		{
			HorizonDiagnostics::Log("profile: can't write %s", s_capture.options.path.c_str());
			return;
		}
		const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
		const bool closed = std::fclose(file) == 0;
		HorizonDiagnostics::Log("profile: %s %s, %zu samples in %.1f s", written && closed ? "wrote" : "failed to write",
								s_capture.options.path.c_str(), s_capture.sampleCount, s_capture.elapsed);
	}

	void SamplerThread()
	{
		HorizonDiagnostics::SetCurrentThreadName("Profiler");
		const uint64_t lightCores = HorizonDiagnostics::GetProcessCoreMask() & ~HorizonDiagnostics::GetApplicationCoreMask();
		const uint64_t mask = lightCores ? lightCores : HorizonDiagnostics::GetApplicationCoreMask();
		HorizonDiagnostics::PlaceCurrentThread("profiler", 63 - __builtin_clzll(mask), mask, 0x2b);
		s_capture.samplerHandle = threadGetCurHandle();

		const uint64_t ownStart = HorizonDiagnostics::GetThreadTicks(s_capture.samplerHandle);
		const uint64_t start = armGetSystemTick();
		const uint64_t bucketTicks = armNsToTicks(BUCKET_NS);
		uint64_t nextBucket = start + bucketTicks;
		uint32_t random = 0x2545f491;

		RefreshThreads();
		RecordBucket();
		while (!s_stopRequested.load(std::memory_order_relaxed))
		{
			const uint64_t now = armGetSystemTick();
			s_capture.elapsed = (double)armTicksToNs(now - start) / 1e9;
			if (s_capture.elapsed >= s_capture.options.seconds)
				break;
			if (now >= nextBucket)
			{
				RefreshThreads();
				RecordBucket();
				nextBucket += bucketTicks;
			}
			s_capture.rounds++;
			for (size_t i = 0; i < s_capture.threadCount; i++)
			{
				if (!s_capture.threads[i].gone)
					TakeSample(i);
			}
			if (s_capture.sampleCount == 0 && s_capture.rounds >= 32)
				break;
			random = random * 1664525u + 1013904223u;
			svcSleepThread(SAMPLE_PERIOD_NS / 2 + random % SAMPLE_PERIOD_NS);
		}
		RecordBucket();

		const uint64_t ownEnd = HorizonDiagnostics::GetThreadTicks(s_capture.samplerHandle);
		if (ownStart != NO_TICKS && ownEnd != NO_TICKS)
			s_capture.samplerTicks = ownEnd - ownStart;
		try
		{
			WriteReport();
		}
		catch (const std::bad_alloc&)
		{
			HorizonDiagnostics::Log("profile: out of memory while writing the report");
		}
		s_capture = {};
		s_running.store(false, std::memory_order_release);
	}
}

namespace HorizonSampler
{
	bool Start(Options options)
	{
		std::lock_guard lock(s_lifecycleMutex);
		if (s_running.load(std::memory_order_acquire))
			return false;
		if (s_thread.joinable())
			s_thread.join();

		s_capture = {};
		s_capture.moduleStart = (uintptr_t)&_start;
		s_capture.moduleEnd = (uintptr_t)__end__;
		const size_t wanted = (size_t)(options.seconds * 1e9 / SAMPLE_PERIOD_NS) * MAX_THREADS;
		const size_t affordable = HorizonDiagnostics::GetFreeHeap() / 4 / sizeof(Sample);
		for (size_t capacity = std::min({wanted, affordable, MAX_SAMPLES}); capacity >= MIN_SAMPLES; capacity /= 2)
		{
			s_capture.samples.reset(new (std::nothrow) Sample[capacity]);
			if (s_capture.samples)
			{
				s_capture.sampleCapacity = capacity;
				break;
			}
		}
		if (!s_capture.samples)
		{
			HorizonDiagnostics::Log("profile: not enough memory for the samples");
			return false;
		}
		s_capture.buckets.reserve((size_t)(options.seconds * 1e9 / BUCKET_NS) + 4);
		s_capture.options = std::move(options);
		HorizonDiagnostics::Log("profile: capturing for %.1f s", s_capture.options.seconds);

		s_stopRequested.store(false);
		s_running.store(true, std::memory_order_release);
		s_thread = std::thread(SamplerThread);
		return true;
	}

	bool IsRunning()
	{
		return s_running.load(std::memory_order_acquire);
	}

	void Stop()
	{
		std::lock_guard lock(s_lifecycleMutex);
		s_stopRequested.store(true);
		if (s_thread.joinable())
			s_thread.join();
	}
}
