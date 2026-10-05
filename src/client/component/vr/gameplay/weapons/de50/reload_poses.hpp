#pragma once
#include "../../part_grip_pose.hpp"

namespace vr::gameplay::weapons::de50
{
	// Candidate poses; HMD fit is pending. No original clips/meshes shipped.
	// Magazine: h2_wpn_pst_de50_reload frame 31.
	// SHA256 8ad2d8272b2c27926708613b90e2d72117d461937bf85a47cc370e0312f88418
	// Slide: h2_wpn_pst_de50_first_time_pullout frame 13, travel removed.
	// SHA256 ce273d1383d53cb0e2249e51356f9caca915259083777feb2f3f8dc00f80e492
	inline constexpr hands::anchor magazine_in_wrist{{3.68991629f, 1.61055433f, 3.87123462f}, {-0.17796848f, 0.17649370f, -0.05593902f, 0.96646160f}};
	// HMD: shared knife-grasp rail orientation, registered 1 cm above the magazine base.
	// The ordinary DE50 grasp rotation must not leak into this common hand pose.
	inline constexpr hands::anchor knife_magazine_in_wrist{{4.663992367f,0.806869976f,4.662478549f},{-0.046751846f,-0.104185134f,-0.111925610f,0.987133416f}};
	inline constexpr hands::anchor slide_wrist_rest{{-3.21248785f, 0.66401617f, 5.91906399f}, {0.96953686f, 0.07695977f, -0.21692974f, -0.08376730f}};
	inline constexpr hands::anchor rigid_in_magazine{{-0.17792659f, 0.00000150f, -0.65286218f}, {0.00000000f, -0.12680247f, 0.00000000f, 0.99192799f}};
	inline constexpr hands::vec magazine_top{0.16628131f, 0.00143019f, 1.99777280f};
	inline constexpr hands::anchor magazine_well{{-0.40383016f, 0.00143019f, -2.74761000f}, {0.00000000f, 0.12677245f, 0.00000000f, 0.99193183f}};
	inline constexpr hands::vec slide_grab_low{-1.69635495f, -1.04827796f, 2.92659204f}, slide_grab_high{1.00000000f, 1.05657005f, 3.87147393f};
	inline const hands::vec slide_contact = hands::rotate(hands::conjugate(slide_wrist_rest.rotation),
		hands::sub(hands::vec{-0.34817747f, 0.00000000f, 3.39903298f},slide_wrist_rest.position));
	inline constexpr joint_pose magazine_fingers[]{
		{"j_index_le_0", {0.73790012f, -0.23172487f, 0.10833969f, 0.62455544f}},
		{"j_mid_le_0", {0.47434331f, -0.47040647f, 0.47333621f, 0.57416811f}},
		{"j_thumb_le_0", {-0.10851819f, -0.12159556f, 0.12975943f, 0.97805972f}},
		{"j_index_le_1", {-0.05655194f, 0.00460837f, 0.69156354f, 0.72008369f}},
		{"j_mid_le_1", {-0.01985252f, -0.00837743f, 0.69268670f, 0.72091666f}},
		{"j_pinky_le_0", {-0.10519740f, 0.01889097f, 0.63261875f, 0.76705290f}},
		{"j_ring_le_0", {-0.02089301f, 0.05576543f, 0.61157391f, 0.78894300f}},
		{"j_thumb_le_1", {0.10105426f, 0.12327340f, -0.16867812f, 0.97269697f}},
		{"j_index_le_2", {0.02778739f, 0.02111900f, 0.54039648f, 0.84068632f}},
		{"j_mid_le_2", {-0.00403372f, -0.02853200f, 0.59692901f, 0.80177641f}},
		{"j_pinky_le_1", {0.02732905f, 0.00625623f, 0.77916799f, 0.62618785f}},
		{"j_ring_le_1", {0.00374364f, 0.00721262f, 0.75188219f, 0.65924740f}},
		{"j_thumb_le_2", {-0.05285096f, -0.02787137f, -0.08201940f, 0.99483807f}},
		{"j_pinky_le_2", {-0.02670364f, 0.02050076f, 0.70952328f, 0.70387737f}},
		{"j_ring_le_2", {0.04513726f, 0.14951145f, 0.73226733f, 0.66286765f}}
	};
	inline constexpr joint_pose slide_fingers[]{
		{"j_index_le_0", {0.67613087f, -0.14438164f, 0.08261279f, 0.71775770f}},
		{"j_mid_le_0", {0.50385656f, -0.35895584f, 0.36502897f, 0.69572489f}},
		{"j_thumb_le_0", {-0.16000617f, -0.21326018f, 0.34073410f, 0.90156442f}},
		{"j_index_le_1", {0.00930800f, -0.00897230f, 0.71247417f, 0.70157923f}},
		{"j_mid_le_1", {-0.02032548f, -0.00695827f, 0.73656962f, 0.67602045f}},
		{"j_pinky_le_0", {-0.21640718f, 0.09570623f, 0.74202849f, 0.62721764f}},
		{"j_ring_le_0", {-0.19312015f, 0.02853466f, 0.60264596f, 0.77376238f}},
		{"j_thumb_le_1", {0.12054721f, 0.19873504f, -0.27368794f, 0.93331006f}},
		{"j_index_le_2", {0.00692782f, 0.03570726f, 0.45564909f, 0.88941605f}},
		{"j_mid_le_2", {-0.02487238f, 0.01559483f, 0.85670837f, 0.51496498f}},
		{"j_pinky_le_1", {0.02566613f, 0.01129188f, 0.64525447f, 0.76345295f}},
		{"j_ring_le_1", {0.01190202f, 0.00717173f, 0.65815146f, 0.75275730f}},
		{"j_thumb_le_2", {-0.01052894f, 0.04107813f, -0.04083398f, 0.99826565f}},
		{"j_pinky_le_2", {-0.02450670f, 0.02307231f, 0.77591317f, 0.62994115f}},
		{"j_ring_le_2", {-0.00250250f, -0.00183110f, 0.84645446f, 0.53245209f}}
	};
	inline const std::array<part_grip_pose,1> slide_grips{{
		{"native_slide",slide_wrist_rest,slide_contact,slide_fingers}
	}};
}

