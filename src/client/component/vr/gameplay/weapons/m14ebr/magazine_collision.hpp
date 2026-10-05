#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::m14ebr
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 630604208cd35ec7c7ed24dded5adb02a2336b8fc54ad95768dfd88fa1a6be9b
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{0.08608367f, 0.00266417f, -1.87974028f}, {-0.00121961f, -0.07810620f, 0.00015540f, 0.99694429f}}, {2.00390058f, 0.64190274f, 3.49637566f}},
}};
}
