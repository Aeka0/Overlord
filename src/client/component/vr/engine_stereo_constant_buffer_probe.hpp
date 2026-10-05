#pragma once

#include "engine_stereo_resource_ops.hpp"

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_constant_buffer_probe
{
	enum class state : std::uint8_t
	{
		idle,
		pair_active,
		eye_active,
		complete,
		failed,
	};

	enum class shader_usage : std::uint8_t
	{
		unknown,
		unused,
		used,
	};

	enum class bind_origin : std::uint8_t
	{
		unknown,
		eye_boundary,
		explicit_bind,
		explicit_null,
		clear_state,
		opaque_state_change,
	};

	enum class upload_source : std::uint8_t
	{
		unknown,
		map_unmap,
		update_subresource,
		copy_resource,
		partial_copy,
	};

	enum class shader_stage : std::uint8_t
	{
		vs,
		ps,
		gs,
		hs,
		ds,
		cs,
		count,
	};

		enum class history_tracking_client : std::uint8_t
	{
		gpu_census,
		ssr_consumer_probe,
		effect_timeline,
		count,
	};

	inline constexpr std::size_t shader_stage_count =
		static_cast<std::size_t>(shader_stage::count);
	inline constexpr std::size_t maximum_tracked_buffers = 2048;
	inline constexpr std::size_t maximum_tracked_vertex_shaders = 4096;
	inline constexpr std::size_t maximum_device_candidates = 8;
	inline constexpr std::size_t maximum_pending_maps_per_thread = 16;
	inline constexpr std::size_t maximum_draws_per_eye = 8192;
	inline constexpr std::size_t maximum_set_events_per_eye = 128;
	inline constexpr std::size_t maximum_samples = 16;
	inline constexpr std::size_t maximum_copy_source_samples = 16;
	inline constexpr std::size_t maximum_shader_bytecode_bytes = 256 * 1024;
	// Keep complete bytes for the 1088-byte CopyResource-fed material buffer and
	// H2's 2 KiB secondary global block. The 8 KiB view globals remain hash-only.
	inline constexpr std::size_t maximum_content_byte_snapshot = 2048;

	struct content_snapshot
	{
		std::uintptr_t buffer{};
		std::uint64_t device_generation{};
		std::uint64_t creation_serial{};
		std::uint64_t upload_generation{};
		std::uint64_t hash_low{};
		std::uint64_t hash_high{};
		std::uintptr_t upload_caller{};
		std::uint32_t byte_width{};
		std::uint32_t upload_thread{};
		upload_source source{upload_source::unknown};
		bool known{};
	};

	struct content_byte_snapshot
	{
		std::array<std::uint8_t, maximum_content_byte_snapshot> bytes{};
		std::uint64_t upload_generation{};
		std::uint32_t byte_width{};
		std::uint32_t captured_bytes{};
		bool complete{};
	};

	struct slot_snapshot
	{
		std::uintptr_t shader{};
		std::uint64_t shader_device_generation{};
		shader_usage usage{shader_usage::unknown};
		content_snapshot content{};
		std::uint64_t last_set_sequence{};
		std::uintptr_t last_set_caller{};
		std::uint32_t last_set_thread{};
		bind_origin origin{bind_origin::unknown};
	};

	struct set_event
	{
		std::uint64_t sequence{};
		std::uintptr_t caller{};
		std::uintptr_t buffer{};
		std::uint32_t thread{};
		bind_origin origin{bind_origin::unknown};
	};

	struct eye_report
	{
		slot_snapshot begin{};
		slot_snapshot end{};
		std::uint64_t draws{};
		std::uint64_t shader_used_draws{};
		std::uint64_t shader_unused_draws{};
		std::uint64_t shader_unknown_draws{};
		std::uint64_t setter_touches{};
		std::uint64_t explicit_binds{};
		std::uint64_t explicit_nulls{};
		std::uint64_t clear_states{};
		std::uint64_t opaque_state_changes{};
		std::array<set_event, maximum_set_events_per_eye> set_events{};
		std::size_t set_event_count{};
		std::uint64_t set_event_overflows{};
	};

	struct comparison_sample
	{
		std::uint64_t ordinal{};
		// Retained as the right-eye call site for diagnostics compatibility.
		std::uintptr_t caller{};
		std::uintptr_t left_caller{};
		std::uintptr_t right_caller{};
		slot_snapshot left{};
		slot_snapshot right{};
	};

	// Bounded evidence for H2's 1088-byte material constant-buffer upload path.
	// This is deliberately descriptive only: no resource is mapped, copied or
	// otherwise mutated by the diagnostic.
	struct copy_source_sample
	{
		std::uintptr_t destination{};
		std::uintptr_t source{};
		std::uintptr_t copy_caller{};
		std::uintptr_t source_upload_caller{};
		std::uint64_t device_generation{};
		std::uint64_t destination_creation_serial{};
		std::uint64_t source_creation_serial{};
		std::uint64_t source_upload_generation{};
		std::uint64_t observations{};
		std::uint64_t registered_observations{};
		std::uint64_t known_observations{};
		std::uint64_t byte_snapshot_observations{};
		std::uint64_t null_source_observations{};
		std::uint64_t non_buffer_observations{};
		std::uint64_t size_mismatch_observations{};
		std::uint64_t registration_failure_observations{};
		std::uint64_t unknown_content_observations{};
		std::uint64_t byte_snapshot_failure_observations{};
		std::uint32_t copy_thread{};
		std::uint32_t source_upload_thread{};
		std::uint32_t destination_byte_width{};
		std::uint32_t source_dimension{};
		std::uint32_t source_byte_width{};
		std::uint32_t source_usage{};
		std::uint32_t source_bind_flags{};
		std::uint32_t source_cpu_access_flags{};
		std::uint32_t source_misc_flags{};
		std::uint32_t source_structure_stride{};
		upload_source source_content_origin{upload_source::unknown};
	};

	struct report
	{
		state current_state{state::idle};
		bool hooks_installed{};
		bool history_tracking_enabled{};
		std::uint32_t history_tracking_clients{};
		std::uintptr_t expected_device{};
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uint64_t registered_device_candidates{};
		std::uint64_t candidate_registration_overflows{};
		std::uint64_t unregistered_context_calls{};
		bool selected_resource_history_complete{};
		std::uint64_t pair_id{};
		std::uint8_t completed_eye_mask{};
		std::uint32_t owner_thread{};
		std::array<std::uintptr_t, shader_stage_count> set_hook_targets{};
		std::uintptr_t map_hook_target{};
		std::uintptr_t unmap_hook_target{};
		std::array<std::uint64_t, shader_stage_count> set_calls{};
		std::uint64_t hook_failures{};
		std::uint64_t callback_quiescence_timeouts{};
		std::uint64_t foreign_context_calls{};
		std::uint64_t foreign_thread_pair_calls{};
		std::uint64_t map_calls{};
		std::uint64_t unmap_calls{};
		std::uint64_t mapped_uploads{};
		std::uint64_t unmatched_unmaps{};
		std::uint64_t pending_map_overflows{};
		std::uint64_t tracked_buffer_overflows{};
		std::uint64_t tracked_shader_overflows{};
		std::uint64_t buffer_identity_tag_failures{};
		std::uint64_t shader_identity_tag_failures{};
		std::uint64_t buffer_identity_reuses{};
		std::uint64_t shader_identity_reuses{};
		std::uint64_t concurrent_update_drops{};
		std::uint64_t unstable_content_reads{};
		std::uint64_t shader_private_data_missing{};
		std::uint64_t shader_private_data_oversized{};
		std::uint64_t shader_reflection_failures{};
		std::uint64_t resource_updates{};
		std::uint64_t resource_copies{};
		std::uint64_t resource_unknown_writes{};
		std::uint64_t copy_source_candidates{};
		std::uint64_t copy_source_registered{};
		std::uint64_t copy_source_known{};
		std::uint64_t copy_source_bytes_propagated{};
		std::array<copy_source_sample, maximum_copy_source_samples>
			copy_source_samples{};
		std::size_t copy_source_sample_count{};
		std::uint64_t copy_source_sample_overflows{};
		std::uint64_t comparisons{};
		std::uint64_t used_left_bound_right_null{};
		std::uint64_t unused_left_bound_right_null{};
		std::uint64_t unknown_usage_left_bound_right_null{};
		std::uint64_t both_bound_same_content{};
		std::uint64_t both_bound_different_content{};
		std::uint64_t both_bound_content_unknown{};
		std::uint64_t same_identity_new_generation{};
		std::uint64_t provenance_unknown{};
		std::uint64_t ordinal_mismatches{};
		std::uint64_t draw_overflows{};
		std::array<eye_report, 2> eyes{};
		std::array<comparison_sample, maximum_samples> samples{};
		std::size_t sample_count{};
	};

	// Install the process-wide pass-through hooks as soon as H2's device and
	// immediate context are known. Shader usage is resolved lazily from the
	// bytecode private data already attached by component/d3d11.
	[[nodiscard]] bool install_early(ID3D11Device* device,
		ID3D11DeviceContext* context, std::uint64_t device_generation) noexcept;
	[[nodiscard]] bool select_device(ID3D11Device* device,
		ID3D11DeviceContext* context, std::uint64_t device_generation) noexcept;
	// The detours stay installed for the production payload observer, while the
	// expensive provenance tables and byte hashing run only during an explicitly
	// requested GPU-census pre-roll/capture.
	void set_history_tracking_enabled(bool enabled) noexcept;
	// Independent diagnostic leases keep one bounded observer from closing the
	// shared upload-history gate while another observer is still active.
	void set_history_tracking_client(history_tracking_client client,
		bool enabled) noexcept;
	// Set by the owner of resource_ops' persistent content observer. Attaching
	// after a candidate was registered cannot retroactively close its history gap.
	void set_resource_observer_attached(bool attached) noexcept;
	void invalidate_device(ID3D11Device* device, ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;

	[[nodiscard]] bool begin_pair(std::uint64_t pair_id,
		ID3D11DeviceContext* context, std::uint32_t owner_thread) noexcept;
	[[nodiscard]] bool begin_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	void observe_vs_draw(ID3D11DeviceContext* context, std::uint64_t ordinal,
		std::uintptr_t caller, ID3D11VertexShader* shader,
		ID3D11Buffer* slot3_buffer) noexcept;
	[[nodiscard]] bool end_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	[[nodiscard]] bool end_pair(std::uint64_t pair_id) noexcept;
	[[nodiscard]] bool abort_pair(std::uint64_t pair_id) noexcept;

	// These entry points are intentionally separate from the hook installation.
	// Existing resource/output-merger hooks can forward their exact observations
	// without installing a second detour on the same D3D11 method.
	void observe_resource_operation(
		const engine_stereo_resource_ops::event& event) noexcept;
	void observe_update_content(ID3D11DeviceContext* context,
		ID3D11Resource* destination, const void* source_data,
		std::size_t source_size, bool complete_resource,
		std::uintptr_t caller) noexcept;
	void observe_clear_state(ID3D11DeviceContext* context,
		std::uintptr_t caller) noexcept;
	void observe_opaque_state_change(ID3D11DeviceContext* context,
		ID3D11CommandList* commands, bool restore_context_state,
		std::uintptr_t caller) noexcept;
	// Returns the latest exact CPU upload provenance known for this constant
	// buffer. A registered buffer with no complete observed upload is returned as
	// an unknown snapshot rather than being guessed from GPU state.
	[[nodiscard]] bool query_content_snapshot(ID3D11Buffer* buffer,
		content_snapshot& output) noexcept;
	// Returns bytes from the exact upload generation named by content_snapshot.
	// The call fails rather than returning a newer upload of the same D3D object.
	[[nodiscard]] bool query_content_bytes(ID3D11Buffer* buffer,
		std::uint64_t upload_generation, content_byte_snapshot& output) noexcept;

	// Caller-owned storage avoids a second large report object in the return
	// path. This API is intentionally not offered by value.
	void get_report(report& output) noexcept;
	[[nodiscard]] const char* to_string(state value) noexcept;
	[[nodiscard]] const char* to_string(shader_usage value) noexcept;
	[[nodiscard]] const char* to_string(bind_origin value) noexcept;
	[[nodiscard]] const char* to_string(upload_source value) noexcept;
}
