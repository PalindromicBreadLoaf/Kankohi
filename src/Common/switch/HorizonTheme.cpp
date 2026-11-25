// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonTheme.h"
#include "HorizonDiagnostics.h"

#include <vector>

#include <switch.h>

namespace
{
	std::vector<uint8_t> CopySharedFont(PlSharedFontType type)
	{
		PlFontData font{};
		const Result result = plGetSharedFontByType(&font, type);
		if (R_FAILED(result))
		{
			HorizonDiagnostics::Log("theme: shared font %d is unavailable (%#x)", static_cast<int>(type), result);
			return {};
		}
		const auto* data = static_cast<const uint8_t*>(font.address);
		return {data, data + font.size};
	}

	struct SharedFonts
	{
		std::vector<uint8_t> standard;
		std::vector<uint8_t> symbols;

		SharedFonts()
		{
			const Result result = plInitialize(PlServiceType_User);
			if (R_FAILED(result))
			{
				HorizonDiagnostics::Log("theme: plInitialize failed (%#x)", result);
				return;
			}
			standard = CopySharedFont(PlSharedFontType_Standard);
			symbols = CopySharedFont(PlSharedFontType_NintendoExt);
			plExit();
		}
	};

	const SharedFonts& GetSharedFonts()
	{
		static const SharedFonts s_fonts;
		return s_fonts;
	}
}

namespace HorizonTheme
{
	bool IsDark()
	{
		if (R_FAILED(setsysInitialize()))
			return true;
		ColorSetId colourSet = ColorSetId_Dark;
		const Result result = setsysGetColorSetId(&colourSet);
		setsysExit();
		return R_FAILED(result) || colourSet == ColorSetId_Dark;
	}

	std::span<const uint8_t> GetStandardFont()
	{
		return GetSharedFonts().standard;
	}

	std::span<const uint8_t> GetSymbolFont()
	{
		return GetSharedFonts().symbols;
	}
}
