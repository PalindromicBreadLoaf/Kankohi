// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "switch/KeyboardSwitch.h"
#include "Cafe/OS/libs/swkbd/swkbd.h"
#include "Common/switch/HorizonKeyboard.h"

#include <condition_variable>

namespace
{
	std::mutex s_mutex;
	std::condition_variable s_condition;
	std::optional<SwkbdHostKeyboardRequest> s_request;
	std::optional<std::string> s_reply;
	bool s_replied = false;
	bool s_cancelled = false;

	std::optional<std::string> ShowFromGuest(const SwkbdHostKeyboardRequest& request)
	{
		std::unique_lock lock(s_mutex);
		if (s_cancelled)
			return std::nullopt;
		s_request = request;
		s_replied = false;
		s_condition.wait(lock, [] { return s_replied || s_cancelled; });
		s_request.reset();
		if (!s_replied)
			return std::nullopt;
		s_replied = false;
		return std::move(s_reply);
	}
}

namespace KeyboardSwitch
{
	void Install()
	{
		{
			std::lock_guard lock(s_mutex);
			s_request.reset();
			s_reply.reset();
			s_replied = false;
			s_cancelled = false;
		}
		swkbd_setHostKeyboard(ShowFromGuest);
	}

	void Pump()
	{
		HorizonKeyboard::Request request;
		{
			std::lock_guard lock(s_mutex);
			if (!s_request)
				return;
			request.initialText = std::move(s_request->initialText);
			request.guideText = std::move(s_request->guideText);
			request.maxLength = s_request->maxLength;
			s_request.reset();
		}

		cemuLog_log(LogType::Force, "System keyboard up {}", request.maxLength);
		std::optional<std::string> text = HorizonKeyboard::Show(request);
		{
			std::lock_guard lock(s_mutex);
			s_reply = std::move(text);
			s_replied = true;
		}
		s_condition.notify_all();
	}

	void Cancel()
	{
		{
			std::lock_guard lock(s_mutex);
			s_cancelled = true;
			s_request.reset();
		}
		s_condition.notify_all();
	}
}
