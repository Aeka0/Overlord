#pragma once
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::pp2000
{
// h2_wpn_pst_pp2000_reload frame 30: b52038b4fcd472f7625a67be66a088a8a4ac6d9208f6cfc64aa57f33fda9ad55
// h2_wpn_pst_pp2000_first_time_pullout frame 9: da51467cfa97be842ef70797ada21d1a104871b4f74d4c38cceb5f4474417568
// Native left grasp carried from deflected j_reload_end to straight idle.
// No rotation is applied to the long reciprocating j_reload rod. HMD fit pending.
inline constexpr hands::anchor magazine_rest={{-0.03854800f, 0.00000000f, 2.23396481f}, {0.00000000f, -0.07846304f, 0.00000000f, 0.99691702f}};
inline constexpr hands::anchor action_rest={{5.69892603f, 0.00950003f, 3.48800518f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{5.95991308f, 0.86422192f, 6.15873991f}, {-0.00624821f, 0.21497499f, -0.02446699f, 0.97629303f}};
inline constexpr hands::anchor action_wrist={{5.01837157f, 5.60983286f, 3.68721888f}, {0.81156884f, -0.53653426f, 0.21842214f, -0.07601823f}};
inline constexpr hands::anchor magazine_well={{0.57195552f, -0.00050678f, -2.46235591f}, {0.00000000f, -0.07846304f, 0.00000000f, 0.99691702f}};
inline constexpr hands::vec magazine_top={-0.13171668f, -0.00050678f, -0.34214238f};
inline constexpr hands::vec action_grab_low={5.43441432f, -0.21687797f, 3.27524908f};
inline constexpr hands::vec action_grab_high={7.47181560f, 0.23587802f, 3.72800408f};
inline constexpr hands::vec action_contact={5.62211527f, 0.27483463f, 1.75328325f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.64929780f, -0.48298459f, 0.31273451f, 0.49732825f}},
	{"j_mid_le_0", {0.57346159f, -0.56760203f, 0.31222903f, 0.50148057f}},
	{"j_pinkypalm_le", {0.66390850f, -0.17118097f, -0.23990819f, 0.68728934f}},
	{"j_ringpalm_le", {0.67924888f, -0.05762230f, -0.14922097f, 0.71626373f}},
	{"j_thumb_le_0", {-0.09076173f, -0.27954857f, 0.21556087f, 0.93120804f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.01712084f, -0.02326725f, 0.47904782f, 0.87731334f}},
	{"j_mid_le_1", {0.01113922f, -0.00976589f, 0.70700782f, 0.70705055f}},
	{"j_pinky_le_0", {0.21058903f, -0.13349960f, 0.70566431f, 0.66322545f}},
	{"j_ring_le_0", {0.14852240f, -0.13620504f, 0.61658799f, 0.76105751f}},
	{"j_thumb_le_1", {0.09938011f, 0.09111823f, -0.29187089f, 0.94690678f}},
	{"j_index_le_2", {0.01348917f, 0.03375956f, 0.61453870f, 0.78804855f}},
	{"j_mid_le_2", {-0.02931595f, 0.00210577f, 0.51518067f, 0.85657750f}},
	{"j_pinky_le_1", {0.01386765f, 0.04369043f, 0.60667928f, 0.79362402f}},
	{"j_ring_le_1", {0.01197541f, 0.00715961f, 0.66461090f, 0.74705935f}},
	{"j_thumb_le_2", {-0.02249207f, -0.06843450f, -0.13379274f, 0.98838774f}},
	{"j_pinky_le_2", {0.01733468f, 0.04187790f, 0.76763940f, 0.63927733f}},
	{"j_ring_le_2", {-0.00190437f, -0.00252084f, 0.65592591f, 0.75481866f}},
}};
inline constexpr std::array<joint_pose, 18> action_fingers{{
	{"j_index_le_0", {0.64288014f, -0.13052924f, 0.07788417f, 0.75073384f}},
	{"j_mid_le_0", {0.64758953f, -0.32348958f, 0.27170073f, 0.63416166f}},
	{"j_pinkypalm_le", {0.64744284f, -0.27878342f, -0.14758584f, 0.69376941f}},
	{"j_ringpalm_le", {0.68059005f, -0.16656923f, -0.05697779f, 0.71119997f}},
	{"j_thumb_le_0", {-0.11124091f, -0.37620485f, 0.28403817f, 0.87488153f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02252263f, -0.01507613f, 0.48414506f, 0.87456789f}},
	{"j_mid_le_1", {-0.01898239f, -0.01001001f, 0.62351972f, 0.78151304f}},
	{"j_pinky_le_0", {0.01477082f, 0.16000708f, 0.30655563f, 0.93819145f}},
	{"j_ring_le_0", {-0.02386506f, -0.00558479f, 0.31195484f, 0.94978074f}},
	{"j_thumb_le_1", {0.03506526f, 0.18097213f, -0.37586050f, 0.90815660f}},
	{"j_index_le_2", {0.00506607f, 0.03598131f, 0.40949718f, 0.91158748f}},
	{"j_mid_le_2", {-0.02932843f, 0.00170904f, 0.50371046f, 0.86337286f}},
	{"j_pinky_le_1", {0.02600162f, 0.01043727f, 0.67234947f, 0.73970344f}},
	{"j_ring_le_1", {0.01208536f, 0.00695824f, 0.67558982f, 0.73714580f}},
	{"j_thumb_le_2", {-0.02481128f, -0.06759776f, -0.09958081f, 0.99242058f}},
	{"j_pinky_le_2", {-0.03057974f, 0.01416068f, 0.53706810f, 0.84286548f}},
	{"j_ring_le_2", {-0.00146490f, -0.00277720f, 0.52125855f, 0.85339303f}},
}};
inline constexpr std::array<part_grip_pose,1> action_grips{{{"top_handle",action_wrist,action_contact,action_fingers}}};
}
