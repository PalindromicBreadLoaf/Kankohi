// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Routes the guest's software keyboard to the system keyboard applet
namespace KeyboardSwitch
{
	void Install();
	void Pump();
	void Cancel();
}
