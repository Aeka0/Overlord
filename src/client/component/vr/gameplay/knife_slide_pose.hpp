#pragma once
#include "weapon_profile.hpp"
namespace vr::gameplay::equipment::knife_slide_pose
{
	// H2 h2_wpn_pst_usp_tactical_pullout_first, frame 17; pull_slide notetrack at 13.
	// Source SHA256 360c5b37df8b7271ce64b168a55ca9d21a6c1f3570fdfd2aeeb9e6d4887153d0
	// Receiver tag_knife relative to wrist; slide travel is removed from each gun-local hand.
	// HMD refinement: raise every knife/slide grasp along gun-local +Z by 1 cm.
	inline constexpr float wrist_raise=1.f/2.54f;
	inline constexpr hands::anchor reverse_attachment{{3.168057549f,0.218485158f,1.044348560f},{-0.296505885f,0.815047795f,0.426336034f,-0.256941506f}};
	inline constexpr hands::joint_pose fingers[]{
		{"j_index_le_0",{0.515707659f,-0.404588499f,0.216226110f,0.723602119f}},
		{"j_mid_le_0",{0.369672051f,-0.465927771f,0.450027571f,0.666129922f}},
		{"j_thumb_le_0",{-0.091220180f,-0.170690679f,0.203528719f,0.959749775f}},
		{"j_index_le_1",{0.029084200f,-0.012268470f,0.561236532f,0.827053293f}},
		{"j_mid_le_1",{-0.016846260f,-0.004089490f,0.863218204f,0.504533262f}},
		{"j_pinky_le_0",{-0.148320370f,0.330607930f,0.624898729f,0.691520819f}},
		{"j_ring_le_0",{-0.216865419f,0.234871419f,0.516039599f,0.794674738f}},
		{"j_thumb_le_1",{0.071534410f,0.011841020f,-0.242557801f,0.967423553f}},
		{"j_index_le_2",{0.013000780f,0.032776630f,0.458567588f,0.887959706f}},
		{"j_mid_le_2",{-0.026093340f,-0.003906370f,0.622059159f,0.782525578f}},
		{"j_pinky_le_1",{0.025727300f,0.011169860f,0.653040081f,0.756803801f}},
		{"j_ring_le_1",{0.012115880f,0.007019280f,0.676353212f,0.736444342f}},
		{"j_thumb_le_2",{-0.030915140f,-0.143344929f,0.045228270f,0.988155296f}},
		{"j_pinky_le_2",{-0.028504260f,0.017944870f,0.641284871f,0.766563241f}},
		{"j_ring_le_2",{-0.001556460f,-0.002777220f,0.549644539f,0.835392569f}},
	};
}
