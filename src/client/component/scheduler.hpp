#pragma once
#include "scheduler_context.hpp"

namespace scheduler
{
	static const bool cond_continue = false;
	static const bool cond_end = true;

	void schedule(const std::function<bool()>& callback, pipeline type = pipeline::async,
	              std::chrono::milliseconds delay = 0ms);
	void loop(const std::function<void()>& callback, pipeline type = pipeline::async,
	          std::chrono::milliseconds delay = 0ms);
	void once(const std::function<void()>& callback, pipeline type = pipeline::async,
	          std::chrono::milliseconds delay = 0ms);
	void on_game_initialized(const std::function<void()>& callback, pipeline type = pipeline::async,
							 std::chrono::milliseconds delay = 0ms);
}
