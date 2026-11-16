// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/interface/WindowSystem.h"

static WindowSystem::WindowInfo s_windowInfo;

void WindowSystem::ShowErrorDialog(std::string_view message, std::string_view title, std::optional<ErrorCategory> errorCategory)
{
	cemuLog_log(LogType::Force, "Error dialog: {}: {}", title, message);
}

WindowSystem::WindowInfo& WindowSystem::GetWindowInfo()
{
	return s_windowInfo;
}

void WindowSystem::UpdateWindowTitles(bool isIdle, bool isLoading, double fps)
{
}

void WindowSystem::GetWindowSize(int& w, int& h)
{
	w = s_windowInfo.width;
	h = s_windowInfo.height;
}

void WindowSystem::GetPadWindowSize(int& w, int& h)
{
	w = s_windowInfo.pad_width;
	h = s_windowInfo.pad_height;
}

void WindowSystem::GetWindowPhysSize(int& w, int& h)
{
	w = s_windowInfo.phys_width;
	h = s_windowInfo.phys_height;
}

void WindowSystem::GetPadWindowPhysSize(int& w, int& h)
{
	w = s_windowInfo.phys_pad_width;
	h = s_windowInfo.phys_pad_height;
}

double WindowSystem::GetWindowDPIScale()
{
	return 1.0;
}

double WindowSystem::GetPadDPIScale()
{
	return 1.0;
}

bool WindowSystem::IsPadWindowOpen()
{
	return false;
}

bool WindowSystem::IsKeyDown(uint32 key)
{
	return s_windowInfo.get_keystate(key);
}

bool WindowSystem::IsKeyDown(PlatformKeyCodes key)
{
	return false;
}

std::string WindowSystem::GetKeyCodeName(uint32 key)
{
	return fmt::format("key {}", key);
}

bool WindowSystem::InputConfigWindowHasFocus()
{
	return false;
}

void WindowSystem::NotifyGameLoaded()
{
}

void WindowSystem::NotifyGameExited()
{
}

void WindowSystem::RefreshGameList()
{
}

bool WindowSystem::IsFullScreen()
{
	return true;
}

void WindowSystem::CaptureInput(const ControllerState& currentState, const ControllerState& lastState)
{
}
