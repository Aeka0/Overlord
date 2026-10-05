#pragma once
#include "../../weapon_profile.hpp"

namespace vr::gameplay::weapons::m9
{
	// Compact runtime calibration only. Official exports: reload frame 23 and
	// pullout_first frame 13; cm / 2.54. No mesh/parser/export dependency.
	// SHA256 reload: 57c3f330eff394739d15dc6496293e9ba41afc97d83b83f30da8fb423b16a002
	// SHA256 pullout: 2ee0713a75fcdfe5594f57b22f1dc5017077b051a1df6449ca0ac9ab1c41c7ed
	inline constexpr hands::anchor magazine_in_wrist{{4.35199777f, 0.23420529f, 2.07577334f}, {0.01870021f, 0.27279578f, -0.14778528f, 0.95046950f}};
	// Native USP knife co-grasp, retaining this magazine's ordinary-grasp registration.
	inline constexpr hands::anchor knife_magazine_in_wrist{{4.568887960f,0.599369456f,3.169380898f},{-0.046803230f,-0.104041452f,-0.111155245f,0.987233177f}};
	inline constexpr hands::vec slide_grab_wrist = {-2.84231467f, 0.52436682f, 3.39119569f};
	inline constexpr hands::anchor slide_wrist_rest{slide_grab_wrist,{.99370920f,.05250228f,.07731445f,-.06170910f}};
	// Same pullout frame: convert the rear-slide contact into wrist-local space.
	// The wrist itself sits ~7 cm behind the slide; it is NOT the contact point.
	inline const hands::vec slide_contact_in_wrist = hands::rotate(
		hands::conjugate(slide_wrist_rest.rotation),
		hands::sub(hands::vec{0,0,3.4f},slide_grab_wrist));
	// Rear gripping region of the j_bolt mesh (native units). Export bounds in cm:
	// [-2.83023,-2.11469,4.63068] .. [18.55890,2.09658,9.55312].
	// Exclude forward barrel and lower surfaces; contact tolerance is separate.
	inline constexpr hands::vec slide_grab_low{-1.12f,-.833f,2.7f}, slide_grab_high{1.f,.826f,3.762f};
	// VM tag_clip bind origin + rigid-clip vertex offset, inverted.
	// The standalone h2_weapon_beretta_clip has the same 450 magazine vertices.
	inline constexpr hands::vec rigid_clip_origin{-0.17079505f, 0, -0.41608491f};
	inline constexpr hands::vec magazine_top{0.32566827f, -0.01202360f, 2.29590134f};
	// Mouth at the grip base, not the seated top. Last 12 cm seat visually.
	inline constexpr hands::vec magazine_well{0.61504f, -0.01925f, -1.96850f};
	inline constexpr const char* magazine_model = "h2_weapon_beretta_clip";
	inline constexpr joint_pose magazine_fingers[]{
		{"j_index_le_0", {0.63239092f, -0.40386157f, 0.18867897f, 0.63354384f}},
		{"j_mid_le_0", {0.57163643f, -0.43391853f, 0.22403651f, 0.65935889f}},
		{"j_thumb_le_0", {0.10354549f, -0.31657017f, 0.09719325f, 0.93787799f}},
		{"j_index_le_1", {0.02702703f, -0.00252692f, 0.00825826f, 0.99959740f}},
		{"j_mid_le_1", {-0.01658980f, -0.01355627f, 0.45606085f, 0.88969068f}},
		{"j_pinky_le_0", {-0.37517044f, 0.23465617f, 0.46997579f, 0.76374497f}},
		{"j_ring_le_0", {-0.24298039f, -0.13748829f, 0.44489433f, 0.85095625f}},
		{"j_thumb_le_1", {0.10684562f, 0.08224327f, -0.20943607f, 0.96848159f}},
		{"j_index_le_2", {-0.00927148f, 0.03515714f, 0.02530582f, 0.99901833f}},
		{"j_mid_le_2", {-0.02936509f, -0.00112309f, 0.41725162f, 0.90831576f}},
		{"j_pinky_le_1", {0.02651455f, 0.00897857f, 0.71200485f, 0.70161632f}},
		{"j_ring_le_1", {0.01226843f, 0.00663472f, 0.69290369f, 0.72089524f}},
		{"j_thumb_le_2", {-0.02442777f, -0.06781180f, -0.10554558f, 0.99179886f}},
		{"j_pinky_le_2", {-0.03271624f, 0.00802036f, 0.36509132f, 0.93036211f}},
		{"j_ring_le_2", {-0.00131840f, -0.00286874f, 0.48435268f, 0.87486714f}},
	};
	inline constexpr joint_pose slide_fingers[]{
		{"j_index_le_0", {0.56339732f, -0.29437899f, 0.06472920f, 0.76924288f}},
		{"j_mid_le_0", {0.53905177f, -0.27851364f, 0.14566575f, 0.78143127f}},
		{"j_thumb_le_0", {-0.06137312f, -0.11203417f, 0.26487184f, 0.95578481f}},
		{"j_index_le_1", {0.02471975f, -0.01123070f, 0.33380824f, 0.94224990f}},
		{"j_mid_le_1", {-0.05499404f, -0.03509609f, 0.52064280f, 0.85127845f}},
		{"j_pinky_le_0", {-0.08517731f, 0.26642705f, 0.45277339f, 0.84661544f}},
		{"j_ring_le_0", {-0.15265552f, 0.15561585f, 0.35011278f, 0.91098905f}},
		{"j_thumb_le_1", {0.10318305f, 0.09588913f, -0.27167569f, 0.95202461f}},
		{"j_index_le_2", {-0.00155644f, 0.03634734f, 0.23691383f, 0.97084926f}},
		{"j_mid_le_2", {-0.02920618f, -0.00262459f, 0.37186756f, 0.92782252f}},
		{"j_pinky_le_1", {0.02200386f, 0.01736504f, 0.43614027f, 0.89944202f}},
		{"j_ring_le_1", {0.00994895f, 0.00973532f, 0.46729545f, 0.88399163f}},
		{"j_thumb_le_2", {-0.04129182f, -0.05899268f, 0.15762917f, 0.98486948f}},
		{"j_pinky_le_2", {-0.03164798f, 0.01159714f, 0.46718151f, 0.88351873f}},
		{"j_ring_le_2", {-0.00097659f, -0.00299082f, 0.37534761f, 0.92687878f}},
	};
}
