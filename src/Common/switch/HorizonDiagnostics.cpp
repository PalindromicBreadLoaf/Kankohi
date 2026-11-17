// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonDiagnostics.h"
#include "HorizonException.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <mutex>
#include <new>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

extern "C" void _start();
extern "C" char* fake_heap_start;
extern "C" char* fake_heap_end;

extern "C" int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg);
extern "C" int __real_pthread_join(pthread_t thread, void** value);

namespace
{
	constexpr size_t NRO_BUILD_ID_OFFSET = 0x40;
	constexpr size_t BUILD_ID_SIZE = 20;
	constexpr size_t MIN_THREAD_STACK = 256 * 1024;
	constexpr uint64_t SYSTEM_CORE_MASK = 1u << 3;

	int s_logFd = -1;
	char s_buildId[BUILD_ID_SIZE * 2 + 1];

	struct ThreadStart
	{
		void* (*start)(void*);
		void* arg;
		std::array<uintptr_t, 2> argWords;
	};

	struct LiveThread
	{
		pthread_t pthread;
		Handle handle;
		uintptr_t stackMem;
		size_t stackSize;
		uintptr_t start;
		std::array<uintptr_t, 2> argWords;
	};

	std::mutex s_liveMutex;
	std::vector<LiveThread> s_liveThreads;

	struct HeapRange
	{
		uint64_t address;
		uint64_t size;
	};

	constexpr size_t MAX_BASELINE_RANGES = 16;
	HeapRange s_baselineRanges[MAX_BASELINE_RANGES];
	size_t s_baselineCount = 0;

	template<typename TCallback>
	void ForEachNonPlainHeapRange(TCallback callback)
	{
		u64 heapStart = 0, heapSize = 0;
		svcGetInfo(&heapStart, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
		svcGetInfo(&heapSize, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
		u64 address = heapStart;
		while (address < heapStart + heapSize)
		{
			MemoryInfo info{};
			u32 pageInfo;
			if (R_FAILED(svcQueryMemory(&info, &pageInfo, address)) || info.size == 0)
				break;
			const bool plain = info.type == MemType_Heap && info.perm == Perm_Rw && info.attr == 0;
			if (!plain && info.type != MemType_Unmapped)
				callback(info, heapStart);
			address = info.addr + info.size;
		}
	}

	bool IsBaselineRange(const MemoryInfo& info)
	{
		for (size_t i = 0; i < s_baselineCount; i++)
		{
			if (s_baselineRanges[i].address == info.addr && s_baselineRanges[i].size == info.size)
				return true;
		}
		return false;
	}

	bool IsReadable(uintptr_t address, size_t size)
	{
		MemoryInfo info{};
		u32 pageInfo;
		return address != 0 && (address & 7) == 0 && R_SUCCEEDED(svcQueryMemory(&info, &pageInfo, address)) &&
			(info.perm & Perm_R) && address + size <= info.addr + info.size;
	}

	bool IsStackBorrowed(uintptr_t stackMem)
	{
		MemoryInfo info{};
		u32 pageInfo;
		return R_SUCCEEDED(svcQueryMemory(&info, &pageInfo, stackMem)) && info.attr != 0;
	}

	void SpreadCurrentThread()
	{
		s32 idealCore;
		u64 mask;
		if (R_FAILED(svcGetThreadCoreMask(&idealCore, &mask, CUR_THREAD_HANDLE)))
			return;
		const u64 allowed = HorizonDiagnostics::GetApplicationCoreMask();
		if (mask == allowed)
			return;
		if (idealCore < 0 || ((allowed >> idealCore) & 1) == 0)
			idealCore = __builtin_ctzll(allowed);
		svcSetThreadCoreMask(CUR_THREAD_HANDLE, idealCore, allowed);
	}

	void* WrappedThreadStart(void* param)
	{
		const ThreadStart threadStart = *(ThreadStart*)param;
		delete (ThreadStart*)param;
		SpreadCurrentThread();
		if (const Thread* self = threadGetSelf())
		{
			std::lock_guard lock(s_liveMutex);
			s_liveThreads.push_back({pthread_self(), self->handle, (uintptr_t)self->stack_mem, self->stack_sz,
									 (uintptr_t)threadStart.start, threadStart.argWords});
		}
		return threadStart.start(threadStart.arg);
	}
}

extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg)
{
	ThreadStart* threadStart = new (std::nothrow) ThreadStart{start, arg, {}};
	if (!threadStart)
		return EAGAIN;
	if (IsReadable((uintptr_t)arg, sizeof(threadStart->argWords)))
		std::memcpy(threadStart->argWords.data(), arg, sizeof(threadStart->argWords));

	pthread_attr_t sizedAttr;
	if (attr)
		sizedAttr = *attr;
	else
		pthread_attr_init(&sizedAttr);
	size_t stackSize = 0;
	pthread_attr_getstacksize(&sizedAttr, &stackSize);
	if (stackSize < MIN_THREAD_STACK)
		pthread_attr_setstacksize(&sizedAttr, MIN_THREAD_STACK);

	const int result = __real_pthread_create(thread, &sizedAttr, WrappedThreadStart, threadStart);
	if (!attr)
		pthread_attr_destroy(&sizedAttr);
	if (result != 0)
		delete threadStart;
	return result;
}

extern "C" int __wrap_pthread_join(pthread_t thread, void** value)
{
	const int result = __real_pthread_join(thread, value);
	if (result == 0)
	{
		std::lock_guard lock(s_liveMutex);
		std::erase_if(s_liveThreads, [thread](const LiveThread& t) { return t.pthread == thread; });
	}
	return result;
}

namespace HorizonDiagnostics
{
	void Init(const char* directory, const char* name)
	{
		const auto* id = (const unsigned char*)&_start + NRO_BUILD_ID_OFFSET;
		for (size_t i = 0; i < BUILD_ID_SIZE; i++)
			std::snprintf(s_buildId + i * 2, 3, "%02x", id[i]);

		CreateDirectories(directory);
		char path[FS_MAX_PATH];
		std::snprintf(path, sizeof(path), "%s/%s.log", directory, name);
		s_logFd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		std::snprintf(path, sizeof(path), "%s/%s-crash.log", directory, name);
		HorizonException::OpenCrashLog(path);

		s_baselineCount = 0;
		ForEachNonPlainHeapRange([](const MemoryInfo& info, u64 heapStart) {
			if (s_baselineCount < MAX_BASELINE_RANGES)
				s_baselineRanges[s_baselineCount++] = {info.addr, info.size};
		});

		Log("%s, build id %s, module base %016lx", name, s_buildId, GetModuleBase());
		Log("process core mask %#lx, application cores %#lx", GetProcessCoreMask(), GetApplicationCoreMask());
	}

	void Shutdown()
	{
		HorizonException::CloseCrashLog();
		if (s_logFd >= 0)
			close(s_logFd);
		s_logFd = -1;
	}

	void Log(const char* format, ...)
	{
		char line[512];
		va_list args;
		va_start(args, format);
		int length = std::vsnprintf(line, sizeof(line) - 1, format, args);
		va_end(args);
		if (length < 0)
			return;
		length = std::min<int>(length, sizeof(line) - 2);
		line[length++] = '\n';
		if (s_logFd >= 0)
			(void)write(s_logFd, line, length);
		std::fwrite(line, 1, length, stdout);
	}

	const char* GetBuildId()
	{
		return s_buildId;
	}

	uintptr_t GetModuleBase()
	{
		return (uintptr_t)&_start;
	}

	bool CreateDirectories(const char* path)
	{
		char partial[FS_MAX_PATH];
		const size_t length = std::strlen(path);
		if (length >= sizeof(partial))
			return false;
		const char* root = std::strstr(path, ":/");
		size_t i = root ? (size_t)(root - path) + 2 : 1;
		for (; i <= length; i++)
		{
			if (path[i] != '/' && path[i] != '\0')
				continue;
			std::memcpy(partial, path, i);
			partial[i] = '\0';
			if (mkdir(partial, 0777) != 0 && errno != EEXIST)
				return false;
		}
		return true;
	}

	size_t GetFreeHeap()
	{
		const struct mallinfo info = mallinfo();
		return (size_t)(fake_heap_end - fake_heap_start) - info.arena + info.fordblks;
	}

	size_t AuditHeap(const char* when)
	{
		size_t count = 0;
		ForEachNonPlainHeapRange([&](const MemoryInfo& info, u64 heapStart) {
			if (IsBaselineRange(info))
				return;
			Log("heap audit (%s): +%#lx size %#lx type %#x perm %#x attr %#x", when, info.addr - heapStart,
				info.size, info.type, info.perm, info.attr);
			count++;
		});
		Log("heap audit (%s): %zu non-plain ranges since start, %zu MiB free", when, count, GetFreeHeap() >> 20);
		return count;
	}

	size_t AuditThreads(const char* when)
	{
		const uintptr_t base = GetModuleBase();
		std::lock_guard lock(s_liveMutex);
		size_t count = 0;
		for (const LiveThread& t : s_liveThreads)
		{
			if (!IsStackBorrowed(t.stackMem))
				continue;
			const bool exited = R_SUCCEEDED(svcWaitSynchronizationSingle(t.handle, 0));
			Log("thread audit (%s): unjoined %s, stack %#lx size %#lx, start +%#lx, arg words %#lx %#lx", when,
				exited ? "exited" : "running", t.stackMem, t.stackSize, t.start - base, t.argWords[0], t.argWords[1]);
			count++;
		}
		Log("thread audit (%s): %zu unjoined", when, count);
		return count;
	}

	uint64_t GetProcessCoreMask()
	{
		u64 mask = 0;
		if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
			return 0x7;
		return mask;
	}

	uint64_t GetApplicationCoreMask()
	{
		const uint64_t mask = GetProcessCoreMask() & ~SYSTEM_CORE_MASK;
		return mask ? mask : 0x7;
	}

	void PlaceCurrentThread(const char* role, int idealCore, uint64_t mask, int priority)
	{
		mask &= GetProcessCoreMask();
		if (mask == 0)
			mask = GetApplicationCoreMask();
		if (idealCore < 0 || ((mask >> idealCore) & 1) == 0)
			idealCore = __builtin_ctzll(mask);
		const Result maskResult = svcSetThreadCoreMask(CUR_THREAD_HANDLE, idealCore, mask);
		const Result priorityResult = svcSetThreadPriority(CUR_THREAD_HANDLE, priority);
		Log("thread %s: core %d mask %#lx priority %d (%#x, %#x)", role, idealCore, mask, priority, maskResult,
			priorityResult);
	}
}
