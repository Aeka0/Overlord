#pragma once
#include "weapon_holding.hpp"
#include "hand_pose_solver.hpp"
#include "arm_geometry.hpp"
#include "../controller_input.hpp"
#include "weapon_ejection.hpp"
namespace vr::gameplay::weapons {struct profile;}

namespace vr::gameplay::weapons::carry
{
	struct scene
	{
		hold owner{};
		hands::anchor gun{};
		std::array<hands::anchor,2> wrists{}; // authored gun-local contacts
		hand support_candidate{hand::none};
		std::uint64_t sequence{},reference{};
		controller_input::clock::time_point at{};
		std::array<hands::anchor,2> supports{};
		hands::anchor muzzle{}; // viewmodel muzzle in gun coordinates; world models have different origins
		bool has_muzzle{};
		const profile* authored{}; // Immutable profile, never the stack-owned mirror adapter.
		bool independent{}; // Pose belongs to this instance's own skinned object.
		ejection_port ejection{}; // Receiver marker reference; physical-window calibration is separate.
		std::uint64_t assembly{}; // Local contacts remain valid until this rig identity changes.
		std::array<hands::anchor,2> moving_controls{};bool has_moving_control{};
		bool support_available{true}; // Whole-weapon manipulation can temporarily exclude acquisition.
		std::array<std::array<float,2>,2> arm_lengths{}; // Native upper/lower lengths for optional forearm mounts.
		std::array<float,3> shoulder_dimensions{}; // Same half-width/down/back settings as the source solve.
		hands::arm_geometry firing_arm{};
		hands::quat support_controller{0,0,0,1};bool has_support_controller{};
		std::array<hands::quat,2> control_rotations{{{0,0,0,1},{0,0,0,1}}}; // Value-owned rig calibration, never a stack-profile pointer.
	};
}
