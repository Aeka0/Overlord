#pragma once
#include "../../physical_reload_profile.hpp"
namespace vr::gameplay::weapons::pp2000
{
// Attached tag_clip body bounds and index-pad contact, in native model units.
// h2_viewmodel_p2000_base; SHA-256 769aefc737b8671dc1a281672b59849b666613440ad2738670c5bad179c385cb.
// Uses the shared 5 cm magazine margin; no latch-strike authority.
inline constexpr magazine_contact_profile contacts{{3.58920814f, 0.24132618f, 0.96074088f},{-0.73395400f, -0.61089495f, -3.93037372f},{1.63308807f, 0.61089401f, 1.98482596f},{}, {}};
}
