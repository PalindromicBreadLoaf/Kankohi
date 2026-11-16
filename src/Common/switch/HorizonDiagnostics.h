// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

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

	// Logs every mapped range in the heap region that isn't plain RW heap and returns how many
	// there were.
	size_t AuditHeap(const char* when);

	// Logs every thread created through pthread_create whose stack is still borrowed
	size_t AuditThreads(const char* when);

	// The allowed cores the process has
	uint64_t GetApplicationCoreMask();
	uint64_t GetProcessCoreMask();
}
