#pragma once
#include "../../part_grip_pose.hpp"
#include "../../knife_slide_pose.hpp"
namespace vr::gameplay::weapons::de50
{
	// HMD: open thumb/index/middle 25% toward pickup frame 13.
	// Thumb/index distal joint separation grows about 4 mm; ring/pinky retain the knife.
	inline constexpr joint_pose knife_slide_fingers[]{
		{"j_index_le_0",{0.527147477f,-0.380945998f,0.192873345f,0.734707804f}},
		{"j_mid_le_0",{0.388791071f,-0.449780225f,0.430439028f,0.679162348f}},
		{"j_thumb_le_0",{-0.088455252f,-0.170325893f,0.208949428f,0.958908179f}},
		{"j_index_le_1",{0.028186831f,-0.012347737f,0.523726868f,0.851330255f}},
		{"j_mid_le_1",{-0.017127700f,-0.004856389f,0.843475421f,0.536872678f}},
		{"j_pinky_le_0",{-0.148320370f,0.330607930f,0.624898729f,0.691520819f}},
		{"j_ring_le_0",{-0.216865419f,0.234871419f,0.516039599f,0.794674738f}},
		{"j_thumb_le_1",{0.073936452f,0.017967049f,-0.243875198f,0.966817187f}},
		{"j_index_le_2",{0.010214690f,0.033620246f,0.421367798f,0.906208871f}},
		{"j_mid_le_2",{-0.026587274f,-0.003794815f,0.604078044f,0.796472493f}},
		{"j_pinky_le_1",{0.025727300f,0.011169860f,0.653040081f,0.756803801f}},
		{"j_ring_le_1",{0.012115880f,0.007019280f,0.676353212f,0.736444342f}},
		{"j_thumb_le_2",{-0.031214870f,-0.136868959f,0.047200065f,0.988971523f}},
		{"j_pinky_le_2",{-0.028504260f,0.017944870f,0.641284871f,0.766563241f}},
		{"j_ring_le_2",{-0.001556460f,-0.002777220f,0.549644539f,0.835392569f}},
	};
	// Native USP pickup frame 17, fitted by slide rear edge and upper body height.
	// Shared +Z lift; contact rebound to the real slide, not lifted off the metal.
	inline const std::array<part_grip_pose,1> knife_slide_grips{{
		{"knife_pickup_slide",{{-3.756540807f,-0.039551332f,4.894780926f+equipment::knife_slide_pose::wrist_raise},{0.991546151f,0.119713046f,-0.009198040f,-0.049197689f}},{3.232027206f,0.141570123f,2.238113749f},knife_slide_fingers}
	}};
}
