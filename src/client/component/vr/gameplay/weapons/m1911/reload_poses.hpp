#pragma once
#include "../../part_grip_pose.hpp"

namespace vr::gameplay::weapons::m1911
{
	// Candidate poses; HMD fit is pending. No original clips/meshes shipped.
	// Magazine: h2_wpn_pst_m1911_reload_empty frame 22.
	// SHA256 5107911439124162b624efc75c1ed4b6c876ef160d2a3218a37ec8707b7a5746
	// Slide: h2_wpn_pst_m1911_pullout_first frame 13, travel removed.
	// SHA256 36b6fce20ce03df65d6f62dc590904bcd651c66c3f21d336b898e283f261abd7
	inline constexpr hands::anchor magazine_in_wrist{{5.06942160f, -0.03715059f, 2.94227158f}, {0.01457321f, 0.24729596f, -0.14869962f, 0.95735090f}};
	// Native USP knife co-grasp, retaining this magazine's ordinary-grasp registration.
	inline constexpr hands::anchor knife_magazine_in_wrist{{4.516160176f,0.602115031f,4.325392323f},{-0.049739681f,-0.130423606f,-0.110362046f,0.984040581f}};
	// Calibrated preview X coordinate: the foremost FINGER SKIN edge, not
	// the wrist, grasp centre or a "source minus N" distance. Closed slide frame.
	// Calibration glove: viewhands_us_army, SHA256
	// 023d1494eaf9cc24864a322e0aef5b54f389298d701ff3b9d67835ab65bbe82a.
	// Other live glove dimensions are retained and still need visual verification.
	// HMD: after the quarter-return candidate at X=4.84335427, move another
	// 1.5 cm toward the muzzle. This translates the whole grasp, not its curls.
	inline constexpr float slide_finger_front_cm = 6.34335427f;
	inline constexpr float slide_source_finger_front_cm = 13.87341707f;
	inline constexpr float slide_grasp_back = (slide_source_finger_front_cm-slide_finger_front_cm)/2.54f;
	inline constexpr hands::anchor slide_wrist_rest{{-1.31956988f-slide_grasp_back, 0.58226007f, 3.75442537f}, {0.99580233f, 0.01152831f, 0.04416179f, -0.07933821f}};
	inline constexpr hands::anchor rigid_in_magazine{{-1.02024205f, 0.00000000f, -0.27043885f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::vec magazine_top{0.17522350f, -0.03702679f, 1.53807877f};
	inline constexpr hands::anchor magazine_well{{0.76567186f, -0.03159300f, -2.12452542f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::vec slide_grab_low{-0.72595501f, -0.49504300f, 2.71685803f}, slide_grab_high{1.00000000f, 0.49504600f, 3.66173992f};
	// Rebind the acquisition contact to the rear slide surface (0.17 mm from
	// calibrated finger skin). Translating the OLD contact with the wrist would
	// put it behind the gun, no longer on the slide. Stroke/tolerance stay intact.
	inline const hands::vec slide_contact = hands::rotate(hands::conjugate(slide_wrist_rest.rotation),
		hands::sub(hands::vec{2.12521291f/2.54f, 1.19250453f/2.54f, 7.92911577f/2.54f},slide_wrist_rest.position));
	inline constexpr joint_pose magazine_fingers[]{
		{"j_index_le_0", {0.63233618f, -0.40412141f, 0.18887686f, 0.63337380f}},
		{"j_mid_le_0", {0.57151855f, -0.43413040f, 0.22417227f, 0.65927546f}},
		{"j_thumb_le_0", {0.08761918f, -0.32151447f, 0.14389565f, 0.93179685f}},
		{"j_index_le_1", {0.02700874f, -0.00254828f, 0.00824758f, 0.99959793f}},
		{"j_mid_le_1", {-0.01664393f, -0.01356921f, 0.45619547f, 0.88962045f}},
		{"j_pinky_le_0", {-0.35156541f, 0.23738295f, 0.46742749f, 0.77560469f}},
		{"j_ring_le_0", {-0.24134844f, -0.13363906f, 0.44660433f, 0.85113812f}},
		{"j_thumb_le_1", {0.10687428f, 0.08224620f, -0.20965914f, 0.96842991f}},
		{"j_index_le_2", {-0.00925471f, 0.03514960f, 0.02522730f, 0.99902074f}},
		{"j_mid_le_2", {-0.02941948f, -0.00112917f, 0.41708362f, 0.90839114f}},
		{"j_pinky_le_1", {0.02653968f, 0.00892286f, 0.71218040f, 0.70143788f}},
		{"j_ring_le_1", {0.01222636f, 0.00667204f, 0.69306855f, 0.72073712f}},
		{"j_thumb_le_2", {-0.02435884f, -0.06782735f, -0.10658584f, 0.99168824f}},
		{"j_pinky_le_2", {-0.03269296f, 0.00801492f, 0.36518454f, 0.93032639f}},
		{"j_ring_le_2", {-0.00135045f, -0.00283823f, 0.48422672f, 0.87493691f}}
	};
	inline constexpr joint_pose slide_fingers[]{
		{"j_index_le_0", {0.56367291f, -0.27545813f, 0.00787372f, 0.77867430f}},
		{"j_mid_le_0", {0.51841925f, -0.27897041f, 0.10763906f, 0.80114345f}},
		{"j_thumb_le_0", {-0.11676379f, -0.12167727f, 0.22681351f, 0.95922703f}},
		{"j_index_le_1", {0.02478085f, -0.01101710f, 0.32630151f, 0.94487664f}},
		{"j_mid_le_1", {-0.01748686f, -0.01235982f, 0.51889895f, 0.85456733f}},
		{"j_pinky_le_0", {-0.04968380f, 0.28055480f, 0.50660390f, 0.81374014f}},
		{"j_ring_le_0", {-0.17078013f, 0.16980354f, 0.34610747f, 0.90675825f}},
		{"j_thumb_le_1", {0.10126063f, 0.08905320f, -0.27225622f, 0.95272890f}},
		{"j_index_le_2", {-0.00213629f, 0.03631696f, 0.22202178f, 0.97436279f}},
		{"j_mid_le_2", {-0.02917583f, -0.00369276f, 0.33469799f, 0.94186644f}},
		{"j_pinky_le_1", {0.02243086f, 0.01681552f, 0.45865777f, 0.88817067f}},
		{"j_ring_le_1", {0.01013218f, 0.00961336f, 0.48552041f, 0.87411375f}},
		{"j_thumb_le_2", {-0.02633731f, -0.06707927f, -0.07757757f, 0.99437842f}},
		{"j_pinky_le_2", {-0.03213557f, 0.01028460f, 0.42972550f, 0.90232895f}},
		{"j_ring_le_2", {-0.00085451f, -0.00305181f, 0.32953437f, 0.94413826f}}
	};
	inline const std::array<part_grip_pose,1> slide_grips{{
		{"native_slide",slide_wrist_rest,slide_contact,slide_fingers}
	}};
}
