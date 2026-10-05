#pragma once
#include "weapon_profile.hpp"
#include "physical_reload_geometry.hpp"
#include <cstdint>
#include <optional>

namespace vr::gameplay::weapons
{
	inline constexpr std::uint8_t max_part_grips = 8, no_part_grip = 255;
	namespace part_grip_capture
	{
		inline constexpr float radius_m=.11f;
		// Contact-only assistance: rotate toward the selected authored grasp by
		// at most 60 degrees. Raw wrist translation and physical stroke stay raw.
		inline hands::quat relaxed_rotation(hands::quat raw,hands::quat target) noexcept
		{
			target=hands::normalize(target);
			float cosine{}; for (size_t i=0;i<4;++i) cosine+=raw[i]*target[i];
			if (cosine<0) { for (auto& v:target) v=-v; cosine=-cosine; }
			const float half_angle=std::acos(std::clamp(cosine,0.f,1.f));
			constexpr float slack=.5235987756f; // half of the physical rotation
			if (half_angle<=slack) return target;
			const float denominator=std::sin(half_angle);
			const float a=std::sin(half_angle-slack)/denominator,b=std::sin(slack)/denominator;
			hands::quat out{}; for (size_t i=0;i<4;++i) out[i]=a*raw[i]+b*target[i];
			return hands::normalize(out);
		}
	}
	struct part_grip_fit
	{
		hands::anchor wrist;
		hands::vec contact_in_wrist;
		std::span<const joint_pose> fingers;
	};
	enum class part_palm_facing { any, down, up };
	struct part_grip_pose
	{
		std::string_view name;
		hands::anchor wrist; // Part rest pose in gun-local native units.
		hands::vec contact_in_wrist;
		std::span<const joint_pose> fingers;
		// Explicit real bilateral hardware: the opposite hand reflects the whole
		// grasp about this receiver-local centre, reaching the opposite handle.
		// Null keeps the actual contact fixed for ordinary single-sided parts.
		const hands::vec* symmetry_center{};
		// Complete canonical-left fit for the opposite hand in the same gun frame.
		// Each hand may grasp a different real point on a large handle. Stored by
		// value so a translated profile never retains pointers into a temporary.
		std::optional<part_grip_fit> opposite_pose{};
		unsigned allowed_hands{3}; // Bit 0: left, bit 1: right; selection only, never moves hardware.
		part_palm_facing palm{part_palm_facing::any}; // Raw controller palm; never an animation wrist offset.
		unsigned palm_hands{3}; // Apply facing only to these physical hands.
	};
	struct part_grip_candidate
	{
		std::uint8_t pose{no_part_grip};
		float distance_meters{10};
	};
	struct part_capture_halfspace { hands::vec normal;float maximum; }; // dot(wrist,normal) <= maximum; gun-local native units.
	inline bool finite_part_quat(hands::quat q) noexcept
	{
		float square{};
		for (float v : q) { if (!std::isfinite(v)) return false; square += v*v; }
		return std::isfinite(square) && square > 1e-8f;
	}
	inline bool finite_part_vec(hands::vec v) noexcept
	{
		for (float x : v) if (!std::isfinite(x) || std::abs(x) > 1e7f) return false;
		return true;
	}
	inline bool closer_grasp_facing(hands::quat raw,hands::quat preferred,hands::quat other) noexcept
	{
		if (!finite_part_quat(raw) || !finite_part_quat(preferred) || !finite_part_quat(other)) return false;
		raw=hands::normalize(raw);preferred=hands::normalize(preferred);other=hands::normalize(other);
		float a{},b{};
		for (size_t i=0;i<4;++i) {a+=raw[i]*preferred[i];b+=raw[i]*other[i];}
		// q and -q are equivalent. A tie retains the ordinary action priority.
		return std::abs(a)>std::abs(b)+1e-6f;
	}
	// Compare anatomical wrist orientation in the SAME gun frame. First choose
	// the closest facing, then test its contact: a distant facing cannot
	// win merely because its unrelated contact point happens to touch the gun.
	// The simulation latches this ID only on a valid new press. Do not reselect
	// an already held part as the wrist turns or crosses the angular midpoint.
	inline part_grip_candidate choose_part_grip(std::span<const part_grip_pose> poses,
		hands::anchor raw_wrist, hands::vec part_offset, hands::vec low, hands::vec high,
		float units_per_meter,const part_capture_halfspace* envelope=nullptr,int actor=-1,
		std::optional<float> palm_up=std::nullopt,bool retaining=false) noexcept
	{
		using namespace hands;
		if (poses.empty() || poses.size() > max_part_grips || !finite_part_quat(raw_wrist.rotation) ||
			!finite_part_vec(raw_wrist.position) || !finite_part_vec(part_offset) ||
			!finite_part_vec(low) || !finite_part_vec(high) ||
			!std::isfinite(units_per_meter) || units_per_meter <= 0) return {};
		for (size_t i = 0; i < 3; ++i) if (low[i] > high[i]) return {};
		if(envelope)
		{
			if(!finite_part_vec(envelope->normal) || !std::isfinite(envelope->maximum) || std::abs(dot(envelope->normal,envelope->normal)-1)>.001f)return {};
			const auto wrist=sub(raw_wrist.position,part_offset);
			if(dot(wrist,envelope->normal)>envelope->maximum)return {};
		}
		const auto orientation = normalize(raw_wrist.rotation);
		float best = -1;
		part_grip_candidate result;
		for (size_t i = 0; i < poses.size(); ++i)
		{
			const auto& pose = poses[i];
			if(actor>=0 && actor<2 && !(pose.allowed_hands&(1u<<actor)))continue;
			if(!retaining && actor>=0 && actor<2 && (pose.palm_hands&(1u<<actor)) && pose.palm!=part_palm_facing::any)
			{
				if(!palm_up || !std::isfinite(*palm_up) || std::abs(*palm_up)>1.001f)continue;
				const bool up=*palm_up>.17364818f; // Clear up intent; the ambiguous band uses the down grasp.
				if(up!=(pose.palm==part_palm_facing::up))continue;
			}
			if (!finite_part_quat(pose.wrist.rotation) || !finite_part_vec(pose.wrist.position) ||
				!finite_part_vec(pose.contact_in_wrist)) return {};
			const auto rotation = normalize(pose.wrist.rotation);
			float cosine{};
			for (size_t j = 0; j < 4; ++j) cosine += orientation[j] * rotation[j];
			cosine = std::abs(cosine); // q and -q describe the same orientation.
			if (cosine <= best + 1e-6f) continue; // Stable authored priority at an exact tie.
			best = cosine;
			result.pose = static_cast<std::uint8_t>(i);
		}
		if(result.pose==no_part_grip)return {};
		const auto& selected=poses[result.pose];
		const auto distance=[&](quat rotation) {
			const auto contact=add(raw_wrist.position,rotate(rotation,selected.contact_in_wrist));
			return physical_reload::box_distance(sub(contact,part_offset),low,high)/units_per_meter;
		};
		// Preserve true skin contact, and also accept the bounded assisted pose.
		// Facing selects the style first; another style cannot win on proximity.
		result.distance_meters=std::min(distance(orientation),
			distance(part_grip_capture::relaxed_rotation(orientation,selected.wrist.rotation)));
		return std::isfinite(result.distance_meters) ? result : part_grip_candidate{};
	}
}
