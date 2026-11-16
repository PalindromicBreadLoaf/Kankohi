// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Common/switch/HorizonDiagnostics.h"
#include "config/LaunchSettings.h"

static constexpr const char* LOG_DIRECTORY = "sdmc:/switch/kankohi/logs";

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

int main(int argc, char* argv[])
{
	int exitCode = LaunchSettings::HandleCommandline(argc, argv).value_or(0);

	HorizonDiagnostics::Log("Reached main, exiting with %d", exitCode);
	return exitCode;
}
