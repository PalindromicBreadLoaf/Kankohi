// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "input/api/ControllerProvider.h"

class SwitchControllerProvider : public ControllerProviderBase
{
public:
	SwitchControllerProvider();
	~SwitchControllerProvider() override;

	inline static InputAPI::Type kAPIType = InputAPI::Switch;
	InputAPI::Type api() const override { return kAPIType; }

	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override;
};
