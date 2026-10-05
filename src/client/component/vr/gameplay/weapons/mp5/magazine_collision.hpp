#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::mp5
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 9d42162e1c5836e64f2beb14001cf8c199c8bdac7c66b64bbe359fe2df9ade4d
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{0.45823064f, 0.00231209f, -2.71149565f}, {0.00005510f, -0.11456850f, -0.01617017f, 0.99328374f}}, {0.90772419f, 0.44780600f, 3.95667449f}},
}};
}
