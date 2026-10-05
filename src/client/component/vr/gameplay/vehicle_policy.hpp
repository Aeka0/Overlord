#pragma once
#include "body_equipment.hpp"
#include "special_equipment_policy.hpp"
#include "weapon_holsters.hpp"
#include "quick_reload.hpp"
#include <string_view>

namespace vr::gameplay::vehicles
{
	using namespace hands;
	enum class kind { none, zodiac, snowmobile };
	inline kind classify(std::string_view map,bool alive,bool linked,bool same_vehicle,std::string_view rig) noexcept
	{
		if(!alive || !linked || !same_vehicle)return kind::none;
		if(map=="af_chase" && rig=="zodiac_player")return kind::zodiac;
		if(map=="cliffhanger" && rig=="snowmobile_player")return kind::snowmobile;
		return kind::none;
	}
	inline const char* weapon_name(kind type) noexcept
	{return type==kind::zodiac?"uzi_zodiac":type==kind::snowmobile?"snowmobile_glock":"";}
	inline const char* receiver_name(kind type) noexcept
	{return type==kind::zodiac?"h2_viewmodel_miniuzi_base":type==kind::snowmobile?"h2_viewmodel_glock_base":"";}
	inline const char* vehicle_model(kind type) noexcept
	{return type==kind::zodiac?"vehicle_zodiac_viewmodel":type==kind::snowmobile?"vehicle_snowmobile_player":"";}
	inline unsigned ammo_field(kind type) noexcept {return type==kind::zodiac?0xB016u:0xBFD3u;}
	inline anchor chest(const head_pose_bridge::spatial_frame& body) noexcept
	{
		const auto slots=equipment::locate_chest(body);if(!slots.valid)return {};
		auto result=slots.anchors[unsigned(equipment::slot::tactical)];
		// Share Exodus orientation; only this slot's position remains on the chest.
		result.rotation=equipment::special::abdominal_display(body,true).rotation;return result;
	}
	inline float chest_distance(const head_pose_bridge::spatial_frame& body,vec point,float margin_meters=0) noexcept
	{
		const auto slots=equipment::locate_chest(body);if(!slots.valid)return INFINITY;
		const auto frame=head_pose_bridge::body_slots_frame(body);const auto u=frame.units_per_meter;
		const body_reach_volume volume{chest(body).position,scale(vec{-.03f,-.13f,-.07f},u),
			scale(vec{.03f,.13f,.07f},u),frame.head_yaw_axis,std::max(slots.radius,margin_meters*u)};
		return volume.distance(point)/volume.radius;
	}
	inline constexpr float weapon_grab_margin=.20f,magazine_grab_margin=.12f,support_grab_margin=.18f;
	inline float weapon_grab_distance(const head_pose_bridge::spatial_frame& body,vec point) noexcept
	{return chest_distance(body,point,weapon_grab_margin);}
	inline float magazine_supply_distance(const head_pose_bridge::spatial_frame& body,const weapons::carry::holsters& waist,vec point) noexcept
	{
		float distance=weapon_grab_distance(body,point);
		if(waist.valid)for(unsigned i=0;i<2;++i)distance=std::min(distance,waist.volumes[i].distance(point)/std::max(waist.radii[i],weapon_grab_margin*body.units_per_meter));
		return distance;
	}
	inline int reload_zone(const head_pose_bridge::spatial_frame& body,const weapons::carry::holsters& waist,vec wrist) noexcept
	{
		if(chest_distance(body,wrist)<=1)return 2;
		const auto hit=weapons::carry::hit(waist,wrist,3);
		return hit==weapons::carry::location::left_waist?0:hit==weapons::carry::location::right_waist?1:-1;
	}
	// Native vehicle targeting constants, measured in the controller muzzle's
	// reference instead of player.angles. Independent of vr_aimAssistStrength.
	inline float aim_score(kind type,vec local,float range) noexcept
	{
		const auto distance=length(local);if(!std::isfinite(distance) || distance<.001f || distance>range)return INFINITY;
		if(type==kind::snowmobile)return local[0]/distance>.94f?1-local[0]/distance:INFINITY;
		constexpr float degrees=57.295779513f;
		const float yaw=std::abs(std::atan2(local[1],local[0])*degrees);
		const float pitch=std::abs(std::atan2(local[2],std::hypot(local[0],local[1]))*degrees);
		return type==kind::zodiac && yaw<20 && pitch<=15?yaw:INFINITY;
	}
}
