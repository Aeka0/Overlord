#pragma once
#include <array>
#include <string_view>

namespace vr::gameplay::weapons::special_melee
{
	// Exact native identities: these report BULLET/SMG despite having no feed.
	// The bloody alternate is a native link of ending_knife_bloody, not a skin guess.
	inline constexpr std::string_view receiver(std::string_view name) noexcept
	{
		if(name=="ending_knife" || name=="alt_ending_knife")return "viewmodel_commando_knife";
		if(name=="ending_knife_bloody" || name=="alt_ending_knife_bloody")return "viewmodel_commando_knife_bloody";
		if(name=="h2_cheatcommandoknife")return "wpn_h1_melee_rifle_bayonet_vm";
		return {};
	}
	inline constexpr bool supported(std::string_view name) noexcept {return !receiver(name).empty();}
	inline constexpr bool uses_ammunition(int native_type,std::string_view name) noexcept
	{return native_type!=4 && !supported(name) && name!="h2_cheatpickaxe";}
	inline constexpr std::string_view root(std::string_view model) noexcept
	{
		if(model=="viewmodel_commando_knife" || model=="viewmodel_commando_knife_bloody")return "tag_knife";
		return model=="wpn_h1_melee_rifle_bayonet_vm" ? "j_gun" : "";
	}
	inline constexpr int bone_count(std::string_view model) noexcept
	{return root(model).empty() ? 0 : root(model)=="tag_knife" ? 2 : 3;}
	inline constexpr int hidden_bone(std::string_view model) noexcept
	{return model=="wpn_h1_melee_rifle_bayonet_vm" ? 1 : -1;} // Native tag_clip carries the sheath.
	struct blade
	{
		std::array<float,3> base{},tip{}; // Receiver-local native units, no muzzle/world-model dependency.
	};
}
