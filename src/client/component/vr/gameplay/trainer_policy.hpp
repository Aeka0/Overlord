#pragma once
#include "weapon_carry.hpp"
#include "melee_motion.hpp"
#include <string_view>

namespace vr::gameplay::trainer
{
	enum class switch_hint {none,primary,sidearm};
	inline switch_hint switch_event(std::string_view event) noexcept
	{return event=="did_action_primary"?switch_hint::primary:event=="did_action_sidearm"?switch_hint::sidearm:switch_hint::none;}
	inline bool completes_hint(switch_hint hint,std::uint32_t held,std::uint32_t pistol) noexcept
	{return held && pistol && ((hint==switch_hint::primary && held!=pistol) || (hint==switch_hint::sidearm && held==pistol));}
	// Dunn asks which gun the player is using. The native projection can keep
	// the other hand's gun selected; supporting it is not another draw.
	inline std::uint32_t tutorial_weapon(std::span<const weapons::carry::instance> held,
		std::uint32_t previous) noexcept
	{
		const weapons::carry::instance* newest{};
		for (const auto& item:held)
			if (item.id && item.at==weapons::carry::location::held && item.owner.can_fire() &&
				(!newest || item.owner.rear_revision>newest->owner.rear_revision)) newest=&item;
		// Empty hands must not satisfy the script's "current != pistol" test.
		return newest ? newest->id.weapon : previous;
	}
	inline bool knife_target(std::string_view map,std::string_view classname,
		std::string_view noteworthy,bool damageable,melee::tool tool) noexcept
	{
		return map=="trainer" && classname=="script_model" && damageable && tool==melee::tool::knife &&
			(noteworthy=="target_enemy" || noteworthy=="target_friendly");
	}
}
