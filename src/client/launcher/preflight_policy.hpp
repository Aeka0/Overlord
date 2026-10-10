#pragma once
#include "risk_settings_config.hpp"

namespace launcher_preflight
{
	using nlohmann::json;
	inline constexpr std::array game_dlls{"amd_ags_x64.dll", "bink2w64.dll"};
	enum class risk_state { safe, enabled, unknown };

	inline risk_state risk_value(std::size_t index, const std::optional<std::string>& value)
	{
		if (!value || index >= launcher_vr_settings::risk_settings.size()) return risk_state::unknown;
		const auto type = launcher_vr_settings::risk_settings[index].type;
		if (type == launcher_vr_settings::risk_value_type::toggle_enum)
		{
			if (launcher_vr_settings::same_name(*value, "Disabled") || *value == "0") return risk_state::safe;
			if (launcher_vr_settings::same_name(*value, "Enabled") || *value == "1") return risk_state::enabled;
			return risk_state::unknown;
		}
		if (type == launcher_vr_settings::risk_value_type::boolean)
		{
			if (launcher_vr_settings::same_name(*value, "false")) return risk_state::safe;
			if (launcher_vr_settings::same_name(*value, "true")) return risk_state::enabled;
		}
		double number{};
		const auto parsed = std::from_chars(value->data(), value->data() + value->size(), number);
		if (parsed.ec != std::errc{} || parsed.ptr != value->data() + value->size() || !std::isfinite(number)) return risk_state::unknown;
		if (type == launcher_vr_settings::risk_value_type::samples) return number == 1 ? risk_state::safe : number > 1 ? risk_state::enabled : risk_state::unknown;
		return number == 0 ? risk_state::safe : number == 1 ? risk_state::enabled : risk_state::unknown;
	}

	inline void append_risks(json& issues, std::string_view profile, bool fixable)
	{
		const auto values = launcher_vr_settings::read_risk_settings(profile);
		for (const auto* id : {"ssaa", "shaders", "shadows", "fillMemory"})
		{
			const auto fields = launcher_vr_settings::risk_group(id);
			const auto begin = static_cast<std::size_t>(fields.data() - launcher_vr_settings::risk_settings.data());
			auto state = risk_state::safe;
			for (auto i = begin; i < begin + fields.size(); ++i)
			{
				const auto current = risk_value(i, values[i]);
				if (current == risk_state::enabled) state = current;
				else if (current == risk_state::unknown && state != risk_state::enabled) state = current;
			}
			if (state == risk_state::safe) continue;
			const bool ssaa = begin == 0;
			const auto details = ssaa && state == risk_state::enabled ? json{{"value", *values[begin]}} : json::object();
			issues.push_back({{"id", id}, {"severity", ssaa ? "error" : "warning"},
				{"titleKey", std::string("preflight.") + id},
				{"detailKey", state == risk_state::unknown ? "preflight.unknownSetting" : std::string("preflight.") + id + "Detail"},
				{"values", details}, {"fixable", fixable}});
		}
	}

	inline json report(json issues, bool game_available)
	{
		const bool blocked = std::any_of(issues.begin(), issues.end(), [](const auto& issue) { return issue.at("severity") == "error"; });
		return {{"ok", true}, {"issues", std::move(issues)}, {"canLaunch", game_available && !blocked}, {"gameAvailable", game_available}};
	}

	// Acknowledgements live only in this launch request. Newly discovered
	// warnings still require review; a blocking error is never overridable.
	inline bool launch_allowed(const json& report, const json& acknowledged)
	{
		if (!report.at("canLaunch").get<bool>()) return false;
		for (const auto& issue : report.at("issues"))
			if (std::find(acknowledged.begin(), acknowledged.end(), issue.at("id")) == acknowledged.end()) return false;
		return true;
	}
}
