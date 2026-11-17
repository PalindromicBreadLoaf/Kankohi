#pragma once

#include <atomic>
#include <functional>
#include <list>
#include <mutex>
#include <thread>

class ThreadPool
{
public:
	// every task gets its own thread
	template<class TFunction, class... TArgs>
	static void FireAndForget(TFunction&& f, TArgs&&... args)
	{
		State& state = GetState();
		std::unique_lock _l(state.mutex);
		ReapFinished(state);
		Task& task = state.tasks.emplace_back();
		task.thread = std::thread([&finished = task.finished, f = std::forward<TFunction>(f), ... args = std::forward<TArgs>(args)]() mutable {
			std::invoke(std::move(f), std::move(args)...);
			finished.store(true);
		});
	}

	static void JoinAll()
	{
		State& state = GetState();
		std::unique_lock _l(state.mutex);
		while (!state.tasks.empty())
		{
			std::list<Task> tasks = std::move(state.tasks);
			state.tasks.clear();
			_l.unlock();
			for (Task& task : tasks)
				task.thread.join();
			_l.lock();
		}
	}

private:
	struct Task
	{
		std::thread thread;
		std::atomic_bool finished{false};
	};

	struct State
	{
		std::mutex mutex;
		std::list<Task> tasks;
	};

	static State& GetState()
	{
		static State* s_state = new State();
		return *s_state;
	}

	static void ReapFinished(State& state)
	{
		for (auto it = state.tasks.begin(); it != state.tasks.end();)
		{
			if (it->finished.load())
			{
				it->thread.join();
				it = state.tasks.erase(it);
			}
			else
				++it;
		}
	}
};
