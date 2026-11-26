// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cemu/FileCache/FileCache.h"
#include "Common/FileStream.h"
#include "Common/switch/HorizonApplet.h"
#include "Common/switch/HorizonDiagnostics.h"
#include "Common/switch/HorizonInput.h"
#include "Common/switch/HorizonNetwork.h"
#include "config/LaunchSettings.h"
#include "gui/interface/WindowSystem.h"
#include "input/InputManager.h"
#include "switch/EmulationSwitch.h"
#include "switch/PauseMenuSwitch.h"
#include "switch/ShellSwitch.h"
#include "util/ThreadPool/ThreadPool.h"
#include "util/helpers/helpers.h"

static constexpr const char* LOG_DIRECTORY = "sdmc:/switch/kankohi/logs";
static constexpr auto FRONTEND_POLL_INTERVAL = std::chrono::milliseconds(10);

static void LogUncaughtException()
{
	if (std::exception_ptr exception = std::current_exception())
	{
		try
		{
			std::rethrow_exception(exception);
		}
		catch (const std::exception& e)
		{
			HorizonDiagnostics::Log("Uncaught exception: %s", e.what());
		}
		catch (...)
		{
			HorizonDiagnostics::Log("Uncaught exception of unknown type");
		}
	}
	else
		HorizonDiagnostics::Log("std::terminate called without an exception");
	cemuLog_waitForFlush();
	std::abort();
}

// libnx never runs .fini_array
// I feel it should, but whatever
static void ShutdownDiagnostics()
{
	HorizonDiagnostics::AuditThreads("exit");
	HorizonDiagnostics::AuditHeap("exit");
	HorizonDiagnostics::Shutdown();
}

__attribute__((constructor(101))) static void InitDiagnostics()
{
	HorizonDiagnostics::Init(LOG_DIRECTORY, "kankohi");
	std::set_terminate(LogUncaughtException);
	std::atexit(ShutdownDiagnostics);
}

static void RunTitle(const std::string& exitLabel)
{
	PauseMenu pauseMenu(CafeSystem::GetForegroundTitleName(), exitLabel);
	const uint64 flipsAtLaunch = LatteGPUState.flipRequestCount.load();
	bool boosting = true;
	while (HorizonApplet::Update() && !EmulationSwitch::HasTitleExited() && !pauseMenu.IsExitRequested())
	{
		WindowSystem::GetWindowInfo().app_active = HorizonApplet::IsInFocus();
		EmulationSwitch::Update();
		if (boosting && LatteGPUState.flipRequestCount.load() != flipsAtLaunch)
		{
			HorizonApplet::EndBoost();
			boosting = false;
		}
		pauseMenu.Update(!boosting);
		FileStream::FlushStaleWrites();
		std::this_thread::sleep_for(FRONTEND_POLL_INTERVAL);
	}
	if (boosting)
		HorizonApplet::EndBoost();
	EmulationSwitch::StopTitle();
	HorizonDiagnostics::AuditThreads("title stopped");
	HorizonDiagnostics::AuditHeap("title stopped");
}

static std::string LaunchAndRun(const fs::path& path, const std::string& exitLabel)
{
	HorizonApplet::BeginBoost();
	std::string error = EmulationSwitch::LaunchTitle(path);
	if (!error.empty())
	{
		WindowSystem::ShowErrorDialog(error, "Launch failed");
		HorizonApplet::EndBoost();
		return error;
	}
	RunTitle(exitLabel);
	return {};
}

static void RunShell()
{
	std::string notice;
	while (HorizonApplet::Update())
	{
		std::optional<std::string> chosen = Shell::Run(notice);
		HorizonInput::Reconfigure();
		if (!chosen || !HorizonApplet::Update())
			break;
		notice = LaunchAndRun(_utf8ToPath(*chosen), "Exit to library");
	}
}

static void Shutdown()
{
	EmulationSwitch::Shutdown();
	InputManager::instance().Shutdown();
	ThreadPool::JoinAll();
	HorizonApplet::Shutdown();
}

int main(int argc, char* argv[])
{
	std::atexit(cemuLog_shutdown);
	std::atexit(HorizonNetwork::Shutdown);
	std::atexit(FileStream::FlushAllWrites);
	std::atexit(FileCache::ShutdownAsyncWriter);
	if (std::optional<int> exitCode = LaunchSettings::HandleCommandline(argc, argv))
		return *exitCode;

	HorizonApplet::Init();
	SetThreadName("Frontend");
	SetHostThreadRole(HostThreadRole::Frontend);
	if (!HorizonApplet::IsApplication())
		HorizonDiagnostics::Log("Not running as an application!!");

	HorizonApplet::BeginBoost();
	const bool initialised = EmulationSwitch::Initialise();
	HorizonApplet::EndBoost();
	if (!initialised)
	{
		Shutdown();
		return 1;
	}

	if (std::optional<fs::path> launchPath = LaunchSettings::GetLoadFile())
		LaunchAndRun(*launchPath, "Quit");
	else
		RunShell();

	Shutdown();
	HorizonDiagnostics::Log("Reached the end of main");
	return 0;
}
