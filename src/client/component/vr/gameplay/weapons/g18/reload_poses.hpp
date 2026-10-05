#pragma once
#include "../../part_grip_pose.hpp"

namespace vr::gameplay::weapons::g18
{
// h2_wpn_pst_glock_reload frame 10: 456abd86a976aefbfe56f27025044d8f0f7e8ad482aae68a40000f24c6b35eef
// h2_wpn_pst_glock_first_time_pullout frame 10: 03943caf346b1ea7f78ee12756da9f5dc943efdf7d353c2954a1d2bac3e81abc
// Source slide grasp moved back 0.99650324 cm for the rear-slide candidate.
// Finger skin front X=3 cm; nearest slide surface distance=2.50 mm. HMD pending.
inline constexpr hands::anchor magazine_in_wrist={{7.19306342f, 7.32102317f, 1.35735025f}, {-0.63352081f, 0.29548382f, -0.09105585f, 0.70925984f}};
inline constexpr hands::anchor slide_wrist_rest={{-3.48813709f, 0.73908387f, 6.52272193f}, {0.95533514f, 0.06939264f, -0.26296520f, -0.11562328f}};
// Extended clip: well is the receiver mouth, not the protruding baseplate.
inline constexpr hands::vec magazine_top={0.58368701f, 0.00000714f, -0.17377920f};
inline constexpr hands::anchor magazine_well={{-0.16306782f, 0.00000714f, -2.14438885f}, {0.00000000f, 0.18393474f, 0.00000000f, 0.98293846f}};
inline constexpr hands::vec slide_grab_low={-1.55511811f, -0.72834646f, 2.22440945f}, slide_grab_high={0.62992126f, 0.78740157f, 4.05511811f};
inline constexpr hands::vec slide_contact={2.96088474f, 1.90286153f, 1.35541165f};
inline constexpr std::array<joint_pose,15> magazine_fingers{{
	{"j_index_le_0", {0.56995977f, -0.34320880f, 0.05673352f, 0.74440237f}},
	{"j_mid_le_0", {0.44843940f, -0.52623116f, 0.24192045f, 0.68077703f}},
	{"j_thumb_le_0", {-0.33384347f, -0.14059940f, 0.29777043f, 0.88324013f}},
	{"j_index_le_1", {-0.00418101f, -0.07193174f, 0.65394714f, 0.75310125f}},
	{"j_mid_le_1", {-0.01950128f, -0.00772116f, 0.68129343f, 0.73170988f}},
	{"j_pinky_le_0", {-0.08795565f, 0.05682631f, 0.57964664f, 0.80811159f}},
	{"j_ring_le_0", {-0.12713963f, -0.11419983f, 0.57826253f, 0.79775082f}},
	{"j_thumb_le_1", {0.17255284f, 0.32682497f, -0.21610305f, 0.90372033f}},
	{"j_index_le_2", {0.01303139f, 0.03390602f, 0.60450983f, 0.79576908f}},
	{"j_mid_le_2", {0.00045778f, 0.01727357f, 0.38673257f, 0.92203001f}},
	{"j_pinky_le_1", {0.02600194f, 0.00796538f, 0.67025194f, 0.74163521f}},
	{"j_ring_le_1", {0.01208535f, 0.00698875f, 0.67311747f, 0.73940379f}},
	{"j_thumb_le_2", {-0.01464898f, -0.07382475f, -0.23215578f, 0.96976235f}},
	{"j_pinky_le_2", {-0.03341792f, 0.00665307f, 0.39390418f, 0.91851972f}},
	{"j_ring_le_2", {-0.00106814f, -0.00299078f, 0.40531226f, 0.91417279f}},
}};
inline constexpr std::array<joint_pose,15> slide_fingers{{
	{"j_index_le_0", {0.71597251f, -0.25968500f, 0.17499516f, 0.62395814f}},
	{"j_mid_le_0", {0.55803970f, -0.44035848f, 0.21543354f, 0.66952557f}},
	{"j_thumb_le_0", {-0.18625534f, -0.16144367f, 0.32603077f, 0.91266030f}},
	{"j_index_le_1", {0.01849409f, -0.01977586f, 0.66938858f, 0.74241889f}},
	{"j_mid_le_1", {-0.01879939f, -0.01037629f, 0.60789780f, 0.79372488f}},
	{"j_pinky_le_0", {-0.01461834f, 0.10559385f, 0.69560709f, 0.71046958f}},
	{"j_ring_le_0", {-0.11728282f, -0.07669314f, 0.58061557f, 0.80202772f}},
	{"j_thumb_le_1", {0.14539008f, 0.19434173f, -0.19595921f, 0.95010158f}},
	{"j_index_le_2", {-0.00494395f, 0.03601152f, 0.14502267f, 0.98876041f}},
	{"j_mid_le_2", {-0.02777226f, 0.00967451f, 0.72119360f, 0.69210903f}},
	{"j_pinky_le_1", {0.02441500f, 0.01358084f, 0.57311150f, 0.81900102f}},
	{"j_ring_le_1", {0.01086447f, 0.00869768f, 0.55448448f, 0.83207768f}},
	{"j_thumb_le_2", {-0.01705990f, 0.06054890f, 0.05056932f, 0.99673745f}},
	{"j_pinky_le_2", {-0.02877851f, 0.01739528f, 0.62638283f, 0.77878993f}},
	{"j_ring_le_2", {-0.00201421f, -0.00238043f, 0.70137338f, 0.71278725f}},
}};
inline constexpr std::array<part_grip_pose,1> slide_grips{{{"rear_overhand",slide_wrist_rest,slide_contact,slide_fingers}}};
}
