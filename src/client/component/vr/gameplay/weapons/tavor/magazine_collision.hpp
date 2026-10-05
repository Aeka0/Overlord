#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::tavor
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 0c1603120beb517c31ec33de82a6d6315ae827963c16ad276151acc0c6e604a2
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{1.49955028f, 0.00490306f, -3.06964207f}, {0.00068694f, -0.15192958f, 0.00135039f, 0.98839016f}}, {1.56255878f, 0.54208776f, 3.79955255f}},
}};
// Rear release button and forward paddle are different hardware. Coordinates
// are receiver-local native units, measured on the same exported mesh above.
inline constexpr hands::vec magazine_latch={-9.87877508f, 0.00000000f, 0.59137302f};
}
