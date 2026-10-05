#pragma once
#include <array>
#include <cstdint>

namespace vr::gameplay::weapons
{
	// Native command replay is not a new mechanical shot. Keep the admitted
	// pre-shot readiness/count, even after the authoritative chamber cycles.
	class native_shot_history
	{
		struct record
		{
			std::uint32_t weapon{};
			std::uint64_t generation{};
			int command{}, loaded{};
			bool allow{}, spent{};
		};
		std::array<record,128> records_{};
		std::uint64_t sequence_{};
	public:
		bool allow(std::uint32_t weapon, std::uint64_t generation, int command, int loaded,
			bool server, bool current_ready) noexcept
		{
			if (!weapon || !generation || loaded < 0) return false;
			for (const auto& r : records_)
				if (r.weapon == weapon && r.generation == generation && r.command == command)
					return r.allow && r.loaded == loaded && (!server || !r.spent);
			if (server) records_[sequence_++ % records_.size()] = {weapon,generation,command,loaded,current_ready,false};
			return current_ready; // caller requires exact current native/mechanical count match
		}
		void spent(std::uint32_t weapon, std::uint64_t generation, int command) noexcept
		{
			for (auto& r : records_)
				if (r.weapon == weapon && r.generation == generation && r.command == command) r.spent = true;
		}
	};
}
