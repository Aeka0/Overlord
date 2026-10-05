#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::fal
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 7dc28d5381843b0dfe3a643bd7b60a222fcc5720d215942bd4c1073bcf990144
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{1.48060325f, 0.01546306f, -3.66152115f}, {-0.00009341f, -0.04948738f, 0.00622408f, 0.99875535f}}, {1.94837762f, 0.80262318f, 4.83003329f}},
}};
}
