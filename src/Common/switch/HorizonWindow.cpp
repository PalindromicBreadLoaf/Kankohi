// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonDiagnostics.h"

#include <atomic>

#include <switch.h>

extern "C" Result __real_nwindowDequeueBuffer(NWindow* window, s32* slot, NvMultiFence* fence);
extern "C" Result __real_binderConvertErrorCode(s32 code);

namespace
{
	constexpr int MAX_LOGGED_ERRORS = 32;
	constexpr s32 BINDER_WOULD_BLOCK = -11;

	std::atomic_int s_loggedErrors = 0;

	bool ShouldLog()
	{
		return s_loggedErrors.fetch_add(1, std::memory_order_relaxed) < MAX_LOGGED_ERRORS;
	}
}

extern "C" Result __wrap_nwindowDequeueBuffer(NWindow* window, s32* slot, NvMultiFence* fence)
{
	const Result result = __real_nwindowDequeueBuffer(window, slot, fence);
	if (R_FAILED(result) && window && ShouldLog())
		HorizonDiagnostics::Log("window: dequeue failed (%#x), %ux%u format %u usage %#x, slots configured %#lx requested %#lx, held %d, swap interval %u",
			result, window->width, window->height, window->format, window->usage, window->slots_configured,
			window->slots_requested, window->cur_slot, window->swap_interval);
	return result;
}

extern "C" Result __wrap_binderConvertErrorCode(s32 code)
{
	if (code < 0 && code != BINDER_WOULD_BLOCK && ShouldLog())
		HorizonDiagnostics::Log("window: binder error %d", code);
	return __real_binderConvertErrorCode(code);
}
