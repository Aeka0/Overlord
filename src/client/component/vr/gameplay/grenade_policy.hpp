#pragma once
#include "body_equipment.hpp"
#include "../settings.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace vr::gameplay::grenades
{
	enum class kind : unsigned { frag, flash, smoke, pomegranate, football, count };
	inline constexpr std::size_t kind_count=unsigned(kind::count);
	struct behavior
	{
		bool pin_gesture{},cook{},timed{};
		const char* chest_model{};
		float chest_diameter_m{};
	};
	inline constexpr std::array<behavior,kind_count> behaviors{{
		{true,true,true},{true,false,true},{true,false,true},
		{true,true,true,"h2_cheat_pomegranate",0},
		{false,false,false,"h2_projectile_cheat_soccer_ball",.10f}
	}};
	inline constexpr bool valid(kind type)noexcept{return unsigned(type)<kind_count;}
	inline kind classify(std::string_view name,std::string_view world_model={})noexcept
	{
		if(name=="fraggrenade")return kind::frag;
		if(name=="flash_grenade")return kind::flash;
		if(name=="h2_cheatpomegrenade")return kind::pomegranate;
		if(name=="h2_cheatfootball")return kind::football;
		if(world_model=="weapon_us_smoke_grenade")return kind::smoke;
		return kind::count;
	}
	inline constexpr float throw_gain_default=settings::grenade_throw_speed.default_value,
		football_gain_default=settings::football_throw_speed.default_value;
	// Only call on a deliberate release, after raw tracking validation. Repeated
	// native spawn attempts keep this already-calibrated vector unchanged.
	inline hands::vec throw_velocity(hands::vec raw,float units,kind type,float gain,float football_gain)noexcept
	{
		if(!valid(type) || !std::isfinite(units) || units<=0 || !std::isfinite(gain) || !std::isfinite(football_gain))return {};
		for(float x:raw)if(!std::isfinite(x) || std::abs(x)>1e7f)return {};
		const float multiplier=std::clamp(gain,settings::grenade_throw_speed.min,settings::grenade_throw_speed.max)*
			(type==kind::football?std::clamp(football_gain,settings::football_throw_speed.min,settings::football_throw_speed.max):1.f);
		const float speed=hands::length(raw);if(speed<=0)return {};
		const float admitted=std::min({speed,units*15.f,9000.f/multiplier});
		return hands::scale(raw,admitted/speed*multiplier);
	}
	inline float stowed_scale(kind type,hands::vec half_extent,float units)noexcept
	{
		if(!valid(type) || !behaviors[unsigned(type)].chest_diameter_m)return 1;
		if(!std::isfinite(units) || units<=0)return 1;
		float diameter{};for(float x:half_extent){if(!std::isfinite(x) || x<=0)return 1;diameter=std::max(diameter,2*x);}
		return std::clamp(behaviors[unsigned(type)].chest_diameter_m*units/diameter,.05f,1.f);
	}
}
