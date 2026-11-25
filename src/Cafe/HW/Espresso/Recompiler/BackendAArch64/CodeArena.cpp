// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CodeArena.h"

#include <cstring>
#include <iterator>

static constexpr size_t CODE_ALIGNMENT = 16;

static size_t AlignCodeSize(size_t size)
{
	return (size + CODE_ALIGNMENT - 1) & ~(CODE_ALIGNMENT - 1);
}

uint32_t CodeArena::Create(size_t size)
{
	std::unique_lock lock(m_mutex);
	const uint32_t result = HorizonMemory::CreateCodeRegion(m_region, size);
	if (result != 0)
		return result;
	m_freeRanges.clear();
	m_freeRanges.emplace(0, size);
	m_usedSize = 0;
	return 0;
}

uint32_t CodeArena::Destroy()
{
	std::unique_lock lock(m_mutex);
	m_freeRanges.clear();
	m_usedSize = 0;
	return HorizonMemory::DestroyCodeRegion(m_region);
}

void* CodeArena::Publish(const void* code, size_t size)
{
	const size_t alignedSize = AlignCodeSize(size);
	std::unique_lock lock(m_mutex);
	for (auto it = m_freeRanges.begin(); it != m_freeRanges.end(); ++it)
	{
		const auto [offset, rangeSize] = *it;
		if (rangeSize < alignedSize)
			continue;
		if (rangeSize > alignedSize)
			m_freeRanges.emplace_hint(std::next(it), offset + alignedSize, rangeSize - alignedSize);
		m_freeRanges.erase(it);
		m_usedSize += alignedSize;
		std::memcpy(m_region.writable + offset, code, size);
		HorizonMemory::FlushCode(m_region, offset, size);
		return m_region.executable + offset;
	}
	return nullptr;
}

void CodeArena::Free(void* code, size_t size)
{
	const size_t alignedSize = AlignCodeSize(size);
	std::unique_lock lock(m_mutex);
	const size_t offset = (uint8_t*)code - m_region.executable;
	auto it = m_freeRanges.emplace(offset, alignedSize).first;
	m_usedSize -= alignedSize;
	if (auto next = std::next(it); next != m_freeRanges.end() && it->first + it->second == next->first)
	{
		it->second += next->second;
		m_freeRanges.erase(next);
	}
	if (it != m_freeRanges.begin())
	{
		auto previous = std::prev(it);
		if (previous->first + previous->second == it->first)
		{
			previous->second += it->second;
			m_freeRanges.erase(it);
		}
	}
}
