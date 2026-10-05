#pragma once
#include "../../part_grip_pose.hpp"
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::m9
{
	// Borrow only the closing hand from h2_wpn_smg_mp5k_reload_empty frame 9.
	// SHA-256 3c4c3e1bd72f0932df4fb98c2f90b8f2e461615c9465ab35df456202aa37d06c.
	// Reorient the palm across the M9 slide: pinky toward muzzle (+X), web
	// toward rear, fingers across -Y. Palm knuckles sit 1 cm above the slide
	// mesh top; native grip lengths and all 15 source finger rotations survive.
	// No MP5 action timing, model placement, travel or ammunition is imported.
	inline constexpr hands::anchor overhand_slide_wrist{
		{0.f, 3.56123727f, 4.15354331f}, {-0.61182110f, 0.76853296f, -0.12782648f, 0.13672026f}};
	inline constexpr hands::vec overhand_slide_contact = {3.51398758f, -0.50461838f, 0.80466762f};
	inline constexpr joint_pose overhand_slide_fingers[]{
		{"j_index_le_0", {0.70321080f, -0.07470966f, 0.01068153f, 0.70696460f}},
		{"j_mid_le_0", {0.54201062f, -0.42744374f, 0.32120843f, 0.64833748f}},
		{"j_thumb_le_0", {-0.19855493f, -0.10925709f, 0.24085390f, 0.94373101f}},
		{"j_index_le_1", {0.01931806f, -0.01907392f, 0.63911357f, 0.76863310f}},
		{"j_mid_le_1", {-0.01962357f, -0.00857577f, 0.67715047f, 0.73553288f}},
		{"j_pinky_le_0", {-0.09735393f, 0.18558284f, 0.43470514f, 0.87584968f}},
		{"j_ring_le_0", {-0.11563460f, 0.07589951f, 0.54396178f, 0.82763125f}},
		{"j_thumb_le_1", {0.06006060f, 0.06256313f, -0.39307956f, 0.91540539f}},
		{"j_index_le_2", {0.01525918f, 0.03302087f, 0.65440529f, 0.75526849f}},
		{"j_mid_le_2", {-0.02826014f, 0.00824000f, 0.68346198f, 0.72939234f}},
		{"j_pinky_le_1", {0.02652075f, 0.00912509f, 0.70736249f, 0.70629434f}},
		{"j_ring_le_1", {0.01214627f, 0.00683609f, 0.68421977f, 0.72914268f}},
		{"j_thumb_le_2", {0.03881962f, 0.00534075f, -0.00683616f, 0.99920858f}},
		{"j_pinky_le_2", {-0.02313309f, 0.02444539f, 0.81173339f, 0.58305762f}},
		{"j_ring_le_2", {0.00445568f, 0.03799538f, 0.80345743f, 0.59413185f}}
	};
	inline const std::array<part_grip_pose, 2> slide_grips{{
		{"thumb", slide_wrist_rest, slide_contact_in_wrist, slide_fingers},
		{"overhand_pinky_forward", overhand_slide_wrist, overhand_slide_contact, overhand_slide_fingers}
	}};
}

