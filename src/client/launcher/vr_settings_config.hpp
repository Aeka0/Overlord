#pragma once
#include "component/vr/settings.hpp"
#include <json.hpp>
#include <cmath>
#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace launcher_vr_settings
{
	using json = nlohmann::json;
	inline constexpr std::size_t max_config_bytes = 4 * 1024 * 1024;
	inline constexpr std::size_t max_payload_bytes = 4096;
	inline double setting_number(float value) noexcept {return std::round(double(value)*1000000)/1000000;}
	inline json controller_presets(bool standard = false)
	{
		auto result=json::array();
		const auto& fields = standard ? vr::settings::standard_hand_alignment : vr::settings::hand_alignment;
		for (const auto& preset:vr::settings::alignment_presets)
		{
			json values=json::object();
			for (std::size_t i=0;i<vr::settings::hand_alignment.size();++i)
				values[fields[i].name]=setting_number(preset.values[i]);
			result.push_back({{"id",preset.id},{"label",preset.label},{"values",values}});
		}
		return result;
	}

	inline json choice_catalog()
	{
		json result=json::object();
		for(const auto& field:vr::settings::choices)
		{
			json options=json::array();
			for(std::size_t i=0;i<field.values.size() && field.values[i];++i)
				options.push_back({{"value",field.values[i]},{"labelKey",field.label_keys[i]}});
			result[field.name]=std::move(options);
		}
		return result;
	}

	inline json defaults()
	{
		json result=json::object();
		for(const auto& field:vr::settings::choices)result[field.name]=field.values[field.default_index];
		for(const auto& field:vr::settings::toggles)result[field.name]=field.default_value;
		for (const auto& field : vr::settings::numbers)
			result[field.name] = setting_number(field.default_value);
		return result;
	}

	inline bool validate(const json& values)
	{
		if (!values.is_object() || values.size() != vr::settings::numbers.size() + vr::settings::toggles.size() + vr::settings::choices.size()) return false;
		for(const auto& field:vr::settings::choices)
		{
			const auto value=values.find(field.name);
			if(value==values.end() || !value->is_string() ||
				!std::any_of(field.values.begin(),field.values.end(),[&](const char* option){return option && *value==option;}))return false;
		}
		for(const auto& field:vr::settings::toggles)
		{const auto value=values.find(field.name);if(value==values.end() || !value->is_boolean())return false;}
		for (const auto& field : vr::settings::numbers)
		{
			const auto value = values.find(field.name);
			if (value == values.end() || !value->is_number()) return false;
			const auto number = value->get<double>();
			if (!std::isfinite(number) || number < setting_number(field.min) || number > setting_number(field.max)) return false;
		}
		return true;
	}

	enum class startup_source { launcher, direct };
	// The launcher controls the upcoming child game process. Direct starts
	// retain an explicit command-line environment override for diagnosis.
	inline std::optional<std::string> backend_environment_update(
		const json& values, startup_source source, bool has_environment_override)
	{
		if (source == startup_source::direct && has_environment_override) return std::nullopt;
		if (!validate(values)) throw std::runtime_error("Invalid VR startup settings.");
		return values.at(vr::settings::runtime_backend.name).get<std::string>();
	}

	inline vr::debug_options::selection debug_selection(const json& values)
	{
		vr::debug_options::selection result{};
		for (std::size_t i{}; i < result.size(); ++i)
		{
			const auto value = values.find(vr::debug_options::names[i]);
			result[i] = value != values.end() && value->is_boolean() && value->get<bool>();
		}
		return result;
	}

	inline bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

	// Native config output uses one set/seta command per line. Only replace
	// complete recognized assignments; retain binds, comments and other bytes.
	inline std::vector<std::string> tokens(std::string_view line)
	{
		std::vector<std::string> result;
		std::size_t pos{};
		while (pos < line.size())
		{
			while (pos < line.size() && space(line[pos])) ++pos;
			if (pos == line.size() || line.substr(pos, 2) == "//") break;
			const bool quoted = line[pos] == '"';
			if (quoted) ++pos;
			const auto begin = pos;
			while (pos < line.size() && (quoted ? line[pos] != '"' : !space(line[pos])))
			{
				if (line[pos] == ';' || line[pos] == '\\') return {};
				++pos;
				if (pos - begin > 128) return {};
			}
			if (quoted && pos == line.size()) return {};
			result.emplace_back(line.substr(begin, pos - begin));
			if (quoted) ++pos;
			if (result.size() > 3 || (pos < line.size() && !space(line[pos]) && line.substr(pos, 2) != "//")) return {};
		}
		return result;
	}

	inline bool same_name(std::string_view left, std::string_view right)
	{
		if (left.size() != right.size()) return false;
		for (std::size_t i = 0; i < left.size(); ++i)
		{
			const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
			if (lower(left[i]) != lower(right[i])) return false;
		}
		return true;
	}

	inline std::string setting_name(const std::vector<std::string>& parts)
	{
		if (parts.size() != 3 || (!same_name(parts[0], "seta") && !same_name(parts[0], "set"))) return {};
		for(const auto& field:vr::settings::choices)if(same_name(field.name,parts[1]))return field.name;
		for(const auto& field:vr::settings::toggles)if(same_name(field.name,parts[1]))return field.name;
		for (const auto& field : vr::settings::numbers)
			if (same_name(field.name, parts[1])) return field.name;
		return {};
	}

	inline bool has_vr_configuration(std::string_view config)
	{
		if (config.substr(0, 3) == "\xef\xbb\xbf") config.remove_prefix(3);
		while (!config.empty())
		{
			const auto end = config.find('\n');
			const auto parts = tokens(config.substr(0, end));
			// Include advanced VR dvars, not just fields exposed by this launcher.
			if (parts.size() == 3 && (same_name(parts[0], "set") || same_name(parts[0], "seta")) &&
				parts[1].size() > 3 && same_name(std::string_view(parts[1]).substr(0, 3), "vr_")) return true;
			if (end == std::string_view::npos) break;
			config.remove_prefix(end + 1);
		}
		return false;
	}

	inline json read_values(std::string_view config)
	{
		if (config.substr(0, 3) == "\xef\xbb\xbf") config.remove_prefix(3);
		auto result = defaults();
		while (!config.empty())
		{
			const auto end = config.find('\n');
			const auto parts = tokens(config.substr(0, end));
			const auto name = setting_name(parts);
			if (!name.empty())
			{
				auto candidate = result;
				const auto choice=std::find_if(vr::settings::choices.begin(),vr::settings::choices.end(),[&](const auto& field){return name==field.name;});
				if(choice!=vr::settings::choices.end())
				{
					candidate[name]=parts[2];
					for(std::size_t i=0;i<choice->values.size() && choice->values[i];++i)
						if(parts[2]==std::to_string(i))candidate[name]=choice->values[i];
				}
				else if (std::any_of(vr::settings::toggles.begin(),vr::settings::toggles.end(),[&](const auto& field){return name==field.name;}))
				{
					if (parts[2] == "0" || parts[2] == "1") candidate[name] = parts[2] == "1";
				}
				else
				{
					double number{};
					const auto& text = parts[2];
					const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
					if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(number))
						candidate[name] = number;
				}
				if (validate(candidate)) result = std::move(candidate);
			}
			if (end == std::string_view::npos) break;
			config.remove_prefix(end + 1);
		}
		return result;
	}

	template <typename Predicate>
	std::string remove_config_assignments(std::string_view config, Predicate replace)
	{
		std::string result;
		result.reserve(config.size() + 512);
		if (config.substr(0, 3) == "\xef\xbb\xbf")
		{
			result.append(config.substr(0, 3));
			config.remove_prefix(3);
		}
		while (!config.empty())
		{
			const auto end = config.find('\n');
			const auto length = end == std::string_view::npos ? config.size() : end + 1;
			if (!replace(tokens(config.substr(0, end)))) result.append(config.substr(0, length));
			config.remove_prefix(length);
		}
		if (!result.empty() && result != "\xef\xbb\xbf" && result.back() != '\n') result += "\r\n";
		return result;
	}

	inline std::string update_config(std::string_view config, const json& values)
	{
		if (!validate(values)) throw std::runtime_error("Invalid VR settings.");
		auto result = remove_config_assignments(config,
			[](const auto& parts) { return !setting_name(parts).empty(); });
		for (auto it = values.begin(); it != values.end(); ++it)
		{
			const auto value = it->is_string() ? it->get<std::string>() :
				it->is_boolean() ? (it->get<bool>() ? std::string("1") : std::string("0")) : it->dump();
			result += "seta " + it.key() + " \"" + value + "\"\r\n";
		}
		return result;
	}
}
