#pragma once
#include "../../physical_reload_profile.hpp"
namespace vr::gameplay::weapons::m93r
{
// Attached tag_clip body bounds and index-pad contact, in native model units.
// h2_viewmodel_beretta_393_base; SHA-256 5b45c88a2a6826a27aeb4d08dc656cb931fb10023b83baa2fb68a01a7d4507c5.
// Uses the shared 5 cm magazine margin; no latch-strike authority.
inline constexpr magazine_contact_profile contacts{{4.50563841f, 0.67095583f, 1.37896563f},{-1.56444793f, -0.50399501f, -4.36081751f},{1.11891778f, 0.64294601f, 0.88509682f},{}, {}};
}
