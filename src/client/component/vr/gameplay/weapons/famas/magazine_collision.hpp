#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::famas
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 991c921bb8913c95b1337b19f695be00078699b840b68e69439d19290034c3e2
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{-0.16159647f, 0.00135944f, -2.95500169f}, {-0.00056015f, 0.00578078f, -0.00004373f, 0.99998313f}}, {1.18294749f, 0.36712468f, 2.86968729f}},
}};
// Exposed receiver release surface; gun-local native units.
inline constexpr hands::vec magazine_latch={-3.19473687f, 0.00000000f, -0.05234700f};
}
