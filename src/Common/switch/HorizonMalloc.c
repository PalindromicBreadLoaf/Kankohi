// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

// Replaces newlib's dlmalloc 2.6.
#include <malloc.h>
#include <reent.h>
#include <stdlib.h>
#include <string.h>

#include <switch/kernel/mutex.h>
#include <switch/kernel/svc.h>
#include <switch/result.h>

#include "HorizonLargeHeap.h"

static inline int InitialLock(Mutex* lock)
{
	mutexInit(lock);
	return 0;
}

static inline int AcquireLock(Mutex* lock)
{
	mutexLock(lock);
	return 0;
}

#define USE_LOCKS 2
#define MLOCK_T Mutex
#define INITIAL_LOCK(lk) InitialLock(lk)
#define DESTROY_LOCK(lk) (0)
#define ACQUIRE_LOCK(lk) AcquireLock(lk)
#define RELEASE_LOCK(lk) mutexUnlock(lk)
#define TRY_LOCK(lk) mutexTryLock(lk)
static MLOCK_T malloc_global_mutex = 0;

#undef M_TRIM_THRESHOLD
#undef M_MMAP_THRESHOLD

#define USE_DL_PREFIX 1
#define MALLOC_INSPECT_ALL 1
#define STRUCT_MALLINFO_DECLARED 1
#define HAVE_MMAP 0
#define HAVE_MREMAP 0
#define MORECORE MoreCore
#define MORECORE_CONTIGUOUS 1
#define DEFAULT_GRANULARITY ((size_t)1 << 20)
#define malloc_getpagesize ((size_t)0x1000)
#define LACKS_TIME_H 1
#define LACKS_SYS_MMAN_H 1
#define REALLOC_ZERO_BYTES_FREES 1

#define LARGE_SIZE ((size_t)4 << 20)

extern char* fake_heap_start;
extern char* fake_heap_end;

// Small blocks grow up from the start of the heap and large ones down from its end.
// This is to hopefully prevent small allocations fragmenting the heap
static Mutex s_layoutLock;
static char* s_break;
static uintptr_t s_largeBottom;

static void InitLayout(void)
{
	if (s_break)
		return;
	s_break = (char*)(((uintptr_t)fake_heap_start + 15) & ~(uintptr_t)15);
	LargeHeap_Init((uintptr_t)fake_heap_end);
	__atomic_store_n(&s_largeBottom, LargeHeap_GetBottom(), __ATOMIC_RELAXED);
}

static void* MoreCore(ptrdiff_t increment)
{
	mutexLock(&s_layoutLock);
	InitLayout();
	void* previous = s_break;
	if (increment > 0 && (size_t)increment > s_largeBottom - (uintptr_t)s_break)
		previous = (void*)-1;
	else
		s_break += increment;
	mutexUnlock(&s_layoutLock);
	return previous;
}

static void GetCallers(uintptr_t* callers)
{
	memset(callers, 0, sizeof(uintptr_t) * LARGE_HEAP_CALLERS);
	uintptr_t frame = (uintptr_t)__builtin_frame_address(0);
	MemoryInfo info;
	u32 pageInfo;
	if (R_FAILED(svcQueryMemory(&info, &pageInfo, frame)))
		return;
	const uintptr_t end = info.addr + info.size;
	for (size_t i = 0; i < LARGE_HEAP_CALLERS; i++)
	{
		const uintptr_t* words = (const uintptr_t*)frame;
		callers[i] = words[1];
		if (words[0] <= frame || words[0] > end - 2 * sizeof(uintptr_t) || (words[0] & 7) != 0)
			return;
		frame = words[0];
	}
}

static __attribute__((noinline)) void* AllocateLarge(size_t alignment, size_t size)
{
	uintptr_t callers[LARGE_HEAP_CALLERS];
	GetCallers(callers);
	mutexLock(&s_layoutLock);
	InitLayout();
	void* ptr = LargeHeap_Allocate(size, alignment, (uintptr_t)s_break, callers);
	__atomic_store_n(&s_largeBottom, LargeHeap_GetBottom(), __ATOMIC_RELAXED);
	mutexUnlock(&s_layoutLock);
	return ptr;
}

static void FreeLarge(void* ptr)
{
	mutexLock(&s_layoutLock);
	LargeHeap_Free(ptr);
	__atomic_store_n(&s_largeBottom, LargeHeap_GetBottom(), __ATOMIC_RELAXED);
	mutexUnlock(&s_layoutLock);
}

static size_t GetLargeSize(void* ptr)
{
	mutexLock(&s_layoutLock);
	const size_t size = LargeHeap_GetSize(ptr);
	mutexUnlock(&s_layoutLock);
	return size;
}

static int ResizeLarge(void* ptr, size_t size)
{
	mutexLock(&s_layoutLock);
	const int resized = LargeHeap_Resize(ptr, size);
	mutexUnlock(&s_layoutLock);
	return resized;
}

static int IsLarge(const void* ptr)
{
	const uintptr_t bottom = __atomic_load_n(&s_largeBottom, __ATOMIC_RELAXED);
	return bottom != 0 && (uintptr_t)ptr >= bottom;
}

#include "malloc.c"

static void* Allocate(size_t size)
{
	if (size >= LARGE_SIZE)
	{
		void* ptr = AllocateLarge(0, size);
		return ptr ? ptr : dlmalloc(size);
	}
	void* ptr = dlmalloc(size);
	return ptr ? ptr : AllocateLarge(0, size);
}

static void* AllocateAligned(size_t alignment, size_t size)
{
	if (size >= LARGE_SIZE)
	{
		void* ptr = AllocateLarge(alignment, size);
		return ptr ? ptr : dlmemalign(alignment, size);
	}
	void* ptr = dlmemalign(alignment, size);
	return ptr ? ptr : AllocateLarge(alignment, size);
}

static void Free(void* ptr)
{
	if (IsLarge(ptr))
		FreeLarge(ptr);
	else
		dlfree(ptr);
}

static void* Reallocate(void* ptr, size_t size)
{
	if (!ptr)
		return Allocate(size);
	if (size == 0)
	{
		Free(ptr);
		return NULL;
	}
	const int large = IsLarge(ptr);
	if (large && size >= LARGE_SIZE && ResizeLarge(ptr, size))
		return ptr;
	if (!large && size < LARGE_SIZE)
	{
		void* resized = dlrealloc(ptr, size);
		if (resized)
			return resized;
	}
	void* moved = Allocate(size);
	if (!moved)
		return NULL;
	const size_t oldSize = large ? GetLargeSize(ptr) : dlmalloc_usable_size(ptr);
	memcpy(moved, ptr, oldSize < size ? oldSize : size);
	Free(ptr);
	return moved;
}

void* _malloc_r(struct _reent* reent, size_t size)
{
	(void)reent;
	return Allocate(size);
}

void _free_r(struct _reent* reent, void* ptr)
{
	(void)reent;
	Free(ptr);
}

void* _realloc_r(struct _reent* reent, void* ptr, size_t size)
{
	(void)reent;
	return Reallocate(ptr, size);
}

void* _calloc_r(struct _reent* reent, size_t count, size_t size)
{
	(void)reent;
	size_t total;
	if (__builtin_mul_overflow(count, size, &total))
		return NULL;
	if (total < LARGE_SIZE)
	{
		void* ptr = dlcalloc(count, size);
		if (ptr)
			return ptr;
	}
	void* ptr = Allocate(total);
	if (ptr)
		memset(ptr, 0, total);
	return ptr;
}

void* _memalign_r(struct _reent* reent, size_t alignment, size_t size)
{
	(void)reent;
	return AllocateAligned(alignment, size);
}

void* _valloc_r(struct _reent* reent, size_t size)
{
	(void)reent;
	return AllocateAligned(malloc_getpagesize, size);
}

void* _pvalloc_r(struct _reent* reent, size_t size)
{
	(void)reent;
	return AllocateAligned(malloc_getpagesize, (size + malloc_getpagesize - 1) & ~(malloc_getpagesize - 1));
}

size_t _malloc_usable_size_r(struct _reent* reent, void* ptr)
{
	(void)reent;
	return IsLarge(ptr) ? GetLargeSize(ptr) : dlmalloc_usable_size(ptr);
}

static void FindLargestFree(void* start, void* end, size_t usedBytes, void* largest)
{
	const size_t size = (size_t)((char*)end - (char*)start);
	if (usedBytes == 0 && size > *(size_t*)largest)
		*(size_t*)largest = size;
}

size_t HorizonMalloc_GetLargestFree(void)
{
	size_t largest = 0;
	dlmalloc_inspect_all(FindLargestFree, &largest);
	mutexLock(&s_layoutLock);
	InitLayout();
	const size_t gap = s_largeBottom - (uintptr_t)s_break;
	const size_t largestLarge = LargeHeap_GetLargestFree();
	mutexUnlock(&s_layoutLock);
	if (gap > largest)
		largest = gap;
	return largestLarge > largest ? largestLarge : largest;
}

size_t HorizonMalloc_GetLargeBlocks(LargeHeapBlock* blocks, size_t capacity)
{
	mutexLock(&s_layoutLock);
	const size_t count = LargeHeap_GetBlocks(blocks, capacity);
	mutexUnlock(&s_layoutLock);
	return count;
}

size_t HorizonMalloc_GetSmallHeapSize(void)
{
	mutexLock(&s_layoutLock);
	InitLayout();
	const size_t size = (size_t)(s_break - fake_heap_start);
	mutexUnlock(&s_layoutLock);
	return size;
}

struct mallinfo _mallinfo_r(struct _reent* reent)
{
	(void)reent;
	struct mallinfo info = dlmallinfo();
	mutexLock(&s_layoutLock);
	InitLayout();
	const size_t largeArena = (uintptr_t)fake_heap_end - s_largeBottom;
	const size_t largeFree = LargeHeap_GetFree();
	mutexUnlock(&s_layoutLock);
	info.arena += largeArena;
	info.fordblks += largeFree;
	info.uordblks += largeArena - largeFree;
	return info;
}

int _malloc_trim_r(struct _reent* reent, size_t pad)
{
	(void)reent;
	return dlmalloc_trim(pad);
}

int _mallopt_r(struct _reent* reent, int parameter, int value)
{
	(void)reent;
	return dlmallopt(parameter, value);
}

void _malloc_stats_r(struct _reent* reent)
{
	(void)reent;
	dlmalloc_stats();
}
