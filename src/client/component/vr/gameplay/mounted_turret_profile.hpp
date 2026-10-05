#pragma once
#include <array>
#include <span>
#include <string_view>

namespace vr::gameplay::mounted
{
	enum class mount_kind { none,suburban,blackhawk };
	struct mount_profile
	{
		mount_kind kind{};
		std::string_view weapon;
		std::array<std::string_view,3> models;
		unsigned model_count{},hand_model{};
		std::string_view root,gun,seat,pitch,yaw,left_button,right_button,hidden_material;
	};
	inline constexpr mount_profile suburban{
		mount_kind::suburban,"minigun_laatpv_player",
		{"weapon_suburban_minigun_viewmodel","viewhands_player_us_army",{}},2,1,
		"tag_cover","j_mg","tag_player","tag_aim","tag_aim_pivot","j_button_l","j_button_r","m/mtl_h2_minigun_suburban_shield"};
	inline constexpr mount_profile blackhawk{
		mount_kind::blackhawk,"weapon_blackhawk_minigun",
		{"h2_vehicle_blackhawk_minigun_hero_exterior","h2_vehicle_blackhawk_minigun_hero_interior_low","viewhands_player_us_army"},3,2,
		"tag_turret_base","tag_barrel","tag_player","tag_barrel","tag_turret",{},{},{}};
	inline constexpr float blackhawk_eye_clearance_meters=.35f;
	inline constexpr std::array profiles{&suburban,&blackhawk};
	inline bool attached(unsigned flags,mount_kind kind)noexcept
	{return kind==mount_kind::suburban?(flags&0x3000)!=0:kind==mount_kind::blackhawk && (flags&0x100000)!=0;}
	inline bool matches(const mount_profile& profile,std::span<const std::string_view> names)noexcept
	{
		if(names.size()!=profile.model_count)return false;
		for(unsigned i=0;i<names.size();++i)if(names[i]!=profile.models[i])return false;
		return true;
	}
}
