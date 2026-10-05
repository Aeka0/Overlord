#pragma once
#include "resource.hpp"
#include <array>
#include <string>
#include <string_view>
#include <json.hpp>

namespace launcher_localization
{
	struct locale { const char* id; int resource; };
	inline constexpr std::array locales{locale{"en", LAUNCHER_EN}, locale{"zh-CN", LAUNCHER_ZH_CN},
		locale{"zh-TW", LAUNCHER_ZH_TW}, locale{"ru", LAUNCHER_RU}, locale{"fr", LAUNCHER_FR},
		locale{"de", LAUNCHER_DE}, locale{"es", LAUNCHER_ES}, locale{"ja", LAUNCHER_JA}, locale{"ko", LAUNCHER_KO}};
	inline constexpr const char* default_locale = "en";
	inline constexpr size_t max_preferences_bytes = 4096;
	inline std::string match_system_locale(std::string_view name)
	{
		if (name.empty() || name.size() > 85) return default_locale;
		std::string normalized(name);
		for (auto& c : normalized)
		{
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
			if (c == '_') c = '-';
		}
		const auto separator = normalized.find('-');
		const auto base = normalized.substr(0, separator);
		if (base == "zh")
		{
			// Explicit script wins over region (e.g. zh-Hans-HK).
			const auto tagged = "-" + normalized + "-";
			if (tagged.find("-hans-") != std::string::npos) return "zh-CN";
			if (tagged.find("-hant-") != std::string::npos || tagged.find("-tw-") != std::string::npos ||
				tagged.find("-hk-") != std::string::npos || tagged.find("-mo-") != std::string::npos) return "zh-TW";
			return "zh-CN";
		}
		for (const auto* id : {"en", "ru", "fr", "de", "es", "ja", "ko"}) if (base == id) return id;
		return default_locale;
	}
	inline bool supported(std::string_view id) noexcept
	{
		for (const auto& value : locales) if (id == value.id) return true;
		return false;
	}
	inline std::string preference(std::string_view text)
	{
		if (text.size() > max_preferences_bytes) return default_locale;
		const auto json = nlohmann::json::parse(text, nullptr, false);
		if (!json.is_object()) return default_locale;
		const auto value = json.find("language");
		if (value == json.end() || !value->is_string()) return default_locale;
		const auto id = value->get<std::string>();
		return supported(id) ? id : default_locale;
	}
	std::string scripts();
	std::string load();
	std::string save(const std::string& language);
}
