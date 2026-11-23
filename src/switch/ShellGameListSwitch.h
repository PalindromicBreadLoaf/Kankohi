// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "switch/ShellLibrarySwitch.h"

#include <optional>

#include <borealis.hpp>

namespace Shell
{
	class GameCell;

	class GameListView : public brls::Box
	{
	public:
		GameListView(std::optional<ShellLibrary::Title>& chosen, uint64_t focusTitleId);
		~GameListView() override;

		GameListView(const GameListView&) = delete;
		GameListView& operator=(const GameListView&) = delete;

	private:
		void OnRunLoop();
		void Rebuild();
		void FocusRow(size_t row);
		void ShowDetails(const ShellLibrary::Title& title);
		void ShowIcon(const ShellLibrary::Title& title);
		void UpdateStatus();
		void Launch(const ShellLibrary::Title& title);
		void Rescan();

		brls::Box* AddDetailRow(const std::string& key, brls::Label** value);

		std::optional<ShellLibrary::Title>& m_chosen;
		uint64_t m_focusTitleId;
		uint64_t m_generation = 0;
		bool m_wasScanning = false;
		brls::VoidEvent::Subscription m_runLoopSubscription;

		std::vector<ShellLibrary::Title> m_titles;
		std::vector<GameCell*> m_cells;

		brls::Label* m_status = nullptr;
		brls::ScrollingFrame* m_scroll = nullptr;
		brls::Box* m_list = nullptr;

		brls::Box* m_details = nullptr;
		brls::Image* m_icon = nullptr;
		brls::Label* m_iconPlaceholder = nullptr;
		std::shared_ptr<const std::vector<uint8_t>> m_shownIcon;
		bool m_iconSet = false;
		brls::Label* m_name = nullptr;
		brls::Label* m_titleId = nullptr;
		brls::Label* m_region = nullptr;
		brls::Label* m_version = nullptr;
		brls::Label* m_dlc = nullptr;
		brls::Label* m_played = nullptr;
		brls::Label* m_lastPlayed = nullptr;
		brls::Label* m_format = nullptr;
		brls::Label* m_path = nullptr;
	};
}
