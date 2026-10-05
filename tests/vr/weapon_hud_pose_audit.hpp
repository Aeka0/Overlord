#pragma once
#include "component/vr/gameplay/weapon_interaction.hpp"

namespace vr::gameplay::weapon_hud
{
	// Diagnostic only: comparing a later CPU pose does NOT establish which bones
	// the GPU consumed. Never feed these observations back into HUD positioning.
	struct pose_audit
	{
		std::uint64_t samples{}, rejected{}, changed{}, same_input_changed{}, same_camera_changed{};
		float peak_units{};
		std::uint64_t peak_pair{}, frozen_input{}, later_input{};
		double camera_delta_ms{};
		vr::gameplay::hands::vec local_delta{}, origin_delta{};
		void observe(std::uint64_t pair, const weapons::muzzle_frame& frozen,
			const weapons::muzzle_frame& later) noexcept
		{
			if (!pair || !frozen.valid || !later.valid || !frozen.owner.can_fire() ||
				frozen.owner.weapon != later.owner.weapon || frozen.owner.revision != later.owner.revision ||
				frozen.owner.rear != later.owner.rear || frozen.reference_generation != later.reference_generation ||
				!weapons::valid_model_anchor(frozen.model) || !weapons::valid_model_anchor(later.model))
			{ ++rejected; return; }
			++samples;
			const auto delta = vr::gameplay::hands::sub(later.model.position, frozen.model.position);
			const auto distance = vr::gameplay::hands::length(delta);
			if (distance <= .001f) return;
			++changed;
			const bool same_input = frozen.input_sequence == later.input_sequence && frozen.sampled_at == later.sampled_at;
			if (same_input) ++same_input_changed;
			if (same_input && frozen.camera_at == later.camera_at) ++same_camera_changed;
			if (distance <= peak_units) return;
			peak_units = distance; peak_pair = pair;
			frozen_input = frozen.input_sequence; later_input = later.input_sequence;
			camera_delta_ms = std::chrono::duration<double, std::milli>(later.camera_at-frozen.camera_at).count();
			local_delta = delta;
			origin_delta = vr::gameplay::hands::sub(later.model.solve_origin, frozen.model.solve_origin);
		}
	};
}
