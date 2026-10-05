#pragma once
#include "../../knife_slide_pose.hpp"
#include "../../part_grip_pose.hpp"
namespace vr::gameplay::weapons::m93r
{
// Shared native USP knife co-grasps, fitted to the M93R magazine and slide.
// Reload frame 15; tactical_pullout_first frame 17; existing shared source hashes apply.
inline constexpr hands::anchor knife_magazine_in_wrist={{4.06906601f, 0.98432279f, 5.76185964f}, {-0.04685785f, -0.10364436f, -0.11036413f, 0.98736110f}};
inline const std::array<part_grip_pose,1> knife_slide_grips{{
{"knife_slide",{{-2.88882876f, -0.03389927f, 5.12665569f}, {0.99154615f, 0.11971305f, -0.00919804f, -0.04919769f}},{3.46645652f, 1.69119345f, 2.06854496f},equipment::knife_slide_pose::fingers}
}};
}
