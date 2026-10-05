#pragma once
#include "reload_poses.hpp"
namespace vr::gameplay::weapons::p90
{
// Style 0: original reverse grasp. Style 1: forward grasp, thumb toward muzzle.
// Preserve palm X/Z; exchange four-finger/thumb sides about the magazine Y centreline.
// Live comfort correction: the forward grasp sits 5 cm farther rearward.
// Rotating about the palm itself would displace the forward grasp to the left.
inline constexpr hands::vec magazine_palm_in_wrist={1.74253108f, -0.39454986f, -0.01066914f};
inline constexpr float magazine_grasp_centre_y=0.00000047f;
inline constexpr std::array<magazine_grasp_pose,2> magazine_grasps{{
	{magazine_in_wrist,contacts.grip_contact,magazine_fingers},
	{{{8.17477003f, 7.05836965f, 1.43721818f}, {-0.86908410f, -0.41064699f, -0.03737325f, -0.27324917f}},contacts.grip_contact,magazine_fingers}}};
}
