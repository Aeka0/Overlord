#include <std_include.hpp>
#include "melee_weapon_policy.hpp"
#include "../settings.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::equipment::melee_weapons
{
	choice selected() noexcept
	{
		const auto* setting=game::Dvar_FindVar(settings::melee_weapon.name);
		return setting && setting->type == game::dvar_type::enumeration
			? sanitize(setting->current.integer) : choice::native;
	}
	bool cheat_tools_allowed() noexcept {return follows_cheats(selected());}

	class component final : public component_interface
	{
		void post_unpack() override
		{
			// Register before native profile execution: seta must resolve the
			// symbolic choice to an enum ordinal and retain its readable name.
			static auto values = settings::melee_weapon.values;
			dvars::register_enum(settings::melee_weapon.name, values.data(),
				settings::melee_weapon.default_index, game::DVAR_FLAG_SAVED,
				"Chest melee weapon; default follows official cheat substitutions");
		}
	};
}

REGISTER_COMPONENT(vr::gameplay::equipment::melee_weapons::component)
