#pragma once
#include "physical_reload_profile.hpp"

namespace vr::gameplay::weapons
{
	// Presentation of an owned LIVE chamber round only. A committed extraction
	// clears chamber_loaded; spent cases and uncommitted speculative feeding do
	// not borrow this geometry. Bolt rotation never rotates the cartridge.
	inline std::optional<hands::anchor> chamber_cartridge_pose(const reload_profile& p,const mechanics::state& ammo,
		float manual_travel_m,float manual_fraction,float units) noexcept
	{
		if(!ammo.chamber_loaded || !std::isfinite(units) || units<=0 || !std::isfinite(manual_travel_m) ||
			!std::isfinite(manual_fraction))return {};
		hands::anchor pose;
		float travel{};
		if(p.interaction.manual_bolt && p.feeding_path)
		{
			if(ammo.bolt.spent_case || ammo.bolt.feeding)return {};
			pose=(*p.feeding_path)[2];
			travel=std::clamp(manual_fraction,0.f,.85f)*p.interaction.manual_bolt->stroke;
		}
		else if(p.chamber_round)
		{
			pose=*p.chamber_round;
			travel=std::clamp(manual_travel_m,0.f,p.interaction.full_stroke);
		}
		else return {};
		pose.position=hands::add(pose.position,hands::scale(p.interaction.slide_axis,travel*units));
		return pose;
	}
}
