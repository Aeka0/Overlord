#pragma once
#include "physical_reload_profile.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "controller_palm.hpp"

namespace vr::gameplay::weapons
{
	struct magazine_grip_selection
	{
		hands::anchor in_wrist;hands::vec contact;std::uint8_t index{};
		std::span<const joint_pose> fingers;
	};
	inline magazine_grasp_kind magazine_palm_intent(std::optional<hands::quat> controller,int hand) noexcept
	{
		using namespace hands;
		if(!controller || hand<0 || hand>1)return magazine_grasp_kind::native;
		float norm{};for(float x:*controller){if(!std::isfinite(x))return magazine_grasp_kind::native;norm+=x*x;}
		if(norm<.5f || norm>1.5f)return magazine_grasp_kind::native;
		const auto rotation=normalize(*controller);
		const auto forward=rotate(rotation,{1,0,0});
		const auto palm=rotate(rotation,controller_palm_axis(hand));
		const auto projected=sub(vec{0,0,1},scale(forward,forward[2]));
		const auto length_up=length(projected);
		// Signed roll about the tracked hand's forward axis: palm-up=0,
		// inward=90, down=180, outward=270, with physical left/right parity.
		// Near vertical pointing there is no reliable up reference; prefer wrap.
		if(length_up<.35f)return magazine_grasp_kind::body_wrap;
		const auto up=scale(projected,1/length_up);
		float degrees=std::atan2(dot(cross(up,palm),forward)*(hand?-1.f:1.f),dot(up,palm))*57.29577951f;
		if(degrees<0)degrees+=360.f;
		// Returning close to palm-up is wrap, not a 0/360 branch-cut pinch.
		// This is pose intent, not a measured anatomical/accumulated joint turn.
		return degrees>190.0001f && degrees<350.f ? magazine_grasp_kind::bottom_pinch : magazine_grasp_kind::body_wrap;
	}
	inline hands::quat magazine_wrist_basis(const reload_profile& p,const magazine_grip_selection& pose,
		hands::quat ordinary,bool knife) noexcept
	{
		// The complete hand follows its authored attachment while magazine +Z
		// follows calibrated controller +Z (including pitch/roll), never world-up.
		return p.magazine_tracking==magazine_tracking_frame::controller && !knife ?
			hands::conjugate(hands::normalize(pose.in_wrist.rotation)) : ordinary;
	}
	inline magazine_grip_selection select_magazine_grip(const reload_profile& p,hands::quat raw,
		int hand,hands::quat mirror,bool knife,bool held,std::uint8_t selected,
		std::optional<hands::quat> controller_body=std::nullopt,bool attached=false)noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		const auto source=knife && p.knife_magazine_in_wrist?*p.knife_magazine_in_wrist:p.magazine_in_wrist;
		auto grasp=hand?hands::pose_mirror::object_in_wrist(p.magazine_rest,source,mirror):source;
		auto point=p.magazine_contacts?p.magazine_contacts->grip_contact:vec{};
		if(hand)point=hands::pose_mirror::local_point(point,mirror);
		if(knife || p.magazine_grasps.empty() || p.magazine_grasps.size()>max_part_grips)return {grasp,point,0,p.magazine_fingers};
		const auto fallback=p.magazine_default_pose<p.magazine_grasps.size()?p.magazine_default_pose:std::uint8_t{};
		std::uint8_t index=held && selected<p.magazine_grasps.size()?selected:fallback;
		const bool directional=p.magazine_selection==magazine_grasp_policy::body_palm;
		if(!held && directional)
		{
			const auto intent=magazine_palm_intent(controller_body,hand);
			if(intent!=magazine_grasp_kind::native)for(size_t i=0;i<p.magazine_grasps.size();++i)
				if(p.magazine_grasps[i].kind==intent){index=static_cast<std::uint8_t>(i);break;}
		}
		float best=-1;
		for(size_t i=0;!held && p.magazine_selection==magazine_grasp_policy::wrist_facing && i<p.magazine_grasps.size();++i)
		{
			const auto& source_pose=p.magazine_grasps[i].in_wrist;
			const auto g=hand?hands::pose_mirror::object_in_wrist(p.magazine_rest,source_pose,mirror):source_pose;
			const auto rotation=compose(p.magazine_rest,inverse(g)).rotation;
			float cosine{};for(size_t j=0;j<4;++j)cosine+=raw[j]*rotation[j];
			if(std::abs(cosine)>best+1e-6f){best=std::abs(cosine);index=static_cast<std::uint8_t>(i);}
		}
		// A rifle magazine constrained by its receiver always uses the fitted
		// wrap. Free magazines keep their palm-selected grasp until insertion.
		if(attached)for(size_t i=0;i<p.magazine_grasps.size();++i)
			if(p.magazine_grasps[i].kind==magazine_grasp_kind::body_wrap){index=static_cast<std::uint8_t>(i);break;}
		const auto& pose=p.magazine_grasps[index];
		const auto chosen=hand?hands::pose_mirror::object_in_wrist(p.magazine_rest,pose.in_wrist,mirror):pose.in_wrist;
		point=hand?hands::pose_mirror::local_point(pose.contact_in_wrist,mirror):pose.contact_in_wrist;
		return {chosen,point,index,pose.fingers};
	}
	inline void sample_attached_magazine(const reload_profile& p,physical_reload::geometry& g,
		hands::anchor gun,hands::anchor wrist,hands::quat controller,hands::quat basis,
		const magazine_grip_selection& grasp,hands::anchor free_magazine,float units,bool knife)noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		g.attached_magazine_pose=grasp.index;
		const anchor contact_wrist{wrist.position,normalize(multiply(controller,magazine_wrist_basis(p,grasp,basis,knife)))};
		g.magazine=magazine_contacts(p,gun,contact_wrist,free_magazine,units,&grasp.contact);
		// Button-release profiles without a separate latch box use the authored
		// seated wrist as their spherical contact. No render-snapped hand input.
		if(!g.magazine.valid && p.ammunition.release==mechanics::magazine_release::button && std::isfinite(units) && units>0)
		{
			g.magazine.valid=true;
			g.magazine.grip_distance=length(sub(wrist.position,compose(compose(gun,p.magazine_rest),inverse(grasp.in_wrist)).position))/units;
		}
	}
}
