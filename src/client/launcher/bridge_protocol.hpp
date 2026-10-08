#pragma once
#include <json.hpp>
#include <array>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <string>
#include <string_view>
#include <stdexcept>

namespace launcher_bridge
{
	inline constexpr std::string_view origin = "https://launcher.invalid";
	inline constexpr std::size_t max_message_bytes = 64 * 1024;
	inline constexpr std::array methods{"bootstrap", "settings.save", "settings.disableRisk",
		"preferences.language", "languages.scan", "languages.save",
		"languages.poll", "game.preflight", "game.fix", "game.launch", "window.control", "window.state", "links.open", "renderer.ready"};

	inline bool trusted_source(std::string_view source, std::string_view expected = origin)
	{
		return source == expected || (source.starts_with(expected) && source.size() > expected.size()
			&& source[expected.size()] == '/');
	}

	struct request
	{
		int id{};
		std::string session;
		std::string method;
		nlohmann::json params;
	};

	inline request parse(std::string_view text)
	{
		if (text.empty() || text.size() > max_message_bytes) throw std::runtime_error("Launcher message is too large.");
		int depth{};
		bool quoted{}, escaped{};
		for (const auto c : text)
		{
			if (quoted)
			{
				if (escaped) escaped = false;
				else if (c == '\\') escaped = true;
				else if (c == '"') quoted = false;
			}
			else if (c == '"') quoted = true;
			else if (c == '{' || c == '[') { if (++depth > 16) throw std::runtime_error("Launcher message is too deeply nested."); }
			else if (c == '}' || c == ']') --depth;
		}
		const auto data = nlohmann::json::parse(text, nullptr, false);
		if (!data.is_object() || data.size() != 4 || !data.contains("id") || !data["id"].is_number_integer()
			|| !data.contains("session") || !data["session"].is_string()
			|| !data.contains("method") || !data["method"].is_string()
			|| !data.contains("params") || !data["params"].is_object()) throw std::runtime_error("Invalid launcher request.");
		const auto id = data["id"].get<std::int64_t>();
		const auto session = data["session"].get<std::string>();
		const auto method = data["method"].get<std::string>();
		if (id <= 0 || id > 0x7fffffff || session.empty() || session.size() > 64
			|| session.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") != std::string::npos
			|| std::find(methods.begin(), methods.end(), method) == methods.end()) throw std::runtime_error("Unsupported launcher request.");
		return {static_cast<int>(id), session, method, data["params"]};
	}

	inline nlohmann::json response(const request& request, nlohmann::json result)
	{
		return {{"id", request.id}, {"session", request.session}, {"result", std::move(result)}};
	}

	// Windows CRT argument quoting, including embedded quotes and trailing slashes.
	inline std::wstring quote_argument(std::wstring_view value)
	{
		std::wstring result = L"\"";
		std::size_t slashes{};
		for (const auto c : value)
		{
			if (c == L'\\') { ++slashes; continue; }
			result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
			slashes = 0;
			result += c;
		}
		result.append(slashes * 2, L'\\');
		return result + L'"';
	}
}
