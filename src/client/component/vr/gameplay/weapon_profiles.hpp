#pragma once
#include "weapon_registration.hpp"

namespace vr::gameplay::weapons
{
	profile_match select_profile(std::span<const hands::model_definition> models,
		const hands::rig& rig, std::span<const hands::bone_definition> bones = {}) noexcept;
}
