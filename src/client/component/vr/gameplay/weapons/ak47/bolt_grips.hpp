#pragma once
#include "../hand_poses/edge_handle.hpp"
namespace vr::gameplay::weapons::ak47
{
// Physical right-side tab, gun-local cm (21.8,-5.9,9.7)..(23,-3,11.2).
inline constexpr hands::vec bolt_grab_low={8.58267717f, -2.32283465f, 3.81889764f}, bolt_grab_high={9.05511811f, -1.18110236f, 4.40944882f};
// The complete shared wrap fit replaces the previous five-millimetre nudge.
inline const auto bolt_grips=hand_poses::edge_handle::at({8.74752512f,-2.26872609f,4.10172315f});
}
