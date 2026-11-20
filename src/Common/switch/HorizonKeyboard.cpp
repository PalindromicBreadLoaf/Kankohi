// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonKeyboard.h"
#include "HorizonDiagnostics.h"

#include <algorithm>
#include <vector>

#include <switch.h>

namespace
{
	constexpr size_t MIN_OUTPUT_SIZE = 1024;
	constexpr size_t MAX_UTF8_BYTES_PER_CHARACTER = 4;
}

namespace HorizonKeyboard
{
	std::optional<std::string> Show(const Request& request)
	{
		SwkbdConfig config;
		const Result createResult = swkbdCreate(&config, 0);
		if (R_FAILED(createResult))
		{
			HorizonDiagnostics::Log("swkbd: swkbdCreate failed (%#x)", createResult);
			return std::nullopt;
		}

		swkbdConfigMakePresetDefault(&config);
		swkbdConfigSetReturnButtonFlag(&config, 0);
		swkbdConfigSetStringLenMax(&config, request.maxLength);
		if (!request.initialText.empty())
			swkbdConfigSetInitialText(&config, request.initialText.c_str());
		if (!request.guideText.empty())
			swkbdConfigSetGuideText(&config, request.guideText.c_str());

		std::vector<char> output(std::max<size_t>(MIN_OUTPUT_SIZE, size_t{request.maxLength} * MAX_UTF8_BYTES_PER_CHARACTER + 1), '\0');
		const Result showResult = swkbdShow(&config, output.data(), output.size());
		swkbdClose(&config);
		if (R_FAILED(showResult))
		{
			if (R_VALUE(showResult) != MAKERESULT(Module_Libnx, LibnxError_LibAppletBadExit))
				HorizonDiagnostics::Log("swkbd: swkbdShow failed (%#x)", showResult);
			return std::nullopt;
		}
		return std::string(output.data());
	}
}
