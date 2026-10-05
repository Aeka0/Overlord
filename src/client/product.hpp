#pragma once
#include <string>
#include <string_view>

namespace product
{
	inline constexpr auto name = "H2-MOD VR";
	inline constexpr auto repository_url = "https://github.com/Aeka0/h2-mod-vr";
	inline constexpr auto releases_url = "https://github.com/Aeka0/h2-mod-vr/releases";

	constexpr std::string_view localization_key(std::string_view key) noexcept
	{
		if (!key.empty() && key.front() == '@') key.remove_prefix(1);
		return key;
	}
	constexpr const char* fixed_menu_text(std::string_view key) noexcept
	{
		key = localization_key(key);
		if (key == "MENU_SP_CAMPAIGN" || key == "MENU_GENERAL") return name;
		if (key == "MENU_SYSINFO_CUSTOMER_SUPPORT_URL" || key == "MENU_SYSINFO_DONATION_URL") return repository_url;
		return nullptr;
	}
	constexpr const char* menu_text_alias(std::string_view key) noexcept
	{
		// The old generic donation route is now a project link. Keep its label
		// honest without inventing a donation destination for this project.
		return localization_key(key) == "MENU_SYSINFO_DONATION_LINK" ? "MENU_SYSINFO_CUSTOMER_SUPPORT_LINK" : nullptr;
	}
	constexpr bool product_description(std::string_view key) noexcept
	{
		key = localization_key(key);
		return key == "MENU_GENERAL_DESC" || key == "LUA_MENU_FALLBACK_ENABLE" || key == "LUA_MENU_SWITCH_BRANCH_DESC";
	}
	inline std::string menu_description(std::string_view key, std::string text)
	{
		// Only these product descriptions are ours. Never rewrite dialogue,
		// credits, URLs inside documents, asset identifiers or arbitrary prose.
		if (!product_description(key) || text.size() > 4096) return text;
		constexpr std::string_view legacy = "h2-mod";
		const auto lower = [](char c) {return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c;};
		for (std::size_t at = 0; at + legacy.size() <= text.size(); ++at)
		{
			bool match = true;
			for (std::size_t i = 0; i < legacy.size(); ++i) match &= lower(text[at + i]) == legacy[i];
			if (!match) continue;
			const auto after = at + legacy.size();
			if (after + 3 <= text.size() && (text[after] == ' ' || text[after] == '-') &&
				lower(text[after + 1]) == 'v' && lower(text[after + 2]) == 'r') continue;
			text.replace(at, legacy.size(), name);
			at += std::char_traits<char>::length(name) - 1;
		}
		return text;
	}
}
