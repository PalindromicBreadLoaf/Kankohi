#include "HorizonMemory.h"

#include <cstdlib>

#include <switch.h>

namespace HorizonMemory
{
	static VirtmemReservation* ReserveAligned(void* (*find)(size_t, size_t), size_t size, size_t alignment, uint8_t** out)
	{
		virtmemLock();
		uint8_t* found = (uint8_t*)find(size + alignment, HORIZON_PAGE_SIZE);
		VirtmemReservation* reservation = nullptr;
		if (found)
		{
			uint8_t* aligned = (uint8_t*)(((uintptr_t)found + alignment - 1) & ~(uintptr_t)(alignment - 1));
			reservation = virtmemAddReservation(aligned, size);
			if (reservation)
				*out = aligned;
		}
		virtmemUnlock();
		return reservation;
	}

	static void RemoveReservation(void* reservation)
	{
		virtmemLock();
		virtmemRemoveReservation((VirtmemReservation*)reservation);
		virtmemUnlock();
	}

	uint32_t CreateSegment(Segment& segment, size_t size)
	{
		const Handle self = envGetOwnProcessHandle();
		void* backing = std::aligned_alloc(HORIZON_LARGE_PAGE_SIZE, size);
		if (!backing)
			return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);

		uint8_t* canonical = nullptr;
		VirtmemReservation* reservation = ReserveAligned(virtmemFindCodeMemory, size, HORIZON_LARGE_PAGE_SIZE, &canonical);
		if (!reservation)
		{
			std::free(backing);
			return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
		}

		Result result = svcMapProcessCodeMemory(self, (u64)canonical, (u64)backing, size);
		if (R_SUCCEEDED(result))
		{
			result = svcSetProcessMemoryPermission(self, (u64)canonical, size, Perm_Rw);
			if (R_FAILED(result))
				svcUnmapProcessCodeMemory(self, (u64)canonical, (u64)backing, size);
		}
		if (R_FAILED(result))
		{
			RemoveReservation(reservation);
			std::free(backing);
			return result;
		}
		segment = {backing, canonical, reservation, size};
		return 0;
	}

	uint32_t DestroySegment(Segment& segment)
	{
		if (!segment.canonical)
			return 0;
		const Result result = svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)segment.canonical, (u64)segment.backing, segment.size);
		if (R_FAILED(result))
		{
			segment = {};
			return result;
		}
		RemoveReservation(segment.reservation);
		std::free(segment.backing);
		segment = {};
		return 0;
	}

	void* ReserveAddressSpace(size_t size, size_t alignment, void** reservation)
	{
		uint8_t* base = nullptr;
		*reservation = ReserveAligned(virtmemFindAslr, size, alignment, &base);
		return *reservation ? base : nullptr;
	}

	void ReleaseAddressSpace(void* reservation)
	{
		if (reservation)
			RemoveReservation(reservation);
	}

	uint32_t MapAlias(void* destination, const void* source, size_t size)
	{
		return svcMapProcessMemory(destination, envGetOwnProcessHandle(), (u64)source, size);
	}

	uint32_t UnmapAlias(void* destination, const void* source, size_t size)
	{
		return svcUnmapProcessMemory(destination, envGetOwnProcessHandle(), (u64)source, size);
	}

	const char* FindMissingSyscall()
	{
		static constexpr struct
		{
			unsigned number;
			const char* name;
		} REQUIRED[] = {
			{0x02, "svcSetMemoryPermission"},
			{0x73, "svcSetProcessMemoryPermission"},
			{0x74, "svcMapProcessMemory"},
			{0x75, "svcUnmapProcessMemory"},
			{0x77, "svcMapProcessCodeMemory"},
			{0x78, "svcUnmapProcessCodeMemory"},
		};
		for (const auto& syscall : REQUIRED)
		{
			if (!envIsSyscallHinted(syscall.number))
				return syscall.name;
		}
		if (envGetOwnProcessHandle() == INVALID_HANDLE)
			return "own process handle";
		return nullptr;
	}
}
