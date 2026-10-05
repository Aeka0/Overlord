#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::aug
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 2a88b8e7af7b04370ecddf182af06835a5944d6af226b04d149e82735de86bde
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{0.56250905f, 0.02184228f, -2.68400114f}, {0.00000001f, -0.09605646f, 0.00000014f, 0.99537589f}}, {1.73858407f, 0.78481697f, 4.16783027f}},
}};
// Exposed receiver release surface; gun-local native units.
inline constexpr hands::vec magazine_latch={-8.56522237f, 0.00000000f, -0.02254800f};
}
