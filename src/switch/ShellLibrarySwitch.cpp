// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "switch/ShellLibrarySwitch.h"
#include "Cafe/Filesystem/fsc.h"
#include "Cafe/IOSU/PDM/iosu_pdm.h"
#include "Cafe/TitleList/GameInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "config/CemuConfig.h"
#include "util/helpers/helpers.h"

#include <condition_variable>

namespace
{
	constexpr auto COALESCE_DELAY = std::chrono::milliseconds(100);
	constexpr size_t MAX_ICON_SIZE = 70 * 1024;

	std::mutex s_mutex;
	std::condition_variable s_condition;
	std::thread s_worker;
	bool s_stopRequested = false;
	bool s_dirty = false;
	uint64 s_callbackId = 0;
	std::atomic<uint64> s_generation = 1;
	std::vector<ShellLibrary::Title> s_titles;

	std::unordered_map<TitleId, std::shared_ptr<const std::vector<uint8>>> s_iconCache;
	std::unordered_set<TitleId> s_iconFailed;

	void OnTitleListEvent(CafeTitleListCallbackEvent*, void*)
	{
		{
			std::lock_guard lock(s_mutex);
			s_dirty = true;
		}
		s_generation++;
		s_condition.notify_all();
	}

	bool IsStopRequested()
	{
		std::lock_guard lock(s_mutex);
		return s_stopRequested;
	}

	const char* GetFormatName(TitleInfo::TitleDataFormat format)
	{
		switch (format)
		{
		case TitleInfo::TitleDataFormat::HOST_FS:
			return "Folder";
		case TitleInfo::TitleDataFormat::WUD:
			return "WUD";
		case TitleInfo::TitleDataFormat::WIIU_ARCHIVE:
			return "WUA";
		case TitleInfo::TitleDataFormat::NUS:
			return "NUS";
		case TitleInfo::TitleDataFormat::WUHB:
			return "WUHB";
		default:
			return "Unknown";
		}
	}

	std::vector<ShellLibrary::Title> BuildTitles()
	{
		std::vector<ShellLibrary::Title> titles;
		std::unordered_set<TitleId> seen;
		for (TitleId titleId : CafeTitleList::GetAllTitleIds())
		{
			TitleId baseTitleId;
			if (!CafeTitleList::FindBaseTitleId(titleId, baseTitleId) || !seen.emplace(baseTitleId).second)
				continue;
			GameInfo2 gameInfo = CafeTitleList::GetGameInfo(baseTitleId);
			if (!gameInfo.IsValid() || gameInfo.IsSystemDataTitle())
				continue;

			ShellLibrary::Title& title = titles.emplace_back();
			title.titleId = baseTitleId;
			if (!GetConfig().GetGameListCustomName(baseTitleId, title.name))
				title.name = gameInfo.GetTitleName();
			const TitleInfo& base = gameInfo.GetBase();
			title.path = _pathToUtf8(base.GetPath());
			title.printPath = base.GetPrintPath();
			title.region = fmt::format("{}", gameInfo.GetRegion());
			title.format = GetFormatName(base.GetFormat());
			title.version = gameInfo.GetVersion();
			title.hasDlc = gameInfo.HasAOC();
			title.dlcVersion = gameInfo.GetAOCVersion();

			iosu::pdm::GameListStat stat;
			if (iosu::pdm::GetStatForGamelist(baseTitleId, stat))
			{
				title.minutesPlayed = stat.numMinutesPlayed;
				if (stat.last_played.year != 0)
					title.lastPlayed = fmt::format("{:04}-{:02}-{:02}", stat.last_played.year, stat.last_played.month + 1, stat.last_played.day);
			}

			if (auto it = s_iconCache.find(baseTitleId); it != s_iconCache.end())
				title.icon = it->second;
		}
		std::ranges::sort(titles, [](const ShellLibrary::Title& a, const ShellLibrary::Title& b) {
			return boost::ilexicographical_compare(a.name, b.name);
		});
		return titles;
	}

	std::optional<std::vector<uint8>> LoadIcon(TitleId titleId)
	{
		TitleInfo titleInfo;
		if (!CafeTitleList::GetFirstByTitleId(titleId, titleInfo))
			return std::nullopt;
		const std::string mountPath = TitleInfo::GetUniqueTempMountingPath();
		if (!titleInfo.Mount(mountPath, "", FSC_PRIORITY_BASE))
			return std::nullopt;
		auto icon = fsc_extractFile((mountPath + "/meta/iconTex.tga").c_str());
		if (!icon)
		{
			if (auto compressed = fsc_extractFile((mountPath + "/meta/iconTex.tga.gz").c_str()))
				icon = zlibDecompress(*compressed, MAX_ICON_SIZE);
		}
		titleInfo.Unmount(mountPath);
		if (!icon || icon->size() <= 16)
			return std::nullopt;
		return icon;
	}

	void LoadMissingIcons()
	{
		std::vector<TitleId> missing;
		{
			std::lock_guard lock(s_mutex);
			for (const ShellLibrary::Title& title : s_titles)
			{
				if (!title.icon && !s_iconFailed.contains(title.titleId))
					missing.emplace_back(title.titleId);
			}
		}
		for (TitleId titleId : missing)
		{
			if (IsStopRequested())
				return;
			std::optional<std::vector<uint8>> icon = LoadIcon(titleId);
			if (!icon)
			{
				cemuLog_log(LogType::Force, "Failed to load the icon of title {:016x}", titleId);
				s_iconFailed.emplace(titleId);
				continue;
			}
			auto shared = std::make_shared<const std::vector<uint8>>(std::move(*icon));
			s_iconCache.emplace(titleId, shared);
			std::lock_guard lock(s_mutex);
			for (ShellLibrary::Title& title : s_titles)
			{
				if (title.titleId == titleId)
					title.icon = shared;
			}
			s_generation++;
		}
	}

	void WorkerThread()
	{
		SetThreadName("ShellLibrary");
		SetHostThreadRole(HostThreadRole::Background);
		while (true)
		{
			{
				std::unique_lock lock(s_mutex);
				s_condition.wait(lock, [] { return s_stopRequested || s_dirty; });
				if (s_condition.wait_for(lock, COALESCE_DELAY, [] { return s_stopRequested; }))
					return;
				s_dirty = false;
			}
			std::vector<ShellLibrary::Title> titles = BuildTitles();
			cemuLog_log(LogType::Force, "Library has {} titles out of {} title ids", titles.size(), CafeTitleList::GetAllTitleIds().size());
			{
				std::lock_guard lock(s_mutex);
				s_titles = std::move(titles);
			}
			s_generation++;
			LoadMissingIcons();
		}
	}
}

namespace ShellLibrary
{
	void Start()
	{
		if (s_worker.joinable())
			return;
		{
			std::lock_guard lock(s_mutex);
			s_stopRequested = false;
			s_dirty = true;
		}
		s_worker = std::thread(WorkerThread);
		s_callbackId = CafeTitleList::RegisterCallback(OnTitleListEvent, nullptr);
	}

	void Stop()
	{
		if (!s_worker.joinable())
			return;
		CafeTitleList::UnregisterCallback(s_callbackId);
		{
			std::lock_guard lock(s_mutex);
			s_stopRequested = true;
		}
		s_condition.notify_all();
		s_worker.join();
	}

	uint64_t GetGeneration()
	{
		return s_generation.load();
	}

	std::vector<Title> GetTitles()
	{
		std::lock_guard lock(s_mutex);
		return s_titles;
	}

	void Rescan()
	{
		CafeTitleList::Refresh();
	}

	bool IsScanning()
	{
		return CafeTitleList::IsScanning();
	}

	std::vector<std::string> GetGameDirectories()
	{
		return GetConfig().game_paths;
	}
}
