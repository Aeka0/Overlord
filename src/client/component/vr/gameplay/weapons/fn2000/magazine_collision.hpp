#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::fn2000
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 35af0bd35ce62c5f23747d7e3da55b251c7840371f34beed8b6777978ce0d999
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{-0.30864424f, 0.01725470f, -1.58209906f}, {-0.00291352f, -0.07014879f, -0.00553510f, 0.99751693f}}, {1.32682275f, 0.44106515f, 3.26132271f}},
}};
// Exposed receiver release surface; gun-local native units.
inline constexpr hands::vec magazine_latch={-2.59535913f, 0.00000000f, -1.53071796f};
}
