#pragma once
#include "../../world_interaction_policy.hpp"
#include <chrono>

namespace vr::gameplay::cliffhanger::boarding
{
	inline constexpr float reach_meters = 1.6f;
	struct evidence
	{
		bool available{}, riding{}, trip_started{}, player_ready{};
		interaction::target_key vehicle{}, trigger{};
		hands::vec vehicle_origin{}, velocity{};
		float distance{}, units{};
		std::uint64_t reference{}, continuity{};
	};
	// Native availability is the story gate. Stillness alone cannot authorize
	// a spawned vehicle, and every interrupted observation restarts the dwell.
	class gate
	{
		using clock = std::chrono::steady_clock;
		interaction::target_key vehicle_{}, trigger_{}, consumed_{};
		hands::vec origin_{};
		std::uint64_t reference_{}, continuity_{};
		clock::time_point since_{}, last_{};
	public:
		void interrupt() noexcept { vehicle_ = {}; trigger_ = {}; since_ = {}; last_ = {}; }
		void reset() noexcept { interrupt(); consumed_ = {}; }
		bool update(const evidence& e, clock::time_point now) noexcept
		{
			const auto finite = [](const hands::vec& v)
			{
				return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
			};
			if (!e.available || e.riding || e.trip_started || !e.player_ready || !e.vehicle || !e.trigger ||
			    e.trigger == consumed_ || !finite(e.vehicle_origin) || !finite(e.velocity) ||
			    !std::isfinite(e.units) || e.units <= 1 || e.units >= 1000 ||
			    !std::isfinite(e.distance) || e.distance < 0 || e.distance > reach_meters * e.units ||
			    hands::length(e.velocity) > 1.f)
			{
				interrupt();
				return false;
			}
			if (e.vehicle != vehicle_ || e.trigger != trigger_ || e.reference != reference_ ||
			    e.continuity != continuity_ || now < last_ ||
			    now - last_ > std::chrono::milliseconds(150) ||
			    hands::length(hands::sub(e.vehicle_origin, origin_)) > .5f)
			{
				vehicle_ = e.vehicle;
				trigger_ = e.trigger;
				origin_ = e.vehicle_origin;
				reference_ = e.reference;
				continuity_ = e.continuity;
				since_ = now;
			}
			last_ = now;
			return now - since_ >= std::chrono::milliseconds(250);
		}
		void consumed(interaction::target_key trigger) noexcept { consumed_ = trigger; interrupt(); }
	};
}
