// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "switch/ShellConfigSwitch.h"
#include "Common/switch/HorizonDiagnostics.h"
#include "Common/version.h"
#include "audio/IAudioAPI.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "config/LaunchSettings.h"
#include "switch/EmulationSwitch.h"

#include <initializer_list>

namespace
{
	using ShellConfig::Setting;

	template<typename T>
	struct Option
	{
		std::string label;
		T value;
	};

	template<typename T, typename TField>
	Setting Choice(std::string title, TField& field, std::vector<Option<T>> options)
	{
		Setting setting;
		setting.title = std::move(title);
		for (const Option<T>& option : options)
			setting.options.push_back(option.label);
		setting.get = [&field, options] {
			const T value = static_cast<T>(field);
			for (size_t i = 0; i < options.size(); i++)
			{
				if (options[i].value == value)
					return (int)i;
			}
			return -1;
		};
		setting.set = [&field, options](int index) {
			if (index >= 0 && index < (int)options.size())
				field = options[index].value;
		};
		return setting;
	}

	template<typename TField>
	Setting Toggle(std::string title, TField& field)
	{
		Setting setting = Choice<bool>(std::move(title), field, {{"Off", false}, {"On", true}});
		setting.isToggle = true;
		return setting;
	}

	Setting AtLaunch(Setting setting)
	{
		setting.appliesAtLaunch = true;
		return setting;
	}

	std::vector<Option<ScreenPosition>> ScreenPositions(const char* disabledLabel)
	{
		return {
			{disabledLabel, ScreenPosition::kDisabled},
			{"Top left", ScreenPosition::kTopLeft},
			{"Top centre", ScreenPosition::kTopCenter},
			{"Top right", ScreenPosition::kTopRight},
			{"Bottom left", ScreenPosition::kBottomLeft},
			{"Bottom centre", ScreenPosition::kBottomCenter},
			{"Bottom right", ScreenPosition::kBottomRight},
		};
	}

	std::vector<Option<sint32>> Filters()
	{
		return {
			{"Bilinear", kLinearFilter},
			{"Bicubic", kBicubicFilter},
			{"Hermite", kBicubicHermiteFilter},
			{"Nearest neighbour", kNearestNeighborFilter},
		};
	}

	ShellConfig::Page GeneralPage(CemuConfig& config)
	{
		return {"General", {
			{"Emulation", {}, {
				AtLaunch(Choice<CPUMode>("CPU mode", config.cpu_mode, {
					{"Auto", CPUMode::Auto},
					{"Single-core recompiler", CPUMode::SinglecoreRecompiler},
					{"Multi-core recompiler", CPUMode::MulticoreRecompiler},
					{"Interpreter", CPUMode::SinglecoreInterpreter},
				})),
			}},
			{"Console", {}, {
				AtLaunch(Choice<CafeConsoleLanguage>("Language", config.console_language, {
					{"Japanese", CafeConsoleLanguage::JA},
					{"English", CafeConsoleLanguage::EN},
					{"French", CafeConsoleLanguage::FR},
					{"German", CafeConsoleLanguage::DE},
					{"Italian", CafeConsoleLanguage::IT},
					{"Spanish", CafeConsoleLanguage::ES},
					{"Chinese (simplified)", CafeConsoleLanguage::ZH},
					{"Korean", CafeConsoleLanguage::KO},
					{"Dutch", CafeConsoleLanguage::NL},
					{"Portuguese", CafeConsoleLanguage::PT},
					{"Russian", CafeConsoleLanguage::RU},
					{"Chinese (traditional)", CafeConsoleLanguage::TW},
				})),
			}},
		}};
	}

	ShellConfig::Page GraphicsPage(CemuConfig& config)
	{
		return {"Graphics", {
			{"Presentation", {}, {
				Choice<int>("VSync", config.vsync, {
					{"Off", 0},
					{"On", 1},
				}),
				Choice<sint32>("Scaling", config.fullscreen_scaling, {
					{"Keep aspect ratio", kKeepAspectRatio},
					{"Stretch", kStretch},
				}),
				Choice<sint32>("Upscale filter", config.upscale_filter, Filters()),
				Choice<sint32>("Downscale filter", config.downscale_filter, Filters()),
			}},
			{"Rendering", "Asynchronous compilation avoids shader compilation stutter at the cost of briefly missing objects.", {
				Toggle("Asynchronous shader compilation", config.async_compile),
				Toggle("Full sync at GX2DrawDone", config.gx2drawdone_sync),
			}},
		}};
	}

	ShellConfig::Page OverlayPage(CemuConfig& config)
	{
		return {"Overlay", {
			{"Statistics", {}, {
				Choice<ScreenPosition>("Position", config.overlay.position, ScreenPositions("Hidden")),
				Toggle("Frame rate", config.overlay.fps),
				Toggle("Draw calls", config.overlay.drawcalls),
				Toggle("CPU usage", config.overlay.cpu_usage),
				Toggle("CPU usage per core", config.overlay.cpu_per_core_usage),
				Toggle("RAM usage", config.overlay.ram_usage),
			}},
			{"Notifications", {}, {
				Choice<ScreenPosition>("Position", config.notification.position, ScreenPositions("Hidden")),
				Toggle("Shader compilation", config.notification.shader_compiling),
				Toggle("Controller profiles", config.notification.controller_profiles),
			}},
		}};
	}

	ShellConfig::Page AudioPage(CemuConfig& config)
	{
		std::vector<Option<sint32>> volumes;
		for (sint32 volume = 0; volume <= 100; volume += 10)
			volumes.push_back({fmt::format("{}%", volume), volume});
		std::vector<Option<sint32>> latencies;
		for (sint32 blocks = 0; blocks < (sint32)IAudioAPI::kBlockCount; blocks++)
			latencies.push_back({fmt::format("{} ms", blocks * 12), blocks});
		return {"Audio", {
			{"TV", "Higher latency avoids crackling when emulation runs slowly.", {
				Choice<sint32>("Volume", config.tv_volume, std::move(volumes)),
				AtLaunch(Choice<AudioChannels>("Channels", config.tv_channels, {
					{"Mono", kMono},
					{"Stereo", kStereo},
				})),
				AtLaunch(Choice<sint32>("Latency", config.audio_delay, std::move(latencies))),
			}},
		}};
	}
}

namespace ShellConfig
{
	std::vector<Page> GetPages()
	{
		CemuConfig& config = GetConfig();
		return {GeneralPage(config), GraphicsPage(config), OverlayPage(config), AudioPage(config)};
	}

	std::vector<std::pair<std::string, std::string>> GetAbout()
	{
		std::vector<std::pair<std::string, std::string>> about = {
			{"Version", BUILD_VERSION_WITH_NAME_STRING},
			{"Build ID", HorizonDiagnostics::GetBuildId()},
			{"User directory", EmulationSwitch::USER_DIRECTORY},
			{"MLC path", _pathToUtf8(ActiveSettings::GetMlcPath())},
		};
		for (const std::string& path : GetConfig().game_paths)
			about.emplace_back("Game folder", path);
		return about;
	}

	void Save()
	{
		if (LaunchSettings::GetSettingOverrides().empty())
			GetConfigHandle().Save();
	}

	void ApplyRunning()
	{
		std::shared_lock lock(g_audioMutex);
		if (g_tvAudio)
			g_tvAudio->SetVolume(GetConfig().tv_volume);
	}
}
