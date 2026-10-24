#pragma once
#include "melee_weapon_policy.hpp"
#include "knife_profile.hpp"
#include "melee_poses.generated.hpp"

namespace vr::gameplay::equipment::melee_weapons
{
	inline constexpr std::array<const char*,11> baton_bones{
		"j_gun","tag_fx","tag_ring","tag_rod_1","tag_switch","tag_loop_1",
		"tag_rod_2","tag_loop_2","tag_rod_3","tag_loop_3","tag_loop_4"};
	inline constexpr std::array<unsigned char,10> baton_parents{1,2,3,4,3,3,2,2,2,1};
	struct profile
	{
		const char* model;
		const char* root;
		unsigned bones;
		anchor left_attachment;
		vec grip_center,blade_base,blade_tip;
		knife_grip source_grip;
		quat slot_rotation{0,0,0,1};
		std::span<const hands::joint_pose> fingers;
	};
	inline const std::array<profile,unsigned(kind::count)> profiles{{
		{knife_profile::model_name,"tag_knife",2,knife_profile::left_reverse_attachment,
			knife_profile::grip_center,knife_profile::blade_base,knife_profile::blade_tip,knife_grip::reverse,
			{0,0,0,1},knife_profile::fingers},
		{"wpn_h1_melee_baton_vm","j_gun",11,melee_poses::baton_attachment,melee_poses::baton_grip_center,
			{0,0,melee_poses::baton_grip_center[2]+1.18110236f},{0,0,18.7f},knife_grip::forward,
			{0,.707106781f,0,.707106781f},melee_poses::baton_fingers},
		{"wpn_h1_melee_karambit_vm","j_gun",2,melee_poses::karambit_attachment,melee_poses::karambit_grip_center,
			{0,0,melee_poses::karambit_grip_center[2]-1.18110236f},{.555757f,-.007459f,-10.258812f},knife_grip::reverse,
			{0,-.707106781f,0,.707106781f},melee_poses::karambit_fingers},
		{"viewmodel_commando_knife","tag_knife",2,melee_poses::ending_attachment,melee_poses::ending_grip_center,
			{.787401575f,0,0},{7.915123617f,-.001431f,-1.408213987f},knife_grip::forward,
			{0,0,0,1},melee_poses::ending_fingers},
		{"viewmodel_commando_knife_bloody","tag_knife",2,melee_poses::ending_attachment,melee_poses::ending_grip_center,
			{.787401575f,0,0},{7.915123617f,-.001431f,-1.408213987f},knife_grip::forward,
			{0,0,0,1},melee_poses::ending_fingers},
		{knife_profile::bayonet_model_name,"j_gun",3,{},knife_profile::bayonet_grip_center,
			knife_profile::bayonet_blade_base,knife_profile::bayonet_blade_tip,knife_grip::reverse,
			{0,.707106781f,0,.707106781f},knife_profile::fingers}
	}};
	inline const profile& get(kind value) noexcept
	{return profiles[unsigned(value)<profiles.size()?unsigned(value):0];}
	inline anchor attachment(kind value,knife_grip requested,quat mirror,bool right,knife_hand_pose pose=knife_hand_pose::grip) noexcept
	{
		using namespace hands::pose_math;
		const auto selected=grip(value,requested);
		if(value==kind::ghost || value==kind::bayonet)
			return knife_profile::attachment(selected,mirror,right,pose,value==kind::bayonet);
		const auto& p=get(value);auto out=p.left_attachment;
		if(pose!=knife_hand_pose::grip)
		{
			// Keep the existing magazine/slide hand authority and retarget only
			// its object attachment through this weapon's native handle frame.
			const auto common=knife_profile::attachment(p.source_grip,{0,0,0,1},false);
			out=compose(knife_profile::attachment(selected,{0,0,0,1},false,pose),compose(inverse(common),out));
		}
		else if(selected!=p.source_grip)
		{
			const auto centre=compose(out,{p.grip_center,{0,0,0,1}}).position;
			out.rotation=normalize(multiply(out.rotation,{0,0,1,0}));
			out.position=sub(centre,rotate(out.rotation,p.grip_center));
		}
		return right?hands::pose_mirror::object_in_wrist({},out,mirror):out;
	}
	inline anchor stowed(const chest_slots& chest,kind value) noexcept
	{
		if(value==kind::ghost || value==kind::bayonet)return knife_profile::stowed(chest,value==kind::bayonet);
		const auto& p=get(value);auto out=hands::pose_math::compose(chest.anchors[0],{{},p.slot_rotation});
		out.position=sub(out.position,rotate(out.rotation,p.grip_center));return out;
	}
	inline std::array<anchor,11> baton_pose(float seconds,bool held) noexcept
	{
		const float frame=held && std::isfinite(seconds)?std::clamp(seconds*melee_poses::baton_fps,0.f,23.f):0.f;
		const auto first=unsigned(frame),last=std::min(first+1,23u);const float t=frame-float(first);
		auto out=melee_poses::baton_deploy[first];
		for(unsigned i=0;i<out.size();++i)
		{
			out[i].position=add(out[i].position,scale(sub(melee_poses::baton_deploy[last][i].position,out[i].position),t));
			out[i].rotation=hands::blend_quat(out[i].rotation,melee_poses::baton_deploy[last][i].rotation,t);
		}
		return out;
	}
	inline vec baton_tip(float seconds) noexcept
	{return hands::pose_math::compose(baton_pose(seconds,true)[8],{melee_poses::baton_tip_in_rod,{0,0,0,1}}).position;}
}
