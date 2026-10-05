#pragma once
#include <cstdint>
#include <chrono>
#include <span>
#include <string>

namespace game { struct GfxPlacement; struct GfxScaledPlacement; struct XModel; }

namespace scene_models
{
	// H2 shadow views cull 0x01000040: bit 24 identifies viewmodels; bit 6
	// suppresses casting without changing world depth, lighting or skinning.
	inline constexpr unsigned no_cast_shadow=0x40;

	// Register during post_unpack only. Callbacks submit read-only, bounded
	// model snapshots before the native scene sorter; never register per frame.
	using callback = void(*)();
	void on_submit(callback function);
	bool ready() noexcept;
	struct runtime_model { game::XModel* descriptor{}; game::XModel* source{}; };
	// Main asset owner only, one immutable batch. Subsets share the source asset's
	// streaming/distance identity, never its mutable geometry. Retain every
	// published descriptor until queued rendering is finished; no slot recycling.
	bool register_runtime_models(std::span<const runtime_model> models) noexcept;
	std::string runtime_model_status();
	// Per-submission world-unit frustum padding. Never changes the shared asset,
	// model scale, LOD policy or another caller/thread's visibility. Native retains
	// lighting_handle; the caller must keep that storage alive for queued work.
	void submit(game::XModel* model, game::GfxScaledPlacement* placement, unsigned flags,
		unsigned short* lighting_handle, float* lit, float* unlit, float* emissive, float cull_padding);
	struct preparation
	{
		const void* record{};
		// One time sample for the whole native packing job. Both eyes consume
		// its packed surfaces; individual parts must not sample their own clock.
		std::chrono::steady_clock::time_point at{};
	};
	enum class placement_result { unchanged, replace, omit, retain };
	// Optional, copied placement adjustment at the native rigid-surface builder.
	// Called after native skeletal preparation, before transforms are packed into
	// the frame's surface buffer. Never mutate a scene entity or retain arguments.
	// Bounded multiple consumers are supported. Return unchanged for foreign
	// entries; retain identifies an owned entry that keeps native placement.
	// Competing replacements of one entry are omitted, not composed.
	using placement_callback = placement_result(*)(const preparation&, const void* model_entry,
		game::GfxPlacement& current, game::GfxPlacement& previous) noexcept;
	void on_prepare_placement(placement_callback function);

	enum class build_outcome : unsigned { retained=1, replaced, omitted, conflicting };
	struct build_observation
	{
		const void* record{};
		const void* entry{};
		const game::XModel* model{};
		placement_callback owner{};
		const game::GfxPlacement* submitted{};
		const game::GfxPlacement* resolved{};
		build_outcome outcome{};
		int native_result{}; // zero for omitted/conflicting; native was not invoked
	};
	// Optional bounded diagnostics at the actual native packing boundary. Called
	// synchronously for owned entries only; copy data, never retain pointers or do
	// file I/O. Observers cannot alter admission, placement or the native result.
	using build_observer = void(*)(const build_observation&) noexcept;
	void observe_builds(build_observer observer) noexcept;
	struct scene_observation
	{
		std::uint64_t sequence{};
		unsigned stage{}; // 0=enter, 1=after mod submissions, 2=native generator returned
		std::uint32_t index{};
		const void* scratch{};
		const void* slot{};
	};
	using scene_observer = void(*)(const scene_observation&) noexcept;
	void observe_scenes(scene_observer observer) noexcept;
}
