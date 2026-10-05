#pragma once

#include "engine_stereo_bridge.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>

namespace vr::engine_stereo_view
{
	inline constexpr std::size_t h2_view_slot_size = 0x170;
	inline constexpr std::size_t h2_scene_record_size = 0x8090;
	inline constexpr std::size_t h2_frontend_rebase_view_offset = 0x2CC0;
	inline constexpr std::size_t h2_view_origin_offset = 0x100;
	// GfxViewInfo +0x188 parameters: +0x08 CURRENT skinned placement.
	// +0x14 (record +0x19C) belongs to PREV_FRAME_WORLD, not WORLD_MATRIX0.
	inline constexpr std::size_t h2_current_model_placement_origin_offset = 0x190;
	inline constexpr std::size_t h2_current_view_projection_offset = 0x80;
	inline constexpr std::size_t h2_temporal_history_view_projection_offset = 0x2D40;
	inline constexpr std::size_t h2_previous_view_projection_constant_offset = 0xF90;
	inline constexpr std::size_t h2_previous_eye_position_constant_offset = 0xFD0;
	// record input (0x580) + CONST_SRC_CODE_INV_SCENE_PROJECTION (207) * 16.
	inline constexpr std::size_t h2_inverse_scene_projection_constant_offset = 0x1270;
	inline constexpr std::size_t h2_relative_eye_offset_offset =
		h2_frontend_rebase_view_offset + h2_view_origin_offset;

	struct eye_slot
	{
		alignas(16) std::array<std::uint8_t, h2_view_slot_size> bytes{};
		std::uint64_t pair_id{};
		std::uint64_t publication{};
		std::uint32_t output_eye{};
		std::uint32_t view_eye{};
	};

	struct slot_pair
	{
		std::array<eye_slot, 2> eyes{};
		// Immutable scene-source origin + axis used to derive this pair. Model
		// rebasing uses this same center, never a later mutable record prefix.
		std::array<float, 12> natural_camera{};
		std::chrono::steady_clock::time_point camera_sampled_at{};
		std::uint64_t stabilization_epoch{};
		// Scene-bound native optical framing for scripted fullscreen scopes.
		std::array<float,2> native_tan_half{};
		std::uint64_t screen_scope_epoch{};
		std::uint64_t remote_camera_epoch{};
		std::uint64_t weapon_display_epoch{};
		float native_near_distance{},screen_scope_aspect{};

		[[nodiscard]] std::array<float, 3> source_origin() const noexcept
		{
			return {natural_camera[0], natural_camera[1], natural_camera[2]};
		}
	};

	struct culling_union_slot
	{
		alignas(16) std::array<std::uint8_t, h2_view_slot_size> bytes{};
		float tan_left{};
		float tan_right{};
		float tan_down{};
		float tan_up{};
		float near_distance_units{};
		float horizontal_origin_expansion{};
	};

	struct scene_record_pair
	{
		alignas(16) std::array<std::uint8_t, h2_scene_record_size> left{};
		alignas(16) std::array<std::uint8_t, h2_scene_record_size> right{};
		std::uint64_t pair_id{};
		std::uint64_t publication{};
	};

	struct temporal_history_state
	{
		std::array<std::array<float, 16>, 2> view_projection{};
		std::array<std::array<float, 3>, 2> origin{};
		std::uint64_t device_generation{};
		std::uint64_t pair_id{};
		bool ready{};
	};

	struct temporal_history_preparation
	{
		temporal_history_state current{};
		bool valid{};
		bool seeded{};
	};

	enum class backend_model_state_sync_failure : std::uint8_t
	{
		none,
		invalid_argument,
		pointer_overflow,
		primary_source,
		rebase_source,
		primary_origin,
		relative_eye_offset,
		non_finite,
	};

	struct backend_model_state_sync_result
	{
		bool matched{};
		bool cache_invalidated{};
		backend_model_state_sync_failure failure{backend_model_state_sync_failure::none};
		std::uintptr_t cache_before{};
		std::uintptr_t primary_source{};
		std::uintptr_t expected_primary_source{};
		std::uintptr_t rebase_source{};
		std::uintptr_t expected_rebase_source{};
		std::array<float, 3> primary_origin{};
		std::array<float, 3> expected_primary_origin{};
		std::array<float, 3> relative_eye_offset{};
		std::array<float, 3> expected_relative_eye_offset{};
	};

	struct backend_depth_hack_projection_result
	{
		bool matched{};
		std::array<float, 4> previous_terms{};
		std::array<float, 4> restored_terms{};
		float depth_hack_near{};
	};

	enum class backend_depth_hack_projection_outcome : std::uint8_t
	{
		not_applicable,
		restored,
		contract_mismatch,
	};

	inline constexpr std::size_t h2_backend_primary_source_pointer_offset = 0x3238;
	inline constexpr std::size_t h2_backend_rebase_source_pointer_offset = 0x3240;
	inline constexpr std::size_t h2_backend_primary_origin_offset = 0x2D60;
	inline constexpr std::size_t h2_backend_relative_eye_offset = 0x2D70;
	inline constexpr std::size_t h2_backend_xmodel_placement_cache_offset = 0x3270;
	inline constexpr std::size_t h2_backend_projection_cache_offset = 0x0100;
	inline constexpr std::size_t h2_backend_eye_projection_offset = 0x2C30;
	inline constexpr std::size_t h2_backend_depth_hack_near_offset = 0x2D3C;
	inline constexpr std::size_t h2_backend_projection_cache_version_offset = 0x3160;
	inline constexpr std::size_t h2_backend_projection_input_version_offset = 0x31EA;
	inline constexpr std::size_t h2_backend_depth_hack_flags_offset = 0x3340;
	inline constexpr std::size_t h2_draw_list_descriptor_base = 0x2FC8;
	inline constexpr std::size_t h2_draw_list_descriptor_size = 0x130;
	inline constexpr std::size_t h2_draw_list_active_type_offset = 0x100;
	inline constexpr std::size_t h2_draw_list_origin_offset = 0x110;
	inline constexpr std::size_t h2_draw_list_descriptor_count = 68;
	inline constexpr std::array<std::size_t, 27> h2_camera_model_list_indices{
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
		18, 19, 20, 21, 22, 23, 24, 25, 29,
	};

	struct camera_model_origin_update
	{
		std::uint32_t rewritten{};
		std::uint32_t already_eye{};
		std::uint32_t inactive{};
		std::uint32_t foreign{};
	};

	struct camera_model_origin_census
	{
		std::uint32_t eye{};
		std::uint32_t inactive{};
		std::uint32_t other{};
	};

	// Derive two record-local H2 view slots from one natural center-eye slot.
	// This function performs no H2, D3D or OpenVR call. It rebuilds the view
	// rotation and the base asymmetric reverse-Z projection; the caller must run
	// H2's pure view-slot finalizer before publishing or consuming the slots.
	[[nodiscard]] bool derive(const void* natural_slot,
		const std::array<engine_stereo_bridge::render_config, 2>& configs,
		slot_pair& output) noexcept;

	// Build the single conservative frontend frustum whose volume contains both
	// translated eye frusta for every point on or beyond H2's reverse-Z near
	// plane. The origin and camera basis remain the natural center/head view;
	// only projection/frustum fields are widened. The caller must run H2's pure
	// view-slot finalizer before giving this slot to the visibility transaction.
	[[nodiscard]] bool derive_culling_union(const void* natural_slot,
		const std::array<engine_stereo_bridge::render_config, 2>& configs,
		culling_union_slot& output,float screen_scope_aspect=0) noexcept;
	[[nodiscard]] bool validate_finalized_culling_union(
		const culling_union_slot& value) noexcept;

	// H2's independent FX camera is built before FX worker publication. Replace
	// only its four side planes with an envelope of both translated eye frusta.
	// Camera ABI: size 0xC0, planes +0x10, origin +0, axis +0x70, count +0x94.
	// No mutation on failure; near/far, FOV/LOD inputs and flags remain intact.
	[[nodiscard]] bool apply_fx_culling_union(void* camera,
		const std::array<engine_stereo_bridge::render_config, 2>& configs) noexcept;

	// Validate the fields owned by derive(). Derived view-projection and inverse
	// matrices are deliberately excluded until H2's finalizer has run.
	[[nodiscard]] bool validate_derived(const slot_pair& value) noexcept;

	// Validate the complete matrix set after H2's pure finalizer has run.
	[[nodiscard]] bool validate_finalized(const slot_pair& value) noexcept;
	// Recover optical bounds from the exact eye slot used for rasterization.
	// Do not substitute current runtime globals or the frontend culling union.
	[[nodiscard]] bool read_projection(const eye_slot& slot,
		engine_stereo_bridge::eye_projection& output) noexcept;

	// Clone one complete, immutable H2 world record and install its finalized
	// leading eye view. H2's +0x2CC0 view is a frontend-rebase coordinate space,
	// not a second absolute camera. Preserve its natural origin and add only the
	// leading eye's displacement from the published scene-source primary origin.
	// Reject a source camera that differs at backend consumption; a stale eye
	// publication must not be overlaid on a different scene's model payload.
	// The backend
	// consumes that result as the rigid/skinned XModel eyeOffset. The remaining
	// rebase view bytes deliberately stay untouched; no pointer inside the record
	// is followed and no H2/D3D state is touched.
	// The backend owner remains responsible for the record-local 0x2C90..0x2C9C
	// target mutations.
	[[nodiscard]] bool clone_scene_records(const void* natural_record,
		const slot_pair& views, scene_record_pair& output) noexcept;
	// Give each eye an independent temporal namespace. The previous successful
	// output-eye view is written only to the three proven SSR constant ranges;
	// record+0x2DC0 remains the independently proven XModel rebase origin.
	[[nodiscard]] bool prepare_temporal_history(scene_record_pair& records,
		std::uint64_t device_generation, const temporal_history_state& committed,
		temporal_history_preparation& output) noexcept;
	[[nodiscard]] bool commit_temporal_history(temporal_history_state& state,
		const temporal_history_preparation& prepared) noexcept;

	// Apply the proven spatial and temporal eye-local ranges to H2's arena-owned record. Eye 0
	// executes that natural record directly, while eye 1 executes its private
	// clone; keeping this operation explicit prevents the relative XModel
	// eyeOffset from being lost on the natural-record path.
	[[nodiscard]] bool copy_eye_local_record_fields(void* destination,
		const std::array<std::uint8_t, h2_scene_record_size>& source) noexcept;

	// Validate the two exact view-copy sources and their published origins, then
	// invalidate only the borrowed placement key whose native lookup omits eye
	// identity. H2 itself rebuilds the XModel matrix and version state.
	[[nodiscard]] bool synchronize_backend_model_state(void* backend_state,
		const void* eye_record, backend_model_state_sync_result& output) noexcept;
	[[nodiscard]] const char* to_string(backend_model_state_sync_failure value) noexcept;

	// H2's flat depth-hack builder replaces the entire current-eye projection
	// with a symmetric viewmodel-FOV matrix. Native stereo instead follows the
	// engine's mature projection contract: restore the complete current-eye
	// projection and change only m[3][2] to H2's depth-hack near clip. This helper
	// runs only after the native builder and version publication have completed.
	[[nodiscard]] backend_depth_hack_projection_outcome
		restore_backend_depth_hack_projection(void* backend_state,
			backend_depth_hack_projection_result& output) noexcept;

	// Rebase only the proven camera/dynamic-model list subset. Descriptors with a
	// natural-center origin become eye-local; zero/lazy and special origins remain
	// under H2 ownership. The end census is read-only and provides a strict owner
	// acceptance contract after H2 has initialized lazy descriptors.
	[[nodiscard]] bool rebase_camera_model_list_origins(void* eye_record,
		const std::array<float, 3>& natural_center,
		camera_model_origin_update& output) noexcept;
	[[nodiscard]] bool census_camera_model_list_origins(const void* eye_record,
		camera_model_origin_census& output) noexcept;
}
