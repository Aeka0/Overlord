#pragma once
#include "grenade_contact.hpp"
#include <optional>

namespace vr::gameplay::grenades
{
	// Center-eye to estimated teeth; full head axes retain pitch and roll.
	inline std::optional<hands::vec> mouth_point(hands::vec head,
	                                             hands::vec forward,
	                                             hands::vec up,
	                                             float units) noexcept
	{
		if (!std::isfinite(units) || units <= 0)
			return {};
		for (const auto& v : {head, forward, up})
			for (float x : v)
				if (!std::isfinite(x))
					return {};
		if (std::abs(hands::length(forward) - 1) > .02f || std::abs(hands::length(up) - 1) > .02f ||
		    std::abs(hands::dot(forward, up)) > .02f)
			return {};
		return hands::add(
		    head, hands::scale(hands::sub(hands::scale(forward, .07f), hands::scale(up, .07f)), units));
	}

	class mouth_pin_pull
	{
		using clock = controller_input::clock;
		bool pending_{}, bitten_{};
		hands::vec start_{}, last_{}, travel_{};
		clock::time_point began_{}, at_{};
		std::uint64_t reference_{}, continuity_{};
		float units_{};

	  public:
		static constexpr float acquire_meters = .045f;
		void reset() noexcept
		{
			*this = {};
		}
		bool engaged() const noexcept
		{
			return pending_;
		}
		bool bitten() const noexcept
		{
			return bitten_;
		}
		hands::vec start() const noexcept
		{
			return start_;
		}
		hands::vec travel() const noexcept
		{
			return travel_;
		}

		// Both points are in the current grenade-local frame. Common head/hand
		// translation cannot contribute to extraction, including roomscale walking.
		bool advance(hands::vec mouth,
		             hands::vec pin,
		             float units,
		             clock::time_point at,
		             std::uint64_t reference,
		             std::uint64_t continuity) noexcept
		{
			if (!reference || !continuity || !std::isfinite(units) || units <= 0)
			{
				reset();
				return false;
			}
			for (const auto& v : {mouth, pin})
				for (float x : v)
					if (!std::isfinite(x))
					{
						reset();
						return false;
					}
			if (pending_ && (reference != reference_ || continuity != continuity_ || units != units_ ||
			                 at < at_ || at - at_ > std::chrono::milliseconds(150)))
			{
				reset();
				return false;
			}
			if (!pending_)
			{
				if (hands::length(hands::sub(mouth, pin)) > units * acquire_meters)
					return false;
				pending_ = true;
				start_ = last_ = mouth;
				began_ = at_ = at;
				reference_ = reference;
				continuity_ = continuity;
				units_ = units;
				return false;
			}
			if (at == at_)
				return bitten_;
			const float dt = std::chrono::duration<float>(at - at_).count();
			if (hands::length(hands::sub(mouth, last_)) / units > .02f + 3.f * dt)
			{
				reset();
				return false;
			}
			last_ = mouth;
			at_ = at;
			if (!bitten_)
			{
				if (hands::length(hands::sub(mouth, pin)) > units * acquire_meters)
				{
					reset();
					return false;
				}
				if (at - began_ < std::chrono::milliseconds(150))
					return false;
				bitten_ = true;
				start_ = mouth; // Dwell motion never counts toward the pull stroke.
			}
			const auto delta = hands::scale(hands::sub(mouth, start_), 1 / units);
			if (std::hypot(delta[0], delta[2]) > .065f || delta[1] < -.04f || hands::length(delta) > .25f)
			{
				reset();
				return false;
			}
			travel_ = pin_slide(start_, mouth, units);
			return true;
		}
	};
}
