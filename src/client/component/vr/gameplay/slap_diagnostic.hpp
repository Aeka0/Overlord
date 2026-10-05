#pragma once
#include "swept_impact.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	enum class slap_reason
	{
		disabled, invalid, not_latched, busy, support, trigger, first_sample, sample_gap, jump,
		separate, outside, speed, direction, travel, below, world_slow, world_fast, candidate, rejected, accepted
	};
	inline const char* name(slap_reason reason) noexcept
	{
		switch (reason)
		{
		case slap_reason::disabled:return "OFF";
		case slap_reason::invalid:return "INVALID TRACKING";
		case slap_reason::not_latched:return "NOT LATCHED";
		case slap_reason::busy:return "HAND BUSY";
		case slap_reason::support:return "SUPPORT HELD";
		case slap_reason::trigger:return "TRIGGER HELD";
		case slap_reason::first_sample:return "FIRST SAMPLE";
		case slap_reason::sample_gap:return "SAMPLE GAP";
		case slap_reason::jump:return "CONTACT JUMP";
		case slap_reason::separate:return "NEED SEPARATION";
		case slap_reason::outside:return "OUTSIDE TARGET";
		case slap_reason::speed:return "CONTACT TOO SLOW";
		case slap_reason::direction:return "WRONG DIRECTION";
		case slap_reason::travel:return "TRAVEL TOO SHORT";
		case slap_reason::below:return "FROM BELOW";
		case slap_reason::world_slow:return "WRIST TOO SLOW";
		case slap_reason::world_fast:return "WRIST TOO FAST";
		case slap_reason::candidate:return "CONTACT PASS";
		case slap_reason::rejected:return "WRITE REJECTED";
		case slap_reason::accepted:return "ACCEPTED";
		default:return "UNKNOWN";
		}
	}
	struct slap_observation
	{
		std::uint64_t sequence{};
		std::chrono::steady_clock::time_point at{};
		impact_observation impact{};
		float world_speed{};
		bool examined{}, eligible{}, world_speed_ok{};
		slap_reason reason{slap_reason::disabled};
	};
	struct slap_trace
	{
		slap_observation latest{}, last_contact{};
		std::uint64_t contacts{}, accepted{};
		void record(const slap_observation& value) noexcept
		{
			if (value.sequence==latest.sequence) return;
			latest=value;
			if (value.impact.entered || value.impact.hit)
			{
				last_contact=value; ++contacts;
				if (value.reason==slap_reason::accepted) ++accepted;
			}
		}
	};
}
