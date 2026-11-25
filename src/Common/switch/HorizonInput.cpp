// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonInput.h"
#include "HorizonDiagnostics.h"

#include <array>
#include <atomic>
#include <mutex>

#include <switch.h>

namespace
{
	constexpr float VIBRATION_FREQ_LOW = 160.0f;
	constexpr float VIBRATION_FREQ_HIGH = 320.0f;

	constexpr std::array<HidNpadStyleTag, 6> STYLES{
		HidNpadStyleTag_NpadFullKey, HidNpadStyleTag_NpadHandheld, HidNpadStyleTag_NpadJoyDual,
		HidNpadStyleTag_NpadJoyLeft, HidNpadStyleTag_NpadJoyRight, HidNpadStyleTag_NpadGc,
	};

	struct Slot
	{
		std::mutex mutex;
		PadState pad{};
		HidNpadIdType npadId = HidNpadIdType_No1;

		HidNpadIdType handleId = HidNpadIdType_Other;
		HidNpadStyleTag handleStyle{};
		std::array<HidSixAxisSensorHandle, 2> sixAxis{};
		std::array<HidVibrationDeviceHandle, 2> vibration{};
		int sixAxisCount = 0;
		int vibrationCount = 0;
		bool vibrationIsGcErm = false;

		uint64_t lastSamplingNumber = 0;
		uint64_t lastSampleTick = 0;
		uint64_t motionSampleCount = 0;
		bool logNextSample = false;

		std::atomic<float> rumble = 0.0f;
		float sentRumble = 0.0f;
		std::atomic<uint64_t> heldButtons = 0;
	};

	std::array<Slot, HorizonInput::MAX_PLAYERS> s_slots;
	bool s_initialised = false;

	bool SupportsTwoVibrationDevices(HidNpadStyleTag style)
	{
		return style == HidNpadStyleTag_NpadFullKey || style == HidNpadStyleTag_NpadHandheld ||
			   style == HidNpadStyleTag_NpadJoyDual;
	}

	void SendRumble(Slot& slot, float amplitude)
	{
		slot.sentRumble = amplitude;
		if (slot.vibrationCount == 0)
			return;
		if (slot.vibrationIsGcErm)
		{
			hidSendVibrationGcErmCommand(slot.vibration[0],
				amplitude > 0.5f ? HidVibrationGcErmCommand_Start : HidVibrationGcErmCommand_Stop);
			return;
		}
		const HidVibrationValue value{amplitude, VIBRATION_FREQ_LOW, amplitude, VIBRATION_FREQ_HIGH};
		const std::array<HidVibrationValue, 2> values{value, value};
		hidSendVibrationValues(slot.vibration.data(), values.data(), slot.vibrationCount);
	}

	void ReleaseHandles(Slot& slot)
	{
		if (slot.sentRumble != 0.0f)
			SendRumble(slot, 0.0f);
		for (int i = 0; i < slot.sixAxisCount; i++)
			hidStopSixAxisSensor(slot.sixAxis[i]);
		slot.sixAxisCount = 0;
		slot.vibrationCount = 0;
		slot.vibrationIsGcErm = false;
		slot.handleId = HidNpadIdType_Other;
		slot.handleStyle = HidNpadStyleTag{};
	}

	void RefreshHandles(Slot& slot, int player)
	{
		const HidNpadIdType id = padIsHandheld(&slot.pad) ? HidNpadIdType_Handheld : slot.npadId;
		const u32 styleSet = hidGetNpadStyleSet(id);
		HidNpadStyleTag style{};
		for (const HidNpadStyleTag candidate : STYLES)
		{
			if (styleSet & candidate)
			{
				style = candidate;
				break;
			}
		}
		if (id == slot.handleId && style == slot.handleStyle)
			return;

		ReleaseHandles(slot);
		slot.handleId = id;
		slot.handleStyle = style;
		if (style == HidNpadStyleTag{})
			return;

		const int sixAxisCount = style == HidNpadStyleTag_NpadJoyDual ? 2 : 1;
		if (R_SUCCEEDED(hidGetSixAxisSensorHandles(slot.sixAxis.data(), sixAxisCount, id, style)))
		{
			slot.sixAxisCount = sixAxisCount;
			for (int i = 0; i < sixAxisCount; i++)
				hidStartSixAxisSensor(slot.sixAxis[i]);
			slot.lastSamplingNumber = 0;
			slot.lastSampleTick = 0;
			slot.logNextSample = true;
		}

		const int vibrationCount = SupportsTwoVibrationDevices(style) ? 2 : 1;
		if (R_SUCCEEDED(hidInitializeVibrationDevices(slot.vibration.data(), vibrationCount, id, style)))
		{
			slot.vibrationCount = vibrationCount;
			HidVibrationDeviceInfo info{};
			slot.vibrationIsGcErm = R_SUCCEEDED(hidGetVibrationDeviceInfo(slot.vibration[0], &info)) &&
									info.type == HidVibrationDeviceType_GcErm;
		}
		slot.sentRumble = -1.0f;

		HorizonDiagnostics::Log("input: player %d is style %#x, %d sixaxis, %d vibration", player + 1,
			static_cast<unsigned>(style), slot.sixAxisCount, slot.vibrationCount);
	}

	void ReadMotion(Slot& slot, int player, HorizonInput::PadSnapshot& snapshot)
	{
		if (slot.sixAxisCount == 0)
			return;
		// The right Joy-Con of a pair holds the pointer
		HidSixAxisSensorState state{};
		if (hidGetSixAxisSensorStates(slot.sixAxis[slot.sixAxisCount - 1], &state, 1) != 1)
			return;
		snapshot.hasMotion = true;
		if (state.sampling_number == slot.lastSamplingNumber)
			return;

		const uint64_t tick = armGetSystemTick();
		const bool first = slot.lastSampleTick == 0;
		snapshot.motionDeltaSeconds = first ? 0.0f : static_cast<float>(armTicksToNs(tick - slot.lastSampleTick)) / 1e9f;
		slot.lastSampleTick = tick;
		slot.lastSamplingNumber = state.sampling_number;
		if (first)
			return;

		slot.motionSampleCount++;
		snapshot.acceleration[0] = state.acceleration.x;
		snapshot.acceleration[1] = state.acceleration.y;
		snapshot.acceleration[2] = state.acceleration.z;
		snapshot.angularVelocity[0] = state.angular_velocity.x;
		snapshot.angularVelocity[1] = state.angular_velocity.y;
		snapshot.angularVelocity[2] = state.angular_velocity.z;
		if (slot.logNextSample)
		{
			slot.logNextSample = false;
			HorizonDiagnostics::Log("input: player %d motion sample accel (%.3f, %.3f, %.3f) g, gyro (%.3f, %.3f, %.3f) rot/s",
				player + 1, state.acceleration.x, state.acceleration.y, state.acceleration.z,
				state.angular_velocity.x, state.angular_velocity.y, state.angular_velocity.z);
		}
	}
}

namespace HorizonInput
{
	void Init()
	{
		if (s_initialised)
			return;
		padConfigureInput(MAX_PLAYERS, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadGc);
		hidSetNpadJoyHoldType(HidNpadJoyHoldType_Vertical);
		hidInitializeTouchScreen();
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			Slot& slot = s_slots[i];
			std::scoped_lock lock(slot.mutex);
			slot.npadId = static_cast<HidNpadIdType>(HidNpadIdType_No1 + i);
			u64 mask = 1ULL << slot.npadId;
			if (i == 0)
				mask |= 1ULL << HidNpadIdType_Handheld;
			padInitializeWithMask(&slot.pad, mask);
		}
		s_initialised = true;
	}

	void Reconfigure()
	{
		if (!s_initialised)
			return;
		padConfigureInput(MAX_PLAYERS, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadGc);
		hidSetNpadJoyHoldType(HidNpadJoyHoldType_Vertical);
		for (Slot& slot : s_slots)
		{
			std::scoped_lock lock(slot.mutex);
			ReleaseHandles(slot);
		}
	}

	void Shutdown()
	{
		if (!s_initialised)
			return;
		for (Slot& slot : s_slots)
		{
			std::scoped_lock lock(slot.mutex);
			ReleaseHandles(slot);
		}
		s_initialised = false;
	}

	bool IsConnected(int player)
	{
		if (player < 0 || player >= MAX_PLAYERS)
			return false;
		if (hidGetNpadStyleSet(static_cast<HidNpadIdType>(HidNpadIdType_No1 + player)) != 0)
			return true;
		return player == 0 && hidGetNpadStyleSet(HidNpadIdType_Handheld) != 0;
	}

	void Poll(int player, PadSnapshot& snapshot)
	{
		if (!s_initialised || player < 0 || player >= MAX_PLAYERS)
		{
			snapshot = {};
			return;
		}
		Slot& slot = s_slots[player];
		std::scoped_lock lock(slot.mutex);
		padUpdate(&slot.pad);

		snapshot.connected = padIsConnected(&slot.pad);
		snapshot.buttons = snapshot.connected ? padGetButtons(&slot.pad) & BUTTON_MASK : 0;
		slot.heldButtons.store(snapshot.buttons, std::memory_order_relaxed);
		for (unsigned i = 0; i < 2; i++)
		{
			const HidAnalogStickState stick = padGetStickPos(&slot.pad, i);
			snapshot.sticks[i * 2] = static_cast<float>(stick.x) / JOYSTICK_MAX;
			snapshot.sticks[i * 2 + 1] = static_cast<float>(stick.y) / JOYSTICK_MAX;
			snapshot.triggers[i] = static_cast<float>(padGetGcTriggerPos(&slot.pad, i)) / 0x7fff;
		}

		if (!snapshot.connected)
		{
			ReleaseHandles(slot);
			snapshot.hasMotion = false;
			return;
		}
		RefreshHandles(slot, player);
		snapshot.hasMotion = false;
		ReadMotion(slot, player, snapshot);
		snapshot.motionSampleCount = slot.motionSampleCount;

		const float rumble = slot.rumble.load(std::memory_order_relaxed);
		if (rumble != slot.sentRumble)
			SendRumble(slot, rumble);
	}

	void SetRumble(int player, float amplitude)
	{
		if (player >= 0 && player < MAX_PLAYERS)
			s_slots[player].rumble.store(amplitude, std::memory_order_relaxed);
	}

	void StopAllRumble()
	{
		for (Slot& slot : s_slots)
			slot.rumble.store(0.0f, std::memory_order_relaxed);
	}

	uint64_t GetHeldButtons()
	{
		uint64_t buttons = 0;
		for (const Slot& slot : s_slots)
			buttons |= slot.heldButtons.load(std::memory_order_relaxed);
		return buttons;
	}

	bool GetTouch(int& x, int& y)
	{
		if (!s_initialised)
			return false;
		HidTouchScreenState state{};
		if (hidGetTouchScreenStates(&state, 1) != 1 || state.count == 0)
			return false;
		x = static_cast<int>(state.touches[0].x);
		y = static_cast<int>(state.touches[0].y);
		return true;
	}
}
