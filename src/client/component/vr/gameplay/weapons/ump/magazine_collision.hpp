#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::ump
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 f65f1857e9e152f906e2de28306b4d96ab9b3e64f8c5aa482845fe6ba4dc3cd4
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{0.05073621f, 0.00178308f, -4.88869786f}, {-0.00023912f, -0.00343884f, -0.00399982f, 0.99998606f}}, {1.06709045f, 0.64777265f, 5.83718329f}},
}};
// Exposed receiver release surface; gun-local native units.
inline constexpr hands::vec magazine_latch={6.04366092f, 0.00000000f, 0.82913393f};
}
