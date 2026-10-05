#pragma once
#include "weapon_profile.hpp"
#include "ammunition_transfer.hpp"
#include "weapon_sound_reference.hpp"

namespace vr::gameplay::weapons
{
	enum class launcher_loading { rocket, disposable, native_reload };
	struct launcher_profile
	{
		std::string_view id,native_name;
		launcher_loading loading{launcher_loading::rocket};bool guided{};
		std::span<const std::string_view> aliases{};
		std::string_view rocket_model{};
		hands::anchor rocket_rest{},rocket_in_wrist{};
		std::span<const joint_pose> rocket_fingers{};
		hands::vec tail_start{},tail_end{},load_mouth{}; // Model-local tail segment and receiver-local tube entrance.
		sound_reference load_sound{};
		sound_reference support_grab_sound{},support_release_sound{};
		float sight_lateral_meters{}; // Coarse ADS intent at an offset sight, never a ballistic muzzle offset.
		bool manual_loading()const noexcept{return loading==launcher_loading::rocket;}
		bool retain_empty()const noexcept{return loading!=launcher_loading::rocket;}
		bool blocks_native_reload()const noexcept{return loading!=launcher_loading::native_reload;}
		bool matches(std::string_view name)const noexcept
		{
			if(name==native_name)return true;
			for(auto alias:aliases)if(name==alias)return true;
			return false;
		}
	};
}
