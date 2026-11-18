// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cemu/FileCache/FileCache.h"
#include "Common/FileStream.h"
#include "Common/switch/HorizonApplet.h"
#include "Common/switch/HorizonDiagnostics.h"
#include "Common/switch/HorizonNetwork.h"
#include "config/LaunchSettings.h"
#include "gui/interface/WindowSystem.h"
#include "input/InputManager.h"
#include "switch/EmulationSwitch.h"
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

static void RunTitle()
{
	const uint64 flipsAtLaunch = LatteGPUState.flipRequestCount.load();
	bool boosting = true;
	while (HorizonApplet::Update() && !EmulationSwitch::HasTitleExited())
	{
		WindowSystem::GetWindowInfo().app_active = HorizonApplet::IsInFocus();
		EmulationSwitch::Update();
		if (boosting && LatteGPUState.flipRequestCount.load() != flipsAtLaunch)
		{
			HorizonApplet::EndBoost();
			boosting = false;
		}
		FileStream::FlushStaleWrites();
		std::this_thread::sleep_for(FRONTEND_POLL_INTERVAL);
	}
	if (boosting)
		HorizonApplet::EndBoost();
	EmulationSwitch::StopTitle();
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
	std::atexit(HorizonNetwork::Shutdown);
	std::atexit(FileStream::FlushAllWrites);
	std::atexit(FileCache::ShutdownAsyncWriter);
	if (std::optional<int> exitCode = LaunchSettings::HandleCommandline(argc, argv))
		return *exitCode;

	HorizonApplet::Init();
	SetHostThreadRole(HostThreadRole::Frontend);
	if (!HorizonApplet::IsApplication())
		HorizonDiagnostics::Log("Not running as an application!!");

	HorizonApplet::BeginBoost();
	if (!EmulationSwitch::Initialise())
	{
		HorizonApplet::EndBoost();
		Shutdown();
		return 1;
	}

	std::optional<fs::path> launchPath = LaunchSettings::GetLoadFile();
	if (!launchPath)
	{
		cemuLog_log(LogType::Force, "No title given");
		HorizonApplet::EndBoost();
	}
	else if (std::string error = EmulationSwitch::LaunchTitle(*launchPath); !error.empty())
	{
		WindowSystem::ShowErrorDialog(error, "Launch failed");
		HorizonApplet::EndBoost();
	}
	else
		RunTitle();

	Shutdown();
	HorizonDiagnostics::Log("Reached the end of main");
	return 0;
}
