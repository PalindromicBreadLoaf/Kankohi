// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ShellConfig
{
	struct Setting
	{
		std::string title;
		std::vector<std::string> options;
		std::function<int()> get;
		std::function<void(int)> set;
		bool isToggle = false;
		bool appliesAtLaunch = false;
	};

	struct Section
	{
		std::string header;
		std::string note;
		std::vector<Setting> settings;
	};

	struct Page
	{
		std::string name;
		std::vector<Section> sections;
	};

	std::vector<Page> GetPages();
	std::vector<std::pair<std::string, std::string>> GetAbout();
	void Save();
	void ApplyRunning();
}
