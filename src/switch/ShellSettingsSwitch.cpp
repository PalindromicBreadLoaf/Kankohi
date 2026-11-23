// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "switch/ShellSettingsSwitch.h"
#include "switch/ShellConfigSwitch.h"

#include <memory>

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_selector.hpp>

namespace Shell
{
	namespace
	{
		class SaveOnClose
		{
		public:
			SaveOnClose() = default;
			~SaveOnClose()
			{
				ShellConfig::Save();
			}

			SaveOnClose(const SaveOnClose&) = delete;
			SaveOnClose& operator=(const SaveOnClose&) = delete;
		};

		brls::Box* CreatePageBox()
		{
			brls::Style style = brls::Application::getStyle();
			auto* box = new brls::Box(brls::Axis::COLUMN);
			box->setPadding(style["brls/tab_frame/content_padding_top_bottom"], style["brls/tab_frame/content_padding_sides"],
				style["brls/tab_frame/content_padding_top_bottom"], style["brls/tab_frame/content_padding_sides"]);
			return box;
		}

		brls::View* WrapPage(brls::Box* box)
		{
			auto* scroll = new brls::ScrollingFrame();
			scroll->setContentView(box);
			return scroll;
		}

		void AddHeader(brls::Box* box, const std::string& title)
		{
			auto* header = new brls::Header();
			header->setTitle(title);
			header->setMarginTop(box->getChildren().empty() ? 0 : 24);
			box->addView(header);
		}

		void AddNote(brls::Box* box, const std::string& text)
		{
			auto* label = new brls::Label();
			label->setText(text);
			label->setFontSize(16);
			label->setTextColor(brls::Application::getTheme()["brls/header/subtitle"]);
			label->setVerticalAlign(brls::VerticalAlign::TOP);
			label->setMargins(0, 16, 12, 16);
			box->addView(label);
		}

		void AddSetting(brls::Box* box, const ShellConfig::Setting& setting)
		{
			const int current = setting.get();
			if (setting.isToggle && current >= 0)
			{
				auto* cell = new brls::BooleanCell();
				cell->init(setting.title, current == 1, [set = setting.set](bool on) { set(on ? 1 : 0); });
				box->addView(cell);
				return;
			}

			std::vector<std::string> labels = setting.options;
			const int optionCount = static_cast<int>(labels.size());
			int selected = current;
			if (selected < 0)
			{
				labels.push_back("Custom");
				selected = optionCount;
			}
			auto* cell = new brls::SelectorCell();
			cell->init(setting.title, labels, selected, [set = setting.set, optionCount](int index) {
				if (index < optionCount)
					set(index);
			});
			box->addView(cell);
		}

		brls::View* CreatePage(const ShellConfig::Page& page)
		{
			brls::Box* box = CreatePageBox();
			for (const ShellConfig::Section& section : page.sections)
			{
				AddHeader(box, section.header);
				if (!section.note.empty())
					AddNote(box, section.note);
				for (const ShellConfig::Setting& setting : section.settings)
					AddSetting(box, setting);
			}
			return WrapPage(box);
		}

		brls::View* CreateAboutPage()
		{
			brls::Box* box = CreatePageBox();
			AddHeader(box, "Kankohi");
			for (const auto& [key, value] : ShellConfig::GetAbout())
			{
				auto* cell = new brls::DetailCell();
				cell->setText(key);
				cell->setDetailText(value);
				cell->setDetailTextColor(brls::Application::getTheme()["brls/text"]);
				box->addView(cell);
			}
			return WrapPage(box);
		}
	}

	brls::Activity* CreateSettingsActivity()
	{
		auto saveOnClose = std::make_shared<SaveOnClose>();
		auto* tabs = new brls::TabFrame();
		for (ShellConfig::Page& page : ShellConfig::GetPages())
		{
			const std::string name = page.name;
			tabs->addTab(name, [saveOnClose, page = std::move(page)] { return CreatePage(page); });
		}
		tabs->addSeparator();
		tabs->addTab("About", [saveOnClose] { return CreateAboutPage(); });

		auto* frame = new brls::AppletFrame(tabs);
		frame->setTitle("Settings");
		return new brls::Activity(frame);
	}
}
