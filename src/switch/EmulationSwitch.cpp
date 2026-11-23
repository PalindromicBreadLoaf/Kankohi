// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "switch/EmulationSwitch.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/TitleList/TitleInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cemu/ncrypto/ncrypto.h"
#include "Common/switch/HorizonApplet.h"
#include "audio/AudrenAPI.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"
#include "gui/interface/WindowSystem.h"
#include "input/InputManager.h"
#include "input/api/Switch/SwitchController.h"
#include "switch/KeyboardSwitch.h"

#include <condition_variable>
#include <initializer_list>

void CemuCommonInit();

namespace
{
	class SystemImplementationSwitch : public CafeSystem::SystemImplementation
	{
	public:
		void CafeRecreateCanvas() override
		{
			std::unique_lock lock(m_mutex);
			m_recreateRequested = true;
			m_condition.wait(lock, [this] { return !m_recreateRequested; });
		}

		void CafePPCProcessExit() override
		{
			m_exited = true;
		}

		void ServiceRecreate();

		std::atomic_bool m_exited = false;

	private:
		std::mutex m_mutex;
		std::condition_variable m_condition;
		bool m_recreateRequested = false;
	};

	SystemImplementationSwitch s_systemImplementation;
	bool s_initialised = false;

	bool CreateDirectoriesIfMissing(const fs::path& path)
	{
		std::error_code ec;
		if (fs::exists(path, ec))
			return true;
		return fs::create_directories(path, ec);
	}

	bool WriteFileIfMissing(const fs::path& path, std::string_view content)
	{
		std::error_code ec;
		if (fs::exists(path, ec))
			return true;
		std::unique_ptr<FileStream> file(FileStream::createFile2(path));
		if (!file)
			return false;
		file->writeData(content.data(), content.size());
		return true;
	}

	bool CreateDefaultMlcFiles(const fs::path& mlc)
	{
		for (const char* directory : {"sys", "usr", "usr/title/00050000", "usr/title/0005000c", "usr/title/0005000e",
				 "usr/save/00050010/1004a000/user/common/db", "usr/save/00050010/1004a100/user/common/db",
				 "usr/save/00050010/1004a200/user/common/db", "sys/title/0005001b/1005c000/content"})
		{
			if (!CreateDirectoriesIfMissing(mlc / directory))
				return false;
		}
		const fs::path languagePath = mlc / "sys/title/0005001b/1005c000/content";
		std::string languages;
		for (const char* language : {"ja", "en", "fr", "de", "it", "es", "zh", "ko", "nl", "pt", "ru", "zh"})
			languages.append(fmt::format("\"{}\",\n", language));
		std::string countries;
		for (sint32 i = 0; i < (sint32)NCrypto::GetCountryCount(); i++)
		{
			const char* countryCode = NCrypto::GetCountryAsString(i);
			if (boost::iequals(countryCode, "NN"))
				countries.append("NULL,\n");
			else
				countries.append(fmt::format("\"{}\",\n", countryCode));
		}
		return WriteFileIfMissing(languagePath / "language.txt", languages) &&
			   WriteFileIfMissing(languagePath / "country.txt", countries);
	}

	void UpdateWindowInfo(int width, int height)
	{
		auto& windowInfo = WindowSystem::GetWindowInfo();
		windowInfo.width = windowInfo.phys_width = width;
		windowInfo.height = windowInfo.phys_height = height;
		windowInfo.dpi_scale = 1.0;
		windowInfo.pad_open = false;
		windowInfo.is_fullscreen = true;
		for (auto* handle : {&windowInfo.window_main, &windowInfo.canvas_main})
		{
			handle->backend = WindowSystem::WindowHandleInfo::Backend::NWindow;
			handle->display = nullptr;
			handle->surface = HorizonApplet::GetWindow();
		}
	}

	bool CreateRenderer()
	{
		int width, height;
		HorizonApplet::GetDisplaySize(width, height);
		UpdateWindowInfo(width, height);
		try
		{
			g_renderer = std::make_unique<VulkanRenderer>();
			VulkanRenderer::GetInstance()->InitializeSurface({width, height}, true);
		}
		catch (const std::exception& ex)
		{
			cemuLog_log(LogType::Force, "Error when initialising the Vulkan renderer: {}", ex.what());
			g_renderer.reset();
			return false;
		}
		cemuLog_log(LogType::Force, "Vulkan renderer up at {}x{}", width, height);
		return true;
	}

	void CreateDefaultController()
	{
		InputManager& input = InputManager::instance();
		if (input.get_controller(0))
			return;
		auto controller = std::make_shared<SwitchController>(0);
		controller->set_use_motion(true);
		controller->set_rumble(0.5f);
		const EmulatedControllerPtr gamepad = input.set_controller(0, EmulatedController::Type::VPAD, controller);
		if (!gamepad)
			return;
		gamepad->set_default_mapping(controller);
		input.save();
		cemuLog_log(LogType::Force, "Mapped the GamePad to player 1's controller");
	}

	void FollowDisplaySize()
	{
		int width, height;
		HorizonApplet::GetDisplaySize(width, height);
		auto& windowInfo = WindowSystem::GetWindowInfo();
		if (windowInfo.phys_width == width && windowInfo.phys_height == height)
			return;
		cemuLog_log(LogType::Force, "Display changed to {}x{}", width, height);
		windowInfo.width = windowInfo.phys_width = width;
		windowInfo.height = windowInfo.phys_height = height;
	}

	void SystemImplementationSwitch::ServiceRecreate()
	{
		std::unique_lock lock(m_mutex);
		if (!m_recreateRequested)
			return;
		g_renderer.reset();
		CreateRenderer();
		m_recreateRequested = false;
		m_condition.notify_all();
	}
}

namespace EmulationSwitch
{
	bool Initialise()
	{
		const fs::path userPath = _utf8ToPath(USER_DIRECTORY);
		const fs::path dataPath = HorizonApplet::IsRomfsMounted() ? fs::path("romfs:/") : userPath;
		std::set<fs::path> failedWriteAccess;
		ActiveSettings::SetPaths(false, userPath / "kankohi.nro", userPath, userPath, userPath / "cache", dataPath, failedWriteAccess);
		cemuLog_createLogFile(false);
		for (const auto& path : failedWriteAccess)
			cemuLog_log(LogType::Force, "Can't write to {}", _pathToUtf8(path));
		CreateDirectoriesIfMissing(ActiveSettings::GetConfigPath("controllerProfiles"));

		const fs::path settingsPath = ActiveSettings::GetConfigPath("settings.xml");
		GetConfigHandle().SetFilename(settingsPath.generic_wstring());
		std::error_code ec;
		const bool isFirstStart = !fs::exists(settingsPath, ec);
		NetworkConfig::LoadOnce();
		if (!isFirstStart)
			GetConfigHandle().Load();
		if (GetConfig().game_paths.empty())
		{
			const fs::path gamesPath = userPath / "games";
			CreateDirectoriesIfMissing(gamesPath);
			GetConfig().game_paths.emplace_back(_pathToUtf8(gamesPath));
			GetConfigHandle().Save();
		}

		const fs::path mlcPath = ActiveSettings::GetMlcPath();
		if (!CreateDefaultMlcFiles(mlcPath))
		{
			cemuLog_log(LogType::Force, "Failed to create the mlc directory at {}", _pathToUtf8(mlcPath));
			return false;
		}

		ActiveSettings::Init();
		LatteOverlay_init();
		CemuCommonInit();
		IAudioAPI::InitializeStatic();
		CreateDefaultController();
		if (!InitializeGlobalVulkan())
		{
			cemuLog_log(LogType::Force, "Vulkan is unavailable");
			return false;
		}
		CafeSystem::SetImplementation(&s_systemImplementation);
		s_initialised = true;
		return true;
	}

	void Shutdown()
	{
		if (!s_initialised)
			return;
		StopTitle();
		CafeTitleList::Shutdown();
		CafeSystem::Shutdown();
		AudrenAPI::Destroy();
		GetConfigHandle().Save();
		s_initialised = false;
	}

	std::string LaunchTitle(const fs::path& path)
	{
		TitleInfo launchTitle{path};
		if (launchTitle.IsValid())
		{
			CafeTitleList::AddTitleFromPath(path);
			TitleId baseTitleId;
			if (!CafeTitleList::FindBaseTitleId(launchTitle.GetAppTitleId(), baseTitleId))
				return "Unable to launch the game because the base files were not found.";
			const CafeSystem::PREPARE_STATUS_CODE r = CafeSystem::PrepareForegroundTitle(baseTitleId);
			if (r == CafeSystem::PREPARE_STATUS_CODE::UNABLE_TO_MOUNT)
				return fmt::format("Unable to mount the title at {}.", _pathToUtf8(path));
			if (r != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
				return fmt::format("Failed to launch {}.", _pathToUtf8(path));
		}
		else
		{
			const CafeTitleFileType fileType = DetermineCafeSystemFileType(path);
			if (fileType != CafeTitleFileType::RPX && fileType != CafeTitleFileType::ELF)
			{
				if (launchTitle.GetInvalidReason() == TitleInfo::InvalidReason::NO_DISC_KEY)
					return "Could not decrypt the title. Make sure keys.txt contains its disc key.";
				if (launchTitle.GetInvalidReason() == TitleInfo::InvalidReason::NO_TITLE_TIK)
					return "Could not decrypt the title because title.tik is missing.";
				return fmt::format("Unable to launch {}.", _pathToUtf8(path));
			}
			if (CafeSystem::PrepareForegroundTitleFromStandaloneRPX(path) != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
				return fmt::format("Failed to launch the executable {}.", _pathToUtf8(path));
		}

		if (!CreateRenderer())
			return "Failed to initialise the Vulkan renderer.";
		s_systemImplementation.m_exited = false;
		KeyboardSwitch::Install();
		cemuLog_log(LogType::Force, "Launching {}", CafeSystem::GetForegroundTitleName());
		CafeSystem::LaunchForegroundTitle();
		return {};
	}

	void StopTitle()
	{
		s_systemImplementation.ServiceRecreate();
		KeyboardSwitch::Cancel();
		CafeSystem::ShutdownTitle();
		g_renderer.reset();
	}

	void Update()
	{
		s_systemImplementation.ServiceRecreate();
		FollowDisplaySize();
		KeyboardSwitch::Pump();
	}

	bool HasTitleExited()
	{
		return s_systemImplementation.m_exited;
	}
}
