// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace HorizonDiagnostics
{
	// Creates the directory, sets up logging, and logs the build id
	void Init(const char* directory, const char* name);
	void Shutdown();

	void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));

	const char* GetBuildId();
	uintptr_t GetModuleBase();

	bool CreateDirectories(const char* path);

	size_t GetFreeHeap();
	size_t GetLargestFreeBlock();

	// Logs every mapped range in the heap region that isn't plain RW heap and returns how many
	// there were.
	size_t AuditHeap(const char* when);

	// Logs every thread created through pthread_create whose stack is still borrowed
	size_t AuditThreads(const char* when);

	struct ThreadInfo
	{
		uint32_t handle;
		char name[32];
	};

	void SetCurrentThreadName(const char* name);
	std::vector<ThreadInfo> GetThreads();
	uint32_t GetCurrentThreadHandle();

	// CPU time a thread has used in system ticks
	uint64_t GetThreadTicks(uint32_t handle);
	uint64_t GetSystemTicks();
	uint64_t GetTickFrequency();

	struct Clocks
	{
		uint32_t cpuMHz = 0;
		uint32_t gpuMHz = 0;
		uint32_t memoryMHz = 0;
	};

	Clocks ReadClocks();

	struct Battery
	{
		uint32_t percentage = 0;
		bool charging = false;
		bool voltageLow = false;
	};

	Battery ReadBattery();

	// The allowed cores the process has
	uint64_t GetApplicationCoreMask();
	uint64_t GetProcessCoreMask();

	void PlaceCurrentThread(const char* role, int idealCore, uint64_t mask, int priority);
}
