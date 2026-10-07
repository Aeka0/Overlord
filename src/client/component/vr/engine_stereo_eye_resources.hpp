#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include "native_post_aa_contract.hpp"

namespace vr::engine_stereo_eye_resources
{
	enum class role : std::uint8_t
	{
		color,
		depth,
	};

	enum class failure : std::uint8_t
	{
		none,
		hooks,
		lifecycle,
		context,
		thread,
		binding_contract,
		resource_contract,
		resource_creation,
		view_creation,
		view_capacity,
	};

	enum class exact_read_binding : std::uint8_t
	{
		null_view,
		original,
		left,
		right,
		foreign,
	};

	struct source_binding
	{
		std::uint32_t target_id{};
		role target_role{role::color};
		ID3D11View* owner_view{};
	};

	// The SSR material samples target 91 (scene color mips) through PS t13.
	// Both output eyes need persistent histories: the natural H2 path also runs
	// between admitted VR pairs and must never overwrite either eye's history.
	inline constexpr std::uint32_t isolated_target_id = 91;
	// Slot 0 is mandatory SSR history. Optional slots 1..6 are native AA
	// histories, admitted only for the active mode; scratch targets stay shared
	// because the native owner executes each view sequentially.
	inline constexpr std::size_t isolated_target_count = 1 + native_post_aa::history_targets.size();
	inline constexpr std::size_t shader_stage_count = 6;
	inline constexpr std::size_t exact_read_binding_count = 5;
	inline constexpr std::uintptr_t exact_read_draw_indexed_caller = 0x14072CF47;
	inline constexpr std::uint32_t exact_read_ps_slot = 13;
	inline constexpr std::uint32_t exact_read_output_target = 4;

	struct target_status
	{
		std::uint32_t target_id{};
		role target_role{role::color};
		std::uintptr_t owner_view{};
		std::uintptr_t original_resource{};
		std::uintptr_t left_resource{};
		std::uintptr_t right_resource{};
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t mip_levels{};
		std::uint32_t format{};
		std::uint32_t bind_flags{};
	};

	struct status
	{
		bool hooks_installed{};
		bool resources_ready{};
		bool pair_active{};
		bool pair_failed{};
		bool cleanup_quarantined{};
		bool invalidation_pending{};
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uint64_t active_pair{};
		std::uint32_t owner_thread{};
		std::uint32_t active_eye{3}; // left=0, right=1, auxiliary=2, inactive=3
		std::uintptr_t quarantine_context{};
		std::uint64_t quarantine_generation{};
		std::uint64_t quarantine_pair{};
		std::uint32_t quarantine_owner_thread{};
		failure last_failure{failure::none};
		std::uint64_t hook_failures{};
		std::uint64_t pair_attempts{};
		std::uint64_t pair_completions{};
		std::uint64_t pair_failures{};
		std::uint64_t quarantine_events{};
		std::uint64_t quarantine_rejections{};
		std::uint64_t cleanup_retries{};
		std::uint64_t cleanup_recoveries{};
		std::uint64_t deferred_invalidations{};
		std::uint64_t resource_rebuilds{};
		std::uint64_t seed_copies{};
		std::uint64_t view_creations{};
		std::uint64_t view_capacity_failures{};
		std::uint64_t boundary_rebind_attempts{};
		std::uint64_t boundary_rebind_completions{};
		std::uint64_t boundary_rebind_failures{};
		std::uint64_t boundary_rebind_calls{};
		std::uint64_t boundary_original_views_remaining{};
		std::uint64_t restore_rebind_attempts{};
		std::uint64_t restore_rebind_completions{};
		std::uint64_t restore_rebind_failures{};
		std::uint64_t restore_rebind_calls{};
		std::uint64_t restore_isolated_views_remaining{};
		std::uint64_t resource_replacements{};
		std::uint64_t render_target_replacements{};
		std::uint64_t depth_stencil_replacements{};
		std::uint64_t unordered_access_replacements{};
		std::array<std::uint64_t, shader_stage_count>
			shader_resource_replacements{};
		std::array<std::uintptr_t, shader_stage_count> shader_hook_targets{};
		std::uintptr_t unordered_access_hook_target{};
		bool exact_read_enabled{};
		std::uint64_t exact_read_pair{};
		std::uint64_t exact_read_observations{};
		std::uint64_t exact_read_expected{};
		std::uint64_t exact_read_unexpected{};
		std::array<std::array<std::uint64_t, exact_read_binding_count>, 2>
			exact_read_bindings{};
		std::uint64_t exact_read_last_pair{};
		std::uint32_t exact_read_last_eye{2};
		std::uintptr_t exact_read_last_caller{};
		std::uint32_t exact_read_last_output_target{};
		std::uintptr_t exact_read_last_view{};
		std::uintptr_t exact_read_last_resource{};
		exact_read_binding exact_read_last_binding{exact_read_binding::null_view};
		std::uint32_t exact_read_last_view_dimension{};
		std::uint32_t exact_read_last_format{};
		std::uint32_t exact_read_last_most_detailed_mip{};
		std::uint32_t exact_read_last_mip_levels{};
		std::uint32_t exact_read_last_first_array_slice{};
		std::uint32_t exact_read_last_array_size{};
		std::array<target_status, isolated_target_count> targets{};
	};

	// Installs process-wide pass-through binding hooks. Redirection is impossible
	// until an exact owner pair and one of its isolated eyes are active.
	[[nodiscard]] bool install(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	void invalidate_device(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;

	// The caller holds H2's context ownership while resolving the proven target
	// registry RTV before eye 0. This function retains the supplied view/resource
	// and prepares two persistent same-device histories. Neither eye uses the
	// natural resource after admission; the natural H2 path remains independent.
	[[nodiscard]] bool begin_pair(std::uint64_t pair_id, ID3D11Device* device,
		ID3D11DeviceContext* context, std::uint64_t device_generation,
		const std::array<source_binding, isolated_target_count>& bindings) noexcept;
	// Optional third scene consumer; never a runtime output eye.
	[[nodiscard]] bool begin_auxiliary(std::uint64_t pair_id, bool reset_history) noexcept;
	[[nodiscard]] bool end_auxiliary(std::uint64_t pair_id) noexcept;
	[[nodiscard]] bool begin_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	[[nodiscard]] bool end_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	[[nodiscard]] bool end_pair(std::uint64_t pair_id) noexcept;
	void cancel_pair(std::uint64_t pair_id) noexcept;
	// Borrowed on the active owner thread only, for initializing one eye's
	// history without ever exposing or overwriting the natural desktop image.
	[[nodiscard]] ID3D11RenderTargetView* isolated_color_view(std::uint64_t pair_id,
		std::uint32_t eye, std::uint32_t target_id) noexcept;
	[[nodiscard]] std::uint64_t active_resource_revision(std::uint64_t pair_id,
		std::uint32_t eye) noexcept;

	// The exact-read observer is armed only for an explicitly selected production
	// experiment. It reads PS slot 13 at the proven H2 DrawIndexed caller and never
	// mutates a binding or changes pair admission.
	[[nodiscard]] bool set_exact_read_proof(std::uint64_t pair_id,
		bool enabled) noexcept;
	void note_draw_indexed(ID3D11DeviceContext* context,
		std::uintptr_t caller, std::uint32_t output_target_id =
			exact_read_output_target) noexcept;

	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] const char* to_string(role value) noexcept;
	[[nodiscard]] const char* to_string(failure value) noexcept;
	[[nodiscard]] const char* to_string(exact_read_binding value) noexcept;
}
