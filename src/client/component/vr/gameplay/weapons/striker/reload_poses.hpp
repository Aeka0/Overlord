#pragma once
#include "../../tube_profile.hpp"
#include "../hand_poses/shell_grasp.hpp"
namespace vr::gameplay::weapons::striker
{
// Original insertion: reload_intro frame 20 SHA256 d7a8bd053a2aaeeb90b7f344a768fce0f33c465bc90c6b9f712221c426bc6bdc
inline constexpr auto shell_in_wrist=hand_poses::shell_grasp::shell_in_wrist;
inline constexpr auto& shell_fingers=hand_poses::shell_grasp::shell_fingers;
inline constexpr hands::anchor cover_rest={{3.88760304f, -1.88665897f, 2.62106291f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor cover_open={{3.88764596f, -1.88667125f, 2.62085120f}, {-0.21100655f, 0.00000000f, 0.00000000f, 0.97748465f}};
inline constexpr hands::anchor drum_rest={{5.42068557f, -0.01145200f, 1.11582101f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec shell_center={-0.36737115f, -0.01268014f, -0.01186094f};
inline constexpr hands::vec port_center={2.37613156f, -1.43694106f, 2.51377886f};
inline constexpr hands::vec port_forward={0.96087445f, 0.24652077f, 0.12628457f};
}
