#pragma once

#include <filesystem>
#include <fstream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <json.hpp>

namespace vr::diagnostics
{
	inline std::filesystem::path input_path(std::string_view utf8)
	{
		return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
	}

	inline std::string input_quoted(std::string value)
	{
		if (value.size() > 512) value = value.substr(0, 512) + "[truncated]";
		return nlohmann::json(value).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	}

	inline nlohmann::json read_input_manifest(std::ostream& out, const std::filesystem::path& path)
	{
		const auto utf8 = path.u8string();
		out << "    input_file: path=" << input_quoted(std::string(reinterpret_cast<const char*>(utf8.c_str())));
		std::error_code error;
		if (!std::filesystem::is_regular_file(path, error))
		{
			out << " state=unavailable filesystem_code=" << error.value() << '\n';
			return {};
		}
		const auto size = std::filesystem::file_size(path, error);
		out << " bytes=" << size;
		if (error || size > 1024 * 1024)
		{
			out << " state=unreadable_or_over_1MiB filesystem_code=" << error.value() << '\n';
			return {};
		}
		const auto modified = std::filesystem::last_write_time(path, error);
		if (!error) out << " modified_file_clock_ticks=" << modified.time_since_epoch().count();
		std::ifstream file(path, std::ios::binary);
		std::string bytes(static_cast<std::size_t>(size), '\0');
		file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		if (!file || file.gcount() != static_cast<std::streamsize>(size) || file.peek() != EOF)
		{
			out << " state=unreadable_or_changed_during_read\n";
			return {};
		}
		try
		{
			const auto document = nlohmann::json::parse(bytes, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
				if (depth > 32) throw std::runtime_error("input JSON nesting exceeds 32");
				return true;
			});
			out << " state=" << (document.is_object() ? "json_object" : "unexpected_json_type") << '\n';
			return document.is_object() ? document : nlohmann::json{};
		}
		catch (const std::exception& e)
		{
			out << " state=invalid_json error=" << input_quoted(e.what()) << '\n';
			return {};
		}
	}

	inline bool local_input_binding(const std::filesystem::path& path)
	{
		if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
		for (const auto& part : path) if (part == ".." || part == ".") return false;
		// URLs are descriptive only; diagnostics never fetch remote content.
		const auto utf8 = path.generic_u8string();
		return utf8.find(u8':') == std::u8string::npos && utf8.find(u8'\0') == std::u8string::npos;
	}
}
