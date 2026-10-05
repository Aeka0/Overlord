#pragma once
#include "../../physical_reload_profile.hpp"
namespace vr::gameplay::weapons::miniuzi
{
// Attached tag_clip body bounds and index-pad contact, in native model units.
// h2_viewmodel_miniuzi_base; SHA-256 995818e57c0c2e54a9ca81656a54f983fd8325f9cb54041eb0d389697868ce58.
// Uses the shared 5 cm magazine margin; no latch-strike authority.
inline constexpr magazine_contact_profile contacts{{4.91228419f, 0.46844930f, 0.84048509f},{-0.59620101f, -0.58414603f, -5.76961690f},{1.04649995f, 0.58414603f, 2.77288399f},{}, {}};
}
