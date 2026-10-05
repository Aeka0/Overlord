#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::gameplay::interaction_schedule
{
	// Local dispatch keys only, never native IDs or serialized interaction identities.
	enum class provider_id : std::uint8_t
	{
		reload_items,
		detachable_reload,
		cylinder_reload,
		tube_reload,
		break_action_reload,
		launcher_reload,
		underbarrel,
		equipment,
		grenades,
		mission_equipment,
		nightvision,
		heartbeat,
		world,
		ladders,
		count
	};
	inline constexpr auto provider_count = std::size_t(provider_id::count);
	enum class reporting
	{
		acquisition,
		reconciliation
	};

	// Fresh acquisition observes supply first; candidate arbitration follows collection.
	inline constexpr std::array acquisition_reports{provider_id::reload_items,
	                                                provider_id::detachable_reload,
	                                                provider_id::cylinder_reload,
	                                                provider_id::tube_reload,
	                                                provider_id::break_action_reload,
	                                                provider_id::launcher_reload,
	                                                provider_id::underbarrel,
	                                                provider_id::equipment,
	                                                provider_id::grenades,
	                                                provider_id::mission_equipment,
	                                                provider_id::nightvision,
	                                                provider_id::heartbeat,
	                                                provider_id::world,
	                                                provider_id::ladders};

	// Reconciliation observes settled feeds first, retaining native escrow settlement order.
	inline constexpr std::array reconciliation_reports{provider_id::detachable_reload,
	                                                   provider_id::cylinder_reload,
	                                                   provider_id::tube_reload,
	                                                   provider_id::break_action_reload,
	                                                   provider_id::launcher_reload,
	                                                   provider_id::reload_items,
	                                                   provider_id::underbarrel,
	                                                   provider_id::equipment,
	                                                   provider_id::grenades,
	                                                   provider_id::mission_equipment,
	                                                   provider_id::heartbeat,
	                                                   provider_id::world,
	                                                   provider_id::nightvision,
	                                                   provider_id::ladders};

	// Every domain proposes before carry contributes and the arbiter resolves the batch.
	inline constexpr std::array collections{provider_id::nightvision,
	                                        provider_id::reload_items,
	                                        provider_id::underbarrel,
	                                        provider_id::detachable_reload,
	                                        provider_id::cylinder_reload,
	                                        provider_id::tube_reload,
	                                        provider_id::break_action_reload,
	                                        provider_id::launcher_reload,
	                                        provider_id::equipment,
	                                        provider_id::grenades,
	                                        provider_id::mission_equipment,
	                                        provider_id::heartbeat,
	                                        provider_id::world,
	                                        provider_id::ladders};

	// Granted commits settle before magazine/support handoffs and world-use acquisition.
	inline constexpr std::array settlements{provider_id::nightvision,
	                                        provider_id::grenades,
	                                        provider_id::mission_equipment,
	                                        provider_id::detachable_reload,
	                                        provider_id::cylinder_reload,
	                                        provider_id::tube_reload,
	                                        provider_id::break_action_reload,
	                                        provider_id::launcher_reload,
	                                        provider_id::reload_items,
	                                        provider_id::ladders};

	// These domains reconcile even after an admitted weapon batch.
	inline constexpr std::array continuous_lifecycle{provider_id::grenades,
	                                                 provider_id::mission_equipment,
	                                                 provider_id::nightvision,
	                                                 provider_id::ladders};

	// Feed lifecycle runs when no fresh weapon batch was admitted; it never acquires from cached input.
	inline constexpr std::array idle_weapon_lifecycle{provider_id::detachable_reload,
	                                                  provider_id::cylinder_reload,
	                                                  provider_id::tube_reload,
	                                                  provider_id::break_action_reload,
	                                                  provider_id::launcher_reload,
	                                                  provider_id::reload_items};

	template <std::size_t N>
	constexpr bool contains(const std::array<provider_id, N>& phase, provider_id id) noexcept
	{
		for (auto current : phase)
			if (current == id)
				return true;
		return false;
	}
	template <std::size_t N> constexpr bool valid_phase(const std::array<provider_id, N>& phase) noexcept
	{
		std::array<bool, provider_count> seen{};
		for (auto id : phase)
		{
			const auto i = std::size_t(id);
			if (i >= seen.size() || seen[i])
				return false;
			seen[i] = true;
		}
		return true;
	}
	static_assert(acquisition_reports.size() == provider_count && valid_phase(acquisition_reports));
	static_assert(reconciliation_reports.size() == provider_count && valid_phase(reconciliation_reports));
	static_assert(collections.size() == provider_count && valid_phase(collections));
	static_assert(valid_phase(settlements) && valid_phase(continuous_lifecycle) &&
	              valid_phase(idle_weapon_lifecycle));

	// One binding inventory. Each non-null callback must have exactly one place
	// in its applicable phase, including suspended/repeated-input lifecycle paths.
	template <class Providers> constexpr bool valid_bindings(const Providers& providers) noexcept
	{
		if (providers.size() != provider_count)
			return false;
		for (std::size_t i = 0; i < providers.size(); ++i)
		{
			const auto& p = providers[i];
			const auto id = static_cast<provider_id>(i);
			if (p.id != id || !p.report || !p.collect)
				return false;
			if (bool(p.settle) != contains(settlements, id))
				return false;
			const bool continuous = contains(continuous_lifecycle, id),
			           idle = contains(idle_weapon_lifecycle, id);
			if (continuous && idle)
				return false;
			if (bool(p.lifecycle) != (continuous || idle))
				return false;
		}
		return true;
	}
}
