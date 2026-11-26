// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace PerfMonitor
{
	void Start();
	void Update();
	void Stop();

	uint64_t GetFrameCount();
	bool IsBenchmarkFinished();

	bool StartProfile(double seconds);
	bool IsProfiling();
}
