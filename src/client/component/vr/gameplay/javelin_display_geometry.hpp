#pragma once
#include "weapon_interaction.hpp"
#include "native_muzzle_contract.hpp"

namespace vr::gameplay::weapons::javelin_screen
{
	// Native javelinhud.lua uses a 1280x720 design canvas and aspect/1.78 widths.
	// The GPU capture's per-eye dimensions are storage, not the art's aspect.
	inline constexpr unsigned canvas_width=1280,canvas_height=720;
	inline constexpr float canvas_aspect=float(canvas_width)/canvas_height;
	inline float optical_half_y(float native_ads_fov) noexcept
	{
		// H2's WeaponDef ADS FOV is horizontal at 4:3. This reproduces the
		// witnessed 25-degree definition -> 0.166271 vertical half tangent.
		return std::isfinite(native_ads_fov) && native_ads_fov>1 && native_ads_fov<179 ?
			std::tan(native_ads_fov*.00872664626f)*.75f : 0.f;
	}
	inline bool near_eyepiece(const muzzle_frame& muzzle,bool active) noexcept
	{
		using namespace hands;
		if(!valid_geometry(muzzle) || !std::isfinite(muzzle.units_per_meter) || muzzle.units_per_meter<=0 || muzzle.units_per_meter>10000)return false;
		const auto* contract=reviewed_muzzle("h2_viewmodel_javelin_base");
		if(!contract)return false;
		const auto transform=[&](vec v) {
			v=rotate(conjugate(normalize(contract->muzzle.rotation)),v);vec world{};
			for(unsigned i=0;i<3;++i)world=add(world,scale(muzzle.axis[i],v[i]));return world;
		};
		// Measured actual rear CLU aperture, not the muzzle or distant tag_view.
		const auto center=add(muzzle.position,transform(sub(vec{-.052678456f*39.37007874f,.1841611f*39.37007874f,.0056947f*39.37007874f},contract->muzzle.position)));
		const auto delta=scale(sub(muzzle.head_position,center),1.f/muzzle.units_per_meter);
		for(float v:delta)if(!std::isfinite(v))return false;
		const auto forward=transform({1,0,0});
		const float distance=length(delta),behind=-dot(delta,forward),facing=dot(muzzle.head_forward,forward);
		// Near-eye presence is mandatory. A wider exit band prevents boundary
		// chatter; neither this test nor its caller rotates/translates the gun.
		return std::isfinite(facing) && distance<=(active?.20f:.14f) && behind>=.005f && facing>=(active?.6427876f:.8191520f);
	}
}
