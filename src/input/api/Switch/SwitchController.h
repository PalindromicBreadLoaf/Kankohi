// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "input/api/Controller.h"
#include "input/api/Switch/SwitchControllerProvider.h"
#include "input/motion/MotionHandler.h"
#include "Common/switch/HorizonInput.h"

enum SwitchButton : uint64
{
	kSwitchButton_A = 0,
	kSwitchButton_B = 1,
	kSwitchButton_X = 2,
	kSwitchButton_Y = 3,
	kSwitchButton_StickL = 4,
	kSwitchButton_StickR = 5,
	kSwitchButton_L = 6,
	kSwitchButton_R = 7,
	kSwitchButton_ZL = 8,
	kSwitchButton_ZR = 9,
	kSwitchButton_Plus = 10,
	kSwitchButton_Minus = 11,
	kSwitchButton_Left = 12,
	kSwitchButton_Up = 13,
	kSwitchButton_Right = 14,
	kSwitchButton_Down = 15,
	kSwitchButton_LeftSL = 24,
	kSwitchButton_LeftSR = 25,
	kSwitchButton_RightSL = 26,
	kSwitchButton_RightSR = 27,
};

class SwitchController : public Controller<SwitchControllerProvider>
{
public:
	explicit SwitchController(sint32 player);
	~SwitchController() override;

	std::string_view api_name() const override
	{
		static_assert(to_string(InputAPI::Switch) == "Switch");
		return to_string(InputAPI::Switch);
	}
	InputAPI::Type api() const override { return InputAPI::Switch; }

	void update() override;
	bool is_connected() override;

	bool has_motion() override { return true; }
	MotionSample get_motion_sample() override;

	bool has_rumble() override { return true; }
	void start_rumble() override;
	void stop_rumble() override;

	std::string get_button_name(uint64 button) const override;

protected:
	ControllerState raw_state() override;

private:
	const sint32 m_player;

	HorizonInput::PadSnapshot m_polled{};
	uint64 m_lastMotionSample = 0;
	WiiUMotionHandler m_motionHandler;

	std::mutex m_publishedMutex;
	HorizonInput::PadSnapshot m_published{};
	MotionSample m_motionSample{};
};
