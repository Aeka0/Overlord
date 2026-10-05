#pragma once
#include "../controller_input.hpp"
#include "hand_pose_solver.hpp"
#include "weapon_holding.hpp"
#include "weapon_model_anchor.hpp"
#include "weapon_fire_delivery.hpp"
#include "optic_view.hpp"
#include <string_view>

namespace vr::gameplay::weapons
{
	struct muzzle_frame
	{
		bool valid{};
		hold owner{};
		std::uint64_t reference_generation{}, input_sequence{};
		controller_input::clock::time_point sampled_at{}, camera_at{};
		hands::vec position{};
		std::array<hands::vec, 3> axis{}; // forward, left, up; native shot wants right = -left
		std::string_view profile_id{}; // Static authored identity, empty for generic hands.
		float units_per_meter{}; // Same spatial sample as the solved bones; no ballistic scaling.
		model_anchor model{}; // Exact solved bone + solve origin, separate from firing position.
		bool firing_capable{true}; // Non-firing weapons retain a skin/attachment frame only.
		hands::vec head_position{}, head_forward{}; // Same camera sample as this solved muzzle.
		optics::view optic{};
		hands::vec ads_translation{}; // Comfort pose offset; remove only for raised-weapon intent.
		model_anchor laser{}; // Optional solved receiver tag_laser, not the muzzle or camera origin.
		std::array<hands::vec,3> laser_axis{};
		float ads_sight_to_muzzle_meters{}; // Admitted rear optic to muzzle, from the same unassisted solve; 0 without an optic.
	};
	inline bool valid_geometry(const muzzle_frame& value) noexcept
	{
		for (auto x : value.position)
			if (!std::isfinite(x) || std::abs(x) > 1e7f)
				return false;
		for (const auto& row : value.axis)
		{
			for (auto x : row)
				if (!std::isfinite(x))
					return false;
			if (std::abs(hands::dot(row, row) - 1) > 0.002f)
				return false;
		}
		return std::abs(hands::dot(value.axis[0], value.axis[1])) <= 0.002f &&
			   hands::length(hands::sub(hands::cross(value.axis[0], value.axis[1]), value.axis[2])) <= 0.002f;
	}
	inline bool laser_pose(const muzzle_frame& source,muzzle_frame& output)noexcept
	{
		if(!source.valid || !valid_model_anchor(source.laser))return false;
		auto result=source;result.model=source.laser;result.axis=source.laser_axis;
		result.position=hands::add(source.laser.position,source.laser.solve_origin);
		if(!valid_geometry(result))return false;
		output=result;return true;
	}
	// A supported/carry-only gun still owns a render/contact pose. Firing adds
	// the separate control-grip requirement below; attachments must not use it.
	inline bool pose_ready(const muzzle_frame& value, const hold& owner, std::uint64_t reference,
					  controller_input::clock::time_point now) noexcept
	{
		return value.valid && value.input_sequence && valid_geometry(value) && valid_hand(owner.holding_hand()) &&
			   value.owner.id() == owner.id() && value.owner.revision == owner.revision &&
			   value.owner.rear == owner.rear && value.owner.support == owner.support &&
			   value.owner.rear_revision == owner.rear_revision && value.reference_generation == reference &&
			   now >= value.sampled_at && now >= value.camera_at &&
			   now - value.sampled_at <= std::chrono::milliseconds(150) &&
			   now - value.camera_at <= std::chrono::milliseconds(150);
	}
	inline bool ready(const muzzle_frame& value, const hold& owner, std::uint64_t reference,
		controller_input::clock::time_point now) noexcept
	{return value.firing_capable && owner.can_fire() && pose_ready(value,owner,reference,now);}
	// All locks are short copies; none are held while calling the engine or runtime.
	hold sample_native_equipped() noexcept;
	hold current_hold() noexcept;
	bool commit_grip(const hold& expected, grip_role role, hand owner) noexcept;
	bool publish_muzzle(const hold& expected, hand support, muzzle_frame value, hold& committed) noexcept;
	void invalidate_muzzle() noexcept;
	muzzle_frame current_muzzle() noexcept;
	bool firing_enabled() noexcept;
	fire_delivery controller_fire_delivery(std::uint32_t weapon, bool alternate=false) noexcept;
	bool firing_ready(const hold& owner, std::uint64_t reference,
					  controller_input::clock::time_point now) noexcept;
} // namespace vr::gameplay::weapons
