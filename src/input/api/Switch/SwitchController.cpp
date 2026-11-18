// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "input/api/Switch/SwitchController.h"

SwitchController::SwitchController(sint32 player)
	: base_type(fmt::format("{}", player), fmt::format("Player {}", player + 1)), m_player(player)
{
}

SwitchController::~SwitchController()
{
	HorizonInput::SetRumble(m_player, 0.0f);
}

void SwitchController::update()
{
	HorizonInput::Poll(m_player, m_polled);

	std::optional<MotionSample> motionSample;
	if (m_polled.hasMotion && m_polled.motionSampleCount != m_lastMotionSample)
	{
		m_lastMotionSample = m_polled.motionSampleCount;
		// HID is (x, y, z) = (right, forward, up) with acceleration pointing towards gravity.
		constexpr float TAU = 6.28318531f;
		const float* acc = m_polled.acceleration;
		const float* gyro = m_polled.angularVelocity;
		m_motionHandler.processMotionSample(std::min(m_polled.motionDeltaSeconds, 1.0f),
			gyro[0] * TAU, -gyro[2] * TAU, gyro[1] * TAU, acc[0], -acc[2], acc[1]);
		motionSample = m_motionHandler.getMotionSample();
	}

	std::scoped_lock lock(m_publishedMutex);
	m_published = m_polled;
	if (motionSample)
		m_motionSample = *motionSample;
}

bool SwitchController::is_connected()
{
	std::scoped_lock lock(m_publishedMutex);
	return m_published.connected;
}

MotionSample SwitchController::get_motion_sample()
{
	std::scoped_lock lock(m_publishedMutex);
	return m_motionSample;
}

void SwitchController::start_rumble()
{
	const float rumble = get_settings().rumble;
	if (rumble > 0.0f)
		HorizonInput::SetRumble(m_player, rumble);
}

void SwitchController::stop_rumble()
{
	HorizonInput::SetRumble(m_player, 0.0f);
}

std::string SwitchController::get_button_name(uint64 button) const
{
	switch (button)
	{
	case kSwitchButton_A: return "A";
	case kSwitchButton_B: return "B";
	case kSwitchButton_X: return "X";
	case kSwitchButton_Y: return "Y";
	case kSwitchButton_StickL: return "L stick";
	case kSwitchButton_StickR: return "R stick";
	case kSwitchButton_L: return "L";
	case kSwitchButton_R: return "R";
	case kSwitchButton_ZL: return "ZL";
	case kSwitchButton_ZR: return "ZR";
	case kSwitchButton_Plus: return "Plus";
	case kSwitchButton_Minus: return "Minus";
	case kSwitchButton_Left: return "D-pad left";
	case kSwitchButton_Up: return "D-pad up";
	case kSwitchButton_Right: return "D-pad right";
	case kSwitchButton_Down: return "D-pad down";
	case kSwitchButton_LeftSL: return "Left SL";
	case kSwitchButton_LeftSR: return "Left SR";
	case kSwitchButton_RightSL: return "Right SL";
	case kSwitchButton_RightSR: return "Right SR";
	}
	return base_type::get_button_name(button);
}

ControllerState SwitchController::raw_state()
{
	HorizonInput::PadSnapshot snapshot;
	{
		std::scoped_lock lock(m_publishedMutex);
		snapshot = m_published;
	}
	ControllerState result{};
	if (!snapshot.connected)
		return result;
	for (uint64 buttons = snapshot.buttons; buttons != 0; buttons &= buttons - 1)
		result.buttons.SetButtonState(std::countr_zero(buttons), true);
	result.axis = {snapshot.sticks[0], -snapshot.sticks[1]};
	result.rotation = {snapshot.sticks[2], -snapshot.sticks[3]};
	result.trigger = {snapshot.triggers[0], snapshot.triggers[1]};
	return result;
}
