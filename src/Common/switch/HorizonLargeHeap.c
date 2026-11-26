// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonLargeHeap.h"

#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE ((size_t)0x1000)
#define MAX_BLOCKS 4096

typedef struct
{
	uintptr_t start;
	uintptr_t end;
	bool used;
	uintptr_t callers[LARGE_HEAP_CALLERS];
} Block;

static Block s_blocks[MAX_BLOCKS];
static size_t s_blockCount;
static uintptr_t s_end;

static uintptr_t AlignDown(uintptr_t value, size_t alignment)
{
	return value & ~(uintptr_t)(alignment - 1);
}

static size_t PageAlignUp(size_t size)
{
	return (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static void InsertBlock(size_t index, uintptr_t start, uintptr_t end, bool used)
{
	memmove(&s_blocks[index + 1], &s_blocks[index], (s_blockCount - index) * sizeof(Block));
	s_blocks[index] = (Block){.start = start, .end = end, .used = used};
	s_blockCount++;
}

static void RemoveBlock(size_t index)
{
	s_blockCount--;
	memmove(&s_blocks[index], &s_blocks[index + 1], (s_blockCount - index) * sizeof(Block));
}

static size_t FindBlock(const void* ptr)
{
	const uintptr_t address = (uintptr_t)ptr;
	size_t low = 0, high = s_blockCount;
	while (low < high)
	{
		const size_t middle = (low + high) / 2;
		if (s_blocks[middle].start > address)
			low = middle + 1;
		else
			high = middle;
	}
	if (low == s_blockCount || s_blocks[low].start != address || !s_blocks[low].used)
		abort();
	return low;
}

// Splits whatever of block index lies outside start-end into free blocks
static void CarveBlock(size_t index, uintptr_t start, uintptr_t end)
{
	const Block block = s_blocks[index];
	s_blocks[index].start = start;
	s_blocks[index].end = end;
	s_blocks[index].used = true;
	if (start > block.start)
		InsertBlock(index + 1, block.start, start, false);
	if (end < block.end)
		InsertBlock(index, end, block.end, false);
}

void LargeHeap_Init(uintptr_t end)
{
	s_end = AlignDown(end, PAGE_SIZE);
	s_blockCount = 0;
}

static void* AllocateAt(size_t index, const uintptr_t* callers)
{
	memcpy(s_blocks[index].callers, callers, sizeof(s_blocks[index].callers));
	return (void*)s_blocks[index].start;
}

void* LargeHeap_Allocate(size_t size, size_t alignment, uintptr_t floor, const uintptr_t* callers)
{
	if (size == 0 || size > s_end || s_blockCount + 2 > MAX_BLOCKS)
		return NULL;
	size = PageAlignUp(size);
	if (alignment < PAGE_SIZE)
		alignment = PAGE_SIZE;

	size_t best = s_blockCount;
	size_t bestSize = SIZE_MAX;
	for (size_t i = 0; i < s_blockCount; i++)
	{
		const Block* block = &s_blocks[i];
		const size_t blockSize = block->end - block->start;
		if (block->used || blockSize < size || blockSize >= bestSize || AlignDown(block->end - size, alignment) < block->start)
			continue;
		best = i;
		bestSize = blockSize;
	}
	if (best != s_blockCount)
	{
		const uintptr_t start = AlignDown(s_blocks[best].end - size, alignment);
		const size_t index = best + (start + size < s_blocks[best].end);
		CarveBlock(best, start, start + size);
		return AllocateAt(index, callers);
	}

	const uintptr_t bottom = LargeHeap_GetBottom();
	if (bottom < size)
		return NULL;
	const uintptr_t start = AlignDown(bottom - size, alignment);
	if (start < floor)
		return NULL;
	if (start + size < bottom)
		InsertBlock(s_blockCount, start + size, bottom, false);
	InsertBlock(s_blockCount, start, start + size, true);
	return AllocateAt(s_blockCount - 1, callers);
}

void LargeHeap_Free(void* ptr)
{
	size_t index = FindBlock(ptr);
	s_blocks[index].used = false;
	if (index + 1 < s_blockCount && !s_blocks[index + 1].used)
	{
		s_blocks[index].start = s_blocks[index + 1].start;
		RemoveBlock(index + 1);
	}
	if (index > 0 && !s_blocks[index - 1].used)
	{
		s_blocks[index - 1].start = s_blocks[index].start;
		RemoveBlock(index);
		index--;
	}
	if (index + 1 == s_blockCount)
		s_blockCount--;
}

bool LargeHeap_Resize(void* ptr, size_t size)
{
	const size_t index = FindBlock(ptr);
	size = PageAlignUp(size);
	const uintptr_t start = s_blocks[index].start;
	const size_t currentSize = s_blocks[index].end - start;
	if (size == currentSize)
		return true;
	const bool followedByFree = index > 0 && !s_blocks[index - 1].used;
	if (size > currentSize)
	{
		if (!followedByFree || s_blocks[index - 1].end - start < size)
			return false;
		s_blocks[index].end = start + size;
		s_blocks[index - 1].start = start + size;
		if (s_blocks[index - 1].start == s_blocks[index - 1].end)
			RemoveBlock(index - 1);
		return true;
	}
	if (followedByFree)
	{
		s_blocks[index].end = start + size;
		s_blocks[index - 1].start = start + size;
		return true;
	}
	if (s_blockCount + 1 > MAX_BLOCKS)
		return false;
	CarveBlock(index, start, start + size);
	return true;
}

size_t LargeHeap_GetSize(const void* ptr)
{
	const Block* block = &s_blocks[FindBlock(ptr)];
	return block->end - block->start;
}

uintptr_t LargeHeap_GetBottom(void)
{
	return s_blockCount ? s_blocks[s_blockCount - 1].start : s_end;
}

size_t LargeHeap_GetFree(void)
{
	size_t total = 0;
	for (size_t i = 0; i < s_blockCount; i++)
		total += s_blocks[i].used ? 0 : s_blocks[i].end - s_blocks[i].start;
	return total;
}

size_t LargeHeap_GetLargestFree(void)
{
	size_t largest = 0;
	for (size_t i = 0; i < s_blockCount; i++)
	{
		const size_t size = s_blocks[i].end - s_blocks[i].start;
		if (!s_blocks[i].used && size > largest)
			largest = size;
	}
	return largest;
}

size_t LargeHeap_GetBlocks(LargeHeapBlock* blocks, size_t capacity)
{
	size_t count = 0;
	for (size_t i = 0; i < s_blockCount; i++)
	{
		const Block* block = &s_blocks[i];
		if (!block->used)
			continue;
		if (count < capacity)
		{
			blocks[count].start = block->start;
			blocks[count].size = block->end - block->start;
			memcpy(blocks[count].callers, block->callers, sizeof(block->callers));
		}
		count++;
	}
	return count;
}
