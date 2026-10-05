#pragma once
#include "body_equipment.hpp"
#include "knife_profile.hpp"
#include <cstring>

namespace vr::gameplay::equipment::special
{
	struct action_slot {unsigned index{},weapon{};explicit operator bool()const noexcept{return index<4 && weapon>0 && weapon<512;}};
	inline std::array<action_slot,4> weapon_slots(std::span<const std::byte> ps)noexcept
	{
		std::array<action_slot,4> result{};if(ps.size()<0x1fc0)return result;
		for(unsigned i=0;i<4;++i){unsigned type{},weapon{};std::memcpy(&type,ps.data()+0x1fa0+i*4,4);std::memcpy(&weapon,ps.data()+0x1fb0+i*4,4);
			if(type==1 && weapon>0 && weapon<512)result[i]={i,weapon};}
		return result;
	}
	inline anchor abdomen(const head_pose_bridge::spatial_frame& body)noexcept
	{
		const auto chest=locate_chest(body);if(!chest.valid)return {};
		const auto frame=head_pose_bridge::body_slots_frame(body);
		auto result=chest.anchors[unsigned(slot::tactical)];
		result.position=sub(result.position,scale(frame.head_yaw_axis[2],frame.units_per_meter*.24f));
		result.rotation=normalize(multiply(result.rotation,{0,-.70710678f,0,.70710678f}));return result;
	}
	inline anchor abdominal_display(const head_pose_bridge::spatial_frame& body,bool designator)noexcept
	{
		auto result=abdomen(body);if(!designator)return result;
		// Gun +Y (left side) faces the torso. Muzzle points to the player's
		// left and 25 degrees down; this orientation belongs only to this device.
		const auto frame=head_pose_bridge::body_slots_frame(body);
		const auto& axes=frame.head_yaw_axis;constexpr float c=.906307787f,s=.422618262f;
		const auto forward=add(scale(axes[1],c),scale(axes[2],-s));const auto left=scale(axes[0],-1);
		result.rotation=from_axis({forward,left,cross(forward,left)});return result;
	}
	inline float abdominal_grab_distance_at(const head_pose_bridge::spatial_frame& body,vec center,anchor wrist,quat basis,quat mirror,unsigned hand)noexcept
	{
		const auto chest=locate_chest(body);if(!chest.valid || hand>1)return INFINITY;
		const auto frame=head_pose_bridge::body_slots_frame(body);
		// One shared rounded volume for all abdominal items: 20 cm across,
		// 14 cm high, plus the same 10 cm contact tolerance as the chest rig.
		const body_reach_volume reach{center,scale(vec{-.03f,-.10f,-.07f},frame.units_per_meter),scale(vec{.03f,.10f,.07f},frame.units_per_meter),frame.head_yaw_axis,chest.radius};
		const auto palm=knife_profile::palm_contact(wrist,basis,mirror,hand==1);
		return std::min(reach.distance(palm),reach.distance(wrist.position))/reach.radius;
	}
	inline float abdominal_grab_distance(const head_pose_bridge::spatial_frame& body,anchor wrist,quat basis,quat mirror,unsigned hand)noexcept
	{return abdominal_grab_distance_at(body,abdomen(body).position,wrist,basis,mirror,hand);}
	inline float ray_box(vec origin,vec direction,vec center,vec half,float reach)noexcept
	{
		float entry_distance=0,exit_distance=reach;if(!std::isfinite(reach) || reach<=0)return INFINITY;
		for(unsigned axis=0;axis<3;++axis)
		{
			for(float x:{origin[axis],direction[axis],center[axis],half[axis]})if(!std::isfinite(x))return INFINITY;
			if(half[axis]<=0)return INFINITY;
			const float relative=origin[axis]-center[axis];
			if(std::abs(direction[axis])<1e-6f){if(std::abs(relative)>half[axis])return INFINITY;continue;}
			float a=(-half[axis]-relative)/direction[axis],b=(half[axis]-relative)/direction[axis];if(a>b)std::swap(a,b);
			entry_distance=std::max(entry_distance,a);exit_distance=std::min(exit_distance,b);if(entry_distance>exit_distance)return INFINITY;
		}
		return entry_distance;
	}
	struct grasp_state
	{
		vr::hand primary{vr::hand::none},support{vr::hand::none};
		std::uint64_t revision{};bool preview{},trigger_armed{};
		bool held()const noexcept{return vr::valid_hand(primary);}
		bool take(vr::hand hand)noexcept{if(held() || !vr::valid_hand(hand))return false;primary=hand;support=vr::hand::none;preview=trigger_armed=false;++revision;return true;}
		void stow()noexcept{primary=support=vr::hand::none;preview=trigger_armed=false;++revision;}
		bool join(vr::hand hand)noexcept{if(!held() || hand==primary || !vr::valid_hand(hand) || vr::valid_hand(support))return false;support=hand;++revision;return true;}
		void release(unsigned hand_mask)noexcept
		{
			if(!held())return;
			if(vr::valid_hand(support) && (hand_mask&(1u<<unsigned(support)))){support=vr::hand::none;++revision;}
			if(hand_mask&(1u<<unsigned(primary))){if(vr::valid_hand(support)){primary=support;support=vr::hand::none;preview=trigger_armed=false;++revision;}else stow();}
		}
		bool trigger(bool active,bool down,bool press,bool release,bool valid_target)noexcept
		{
			if(!held() || !active){preview=trigger_armed=false;return false;}
			if(!down && !release)trigger_armed=true;
			if(trigger_armed && press && down)preview=true;
			if(!release)return false;
			const bool commit=preview && valid_target;preview=false;trigger_armed=true;return commit;
		}
	};
}
