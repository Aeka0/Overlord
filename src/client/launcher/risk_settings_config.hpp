#pragma once
#include "vr_settings_config.hpp"
#include "game/dvar_hash.hpp"
#include <array>

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

	inline std::string disable_risk_settings(std::string_view config)
	{
		auto result = remove_config_assignments(config, [](const auto& parts)
		{
			if (parts.size() != 3 || (!same_name(parts[0], "set") && !same_name(parts[0], "seta"))) return false;
			// Engine-written profiles may contain hexadecimal hashes instead of names.
			std::uint32_t hash{};
			bool hashed = false;
			if (parts[1].compare(0, 2, "0x") == 0)
			{
				const auto begin = parts[1].data() + 2, end = parts[1].data() + parts[1].size();
				const auto parsed = std::from_chars(begin, end, hash, 16);
				hashed = parsed.ec == std::errc{} && parsed.ptr == end;
			}
			for (const auto& field : risk_settings)
				if (same_name(parts[1], field.name) ||
					(hashed && hash == static_cast<std::uint32_t>(dvars::generate_hash(field.name))))
					return true;
			return false;
		});
		for (const auto& field : risk_settings)
			result += std::string("seta ") + field.name + " \"" + field.disabled_value + "\"\r\n";
		return result;
	}
}
