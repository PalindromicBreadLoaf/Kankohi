// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LARGE_HEAP_CALLERS 8

typedef struct
{
	uintptr_t start;
	size_t size;
	uintptr_t callers[LARGE_HEAP_CALLERS];
} LargeHeapBlock;

// Page allocator for large blocks
void LargeHeap_Init(uintptr_t end);

void* LargeHeap_Allocate(size_t size, size_t alignment, uintptr_t floor, const uintptr_t* callers);
void LargeHeap_Free(void* ptr);
bool LargeHeap_Resize(void* ptr, size_t size);
size_t LargeHeap_GetSize(const void* ptr);

uintptr_t LargeHeap_GetBottom(void);
size_t LargeHeap_GetFree(void);
size_t LargeHeap_GetLargestFree(void);

size_t LargeHeap_GetBlocks(LargeHeapBlock* blocks, size_t capacity);
