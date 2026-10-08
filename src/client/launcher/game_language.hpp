#pragma once
#include <future>
#include <atomic>
#include <memory>
#include <string>

namespace launcher_game_language
{
	// Only the UI thread accesses the future. The worker owns its CASC handle
	// and never calls the engine or WebView2. Polling never waits on disk IO.
	class service
	{
	public:
		~service() { cancelled_->store(true); }
		std::string scan();
		std::string save(const std::string& language);
		std::string poll();
	private:
		std::shared_ptr<std::atomic<bool>> cancelled_ = std::make_shared<std::atomic<bool>>(false);
		std::future<std::string> worker_;
		std::string start(const std::string& language);
	};
}
