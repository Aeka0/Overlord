#pragma once
#include "reload_poses.hpp"
#include "../hand_poses/left_handle.hpp"
namespace vr::gameplay::weapons::fn2000
{
// Authored lateral articulation: native clips only translate j_reload.
// Only the 1126-face end rotates; the separate carrier/pin remain axial.
// Source SHA-256 35af0bd35ce62c5f23747d7e3da55b251c7840371f34beed8b6777978ce0d999.
inline constexpr std::array<std::array<unsigned,2>,9> handle_surfaces{{{5053,8080},{1865,2296},{2254,2656},{499,512},{2696,3952},{1714,2874},{9734,14682},{1804,2758},{8079,11702}}};
inline constexpr std::array<scene_models::surface_face_range,3> handle_tip_faces{{{0,3600,4175},{0,4228,4451},{0,6126,6451}}};
inline constexpr part_mesh_partition handle_mesh{"h2_viewmodel_fn2000_base",21,handle_surfaces,handle_tip_faces,{3.82342076f, 0.70165400f, 3.55971517f},{5.94050603f, 1.60688997f, 4.34404621f}};
inline constexpr charging_handle_fold handle_fold{{0,0,.70710678f,.70710678f},{},{{-2.52702086f, 1.02377945f, 0.20093317f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}},std::nullopt,&handle_mesh};
inline const auto folding_grips=[] {
	// Fits are authored on the deployed end. Acquisition still samples its
	// folded material point: changing a wrist must not shift the capture box.
	const hands::vec folded_contact{5.92901087f,1.37421296f,4.03117157f};
	const auto pivot=compose_reload(action_rest,handle_fold.pivot);
	const auto deployed=folded_handle_pose(action_rest,handle_fold,1,0);
	const auto contact=carry_with_handle(pivot,deployed,{folded_contact,{0,0,0,1}}).position;
	// Refit both native pulling fingers, not just the isolated index tip.
	const hands::anchor wrist{{-3.50991328f,4.40344076f,4.21982254f},action_wrist.rotation};
	const part_grip_pose down{"index_middle_down",wrist,
		hands::rotate(hands::conjugate(wrist.rotation),hands::sub(contact,wrist.position)),action_fingers};
	auto out=hand_poses::left_handle::with_native(down);
	for(size_t i=0;i<out.size();++i)
	{
		auto& pose=out[i];pose.palm=i?part_palm_facing::up:part_palm_facing::down;
		pose.contact_in_wrist=hands::rotate(hands::conjugate(pose.wrist.rotation),hands::sub(folded_contact,pose.wrist.position));
		// Rebase the canonical opposite wrist so anatomical mirroring about
		// the folded contact preserves the deployed right-hand fit. Both right
		// hooks approach from the rear; never yaw the glove toward the muzzle.
		auto& right=*pose.opposite_pose;
		right.wrist.position[1]+=2*(folded_contact[1]-contact[1]);
		right.contact_in_wrist=hands::rotate(hands::conjugate(right.wrist.rotation),hands::sub(folded_contact,right.wrist.position));
	}
	return out;
}();
}
