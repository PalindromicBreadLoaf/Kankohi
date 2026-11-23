// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace HorizonApplet
{
	// Locks exit, disables auto-sleep, mounts romfs
	void Init();
	void Shutdown();

	bool Update();
	void RequestExit();

	bool IsApplication();
	bool IsInFocus();
	bool IsRomfsMounted();
	bool IsDocked();

	void GetDisplaySize(int& width, int& height);
	bool ResizeWindow(int width, int height);
	void* GetWindow();

	void BeginBoost();
	void EndBoost();
}
