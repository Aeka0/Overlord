#pragma once
#include "../../physical_reload_profile.hpp"
namespace vr::gameplay::weapons::g18
{
// Attached tag_clip body bounds and index-pad contact, in native model units.
// h2_viewmodel_glock_base; SHA-256 2ed3651182a36b5c17a27d15b4080f0ea2dfdf149c2b0a9b1e06348fb02bad4f.
// Uses the shared 5 cm magazine margin; no latch-strike authority.
inline constexpr magazine_contact_profile contacts{{3.49037723f, 0.77395955f, 1.55656991f},{-2.95789173f, -0.59730199f, -6.27181482f},{1.94455858f, 0.59731100f, 2.65500120f},{}, {}};
}
