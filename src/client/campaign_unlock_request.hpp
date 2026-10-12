#pragma once
#include <stdexcept>
#include <string_view>

namespace campaign_unlock
{
	inline bool parse(std::string_view value)
	{
		if (value == "0") return false;
		if (value == "1") return true;
		throw std::runtime_error("Invalid campaign unlock request.");
	}

	// Owned by the game process, so rebuilding the Lua VM cannot replay a request.
	class request
	{
		bool pending_;
	public:
		explicit request(std::string_view value) : pending_(parse(value)) {}
		bool take(bool frontend)
		{
			if (!frontend) return false;
			const auto value = pending_;
			pending_ = false;
			return value;
		}
	};
}
