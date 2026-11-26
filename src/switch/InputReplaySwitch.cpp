// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "switch/InputReplaySwitch.h"
#include "Common/switch/HorizonInput.h"
#include "config/LaunchSettings.h"
#include "switch/PerfMonitorSwitch.h"

#include <mutex>

namespace
{
	constexpr float STICK_RANGE = 32767.0f;
	constexpr const char* FILE_HEADER = "kankohi-input 1";

	struct Entry
	{
		uint64 frame = 0;
		uint64 buttons = 0;
		std::array<sint32, 4> sticks{};

		bool HasSameInput(const Entry& other) const
		{
			return buttons == other.buttons && sticks == other.sticks;
		}
	};

	std::mutex s_mutex;
	std::ofstream s_recording;
	std::vector<Entry> s_replay;
	size_t s_replayIndex = 0;
	Entry s_lastRecorded;
	bool s_replaying = false;

	void Record(const HorizonInput::PadSnapshot& snapshot)
	{
		Entry entry;
		entry.frame = PerfMonitor::GetFrameCount();
		entry.buttons = snapshot.buttons;
		for (size_t i = 0; i < entry.sticks.size(); i++)
			entry.sticks[i] = (sint32)std::lround(snapshot.sticks[i] * STICK_RANGE);
		if (entry.HasSameInput(s_lastRecorded))
			return;
		s_lastRecorded = entry;
		s_recording << fmt::format("{} {:x} {} {} {} {}\n", entry.frame, entry.buttons, entry.sticks[0], entry.sticks[1],
			entry.sticks[2], entry.sticks[3]);
	}

	void Replay(HorizonInput::PadSnapshot& snapshot)
	{
		const uint64 frame = PerfMonitor::GetFrameCount();
		while (s_replayIndex + 1 < s_replay.size() && s_replay[s_replayIndex + 1].frame <= frame)
			s_replayIndex++;
		const Entry& entry = s_replay[s_replayIndex];
		snapshot.connected = true;
		snapshot.buttons = entry.buttons;
		for (size_t i = 0; i < entry.sticks.size(); i++)
			snapshot.sticks[i] = (float)entry.sticks[i] / STICK_RANGE;
		std::fill(std::begin(snapshot.triggers), std::end(snapshot.triggers), 0.0f);
		snapshot.hasMotion = false;
	}

	void FilterSnapshot(int player, HorizonInput::PadSnapshot& snapshot)
	{
		if (player != 0)
			return;
		std::scoped_lock lock(s_mutex);
		if (s_replaying)
			Replay(snapshot);
		else if (s_recording.is_open())
			Record(snapshot);
	}

	bool LoadReplay(const std::string& path)
	{
		std::ifstream file(path);
		std::string line;
		if (!std::getline(file, line) || line != FILE_HEADER)
			return false;
		s_replay = {Entry{}};
		while (std::getline(file, line))
		{
			Entry entry;
			std::istringstream fields(line);
			fields >> entry.frame >> std::hex >> entry.buttons >> std::dec;
			for (sint32& stick : entry.sticks)
				fields >> stick;
			if (fields.fail() || entry.frame < s_replay.back().frame)
				return false;
			s_replay.push_back(entry);
		}
		return true;
	}
}

namespace InputReplay
{
	void Start()
	{
		std::scoped_lock lock(s_mutex);
		s_replayIndex = 0;
		s_lastRecorded = {};
		s_replaying = false;
		if (std::optional<std::string> path = LaunchSettings::GetReplayInputPath())
		{
			s_replaying = LoadReplay(*path);
			if (s_replaying)
				cemuLog_log(LogType::Force, "Replaying {} input changes from {}", s_replay.size() - 1, *path);
			else
				cemuLog_log(LogType::Force, "Failed to load the input recording {}", *path);
		}
		else if (std::optional<std::string> recordPath = LaunchSettings::GetRecordInputPath())
		{
			s_recording.open(*recordPath, std::ios::trunc);
			if (s_recording.is_open())
				s_recording << FILE_HEADER << '\n';
			else
				cemuLog_log(LogType::Force, "Failed to create the input recording {}", *recordPath);
		}
		if (s_replaying || s_recording.is_open())
			HorizonInput::SetSnapshotFilter(FilterSnapshot);
	}

	void Stop()
	{
		HorizonInput::SetSnapshotFilter(nullptr);
		std::scoped_lock lock(s_mutex);
		s_replaying = false;
		s_replay.clear();
		if (s_recording.is_open())
			s_recording.close();
	}
}
