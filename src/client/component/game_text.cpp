#include <std_include.hpp>
#include "game_text.hpp"
#include "game_text_native.hpp"
#include "game/game.hpp"

namespace game_text
{
	locale current() noexcept
	{
		// Snapshot once per UI transaction, independently of launcher language.
		// Preserve an unavailable language as unknown. Prompt overrides then
		// defer to native; captions without a native source can still use text().
		const auto* language=game::Dvar_FindVar("loc_language");
		if(!language || language->current.integer<0 || language->current.integer>=game::LANGUAGE_COUNT)return locale::count;
		return native_locale(language->current.integer);
	}
}
