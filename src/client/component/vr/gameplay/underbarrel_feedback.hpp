#pragma once
#include "underbarrel_feed.hpp"
#include "weapon_sound_reference.hpp"

namespace vr::gameplay::weapons::underbarrel
{
	inline bool ejects_spent_case(const transaction& tx) noexcept
	{
		// Opening a live round has a separate ammunition debit. It must never
		// be presented as a spent case, nor may an empty opening create brass.
		return tx && (tx.op == operation::open || tx.op == operation::extract_case) &&
		       tx.next.id.type == kind::m203 && tx.ejected && tx.spent == 0;
	}
	inline sound_reference interaction_sound(kind type,operation op) noexcept
	{
		if(op!=operation::open && op!=operation::close && op!=operation::insert)return {};
		if(type==kind::m203)return {op==operation::open?"weap_m203_chamber_open_plr":
			op==operation::close?"weap_m203_chamber_close_plr":"weap_m203_load_plr"};
		if(type==kind::gp25)return {"weap_gp25_chamber_plr"};
		// SCAR maps Winchester notetrack keys to these aliases; AK/FAL use the
		// alias names as keys too. The shared, captured native aliases are explicit.
		if(type==kind::shotgun)return {op==operation::insert?"weap_shotattach_clipin_plr":"weap_shotattach_chamber_plr",
			sound_reference_kind::alias};
		return {};
	}
}
