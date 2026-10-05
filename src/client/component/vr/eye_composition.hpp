#pragma once
#include "engine_stereo_view.hpp"
#include "desktop_mirror_layout.hpp"
#include "auxiliary_scene.hpp"
#include "presentation_options.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <d3d11.h>

namespace vr::eye_composition
{
	struct model_view_origins
	{
		std::array<std::array<float, 3>, 2> eyes{};
		std::array<float, 3> placement{};
		bool valid{};
	};
	// CURRENT native WORLD_MATRIX0 (backend +0x900): placement at +0x325C
	// minus primary eye at +0x2D60. Record inputs are +0x190 and +0x100.
	// Do NOT use PREV_FRAME_WORLD (+0x800): its +0x19C placement and +0x2DC0
	// eye belong to history and cannot be combined with the current projection.
	// Export immutable, pair-matched CPU metadata, never later backend globals.
	// Native skinned placement has identity rotation/unit scale.
	// Both eyes must share that placement before using a single world billboard.
	[[nodiscard]] inline model_view_origins model_origins_for(
		const engine_stereo_view::slot_pair& views,
		const engine_stereo_view::scene_record_pair& records) noexcept
	{
		model_view_origins result;
		if (!records.pair_id || !records.publication) return result;
		for (unsigned eye = 0; eye < 2; ++eye)
		{
			const auto& slot = views.eyes[eye];
			if (slot.pair_id != records.pair_id || slot.publication != records.publication ||
				slot.output_eye != eye) return {};
			const auto& record = eye == 0 ? records.left : records.right;
			std::memcpy(result.eyes[eye].data(), record.data() +
				engine_stereo_view::h2_view_origin_offset, sizeof(result.eyes[eye]));
			for (const float value : result.eyes[eye])
				if (!std::isfinite(value) || std::abs(value) > 1e7f) return {};
			std::array<float, 3> placement{};
			std::memcpy(placement.data(), record.data() +
				engine_stereo_view::h2_current_model_placement_origin_offset, sizeof(placement));
			for (const float value : placement)
				if (!std::isfinite(value) || std::abs(value) > 1e7f) return {};
			if (eye == 0) result.placement = placement;
			else if (placement != result.placement) return {};
		}
		result.valid = true;
		return result;
	}
	struct event
	{
		const engine_stereo_view::slot_pair& views;
		std::uint64_t pair_id{}, device_generation{};
		std::uint32_t eye{}, width{}, height{};
		model_view_origins model_origins{};
		// Optional output owned by copy_eye for this call only. A successfully
		// drawn right-eye recording frame publishes its immutable desktop crop.
		desktop_mirror::crop* recording_crop{};
		ID3D11DepthStencilView* scene_depth{}; // Borrowed for this eye only, before its depth is reused.
		const auxiliary_scene::request* auxiliary{};
		ID3D11ShaderResourceView* auxiliary_image{}; // Same-frame display image, borrowed for this call.
	};
	// Optional presentation after native display conversion, BEFORE publishing
	// the ring target. Runs on H2's serialized immediate-context owner. No waits,
	// runtime IPC, native-session reentry or retained destination targets allowed.
	// Source is display-encoded; destination is linear. Restore every D3D state.
	using callback = void(*)(const event&, ID3D11DeviceContext*,
		ID3D11ShaderResourceView*, ID3D11RenderTargetView*) noexcept;
	// Ordered independent layers; a diagnostic must not replace the spatial HUD.
	enum class layer : unsigned { world_equipment, weapon_guides, optics, weapon_display, screen_scope, spatial_hud, diagnostics, indicators, remote_hud, damage, narrative, interaction_diagnostics, menu_backdrop, recording_frame, count };
	inline std::atomic<std::uint64_t(*)() noexcept> remote_camera_epoch{};
	inline std::array<std::atomic<callback>, static_cast<unsigned>(layer::count)> consumers{};
	inline bool visible_on_weapon_display(layer slot) noexcept
	{
		// Its scene camera follows the launcher, while the viewer sees a separate
		// plane. World-projected panels/markers cannot be pasted over that plane.
		return slot==layer::weapon_display || slot==layer::screen_scope || slot==layer::damage ||
			slot==layer::narrative || slot==layer::recording_frame;
	}
	// Optional scene registration BEFORE dispatching native backend jobs.
	// Copy MOD metadata only; no native/GPU calls or waits.
	using scene_record_callback = void(*)(const engine_stereo_view::slot_pair&, std::uintptr_t) noexcept;
	inline std::atomic<scene_record_callback> scene_record_consumer{};
	inline void register_scene(const engine_stereo_view::slot_pair& views, std::uintptr_t record) noexcept
	{
		// Register an address only while paired with its immutable publication.
		// The consumer may bind native worker output later; never wait here.
		if (record) if (const auto fn = scene_record_consumer.load()) fn(views, record);
	}
	inline void set_consumer(callback value, layer slot = layer::spatial_hud) noexcept
	{
		const auto index = static_cast<unsigned>(slot);
		if (index < consumers.size()) consumers[index].store(value);
	}
	inline void compose(const event& value, ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* source, ID3D11RenderTargetView* output) noexcept
	{
		thread_local std::uint64_t visibility_pair{};
		thread_local bool hidden{};
		if(value.eye==0 || visibility_pair!=value.pair_id)
		{visibility_pair=value.pair_id;hidden=!presentation_options::show_hud();}
		for(unsigned i=0;i<consumers.size();++i)
		{
			if(hidden && (static_cast<layer>(i)==layer::spatial_hud || static_cast<layer>(i)==layer::indicators || static_cast<layer>(i)==layer::remote_hud))continue;
			if(value.views.weapon_display_epoch && !visible_on_weapon_display(static_cast<layer>(i)))continue;
			if(const auto fn=consumers[i].load())fn(value,context,source,output);
		}
	}
}
