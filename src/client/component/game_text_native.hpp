#pragma once
#include "game_text_catalog.hpp"
#include <csetjmp>
#include "game/structs.hpp"

namespace game_text
{
	// Native language aliases describe voice/content editions, not a different
	// written HUD language. Unknown values retain the mandatory English fallback.
	inline constexpr locale native_locale(int language) noexcept
	{
		switch (language)
		{
		case game::LANGUAGE_FRENCH: return locale::french;
		case game::LANGUAGE_GERMAN: return locale::german;
		case game::LANGUAGE_ITALIAN: return locale::italian;
		case game::LANGUAGE_SPANISH: return locale::spanish;
		case game::LANGUAGE_RUSSIAN: case game::LANGUAGE_RUSSIAN_PARTIAL: return locale::russian;
		case game::LANGUAGE_POLISH: return locale::polish;
		case game::LANGUAGE_PORTUGUESE: return locale::portuguese;
		case game::LANGUAGE_JAPANESE_FULL: case game::LANGUAGE_JAPANESE_PARTIAL: return locale::japanese;
		case game::LANGUAGE_TRADITIONAL_CHINESE: return locale::traditional_chinese;
		case game::LANGUAGE_SIMPLIFIED_CHINESE: return locale::simplified_chinese;
		case game::LANGUAGE_ARABIC: return locale::arabic;
		case game::LANGUAGE_CZECH: return locale::czech;
		case game::LANGUAGE_SPANISHNA: return locale::spanish_latin_america;
		case game::LANGUAGE_KOREAN: return locale::korean;
		case game::LANGUAGE_TURKISH: return locale::turkish;
		default: return locale::english;
		}
	}
}
