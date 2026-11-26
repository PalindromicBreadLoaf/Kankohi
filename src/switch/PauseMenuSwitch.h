// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "switch/ShellConfigSwitch.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// In-game menu drawn over the last presented frame
class PauseMenu
{
public:
	PauseMenu(std::string title, std::string exitLabel);
	~PauseMenu();

	PauseMenu(const PauseMenu&) = delete;
	PauseMenu& operator=(const PauseMenu&) = delete;

	void Update(bool canOpen);
	bool IsExitRequested() const { return m_exitRequested; }

private:
	using Clock = std::chrono::steady_clock;

	enum class State
	{
		Closed,
		Open,
		Closing,
	};

	enum class PageId
	{
		Main,
		Settings,
		SettingsPage,
		ConfirmExit,
	};

	enum class RowKind
	{
		Action,
		Submenu,
		Choice,
		Header,
	};

	struct Row
	{
		RowKind kind = RowKind::Action;
		std::string label;
		std::string value;
		bool enabled = true;
		std::function<void()> activate;
		std::function<void(int)> change;
	};

	struct Page
	{
		PageId id;
		int selected = 0;
		int firstVisible = 0;
		size_t settingsPage = 0;
	};

	struct View
	{
		bool visible = false;
		std::string pageTitle;
		std::string message;
		std::vector<Row> rows;
		int selected = 0;
		int firstVisible = 0;
		bool darkTheme = true;
	};

	void Open();
	void Close(bool exit);
	void FinishClose();

	void HandleInput(uint64_t buttons, uint64_t pressed);
	bool TakeRepeat(size_t direction, bool held, bool pressed);
	void MoveSelection(int delta);
	void JumpSelection(int delta);
	void ChangeSelection(int delta);
	void Push(PageId id, size_t settingsPage = 0);
	void Back();

	Page& CurrentPage() { return m_pages.back(); }
	void Rebuild();
	void ClampSelection();
	std::vector<Row> BuildMain();
	std::vector<Row> BuildSettings();
	std::vector<Row> BuildSettingsPage(size_t index);
	std::vector<Row> BuildConfirmExit();

	void Publish();
	void Redraw();
	void Draw();

	std::string m_title;
	std::string m_exitLabel;
	State m_state = State::Closed;
	bool m_exitRequested = false;
	bool m_configDirty = false;
	bool m_profileRequested = false;
	bool m_darkTheme = true;

	uint64_t m_previousButtons = 0;
	std::optional<Clock::time_point> m_minusPressedAt;
	std::array<Clock::time_point, 4> m_nextRepeat{};
	Clock::time_point m_nextKeepAlive{};

	std::vector<Page> m_pages;
	std::vector<Row> m_rows;
	std::vector<ShellConfig::Page> m_settings;

	std::mutex m_viewMutex;
	View m_view;
};
