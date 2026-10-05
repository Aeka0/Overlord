#pragma once
#include <future>
#include <string>

namespace launcher_game_language
{
	// Only the UI thread accesses the future. The worker owns its CASC handle
	// and never calls the engine or MSHTML. Polling never waits on disk IO.
	class service
	{
	public:
		std::string scan();
		std::string save(const std::string& language);
		std::string poll();
	private:
		std::future<std::string> worker_;
		std::string start(const std::string& language);
	};
}
