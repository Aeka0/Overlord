#pragma once
#include "vr_settings_config.hpp"
#include "game/dvar_hash.hpp"
#include <array>
#include <span>

namespace launcher_vr_settings
{
	struct risk_setting
	{
		const char* name;
		const char* disabled_value;
	};

	// Native PCVideo/AdvancedVideo options, committed by PCOptions. SSAA Off
	// means one sample; shadow caches are enums, not the derived *Enabled bools.
	// Disable both frontend precaching and level shader preloading. The latter
	// also gates its subordinate AfterCinematic timing option.
	inline constexpr std::array<risk_setting, 5> risk_settings{{
		{"r_ssaaSamples", "1"},
		{"r_preloadShadersFrontendAllow", "0"},
		{"r_preloadShaders", "0"},
		{"sm_cacheSunShadow", "Disabled"},
		{"sm_cacheSpotShadows", "Disabled"},
	}};

	inline bool risk_name(std::string_view saved, const char* name)
	{
		if (same_name(saved, name)) return true;
		if (saved.size() <= 2 || saved.substr(0, 2) != "0x") return false;
		std::uint32_t hash{};
		const auto parsed = std::from_chars(saved.data() + 2, saved.data() + saved.size(), hash, 16);
		return parsed.ec == std::errc{} && parsed.ptr == saved.data() + saved.size() &&
			hash == static_cast<std::uint32_t>(dvars::generate_hash(name));
	}

	inline std::array<std::optional<std::string>, risk_settings.size()> read_risk_settings(std::string_view config)
	{
		std::array<std::optional<std::string>, risk_settings.size()> result{};
		if (config.substr(0, 3) == "\xef\xbb\xbf") config.remove_prefix(3);
		while (!config.empty())
		{
			const auto end = config.find('\n');
			const auto parts = tokens(config.substr(0, end));
			if (parts.size() == 3 && (same_name(parts[0], "set") || same_name(parts[0], "seta")))
				for (std::size_t i{}; i < risk_settings.size(); ++i)
					if (risk_name(parts[1], risk_settings[i].name)) result[i] = parts[2];
			if (end == std::string_view::npos) break;
			config.remove_prefix(end + 1);
		}
		return result;
	}

	inline std::span<const risk_setting> risk_group(std::string_view id)
	{
		if (id == "ssaa") return {risk_settings.data(), 1};
		if (id == "shaders") return {risk_settings.data() + 1, 2};
		if (id == "shadows") return {risk_settings.data() + 3, 2};
		throw std::runtime_error("Unsupported risk-setting repair.");
	}

	inline std::string disable_risk_settings(std::string_view config, std::span<const risk_setting> fields = risk_settings)
	{
		auto result = remove_config_assignments(config, [fields](const auto& parts)
		{
			if (parts.size() != 3 || (!same_name(parts[0], "set") && !same_name(parts[0], "seta"))) return false;
			for (const auto& field : fields) if (risk_name(parts[1], field.name)) return true;
			return false;
		});
		for (const auto& field : fields)
			result += std::string("seta ") + field.name + " \"" + field.disabled_value + "\"\r\n";
		return result;
	}
}
