#pragma once
#include "../../physical_reload_profile.hpp"
namespace vr::gameplay::weapons::m4
{
// Attached tag_clip body bounds and index-pad contact, in native model units.
// h2_viewmodel_m4_base; SHA-256 90307e116e650ae82a652c24bac94b8ef9fcec4af19f9b097e82a3c56fd3ff44.
// Uses the shared 5 cm magazine margin; no latch-strike authority.
inline constexpr magazine_contact_profile contacts{{4.68068709f, 1.33658989f, 1.11808471f},{3.50614380f, -0.60800600f, -4.17592808f},{7.05197174f, 0.61434798f, 3.38810371f},{}, {}};
}
