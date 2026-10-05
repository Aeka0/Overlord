#pragma once
#include "weapon_carry.hpp"
#include "shoulder_anchors.hpp"
#include "body_reach_volume.hpp"

namespace vr::gameplay::weapons::carry
{
	struct holster_layout
	{
		float waist_width{.23f},waist_down{.60f},back_distance{.22f},back_down{.24f};
		float waist_radius{.16f},back_radius{.20f};
	};
	struct holsters {bool valid{};std::array<hands::vec,3> centers{};std::array<float,3> radii{};std::array<body_reach_volume,3> volumes{};};
	inline hands::quat holster_rotation(const std::array<hands::vec,3>& body_axis,location slot) noexcept
	{
		using namespace hands;
		const auto forward=body_axis[0],up=body_axis[2];
		if (slot==location::back) return from_axis({up,body_axis[1],scale(forward,-1)});
		// Gun +X is the barrel, +Z is the slide/top. Point the muzzle down,
		// keep the slide forward, and cant the muzzle eight degrees inward.
		const auto outside=scale(body_axis[1],slot==location::left_waist ? 1.f : -1.f);
		const auto barrel=sub(scale(up,-.99026807f),scale(outside,.13917310f));
		return from_axis({barrel,cross(forward,barrel),forward});
	}
	inline holsters locate_holsters(const head_pose_bridge::spatial_frame& frame,const holster_layout& layout={}) noexcept
	{
		const auto body=head_pose_bridge::body_slots_frame(frame);
		using namespace hands;
		holsters out;std::array<vec,2> validation{};
		if (!make_shoulders(body,{}, {},validation)) return out;
		for (auto x:{layout.waist_width,layout.waist_down,layout.back_distance,layout.back_down,layout.waist_radius,layout.back_radius})
			if (!std::isfinite(x) || x<=0 || x>1.2f) return out;
		const auto waist=sub(body.head_position,vec{0,0,layout.waist_down*body.units_per_meter});
		const auto side=scale(body.head_yaw_axis[1],layout.waist_width*body.units_per_meter);
		out.centers={add(waist,side),sub(waist,side),sub(body.head_position,
			scale(add(scale(body.head_yaw_axis[0],layout.back_distance),vec{0,0,layout.back_down}),body.units_per_meter))};
		out.radii={layout.waist_radius*body.units_per_meter,layout.waist_radius*body.units_per_meter,layout.back_radius*body.units_per_meter};
		for (unsigned h=0;h<2;++h) out.volumes[h]=waist_reach(out.centers[h],body.head_yaw_axis,body.units_per_meter,h,0,layout.waist_radius);
		const auto u=body.units_per_meter;
		out.volumes[2]={out.centers[2],{-.16f*u,-.30f*u,-.30f*u},{0,.30f*u,.12f*u},body.head_yaw_axis,layout.back_radius*u};
		out.valid=true;return out;
	}
	inline location hit(const holsters& slots,const hands::vec& point,unsigned eligible=7) noexcept
	{
		if (!slots.valid) return location::absent;
		constexpr std::array places{location::left_waist,location::right_waist,location::back};
		float nearest=INFINITY;location selected=location::absent;
		for (std::size_t i=0;i<3;++i)
		{
			if (!(eligible&(1u<<i))) continue;
			if (!slots.volumes[i].contains(point)) continue;
			// Overlapping generous volumes resolve by their original anchor, not
			// iteration order or the first box whose interior has zero distance.
			const auto distance=hands::length(hands::sub(point,slots.centers[i]))/slots.radii[i];
			if (std::isfinite(distance) && distance<nearest) {nearest=distance;selected=places[i];}
		}
		return selected;
	}
}
