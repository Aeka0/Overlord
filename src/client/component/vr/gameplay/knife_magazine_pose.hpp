#pragma once
#include "weapon_profile.hpp"
namespace vr::gameplay::equipment::knife_magazine_pose
{
	// H2 h2_wpn_pst_usp_tactical_reload, frame 15; receiver tag_knife and tag_clip.
	// Source SHA256 0f55fcdcbdd7a65eb01b51548b99f2c8c935b7d3f9380ff420974d686215fdfa
	// Wrist-local native inches. Finger translations retain the live glove.
	inline constexpr hands::anchor reverse_attachment{{2.794830960f,-0.061122779f,1.161515487f},{-0.453117538f,0.698055306f,0.396629980f,-0.387411855f}};
	inline constexpr hands::joint_pose fingers[]{
		{"j_index_le_0",{0.716882553f,-0.190314161f,0.024994760f,0.670250093f}},
		{"j_mid_le_0",{0.535641564f,-0.396279843f,0.304679232f,0.680603385f}},
		{"j_thumb_le_0",{-0.135043470f,-0.201725960f,0.154376820f,0.957725271f}},
		{"j_index_le_1",{0.022919390f,-0.014526810f,0.461226092f,0.886867614f}},
		{"j_mid_le_1",{-0.013717960f,-0.016540890f,0.276068140f,0.960897809f}},
		{"j_pinky_le_0",{-0.058717200f,0.179722231f,0.707291975f,0.681168314f}},
		{"j_ring_le_0",{-0.117006350f,0.114961640f,0.729534901f,0.663982051f}},
		{"j_thumb_le_1",{0.094266889f,0.096324699f,-0.342599238f,0.929763985f}},
		{"j_index_le_2",{-0.000234380f,0.036398830f,0.272540200f,0.961455620f}},
		{"j_mid_le_2",{-0.028092300f,-0.008621480f,0.170735879f,0.984878547f}},
		{"j_pinky_le_1",{0.026246020f,0.009857520f,0.688530854f,0.724664915f}},
		{"j_ring_le_1",{0.012207460f,0.006805660f,0.685021641f,0.728388641f}},
		{"j_thumb_le_2",{-0.041673060f,-0.058748190f,0.164006620f,0.983826120f}},
		{"j_pinky_le_2",{-0.027680160f,0.019226580f,0.674364498f,0.737629088f}},
		{"j_ring_le_2",{-0.001861650f,-0.002472020f,0.649226119f,0.760589159f}},
	};
}
