// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <span>

namespace HorizonTheme
{
	bool IsDark();

	std::span<const uint8_t> GetStandardFont();
	std::span<const uint8_t> GetSymbolFont();
}
