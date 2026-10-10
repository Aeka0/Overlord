#pragma once
#include "mounted_turret_policy.hpp"
#include "../engine_stereo_view.hpp"

namespace vr::gameplay::mounted
{
	struct laser_ray
	{
		hands::vec origin{},forward{};
		std::array<float,12> camera{};
		std::uint64_t instance{},reference{};
		controller_input::clock::time_point at{};
		float units{};
		int entity{-1};
	};
	// Final frontend camera only: release the DObj lock before collision queries.
	bool prepare_laser(laser_ray& output) noexcept;
	// Cached ownership/input only; safe for the serialized eye composer.
	bool laser_current(std::uint64_t instance,std::uint64_t reference) noexcept;
	inline bool laser_matches_scene(const laser_ray& ray,const engine_stereo_view::slot_pair& views,
		controller_input::clock::time_point now) noexcept
	{
		return ray.instance && ray.reference && ray.entity>0 && ray.entity<4000 &&
			std::isfinite(ray.units) && ray.units>0 && finite(ray.origin) &&
			finite(ray.forward) && std::abs(hands::length(ray.forward)-1.f)<.001f &&
			ray.at!=controller_input::clock::time_point{} && ray.at<=views.camera_sampled_at &&
			now>=views.camera_sampled_at && now-ray.at<=std::chrono::milliseconds(150) &&
			ray.camera==views.natural_camera;
	}
}
