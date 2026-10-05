#pragma once
#include "physical_reload_profile.hpp"

namespace vr::gameplay::weapons
{
	// Explicit opt-in for box magazines whose authored local +Z is the feed axis.
	// Resolve once while constructing immutable profiles, not on a tracking tick.
	// Mesh size, finger poses and contact offsets remain the authored resources.
	inline reload_profile with_controller_magazine(reload_profile p) noexcept
	{
		const auto valid_anchor=[](const hands::anchor& a) {
			for(float x:a.position)if(!std::isfinite(x))return false;
			float norm{};for(float x:a.rotation){if(!std::isfinite(x))return false;norm+=x*x;}
			return std::abs(norm-1.f)<.001f;
		};
		// Unsupported or incomplete profiles keep their entire legacy policy.
		// Top-fed, facing-disambiguated magazines and pistol co-grasps are separate.
		if(p.interaction.belt || p.interaction.support_magazine_catch || !p.magazine_contacts ||
			(p.interaction.manual_magazine && p.interaction.manual_magazine->prefer_grasp_facing) ||
			p.magazine_grasps.size()>max_part_grips || !valid_anchor(p.magazine_rest))return p;
		std::uint8_t selected{};
		if(!p.magazine_grasps.empty())
		{
			selected=no_part_grip;
			for(std::size_t i=0;i<p.magazine_grasps.size();++i)
				if(p.magazine_grasps[i].kind==magazine_grasp_kind::body_wrap){selected=static_cast<std::uint8_t>(i);break;}
			if(selected==no_part_grip)for(std::size_t i=0;i<p.magazine_grasps.size();++i)
				if(p.magazine_grasps[i].kind==magazine_grasp_kind::native){selected=static_cast<std::uint8_t>(i);break;}
			if(selected==no_part_grip)return p;
		}
		const auto pose=p.magazine_grasps.empty() ?
			magazine_grasp_pose{p.magazine_in_wrist,p.magazine_contacts->grip_contact,p.magazine_fingers} : p.magazine_grasps[selected];
		if(!valid_anchor(pose.in_wrist) || pose.fingers.empty())return p;
		for(float x:pose.contact_in_wrist)if(!std::isfinite(x))return p;
		p.magazine_selection=magazine_grasp_policy::fixed;
		p.magazine_default_pose=selected;
		p.magazine_tracking=magazine_tracking_frame::controller;
		p.interaction.magazine_pose_count=static_cast<std::uint8_t>(p.magazine_grasps.empty()?1:p.magazine_grasps.size());
		return p;
	}
}
