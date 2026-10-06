#pragma once
#include "controller_input.hpp"
#include "pose_filter.hpp"
#include <array>
#include <string>

namespace vr::controller_pose_reference
{
	enum class basis
	{
		runtime_grip,
		calibration_frame
	};
	struct hand_reference
	{
		// Right-multiply the SDK's grip pose by grip_from_calibration. This maps the frame in
		// which the existing wrist lever is expressed, including its orientation.
		// Aim remains the SDK's pointing/fire witness.
		pose_filter::pose grip_from_calibration;
		// A selected model name or canonical calibration reference identifier.
		std::string reference_id;
		bool ready{};
	};
	struct configuration
	{
		basis target{basis::runtime_grip};
		std::array<hand_reference, 2> hands;
		std::string error;
		std::string expected_runtime;
		std::string name{"openxr_grip"};
		// Optional profile contract, checked after action sync and on profile events.
		std::string required_profile;
	};
	inline bool valid_component(const pose_filter::pose& value) noexcept
	{
		if (!pose_filter::valid(value) || pose_filter::length(value.position) > 1.f)
			return false;
		const auto unit = pose_filter::multiply(value.orientation, pose_filter::transpose(value.orientation));
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned column = 0; column < 3; ++column)
				if (std::abs(unit[row][column] - (row == column ? 1.f : 0.f)) > .001f)
					return false;
		return true;
	}
	inline bool normalize_grip(controller_input::hand_pose& source,
	                           const configuration& reference,
	                           unsigned hand) noexcept
	{
		if (reference.target == basis::runtime_grip)
			return source.valid;
		if (hand >= reference.hands.size() || !source.valid || !reference.hands[hand].ready ||
		    !valid_component(reference.hands[hand].grip_from_calibration))
		{
			source.valid = false;
			return false;
		}
		const auto converted =
		    pose_filter::compose({source.tracking.position_meters, source.tracking.orientation},
		                         reference.hands[hand].grip_from_calibration);
		if (!pose_filter::valid(converted))
		{
			source.valid = false;
			return false;
		}
		source.tracking = {converted.position, converted.orientation};
		return true;
	}
}
