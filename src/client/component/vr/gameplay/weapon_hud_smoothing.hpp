#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace vr::gameplay::weapon_hud
{
	// Presentation only: never feeds back into the gun, hands or firing pose.
	class anchor_smoother
	{
	public:
		using vec3 = std::array<float, 3>;
		struct identity
		{
			std::uint64_t weapon{}, rear_revision{}, reference{}, instance_generation{};
			bool operator==(const identity&) const = default;
		};
		void reset() noexcept { ready_ = false; }
		[[nodiscard]] vec3 apply(const vec3& anchor, const vec3& model_center,
			const std::array<float, 12>& camera, float units, double scene_seconds,
			identity owner, bool enabled) noexcept
		{
			if (!enabled || !std::isfinite(scene_seconds) || !std::isfinite(units) || units <= 0)
			{ reset(); return anchor; }
			vec3 target{};
			for (unsigned axis = 0; axis < 3; ++axis)
			{
				for (unsigned c = 0; c < 3; ++c)
					target[axis] += (anchor[c] - model_center[c]) / units * camera[3 + axis*3 + c];
				if (!std::isfinite(target[axis])) { reset(); return anchor; }
			}
			const double dt = scene_seconds - seconds_;
			float distance_squared{};
			for (unsigned c = 0; c < 3; ++c)
				distance_squared += (target[c]-filtered_[c]) * (target[c]-filtered_[c]);
			// Use scene time, once per stereo pair. No interpolation across tracking
			// loss, pause/hitch, equip/recenter, a backward clock or a large pose jump.
			if (!ready_ || !(owner == owner_) || dt <= 0 || dt > .25 || distance_squared > .25f)
			{
				filtered_ = target; seconds_ = scene_seconds; owner_ = owner; ready_ = true;
				return anchor;
			}
			const float alpha = static_cast<float>(-std::expm1(-std::log(2.0) * dt / .025));
			for (unsigned c = 0; c < 3; ++c) filtered_[c] += (target[c] - filtered_[c]) * alpha;
			seconds_ = scene_seconds;
			vec3 result = model_center;
			for (unsigned c = 0; c < 3; ++c)
				for (unsigned axis = 0; axis < 3; ++axis)
					result[c] += units * filtered_[axis] * camera[3 + axis*3 + c];
			return result;
		}
	private:
		vec3 filtered_{};
		identity owner_{};
		double seconds_{};
		bool ready_{};
	};
}
