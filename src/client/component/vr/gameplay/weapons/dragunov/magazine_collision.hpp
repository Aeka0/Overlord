#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::dragunov
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 e50169fa08487e5ee5ab9be58e5ca8e47203ffecfd79da4a4d81bf6388e9d32d
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{-0.26272236f, -0.01501200f, -2.53181440f}, {-0.00000000f, 0.12250444f, -0.00000000f, 0.99246796f}}, {2.00568331f, 0.65816350f, 2.43826153f}},
}};
}
