// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace HorizonInput
{
	constexpr int MAX_PLAYERS = 8;

	// Bits follow HidNpadButton
	constexpr uint64_t BUTTON_MASK = 0x0f00ffff;

	struct PadSnapshot
	{
		bool connected = false;
		uint64_t buttons = 0;
		float sticks[4]{};
		float triggers[2]{};

		bool hasMotion = false;
		uint64_t motionSampleCount = 0;
		float motionDeltaSeconds = 0.0f;
		// acceleration in g, angular velocity in rotations per second
		float acceleration[3]{};
		float angularVelocity[3]{};
	};

	void Init();
	void Shutdown();
	void Reconfigure();

	bool IsConnected(int player);
	void Poll(int player, PadSnapshot& snapshot);
	void SetSnapshotFilter(void (*filter)(int player, PadSnapshot& snapshot));
	void SetRumble(int player, float amplitude);
	void StopAllRumble();
	uint64_t GetHeldButtons();

	bool GetTouch(int& x, int& y);
}
