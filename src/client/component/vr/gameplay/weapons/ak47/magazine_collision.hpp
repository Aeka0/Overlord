#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::ak47
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 a28488bfea1a241a196707a2d10e5ec4bffc4e276f68b15d7b4a265a27ec126e
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{1.28998715f, -0.00689921f, -3.28010667f}, {-0.00116451f, -0.25641339f, 0.00092706f, 0.96656606f}}, {2.00014024f, 0.80759762f, 5.06594041f}},
}};
}
