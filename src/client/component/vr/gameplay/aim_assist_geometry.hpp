#pragma once
#include "../settings.hpp"
#include "shot_geometry.hpp"

namespace vr::gameplay::aim_assist
{
	// Retain the verified zodiac AI selection distance, in native game units.
	inline constexpr float max_distance = 1300.f;
	inline constexpr unsigned max_candidates = 4000; // H2 g_entities / native use-list ceiling.

	// One selection per accepted shot. Scores always use the original muzzle,
	// so visiting several targets cannot accumulate rotations beyond the cone.
	class selection
	{
		static double precise_dot(const hands::vec& a, const hands::vec& b) noexcept
		{
			return double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2];
		}

	  public:
		selection(const weapons::shot_geometry& shot, float strength) noexcept
			: shot_(shot), degrees_(settings::aim_assist_degrees(strength)),
			  cosine_(std::cos(double(degrees_) * 0.017453292519943295)),
			  forward_length_(std::sqrt(precise_dot(shot.forward, shot.forward)))
		{
		}

		template <typename Visible>
		bool consider(unsigned entity, const hands::vec& point, Visible&& visible)
		{
			if (degrees_ <= 0 || entity >= max_candidates) return false;
			for (auto x : point)
				if (!std::isfinite(x) || std::abs(x) > 1e7f) return false;
			const auto delta = hands::sub(point, shot_.origin);
			const auto distance_squared = precise_dot(delta, delta);
			if (!std::isfinite(distance_squared) || distance_squared < 1e-6f ||
				distance_squared > max_distance * max_distance) return false;
			const auto distance = std::sqrt(distance_squared);
			const auto direction = hands::scale(delta, static_cast<float>(1. / distance));
			// Double precision keeps fractional strengths from widening when a
			// tiny cone's cosine rounds to 1.0 in single precision.
			const auto alignment = precise_dot(shot_.forward, delta) / (forward_length_ * distance);
			if (!std::isfinite(alignment) || alignment < cosine_) return false;
			if (found_ && (alignment < best_alignment_ || (alignment == best_alignment_ &&
				(distance_squared > best_distance_squared_ ||
				 (distance_squared == best_distance_squared_ && entity >= best_entity_))))) return false;
			// Test visibility only for candidates that can improve the result.
			// An occluded target must not hide the next visible candidate.
			if (!visible(entity, point)) return false;
			found_ = true;
			best_entity_ = entity;
			best_alignment_ = alignment;
			best_distance_squared_ = distance_squared;
			best_direction_ = direction;
			return true;
		}

		bool apply(weapons::shot_geometry& result) const noexcept
		{
			if (!found_) return false;
			const auto rotation = hands::from_to(shot_.forward, best_direction_);
			result = {best_direction_, hands::rotate(rotation, shot_.right),
				hands::rotate(rotation, shot_.up), shot_.origin};
			return true;
		}
		int entity() const noexcept {return found_ ? static_cast<int>(best_entity_) : -1;}
		float correction_degrees() const noexcept
		{return found_ ? static_cast<float>(std::acos(std::clamp(best_alignment_,-1.,1.))*57.29577951308232) : 0.f;}

	  private:
		weapons::shot_geometry shot_;
		float degrees_;
		double cosine_, forward_length_;
		bool found_{};
		unsigned best_entity_{};
		double best_alignment_{}, best_distance_squared_{};
		hands::vec best_direction_{};
	};
}
