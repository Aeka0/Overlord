#pragma once
#include "../../swept_box_contact.hpp"
namespace vr::gameplay::weapons::cheytac
{
// Conservative oriented box for the entire magazine body, including
// permanent body components and excluding ammunition; magazine-local native units.
// All body vertices are enclosed, with 0.05 mm numerical authoring padding.
// Source SHA256 7895595603031f94d318cfb8b789bc1f4c10a5e4543885efe1862e738fdaf035
inline constexpr std::array<physical_reload::contact_box,1> strike_regions{{
	{{{0.37898632f, 0.00000042f, -1.62119267f}, {-0.00000055f, 0.00397770f, 0.00000062f, 0.99999209f}}, {2.60867145f, 0.58777470f, 3.17227250f}},
}};
}
