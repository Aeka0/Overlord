#pragma once
#include "../../physical_reload_profile.hpp"
namespace vr::gameplay::weapons::tmp
{
// Attached tag_clip body bounds and index-pad contact, in native model units.
// h2_viewmodel_mp9_base; SHA-256 19f45105a81c07afec24ed3508a30ac1a957d7a40a43991d8e0990d45a761785.
// Uses the shared 5 cm magazine margin; no latch-strike authority.
inline constexpr magazine_contact_profile contacts{{3.91324397f, 1.90892089f, 1.66902846f},{-0.86707900f, -0.52932102f, -6.25446383f},{0.82165397f, 0.52247798f, 2.88362601f},{}, {}};
}
