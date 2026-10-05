#pragma once
#include "../../tube_profile.hpp"
namespace vr::gameplay::weapons::winchester1200
{
// h2_wpn_sho_w1200_reload_loop frame 4 SHA-256 5b052825e74711673b0e845ca310f1d57c0710cdce3e919cdccf62552fe28877
// h2_wpn_sho_w1200_reload_start_empty frame 20 SHA-256 c88c924c577dc102800ed7b2977381b9d56548666e301551298e7e7d46c86238
// h2_wpn_sho_w1200_reload_start_empty frame 20 SHA-256 c88c924c577dc102800ed7b2977381b9d56548666e301551298e7e7d46c86238
// h2_wpn_sho_w1200_reload_loop frame 6 SHA-256 5b052825e74711673b0e845ca310f1d57c0710cdce3e919cdccf62552fe28877
inline constexpr hands::anchor shell_in_wrist={{4.62378400f, 1.65220920f, 1.16030235f}, {-0.66171397f, -0.07030447f, -0.16502778f, 0.72798196f}};
inline constexpr hands::anchor bolt_rest={{7.81158763f, -0.67866201f, 3.95802475f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor bolt_open={{4.80985491f, -0.67877643f, 3.95810770f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor pump_rest={{19.39149842f, 0.03022500f, 2.54646305f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor port_shell={{8.19505930f, -0.17767848f, 3.78611288f}, {-0.03031801f, -0.01355024f, 0.01069893f, 0.99939119f}};
inline constexpr hands::anchor lifter_rest={{6.24558306f, 0.00000000f, 2.13382507f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor lifter_loaded={{6.24558306f, 0.00000000f, 2.13382507f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec shell_center={-0.38404823f, 0.23275428f, -0.06226540f};
inline constexpr hands::vec rack_contact={4.07056823f, -0.93571577f, 1.01979784f};
inline constexpr hands::vec rack_low={14.23663342f, -1.11962604f, 1.42365093f};
inline constexpr hands::vec rack_high={21.80021091f, 1.11962604f, 3.60346178f};
inline constexpr hands::vec port_center={7.33071213f, -1.25984252f, 4.39574052f};
inline constexpr hands::vec tube_center={6.52810252f, 0.04387538f, 1.25984252f};
inline constexpr hands::vec port_forward={0.97867273f, -0.20525760f, 0.00830656f};
inline constexpr hands::vec tube_forward={0.79119886f, 0.11824445f, 0.60001885f};
// Native RIGHT loading hand retargeted to canonical left around the rigid shell.
inline constexpr std::array<joint_pose, 18> shell_fingers{{
	{"j_index_le_0", {0.61864242f, -0.30512492f, 0.30039451f, 0.65874386f}},
	{"j_mid_le_0", {0.59015882f, -0.25091971f, 0.33124333f, 0.69211973f}},
	{"j_pinkypalm_le", {0.68991004f, -0.23197406f, -0.19031578f, 0.65878075f}},
	{"j_ringpalm_le", {0.71477630f, -0.11459738f, -0.10510610f, 0.68184674f}},
	{"j_thumb_le_0", {0.05768063f, -0.10974570f, 0.32679553f, 0.93692770f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987477f}},
	{"j_index_le_1", {0.02008138f, -0.01831133f, 0.60885114f, 0.79281884f}},
	{"j_mid_le_1", {-0.01919596f, -0.00952168f, 0.64130926f, 0.76698324f}},
	{"j_pinky_le_0", {-0.02694790f, 0.33326285f, 0.24173765f, 0.91091854f}},
	{"j_ring_le_0", {-0.10571703f, 0.18854500f, 0.36976557f, 0.90363052f}},
	{"j_thumb_le_1", {0.10476881f, 0.08481004f, -0.23260950f, 0.96318408f}},
	{"j_index_le_2", {0.00772117f, 0.03558465f, 0.47566245f, 0.87887391f}},
	{"j_mid_le_2", {-0.02929729f, 0.00253300f, 0.52790030f, 0.84879710f}},
	{"j_pinky_le_1", {0.02462864f, 0.01336723f, 0.58232845f, 0.81247050f}},
	{"j_ring_le_1", {0.01181052f, 0.00735485f, 0.65092191f, 0.75901719f}},
	{"j_thumb_le_2", {-0.02352994f, -0.06808730f, -0.11777178f, 0.99042429f}},
	{"j_pinky_le_2", {-0.03048790f, 0.01437419f, 0.54283121f, 0.83916515f}},
	{"j_ring_le_2", {-0.00143440f, -0.00280772f, 0.51353697f, 0.85806167f}},
}};
inline constexpr std::array<joint_pose, 18> rack_fingers{{
	{"j_index_le_0", {0.61213833f, -0.30201022f, -0.23004780f, 0.69365301f}},
	{"j_mid_le_0", {0.59962483f, -0.41764382f, -0.07055846f, 0.67900310f}},
	{"j_pinkypalm_le", {0.68993187f, -0.23197117f, -0.19037444f, 0.65874196f}},
	{"j_ringpalm_le", {0.71477381f, -0.11462749f, -0.10510572f, 0.68184435f}},
	{"j_thumb_le_0", {-0.21161455f, -0.10177885f, 0.12881815f, 0.96346574f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.05493320f, -0.14041536f, 0.21030260f, 0.96593928f}},
	{"j_mid_le_1", {-0.01733455f, -0.01263469f, 0.50380063f, 0.86355359f}},
	{"j_pinky_le_0", {-0.16165485f, 0.04043660f, 0.65156334f, 0.74006608f}},
	{"j_ring_le_0", {-0.19889097f, -0.19345862f, 0.48256314f, 0.83075204f}},
	{"j_thumb_le_1", {0.10772891f, 0.08108660f, -0.19910012f, 0.97065884f}},
	{"j_index_le_2", {-0.02258361f, 0.05465844f, 0.34888626f, 0.93529718f}},
	{"j_mid_le_2", {-0.02917556f, -0.00317391f, 0.35175469f, 0.93563206f}},
	{"j_pinky_le_1", {0.01965373f, 0.02001995f, 0.31925103f, 0.94725483f}},
	{"j_ring_le_1", {0.01000992f, 0.00973526f, 0.47046632f, 0.88230747f}},
	{"j_thumb_le_2", {-0.03689638f, -0.03900213f, -0.05416963f, 0.99708733f}},
	{"j_pinky_le_2", {-0.03369258f, 0.00115971f, 0.16840187f, 0.98514175f}},
	{"j_ring_le_2", {-0.00051882f, -0.00311289f, 0.23603032f, 0.97174057f}},
}};
inline constexpr hands::vec pump_symmetry_center{};
inline constexpr part_grip_pose rack_pose{"pump",wrists[0],rack_contact,rack_fingers,&pump_symmetry_center};
}
