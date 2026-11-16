// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Common/unix/platform.h"
#include <cstdint>
#include <ctime>

extern "C" time_t timegm(struct tm* tm);

inline uint64_t HorizonSystemTick()
{
	uint64_t tick;
	asm volatile("mrs %0, cntpct_el0" : "=r"(tick));
	return tick;
}

inline uint64_t HorizonTicksToNs(uint64_t ticks)
{
	return (ticks * 625) / 12;
}
