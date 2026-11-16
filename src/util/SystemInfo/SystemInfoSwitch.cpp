// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "util/SystemInfo/SystemInfo.h"

#include <malloc.h>

uint64 QueryRamUsage()
{
	return (uint64)mallinfo().uordblks;
}

void QueryProcTime(uint64 &out_now, uint64 &out_user, uint64 &out_kernel)
{
	out_now = 0;
	out_user = 0;
	out_kernel = 0;
}

void QueryCoreTimes(uint32 count, std::vector<ProcessorTime>& out)
{
	for (auto& time : out)
		time = {};
}
