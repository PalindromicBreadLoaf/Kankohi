// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Samples every named thread and writes a report of where each one spent its time
namespace HorizonSampler
{
	struct Counters
	{
		uint64_t frames = 0;
		uint64_t gpuBusyNs = 0;
	};

	struct GuestFunction
	{
		uint32_t guestAddress;
		uintptr_t hostAddress;
		size_t hostSize;
	};

	struct Options
	{
		double seconds = 10.0;
		std::string path;
		std::string description;
		uintptr_t codeStart = 0;
		uintptr_t codeEnd = 0;
		Counters (*readCounters)() = nullptr;
		std::vector<GuestFunction> (*listFunctions)() = nullptr;
		std::string (*nameFunction)(uint32_t guestAddress) = nullptr;
	};

	bool Start(Options options);
	bool IsRunning();

	void Stop();
}
