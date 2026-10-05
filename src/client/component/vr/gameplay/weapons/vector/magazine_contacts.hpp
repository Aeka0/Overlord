#pragma once
#include "../../physical_reload_profile.hpp"
namespace vr::gameplay::weapons::vector
{
// Attached tag_clip body bounds and index-pad contact, in native model units.
// h2_viewmodel_kriss_super_v_base; SHA-256 b7801e00d2aec40edb6f9e5ef7bc2a9e4d263ab149167cc8e5c0a2b9321ece7b.
// Uses the shared 5 cm magazine margin; no latch-strike authority.
inline constexpr magazine_contact_profile contacts{{4.63135721f, 0.02778028f, 3.02308372f},{3.70163926f, -0.44906699f, -7.37433432f},{8.62203292f, 0.44906697f, 1.09208213f},{}, {}};
}
