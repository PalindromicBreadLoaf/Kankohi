// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "switch/PerfMonitorSwitch.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Espresso/Recompiler/PPCRecompiler.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/LatteTiming.h"
#include "Cafe/OS/RPL/rpl_symbol_storage.h"
#include "Common/switch/HorizonApplet.h"
#include "Common/switch/HorizonDiagnostics.h"
#include "Common/switch/HorizonSampler.h"
#include "config/ActiveSettings.h"
#include "config/LaunchSettings.h"
#include "switch/EmulationSwitch.h"

#include <ctime>
#include <numeric>

namespace
{
	constexpr double PERF_LINE_SECONDS = 10.0;
	constexpr double MAX_UPDATE_GAP_SECONDS = 2.0;
	constexpr double BOOT_TIMEOUT_SECONDS = 300.0;
	constexpr double MAX_RECORDED_FPS = 240.0;
	constexpr double NS_PER_MS = 1e6;

	constexpr const char* CSV_HEADER =
		"run_id,label,title_id,title,build,docked,cpu_mhz,gpu_mhz,emc_mhz,cpu_mode,warmup_s,duration_s,"
		"frames,fps,speed_pct,low1_fps,low01_fps,ft_avg_ms,ft_p50_ms,ft_p95_ms,ft_p99_ms,ft_max_ms,"
		"guest0_pct,guest1_pct,guest2_pct,latte_pct,recompiler_pct,compilers_pct,audio_pct,host_gpu_pct,host_gpu_ms,"
		"idle_ms,fence_ms,host_gpu_wait_ms,present_ms,async_ms,shader_ms,settings";

	struct ThreadTicks
	{
		uint32 handle;
		std::string name;
		uint64 ticks;
	};

	struct Snapshot
	{
		uint64 ticks = 0;
		uint64 frames = 0;
		uint64 vsyncs = 0;
		uint64 hostGPUBusyNs = 0;
		uint64 idle = 0;
		uint64 fence = 0;
		uint64 hostGPUWait = 0;
		uint64 present = 0;
		uint64 async = 0;
		uint64 shader = 0;
		std::vector<ThreadTicks> threads;
	};

	struct Interval
	{
		double seconds = 0.0;
		uint64 frames = 0;
		double fps = 0.0;
		double speed = 0.0;
		std::array<double, 3> guest{};
		double latte = 0.0;
		double recompiler = 0.0;
		double compilers = 0.0;
		double audio = 0.0;
		double hostGPU = 0.0;
		double hostGPUMs = 0.0;
		double idleMs = 0.0;
		double fenceMs = 0.0;
		double hostGPUWaitMs = 0.0;
		double presentMs = 0.0;
		double asyncMs = 0.0;
		double shaderMs = 0.0;
	};

	enum class BenchmarkState
	{
		Off,
		Booting,
		WarmingUp,
		Recording,
		Finished,
	};

	std::atomic<uint64> s_frames = 0;
	std::atomic<uint64> s_vsyncs = 0;
	std::atomic_bool s_recordFrames = false;
	std::atomic<size_t> s_recordedFrames = 0;
	std::vector<uint64> s_frameTicks;

	bool s_started = false;
	std::optional<Snapshot> s_lineStart;
	uint64 s_lastUpdate = 0;
	uint32 s_lastResumeCount = 0;
	BenchmarkState s_benchmarkState = BenchmarkState::Off;
	uint64 s_benchmarkStateStart = 0;
	Snapshot s_benchmarkStart;

	double TicksToSeconds(uint64 ticks)
	{
		return (double)ticks / (double)HorizonDiagnostics::GetTickFrequency();
	}

	void OnFrameEnd()
	{
		uint32 swapInterval = 1;
		if (LatteGPUState.sharedArea)
			swapInterval = std::max((uint32)LatteGPUState.sharedArea->swapInterval, (uint32)1);
		s_vsyncs.fetch_add(swapInterval, std::memory_order_relaxed);
		s_frames.fetch_add(1, std::memory_order_relaxed);
		if (!s_recordFrames.load(std::memory_order_acquire))
			return;
		const size_t index = s_recordedFrames.load(std::memory_order_relaxed);
		if (index >= s_frameTicks.size())
			return;
		s_frameTicks[index] = HorizonDiagnostics::GetSystemTicks();
		s_recordedFrames.store(index + 1, std::memory_order_release);
	}

	Snapshot TakeSnapshot()
	{
		Snapshot snapshot;
		snapshot.ticks = HorizonDiagnostics::GetSystemTicks();
		snapshot.frames = s_frames.load(std::memory_order_relaxed);
		snapshot.vsyncs = s_vsyncs.load(std::memory_order_relaxed);
		snapshot.hostGPUBusyNs = performanceMonitor.hostGPUBusyNs.load(std::memory_order_relaxed);
		snapshot.idle = performanceMonitor.gpuTime_idleTime.getTotal();
		snapshot.fence = performanceMonitor.gpuTime_fenceTime.getTotal();
		snapshot.hostGPUWait = performanceMonitor.gpuTime_waitForHostGPU.getTotal();
		snapshot.present = performanceMonitor.gpuTime_present.getTotal();
		snapshot.async = performanceMonitor.gpuTime_waitForAsync.getTotal();
		snapshot.shader = performanceMonitor.gpuTime_shaderCreate.getTotal();
		for (const HorizonDiagnostics::ThreadInfo& thread : HorizonDiagnostics::GetThreads())
		{
			const uint64 ticks = HorizonDiagnostics::GetThreadTicks(thread.handle);
			if (ticks != UINT64_MAX)
				snapshot.threads.push_back({thread.handle, thread.name, ticks});
		}
		return snapshot;
	}

	double GetVsyncFrequency()
	{
		sint32 customFrequency;
		if (LatteTiming_getCustomVsyncFrequency(customFrequency))
			return customFrequency;
		return 60.0 * 1.002;
	}

	Interval Measure(const Snapshot& start, const Snapshot& end)
	{
		Interval interval;
		interval.seconds = TicksToSeconds(end.ticks - start.ticks);
		interval.frames = end.frames - start.frames;
		if (interval.seconds <= 0.0)
			return interval;
		interval.fps = (double)interval.frames / interval.seconds;
		interval.speed = 100.0 * (double)(end.vsyncs - start.vsyncs) / (interval.seconds * GetVsyncFrequency());
		interval.hostGPU = (double)(end.hostGPUBusyNs - start.hostGPUBusyNs) / (interval.seconds * 1e9) * 100.0;

		for (const ThreadTicks& thread : end.threads)
		{
			uint64 startTicks = 0;
			for (const ThreadTicks& previous : start.threads)
			{
				if (previous.handle == thread.handle && previous.ticks <= thread.ticks)
					startTicks = previous.ticks;
			}
			const double usage = 100.0 * TicksToSeconds(thread.ticks - startTicks) / interval.seconds;
			for (size_t core = 0; core < interval.guest.size(); core++)
			{
				if (thread.name == fmt::format("OSSched[core={}]", core))
					interval.guest[core] += usage;
			}
			if (thread.name == "LatteThread")
				interval.latte += usage;
			else if (thread.name == "PPCRecompiler")
				interval.recompiler += usage;
			else if (thread.name == "vkShaderComp" || thread.name == "compilePl" || thread.name == "plCacheCompiler")
				interval.compilers += usage;
			else if (thread.name == "audren")
				interval.audio += usage;
		}

		if (interval.frames == 0)
			return interval;
		const auto msPerFrame = [&](uint64 startTicks, uint64 endTicks) {
			return TicksToSeconds(endTicks - startTicks) * 1000.0 / (double)interval.frames;
		};
		interval.hostGPUMs = (double)(end.hostGPUBusyNs - start.hostGPUBusyNs) / NS_PER_MS / (double)interval.frames;
		interval.idleMs = msPerFrame(start.idle, end.idle);
		interval.fenceMs = msPerFrame(start.fence, end.fence);
		interval.hostGPUWaitMs = msPerFrame(start.hostGPUWait, end.hostGPUWait);
		interval.presentMs = msPerFrame(start.present, end.present);
		interval.asyncMs = msPerFrame(start.async, end.async);
		interval.shaderMs = msPerFrame(start.shader, end.shader);
		return interval;
	}

	void UpdatePerfLine()
	{
		if (CafeSystem::IsTitlePaused())
		{
			s_lineStart.reset();
			return;
		}
		if (s_lineStart && TicksToSeconds(HorizonDiagnostics::GetSystemTicks() - s_lineStart->ticks) < PERF_LINE_SECONDS)
			return;
		Snapshot now = TakeSnapshot();
		if (s_lineStart)
		{
			const Interval i = Measure(*s_lineStart, now);
			cemuLog_log(LogType::Force, "Perf: {:.2f} fps, speed {:.1f}%, frame {:.2f} ms (idle {:.2f}, fence {:.2f}, host GPU wait {:.2f}, present {:.2f}, async {:.2f}, shaders {:.2f}), host GPU {:.2f} ms {:.1f}%, guest {:.0f}/{:.0f}/{:.0f}%, Latte {:.0f}%, recompiler {:.0f}%, compilers {:.0f}%, audio {:.0f}%",
				i.fps, i.speed, i.fps > 0.0 ? 1000.0 / i.fps : 0.0, i.idleMs, i.fenceMs, i.hostGPUWaitMs, i.presentMs, i.asyncMs, i.shaderMs,
				i.hostGPUMs, i.hostGPU, i.guest[0], i.guest[1], i.guest[2], i.latte, i.recompiler, i.compilers, i.audio);
		}
		s_lineStart = std::move(now);
	}

	std::string LogPath(const std::string& name)
	{
		return fmt::format("{}/logs/{}", EmulationSwitch::USER_DIRECTORY, name);
	}

	std::string CsvField(std::string text)
	{
		for (char& c : text)
		{
			if (c == ',' || c == '"' || c == '\n' || c == '\r')
				c = ' ';
		}
		return text;
	}

	std::string DescribeOverrides()
	{
		std::string text;
		for (const LaunchSettings::SettingOverride& setting : LaunchSettings::GetSettingOverrides())
		{
			if (!text.empty())
				text += ';';
			text += fmt::format("{}/{}={}", setting.section, setting.key, setting.value);
		}
		return text;
	}

	bool AppendCsvRow(const std::string& row, long long runId)
	{
		const std::string path = LogPath("bench.csv");
		bool writeHeader = true;
		if (std::ifstream existing(path); existing)
		{
			std::string header;
			std::getline(existing, header);
			writeHeader = header != CSV_HEADER;
		}
		std::error_code ec;
		if (writeHeader && fs::exists(path, ec))
			fs::rename(path, LogPath(fmt::format("bench-{}.old.csv", runId)), ec);

		std::ofstream file(path, std::ios::app);
		if (writeHeader)
			file << CSV_HEADER << '\n';
		file << row << '\n';
		file.close();
		return !file.fail();
	}

	void WriteBenchmark(const LaunchSettings::Benchmark& benchmark)
	{
		const Interval i = Measure(s_benchmarkStart, TakeSnapshot());
		const size_t tickCount = s_recordedFrames.load(std::memory_order_acquire);
		std::vector<double> frameTimes;
		for (size_t frame = 1; frame < tickCount; frame++)
			frameTimes.push_back(TicksToSeconds(s_frameTicks[frame] - s_frameTicks[frame - 1]) * 1000.0);
		std::vector<double> sorted = frameTimes;
		std::sort(sorted.begin(), sorted.end());
		const auto percentile = [&sorted](double p) {
			if (sorted.empty())
				return 0.0;
			return sorted[std::min(sorted.size() - 1, (size_t)(p * (double)(sorted.size() - 1) + 0.5))];
		};
		const auto toFps = [](double ms) { return ms > 0.0 ? 1000.0 / ms : 0.0; };
		const double average = frameTimes.empty() ? 0.0 : std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0) / (double)frameTimes.size();

		const long long runId = (long long)std::time(nullptr);
		const HorizonDiagnostics::Clocks clocks = HorizonDiagnostics::ReadClocks();
		std::string row = fmt::format("{},{},{:016x},{},{},{},{},{},{},{},{:.0f},{:.0f},",
			runId, CsvField(benchmark.label), CafeSystem::GetForegroundTitleId(), CsvField(CafeSystem::GetForegroundTitleName()),
			HorizonDiagnostics::GetBuildId(), HorizonApplet::IsDocked() ? 1 : 0, clocks.cpuMHz, clocks.gpuMHz, clocks.memoryMHz,
			fmt::underlying(ActiveSettings::GetCPUMode()), benchmark.warmupSeconds, i.seconds);
		row += fmt::format("{},{:.2f},{:.1f},{:.2f},{:.2f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},",
			i.frames, i.fps, i.speed, toFps(percentile(0.99)), toFps(percentile(0.999)), average,
			percentile(0.50), percentile(0.95), percentile(0.99), sorted.empty() ? 0.0 : sorted.back());
		row += fmt::format("{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.3f},",
			i.guest[0], i.guest[1], i.guest[2], i.latte, i.recompiler, i.compilers, i.audio, i.hostGPU, i.hostGPUMs);
		row += fmt::format("{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{}",
			i.idleMs, i.fenceMs, i.hostGPUWaitMs, i.presentMs, i.asyncMs, i.shaderMs, CsvField(DescribeOverrides()));
		if (!AppendCsvRow(row, runId))
			cemuLog_log(LogType::Force, "Benchmark: Failed to write the result");

		std::ofstream frames(LogPath(fmt::format("bench-{}.frames.txt", runId)));
		for (const double frameTime : frameTimes)
			frames << fmt::format("{:.3f}\n", frameTime);
		cemuLog_log(LogType::Force, "Benchmark {} ({}): {} frames, {:.2f} fps, speed {:.1f}%, 1% low {:.2f} fps", runId,
			benchmark.label, i.frames, i.fps, i.speed, toFps(percentile(0.99)));
	}

	void EnterBenchmarkState(BenchmarkState state)
	{
		s_benchmarkState = state;
		s_benchmarkStateStart = HorizonDiagnostics::GetSystemTicks();
	}

	void AbortBenchmark(const char* reason)
	{
		cemuLog_log(LogType::Force, "Benchmark: {}, no result written", reason);
		s_recordFrames = false;
		EnterBenchmarkState(BenchmarkState::Off);
	}

	void UpdateBenchmark()
	{
		if (s_benchmarkState == BenchmarkState::Off || s_benchmarkState == BenchmarkState::Finished)
			return;
		const LaunchSettings::Benchmark& benchmark = *LaunchSettings::GetBenchmark();
		const double seconds = TicksToSeconds(HorizonDiagnostics::GetSystemTicks() - s_benchmarkStateStart);
		if (s_benchmarkState != BenchmarkState::Booting && CafeSystem::IsTitlePaused())
		{
			AbortBenchmark("The title was paused");
			return;
		}
		switch (s_benchmarkState)
		{
		case BenchmarkState::Booting:
			if (s_frames.load() != 0)
			{
				cemuLog_log(LogType::Force, "Benchmark: Warming up for {:.0f} s", benchmark.warmupSeconds);
				EnterBenchmarkState(BenchmarkState::WarmingUp);
			}
			else if (seconds >= BOOT_TIMEOUT_SECONDS)
			{
				cemuLog_log(LogType::Force, "Benchmark: The title didn't present a frame within {:.0f} s", BOOT_TIMEOUT_SECONDS);
				EnterBenchmarkState(BenchmarkState::Finished);
			}
			break;
		case BenchmarkState::WarmingUp:
			if (seconds < benchmark.warmupSeconds)
				break;
			cemuLog_log(LogType::Force, "Benchmark: Recording for {:.0f} s", benchmark.seconds);
			s_frameTicks.assign((size_t)(benchmark.seconds * MAX_RECORDED_FPS) + 1, 0);
			s_recordedFrames = 0;
			s_benchmarkStart = TakeSnapshot();
			s_recordFrames.store(true, std::memory_order_release);
			EnterBenchmarkState(BenchmarkState::Recording);
			break;
		case BenchmarkState::Recording:
			if (seconds < benchmark.seconds)
				break;
			s_recordFrames = false;
			WriteBenchmark(benchmark);
			EnterBenchmarkState(BenchmarkState::Finished);
			break;
		default:
			break;
		}
	}

	void LogBattery()
	{
		const HorizonDiagnostics::Battery battery = HorizonDiagnostics::ReadBattery();
		cemuLog_log(LogType::Force, "Battery: {}%, {}{}", battery.percentage, battery.charging ? "on charger" : "discharging",
			battery.voltageLow ? ", voltage too low for full performance" : "");
	}

	double GetProcessCpuSeconds(const Snapshot& start, const Snapshot& end)
	{
		uint64 ticks = 0;
		for (const ThreadTicks& thread : end.threads)
		{
			ticks += thread.ticks;
			for (const ThreadTicks& previous : start.threads)
			{
				if (previous.handle == thread.handle && previous.ticks <= thread.ticks)
					ticks -= previous.ticks;
			}
		}
		return TicksToSeconds(ticks);
	}

	void CheckForInterruption()
	{
		const uint64 now = HorizonDiagnostics::GetSystemTicks();
		const uint32 resumeCount = HorizonApplet::GetResumeCount();
		const double gap = s_lastUpdate != 0 ? TicksToSeconds(now - s_lastUpdate) : 0.0;
		const bool resumed = resumeCount != s_lastResumeCount;
		s_lastUpdate = now;
		s_lastResumeCount = resumeCount;
		if (gap < MAX_UPDATE_GAP_SECONDS && !resumed)
			return;

		const double cpuSeconds = s_lineStart ? GetProcessCpuSeconds(*s_lineStart, TakeSnapshot()) : 0.0;
		cemuLog_log(LogType::Force, "Perf: Interrupted for {:.1f} s ({}, {:.1f} s of CPU time since the last line)", gap,
			resumed ? "resumed by the system" : "not suspended by the system", cpuSeconds);
		LogBattery();
		s_lineStart.reset();
		if (s_benchmarkState == BenchmarkState::WarmingUp || s_benchmarkState == BenchmarkState::Recording)
			AbortBenchmark("The run was interrupted");
	}

	std::string NameGuestFunction(uint32 address)
	{
		std::unordered_map<uint32, RPLStoredSymbol*>& symbols = rplSymbolStorage_lockSymbolMap();
		std::string name;
		for (uint32 offset = 0; offset < 0x4000 && offset <= address; offset += 4)
		{
			const auto it = symbols.find(address - offset);
			if (it == symbols.end() || !it->second)
				continue;
			name = fmt::format("{}.{}", (const char*)it->second->libName, (const char*)it->second->symbolName);
			if (offset != 0)
				name += fmt::format("+0x{:x}", offset);
			break;
		}
		rplSymbolStorage_unlockSymbolMap();
		return name;
	}

	HorizonSampler::Counters ReadSamplerCounters()
	{
		return {s_frames.load(std::memory_order_relaxed), performanceMonitor.hostGPUBusyNs.load(std::memory_order_relaxed)};
	}

	std::vector<HorizonSampler::GuestFunction> ListGuestFunctions()
	{
		std::vector<HorizonSampler::GuestFunction> functions;
		for (const PPCRecompilerHostRange& range : PPCRecompiler_GetHostRanges())
			functions.push_back({range.ppcAddress, range.hostAddress, range.hostSize});
		return functions;
	}
}

namespace PerfMonitor
{
	void Start()
	{
		if (s_started)
			return;
		s_started = true;
		s_frames = 0;
		s_vsyncs = 0;
		s_lineStart.reset();
		s_lastUpdate = 0;
		s_lastResumeCount = HorizonApplet::GetResumeCount();
		LogBattery();
		performanceMonitor.measureHostGPUTime = true;
		LattePerformanceMonitor_setFrameCallback(OnFrameEnd);
		EnterBenchmarkState(LaunchSettings::GetBenchmark() ? BenchmarkState::Booting : BenchmarkState::Off);
	}

	void Update()
	{
		CheckForInterruption();
		UpdatePerfLine();
		UpdateBenchmark();
	}

	void Stop()
	{
		if (!s_started)
			return;
		HorizonSampler::Stop();
		s_recordFrames = false;
		LattePerformanceMonitor_setFrameCallback(nullptr);
		s_started = false;
	}

	uint64_t GetFrameCount()
	{
		return s_frames.load(std::memory_order_relaxed);
	}

	bool IsBenchmarkFinished()
	{
		return s_benchmarkState == BenchmarkState::Finished;
	}

	bool StartProfile(double seconds)
	{
		const std::time_t now = std::time(nullptr);
		const std::tm time = *std::localtime(&now);
		const std::string stamp = fmt::format("{:04}{:02}{:02}-{:02}{:02}{:02}", time.tm_year + 1900, time.tm_mon + 1,
			time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);

		HorizonSampler::Options options;
		options.seconds = seconds;
		options.path = LogPath(fmt::format("profile-{:016x}-{}.txt", CafeSystem::GetForegroundTitleId(), stamp));
		options.description = fmt::format("title {:016x} {}\n{}, CPU mode {}\nsettings: {}\n",
			CafeSystem::GetForegroundTitleId(), CsvField(CafeSystem::GetForegroundTitleName()),
			HorizonApplet::IsDocked() ? "docked" : "handheld", ActiveSettings::GetCPUMode(), DescribeOverrides());
		std::tie(options.codeStart, options.codeEnd) = PPCRecompiler_GetHostCodeArea();
		options.readCounters = ReadSamplerCounters;
		options.listFunctions = ListGuestFunctions;
		options.nameFunction = NameGuestFunction;
		return HorizonSampler::Start(std::move(options));
	}

	bool IsProfiling()
	{
		return HorizonSampler::IsRunning();
	}
}
