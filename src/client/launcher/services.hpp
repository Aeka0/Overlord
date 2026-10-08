#pragma once
#include "bridge_protocol.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace launcher_bridge
{
	// File mutations are ordered; language enumeration has its own worker.
	class services final
	{
	public:
		services();
		~services();
		void enqueue(request request);
		std::vector<nlohmann::json> drain();
		bool idle();
	private:
		std::mutex mutex_;
		std::condition_variable wake_;
		std::deque<request> requests_;
		std::deque<nlohmann::json> responses_;
		bool stopping_{};
		bool busy_{};
		std::thread worker_;
		void run();
	};
}
