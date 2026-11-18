// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "input/api/Switch/SwitchControllerProvider.h"
#include "input/api/Switch/SwitchController.h"
#include "Common/switch/HorizonInput.h"

SwitchControllerProvider::SwitchControllerProvider()
{
	HorizonInput::Init();
}

SwitchControllerProvider::~SwitchControllerProvider()
{
	HorizonInput::Shutdown();
}

std::vector<std::shared_ptr<ControllerBase>> SwitchControllerProvider::get_controllers()
{
	std::vector<std::shared_ptr<ControllerBase>> result;
	for (sint32 i = 0; i < HorizonInput::MAX_PLAYERS; i++)
	{
		if (HorizonInput::IsConnected(i))
			result.emplace_back(std::make_shared<SwitchController>(i));
	}
	return result;
}
