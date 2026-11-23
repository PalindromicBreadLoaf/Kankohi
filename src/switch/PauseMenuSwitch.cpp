// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "switch/PauseMenuSwitch.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Common/switch/HorizonInput.h"
#include "input/api/Switch/SwitchController.h"
#include "imgui/imgui_extension.h"

#include <imgui.h>

namespace
{
	constexpr uint64_t Button(SwitchButton button)
	{
		return 1ULL << button;
	}

	constexpr uint64_t BUTTON_UP = Button(kSwitchButton_Up);
	constexpr uint64_t BUTTON_DOWN = Button(kSwitchButton_Down);
	constexpr uint64_t BUTTON_LEFT = Button(kSwitchButton_Left);
	constexpr uint64_t BUTTON_RIGHT = Button(kSwitchButton_Right);
	constexpr std::array<uint64_t, 4> DIRECTIONS = {BUTTON_UP, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT};
	constexpr uint64_t BUTTON_PLUS = Button(kSwitchButton_Plus);
	constexpr uint64_t BUTTON_MINUS = Button(kSwitchButton_Minus);
	constexpr uint64_t OPEN_CHORD = BUTTON_PLUS | BUTTON_MINUS;
	constexpr uint64_t CLOSE_BUTTONS = BUTTON_PLUS | BUTTON_MINUS;

	constexpr auto MINUS_HOLD_TO_OPEN = std::chrono::milliseconds(500);
	constexpr auto REPEAT_DELAY = std::chrono::milliseconds(400);
	constexpr auto REPEAT_INTERVAL = std::chrono::milliseconds(75);
	// Covers display changes and late font loads while nothing else asks for a frame
	constexpr auto KEEP_ALIVE_INTERVAL = std::chrono::milliseconds(250);

	constexpr int MAX_VISIBLE_ROWS = 10;

	constexpr float PANEL_WIDTH = 660.0f;
	constexpr float PANEL_PADDING = 32.0f;
	constexpr float PANEL_ROUNDING = 18.0f;
	constexpr float TITLE_SIZE = 30.0f;
	constexpr float PAGE_TITLE_SIZE = 20.0f;
	constexpr float MESSAGE_SIZE = 21.0f;
	constexpr float ROW_HEIGHT = 46.0f;
	constexpr float ROW_TEXT_SIZE = 23.0f;
	constexpr float ROW_INSET = 12.0f;
	constexpr float HEADER_HEIGHT = 38.0f;
	constexpr float HEADER_TEXT_SIZE = 18.0f;
	constexpr float FOOTER_SIZE = 20.0f;
	constexpr float ARROW_SIZE = 7.0f;

	constexpr ImU32 BACKDROP_COLOUR = IM_COL32(0, 0, 0, 150);
	constexpr ImU32 PANEL_COLOUR = IM_COL32(30, 32, 38, 245);
	constexpr ImU32 PANEL_BORDER_COLOUR = IM_COL32(255, 255, 255, 24);
	constexpr ImU32 DIVIDER_COLOUR = IM_COL32(255, 255, 255, 36);
	constexpr ImU32 ACCENT_COLOUR = IM_COL32(0, 195, 227, 255);
	constexpr ImU32 SELECTED_COLOUR = IM_COL32(0, 195, 227, 48);
	constexpr ImU32 TEXT_COLOUR = IM_COL32(240, 240, 244, 255);
	constexpr ImU32 DIM_TEXT_COLOUR = IM_COL32(160, 162, 172, 255);
	constexpr ImU32 DISABLED_TEXT_COLOUR = IM_COL32(105, 107, 116, 255);

	float TextWidth(ImFont* font, float size, const std::string& text)
	{
		return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
	}

	std::string Truncate(ImFont* font, float size, std::string text, float maxWidth)
	{
		if (TextWidth(font, size, text) <= maxWidth)
			return text;
		const float ellipsisWidth = TextWidth(font, size, "...");
		while (!text.empty())
		{
			while (!text.empty() && (static_cast<uint8>(text.back()) & 0xC0) == 0x80)
				text.pop_back();
			if (!text.empty())
				text.pop_back();
			if (TextWidth(font, size, text) + ellipsisWidth <= maxWidth)
				break;
		}
		return text + "...";
	}

	enum class ArrowDirection
	{
		Up,
		Down,
		Left,
		Right,
	};

	void DrawArrow(ImDrawList* draw, ImVec2 centre, float size, ArrowDirection direction, ImU32 colour)
	{
		switch (direction)
		{
		case ArrowDirection::Up:
			draw->AddTriangleFilled({centre.x - size, centre.y + size * 0.6f}, {centre.x + size, centre.y + size * 0.6f},
				{centre.x, centre.y - size * 0.6f}, colour);
			break;
		case ArrowDirection::Down:
			draw->AddTriangleFilled({centre.x - size, centre.y - size * 0.6f}, {centre.x, centre.y + size * 0.6f},
				{centre.x + size, centre.y - size * 0.6f}, colour);
			break;
		case ArrowDirection::Left:
			draw->AddTriangleFilled({centre.x + size * 0.6f, centre.y - size}, {centre.x + size * 0.6f, centre.y + size},
				{centre.x - size * 0.6f, centre.y}, colour);
			break;
		case ArrowDirection::Right:
			draw->AddTriangleFilled({centre.x - size * 0.6f, centre.y - size}, {centre.x + size * 0.6f, centre.y},
				{centre.x - size * 0.6f, centre.y + size}, colour);
			break;
		}
	}
}

PauseMenu::PauseMenu(std::string title, std::string exitLabel)
	: m_title(std::move(title)), m_exitLabel(std::move(exitLabel))
{
	LatteOverlay_setHostUICallback([this] { Draw(); });
}

PauseMenu::~PauseMenu()
{
	LatteOverlay_setHostUICallback({});
	if (CafeSystem::IsTitlePaused())
		CafeSystem::ResumeTitle();
	if (m_configDirty)
		ShellConfig::Save();
}

void PauseMenu::Update(bool canOpen)
{
	const uint64_t buttons = HorizonInput::GetHeldButtons();
	const uint64_t pressed = buttons & ~m_previousButtons;
	m_previousButtons = buttons;

	switch (m_state)
	{
	case State::Closed:
		if (!(buttons & BUTTON_MINUS))
			m_minusPressedAt.reset();
		else if (pressed & BUTTON_MINUS)
			m_minusPressedAt = Clock::now();
		if (canOpen && (((buttons & OPEN_CHORD) == OPEN_CHORD && (pressed & OPEN_CHORD)) ||
						   (m_minusPressedAt && Clock::now() - *m_minusPressedAt >= MINUS_HOLD_TO_OPEN)))
		{
			m_minusPressedAt.reset();
			Open();
		}
		break;

	case State::Open:
		HandleInput(buttons, pressed);
		if (m_state == State::Open && Clock::now() >= m_nextKeepAlive)
			Redraw();
		break;

	case State::Closing:
		if (buttons == 0)
			FinishClose();
		break;
	}
}

void PauseMenu::Open()
{
	CafeSystem::PauseTitle();
	HorizonInput::StopAllRumble();
	m_state = State::Open;
	m_exitRequested = false;
	m_settings = ShellConfig::GetPages();
	m_pages = {Page{PageId::Main}};
	m_nextRepeat.fill(Clock::now() + REPEAT_DELAY);
	cemuLog_log(LogType::Force, "Paused");
	Rebuild();
}

void PauseMenu::Close(bool exit)
{
	m_exitRequested = exit;
	m_state = State::Closing;
	{
		std::scoped_lock lock(m_viewMutex);
		m_view.visible = false;
	}
	Redraw();
}

void PauseMenu::FinishClose()
{
	m_state = State::Closed;
	m_pages.clear();
	m_rows.clear();
	m_settings.clear();
	if (m_configDirty)
	{
		ShellConfig::Save();
		m_configDirty = false;
	}
	// Exiting leaves the title paused for shutdown
	if (m_exitRequested)
		return;
	CafeSystem::ResumeTitle();
	cemuLog_log(LogType::Force, "Resumed");
}

void PauseMenu::HandleInput(uint64_t buttons, uint64_t pressed)
{
	if (pressed & CLOSE_BUTTONS)
	{
		Close(false);
		return;
	}
	if (pressed & Button(kSwitchButton_B))
	{
		Back();
		return;
	}
	if (pressed & (Button(kSwitchButton_L) | Button(kSwitchButton_R)))
	{
		JumpSelection((pressed & Button(kSwitchButton_L)) ? -MAX_VISIBLE_ROWS : MAX_VISIBLE_ROWS);
		return;
	}

	const int selected = CurrentPage().selected;
	if ((pressed & Button(kSwitchButton_A)) && selected < (int)m_rows.size())
	{
		const Row& row = m_rows[selected];
		if (!row.enabled)
			return;
		if (row.kind == RowKind::Choice)
			ChangeSelection(1);
		else if (row.activate)
		{
			const auto activate = row.activate;
			activate();
		}
		return;
	}

	for (size_t direction = 0; direction < DIRECTIONS.size(); direction++)
	{
		if (!TakeRepeat(direction, buttons & DIRECTIONS[direction], pressed & DIRECTIONS[direction]))
			continue;
		if (DIRECTIONS[direction] == BUTTON_UP)
			MoveSelection(-1);
		else if (DIRECTIONS[direction] == BUTTON_DOWN)
			MoveSelection(1);
		else if (DIRECTIONS[direction] == BUTTON_LEFT)
			ChangeSelection(-1);
		else
			ChangeSelection(1);
	}
}

bool PauseMenu::TakeRepeat(size_t direction, bool held, bool pressed)
{
	const auto now = Clock::now();
	if (pressed)
	{
		m_nextRepeat[direction] = now + REPEAT_DELAY;
		return true;
	}
	if (held && now >= m_nextRepeat[direction])
	{
		m_nextRepeat[direction] = now + REPEAT_INTERVAL;
		return true;
	}
	return false;
}

void PauseMenu::MoveSelection(int delta)
{
	const int count = (int)m_rows.size();
	if (count == 0)
		return;
	Page& page = CurrentPage();
	int index = page.selected;
	for (int i = 0; i < count; i++)
	{
		index = (index + delta + count) % count;
		if (m_rows[index].kind != RowKind::Header)
			break;
	}
	page.selected = index;
	ClampSelection();
	Publish();
}

void PauseMenu::JumpSelection(int delta)
{
	const int count = (int)m_rows.size();
	if (count == 0)
		return;
	Page& page = CurrentPage();
	int index = std::clamp(page.selected + delta, 0, count - 1);
	const int step = delta < 0 ? -1 : 1;
	while (index >= 0 && index < count && m_rows[index].kind == RowKind::Header)
		index += step;
	if (index < 0 || index >= count)
		return;
	page.selected = index;
	ClampSelection();
	Publish();
}

void PauseMenu::ChangeSelection(int delta)
{
	const int selected = CurrentPage().selected;
	if (selected >= (int)m_rows.size())
		return;
	const Row& row = m_rows[selected];
	if (!row.enabled || !row.change)
		return;
	const auto change = row.change;
	change(delta);
	Rebuild();
}

void PauseMenu::Push(PageId id, size_t settingsPage)
{
	m_pages.push_back(Page{id, 0, 0, settingsPage});
	Rebuild();
}

void PauseMenu::Back()
{
	if (m_pages.size() <= 1)
	{
		Close(false);
		return;
	}
	m_pages.pop_back();
	Rebuild();
}

void PauseMenu::Rebuild()
{
	const Page& page = CurrentPage();
	switch (page.id)
	{
	case PageId::Main:
		m_rows = BuildMain();
		break;
	case PageId::Settings:
		m_rows = BuildSettings();
		break;
	case PageId::SettingsPage:
		m_rows = BuildSettingsPage(page.settingsPage);
		break;
	case PageId::ConfirmExit:
		m_rows = BuildConfirmExit();
		break;
	}
	ClampSelection();
	Publish();
}

void PauseMenu::ClampSelection()
{
	Page& page = CurrentPage();
	const int count = (int)m_rows.size();
	page.selected = std::clamp(page.selected, 0, std::max(count - 1, 0));
	while (page.selected < count - 1 && m_rows[page.selected].kind == RowKind::Header)
		page.selected++;

	if (page.selected < page.firstVisible)
		page.firstVisible = page.selected;
	if (page.selected >= page.firstVisible + MAX_VISIBLE_ROWS)
		page.firstVisible = page.selected - MAX_VISIBLE_ROWS + 1;
	if (page.firstVisible > 0 && m_rows[page.firstVisible - 1].kind == RowKind::Header &&
		page.selected - page.firstVisible + 1 < MAX_VISIBLE_ROWS)
		page.firstVisible--;
	page.firstVisible = std::clamp(page.firstVisible, 0, std::max(count - MAX_VISIBLE_ROWS, 0));
}

std::vector<PauseMenu::Row> PauseMenu::BuildMain()
{
	std::vector<Row> rows;
	rows.push_back({RowKind::Action, "Resume", {}, true, [this] { Close(false); }});
	Row screen{RowKind::Choice, "Screen", LatteGPUState.isDRCPrimary ? "GamePad" : "TV"};
	screen.change = [](int) { LatteGPUState.isDRCPrimary = !LatteGPUState.isDRCPrimary; };
	rows.push_back(std::move(screen));
	rows.push_back({RowKind::Submenu, "Settings", {}, true, [this] { Push(PageId::Settings); }});
	rows.push_back({RowKind::Action, m_exitLabel, {}, true, [this] { Push(PageId::ConfirmExit); }});
	return rows;
}

std::vector<PauseMenu::Row> PauseMenu::BuildSettings()
{
	std::vector<Row> rows;
	for (size_t i = 0; i < m_settings.size(); i++)
		rows.push_back({RowKind::Submenu, m_settings[i].name, {}, true, [this, i] { Push(PageId::SettingsPage, i); }});
	return rows;
}

std::vector<PauseMenu::Row> PauseMenu::BuildSettingsPage(size_t index)
{
	std::vector<Row> rows;
	if (index >= m_settings.size())
		return rows;
	for (const ShellConfig::Section& section : m_settings[index].sections)
	{
		rows.push_back({RowKind::Header, section.header});
		for (const ShellConfig::Setting& setting : section.settings)
		{
			const int current = setting.get();
			Row row{RowKind::Choice, setting.title};
			if (current >= 0 && current < (int)setting.options.size())
				row.value = setting.options[current];
			row.enabled = !setting.appliesAtLaunch && !setting.options.empty();
			row.change = [this, &setting](int delta) {
				const int count = (int)setting.options.size();
				setting.set((std::max(setting.get(), 0) + delta + count) % count);
				ShellConfig::ApplyRunning();
				m_configDirty = true;
			};
			rows.push_back(std::move(row));
		}
	}
	return rows;
}

std::vector<PauseMenu::Row> PauseMenu::BuildConfirmExit()
{
	std::vector<Row> rows;
	rows.push_back({RowKind::Action, "Cancel", {}, true, [this] { Back(); }});
	rows.push_back({RowKind::Action, m_exitLabel, {}, true, [this] { Close(true); }});
	return rows;
}

void PauseMenu::Publish()
{
	View view;
	view.visible = m_state == State::Open;
	const Page& page = CurrentPage();
	switch (page.id)
	{
	case PageId::Main:
		view.pageTitle = "Paused";
		break;
	case PageId::Settings:
		view.pageTitle = "Settings";
		break;
	case PageId::SettingsPage:
		view.pageTitle = page.settingsPage < m_settings.size() ? m_settings[page.settingsPage].name : "Settings";
		view.message = "Greyed-out settings can only be changed from the library.";
		break;
	case PageId::ConfirmExit:
		view.pageTitle = m_exitLabel;
		view.message = "Progress since your last save will be lost.";
		break;
	}
	view.rows = m_rows;
	for (Row& row : view.rows)
	{
		row.activate = {};
		row.change = {};
	}
	view.selected = page.selected;
	view.firstVisible = page.firstVisible;
	{
		std::scoped_lock lock(m_viewMutex);
		m_view = std::move(view);
	}
	Redraw();
}

void PauseMenu::Redraw()
{
	m_nextKeepAlive = Clock::now() + KEEP_ALIVE_INTERVAL;
	Latte_RequestRedraw();
}

void PauseMenu::Draw()
{
	std::scoped_lock lock(m_viewMutex);
	if (!m_view.visible)
		return;

	const ImVec2 display = ImGui::GetIO().DisplaySize;
	const float scale = display.y / 720.0f;
	ImFont* font = ImGui_GetFont(std::round(TITLE_SIZE * scale));
	if (!font)
	{
		// Loads before the next frame
		Latte_RequestRedraw();
		return;
	}
	ImDrawList* draw = ImGui::GetForegroundDrawList();

	const float width = PANEL_WIDTH * scale;
	const float padding = PANEL_PADDING * scale;
	const float innerWidth = width - padding * 2.0f;
	const float rowHeight = ROW_HEIGHT * scale;
	const float headerHeight = HEADER_HEIGHT * scale;
	const float rowText = ROW_TEXT_SIZE * scale;
	const float messageSize = MESSAGE_SIZE * scale;
	const float footerSize = FOOTER_SIZE * scale;
	const float arrow = ARROW_SIZE * scale;

	const int rowCount = (int)m_view.rows.size();
	const int first = std::clamp(m_view.firstVisible, 0, std::max(rowCount - 1, 0));
	const int last = std::min(first + MAX_VISIBLE_ROWS, rowCount);

	float listHeight = 0.0f;
	for (int i = first; i < last; i++)
		listHeight += m_view.rows[i].kind == RowKind::Header ? headerHeight : rowHeight;
	float messageHeight = 0.0f;
	if (!m_view.message.empty())
		messageHeight = font->CalcTextSizeA(messageSize, FLT_MAX, innerWidth, m_view.message.c_str()).y + 14.0f * scale;
	const float headingHeight = (TITLE_SIZE + 4.0f + PAGE_TITLE_SIZE + 16.0f) * scale + 12.0f * scale;
	const float footerHeight = (12.0f + 14.0f) * scale + footerSize;
	const float height = padding * 2.0f + headingHeight + messageHeight + listHeight + footerHeight;

	const ImVec2 panelMin{std::floor((display.x - width) * 0.5f), std::floor((display.y - height) * 0.5f)};
	const ImVec2 panelMax{panelMin.x + width, panelMin.y + height};
	const float left = panelMin.x + padding;
	const float right = panelMax.x - padding;

	draw->AddRectFilled({0.0f, 0.0f}, display, BACKDROP_COLOUR);
	draw->AddRectFilled(panelMin, panelMax, PANEL_COLOUR, PANEL_ROUNDING * scale);
	draw->AddRect(panelMin, panelMax, PANEL_BORDER_COLOUR, PANEL_ROUNDING * scale, 0, scale);

	float y = panelMin.y + padding;
	const std::string title = Truncate(font, TITLE_SIZE * scale, m_title, innerWidth);
	draw->AddText(font, TITLE_SIZE * scale, {left, y}, TEXT_COLOUR, title.c_str());
	y += (TITLE_SIZE + 4.0f) * scale;
	draw->AddText(font, PAGE_TITLE_SIZE * scale, {left, y}, ACCENT_COLOUR, m_view.pageTitle.c_str());
	y += (PAGE_TITLE_SIZE + 16.0f) * scale;
	draw->AddLine({left, y}, {right, y}, DIVIDER_COLOUR, scale);
	y += 12.0f * scale;

	if (!m_view.message.empty())
	{
		draw->AddText(font, messageSize, {left, y}, DIM_TEXT_COLOUR, m_view.message.c_str(), nullptr, innerWidth);
		y += messageHeight;
	}

	const float listTop = y;
	for (int i = first; i < last; i++)
	{
		const Row& row = m_view.rows[i];
		if (row.kind == RowKind::Header)
		{
			const float textY = y + headerHeight - HEADER_TEXT_SIZE * scale - 8.0f * scale;
			draw->AddText(font, HEADER_TEXT_SIZE * scale, {left, textY}, DIM_TEXT_COLOUR, row.label.c_str());
			y += headerHeight;
			continue;
		}

		const bool selected = i == m_view.selected;
		const ImVec2 rowMin{panelMin.x + ROW_INSET * scale, y};
		const ImVec2 rowMax{panelMax.x - ROW_INSET * scale, y + rowHeight};
		if (selected)
		{
			draw->AddRectFilled(rowMin, rowMax, SELECTED_COLOUR, 10.0f * scale);
			draw->AddRect(rowMin, rowMax, ACCENT_COLOUR, 10.0f * scale, 0, 2.0f * scale);
		}

		const float textY = y + (rowHeight - rowText) * 0.5f;
		const float centreY = y + rowHeight * 0.5f;
		const ImU32 labelColour = row.enabled ? TEXT_COLOUR : DISABLED_TEXT_COLOUR;
		const ImU32 valueColour = !row.enabled ? DISABLED_TEXT_COLOUR : (selected ? TEXT_COLOUR : DIM_TEXT_COLOUR);

		float valueRight = right;
		if (row.kind == RowKind::Submenu)
		{
			DrawArrow(draw, {right - arrow * 0.6f, centreY}, arrow, ArrowDirection::Right, labelColour);
			valueRight -= arrow * 3.0f;
		}
		const bool arrows = row.kind == RowKind::Choice && selected && row.enabled;
		if (arrows)
		{
			DrawArrow(draw, {right - arrow * 0.6f, centreY}, arrow, ArrowDirection::Right, ACCENT_COLOUR);
			valueRight -= arrow * 3.0f;
		}

		const float labelWidth = TextWidth(font, rowText, row.label);
		float valueWidth = 0.0f;
		if (!row.value.empty())
		{
			const float space = valueRight - left - labelWidth - 32.0f * scale - (arrows ? arrow * 3.0f : 0.0f);
			const std::string value = Truncate(font, rowText, row.value, std::max(space, 0.0f));
			valueWidth = TextWidth(font, rowText, value);
			draw->AddText(font, rowText, {valueRight - valueWidth, textY}, valueColour, value.c_str());
		}
		if (arrows)
			DrawArrow(draw, {valueRight - valueWidth - arrow * 2.0f, centreY}, arrow, ArrowDirection::Left, ACCENT_COLOUR);

		const std::string label = Truncate(font, rowText, row.label, innerWidth);
		draw->AddText(font, rowText, {left, textY}, labelColour, label.c_str());
		y += rowHeight;
	}

	if (first > 0)
		DrawArrow(draw, {right, listTop - 6.0f * scale}, arrow * 0.8f, ArrowDirection::Up, DIM_TEXT_COLOUR);
	if (last < rowCount)
		DrawArrow(draw, {right, y + 6.0f * scale}, arrow * 0.8f, ArrowDirection::Down, DIM_TEXT_COLOUR);

	y += 12.0f * scale;
	draw->AddLine({left, y}, {right, y}, DIVIDER_COLOUR, scale);
	y += 14.0f * scale;
	const std::string hints = "A Select    B Back    + Resume";
	const float hintsWidth = TextWidth(font, footerSize, hints);
	draw->AddText(font, footerSize, {right - hintsWidth, y}, DIM_TEXT_COLOUR, hints.c_str());
}
