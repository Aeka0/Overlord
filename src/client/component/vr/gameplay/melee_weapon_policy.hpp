#pragma once
#include "body_equipment.hpp"

namespace vr::gameplay::equipment::melee_weapons
{
	enum class choice : unsigned { native, ghost, baton, karambit, ending, bloody };
	enum class kind : unsigned { ghost, baton, karambit, ending, bloody, bayonet, count };
	inline constexpr choice sanitize(int value) noexcept
	{return value>=0 && value<=int(choice::bloody)?choice(value):choice::native;}
	inline constexpr bool follows_cheats(choice value) noexcept {return value==choice::native;}
	inline constexpr kind resolve(choice value,bool bayonet=false) noexcept
	{
		if(value==choice::native)return bayonet?kind::bayonet:kind::ghost;
		return kind(unsigned(value)-1);
	}
	inline constexpr knife_grip grip(kind value,knife_grip requested) noexcept
	{return value==kind::baton?knife_grip::forward:value==kind::karambit?knife_grip::reverse:requested;}
	inline constexpr int fixed_damage(kind value) noexcept {return value==kind::baton?120:0;}
	inline constexpr bool blade_feedback(kind value) noexcept {return value!=kind::baton;}
	choice selected() noexcept;
	bool cheat_tools_allowed() noexcept;
}
