// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace HorizonKeyboard
{
	struct Request
	{
		std::string initialText;
		std::string guideText;
		uint32_t maxLength = 0;
	};

	// Blocks until the keyboard applet closes.
	std::optional<std::string> Show(const Request& request);
}
