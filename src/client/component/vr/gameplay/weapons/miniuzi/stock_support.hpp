#pragma once
#include "poses.hpp"

namespace vr::gameplay::weapons::miniuzi
{
// MP9 left support, h2_wpn_pst_mp9_idle frame 0.
// SHA256 accd4da9b6c276627195e9b8807bf81bbaa0d1869a2baa926b02b20fbcf678a7
// Fit to folded j_support_chin_arm; current glove segment lengths retained.
// HMD alignment: left wrist at gun-local (10, 3, -5) cm.
// Left wrist yaw is 5 degrees clockwise viewed from above (+Z).
inline constexpr hands::anchor stock_support={{3.93700787f, 1.18110236f, -1.96850394f}, {0.70713843f, -0.33842693f, 0.03012027f, 0.62009291f}};
// Folded front stock section (X>10 cm, Z=-6..-4 cm): Y=-3.44226384..0.16743934 cm.
// Preserve this right-offset hardware centre when reflecting the right hand.
inline constexpr hands::vec stock_contact_center{0,-1.63741225f/2.54f,0};
inline constexpr std::array<joint_pose,18> stock_fingers{{
	{"j_index_le_0", {0.46754426f, -0.40955901f, 0.51878492f, 0.58696337f}},
	{"j_mid_le_0", {0.40027910f, -0.47230248f, 0.50050146f, 0.60514899f}},
	{"j_pinkypalm_le", {0.68993675f, -0.23194229f, -0.19037579f, 0.65874662f}},
	{"j_ringpalm_le", {0.71476394f, -0.11456487f, -0.10510427f, 0.68186545f}},
	{"j_thumb_le_0", {0.18576440f, -0.43229057f, 0.30036031f, 0.82969882f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.00329594f, 0.01507588f, 0.51624198f, 0.85630373f}},
	{"j_mid_le_1", {-0.01843346f, -0.01092579f, 0.58370577f, 0.81168246f}},
	{"j_pinky_le_0", {-0.08340729f, 0.19306056f, 0.66277209f, 0.71868213f}},
	{"j_ring_le_0", {-0.12042507f, 0.07937800f, 0.71870512f, 0.68019107f}},
	{"j_thumb_le_1", {0.08789409f, 0.10223792f, -0.40196288f, 0.90567537f}},
	{"j_index_le_2", {0.03552395f, 0.11603268f, 0.40541245f, 0.90604372f}},
	{"j_mid_le_2", {-0.02932799f, 0.00155643f, 0.49793497f, 0.86671692f}},
	{"j_pinky_le_1", {0.02200399f, 0.01733463f, 0.43657024f, 0.89923398f}},
	{"j_ring_le_1", {0.01007114f, 0.00961336f, 0.48176660f, 0.87618896f}},
	{"j_thumb_le_2", {-0.00515755f, -0.07183951f, -0.37018407f, 0.92616210f}},
	{"j_pinky_le_2", {-0.03170878f, 0.01126135f, 0.45802245f, 0.88830354f}},
	{"j_ring_le_2", {-0.00122075f, -0.00286876f, 0.46071040f, 0.88754504f}},
}};
inline constexpr auto stock_pose=[] {
	std::array<joint_pose,33> out{};
	std::copy(idle_fingers.begin(),idle_fingers.end(),out.begin());
	size_t count=idle_fingers.size();
	for (const auto& joint:stock_fingers)
	{
		bool found=false;
		for (size_t i=0;i<count;++i) if (out[i].name==joint.name) { out[i]=joint; found=true; break; }
		if (!found) out[count++]=joint;
	}
	return out;
}();
}
