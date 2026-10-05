#pragma once
#include "../../physical_reload_profile.hpp"
namespace vr::gameplay::weapons::acr
{
// Attached tag_clip body bounds and index-pad contact, in native model units.
// h2_viewmodel_magpul_masada_base; SHA-256 708f880ad6facbacf7f4f4f2499f8cb97aea5b44c39211d2b2331fd4ba84cecc.
// Uses the shared 5 cm magazine margin; no latch-strike authority.
inline constexpr magazine_contact_profile contacts{{6.19291950f, 1.82892256f, 2.47147744f},{4.45224681f, -0.83001495f, -6.69634502f},{9.83932586f, 0.68321101f, 1.54064508f},{}, {}};
}
