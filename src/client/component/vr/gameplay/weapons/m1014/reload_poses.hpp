#pragma once
#include "../hand_poses/edge_handle.hpp"
#include "../hand_poses/right_handle.hpp"
#include "../../tube_profile.hpp"
#include "../hand_poses/shell_grasp.hpp"
namespace vr::gameplay::weapons::m1014
{
// Rack grasp borrows M14 EBR frame 73; contact fits the actual right handle.
// M1014 reload_start_empty presses the release and is not a grasp.
// Align the actual M1014 shell bounds to the SPAS shell held by this grasp.
// Gun loading ports and shell geometry remain M1014-specific (native units).
inline constexpr hands::vec shell_model_in_source={-.07236736f,-.03993899f,-.02598350f};
inline const auto shell_in_wrist=hand_poses::shell_grasp::for_shell(shell_model_in_source);
inline constexpr hands::anchor bolt_rest={{8.28158762f, -1.33497706f, 3.25361612f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor rack_wrist={{4.69516805f, 0.91489415f, 2.58384116f}, hand_poses::right_handle::rotation};
inline constexpr hands::anchor lifter_rest={{5.84112303f, 0.00000000f, 1.85320903f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor lifter_loaded={{5.54705112f, -0.00000310f, 1.85592080f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec shell_center={0.24585687f, 0.02303444f, 0.01011441f};
inline constexpr auto rack_contact=hand_poses::right_handle::contact;
inline constexpr hands::vec rack_low={8.07825674f, -1.88509705f, 3.26662402f};
inline constexpr hands::vec rack_high={8.58865347f, -1.48412299f, 3.77702000f};
inline constexpr hands::vec port_center={7.63536103f, -1.25984252f, 3.71978681f};
inline constexpr hands::vec tube_center={7.09817265f, -0.06642464f, 1.25984252f};
inline constexpr hands::vec port_forward={0.92884888f, 0.36886628f, 0.03431363f};
inline constexpr hands::vec tube_forward={0.80836708f, 0.16702010f, 0.56448822f};
inline constexpr auto& shell_fingers=hand_poses::shell_grasp::shell_fingers;
inline constexpr auto& rack_fingers=hand_poses::right_handle::fingers;
inline constexpr part_grip_pose rack_pose{"right_bolt_handle",rack_wrist,rack_contact,rack_fingers};
inline const auto rack_grips=hand_poses::edge_handle::with_right_index(rack_pose);
}
