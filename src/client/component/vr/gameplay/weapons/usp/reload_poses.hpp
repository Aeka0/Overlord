#pragma once
#include "../../part_grip_pose.hpp"

namespace vr::gameplay::weapons::usp
{
	// H1 h1_wpn_pst_usp_reload frame 20: magazine hand.
	// SHA256 548a364ba8084dfa0eb5d3413df62e0141dfef9763242491dc4301ddd84b43d6
	// H1 h1_wpn_pst_usp_inspect frame 103: slide hand, travel removed.
	// SHA256 2dc33827b043c9b42e23e63843ebcaf3642a768571ff6f2fed6f6949d22712fd
	// H2 magazine mesh registration: 445 body vertices, max residual < .000003 cm.
	// Receiver SHA256 8fc005b3fb0ca3b8d323790cea510aae4f6462530d6e288c73a24faf69f39e86
	// Magazine SHA256 7f6bd9eee22afe8b19a8835a5f4f564687775c696f6a8d26025d43325ab2af94
	inline constexpr hands::anchor magazine_in_wrist{{4.67882390f, 0.35029274f, 2.44226283f}, {0.01845287f, 0.27270804f, -0.14852723f, 0.95038386f}};
	// Native USP knife co-grasp, retaining this magazine's ordinary-grasp registration.
	inline constexpr hands::anchor knife_magazine_in_wrist{{4.560012509f,0.828771613f,3.618716006f},{-0.046751846f,-0.104185134f,-0.111925610f,0.987133416f}};
	// Calibrated preview X is the foremost finger SKIN edge, not a
	// wrist coordinate or translation amount. Base/suppressed USP share this pose.
	// Calibration glove: viewhands_us_army, SHA256
	// 023d1494eaf9cc24864a322e0aef5b54f389298d701ff3b9d67835ab65bbe82a.
	// HMD: X=-1.5 overshot. Return 1/3 of that shift: +4.74844506 cm forward.
	inline constexpr float slide_finger_front_cm = 3.24844506f;
	inline constexpr float slide_source_finger_front_cm = 12.74533519f;
	inline constexpr float slide_grasp_back = (slide_source_finger_front_cm-slide_finger_front_cm)/2.54f;
	inline constexpr hands::anchor slide_wrist_rest{{-1.48315318f-slide_grasp_back, 0.59702690f, 3.12644976f}, {0.99589248f, 0.04583485f, 0.07704207f, 0.01272215f}};
	inline constexpr hands::anchor rigid_in_magazine{{-0.09414101f, -0.07879500f, -0.15926805f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::vec magazine_top{0.39779450f, -0.06815556f, 2.07477173f};
	inline constexpr hands::anchor magazine_well{{0.39384250f, 0.01063944f, -1.78040764f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	// Rear half of the slide; enlarged acquisition tolerance is in metres below.
	inline constexpr hands::vec slide_grab_low{-1.47578740f, -1.04724409f, 1.80700787f},
		slide_grab_high{1.00000000f, 1.04724409f, 3.44716535f};
	// Rear-slide surface contact, within 1.41 mm of calibrated finger skin;
	// recomputed for the new grasp instead of shifting the old contact off-gun.
	inline const hands::vec slide_contact = hands::rotate(hands::conjugate(slide_wrist_rest.rotation),
		hands::sub(hands::vec{-2.90877223f/2.54f, 0.50351691f/2.54f, 6.63878822f/2.54f},slide_wrist_rest.position));
	inline constexpr joint_pose magazine_fingers[]{
		{"j_index_le_0", {0.65990460f, -0.35639670f, 0.14297984f, 0.64580498f}},
		{"j_mid_le_0", {0.57153360f, -0.43413024f, 0.22417982f, 0.65925995f}},
		{"j_thumb_le_0", {0.09359954f, -0.31983149f, 0.12671187f, 0.93428638f}},
		{"j_index_le_1", {0.02659794f, -0.00534283f, 0.11243209f, 0.99328900f}},
		{"j_mid_le_1", {-0.01665156f, -0.01356158f, 0.45618028f, 0.88962822f}},
		{"j_pinky_le_0", {-0.34553894f, 0.22833828f, 0.47380773f, 0.77715553f}},
		{"j_ring_le_0", {-0.24222470f, -0.13581289f, 0.44580107f, 0.85096619f}},
		{"j_thumb_le_1", {0.10687532f, 0.08222665f, -0.20952892f, 0.96845964f}},
		{"j_index_le_2", {-0.00641696f, 0.03580830f, 0.10458656f, 0.99385021f}},
		{"j_mid_le_2", {-0.02940806f, -0.00112917f, 0.41709926f, 0.90838434f}},
		{"j_pinky_le_1", {0.02653203f, 0.00901059f, 0.71217242f, 0.70144516f}},
		{"j_ring_le_1", {0.01221880f, 0.00666445f, 0.69307246f, 0.72073355f}},
		{"j_thumb_le_2", {-0.02436901f, -0.06778156f, -0.10623486f, 0.99172878f}},
		{"j_pinky_le_2", {-0.03270475f, 0.00802645f, 0.36515792f, 0.93033632f}},
		{"j_ring_le_2", {-0.00133519f, -0.00283824f, 0.48422089f, 0.87494016f}}
	};
	inline constexpr joint_pose slide_fingers[]{
		{"j_index_le_0", {0.63381292f, -0.34278634f, 0.06396725f, 0.69042516f}},
		{"j_mid_le_0", {0.55531604f, -0.41090213f, 0.14859496f, 0.70760375f}},
		{"j_thumb_le_0", {-0.20349524f, -0.19733057f, 0.21063649f, 0.93556539f}},
		{"j_index_le_1", {0.01867734f, 0.19098498f, 0.34553084f, 0.91857734f}},
		{"j_mid_le_1", {-0.00756859f, 0.20975382f, 0.36628333f, 0.90652224f}},
		{"j_pinky_le_0", {-0.08676350f, 0.09817734f, 0.44159966f, 0.88759396f}},
		{"j_ring_le_0", {-0.03860612f, -0.00335705f, 0.49663041f, 0.86709661f}},
		{"j_thumb_le_1", {0.22217359f, 0.17377149f, -0.27011792f, 0.92058605f}},
		{"j_index_le_2", {-0.00268560f, 0.03628616f, 0.20749336f, 0.97755952f}},
		{"j_mid_le_2", {-0.02932779f, -0.00088502f, 0.42658053f, 0.90397353f}},
		{"j_pinky_le_1", {-0.07605241f, 0.04657142f, 0.43876625f, 0.89416515f}},
		{"j_ring_le_1", {-0.05960262f, 0.10568555f, 0.40180652f, 0.90765060f}},
		{"j_thumb_le_2", {-0.03286792f, -0.06408786f, 0.02124055f, 0.99717666f}},
		{"j_pinky_le_2", {-0.03207526f, 0.01037639f, 0.43077285f, 0.90183051f}},
		{"j_ring_le_2", {-0.00109866f, -0.00296029f, 0.41386066f, 0.91033476f}}
	};
	inline const std::array<part_grip_pose,1> slide_grips{{
		{"h1_inspect_slide",slide_wrist_rest,slide_contact,slide_fingers}
	}};
}
