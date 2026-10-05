#pragma once
#include "thermal_scene_policy.hpp"
#include "auxiliary_scene.hpp"

#include "component/d3d11.hpp"
#include "engine_stereo_binding.hpp"
#include "engine_stereo_dynamic_arena.hpp"
#include "engine_stereo_dynamic_upload.hpp"
#include "engine_stereo_tessellation_view.hpp"
#include "native_display_contract.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_resource_ops
{
	struct event;
}

namespace vr::engine_stereo_owner_pass
{
	enum class gate_state : std::uint8_t
	{
		waiting,
		active,
		gpu_pending,
		complete,
		failed,
	};

	enum class failure : std::uint8_t
	{
		none,
		prerequisite,
		claim,
		record,
		thread,
		target_missing,
		target_contract,
		source_contract,
		resource_creation,
		copy,
		query,
		readback,
		content,
		timeout,
		device,
		model_state,
		dynamic_mesh,
		eye_resource,
		display_transform,
	};

	enum class frontend_culling_admission : std::uint8_t
	{
		unarmed,
		accepted,
		unavailable,
	};

	enum class model_state_failure_stage : std::uint8_t
	{
		none,
		invalid_backend,
		thread,
		backend_capacity,
		sync_contract,
		camera_model_rebase,
	};

	inline constexpr std::size_t maximum_backend_states_per_transaction = 8;
	// Include the native scene executor's light/shadow views, not only camera views.
	inline constexpr std::size_t maximum_dynamic_index_boundaries_per_eye = 128;

	// Keep one extra entry so boundary_capacity/order retains the rejected call.
	// This observer never chooses a rebase or changes native execution.
	struct dynamic_index_boundary
	{
		std::uintptr_t caller{};
		std::uintptr_t backend{};
		engine_stereo_dynamic_upload::phase upload_phase{};
		bool geometry{};
	};

	struct dynamic_index_trace
	{
		std::array<std::array<dynamic_index_boundary,
			maximum_dynamic_index_boundaries_per_eye + 1>, auxiliary_scene::view_count> eyes{};
		std::array<std::uint32_t, auxiliary_scene::view_count> counts{};
		std::array<std::uint32_t, auxiliary_scene::view_count> dropped{};
	};

	struct backend_state_registration
	{
		std::uintptr_t identity{};
		std::uint8_t eye_mask{};
	};

	struct report
	{
		gate_state state{gate_state::waiting};
		failure error{failure::none};
		std::uint64_t attempts{};
		std::uint64_t completions{};
		std::uint64_t failures{};
		std::uint64_t production_attempts{};
		std::uint64_t production_completions{};
		std::uint64_t production_failures{};
		failure last_failure{failure::none};
		bool failure_in_production{};
		std::uint64_t failure_pair{};
		std::uint32_t failure_eye{auxiliary_scene::no_view};
		std::uint32_t failure_completed_mask{};
		std::array<std::uint32_t, auxiliary_scene::view_count> failure_model_other{};
		std::uint64_t production_eye_copies{};
		std::array<native_display_contract::route, auxiliary_scene::view_count> display_routes{};
		std::uint64_t display_transform_completions{};
		std::uint64_t display_transform_failures{};
		const char* display_transform_error{"none"};
		std::uint32_t display_format{};
		std::uint32_t display_bind_flags{};
		std::uint64_t production_context_lock_acquires{};
		std::uint64_t production_context_lock_failures{};
		std::uint64_t production_context_lock_wait_total_us{};
		std::uint64_t production_context_lock_wait_max_us{};
		std::uint64_t production_context_lock_wait_last_us{};
		std::uint32_t production_context_lock_last_result{WAIT_FAILED};
		std::uint64_t production_last_pair{};
		std::uint64_t production_transaction_timing_samples{};
		std::uint64_t production_transaction_timing_last_us{};
		std::uint64_t production_transaction_timing_max_us{};
		std::uint64_t production_transaction_timing_total_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_eye_timing_samples{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_eye_timing_last_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_eye_timing_max_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_eye_timing_total_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_owner_invoke_timing_samples{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_owner_invoke_timing_last_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_owner_invoke_timing_max_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_owner_invoke_timing_total_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_dynamic_view_copy_timing_samples{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_dynamic_view_copy_timing_last_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_dynamic_view_copy_timing_max_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_dynamic_view_copy_timing_total_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_native_copy_timing_samples{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_native_copy_timing_last_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_native_copy_timing_max_us{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_native_copy_timing_total_us{};
		std::uint64_t production_dynamic_restore_timing_samples{};
		std::uint64_t production_dynamic_restore_timing_last_us{};
		std::uint64_t production_dynamic_restore_timing_max_us{};
		std::uint64_t production_dynamic_restore_timing_total_us{};
		std::uint64_t temporal_history_preparations{};
		std::uint64_t temporal_history_seeds{};
		std::uint64_t temporal_history_commits{};
		std::uint64_t temporal_history_failures{};
		std::uint64_t temporal_history_reset_requests{};
		std::uint64_t temporal_history_reset_applications{};
		std::uint64_t temporal_history_last_pair{};
		bool temporal_history_reset_pending{};
		bool temporal_history_ready{};
		bool temporal_history_current_frame_diagnostic{};
		std::uint64_t temporal_history_current_frame_pairs{};
		bool material_reuse_left_diagnostic{};
		std::uint64_t material_reuse_left_pairs{};
		std::uint64_t material_reuse_left_captures{};
		std::uint64_t material_reuse_left_replacements{};
		std::uint64_t material_reuse_left_failures{};
		std::uint64_t material_reuse_left_last_pair{};
		std::uint64_t dynamic_index_captures{};
		std::uint64_t dynamic_index_rebases{};
		std::uint64_t dynamic_index_restores{};
		std::uint64_t dynamic_index_validations{};
		std::uint64_t dynamic_index_failures{};
		std::uintptr_t dynamic_index_data_identity{};
		std::uint32_t dynamic_index_last_left_boundaries{};
		std::uint32_t dynamic_index_last_right_boundaries{};
		engine_stereo_dynamic_arena::failure dynamic_index_last_failure{
			engine_stereo_dynamic_arena::failure::none};
		engine_stereo_dynamic_upload::report dynamic_upload;
		dynamic_index_trace dynamic_index_failure_trace;
		std::uint64_t gpu_census_request_sequence{};
		std::uint64_t gpu_census_applied_sequence{};
		std::uint64_t gpu_census_reset_attempts{};
		std::uint64_t gpu_census_reset_failures{};
		std::uint64_t gpu_census_launch_attempts{};
		std::uint64_t gpu_census_launch_failures{};
		std::uint64_t gpu_census_last_capture_pair{};
		std::uint32_t gpu_census_requested_delay{};
		std::uint32_t gpu_census_delay_remaining{};
		bool gpu_census_rearm_applying{};
		bool gpu_census_launch_attempted{};
		std::uint64_t model_state_sync_calls{};
		std::uint64_t model_state_sync_matches{};
		std::uint64_t model_state_foreign_bypasses{};
		std::uint64_t model_state_contract_mismatches{};
		std::uint64_t model_state_backend_registrations{};
		std::uint64_t model_state_backend_reuses{};
		std::uint64_t model_state_backend_maximum_active{};
		model_state_failure_stage model_state_last_failure_stage{
			model_state_failure_stage::none};
		std::uintptr_t model_state_failure_backend{};
		std::uintptr_t model_state_failure_expected_backend{};
		std::uint32_t model_state_failure_thread{};
		std::uint32_t model_state_failure_expected_thread{};
		engine_stereo_view::backend_model_state_sync_failure model_state_last_failure{
			engine_stereo_view::backend_model_state_sync_failure::none};
		std::uint32_t model_state_last_failure_eye{auxiliary_scene::no_view};
		std::uintptr_t model_state_failure_primary_source{};
		std::uintptr_t model_state_failure_expected_primary_source{};
		std::uintptr_t model_state_failure_rebase_source{};
		std::uintptr_t model_state_failure_expected_rebase_source{};
		std::array<float, 3> model_state_failure_primary_origin{};
		std::array<float, 3> model_state_failure_expected_primary_origin{};
		std::array<float, 3> model_state_failure_relative_eye_offset{};
		std::array<float, 3> model_state_failure_expected_relative_eye_offset{};
		std::uint64_t model_cache_invalidations{};
		std::uint64_t model_cache_already_empty{};
		std::array<std::uint64_t, auxiliary_scene::view_count> model_state_eye_matches{};
		std::array<std::uint64_t, auxiliary_scene::view_count> model_cache_eye_invalidations{};
		std::array<std::uintptr_t, auxiliary_scene::view_count> model_cache_last_values{};
		std::array<std::array<float, 3>, auxiliary_scene::view_count> model_primary_origins{};
		std::array<std::array<float, 3>, auxiliary_scene::view_count> model_relative_eye_offsets{};
		std::uint64_t depth_hack_projection_calls{};
		std::uint64_t depth_hack_projection_restores{};
		std::uint64_t depth_hack_projection_bypasses{};
		std::uint64_t depth_hack_projection_foreign_bypasses{};
		std::uint64_t depth_hack_projection_contract_mismatches{};
		std::array<std::uint64_t, auxiliary_scene::view_count> depth_hack_projection_eye_restores{};
		std::array<std::array<float, 4>, auxiliary_scene::view_count> depth_hack_projection_previous_terms{};
		std::array<std::array<float, 4>, auxiliary_scene::view_count> depth_hack_projection_restored_terms{};
		std::array<float, auxiliary_scene::view_count> depth_hack_projection_near{};
		std::uint64_t model_list_pair_id{};
		std::array<std::uint32_t, auxiliary_scene::view_count> model_list_eye_origin_matches{};
		std::array<std::uint32_t, auxiliary_scene::view_count> model_list_center_origin_matches{};
		std::array<std::uint32_t, auxiliary_scene::view_count> model_list_zero_origins{};
		std::array<std::uint32_t, auxiliary_scene::view_count> model_list_other_origins{};
		std::array<std::uint32_t, auxiliary_scene::view_count> camera_model_boundary_rewrites{};
		std::array<std::uint32_t, auxiliary_scene::view_count> camera_model_boundary_foreign{};
		std::array<std::uint32_t, auxiliary_scene::view_count> camera_model_final_eye{};
		std::array<std::uint32_t, auxiliary_scene::view_count> camera_model_final_inactive{};
		std::array<std::uint32_t, auxiliary_scene::view_count> camera_model_final_other{};
		std::uint64_t pair_id{};
		std::uint64_t publication{};
		std::uintptr_t natural_record{};
		std::uintptr_t right_record{};
		std::uintptr_t context{};
		std::uint64_t device_generation{};
		std::uint32_t owner_thread_id{};
		std::uint32_t completed_eye_mask{};
		std::uint32_t final_target_id{};
		std::array<std::uintptr_t, auxiliary_scene::view_count> source_textures{};
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t format{};
		std::uint32_t mip_levels{};
		std::uint32_t array_size{};
		std::uint32_t sample_count{};
		std::uint32_t sample_quality{};
		std::uint32_t usage{};
		std::uint32_t bind_flags{};
		std::uint32_t cpu_access_flags{};
		std::uint32_t misc_flags{};
		std::array<std::uint64_t, 2> content_hashes{};
		std::array<std::uint64_t, 2> nonzero_bytes{};
		std::uint32_t query_polls{};
		HRESULT staging_results[2]{E_PENDING, E_PENDING};
		HRESULT query_result{E_PENDING};
		HRESULT query_poll_result{E_PENDING};
		HRESULT map_results[2]{E_PENDING, E_PENDING};
		HRESULT device_removed_reason{S_OK};
		bool eyes_distinct{};
	};

	// The selected scene ping-pong target retained in the real game is the HDR
	// R11G11B10_FLOAT resource. Preserve that format in the staging texture so
	// CopyResource receives an exactly compatible subresource.
	[[nodiscard]] inline bool supports_readback_source(
		const D3D11_TEXTURE2D_DESC& value) noexcept
	{
		return value.Width != 0 && value.Height != 0 && value.MipLevels == 1 &&
			value.ArraySize == 1 && value.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
			value.SampleDesc.Count == 1 &&
			value.SampleDesc.Quality == 0 && value.Usage == D3D11_USAGE_DEFAULT &&
			value.BindFlags == (D3D11_BIND_SHADER_RESOURCE |
				D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS) &&
			value.CPUAccessFlags == 0 && value.MiscFlags == 0;
	}

	// The native display pass writes the spare ping-pong target, preserving
	// the selected raw scene for H2's natural tail.
	[[nodiscard]] inline bool supports_display_source(
		const D3D11_TEXTURE2D_DESC& value) noexcept
	{
		return native_display_contract::accepts(value);
	}

	struct transaction
	{
		bool active{};
		bool production{};
		bool failed{};
		bool gpu_census_pair_active{};
		bool gpu_census_eye_active{};
		bool scene_batch_probe_pair_active{};
		bool scene_batch_probe_eye_active{};
		bool material_buffer_probe_pair_active{};
		bool particle_buffer_probe_pair_active{};
		bool ssr_consumer_probe_pair_active{};
		bool effect_timeline_pair_active{};
		bool effect_timeline_eye_active{};
		bool eye_resource_pair_active{};
		bool eye_resource_eye_active{};
		failure error{failure::none};
		std::uint32_t current_view{auxiliary_scene::no_view};
		std::uint32_t completed_eye_mask{};
		std::uint32_t owner_thread_id{};
		std::uintptr_t natural_record{};
		std::array<backend_state_registration,
			maximum_backend_states_per_transaction> backend_states{};
		std::uint32_t backend_state_count{};
		engine_stereo_binding::backend_claim claim{};
		std::array<std::uint8_t, engine_stereo_view::h2_view_slot_size> shared_culling_view{};
		engine_stereo_view::scene_record_pair records{};
		alignas(16) std::array<std::uint8_t, engine_stereo_view::h2_scene_record_size> auxiliary_record{};
		engine_stereo_view::temporal_history_preparation temporal_history{};
		std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, auxiliary_scene::view_count> sources{};
		std::array<std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2>, auxiliary_scene::view_count> scene_targets{};
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		std::uint64_t device_generation{};
		D3D11_TEXTURE2D_DESC source_description{};
		std::array<float, 3> source_primary_origin{};
		std::array<std::uint32_t, auxiliary_scene::view_count> model_list_eye_origin_matches{};
		std::array<std::uint32_t, auxiliary_scene::view_count> model_list_center_origin_matches{};
		std::array<std::uint32_t, auxiliary_scene::view_count> model_list_zero_origins{};
		std::array<std::uint32_t, auxiliary_scene::view_count> model_list_other_origins{};
		std::array<std::uint32_t, auxiliary_scene::view_count> camera_model_boundary_rewrites{};
		std::array<std::uint32_t, auxiliary_scene::view_count> camera_model_boundary_foreign{};
		std::array<std::uint32_t, auxiliary_scene::view_count> camera_model_final_eye{};
		std::array<std::uint32_t, auxiliary_scene::view_count> camera_model_final_inactive{};
		std::array<std::uint32_t, auxiliary_scene::view_count> camera_model_final_other{};
		std::array<engine_stereo_dynamic_arena::index_base_snapshot,
			maximum_dynamic_index_boundaries_per_eye> dynamic_index_left{};
		auxiliary_scene::request auxiliary;
		thermal_scene::arena_lease thermal_world_lease;
		bool thermal_world{};
		auxiliary_scene::history auxiliary_history, auxiliary_next_history;
		auxiliary_scene::image_copy auxiliary_image, pending_left_image;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> pending_left_native;
		bool auxiliary_reset{}, auxiliary_complete{}, pending_left{};
		std::uint32_t pending_left_target{};
		std::uint8_t* record(unsigned view) noexcept
		{
			if (view==0) return reinterpret_cast<std::uint8_t*>(natural_record);
			if (view==1) return records.right.data();
			return view==auxiliary_scene::view_index ? auxiliary_record.data() : nullptr;
		}
		const std::uint8_t* record(unsigned view) const noexcept
		{
			if (view==0) return reinterpret_cast<const std::uint8_t*>(natural_record);
			if (view==1) return records.right.data();
			return view==auxiliary_scene::view_index ? auxiliary_record.data() : nullptr;
		}
		engine_stereo_dynamic_upload::cycle dynamic_upload;
		void (*dynamic_upload_original)(void*){};
		engine_stereo_dynamic_arena::index_base_snapshot dynamic_index_restore{};
		std::uint32_t dynamic_index_left_count{};
		std::uint32_t dynamic_index_right_count{};
		dynamic_index_trace dynamic_index_calls;
		bool dynamic_index_scope_active{};
		bool temporal_history_current_frame_diagnostic{};
		bool material_reuse_left_diagnostic{};
		bool material_reuse_left_snapshot_ready{};
		bool material_reuse_left_failed{};
		std::array<std::uint32_t, auxiliary_scene::view_count> material_reuse_left_copy_counts{};
		Microsoft::WRL::ComPtr<ID3D11Buffer> material_reuse_left_snapshot;
		// Monotonic timestamps are transaction-local only. They are consumed at the
		// matching end boundary and cleared again when the record storage is retired.
		std::uint64_t production_transaction_timing_started_ns{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_eye_timing_started_ns{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_owner_invoke_timing_started_ns{};
		std::array<std::uint64_t, auxiliary_scene::view_count> production_dynamic_view_copy_timing_ns{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return active;
		}
	};

	// This is a one-shot GPU proof only. It renders two complete copies of the
	// already-proven outer H2 record on the game device, reads both final scene
	// images back asynchronously, and never calls OpenVR or arms production submit.
	[[nodiscard]] bool ready_to_claim() noexcept;
	// Frontend visibility may be widened only for an exact pair that the proven
	// production owner and native ring are both ready to consume.
	[[nodiscard]] frontend_culling_admission admit_frontend_culling_pair(
		std::uint64_t pair_id) noexcept;
	[[nodiscard]] bool begin(transaction& output,
		engine_stereo_binding::backend_claim&& claim, void* natural_record,
		const d3d11::device_snapshot& graphics) noexcept;
	[[nodiscard]] void* begin_view(transaction& active, std::uint32_t eye) noexcept;
	[[nodiscard]] bool finish_pending_left(transaction& active) noexcept;
	void begin_owner_invoke(transaction& active, std::uint32_t eye) noexcept;
	// Only the validated outer-owner Map-all callsite may enter this boundary.
	void dynamic_upload_boundary(void* data, void (*original)(void*));
	void end_owner_invoke(transaction& active, std::uint32_t eye) noexcept;
	// Read-only, exact-record snapshot for scoped native renderer diagnostics.
	// Never substitutes a latest publication or authorizes native/GPU writes.
	[[nodiscard]] bool snapshot_current_eye(const void* record,
		engine_stereo_view::eye_slot& output) noexcept;
	// Exact owner-thread upload boundary; this snapshot authorizes replacing
	// only the main view in the shared tessellation constant buffer.
	[[nodiscard]] bool snapshot_tessellation_view(const void* frontend,
		engine_stereo_tessellation::shared_view& output) noexcept;
	// Runtime wrapper for the proven B5D0 call boundary. Calls outside the active
	// owner/eye transaction are observation-only no-ops.
	void note_backend_view_copy(void* backend_state,
		bool scene_geometry_boundary,
		std::uintptr_t view_setup_caller = 0) noexcept;
	// Exact post-return wrapper for H2's depth-hack projection derivation. The
	// current owner, eye, backend state, source record and thread must all match.
	void note_backend_depth_hack_projection(void* backend_state) noexcept;
	void note_target_selection(transaction& active, std::uint32_t target_id,
		ID3D11DeviceContext* context, std::uint64_t previous_binding_sequence) noexcept;
	// Display callback runs only for production, under the H2 GPU mutex, with
	// this eye's record. False rejects the pair; raw HDR is never substituted.
	[[nodiscard]] bool end_view(transaction& active, std::uint32_t eye,
		bool (*display_transform)(void* record, native_display_contract::route route)) noexcept;
	void end(transaction& active) noexcept;

	void on_present_pre(const d3d11::present_event& event) noexcept;
	// Control-plane request. The owner thread consumes it before preparing the
	// next production pair, so an in-flight pair can neither republish stale
	// history nor race a reset from the console/Present thread.
	void request_temporal_history_reset() noexcept;
	// Temporary, process-local A/B diagnostics. Both default to disabled and are
	// latched only at the next owner-pair boundary, so a console toggle cannot
	// change an in-flight eye transaction.
	void set_temporal_history_current_frame_diagnostic(bool enabled) noexcept;
	void set_material_reuse_left_diagnostic(bool enabled) noexcept;
	// Persistent resource-operation observer used only by the explicitly enabled
	// 1088-byte material-buffer A/B diagnostic.
	void observe_resource_operation(
		const engine_stereo_resource_ops::event& event) noexcept;
	void invalidate_device(ID3D11DeviceContext* context,
		std::uint64_t generation) noexcept;
	[[nodiscard]] report get_report() noexcept;
	// Queue a one-shot census reset on the scene-owner thread, then capture after
	// the requested number of complete production pairs. Existing rendering is
	// never paused while the diagnostic waits.
	[[nodiscard]] bool schedule_gpu_census(std::uint32_t delay_pairs) noexcept;
	[[nodiscard]] const char* to_string(gate_state state) noexcept;
	[[nodiscard]] const char* to_string(failure value) noexcept;
	[[nodiscard]] const char* to_string(model_state_failure_stage value) noexcept;
}
