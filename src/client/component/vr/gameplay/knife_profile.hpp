#pragma once
#include "native_hand_schema.hpp"
#include "body_equipment.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "knife_magazine_pose.hpp"
#include "knife_slide_pose.hpp"
#include "knife_hand_pose.hpp"
namespace vr::gameplay::equipment::knife_profile
{
	// Common H2 melee knife; left-hand source: h2_wpn_knife_melee_slice, frame 8.
	// Exported centimetres converted to native model inches; assets remain engine-owned.
	inline constexpr const char* model_name="h2_viewmodel_knife";
	inline constexpr const char* bayonet_model_name="wpn_h1_melee_rifle_bayonet_vm";
	// Project the middle/ring knuckle midpoint onto the handle axis. The
	// former -5 cm pivot was near the thumb, translating a flipped handle.
	inline constexpr hands::vec grip_center{-0.667453071f,0.000000000f,0.000000000f};
	inline constexpr hands::vec blade_base{.787401575f,0,0},blade_tip{11.9f,0,-1.8f};
	// h1_wpn_melee_bayonet_knife_idle frame 0: middle/ring knuckle
	// midpoint projected onto the bayonet's +Z handle (native model inches).
	inline constexpr hands::vec bayonet_grip_center{0,0,.782077045f};
	inline constexpr hands::vec bayonet_blade_base{0,0,3.9f},bayonet_blade_tip{0,0,11.3f};
	inline hands::anchor bayonet_in_common() noexcept
	{
		const hands::quat rotation{0,.707106781f,0,.707106781f};
		return {hands::sub(grip_center,hands::rotate(rotation,bayonet_grip_center)),rotation};
	}
	inline constexpr hands::anchor left_reverse_attachment{{2.620412108f,-1.055967714f,1.459448043f},{-0.438371879f,0.632316858f,0.467302269f,-0.435469949f}};
	inline constexpr auto& fingers=hands::native_hand_schema::reference_fingers;
	inline hands::anchor attachment(knife_grip mode,hands::quat mirror_basis,bool right,knife_hand_pose pose=knife_hand_pose::grip,bool bayonet=false) noexcept
	{
		using namespace hands::pose_math;
		// The authored common knife points toward the pinky (reverse grip).
		auto a=pose==knife_hand_pose::slide ? knife_slide_pose::reverse_attachment :
			pose==knife_hand_pose::magazine ? knife_magazine_pose::reverse_attachment : left_reverse_attachment;
		// Flip the handle axis (+X) about local Z so the cutting edge keeps its
		// facing. A Y half-turn also reverses Z, leaving forward grip edge-backward.
		if (mode==knife_grip::forward)
		{
			const auto center=compose(a,{grip_center,{0,0,0,1}}).position;
			a.rotation=normalize(multiply(a.rotation,{0,0,1,0}));
			a.position=sub(center,rotate(a.rotation,grip_center));
		}
		if(right)a=hands::pose_mirror::object_in_wrist({},a,mirror_basis);
		// Transfer the object into the established hand/contact frame AFTER
		// anatomical mirroring. Both co-grasps retain their exact finger poses.
		return bayonet?compose(a,bayonet_in_common()):a;
	}
	inline hands::vec palm_contact(hands::anchor wrist,hands::quat anatomical,hands::quat mirror,bool right) noexcept
	{
		using namespace hands::pose_math;
		wrist.rotation=normalize(multiply(wrist.rotation,anatomical));
		return compose(compose(wrist,attachment(knife_grip::forward,mirror,right)),{grip_center,{0,0,0,1}}).position;
	}
	inline hands::anchor stowed(const chest_slots& chest,bool bayonet=false) noexcept
	{
		auto object=chest.anchors[0];object.position=sub(object.position,rotate(object.rotation,grip_center));
		return bayonet?hands::pose_math::compose(object,bayonet_in_common()):object;
	}
}
