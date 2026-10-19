#pragma once
#include "grenade_contact.hpp"
#include <optional>

namespace vr::gameplay::grenades
{
	// Center-eye to estimated teeth; full head axes retain pitch and roll.
	inline std::optional<hands::anchor> mouth_pose(hands::vec head,
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
		return hands::anchor{
		    hands::add(head,
		               hands::scale(hands::sub(hands::scale(forward, .07f), hands::scale(up, .07f)), units)),
		    hands::from_axis({forward, hands::cross(up, forward), up})};
	}

	// Pin displacement is measured in head space, so the grenade's orientation
	// cannot turn a sideways pull into a forbidden direction. Only motion back
	// toward/behind the head is excluded (2 cm tolerance for tracking noise).
	inline hands::vec mouth_pull_offset(hands::vec start, hands::vec pin, float units) noexcept
	{
		if (!std::isfinite(units) || units <= 0)
			return {};
		const auto delta = hands::sub(start, pin);
		for (float x : delta)
			if (!std::isfinite(x))
				return {};
		if (delta[0] > .02f * units)
			return {};
		const float distance = hands::length(delta), limit = pin_stroke_meters * units;
		return distance > limit ? hands::scale(delta, limit / distance) : delta;
	}

	class mouth_pin_pull
	{
		using clock = controller_input::clock;
		bool bitten_{};
		hands::vec start_{}, last_{}, travel_{};
		clock::time_point at_{};
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
			return bitten_;
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

		// The pin is relative to the current mouth pose. Acquisition is a single
		// explicit attempt; staying nearby or holding Trigger never retries it.
		bool advance(hands::vec pin,
		             float units,
		             clock::time_point at,
		             std::uint64_t reference,
		             std::uint64_t continuity,
		             bool attempt,
		             bool held) noexcept
		{
			if (!held || !reference || !continuity || !std::isfinite(units) || units <= 0)
			{
				reset();
				return false;
			}
			for (float x : pin)
				if (!std::isfinite(x))
				{
					reset();
					return false;
				}
			if (bitten_ && (reference != reference_ || continuity != continuity_ || units != units_ ||
			                at < at_ || at - at_ > std::chrono::milliseconds(150)))
			{
				reset();
				return false;
			}
			if (!bitten_)
			{
				if (!attempt || hands::length(pin) > units * acquire_meters)
					return false;
				bitten_ = true;
				start_ = last_ = pin;
				at_ = at;
				reference_ = reference;
				continuity_ = continuity;
				units_ = units;
				return true;
			}
			if (at == at_)
				return bitten_;
			const float dt = std::chrono::duration<float>(at - at_).count();
			if (hands::length(hands::sub(pin, last_)) / units > .02f + 3.f * dt)
			{
				reset();
				return false;
			}
			last_ = pin;
			at_ = at;
			const auto delta = hands::scale(hands::sub(pin, start_), 1 / units);
			if (delta[0] < -.02f || hands::length(delta) > .5f)
			{
				reset();
				return false;
			}
			travel_ = mouth_pull_offset(start_, pin, units);
			return true;
		}
	};

	inline hands::vec mouth_pin_position(hands::anchor mouth, hands::anchor grenade, hands::vec pin) noexcept
	{
		using namespace hands::pose_math;
		return compose(inverse(mouth), compose(grenade, {pin, {0, 0, 0, 1}})).position;
	}
	inline hands::vec mouth_pin_offset(const mouth_pin_pull& bite,
	                                   hands::anchor mouth,
	                                   hands::anchor grenade,
	                                   hands::vec pin,
	                                   float units) noexcept
	{
		const auto travel = mouth_pull_offset(bite.start(), mouth_pin_position(mouth, grenade, pin), units);
		return hands::rotate(hands::conjugate(grenade.rotation), hands::rotate(mouth.rotation, travel));
	}
}
