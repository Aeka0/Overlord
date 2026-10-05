#pragma once
#include "weapon_interaction.hpp"
#include "ads_alignment.hpp"

namespace vr::gameplay::weapons
{
	// Coarse raised-weapon intent, not an optic eye box. Uses the solved muzzle
	// and center eye from one camera sample; it never moves the weapon or camera.
	class ads_policy
	{
	public:
		void reset() noexcept
		{
			active_ = entering_ = leaving_ = false;
			seen_ = false;
		}

		bool consume(const controller_input::frame& input, const hold& owner,
			const muzzle_frame& muzzle, bool allowed, controller_input::clock::time_point now) noexcept
		{
			const bool discontinuity = continuity_.update(input, now);
			if (!allowed || !input.focused || !input.sequence || input.orientation_settling ||
				!ads_alignment::supported(input,owner) ||
				now < input.sampled_at || now - input.sampled_at > std::chrono::milliseconds(150) ||
				!ready(muzzle, owner, input.reference_generation, now) ||
				muzzle.input_sequence > input.sequence || !valid_translation(muzzle) ||
				!ads_alignment::valid_sight_distance(muzzle.ads_sight_to_muzzle_meters))
			{
				reset();
				return false;
			}
			if (discontinuity || !seen_ || owner.id() != owner_.id() || owner.rear != owner_.rear ||
				owner.revision != owner_.revision || owner.support != owner_.support ||
				owner.rear_revision != owner_.rear_revision || muzzle.optic.thermal != thermal_ || input.reference_generation != reference_ ||
				input.sequence < sequence_ || muzzle.sampled_at < observed_at_)
				reset();
			owner_ = owner;
			reference_ = input.reference_generation;
			sequence_ = input.sequence;
			observed_at_ = muzzle.sampled_at;
			seen_ = true;
			thermal_ = muzzle.optic.thermal;

			const auto alignment=ads_alignment::measure(muzzle.head_position,muzzle.head_forward,
				hands::sub(muzzle.position,muzzle.ads_translation),muzzle.axis,muzzle.units_per_meter);
			if(!alignment.valid) {reset();return false;}
			// Center-eye tolerance admits either aiming eye and different sight
			// heights. The physical lens applies its own eye box afterwards.
			const bool aligned=ads_alignment::inside(alignment,active_,thermal_,muzzle.ads_sight_to_muzzle_meters);
			const auto dwell=std::chrono::milliseconds(thermal_ ? 40 : 100);
			if (!active_)
			{
				leaving_ = false;
				if (!aligned) entering_ = false;
				else if (!entering_) { entering_ = true; transition_at_ = observed_at_; }
				else if (observed_at_ - transition_at_ >= dwell)
					{ active_ = true; entering_ = false; }
			}
			else if (aligned) leaving_ = false;
			else if (!leaving_) { leaving_ = true; transition_at_ = observed_at_; }
			else if (observed_at_ - transition_at_ >= dwell)
				{ active_ = false; leaving_ = false; }
			return active_;
		}

	private:
		static bool valid_translation(const muzzle_frame& muzzle) noexcept
		{
			if (!std::isfinite(muzzle.units_per_meter) || muzzle.units_per_meter <= 0 ||
				muzzle.units_per_meter > 10000) return false;
			for (auto value : muzzle.ads_translation)
				if (!std::isfinite(value) || std::abs(value)>(ads_alignment::maximum_translation(muzzle.optic.thermal)+.001f)*muzzle.units_per_meter) return false;
			return true;
		}
		hold owner_{};
		controller_input::consumer_continuity continuity_;
		std::uint64_t reference_{}, sequence_{};
		controller_input::clock::time_point observed_at_{}, transition_at_{};
		bool active_{}, entering_{}, leaving_{}, seen_{}, thermal_{};
	};
}
