#pragma once
#include "hand_contact.hpp"
#include "swept_box_contact.hpp"
#include <optional>

namespace vr::gameplay::weapons::physical_reload
{
	// A bounded solid palm, from the wrist heel through the four knuckle roots.
	// Dimensions come from the admitted glove, independently of distal finger curl.
	inline constexpr std::array<size_t,3> palm_grid{6,6,4};
	inline constexpr size_t palm_contact_count=palm_grid[0]*palm_grid[1]*palm_grid[2];
	inline constexpr hands::vec maximum_palm_size_m{.16f,.14f,.06f};
	inline constexpr float palm_grid_cover_m=.024f; // Farthest point in a maximum-size grid cell is <24 mm away.
	inline std::optional<contact_box> palm_volume(const std::array<hands::vec,hands::hand_contact_count>& wrist_local,float units) noexcept
	{
		using namespace hands;
		if(!std::isfinite(units) || units<=0)return {};
		std::array<vec,6> roots{}; // Wrist plus thumb/index/middle/ring/pinky roots.
		for(size_t i=0;i<5;++i)
		{
			roots[i+1]=scale(wrist_local[3*i],1/units);
			for(float x:roots[i+1])if(!std::isfinite(x) || std::abs(x)>.25f)return {};
		}
		const auto forward=scale(add(add(roots[2],roots[3]),add(roots[4],roots[5])),.25f);
		if(length(forward)<.025f)return {};
		const auto x=unit(forward),across=sub(roots[2],roots[5]);
		const auto lateral=sub(across,scale(x,dot(across,x)));
		if(length(lateral)<.025f)return {};
		const auto y=unit(lateral),z=unit(cross(x,y));const std::array<vec,3> axes{x,y,z};
		vec low{},high{};
		for(const auto root:roots)for(size_t axis=0;axis<3;++axis)
		{
			const auto value=dot(root,axes[axis]);low[axis]=std::min(low[axis],value);high[axis]=std::max(high[axis],value);
		}
		// Skin around skeletal roots, including the thenar/hypothenar pads and
		// knuckle row. Only 8 mm behind the wrist; this is not a forearm contact.
		constexpr vec skin_m{.008f,.010f,.012f};
		contact_box result{{{},from_axis(axes)},{}};
		for(size_t axis=0;axis<3;++axis)
		{
			low[axis]-=skin_m[axis];high[axis]+=skin_m[axis];
			if(high[axis]-low[axis]>maximum_palm_size_m[axis])return {};
			result.pose.position=add(result.pose.position,scale(axes[axis],(low[axis]+high[axis])*.5f*units));
			result.half[axis]=(high[axis]-low[axis])*.5f*units;
		}
		return valid(result)?std::optional<contact_box>{result}:std::nullopt;
	}
	inline bool valid_palm_meters(const contact_box& palm) noexcept
	{
		if(!valid(palm))return false;
		for(size_t i=0;i<3;++i)if(palm.half[i]*2>maximum_palm_size_m[i]+.00001f)return false;
		return true;
	}
	inline contact_box palm_relative_to_target(const contact_box& local,hands::anchor wrist,hands::vec target,float units) noexcept
	{
		return {{hands::scale(hands::sub(box_point(wrist,local.pose.position),target),1/units),
			hands::normalize(hands::multiply(wrist.rotation,local.pose.rotation))},hands::scale(local.half,1/units)};
	}
	template<class Store> void sample_palm(const contact_box& palm,Store&& store) noexcept
	{
		size_t index{};
		for(size_t x=0;x<palm_grid[0];++x)for(size_t y=0;y<palm_grid[1];++y)for(size_t z=0;z<palm_grid[2];++z)
		{
			const hands::vec local{palm.half[0]*(2.f*x/(palm_grid[0]-1)-1),palm.half[1]*(2.f*y/(palm_grid[1]-1)-1),palm.half[2]*(2.f*z/(palm_grid[2]-1)-1)};
			store(index++,box_point(palm.pose,local));
		}
	}
}
