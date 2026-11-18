// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace EmulationSwitch
{
	constexpr const char* USER_DIRECTORY = "sdmc:/switch/kankohi";

	bool Initialise();
	void Shutdown();

	std::string LaunchTitle(const fs::path& path);
	void StopTitle();
	bool HasTitleExited();

	void Update();
}
