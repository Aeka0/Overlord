#pragma once
#include "handle_poses.hpp"
#include "../../families/ar.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::m16
{
// M16 reload frame 39: e36234dd287a1f5df5d778afe483a24a571b6d6da5744b45aff41955f0d93250
// Right-hand pullout_first frame 14: 7f986975226c3e41d380e4fc9df11133aa73ba8243b9fc81ad6bad479bb0d1ad
// Handle grip mirrors weapon Y and native hand-local parity, as the M4 adapter.
inline constexpr hands::anchor magazine_rest={{5.18395356f, -0.00839800f, 2.81061995f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor handle_rest={{-0.51850055f, 0.00000000f, 4.45790011f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{5.98924793f, 0.52026717f, 6.10174410f}, {-0.01021015f, 0.35329798f, 0.15227748f, 0.92297771f}};
inline constexpr hands::anchor magazine_well={{4.29550532f, 0.01200614f, 0.43607040f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec magazine_top={-0.87030792f, 0.04560138f, 0.81281420f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.66737460f, -0.35535805f, 0.48700118f, 0.43722037f}},
	{"j_mid_le_0", {0.47802610f, -0.38807576f, 0.14225939f, 0.77501646f}},
	{"j_pinkypalm_le", {0.68992174f, -0.23198503f, -0.19037384f, 0.65874785f}},
	{"j_ringpalm_le", {0.71477024f, -0.11459767f, -0.10508380f, 0.68185649f}},
	{"j_thumb_le_0", {-0.03958270f, -0.32819767f, 0.26307085f, 0.90637367f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02882821f, -0.03415603f, -0.09073726f, 0.99487137f}},
	{"j_mid_le_1", {-0.01472162f, -0.01553388f, 0.33822405f, 0.94082223f}},
	{"j_pinky_le_0", {-0.33621704f, -0.02924455f, 0.64463917f, 0.68596152f}},
	{"j_ring_le_0", {-0.33194193f, -0.16557071f, 0.47564281f, 0.79759941f}},
	{"j_thumb_le_1", {-0.01440487f, 0.09631729f, -0.50737483f, 0.85620457f}},
	{"j_index_le_2", {-0.00928463f, 0.03513253f, 0.02529974f, 0.99901923f}},
	{"j_mid_le_2", {-0.02335267f, -0.00440056f, 0.00971081f, 0.99967044f}},
	{"j_pinky_le_1", {0.02027040f, 0.01925741f, 0.35257162f, 0.93536705f}},
	{"j_ring_le_1", {0.00889964f, 0.01072488f, 0.37938051f, 0.92513578f}},
	{"j_thumb_le_2", {-0.06817875f, -0.04934872f, 0.06015234f, 0.99463464f}},
	{"j_pinky_le_2", {-0.03250826f, -0.00875886f, -0.12836742f, 0.99115503f}},
	{"j_ring_le_2", {-0.03624452f, -0.00358947f, -0.00112332f, 0.99933587f}},
}};
inline const auto& handle_fingers=handle_pose_fingers_0;

// Match the M4 handle-head capture footprint at the M16's own rest origin.
// The long forward stem is not an additional grasp surface.
inline constexpr auto handle_capture_box=[] {
	std::array<hands::vec,2> bounds{families::ar::contact_low,families::ar::contact_high};
	for(auto& point:bounds)for(size_t i=0;i<3;++i)point[i]+=handle_rest.position[i]-families::ar::contact_reference.position[i];
	// M16's measured upper contact is slightly higher than the rest-origin
	// translation predicts. Align the box to that head, retaining M4 dimensions.
	const float raise=4.97273310f-bounds[1][2];
	for(auto& point:bounds)point[2]+=raise;
	return bounds;
}();
inline constexpr auto handle_grab_low=handle_capture_box[0],handle_grab_high=handle_capture_box[1];
inline constexpr float handle_stroke_m=0.08102555215358734f;
inline constexpr magazine_contact_profile contacts{{1.49515365f, -0.77024372f, 1.49262409f},{3.68860500f, -0.63981095f, -3.88408796f},{7.24097650f, 0.75472495f, 3.67994384f},{-0.11788762f, 0.70143885f, 4.67249387f},{}};
}
