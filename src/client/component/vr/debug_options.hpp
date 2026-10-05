#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>

namespace vr::debug_options
{
	enum class probe : std::size_t
	{
		view, snapshots, perf, scene_models, menu_input, weapon_events, vehicle,
		reload_well, hk_slap, bolt, cover, count
	};
	inline constexpr std::array names{
		"vr_debugViewProbes", "vr_debugAutoSnapshots", "vr_debugPerfCapture", "vr_debugSceneModels",
		"vr_debugMenuInput", "vr_debugWeaponEvents", "vr_debugVehicle",
		"vr_reloadWellDebug", "vr_hkSlapDebug", "vr_boltDebug", "vr_coverPushDebug"};
	using selection = std::array<bool, static_cast<std::size_t>(probe::count)>;
	static_assert(names.size() == selection{}.size());

	namespace detail
	{
		inline std::array<std::atomic_bool, names.size()> loaded{};
		inline std::once_flag startup;
	}

	// Freeze before loading the game and installing any render hooks. Console
	// edits/save operations affect the next process, never a live hook graph.
	inline void initialize(const selection& requested)
	{
		std::call_once(detail::startup, [&]
		{
			for (std::size_t i{}; i < requested.size(); ++i)
				detail::loaded[i].store(requested[i], std::memory_order_release);
		});
	}

	[[nodiscard]] inline bool enabled(probe value) noexcept
	{
		const auto index = static_cast<std::size_t>(value);
		return index < detail::loaded.size() &&
			detail::loaded[index].load(std::memory_order_acquire);
	}
}
