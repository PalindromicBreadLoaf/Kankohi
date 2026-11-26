// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonApplet.h"
#include "HorizonDiagnostics.h"

#include <atomic>

#include <switch.h>

namespace
{
	AppletHookCookie s_hookCookie;
	std::atomic_bool s_exitRequested = false;
	std::atomic_bool s_inFocus = true;
	std::atomic_uint32_t s_resumeCount = 0;
	bool s_initialised = false;
	bool s_romfsMounted = false;
	int s_boostCount = 0;

	void OnAppletHook(AppletHookType hook, void*)
	{
		switch (hook)
		{
		case AppletHookType_OnExitRequest:
			HorizonDiagnostics::Log("applet: exit requested");
			s_exitRequested = true;
			break;
		case AppletHookType_OnFocusState:
			s_inFocus = appletGetFocusState() == AppletFocusState_InFocus;
			break;
		case AppletHookType_OnResume:
			HorizonDiagnostics::Log("applet: resumed");
			s_resumeCount++;
			break;
		case AppletHookType_OnOperationMode:
			HorizonDiagnostics::Log("applet: operation mode %s", HorizonApplet::IsDocked() ? "docked" : "handheld");
			break;
		default:
			break;
		}
	}
}

namespace HorizonApplet
{
	void Init()
	{
		appletLockExit();
		appletHook(&s_hookCookie, OnAppletHook, nullptr);
		appletSetRestartMessageEnabled(true);
		if (IsApplication())
			appletSetAutoSleepDisabled(true);

		const Result romfsResult = romfsInit();
		s_romfsMounted = R_SUCCEEDED(romfsResult);
		if (!s_romfsMounted)
			HorizonDiagnostics::Log("applet: romfsInit failed (%#x)", romfsResult);

		s_initialised = true;
		HorizonDiagnostics::Log("applet: type %d, %s, %s", static_cast<int>(appletGetAppletType()),
			IsApplication() ? "application" : "not an application", IsDocked() ? "docked" : "handheld");
	}

	void Shutdown()
	{
		if (!s_initialised)
			return;
		if (s_boostCount > 0)
		{
			s_boostCount = 1;
			EndBoost();
		}
		if (s_romfsMounted)
			romfsExit();
		s_romfsMounted = false;
		if (IsApplication())
			appletSetAutoSleepDisabled(false);
		appletUnhook(&s_hookCookie);
		appletUnlockExit();
		s_initialised = false;
	}

	bool Update()
	{
		if (!appletMainLoop())
			s_exitRequested = true;
		return !s_exitRequested;
	}

	void RequestExit()
	{
		s_exitRequested = true;
	}

	bool IsApplication()
	{
		switch (appletGetAppletType())
		{
		case AppletType_Application:
		case AppletType_SystemApplication:
			return true;
		default:
			return false;
		}
	}

	bool IsInFocus()
	{
		return s_inFocus;
	}

	bool IsRomfsMounted()
	{
		return s_romfsMounted;
	}

	bool IsDocked()
	{
		return appletGetOperationMode() == AppletOperationMode_Console;
	}

	uint32_t GetResumeCount()
	{
		return s_resumeCount;
	}

	void GetDisplaySize(int& width, int& height)
	{
		width = IsDocked() ? 1920 : 1280;
		height = IsDocked() ? 1080 : 720;
	}

	bool ResizeWindow(int width, int height)
	{
		u32 currentWidth = 0, currentHeight = 0;
		nwindowGetDimensions(nwindowGetDefault(), &currentWidth, &currentHeight);
		if (currentWidth == static_cast<u32>(width) && currentHeight == static_cast<u32>(height))
			return true;
		const Result result = nwindowSetDimensions(nwindowGetDefault(), width, height);
		if (R_FAILED(result))
		{
			HorizonDiagnostics::Log("applet: nwindowSetDimensions(%d, %d) failed (%#x)", width, height, result);
			return false;
		}
		return true;
	}

	void* GetWindow()
	{
		return nwindowGetDefault();
	}

	void BeginBoost()
	{
		if (s_boostCount++ != 0)
			return;
		const Result result = appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
		HorizonDiagnostics::Log("applet: CPU boost on (%#x)", result);
	}

	void EndBoost()
	{
		if (s_boostCount == 0 || --s_boostCount != 0)
			return;
		const Result result = appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
		HorizonDiagnostics::Log("applet: CPU boost off (%#x)", result);
	}
}
