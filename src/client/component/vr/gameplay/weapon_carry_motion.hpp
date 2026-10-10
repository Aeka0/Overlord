#pragma once
#include "../controller_input.hpp"
#include "hands/pose_math.hpp"
#include <algorithm>

namespace vr::gameplay::weapons::carry
{
	// A missing hand never becomes a world-origin sample. This history supplies
	// only drop velocity; it cannot cancel a proven grip release or change ownership.
	class hand_motion_history
	{
		struct sample
		{
			hands::vec position{};
			controller_input::clock::time_point at{};
			std::uint64_t sequence{};
			bool valid{};
		};
		std::array<sample, 2> previous_{};
		std::uint64_t reference_{}, pose_reference_{}, continuity_{};
		controller_pose_pipeline::mode pipeline_{};
		float units_{};
	public:
		void reset() noexcept { *this = {}; }
		std::array<hands::vec, 2> update(const controller_input::frame& input,
			const std::array<head_pose_bridge::world_pose, 2>& poses, unsigned valid_hands,
			float units) noexcept
		{
			std::array<hands::vec, 2> velocity{};
			if (!input.focused || input.orientation_settling || !input.sequence ||
				!std::isfinite(units) || units <= 0 || units > 10000)
			{ reset(); return velocity; }
			if (reference_ != input.reference_generation || pose_reference_ != input.pose_reference_generation ||
				continuity_ != input.continuity_generation || pipeline_ != input.pose_pipeline || units_ != units)
				previous_ = {};
			reference_ = input.reference_generation;
			pose_reference_ = input.pose_reference_generation;
			continuity_ = input.continuity_generation;
			pipeline_ = input.pose_pipeline;
			units_ = units;
			for (unsigned h = 0; h < 2; ++h)
			{
				auto& before = previous_[h];
				const auto position = poses[h].position;
				if (!(valid_hands & (1u << h)) || !controller_input::interaction_ready(input.grip[h]) ||
					!controller_input::interaction_ready(input.aim[h]) ||
					!std::all_of(position.begin(), position.end(), [](float x) { return std::isfinite(x); }))
				{ before = {}; continue; }
				if (before.valid && input.sequence == before.sequence)
					continue;
				const float dt = std::chrono::duration<float>(input.sampled_at - before.at).count();
				if (before.valid && input.sequence > before.sequence && dt > .001f && dt < .15f)
				{
					velocity[h] = hands::scale(hands::sub(position, before.position), 1 / dt);
					const float speed = hands::length(velocity[h]), limit = 8 * units;
					if (!std::isfinite(speed)) velocity[h] = {};
					else if (speed > limit) velocity[h] = hands::scale(velocity[h], limit / speed);
				}
				before = {position, input.sampled_at, input.sequence, true};
			}
			return velocity;
		}
	};
}
