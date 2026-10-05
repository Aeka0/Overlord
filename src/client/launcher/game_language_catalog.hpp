#pragma once
#include <array>
#include <string_view>

namespace launcher_game_language
{
	struct language_entry { const char* name; const char* code; const char* label; };
	// Native LANGUAGE_COUNT_ORIGINAL names and official TVFS language roots.
	// Czech and Turkish are MOD extensions, not official language packs.
	inline constexpr std::array languages{
		language_entry{"english", "eng", "English"},
		language_entry{"french", "fra", "Français"},
		language_entry{"german", "deu", "Deutsch"},
		language_entry{"italian", "ita", "Italiano"},
		language_entry{"spanish", "spa", "Español"},
		language_entry{"russian", "rus", "Русский"},
		language_entry{"polish", "pol", "Polski"},
		language_entry{"portuguese", "por", "Português (Brasil)"},
		language_entry{"japanese_full", "jpf", "日本語（吹き替え）"},
		language_entry{"japanese_partial", "jpp", "日本語（字幕）"},
		language_entry{"traditional_chinese", "tch", "繁體中文"},
		language_entry{"simplified_chinese", "sch", "简体中文"},
		language_entry{"arabic", "ara", "العربية"},
		language_entry{"spanishna", "sna", "Español (Latinoamérica)"},
		language_entry{"korean", "kor", "한국어"},
		language_entry{"english_safe", "ens", "English (Safe)"},
		language_entry{"russian_partial", "rup", "Русский (субтитры)"},
	};
	constexpr bool official(std::string_view name) noexcept
	{
		for (const auto& entry : languages) if (name == entry.name) return true;
		return false;
	}
}
