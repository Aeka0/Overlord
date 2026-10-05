#pragma once
#include "weapon_holding.hpp"
#include "special_melee.hpp"
#include <string_view>
namespace vr::gameplay::hands
{
	// H2's native definition class values are zero-based (rifle 0 through
	// rocket launcher 6). The legacy shared game enum starts rifles at 1.
	inline bool personal_firearm_definition(int type,int category,int inventory,std::string_view name={}) noexcept
	{return weapons::special_melee::supported(name) || (inventory==0 && (((type==1 || type==3) && category>=0 && category<=6) || (type==1 && category==11 && name=="usp_laserdesignator")));}
	// Ownership does not revert to flat-screen firearms while the VR carry
	// owner, tracking or a cinematic permission is temporarily unavailable.
	inline bool suppress_legacy_viewmodel(bool vr_requested,bool replacing_all,bool personal_firearm) noexcept
	{return vr_requested && (replacing_all || personal_firearm);}
	// A driver hands-only DObj has no firearm owner. Carry intentionally retains
	// the pre-boarding hold for exit; that suspended hold must not reject this rig.
	// Actual weapon DObjs and ordinary on-foot handoffs keep their ownership checks.
	inline weapons::hold model_pose_owner(weapons::hold retained,bool hands_only,bool driver_hands) noexcept
	{return hands_only && driver_hands?weapons::hold{}:retained;}
}
