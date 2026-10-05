#pragma once
#include "weapon_registration.hpp"

namespace vr::gameplay::weapons
{
	// One immutable catalog. Concrete asset recipes are compiled only by its
	// implementation; these indices never serve as persistent identities.
	extern const std::span<const profile_registration> registered_profiles;
}
