// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common/switch/HorizonMemory.h"

#include <map>
#include <mutex>

// Suballocates a single executable region
class CodeArena
{
  public:
	uint32_t Create(size_t size);
	uint32_t Destroy();

	// Copies position-independent code into the arena
	void* Publish(const void* code, size_t size);
	void Free(void* code, size_t size);

	size_t GetSize() const { return m_region.size; }
	size_t GetUsedSize() const { return m_usedSize; }

  private:
	std::mutex m_mutex;
	HorizonMemory::CodeRegion m_region;
	std::map<size_t, size_t> m_freeRanges;
	size_t m_usedSize = 0;
};
