// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "switch/ShellGameListSwitch.h"
#include "switch/ShellSettingsSwitch.h"

#include <algorithm>

#include <borealis/views/cells/cell_detail.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nanovg.h>

namespace Shell
{
	namespace
	{
		constexpr float DETAILS_WIDTH = 420;
		constexpr float DETAILS_PADDING = 30;
		constexpr float ICON_SIZE = 128;

		NVGcolor ThemeColour(const std::string& name)
		{
			brls::Theme theme = brls::Application::getTheme();
			return theme[name];
		}

		std::string FormatPlayTime(uint32_t minutes)
		{
			if (minutes == 0)
				return "Never";
			if (minutes < 60)
				return fmt::format("{} min", minutes);
			return fmt::format("{} h {} min", minutes / 60, minutes % 60);
		}

		bool HasSameEntries(const std::vector<ShellLibrary::Title>& a, const std::vector<ShellLibrary::Title>& b)
		{
			return std::ranges::equal(a, b, [](const ShellLibrary::Title& x, const ShellLibrary::Title& y) {
				return x.titleId == y.titleId && x.name == y.name && x.version == y.version;
			});
		}
	}

	class GameCell : public brls::DetailCell
	{
	public:
		GameCell(const ShellLibrary::Title& title, std::function<void(GameCell*)> onFocus)
			: m_titleId(title.titleId), m_onFocus(std::move(onFocus))
		{
			setText(title.name);
			setDetailText(fmt::format("v{}", title.version));
			setDetailTextColor(ThemeColour("brls/text_disabled"));
		}

		void onFocusGained() override
		{
			brls::DetailCell::onFocusGained();
			m_onFocus(this);
		}

		uint64_t GetTitleId() const
		{
			return m_titleId;
		}

	private:
		uint64_t m_titleId;
		std::function<void(GameCell*)> m_onFocus;
	};

	GameListView::GameListView(std::optional<ShellLibrary::Title>& chosen, uint64_t focusTitleId)
		: brls::Box(brls::Axis::ROW), m_chosen(chosen), m_focusTitleId(focusTitleId)
	{
		setGrow(1);

		auto* left = new brls::Box(brls::Axis::COLUMN);
		left->setGrow(1);
		addView(left);

		m_status = new brls::Label();
		m_status->setFontSize(16);
		m_status->setTextColor(ThemeColour("brls/text_disabled"));
		m_status->setMargins(16, 40, 8, 40);
		left->addView(m_status);

		m_list = new brls::Box(brls::Axis::COLUMN);
		m_list->setPadding(8, 40, 32, 40);
		m_scroll = new brls::ScrollingFrame();
		m_scroll->setGrow(1);
		m_scroll->setContentView(m_list);
		left->addView(m_scroll);

		m_details = new brls::Box(brls::Axis::COLUMN);
		m_details->setWidth(DETAILS_WIDTH);
		m_details->setPadding(DETAILS_PADDING);
		m_details->setLineLeft(1);
		m_details->setLineColor(ThemeColour("brls/applet_frame/separator"));
		addView(m_details);

		auto* iconBox = new brls::Box(brls::Axis::COLUMN);
		iconBox->setDimensions(DETAILS_WIDTH - 2 * DETAILS_PADDING, ICON_SIZE);
		iconBox->setJustifyContent(brls::JustifyContent::CENTER);
		iconBox->setAlignItems(brls::AlignItems::CENTER);
		iconBox->setMarginBottom(20);
		m_details->addView(iconBox);

		m_icon = new brls::Image();
		m_icon->setDimensions(ICON_SIZE, ICON_SIZE);
		m_icon->setScalingType(brls::ImageScalingType::FIT);
		iconBox->addView(m_icon);

		m_iconPlaceholder = new brls::Label();
		m_iconPlaceholder->setText("No icon");
		m_iconPlaceholder->setTextColor(ThemeColour("brls/text_disabled"));
		iconBox->addView(m_iconPlaceholder);

		m_name = new brls::Label();
		m_name->setFontSize(24);
		m_name->setWidth(DETAILS_WIDTH - 2 * DETAILS_PADDING);
		m_name->setVerticalAlign(brls::VerticalAlign::TOP);
		m_name->setMarginBottom(16);
		m_details->addView(m_name);

		AddDetailRow("Title ID", &m_titleId);
		AddDetailRow("Region", &m_region);
		AddDetailRow("Version", &m_version);
		AddDetailRow("DLC", &m_dlc);
		AddDetailRow("Played", &m_played);
		AddDetailRow("Last played", &m_lastPlayed);
		AddDetailRow("Format", &m_format);
		AddDetailRow("Path", &m_path);

		registerAction("Rescan", brls::BUTTON_Y, [this](brls::View*) {
			Rescan();
			return true;
		});
		registerAction("Settings", brls::BUTTON_BACK, [](brls::View*) {
			brls::Application::pushActivity(CreateSettingsActivity());
			return true;
		});

		m_runLoopSubscription = brls::Application::getRunLoopEvent()->subscribe([this] { OnRunLoop(); });
		ShowPlaceholder();
	}

	GameListView::~GameListView()
	{
		brls::Application::getRunLoopEvent()->unsubscribe(m_runLoopSubscription);
	}

	brls::Box* GameListView::AddDetailRow(const std::string& key, brls::Label** value)
	{
		auto* row = new brls::Box(brls::Axis::ROW);
		row->setMarginBottom(6);

		auto* keyLabel = new brls::Label();
		keyLabel->setText(key);
		keyLabel->setFontSize(16);
		keyLabel->setWidth(110);
		keyLabel->setTextColor(ThemeColour("brls/text_disabled"));
		row->addView(keyLabel);

		*value = new brls::Label();
		(*value)->setFontSize(16);
		(*value)->setSingleLine(true);
		(*value)->setGrow(1);
		(*value)->setShrink(1);
		row->addView(*value);

		m_details->addView(row);
		return row;
	}

	void GameListView::OnRunLoop()
	{
		const bool scanning = ShellLibrary::IsScanning();
		if (scanning != m_wasScanning)
		{
			m_wasScanning = scanning;
			UpdateStatus();
		}

		const uint64_t generation = ShellLibrary::GetGeneration();
		if (generation == m_generation)
			return;
		const std::vector<brls::Activity*> stack = brls::Application::getActivitiesStack();
		if (stack.empty() || stack.back() != getParentActivity())
			return;
		Rebuild();
	}

	void GameListView::Rebuild()
	{
		m_generation = ShellLibrary::GetGeneration();
		std::vector<ShellLibrary::Title> titles = ShellLibrary::GetTitles();
		if (!m_cells.empty() && HasSameEntries(titles, m_titles))
		{
			m_titles = std::move(titles);
			const auto focused = std::ranges::find(m_titles, m_focusTitleId, &ShellLibrary::Title::titleId);
			if (focused != m_titles.end())
				ShowDetails(*focused);
			return;
		}

		m_titles = std::move(titles);
		m_list->clearViews();
		m_cells.clear();
		UpdateStatus();

		if (m_titles.empty())
		{
			brls::Application::giveFocus(ShowPlaceholder());
			return;
		}

		m_details->setVisibility(brls::Visibility::VISIBLE);
		size_t focusRow = 0;
		for (size_t row = 0; row < m_titles.size(); row++)
		{
			auto* cell = new GameCell(m_titles[row], [this](GameCell* focusedCell) {
				m_focusTitleId = focusedCell->GetTitleId();
				const auto it = std::ranges::find(m_titles, m_focusTitleId, &ShellLibrary::Title::titleId);
				if (it != m_titles.end())
					ShowDetails(*it);
			});
			cell->registerClickAction([this, cell](brls::View*) {
				const auto it = std::ranges::find(m_titles, cell->GetTitleId(), &ShellLibrary::Title::titleId);
				if (it != m_titles.end())
					Launch(*it);
				return true;
			});
			m_list->addView(cell);
			m_cells.push_back(cell);
			if (m_titles[row].titleId == m_focusTitleId)
				focusRow = row;
		}
		FocusRow(focusRow);
	}

	brls::View* GameListView::ShowPlaceholder()
	{
		auto* cell = new brls::DetailCell();
		cell->setText(m_wasScanning || m_generation == 0 ? "Looking for titles..." : "No titles found");
		cell->setDetailText(fmt::to_string(fmt::join(ShellLibrary::GetGameDirectories(), ", ")));
		cell->registerClickAction([this](brls::View*) {
			Rescan();
			return true;
		});
		m_list->addView(cell);
		m_details->setVisibility(brls::Visibility::INVISIBLE);
		return cell;
	}

	void GameListView::FocusRow(size_t row)
	{
		if (row < m_cells.size())
			brls::Application::giveFocus(m_cells[row]);
	}

	void GameListView::UpdateStatus()
	{
		std::string text = m_titles.size() == 1 ? std::string("1 title") : fmt::format("{} titles", m_titles.size());
		if (m_wasScanning)
			text += " · Scanning...";
		m_status->setText(text);
	}

	void GameListView::ShowDetails(const ShellLibrary::Title& title)
	{
		m_name->setText(title.name);
		m_titleId->setText(fmt::format("{:016X}", title.titleId));
		m_region->setText(title.region);
		m_version->setText(fmt::format("{}", title.version));
		m_dlc->setText(title.hasDlc ? fmt::format("v{}", title.dlcVersion) : std::string("None"));
		m_played->setText(FormatPlayTime(title.minutesPlayed));
		m_lastPlayed->setText(title.lastPlayed.empty() ? std::string("Never") : title.lastPlayed);
		m_format->setText(title.format);
		m_path->setText(title.printPath);
		ShowIcon(title);
	}

	void GameListView::ShowIcon(const ShellLibrary::Title& title)
	{
		if (m_iconSet && title.icon == m_shownIcon)
			return;
		m_iconSet = true;
		m_shownIcon = title.icon;
		m_icon->clear();

		int texture = 0;
		if (title.icon)
		{
			NVGcontext* vg = brls::Application::getNVGContext();
			texture = nvgCreateImageMem(vg, 0, const_cast<unsigned char*>(title.icon->data()), static_cast<int>(title.icon->size()));
		}
		if (texture != 0)
		{
			m_icon->innerSetImage(texture);
			m_icon->setVisibility(brls::Visibility::VISIBLE);
			m_iconPlaceholder->setVisibility(brls::Visibility::GONE);
		}
		else
		{
			m_icon->setVisibility(brls::Visibility::GONE);
			m_iconPlaceholder->setVisibility(brls::Visibility::VISIBLE);
		}
	}

	void GameListView::Launch(const ShellLibrary::Title& title)
	{
		m_chosen = title;
		brls::Application::quit();
	}

	void GameListView::Rescan()
	{
		ShellLibrary::Rescan();
		brls::Application::notify("Looking for titles...");
	}
}
