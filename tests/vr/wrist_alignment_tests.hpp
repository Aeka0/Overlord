#pragma once
#include "component/vr/gameplay/hand_position_offset.hpp"

template<class Check>
void wrist_alignment_tests(Check check,const vr::gameplay::hands::rig& rig,
	std::span<const vr::gameplay::hands::bone> native,
	const std::array<vr::gameplay::hands::vec,2>& shoulders,
	const std::array<vr::gameplay::hands::anchor,2>& wrists)
{
	using namespace vr::gameplay::hands;
	const auto axis=[](quat q){return std::array<vec,3>{rotate(q,{1,0,0}),rotate(q,{0,1,0}),rotate(q,{0,0,1})};};
	const auto q=[](vec a,float degrees){const float half=degrees*.008726646259971648f;
		return quat{a[0]*std::sin(half),a[1]*std::sin(half),a[2]*std::sin(half),std::cos(half)};};
	const auto local=[](position_offsets p,int h){return vec{-p.back_meters,h ? p.inward_meters : -p.inward_meters,p.up_meters};};
	const auto close=[](vec a,vec b){return length(sub(a,b))<.003f;};
	bool old_orbit_reproduced=false;
	for (const position_offsets pivot:{position_offsets{},position_offsets{.025f,.08f,-.03f}})
	for (const position_offsets setting:{position_offsets{},position_offsets{-.02f,.12f,-.1f},position_offsets{.08f,-.06f,.02f},position_offsets{.5f,-.5f,.5f}})
	for (const float units:{40.f,100.f})
	for (const vec view:{vec{},vec{1000,-2500,400}})
	for (const quat reference:{quat{0,0,0,1},q({0,0,1},83)})
	for (const quat raw_to_aim:{quat{0,0,0,1},q({0,1,0},37.4f)})
	for (const quat correction:{quat{0,0,0,1},q({0,1,0},20),q({0,1,0},-55),normalize({.2f,.4f,-.3f,.7f})})
	for (const quat movement:{quat{0,0,0,1},q({0,1,0},65),q({0,1,0},125),q({1,0,0},90),q({0,0,1},-100)})
	{
		const auto raw_rotation=multiply(multiply(reference,movement),conjugate(raw_to_aim));
		const auto aim_rotation=multiply(reference,movement);
		std::array<anchor,2> targets{};auto moved_shoulders=shoulders;
		for (int h=0;h<2;++h)
		{
			// Independent physical fixture: controller tracking origin describes
			// an arc around the stationary anatomical wrist. It does not stay still.
			const auto physical=add(view,wrists[h].position);
			const vr::head_pose_bridge::world_pose grip{
				sub(physical,rotate(raw_rotation,scale(local(pivot,h),units))),axis(raw_rotation)};
			const vr::head_pose_bridge::world_pose raw_aim{{99,88,77},axis(aim_rotation)},
				aim{{-12,25,-36},axis(multiply(aim_rotation,correction))};
			const auto shift=rotate(multiply(reference,conjugate(raw_to_aim)),scale(sub(local(setting,h),local(pivot,h)),units));
			const auto expected=add(wrists[h].position,shift);
			check(make_wrist_target(grip,aim,grip,raw_aim,axis(reference),view,units,h,setting,pivot,targets[h]) &&
				close(targets[h].position,expected),"alignment moves the wrist center without an orbit under physical rotation or angle calibration");
			check(close(rotate(targets[h].rotation,{1,0,0}),aim.axis[0]),"calibrated aim rotates about the translated wrist center");
			moved_shoulders[h]=add(shoulders[h],shift);
			vec old_target;
			if (offset_wrist(sub(grip.position,view),grip.axis,units,h,setting,old_target) && !close(old_target,expected)) old_orbit_reproduced=true;
		}
		for (int rear=0;rear<2;++rear)
		{
			std::array<bone,256> solved{};std::array<bool,2> limited{};
			check(solve(rig,native,targets,moved_shoulders,axis(reference),rear,solved,limited),"translated wrist frame reaches the native arm and gun solver");
			for (int h=0;h<2;++h)
			{
				const auto w=rig.arms[h].wrist;
				check(close(solved[w].position,targets[h].position),"skinned wrist lands at the translated rotation center");
				check(std::abs(length(sub(solved[w+1].position,solved[w].position))-length(sub(native[w+1].position,native[w].position)))<.003f,
					"fingers rotate around the translated wrist with their local radius preserved");
			}
			const auto local_gun=sub(native[rig.gun].position,native[rig.rear_grip_wrist].position);
			check(close(solved[rig.gun].position,add(targets[rear].position,rotate(targets[rear].rotation,local_gun))),
				"both rear-hand owners rotate the weapon around the same translated wrist");
		}
	}
	check(old_orbit_reproduced,"the former full-offset lever reproduces the reported displaced rotation center");
	// Independent trajectory, based on the zero-Up rigid-local trial recorded
	// before physical pivot and cosmetic alignment were split. This is a known
	// synthetic fixture, not a measurement of the current user's anatomy.
	const position_offsets truth{-.02f,.12f,0},old_pivot{-.02f,.12f,-.05f},alignment{-.02f,.12f,-.1f};
	const position_offsets trial{vr::settings::wrist_inward.default_value,
		vr::settings::wrist_back.default_value,vr::settings::wrist_up.default_value};
	for(int h=0;h<2;++h)
	for(const quat relation:{quat{0,0,0,1},q({0,1,0},37.4f)})
	{
		anchor first{},old_first{};float old_excursion{};
		for(const float degrees:{0.f,30.f,60.f,90.f,125.f})
		{
			const auto aim_rotation=q({0,1,0},degrees),raw_rotation=multiply(aim_rotation,conjugate(relation));
			const vr::head_pose_bridge::world_pose grip{
				sub(wrists[h].position,rotate(raw_rotation,scale(local(truth,h),100))),axis(raw_rotation)};
			const vr::head_pose_bridge::world_pose raw_aim{{},axis(aim_rotation)},
				aim{{},axis(multiply(aim_rotation,q({0,1,0},-20)))};
			anchor result{},old{};
			check(make_wrist_target(grip,aim,grip,raw_aim,axis({0,0,0,1}),{},100,h,alignment,trial,result) &&
				make_wrist_target(grip,aim,grip,raw_aim,axis({0,0,0,1}),{},100,h,alignment,old_pivot,old),
				"independent zero-Up trajectory accepts current alignment and either physical pivot");
			if(degrees==0){first=result;old_first=old;}
			check(close(result.position,first.position),"zero-Up physical trial removes the inherited vertical pivot orbit");
			old_excursion=std::max(old_excursion,length(sub(old.position,old_first.position)));
		}
		check(close(first.position,old_first.position),"physical trial preserves placement at neutral runtime aim");
		check(old_excursion>7.f,"the old minus-five-centimeter pivot reproduces an orbit with independent ground truth");
	}
}
