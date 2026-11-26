// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "util/MemMapper/MemMapper.h"
#include "Common/switch/HorizonDiagnostics.h"
#include "Common/switch/HorizonMemory.h"

namespace MemMapper
{
	static constexpr size_t MAX_SEGMENT_SIZE = 0x2000000;

	struct Commit
	{
		HorizonMemory::Segment segment;
		size_t size;
	};

	struct Reservation
	{
		void* handle;
		size_t size;
		std::map<uintptr_t, Commit> commits;
	};

	static std::mutex s_mutex;
	static std::map<uintptr_t, Reservation> s_reservations;

	static uintptr_t PageAlignDown(uintptr_t address)
	{
		return address & ~(uintptr_t)(HorizonMemory::HORIZON_PAGE_SIZE - 1);
	}

	static uintptr_t PageAlignUp(uintptr_t address)
	{
		return PageAlignDown(address + HorizonMemory::HORIZON_PAGE_SIZE - 1);
	}

	static Reservation* FindReservation(uintptr_t start, uintptr_t end)
	{
		auto it = s_reservations.upper_bound(start);
		if (it == s_reservations.begin())
			return nullptr;
		--it;
		if (end > it->first + it->second.size)
			return nullptr;
		return &it->second;
	}

	static bool CreateCommit(Reservation& reservation, uintptr_t address, size_t size)
	{
		Commit commit{{}, size};
		uint32 result = HorizonMemory::CreateSegment(commit.segment, size);
		if (result != 0)
		{
			cemuLog_log(LogType::Force, "MemMapper: Creating a 0x{:x} byte segment failed with 0x{:x}, {} MiB free, largest block {} MiB", size, result,
				HorizonDiagnostics::GetFreeHeap() >> 20, HorizonDiagnostics::GetLargestFreeBlock() >> 20);
			return false;
		}
		result = HorizonMemory::MapAlias((void*)address, commit.segment.canonical, size);
		if (result != 0)
		{
			cemuLog_log(LogType::Force, "MemMapper: Mapping 0x{:x} bytes at 0x{:x} failed with 0x{:x}", size, address, result);
			HorizonMemory::DestroySegment(commit.segment);
			return false;
		}
		memset((void*)address, 0, size);
		reservation.commits.emplace(address, commit);
		return true;
	}

	static void ReleaseCommit(uintptr_t address, Commit& commit)
	{
		uint32 result = HorizonMemory::UnmapAlias((void*)address, commit.segment.canonical, commit.size);
		if (result != 0)
			cemuLog_log(LogType::Force, "MemMapper: Unmapping 0x{:x} failed with 0x{:x}", address, result);
		result = HorizonMemory::DestroySegment(commit.segment);
		if (result != 0)
			cemuLog_log(LogType::Force, "MemMapper: Destroying the segment at 0x{:x} failed with 0x{:x}", address, result);
	}

	size_t GetPageSize()
	{
		return HorizonMemory::HORIZON_PAGE_SIZE;
	}

	void* ReserveMemory(void* baseAddr, size_t size, PAGE_PERMISSION permissionFlags)
	{
		cemu_assert_debug(baseAddr == nullptr);
		size = PageAlignUp(size);
		void* handle;
		void* base = HorizonMemory::ReserveAddressSpace(size, HorizonMemory::HORIZON_LARGE_PAGE_SIZE, &handle);
		if (!base)
		{
			cemuLog_log(LogType::Force, "MemMapper: Unable to reserve 0x{:x} bytes of address space", size);
			return nullptr;
		}
		std::unique_lock lock(s_mutex);
		s_reservations.emplace((uintptr_t)base, Reservation{handle, size, {}});
		return base;
	}

	void FreeReservation(void* baseAddr, size_t size)
	{
		std::unique_lock lock(s_mutex);
		auto it = s_reservations.find((uintptr_t)baseAddr);
		if (it == s_reservations.end())
		{
			cemu_assert_suspicious();
			return;
		}
		for (auto& [address, commit] : it->second.commits)
			ReleaseCommit(address, commit);
		HorizonMemory::ReleaseAddressSpace(it->second.handle);
		s_reservations.erase(it);
	}

	void* AllocateMemory(void* baseAddr, size_t size, PAGE_PERMISSION permissionFlags, bool fromReservation)
	{
		if (HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_EXECUTE))
		{
			cemuLog_log(LogType::Force, "MemMapper: Executable memory has to come from the JIT arena");
			return nullptr;
		}
		const uintptr_t start = PageAlignDown((uintptr_t)baseAddr);
		const uintptr_t end = PageAlignUp((uintptr_t)baseAddr + size);
		if (!fromReservation)
		{
			cemu_assert_debug(baseAddr == nullptr);
			void* memory = std::aligned_alloc(HorizonMemory::HORIZON_PAGE_SIZE, end - start);
			if (memory)
				memset(memory, 0, end - start);
			return memory;
		}

		std::unique_lock lock(s_mutex);
		Reservation* reservation = FindReservation(start, end);
		if (!reservation)
		{
			cemuLog_log(LogType::Force, "MemMapper: 0x{:x}-0x{:x} is outside every reservation", start, end);
			return nullptr;
		}

		std::vector<uintptr_t> created;
		uintptr_t cursor = start;
		auto it = reservation->commits.upper_bound(start);
		if (it != reservation->commits.begin())
		{
			auto previous = std::prev(it);
			cursor = std::max(cursor, previous->first + previous->second.size);
		}
		while (cursor < end)
		{
			const uintptr_t gapEnd = (it != reservation->commits.end()) ? std::min(end, it->first) : end;
			while (cursor < gapEnd)
			{
				const size_t segmentSize = std::min<size_t>(gapEnd - cursor, MAX_SEGMENT_SIZE);
				if (!CreateCommit(*reservation, cursor, segmentSize))
				{
					for (uintptr_t address : created)
					{
						auto commit = reservation->commits.find(address);
						ReleaseCommit(address, commit->second);
						reservation->commits.erase(commit);
					}
					return nullptr;
				}
				created.emplace_back(cursor);
				cursor += segmentSize;
			}
			if (it == reservation->commits.end() || it->first >= end)
				break;
			cursor = it->first + it->second.size;
			++it;
		}
		return baseAddr;
	}

	void FreeMemory(void* baseAddr, size_t size, bool fromReservation)
	{
		if (!fromReservation)
		{
			std::free(baseAddr);
			return;
		}
		const uintptr_t start = PageAlignDown((uintptr_t)baseAddr);
		const uintptr_t end = PageAlignUp((uintptr_t)baseAddr + size);

		std::unique_lock lock(s_mutex);
		Reservation* reservation = FindReservation(start, end);
		if (!reservation)
		{
			cemu_assert_suspicious();
			return;
		}
		auto it = reservation->commits.upper_bound(start);
		if (it != reservation->commits.begin())
			--it;
		while (it != reservation->commits.end() && it->first < end)
		{
			const uintptr_t commitEnd = it->first + it->second.size;
			if (commitEnd <= start)
			{
				++it;
				continue;
			}
			if (it->first < start || commitEnd > end)
			{
				// Segments can only be released whole
				cemuLog_log(LogType::Force, "MemMapper: Keeping 0x{:x}-0x{:x}, which is only partly inside the freed range", it->first, commitEnd);
				++it;
				continue;
			}
			ReleaseCommit(it->first, it->second);
			it = reservation->commits.erase(it);
		}
	}
}
