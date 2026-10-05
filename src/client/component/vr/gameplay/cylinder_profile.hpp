#pragma once
#include "cylinder_gesture.hpp"
#include "weapon_profile.hpp"
#include "weapon_sound_reference.hpp"

namespace vr::gameplay::weapons
{
	struct cylinder_asset_recipe
	{
		std::string_view receiver,loader,case_body,case_tip;
		unsigned receiver_bones{};
	};
	struct cylinder_profile
	{
		std::string_view id, native_name;
		cylinder::rules ammunition;
		cylinder::tuning interaction;
		hands::anchor swing_closed, swing_open, ammo_in_cylinder, loader_in_wrist;
		hands::anchor face_in_cylinder;
		hands::vec loader_tip{};
		std::array<hands::anchor,6> loader_rounds;
		std::span<const joint_pose> loader_fingers;
		sound_reference (*sound_key)(cylinder::effect){};
		std::span<const std::string_view> native_variants{};
		cylinder_asset_recipe assets{};
		bool matches_native(std::string_view name, int capacity) const noexcept
		{
			if (capacity!=ammunition.capacity) return false;
			if (name==native_name) return true;
			return std::find(native_variants.begin(),native_variants.end(),name)!=native_variants.end();
		}
	};
}
