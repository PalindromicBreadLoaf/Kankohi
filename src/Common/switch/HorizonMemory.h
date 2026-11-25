// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace HorizonMemory
{
	constexpr size_t HORIZON_PAGE_SIZE = 0x1000;
	constexpr size_t HORIZON_LARGE_PAGE_SIZE = 0x200000;

	// Heap memory moved into the AliasCodeData state
	struct Segment
	{
		void* backing = nullptr;
		uint8_t* canonical = nullptr;
		void* reservation = nullptr;
		size_t size = 0;
	};

	uint32_t CreateSegment(Segment& segment, size_t size);
	uint32_t DestroySegment(Segment& segment);

	// Executable memory with a separate writable alias
	struct CodeRegion
	{
		void* jit = nullptr;
		uint8_t* writable = nullptr;
		uint8_t* executable = nullptr;
		size_t size = 0;
	};

	uint32_t CreateCodeRegion(CodeRegion& region, size_t size);
	uint32_t DestroyCodeRegion(CodeRegion& region);
	void FlushCode(const CodeRegion& region, size_t offset, size_t size);

	void* ReserveAddressSpace(size_t size, size_t alignment, void** reservation);
	void ReleaseAddressSpace(void* reservation);

	uint32_t MapAlias(void* destination, const void* source, size_t size);
	uint32_t UnmapAlias(void* destination, const void* source, size_t size);

	// Returns the name of the first syscall this process wasn't granted
	const char* FindMissingSyscall();
}
