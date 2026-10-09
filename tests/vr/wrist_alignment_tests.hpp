#pragma once
#include "component/vr/gameplay/hands/position_offset.hpp"

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
	const auto close=[](vec a,vec b){return length(sub(a,b))<.003f;};
	for (const position_offsets setting:{position_offsets{},position_offsets{-.02f,.12f,-.1f},position_offsets{.08f,-.06f,.02f}})
	for (const float units:{40.f,100.f})
	for (const vec view:{vec{},vec{1000,-2500,400}})
	for (const quat movement:{quat{0,0,0,1},q({0,1,0},90),q({1,0,0},90),q({0,0,1},-100)})
	{
		std::array<anchor,2> targets{};
		for (int h=0;h<2;++h)
		{
			// Independent physical fixture: a correctly calibrated controller origin
			// moves around a stationary wrist. Aim has a different pose and origin.
			const vec lever{-setting.back_meters,h?setting.inward_meters:-setting.inward_meters,setting.up_meters};
			const auto physical=add(view,wrists[h].position);
			const vr::head_pose_bridge::world_pose grip{
				sub(physical,rotate(movement,scale(lever,units))),axis(movement)};
			const auto aim_rotation=multiply(movement,q({0,1,0},37.4f));
			const vr::head_pose_bridge::world_pose aim{{99,88,77},axis(aim_rotation)};
			check(make_wrist_target(grip,aim,view,units,h,setting,targets[h]) &&
				close(targets[h].position,wrists[h].position),"calibrated rigid lever recovers a stationary wrist through physical rotation");
			const vr::head_pose_bridge::world_pose corrected_aim{{-12,25,-36},axis(multiply(aim_rotation,q({0,1,0},-20)))};
			anchor corrected;
			check(make_wrist_target(grip,corrected_aim,view,units,h,setting,corrected) &&
				close(corrected.position,targets[h].position),"angle calibration cannot translate the wrist or change its pivot");
		}
		for (int rear=0;rear<2;++rear)
		{
			std::array<bone,256> solved{};std::array<bool,2> limited{};
			check(solve(rig,native,targets,shoulders,axis({0,0,0,1}),rear,solved,limited),"rigid wrist calibration reaches either native arm and gun owner");
			for (int h=0;h<2;++h)
				check(close(solved[rig.arms[h].wrist].position,targets[h].position),"skinned wrist lands at the calibrated point");
		}
	}
	// A complete physical turn must rotate the wrist together with the actual
	// controller. A fixed alignment vector in the recenter frame fails this test.
	for (int h=0;h<2;++h)
	for (const position_offsets setting:{position_offsets{},position_offsets{-.02f,.12f,-.1f},position_offsets{.08f,-.06f,.02f}})
	{
		const auto grip_rotation=q({0,1,0},-37.4f);
		const vr::head_pose_bridge::world_pose grip{{20,5,-10},axis(grip_rotation)},aim{{},axis({0,0,0,1})};
		anchor initial;
		check(make_wrist_target(grip,aim,{},40,h,setting,initial),"physical turn fixture establishes a calibrated wrist");
		for (float degrees:{30.f,90.f,180.f})
		{
			const auto turn=q({0,0,1},degrees);const vec shift{100,-200,300};
			const vr::head_pose_bridge::world_pose moved_grip{add(shift,rotate(turn,grip.position)),axis(multiply(turn,grip_rotation))},
				moved_aim{{},axis(turn)};
			anchor moved;
			check(make_wrist_target(moved_grip,moved_aim,shift,40,h,setting,moved) &&
				close(moved.position,rotate(turn,initial.position)),"whole-body physical yaw and scene translation preserve rigid wrist alignment");
		}
	}
}
