#pragma once

#include <string>
#include <optional>
#include <future>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>

namespace utils::http
{
	using headers = std::unordered_map<std::string, std::string>;
	struct request_options
	{
		std::chrono::milliseconds connect_timeout{std::chrono::seconds(10)};
		std::chrono::milliseconds timeout{std::chrono::seconds(60)};
		std::size_t max_response_bytes{(std::numeric_limits<std::size_t>::max)()};
		// Called on the request owner, including idle transfer progress. A true
		// result aborts the transfer; it is never a request to publish partial data.
		std::function<bool()> cancelled;
	};

	std::optional<std::string> get_data(const std::string& url, const headers& headers = {}, 
		const std::function<void(size_t, size_t, size_t)>& callback = {},
		const request_options& options = {});
	std::future<std::optional<std::string>> get_data_async(const std::string& url, const headers& headers = {});
}
