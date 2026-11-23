// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ShellLibrary
{
	struct Title
	{
		uint64_t titleId = 0;
		std::string name;
		std::string path;
		std::string printPath;
		std::string region;
		std::string format;
		uint16_t version = 0;
		bool hasDlc = false;
		uint16_t dlcVersion = 0;
		uint32_t minutesPlayed = 0;
		std::string lastPlayed;
		std::shared_ptr<const std::vector<uint8_t>> icon;
	};

	void Start();
	void Stop();

	uint64_t GetGeneration();
	std::vector<Title> GetTitles();

	void Rescan();
	bool IsScanning();
	std::vector<std::string> GetGameDirectories();
}
