// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "switch/ShellSwitch.h"
#include "switch/ShellGameListSwitch.h"
#include "switch/ShellLibrarySwitch.h"

#include <cstdio>

#include <borealis.hpp>
#include <yoga/event/event.h>

#include <switch.h>

namespace Shell
{
	namespace
	{
		constexpr const char* LOG_PATH = "sdmc:/switch/kankohi/logs/borealis.log";

		uint64_t s_lastChosen = 0;

		class Services
		{
		public:
			Services()
			{
				plInitialize(PlServiceType_User);
				setsysInitialize();
				setInitialize();
				psmInitialize();
				nifmInitialize(NifmServiceType_User);
				lblInitialize();
			}

			~Services()
			{
				lblExit();
				nifmExit();
				psmExit();
				setExit();
				setsysExit();
				plExit();
			}

			Services(const Services&) = delete;
			Services& operator=(const Services&) = delete;
		};

		class LogFile
		{
		public:
			LogFile() : m_file(std::fopen(LOG_PATH, "w"))
			{
				if (!m_file)
					return;
				std::setvbuf(m_file, nullptr, _IOLBF, 0);
				brls::Logger::setLogOutput(m_file);
			}

			~LogFile()
			{
				if (!m_file)
					return;
				brls::Logger::setLogOutput(stdout);
				std::fclose(m_file);
			}

			LogFile(const LogFile&) = delete;
			LogFile& operator=(const LogFile&) = delete;

		private:
			std::FILE* m_file;
		};

		void RestoreAppletState()
		{
			if (appletGetAppletType() != AppletType_Application)
				return;
			appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleep);
			appletSetWirelessPriorityMode(AppletWirelessPriorityMode_Default);
		}
	}

	std::optional<std::string> Run(const std::string& notice)
	{
		Services services;
		LogFile logFile;

		if (!brls::Application::init())
			return std::nullopt;
		brls::Application::createWindow("Kankohi");
		brls::Application::setGlobalQuit(true);

		std::optional<ShellLibrary::Title> chosen;
		ShellLibrary::Start();
		auto* frame = new brls::AppletFrame(new GameListView(chosen, s_lastChosen));
		frame->setTitle("Kankohi");
		brls::Application::pushActivity(new brls::Activity(frame));
		if (!notice.empty())
		{
			brls::sync([notice] {
				auto* dialog = new brls::Dialog(notice);
				dialog->addButton("OK", [] {});
				dialog->open();
			});
		}

		while (brls::Application::mainLoop())
		{
		}

		ShellLibrary::Stop();
		brls::Threading::getSyncFunctions()->clear();
		facebook::yoga::Event::reset();
		RestoreAppletState();

		if (!chosen)
			return std::nullopt;
		s_lastChosen = chosen->titleId;
		return chosen->path;
	}
}
