#include <std_include.hpp>
#include "diagnostics/screen_display.hpp"
#include "component/vr/native_render_contract.hpp"

#include "engine_stereo_owner_pass.hpp"
#include "engine_scene_resolution.hpp"
#include "diagnostics.hpp"
#include "region_capture.hpp"
#include "native_post_aa.hpp"
#include "engine_scene_job_capture.hpp"
#include "engine_scene_completion.hpp"

#include "engine_backend_probe.hpp"
#include "engine_stereo_constant_buffer_probe.hpp"
#include "engine_stereo_dynamic_arena.hpp"
#include "engine_stereo_eye_resources.hpp"
#include "engine_stereo_effect_timeline.hpp"
#include "engine_stereo_execution.hpp"
#include "engine_stereo_gpu_census.hpp"
#include "engine_stereo_material_buffer_probe.hpp"
#include "engine_stereo_particle_buffer_probe.hpp"
#include "engine_stereo_resource_ops.hpp"
#include "engine_stereo_scene_batch_probe.hpp"
#include "engine_stereo_ssr_history_probe.hpp"
#include "engine_stereo_ssr_consumer_probe.hpp"
#include "native_render_session.hpp"
#include "game/assets.hpp"


#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>

namespace vr::engine_stereo_owner_pass
{
	namespace
	{
		constexpr std::uint32_t maximum_query_polls = 240;
		constexpr std::uintptr_t h2_gpu_context_mutex_handle = 0x140C05740;
		constexpr DWORD h2_gpu_context_mutex_timeout_ms = 100;
		constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
		constexpr std::uint64_t fnv_prime = 1099511628211ull;
		constexpr std::uintptr_t h2_material_buffer_copy_caller = 0x1403591BD;
		constexpr std::uint32_t h2_material_buffer_bytes = 1088;

		struct gpu_resources
		{
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2> staging;
			Microsoft::WRL::ComPtr<ID3D11Query> query;
			D3D11_TEXTURE2D_DESC description{};
			std::uint64_t generation{};
		};

		std::mutex state_mutex;
		std::mutex dynamic_upload_mutex;
		engine_stereo_dynamic_upload::report dynamic_upload_evidence;
		gpu_resources resources;
		report evidence;
		std::atomic<gate_state> state{gate_state::waiting};
		std::atomic<failure> error{failure::none};
		std::atomic_uint64_t attempts{};
		std::atomic_uint64_t completions{};
		std::atomic_uint64_t failures{};
		std::atomic_uint64_t production_attempts{};
		std::atomic_uint64_t production_completions{};
		std::atomic_uint64_t production_failures{};
		std::atomic_uint64_t production_eye_copies{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> display_routes{};
		std::atomic_uint64_t display_transform_completions{};
		std::atomic_uint64_t display_transform_failures{};
		std::atomic<const char*> display_transform_error{"none"};
		std::atomic_uint32_t display_format{};
		std::atomic_uint32_t display_bind_flags{};
		std::atomic_uint64_t production_transaction_timing_samples{};
		std::atomic_uint64_t production_transaction_timing_last_us{};
		std::atomic_uint64_t production_transaction_timing_max_us{};
		std::atomic_uint64_t production_transaction_timing_total_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_eye_timing_samples{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_eye_timing_last_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_eye_timing_max_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_eye_timing_total_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_owner_invoke_timing_samples{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_owner_invoke_timing_last_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_owner_invoke_timing_max_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_owner_invoke_timing_total_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_dynamic_view_copy_timing_samples{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_dynamic_view_copy_timing_last_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_dynamic_view_copy_timing_max_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_dynamic_view_copy_timing_total_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_native_copy_timing_samples{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_native_copy_timing_last_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_native_copy_timing_max_us{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> production_native_copy_timing_total_us{};
		std::atomic_uint64_t production_dynamic_restore_timing_samples{};
		std::atomic_uint64_t production_dynamic_restore_timing_last_us{};
		std::atomic_uint64_t production_dynamic_restore_timing_max_us{};
		std::atomic_uint64_t production_dynamic_restore_timing_total_us{};
		std::atomic_uint64_t temporal_history_preparations{};
		std::atomic_uint64_t temporal_history_seeds{};
		std::atomic_uint64_t temporal_history_commits{};
		std::atomic_uint64_t temporal_history_failures{};
		std::atomic_uint64_t temporal_history_reset_requests{};
		std::atomic_uint64_t temporal_history_reset_applications{};
		std::atomic_bool temporal_history_reset_pending{};
		std::atomic_bool temporal_history_current_frame_diagnostic{};
		std::atomic_uint64_t temporal_history_current_frame_pairs{};
		std::mutex temporal_history_mutex;
		engine_stereo_view::temporal_history_state temporal_history_state{};
		std::atomic_bool material_reuse_left_diagnostic{};
		std::atomic_uint64_t material_reuse_left_pairs{};
		std::atomic_uint64_t material_reuse_left_captures{};
		std::atomic_uint64_t material_reuse_left_replacements{};
		std::atomic_uint64_t material_reuse_left_failures{};
		std::atomic_uint64_t material_reuse_left_last_pair{};
		std::atomic_uint64_t production_context_lock_acquires{};
		std::atomic_uint64_t production_context_lock_failures{};
		std::atomic_uint64_t production_context_lock_wait_total_us{};
		std::atomic_uint64_t production_context_lock_wait_max_us{};
		std::atomic_uint64_t production_context_lock_wait_last_us{};
		std::atomic_uint32_t production_context_lock_last_result{WAIT_FAILED};
		std::atomic_uint64_t dynamic_index_captures{};
		std::atomic_uint64_t dynamic_index_rebases{};
		std::atomic_uint64_t dynamic_index_restores{};
		std::atomic_uint64_t dynamic_index_validations{};
		std::atomic_uint64_t dynamic_index_failures{};
		std::atomic_uintptr_t dynamic_index_data_identity{};
		std::atomic_uint32_t dynamic_index_last_left_boundaries{};
		std::atomic_uint32_t dynamic_index_last_right_boundaries{};
		std::atomic_uint32_t dynamic_index_last_failure{};
		std::atomic_uint64_t model_state_sync_calls{};
		std::atomic_uint64_t model_state_sync_matches{};
		std::atomic_uint64_t model_state_foreign_bypasses{};
		std::atomic_uint64_t model_state_contract_mismatches{};
		std::atomic_uint64_t model_state_backend_registrations{};
		std::atomic_uint64_t model_state_backend_reuses{};
		std::atomic_uint64_t model_state_backend_maximum_active{};
		std::atomic_uint32_t model_state_last_failure_stage{};
		std::atomic_uintptr_t model_state_failure_backend{};
		std::atomic_uintptr_t model_state_failure_expected_backend{};
		std::atomic_uint32_t model_state_failure_thread{};
		std::atomic_uint32_t model_state_failure_expected_thread{};
		std::atomic_uint32_t model_state_last_failure{};
		std::atomic_uint32_t model_state_last_failure_eye{2};
		std::atomic_uintptr_t model_state_failure_primary_source{};
		std::atomic_uintptr_t model_state_failure_expected_primary_source{};
		std::atomic_uintptr_t model_state_failure_rebase_source{};
		std::atomic_uintptr_t model_state_failure_expected_rebase_source{};
		std::array<std::atomic_uint32_t, 3> model_state_failure_primary_origin_bits{};
		std::array<std::atomic_uint32_t, 3>
			model_state_failure_expected_primary_origin_bits{};
		std::array<std::atomic_uint32_t, 3>
			model_state_failure_relative_eye_offset_bits{};
		std::array<std::atomic_uint32_t, 3>
			model_state_failure_expected_relative_eye_offset_bits{};
		std::atomic_uint64_t model_cache_invalidations{};
		std::atomic_uint64_t model_cache_already_empty{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> model_state_eye_matches{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> model_cache_eye_invalidations{};
		std::array<std::atomic_uintptr_t, auxiliary_scene::view_count> model_cache_last_values{};
		std::array<std::array<std::atomic_uint32_t, 3>, auxiliary_scene::view_count> model_primary_origin_bits{};
		std::array<std::array<std::atomic_uint32_t, 3>, auxiliary_scene::view_count>
			model_relative_eye_offset_bits{};
		std::atomic_uint64_t depth_hack_projection_calls{};
		std::atomic_uint64_t depth_hack_projection_restores{};
		std::atomic_uint64_t depth_hack_projection_bypasses{};
		std::atomic_uint64_t depth_hack_projection_foreign_bypasses{};
		std::atomic_uint64_t depth_hack_projection_contract_mismatches{};
		std::array<std::atomic_uint64_t, auxiliary_scene::view_count> depth_hack_projection_eye_restores{};
		std::array<std::array<std::atomic_uint32_t, 4>, auxiliary_scene::view_count>
			depth_hack_projection_previous_bits{};
		std::array<std::array<std::atomic_uint32_t, 4>, auxiliary_scene::view_count>
			depth_hack_projection_restored_bits{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> depth_hack_projection_near_bits{};
		std::atomic_uint64_t model_list_pair_id{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> model_list_eye_origin_matches{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> model_list_center_origin_matches{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> model_list_zero_origins{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> model_list_other_origins{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> camera_model_boundary_rewrites{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> camera_model_boundary_foreign{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> camera_model_final_eye{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> camera_model_final_inactive{};
		std::array<std::atomic_uint32_t, auxiliary_scene::view_count> camera_model_final_other{};
		std::atomic_bool production_active{};
		std::atomic_bool gpu_census_launch_attempted{};
		// High 32 bits are a monotonic request sequence; low 32 bits are the exact
		// requested delay. Publishing both in one release operation prevents a
		// control-thread arm from being paired with another request's delay.
		std::atomic_uint64_t gpu_census_request_token{};
		std::atomic_uint64_t gpu_census_applied_token{};
		std::atomic_bool gpu_census_rearm_applying{};
		std::atomic_uint32_t gpu_census_delay_remaining{};
		std::atomic_uint64_t gpu_census_reset_attempts{};
		std::atomic_uint64_t gpu_census_reset_failures{};
		std::atomic_uint64_t gpu_census_launch_attempts{};
		std::atomic_uint64_t gpu_census_launch_failures{};
		std::atomic_uint64_t gpu_census_last_capture_pair{};
		thread_local transaction* active_transaction{};
		thread_local bool material_reuse_left_reentrant{};

		constexpr auto make_model_list_origin_offsets() noexcept
		{
			std::array<std::size_t,
				engine_stereo_view::h2_draw_list_descriptor_count> output{};
			for (std::size_t index{}; index < output.size(); ++index)
			{
				output[index] = engine_stereo_view::h2_draw_list_descriptor_base +
					index * engine_stereo_view::h2_draw_list_descriptor_size +
					engine_stereo_view::h2_draw_list_origin_offset;
			}
			return output;
		}

		constexpr auto model_list_origin_offsets = make_model_list_origin_offsets();
		constexpr std::size_t origin_size = 3 * sizeof(float);
		static_assert(model_list_origin_offsets.back() + origin_size <=
			engine_stereo_view::h2_scene_record_size);

		std::uint32_t float_bits(const float value) noexcept
		{
			std::uint32_t bits{};
			std::memcpy(&bits, &value, sizeof(bits));
			return bits;
		}

		float float_from_bits(const std::uint32_t bits) noexcept
		{
			float value{};
			std::memcpy(&value, &bits, sizeof(value));
			return value;
		}

		struct model_list_origin_census
		{
			std::uint32_t eye_matches{};
			std::uint32_t center_matches{};
			std::uint32_t zeros{};
			std::uint32_t other{};
		};

		model_list_origin_census census_model_list_origins(
			const std::uint8_t* const record,
			const std::array<float, 3>& center_origin) noexcept
		{
			model_list_origin_census output{};
			if (record == nullptr) return output;
			std::array<float, 3> eye_origin{};
			std::memcpy(eye_origin.data(), record +
				engine_stereo_view::h2_view_origin_offset, origin_size);
			constexpr std::array<float, 3> zero_origin{};
			for (const auto offset : model_list_origin_offsets)
			{
				const auto* const origin = record + offset;
				if (std::memcmp(origin, eye_origin.data(), origin_size) == 0)
				{
					++output.eye_matches;
				}
				else if (std::memcmp(origin, center_origin.data(), origin_size) == 0)
				{
					++output.center_matches;
				}
				else if (std::memcmp(origin, zero_origin.data(), origin_size) == 0)
				{
					++output.zeros;
				}
				else
				{
					++output.other;
				}
			}
			return output;
		}

		void publish_model_list_census(const transaction& active) noexcept
		{
			if (active.completed_eye_mask != 0x3 || active.records.pair_id == 0) return;
			for (std::size_t eye{}; eye < auxiliary_scene::view_count; ++eye)
			{
				model_list_eye_origin_matches[eye].store(
					active.model_list_eye_origin_matches[eye], std::memory_order_relaxed);
				model_list_center_origin_matches[eye].store(
					active.model_list_center_origin_matches[eye], std::memory_order_relaxed);
				model_list_zero_origins[eye].store(active.model_list_zero_origins[eye],
					std::memory_order_relaxed);
				model_list_other_origins[eye].store(active.model_list_other_origins[eye],
					std::memory_order_relaxed);
				camera_model_boundary_rewrites[eye].store(
					active.camera_model_boundary_rewrites[eye],
					std::memory_order_relaxed);
				camera_model_boundary_foreign[eye].store(
					active.camera_model_boundary_foreign[eye],
					std::memory_order_relaxed);
				camera_model_final_eye[eye].store(active.camera_model_final_eye[eye],
					std::memory_order_relaxed);
				camera_model_final_inactive[eye].store(
					active.camera_model_final_inactive[eye],
					std::memory_order_relaxed);
				camera_model_final_other[eye].store(active.camera_model_final_other[eye],
					std::memory_order_relaxed);
			}
			model_list_pair_id.store(active.records.pair_id, std::memory_order_release);
		}

		void update_maximum(std::atomic_uint64_t& target,
			const std::uint64_t candidate) noexcept
		{
			auto current = target.load(std::memory_order_relaxed);
			while (current < candidate && !target.compare_exchange_weak(current,
				candidate, std::memory_order_release, std::memory_order_relaxed))
			{
			}
		}

		[[nodiscard]] std::uint64_t timing_now_ns() noexcept
		{
			const auto count = std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return count > 0 ? static_cast<std::uint64_t>(count) : 1;
		}

		void record_timing(std::atomic_uint64_t& samples,
			std::atomic_uint64_t& last_us, std::atomic_uint64_t& maximum_us,
			std::atomic_uint64_t& total_us, const std::uint64_t started_ns) noexcept
		{
			if (started_ns == 0) return;
			const auto completed_ns = timing_now_ns();
			const auto elapsed_us = completed_ns > started_ns ?
				(completed_ns - started_ns) / 1000u : 0u;
			total_us.fetch_add(elapsed_us, std::memory_order_relaxed);
			update_maximum(maximum_us, elapsed_us);
			last_us.store(elapsed_us, std::memory_order_relaxed);
			samples.fetch_add(1, std::memory_order_release);
		}

		void record_elapsed_timing(std::atomic_uint64_t& samples,
			std::atomic_uint64_t& last_us, std::atomic_uint64_t& maximum_us,
			std::atomic_uint64_t& total_us, const std::uint64_t elapsed_ns) noexcept
		{
			const auto elapsed_us = elapsed_ns / 1000u;
			total_us.fetch_add(elapsed_us, std::memory_order_relaxed);
			update_maximum(maximum_us, elapsed_us);
			last_us.store(elapsed_us, std::memory_order_relaxed);
			samples.fetch_add(1, std::memory_order_release);
		}

		class h2_gpu_context_lock final
		{
		public:
			h2_gpu_context_lock() noexcept
			{
				handle_ = *reinterpret_cast<HANDLE*>(h2_gpu_context_mutex_handle);
				if (handle_ == nullptr) return;
				const auto started = std::chrono::steady_clock::now();
				result_ = WaitForSingleObject(handle_, h2_gpu_context_mutex_timeout_ms);
				wait_us_ = static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::microseconds>(
						std::chrono::steady_clock::now() - started).count());
				acquired_ = result_ == WAIT_OBJECT_0 || result_ == WAIT_ABANDONED;
			}

			~h2_gpu_context_lock()
			{
				if (acquired_) ReleaseMutex(handle_);
			}

			h2_gpu_context_lock(const h2_gpu_context_lock&) = delete;
			h2_gpu_context_lock& operator=(const h2_gpu_context_lock&) = delete;

			[[nodiscard]] explicit operator bool() const noexcept
			{
				return result_ == WAIT_OBJECT_0;
			}

			[[nodiscard]] DWORD result() const noexcept { return result_; }
			[[nodiscard]] std::uint64_t wait_us() const noexcept { return wait_us_; }

		private:
			HANDLE handle_{};
			DWORD result_{WAIT_FAILED};
			std::uint64_t wait_us_{};
			bool acquired_{};
		};

		void fail_transaction(transaction& active, const failure value) noexcept
		{
			active.failed = true;
			if (active.error == failure::none) active.error = value;
		}

		void publish_failure(const transaction& active) noexcept
		{
			region_capture::event(region_capture::kind::failure, active.records.pair_id,
				active.current_view, static_cast<std::uint64_t>(active.error), active.completed_eye_mask);
			// Bootstrap can fail before transport is armed too. Preserve the same
			// failure-only evidence in both paths before transaction retirement.
			std::lock_guard lock(state_mutex);
			evidence.last_failure = active.error;
			evidence.failure_in_production = active.production;
			evidence.failure_pair = active.records.pair_id;
			evidence.failure_eye = active.current_view;
			evidence.failure_completed_mask = active.completed_eye_mask;
			evidence.failure_model_other = active.camera_model_final_other;
			evidence.dynamic_index_failure_trace = active.dynamic_index_calls;
		}

		void finish_dynamic_upload(transaction& active) noexcept
		{
			auto& upload = active.dynamic_upload;
			if (upload.take_deferred(GetCurrentThreadId()))
			{
				// The right owner was never entered or returned before its Map-all.
				// Preserve H2's next-frame upload, using the original helper (including
				// all nine descriptors and H2's own recursive device mutex).
				diagnostics::record_trace(diagnostics::trace_event::dynamic_upload_recover,
					active.records.pair_id, upload.data);
				active.dynamic_upload_original(reinterpret_cast<void*>(upload.data));
				upload.returned();
				diagnostics::record_trace(diagnostics::trace_event::dynamic_upload_return,
					active.records.pair_id, upload.data);
			}
			if (upload.left_calls != 1 || upload.right_calls != 1 ||
				upload.state != engine_stereo_dynamic_upload::phase::advanced)
			{
				upload.fail(engine_stereo_dynamic_upload::failure::missing_boundary);
			}
			if (upload.error != engine_stereo_dynamic_upload::failure::none)
			{
				fail_transaction(active, failure::dynamic_mesh);
			}
			std::lock_guard lock(dynamic_upload_mutex);
			auto& status = dynamic_upload_evidence;
			++status.pairs;
			status.deferred += upload.left_calls != 0;
			status.advances += upload.state == engine_stereo_dynamic_upload::phase::advanced;
			status.recoveries += upload.recovered;
			status.last_pair = active.records.pair_id;
			status.last = upload;
			if (upload.error == engine_stereo_dynamic_upload::failure::none)
			{
				++status.complete;
			}
			else
			{
				++status.failures;
				status.last_failure_pair = active.records.pair_id;
				status.last_failure = upload;
			}
		}

		void fail_dynamic_index(transaction& active,
			const engine_stereo_dynamic_arena::failure value) noexcept
		{
			dynamic_index_last_failure.store(static_cast<std::uint32_t>(value),
				std::memory_order_release);
			dynamic_index_failures.fetch_add(1, std::memory_order_relaxed);
			fail_transaction(active, failure::dynamic_mesh);
		}

		void publish_dynamic_index_boundary_counts(const transaction& active) noexcept
		{
			dynamic_index_last_left_boundaries.store(active.dynamic_index_left_count,
				std::memory_order_release);
			dynamic_index_last_right_boundaries.store(active.dynamic_index_right_count,
				std::memory_order_release);
		}

		[[nodiscard]] bool restore_dynamic_index_scope(transaction& active) noexcept
		{
			if (!active.dynamic_index_scope_active) return true;
			engine_stereo_dynamic_arena::failure restore_error{};
			const auto restored = engine_stereo_dynamic_arena::restore(
				active.dynamic_index_restore, restore_error);
			if (!restored)
			{
				fail_dynamic_index(active, restore_error);
				return false;
			}
			active.dynamic_index_scope_active = false;
			active.dynamic_index_restore = {};
			dynamic_index_restores.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		void record_model_state_failure(const transaction& active,
			const model_state_failure_stage stage, const void* const backend_state) noexcept
		{
			model_state_failure_backend.store(
				reinterpret_cast<std::uintptr_t>(backend_state), std::memory_order_relaxed);
			model_state_failure_expected_backend.store(active.backend_state_count != 0 ?
				active.backend_states[0].identity : 0,
				std::memory_order_relaxed);
			model_state_failure_thread.store(GetCurrentThreadId(),
				std::memory_order_relaxed);
			model_state_failure_expected_thread.store(active.owner_thread_id,
				std::memory_order_relaxed);
			model_state_last_failure_eye.store(active.current_view,
				std::memory_order_relaxed);
			model_state_last_failure_stage.store(static_cast<std::uint32_t>(stage),
				std::memory_order_release);
		}

		bool register_backend_state(transaction& active,
			const std::uintptr_t identity, const std::uint32_t eye) noexcept
		{
			if (identity == 0 || eye >= auxiliary_scene::view_count) return false;
			for (std::size_t index{}; index < active.backend_state_count; ++index)
			{
				auto& entry = active.backend_states[index];
				if (entry.identity != identity) continue;
				entry.eye_mask |= static_cast<std::uint8_t>(1u << eye);
				model_state_backend_reuses.fetch_add(1, std::memory_order_relaxed);
				return true;
			}
			if (active.backend_state_count >= active.backend_states.size()) return false;
			auto& entry = active.backend_states[active.backend_state_count++];
			entry.identity = identity;
			entry.eye_mask = static_cast<std::uint8_t>(1u << eye);
			model_state_backend_registrations.fetch_add(1, std::memory_order_relaxed);
			update_maximum(model_state_backend_maximum_active,
				active.backend_state_count);
			return true;
		}

		bool backend_state_authorized(const transaction& active,
			const std::uintptr_t identity, const std::uint32_t eye) noexcept
		{
			if (identity == 0 || eye >= auxiliary_scene::view_count) return false;
			const auto eye_bit = static_cast<std::uint8_t>(1u << eye);
			for (std::size_t index{}; index < active.backend_state_count; ++index)
			{
				const auto& entry = active.backend_states[index];
				if (entry.identity == identity && (entry.eye_mask & eye_bit) != 0)
					return true;
			}
			return false;
		}

		void retire_transaction_preserving_records(transaction& active) noexcept
		{
			// Backend states may retain +0x3238/+0x3240 until the next native owner
			// installs a record. Keep the private right-eye bytes alive across that
			// interval; the next begin() replaces them before its own owner call.
			active.active = false;
			active.production = false;
			active.failed = false;
			active.gpu_census_pair_active = false;
			active.gpu_census_eye_active = false;
			active.ssr_consumer_probe_pair_active = false;
			active.effect_timeline_pair_active = false;
			active.effect_timeline_eye_active = false;
			active.error = failure::none;
			active.current_view = auxiliary_scene::no_view;
			active.completed_eye_mask = 0;
			active.owner_thread_id = 0;
			active.natural_record = 0;
			active.backend_states = {};
			active.backend_state_count = 0;
			active.claim = {};
			active.temporal_history = {};
			active.auxiliary={};active.auxiliary_next_history={};
			active.thermal_world=false;
			active.auxiliary_complete=false;active.pending_left=false;
			active.pending_left_target=0;active.auxiliary_reset=false;
			active.pending_left_native.Reset();
			active.sources = {};
			active.context.Reset();
			active.device_generation = 0;
			active.source_description = {};
			active.source_primary_origin = {};
			active.model_list_eye_origin_matches = {};
			active.model_list_center_origin_matches = {};
			active.model_list_zero_origins = {};
			active.model_list_other_origins = {};
			active.camera_model_boundary_rewrites = {};
			active.camera_model_boundary_foreign = {};
			active.camera_model_final_eye = {};
			active.camera_model_final_inactive = {};
			active.camera_model_final_other = {};
			active.dynamic_index_left = {};
			active.dynamic_upload = {};
			active.dynamic_upload_original = nullptr;
			active.dynamic_index_restore = {};
			active.dynamic_index_left_count = 0;
			active.dynamic_index_right_count = 0;
			active.dynamic_index_calls = {};
			active.dynamic_index_scope_active = false;
			active.temporal_history_current_frame_diagnostic = false;
			active.material_reuse_left_diagnostic = false;
			active.material_reuse_left_snapshot_ready = false;
			active.material_reuse_left_failed = false;
			active.material_reuse_left_copy_counts = {};
			active.material_reuse_left_snapshot.Reset();
			active.production_transaction_timing_started_ns = 0;
			active.production_eye_timing_started_ns = {};
			active.production_owner_invoke_timing_started_ns = {};
			active.production_dynamic_view_copy_timing_ns = {};
		}

		void fail_global(const failure value) noexcept
		{
			error.store(value, std::memory_order_release);
			state.store(gate_state::failed, std::memory_order_release);
			failures.fetch_add(1, std::memory_order_relaxed);
			std::lock_guard lock(state_mutex);
			evidence.state = gate_state::failed;
			evidence.error = value;
			if (resources.device)
			{
				evidence.device_removed_reason = resources.device->GetDeviceRemovedReason();
			}
		}

		void fail_global_locked(const failure value) noexcept
		{
			error.store(value, std::memory_order_release);
			state.store(gate_state::failed, std::memory_order_release);
			failures.fetch_add(1, std::memory_order_relaxed);
			evidence.state = gate_state::failed;
			evidence.error = value;
			if (resources.device)
			{
				evidence.device_removed_reason = resources.device->GetDeviceRemovedReason();
			}
		}

		bool strict_execution_contract_ready() noexcept
		{
			const auto value = engine_stereo_execution::get_status();
			return value.state == engine_stereo_execution::gate_state::complete &&
				value.error == engine_stereo_execution::failure::none &&
				value.recorded_events != 0 && value.scene_owner_scoped_frame_events != 0 &&
				value.distinct_scene_owner_records == 1 &&
				value.scene_owner_thread_mismatches == 0 &&
				value.foreign_context_events == 0 && value.opaque_execute_command_lists == 0 &&
				!value.deferred_execution_opaque && !value.identity_truncated;
		}

		bool create_readback_resources(const D3D11_TEXTURE2D_DESC& source) noexcept
		{
			if (!resources.device || !supports_readback_source(source)) return false;
			resources.description = source;
			auto staging = source;
			staging.Usage = D3D11_USAGE_STAGING;
			staging.BindFlags = 0;
			staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			staging.MiscFlags = 0;
			for (std::size_t eye{}; eye < resources.staging.size(); ++eye)
			{
				const auto result = resources.device->CreateTexture2D(
					&staging, nullptr, &resources.staging[eye]);
				{
					std::lock_guard lock(state_mutex);
					evidence.staging_results[eye] = result;
				}
				if (FAILED(result)) return false;
			}
			D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
			const auto result = resources.device->CreateQuery(&query, &resources.query);
			{
				std::lock_guard lock(state_mutex);
				evidence.query_result = result;
			}
			return SUCCEEDED(result) && resources.query != nullptr;
		}

		bool copy_source_to_staging(const std::uint32_t eye,
			ID3D11Texture2D* const source) noexcept
		{
			if (eye >= resources.staging.size() || source == nullptr ||
				!resources.context || !resources.staging[eye])
			{
				return false;
			}

			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> current_view;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> current_depth;
			resources.context->OMGetRenderTargets(1, &current_view, &current_depth);
			bool source_bound{};
			if (current_view)
			{
				Microsoft::WRL::ComPtr<ID3D11Resource> current_resource;
				current_view->GetResource(&current_resource);
				source_bound = current_resource.Get() == source;
			}
			if (source_bound) resources.context->OMSetRenderTargets(0, nullptr, nullptr);
			resources.context->CopyResource(resources.staging[eye].Get(), source);
			if (source_bound)
			{
				auto* const view = current_view.Get();
				resources.context->OMSetRenderTargets(1, &view, current_depth.Get());
			}
			return true;
		}

		bool hash_staging(ID3D11Texture2D* const texture, HRESULT& map_result,
			std::uint64_t& hash, std::uint64_t& nonzero) noexcept
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			map_result = resources.context->Map(texture, 0, D3D11_MAP_READ, 0, &mapped);
			if (FAILED(map_result)) return false;
			hash = fnv_offset;
			nonzero = 0;
			const auto row_size = static_cast<std::size_t>(resources.description.Width) * 4;
			for (UINT row{}; row < resources.description.Height; ++row)
			{
				const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData) +
					static_cast<std::size_t>(row) * mapped.RowPitch;
				for (std::size_t index{}; index < row_size; ++index)
				{
					hash ^= bytes[index];
					hash *= fnv_prime;
					nonzero += bytes[index] != 0 ? 1 : 0;
				}
			}
			resources.context->Unmap(texture, 0);
			return true;
		}
	}

	bool ready_to_claim() noexcept
	{
		if (active_transaction != nullptr) return false;
		const auto current = state.load(std::memory_order_acquire);
		if (current == gate_state::waiting) return strict_execution_contract_ready();
		if (current != gate_state::complete ||
			production_active.load(std::memory_order_acquire)) return false;
		const auto native = native_render_session::active().get_status();
		return native.available && native.copy_ring && native.accepting_pairs;
	}

	frontend_culling_admission admit_frontend_culling_pair(
		const std::uint64_t pair_id) noexcept
	{
		if (state.load(std::memory_order_acquire) != gate_state::complete)
			return frontend_culling_admission::unarmed;
		if (pair_id == 0 || production_active.load(std::memory_order_acquire) ||
			!native_render_session::active().accepts_pair(pair_id))
		{
			return frontend_culling_admission::unavailable;
		}
		return frontend_culling_admission::accepted;
	}

	void note_backend_view_copy(void* const backend_state,
		const bool scene_geometry_boundary,
		const std::uintptr_t view_setup_caller) noexcept
	{
		auto* const active = active_transaction;
		if (active == nullptr || !*active || active->current_view >= auxiliary_scene::view_count)
		{
			return;
		}
		model_state_sync_calls.fetch_add(1, std::memory_order_relaxed);
		if (backend_state == nullptr || GetCurrentThreadId() != active->owner_thread_id)
		{
			record_model_state_failure(*active, backend_state == nullptr ?
				model_state_failure_stage::invalid_backend :
				model_state_failure_stage::thread, backend_state);
			model_state_contract_mismatches.fetch_add(1, std::memory_order_relaxed);
			fail_transaction(*active, failure::model_state);
			return;
		}

		const auto eye = active->current_view;
		const auto expected_record = reinterpret_cast<std::uintptr_t>(active->record(eye));
		auto* const state_bytes = static_cast<std::uint8_t*>(backend_state);
		std::uintptr_t source{};
		std::memcpy(&source, state_bytes +
			engine_stereo_view::h2_backend_primary_source_pointer_offset,
			sizeof(source));
		std::uintptr_t backend_data{};
		if(scene_geometry_boundary)
			std::memcpy(&backend_data,state_bytes+engine_stereo_dynamic_arena::backend_data_pointer_offset,sizeof(backend_data));
		const auto scope=engine_stereo_dynamic_arena::classify_view_copy(expected_record,source,
			active->claim.frontend,backend_data,scene_geometry_boundary);
		if (!scope.camera_view && !scope.owned_geometry)
		{
			model_state_foreign_bypasses.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		// Capture the complete owned geometry/camera sequence, including native calls
		// after a rejection. No allocations, stack unwind or cross-thread lock on
		// this hot path. The failed pair alone is retained by publish_failure().
		auto& calls = active->dynamic_index_calls;
		auto& count = calls.counts[eye];
		if (count < calls.eyes[eye].size())
		{
			calls.eyes[eye][count++] = {view_setup_caller,
				reinterpret_cast<std::uintptr_t>(backend_state), active->dynamic_upload.state,
				scene_geometry_boundary};
		}
		else if (calls.dropped[eye] != (std::numeric_limits<std::uint32_t>::max)())
		{
			++calls.dropped[eye];
		}
		if (active->failed) return;
		const auto dynamic_timing_started_ns = active->production ? timing_now_ns() : 0;
		const auto dynamic_timing_scope = gsl::finally([active, eye,
			dynamic_timing_started_ns]() noexcept
		{
			if (dynamic_timing_started_ns == 0 || eye >= auxiliary_scene::view_count) return;
			const auto completed_ns = timing_now_ns();
			if (completed_ns > dynamic_timing_started_ns)
			{
				active->production_dynamic_view_copy_timing_ns[eye] +=
					completed_ns - dynamic_timing_started_ns;
			}
		});
		// H2's generic view setup also serves optional visibility queries and
		// fullscreen effects. A newly allocated sun query can add a setup between
		// eyes, without consuming any of these eight scene-mesh arenas. Only the
		// exact geometry executor's setup belongs to the paired cursor sequence.
		if (scope.owned_geometry)
		{
			engine_stereo_dynamic_arena::failure dynamic_index_error{};
			if (eye == 0)
			{
				if (active->dynamic_index_left_count >=
					active->dynamic_index_left.size())
				{
					fail_dynamic_index(*active,
						engine_stereo_dynamic_arena::failure::boundary_capacity);
					publish_dynamic_index_boundary_counts(*active);
					return;
				}
				auto& snapshot = active->dynamic_index_left[
					active->dynamic_index_left_count];
				if (!engine_stereo_dynamic_arena::capture_backend(backend_state,
					snapshot, dynamic_index_error))
				{
					fail_dynamic_index(*active, dynamic_index_error);
					publish_dynamic_index_boundary_counts(*active);
					return;
				}
				if (active->dynamic_index_left_count == 0)
				{
					dynamic_index_data_identity.store(snapshot.data_identity,
						std::memory_order_release);
				}
				++active->dynamic_index_left_count;
				dynamic_index_captures.fetch_add(1, std::memory_order_relaxed);
			}
			else
			{
				const auto ordinal = active->dynamic_index_right_count;
				if (ordinal >= active->dynamic_index_left_count)
				{
					fail_dynamic_index(*active,
						engine_stereo_dynamic_arena::failure::boundary_order);
					publish_dynamic_index_boundary_counts(*active);
					return;
				}
				engine_stereo_dynamic_arena::index_base_snapshot displaced{};
				if (!engine_stereo_dynamic_arena::replace_backend(backend_state,
					active->dynamic_index_left[ordinal], displaced, dynamic_index_error))
				{
					// replace_backend restores its immediate displaced state after a
					// failed write. If cleanup itself failed, retain that exact snapshot
					// as the owner-scope restore target and retry at transaction exit.
					if (displaced.valid && !active->dynamic_index_scope_active)
					{
						active->dynamic_index_restore = displaced;
						active->dynamic_index_scope_active = true;
					}
					fail_dynamic_index(*active, dynamic_index_error);
					publish_dynamic_index_boundary_counts(*active);
					return;
				}
				if (!active->dynamic_index_scope_active)
				{
					// Preserve H2's exact natural post-left arena state once. Later
					// subviews may legitimately advance or overwrite the installed base,
					// but the complete right owner must return to this state.
					active->dynamic_index_restore = displaced;
					active->dynamic_index_scope_active = true;
				}
				++active->dynamic_index_right_count;
				dynamic_index_rebases.fetch_add(1, std::memory_order_relaxed);
				dynamic_index_validations.fetch_add(1, std::memory_order_relaxed);
			}
		}
		// Dynamic index ownership also covers light/shadow subviews. Their
		// projection/origin/cache remain native, never replaced by the eye camera.
		if(!scope.camera_view)return;
		const auto backend_address = reinterpret_cast<std::uintptr_t>(backend_state);

		engine_stereo_view::backend_model_state_sync_result result{};
		if (!engine_stereo_view::synchronize_backend_model_state(backend_state,
			reinterpret_cast<const void*>(expected_record), result))
		{
			record_model_state_failure(*active,
				model_state_failure_stage::sync_contract, backend_state);
			model_state_last_failure.store(static_cast<std::uint32_t>(result.failure),
				std::memory_order_relaxed);
			model_state_last_failure_eye.store(eye, std::memory_order_relaxed);
			model_state_failure_primary_source.store(result.primary_source,
				std::memory_order_relaxed);
			model_state_failure_expected_primary_source.store(
				result.expected_primary_source, std::memory_order_relaxed);
			model_state_failure_rebase_source.store(result.rebase_source,
				std::memory_order_relaxed);
			model_state_failure_expected_rebase_source.store(
				result.expected_rebase_source, std::memory_order_relaxed);
			for (std::size_t component{}; component < 3; ++component)
			{
				model_state_failure_primary_origin_bits[component].store(
					float_bits(result.primary_origin[component]), std::memory_order_relaxed);
				model_state_failure_expected_primary_origin_bits[component].store(
					float_bits(result.expected_primary_origin[component]),
					std::memory_order_relaxed);
				model_state_failure_relative_eye_offset_bits[component].store(
					float_bits(result.relative_eye_offset[component]),
					std::memory_order_relaxed);
				model_state_failure_expected_relative_eye_offset_bits[component].store(
					float_bits(result.expected_relative_eye_offset[component]),
					std::memory_order_relaxed);
			}
			model_state_contract_mismatches.fetch_add(1, std::memory_order_relaxed);
			fail_transaction(*active, failure::model_state);
			return;
		}
		// This exact B5D0 return is after H2 published the current eye and before
		// the first model-list callback. Rewrite only active camera/model descriptors
		// whose origin is still bit-identical to the saved natural center. Type-zero
		// lazy descriptors and all foreign/light origins remain H2-owned.
		engine_stereo_view::camera_model_origin_update origin_update{};
		if (!engine_stereo_view::rebase_camera_model_list_origins(
			reinterpret_cast<void*>(expected_record), active->source_primary_origin,
			origin_update))
		{
			record_model_state_failure(*active,
				model_state_failure_stage::camera_model_rebase, backend_state);
			model_state_contract_mismatches.fetch_add(1, std::memory_order_relaxed);
			fail_transaction(*active, failure::model_state);
			return;
		}
		active->camera_model_boundary_rewrites[eye] += origin_update.rewritten;
		active->camera_model_boundary_foreign[eye] += origin_update.foreign;
		if (!register_backend_state(*active, backend_address, eye))
		{
			record_model_state_failure(*active,
				model_state_failure_stage::backend_capacity, backend_state);
			model_state_contract_mismatches.fetch_add(1, std::memory_order_relaxed);
			fail_transaction(*active, failure::model_state);
			return;
		}

		model_state_sync_matches.fetch_add(1, std::memory_order_relaxed);
		model_state_eye_matches[eye].fetch_add(1, std::memory_order_relaxed);
		model_cache_last_values[eye].store(result.cache_before,
			std::memory_order_relaxed);
		if (result.cache_invalidated)
		{
			model_cache_invalidations.fetch_add(1, std::memory_order_relaxed);
			model_cache_eye_invalidations[eye].fetch_add(1,
				std::memory_order_relaxed);
		}
		else
		{
			model_cache_already_empty.fetch_add(1, std::memory_order_relaxed);
		}
		for (std::size_t component{}; component < 3; ++component)
		{
			model_primary_origin_bits[eye][component].store(
				float_bits(result.primary_origin[component]), std::memory_order_relaxed);
			model_relative_eye_offset_bits[eye][component].store(
				float_bits(result.relative_eye_offset[component]),
				std::memory_order_relaxed);
		}
	}

	void note_backend_depth_hack_projection(void* const backend_state) noexcept
	{
		auto* const active = active_transaction;
		if (active == nullptr || !*active || active->failed || active->current_view >= auxiliary_scene::view_count)
		{
			return;
		}
		depth_hack_projection_calls.fetch_add(1, std::memory_order_relaxed);
		const auto backend_address = reinterpret_cast<std::uintptr_t>(backend_state);
		if (backend_state == nullptr || !backend_state_authorized(*active,
			backend_address, active->current_view))
		{
			depth_hack_projection_foreign_bypasses.fetch_add(1,
				std::memory_order_relaxed);
			return;
		}
		if (GetCurrentThreadId() != active->owner_thread_id)
		{
			depth_hack_projection_contract_mismatches.fetch_add(1,
				std::memory_order_relaxed);
			fail_transaction(*active, failure::model_state);
			return;
		}

		const auto eye = active->current_view;
		const auto expected_record = reinterpret_cast<std::uintptr_t>(active->record(eye));
		std::uintptr_t source{};
		std::memcpy(&source, static_cast<const std::uint8_t*>(backend_state) +
			engine_stereo_view::h2_backend_primary_source_pointer_offset,
			sizeof(source));
		if (source != expected_record)
		{
			// The backend may derive projections for auxiliary records while the
			// native scene owner is active. They remain entirely H2-owned; only the
			// exact current-eye record authorizes the depth-hack correction.
			depth_hack_projection_foreign_bypasses.fetch_add(1,
				std::memory_order_relaxed);
			return;
		}

		engine_stereo_view::backend_depth_hack_projection_result result{};
		const auto outcome = engine_stereo_view::restore_backend_depth_hack_projection(
			backend_state, result);
		if (outcome == engine_stereo_view::
			backend_depth_hack_projection_outcome::not_applicable)
		{
			depth_hack_projection_bypasses.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (outcome != engine_stereo_view::
				backend_depth_hack_projection_outcome::restored || !result.matched)
		{
			depth_hack_projection_contract_mismatches.fetch_add(1,
				std::memory_order_relaxed);
			fail_transaction(*active, failure::model_state);
			return;
		}
		for (std::size_t index{}; index < result.previous_terms.size(); ++index)
		{
			depth_hack_projection_previous_bits[eye][index].store(
				float_bits(result.previous_terms[index]), std::memory_order_relaxed);
			depth_hack_projection_restored_bits[eye][index].store(
				float_bits(result.restored_terms[index]), std::memory_order_relaxed);
		}
		depth_hack_projection_near_bits[eye].store(
			float_bits(result.depth_hack_near), std::memory_order_relaxed);
		depth_hack_projection_eye_restores[eye].fetch_add(1,
			std::memory_order_relaxed);
		depth_hack_projection_restores.fetch_add(1, std::memory_order_release);
	}

	bool begin(transaction& output, engine_stereo_binding::backend_claim&& claim,
		void* const natural_record, const d3d11::device_snapshot& graphics) noexcept
	{
		const auto transaction_timing_started_ns = timing_now_ns();
		region_capture::phase_scope region_admission(region_capture::phase::admission, natural_record, claim.frontend);
		if (output || active_transaction != nullptr || !claim || natural_record == nullptr ||
			!graphics || claim.record != reinterpret_cast<std::uintptr_t>(natural_record) ||
			claim.record_type != native_render_contract::expected_world_record_type ||
			!engine_stereo_view::validate_finalized(claim.views))
		{
			return false;
		}
		engine_stereo_view::scene_record_pair cloned_records{};
		// No active eye transaction, private snapshot or MOD GPU lock exists yet.
		// A frontend view publication is not proof its async surface payload is
		// complete. Join H2's native CPU producers before the first record copy.
		if (!engine_scene_completion::await(claim.frontend, natural_record))
		{
			native_render_session::active().cancel_pair(claim.views.eyes[0].pair_id);
			return false;
		}
		engine_scene_job_capture::checkpoint(natural_record, claim.frontend, region_capture::phase::clone);
		region_capture::phase_scope region_clone(region_capture::phase::clone, natural_record, claim.frontend);
		const auto cloned = engine_stereo_view::clone_scene_records(natural_record, claim.views, cloned_records);
		region_clone.finish(cloned);
		engine_scene_job_capture::checkpoint(natural_record, claim.frontend, region_capture::phase::clone,
			cloned_records.pair_id);
		if (!cloned)
		{
			return false;
		}
		const auto production = state.load(std::memory_order_acquire) == gate_state::complete;
		if (production)
		{
			// OpenVR admits one immutable pose family at a time. A valid but older
			// frontend publication must run through H2's original owner path without
			// occupying a native ring slot that the compositor will never consume.
			if (!native_render_session::active().accepts_pair(cloned_records.pair_id))
			{
				return false;
			}
			auto expected = false;
			if (!production_active.compare_exchange_strong(expected, true,
				std::memory_order_acq_rel, std::memory_order_acquire)) return false;
		}
		else
		{
			auto expected = gate_state::waiting;
			if (!state.compare_exchange_strong(expected, gate_state::active,
				std::memory_order_acq_rel, std::memory_order_acquire)) return false;
		}

		// Only admitted pairs count. Rejected old pose families still drain through
		// H2, but are not evidence of stale camera data reaching stereo rendering.
		engine_stereo_binding::observe_owner_camera(claim, natural_record);
		output.active = true;
		output.production = production;
		output.temporal_history_current_frame_diagnostic = production &&
			temporal_history_current_frame_diagnostic.load(std::memory_order_acquire);
		output.material_reuse_left_diagnostic = production &&
			material_reuse_left_diagnostic.load(std::memory_order_acquire);
		if (output.temporal_history_current_frame_diagnostic)
		{
			temporal_history_current_frame_pairs.fetch_add(1,
				std::memory_order_relaxed);
		}
		if (output.material_reuse_left_diagnostic)
		{
			material_reuse_left_pairs.fetch_add(1, std::memory_order_relaxed);
			material_reuse_left_last_pair.store(cloned_records.pair_id,
				std::memory_order_release);
		}
		output.production_transaction_timing_started_ns = production ?
			transaction_timing_started_ns : 0;
		output.production_eye_timing_started_ns = {};
		output.owner_thread_id = GetCurrentThreadId();
		output.natural_record = reinterpret_cast<std::uintptr_t>(natural_record);
		std::memcpy(output.shared_culling_view.data(), natural_record, output.shared_culling_view.size());
		// The descriptors and both eye transforms share the frontend source
		// center. clone_scene_records verified it against the actual source before
		// installing either eye; never mix an older publication with newer payload.
		output.source_primary_origin = claim.views.source_origin();
		output.claim = claim;
		claim = {};
		output.records = std::move(cloned_records);
		region_capture::snapshot(output.records.pair_id, 0, 0, output.records.left.data());
		region_capture::snapshot(output.records.pair_id, 1, 0, output.records.right.data());
		if (production)
		{
			temporal_history_preparations.fetch_add(1, std::memory_order_relaxed);
			std::lock_guard lock(temporal_history_mutex);
			if (temporal_history_reset_pending.exchange(false,
				std::memory_order_acq_rel))
			{
				temporal_history_state = {};
				output.auxiliary_history.valid=false;
				temporal_history_reset_applications.fetch_add(1,
					std::memory_order_relaxed);
			}
			const engine_stereo_view::temporal_history_state current_frame_history{};
			const auto& selected_history =
				output.temporal_history_current_frame_diagnostic ?
				current_frame_history : temporal_history_state;
			if (!engine_stereo_view::prepare_temporal_history(output.records,
				graphics.generation, selected_history, output.temporal_history))
			{
				temporal_history_failures.fetch_add(1, std::memory_order_relaxed);
				fail_transaction(output, failure::record);
			}
			else if (output.temporal_history.seeded)
			{
				temporal_history_seeds.fetch_add(1, std::memory_order_relaxed);
			}
		}
		output.context = graphics.context;
		output.device_generation = graphics.generation;
		if (production && !engine_stereo_eye_resources::install(
			output.context.Get(), output.device_generation))
		{
			// Installation normally completed at device creation. Rechecking here is
			// idempotent and prevents either eye from executing if those process-wide
			// hooks are no longer attached to this exact H2 device generation.
			fail_transaction(output, failure::eye_resource);
		}
		if (production)
		{
			production_attempts.fetch_add(1, std::memory_order_relaxed);
			std::lock_guard lock(state_mutex);
			evidence.production_last_pair = output.records.pair_id;
		}
		else
		{
			attempts.fetch_add(1, std::memory_order_relaxed);
			resources = {};
			resources.device = graphics.device;
			resources.context = graphics.context;
			resources.generation = graphics.generation;
			std::lock_guard lock(state_mutex);
			evidence = {};
			evidence.state = gate_state::active;
			evidence.attempts = attempts.load(std::memory_order_relaxed);
			evidence.pair_id = output.records.pair_id;
			evidence.publication = output.records.publication;
			evidence.natural_record = output.natural_record;
			evidence.right_record = reinterpret_cast<std::uintptr_t>(
				output.records.right.data());
			evidence.context = reinterpret_cast<std::uintptr_t>(graphics.context.Get());
			evidence.device_generation = graphics.generation;
			evidence.owner_thread_id = output.owner_thread_id;
			evidence.final_target_id = 0;
		}
		output.dynamic_upload.data = output.claim.frontend;
		output.dynamic_upload.thread = output.owner_thread_id;
		output.auxiliary={};output.auxiliary_complete=false;output.pending_left=false;output.thermal_world=false;
		if (production)
		{
			const eye_composition::event event{output.claim.views,output.records.pair_id,output.device_generation,
				0,0,0,eye_composition::model_origins_for(output.claim.views,output.records)};
			thermal_scene::world_request world;
			if(const auto planner=thermal_scene::plan.load())world=planner(event);
			auxiliary_scene::request request;
			namespace probe=diagnostics::screen;
			auto observed=probe::observation(event,output.claim.views.screen_scope_epoch?output.claim.views.screen_scope_epoch:output.claim.views.weapon_display_epoch,GetTickCount64());
			observed.stage=output.claim.views.screen_scope_epoch?"select_fixed_scope":output.claim.views.weapon_display_epoch?"select_weapon_display":"select_optic";
			if(output.claim.views.screen_scope_epoch)
			{if(const auto planner=auxiliary_scene::screen_plan.load())request=planner(event);}
			else
			{
				if(const auto planner=auxiliary_scene::weapon_display_plan.load())request=planner(event);
				if(!request.valid)if(const auto planner=auxiliary_scene::plan.load()){observed.consumer="optic_fallback";request=planner(event);}
			}
			observed.owner_id=request.owner;observed.owner_generation=request.generation;observed.owner_revision=request.revision;
			observed.reference_id=request.reference;observed.crop=request.window;observed.auxiliary_valid=request.valid;
			observed.active=observed.active||request.valid;
			bool accepted=request.valid&&request.eye<2&&auxiliary_scene::valid_window(request.window);
			probe::check(observed.rejected,request.valid,probe::auxiliary_missing);
			if(request.valid)probe::check(observed.rejected,accepted,probe::window);
			const auto step=[&](const char* name,auto operation)
			{
				if(!accepted)return;
				observed.stage=name;accepted=operation();
				if(!accepted)observed.rejected|=probe::auxiliary_record;
			};
			step("crop_record",[&]{return auxiliary_scene::crop_record(request.eye==0?output.records.left:output.records.right,request.window,output.auxiliary_record);});
			step("near_plane",[&]{return auxiliary_scene::apply_near(output.auxiliary_record,request.near_distance);});
			step("center_camera",[&]{return !request.native_center||auxiliary_scene::center_record(output.auxiliary_record,output.claim.views.source_origin());});
			step("native_thermal",[&]{return auxiliary_scene::apply_thermal(output.auxiliary_record,request);});
			step("auxiliary_history",[&]{return auxiliary_scene::prepare_history(output.auxiliary_record,request,output.device_generation,
				output.records.pair_id,output.auxiliary_history,output.auxiliary_next_history,output.auxiliary_reset);});
			if(accepted){output.auxiliary=request;observed.stage="auxiliary_admitted";observed.success=true;}
			if(observed.active||request.valid)probe::auxiliary.record(observed);
			// The auxiliary must be built from native thermal input BEFORE this
			// split, including when the eye box produces no optical request.
			const auto original=thermal_scene::read(output.records.left.data());
			if(thermal_scene::normalize_world(output.records,world,output.claim.views))
			{
				output.thermal_world=true;++thermal_scene::world_pairs;
				thermal_scene::last_native_ssr=original.ssr;thermal_scene::last_world_ssr=world.ssr_scale;
			}
		}
		if(!output.auxiliary.valid) output.auxiliary_history.valid=false;
		active_transaction = &output;
		region_admission.finish(1);
		return true;
	}

	void* begin_view(transaction& active, const std::uint32_t eye) noexcept
	{
		const auto eye_timing_started_ns = timing_now_ns();
		if (!active || active.failed || active_transaction != &active || eye >= auxiliary_scene::view_count ||
			active.current_view < auxiliary_scene::view_count || GetCurrentThreadId() != active.owner_thread_id ||
			(active.completed_eye_mask & (1u << eye)) != 0)
		{
			if (active) fail_transaction(active, failure::thread);
			return nullptr;
		}
		active.production_eye_timing_started_ns[eye] = active.production ?
			eye_timing_started_ns : 0;
		if ((eye==auxiliary_scene::view_index && (!active.production || !active.auxiliary.valid ||
			active.auxiliary_complete || active.completed_eye_mask!=1)) ||
			(eye==1 && active.auxiliary.valid && (!active.auxiliary_complete || active.pending_left)))
			{fail_transaction(active,failure::record);return nullptr;}
		if (eye!=0) active.dynamic_index_right_count=0;
		active.current_view = eye;
		if (eye<2 && active.scene_batch_probe_pair_active)
		{
			active.scene_batch_probe_eye_active =
				engine_stereo_scene_batch_probe::begin_eye(active.records.pair_id, eye);
			if (!active.scene_batch_probe_eye_active)
			{
				engine_stereo_scene_batch_probe::end_pair(active.records.pair_id, false);
				active.scene_batch_probe_pair_active = false;
			}
		}
		if (active.production)
		{
			// Establish history ownership BEFORE the first SSR read, not when H2
			// eventually selects the mip-generation target near the end of eye 0.
			// 0x140781CF0..0x140781D8A proves registry[id]+8 is the color RTV
			// passed to OMSetRenderTargets; +16 is the independently bound DSV.
			h2_gpu_context_lock context_lock;
			if (!context_lock)
			{
				fail_transaction(active, failure::eye_resource);
				return nullptr;
			}
			if (eye == 0)
			{
				ID3D11RenderTargetView* owner_view{};
				const auto address = native_render_contract::target_registry_base +
					engine_stereo_eye_resources::isolated_target_id *
					native_render_contract::target_registry_stride + sizeof(void*);
				std::memcpy(&owner_view, reinterpret_cast<const void*>(address),
					sizeof(owner_view));
				Microsoft::WRL::ComPtr<ID3D11Device> device;
				active.context->GetDevice(device.GetAddressOf());
				std::array<engine_stereo_eye_resources::source_binding,
					engine_stereo_eye_resources::isolated_target_count> bindings{{
					{engine_stereo_eye_resources::isolated_target_id,
						engine_stereo_eye_resources::role::color, owner_view},
				}};
				if (!owner_view || !device ||
					!native_post_aa::append_history_bindings(active.records.left.data(), bindings) ||
					!engine_stereo_eye_resources::begin_pair(
					active.records.pair_id, device.Get(), active.context.Get(),
					active.device_generation, bindings))
				{
					fail_transaction(active, failure::eye_resource);
					return nullptr;
				}
				active.eye_resource_pair_active = true;
			}
			if (!(eye==auxiliary_scene::view_index ?
				engine_stereo_eye_resources::begin_auxiliary(active.records.pair_id,active.auxiliary_reset) :
				engine_stereo_eye_resources::begin_eye(active.records.pair_id, eye)))
			{
				fail_transaction(active, failure::eye_resource);
				return nullptr;
			}
			active.eye_resource_eye_active = true;
		}
		std::uint8_t* eye_record{};
		if (eye == 0)
		{
			eye_record = reinterpret_cast<std::uint8_t*>(
				active.natural_record);
			// Eye 0 executes H2's arena-owned record rather than the private clone.
			// Mirror the only additional proven eye-local field so the backend
			// XModel eyeOffset cannot remain at the frontend union/centre origin.
			if (!engine_stereo_view::copy_eye_local_record_fields(eye_record,
				active.records.left))
			{
				fail_transaction(active, failure::record);
				return nullptr;
			}
			if(active.thermal_world && !active.thermal_world_lease.install(eye_record,active.records.left))
			{fail_transaction(active,failure::record);return nullptr;}
		}
		else
		{
			eye_record = active.record(eye);
			if (active.dynamic_index_left_count == 0)
			{
				fail_dynamic_index(active,
					engine_stereo_dynamic_arena::failure::boundary_count);
				publish_dynamic_index_boundary_counts(active);
				return nullptr;
			}
		}
		if (eye<2 && active.gpu_census_pair_active)
		{
			active.gpu_census_eye_active = engine_stereo_gpu_census::begin_eye(
				active.records.pair_id, eye);
			if (!active.gpu_census_eye_active)
			{
				// Terminate only the one-shot observer. Production rendering remains
				// governed exclusively by the owner/native-ring contracts below.
				(void)engine_stereo_gpu_census::end_pair(active.records.pair_id);
				active.gpu_census_pair_active = false;
			}
		}
		if (eye<2 && active.material_buffer_probe_pair_active &&
			!engine_stereo_material_buffer_probe::begin_eye(
				active.records.pair_id, eye))
		{
			active.material_buffer_probe_pair_active = false;
		}
		if (eye<2 && active.particle_buffer_probe_pair_active &&
			!engine_stereo_particle_buffer_probe::begin_eye(
				active.records.pair_id, eye))
		{
			active.particle_buffer_probe_pair_active = false;
		}
		if (active.gpu_census_eye_active)
		{
			engine_stereo_gpu_census::note_dynamic_fx_arena_boundary(
				active.records.pair_id, eye,
				engine_stereo_gpu_census::dynamic_fx_arena_phase::eye_begin,
				reinterpret_cast<std::uintptr_t>(eye_record));
		}
		if (eye<2 && active.ssr_consumer_probe_pair_active &&
			!engine_stereo_ssr_consumer_probe::begin_eye(
				active.records.pair_id, eye))
		{
			engine_stereo_ssr_consumer_probe::end_pair(active.records.pair_id, false);
			active.ssr_consumer_probe_pair_active = false;
		}
		if (eye<2 && active.effect_timeline_pair_active)
		{
			active.effect_timeline_eye_active =
				engine_stereo_effect_timeline::begin_eye(active.records.pair_id, eye,
					eye_record);
			if (!active.effect_timeline_eye_active)
				active.effect_timeline_pair_active = false;
		}
		return eye_record;
	}

	bool snapshot_current_eye(const void* const record,
		engine_stereo_view::eye_slot& output) noexcept
	{
		const auto* const active = active_transaction;
		if (!active || !*active || !active->production || active->failed ||
			active->current_view >= auxiliary_scene::view_count || GetCurrentThreadId() != active->owner_thread_id ||
			active->production_owner_invoke_timing_started_ns[active->current_view] == 0)
			return false;
		const auto eye = active->current_view;
		const auto* const expected = static_cast<const void*>(active->record(eye));
		if (record != expected) return false;
		engine_stereo_view::eye_slot snapshot{};
		std::memcpy(snapshot.bytes.data(), expected, snapshot.bytes.size());
		snapshot.pair_id = active->records.pair_id;
		snapshot.publication = active->records.publication;
		snapshot.output_eye = eye;
		snapshot.view_eye = eye;
		output = snapshot;
		return true;
	}

	bool snapshot_tessellation_view(const void* const frontend,
		engine_stereo_tessellation::shared_view& output) noexcept
	{
		const auto* const active = active_transaction;
		if (!active || !*active || !active->production || active->failed || active->current_view >= auxiliary_scene::view_count ||
			GetCurrentThreadId() != active->owner_thread_id ||
			active->production_owner_invoke_timing_started_ns[active->current_view] == 0 ||
			reinterpret_cast<std::uintptr_t>(frontend) != active->claim.frontend) return false;
		std::uint32_t index{};
		std::uintptr_t arena{};
		const auto* const bytes = static_cast<const std::uint8_t*>(frontend);
		std::memcpy(&index, bytes + 0x540f90, sizeof(index));
		std::memcpy(&arena, bytes + 0x540fa0, sizeof(arena));
		if (index != active->claim.record_index || arena > active->natural_record ||
			active->natural_record - arena != std::uintptr_t(index) * engine_stereo_view::h2_scene_record_size)
			return false;
		output.culling = active->shared_culling_view;
		std::memcpy(output.rendered.data(), reinterpret_cast<const void*>(active->natural_record), output.rendered.size());
		return true;
	}

	void dynamic_upload_boundary(void* const data, void (*const original)(void*))
	{
		auto* const active = active_transaction;
		if (active == nullptr || !*active || active->current_view >= auxiliary_scene::view_count)
		{
			original(data);
			return;
		}
		auto& upload = active->dynamic_upload;
		const auto decision = active->current_view==auxiliary_scene::view_index ?
			upload.auxiliary_boundary(reinterpret_cast<std::uintptr_t>(data),GetCurrentThreadId()) :
			upload.boundary(active->current_view,reinterpret_cast<std::uintptr_t>(data), GetCurrentThreadId());
		if (decision == engine_stereo_dynamic_upload::action::reject)
		{
			fail_transaction(*active, failure::dynamic_mesh);
			diagnostics::record_trace(diagnostics::trace_event::dynamic_upload_reject,
				active->records.pair_id, static_cast<std::uint64_t>(upload.error));
			// A foreign arena is not owned by this transaction. Preserve its H2
			// call; any deferred call for our exact arena is drained at end().
			if (reinterpret_cast<std::uintptr_t>(data) != upload.data) original(data);
			return;
		}
		active->dynamic_upload_original = original;
		if (decision == engine_stereo_dynamic_upload::action::defer)
		{
			// Current-frame GPU data is still unmapped and valid for the right
			// eye. Do not read the next WRITE_DISCARD allocation as a left snapshot.
			diagnostics::record_trace(diagnostics::trace_event::dynamic_upload_defer,
				active->records.pair_id, upload.data);
			return;
		}
		diagnostics::record_trace(diagnostics::trace_event::dynamic_upload_advance,
			active->records.pair_id, upload.data);
		original(data);
		upload.returned();
		diagnostics::record_trace(diagnostics::trace_event::dynamic_upload_return,
			active->records.pair_id, upload.data);
	}

	void begin_owner_invoke(transaction& active, const std::uint32_t eye) noexcept
	{
		if (!active.production || active.failed || active.current_view != eye || eye >= auxiliary_scene::view_count ||
			GetCurrentThreadId() != active.owner_thread_id ||
			active.production_owner_invoke_timing_started_ns[eye] != 0)
		{
			return;
		}
		active.production_owner_invoke_timing_started_ns[eye] = timing_now_ns();
		if(eye<2) region_capture::begin_owner(active.records.pair_id, eye,
			static_cast<const void*>(active.record(eye)));
	}

	void end_owner_invoke(transaction& active, const std::uint32_t eye) noexcept
	{
		if (eye >= auxiliary_scene::view_count) return;
		const auto started_ns = active.production_owner_invoke_timing_started_ns[eye];
		active.production_owner_invoke_timing_started_ns[eye] = 0;
		if (started_ns == 0) return;
		if(eye<2) region_capture::end_owner(active.records.pair_id, eye,
			static_cast<const void*>(active.record(eye)));
		record_timing(production_owner_invoke_timing_samples[eye],
			production_owner_invoke_timing_last_us[eye],
			production_owner_invoke_timing_max_us[eye],
			production_owner_invoke_timing_total_us[eye], started_ns);
	}

	void note_target_selection(transaction& active, const std::uint32_t target_id,
		ID3D11DeviceContext* const context,
		const std::uint64_t) noexcept
	{
		if (!active || active.failed || active_transaction != &active ||
			active.current_view >= auxiliary_scene::view_count || context == nullptr ||
			context != active.context.Get() || GetCurrentThreadId() != active.owner_thread_id)
		{
			return;
		}
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
		context->OMGetRenderTargets(1, view.GetAddressOf(), nullptr);
		const auto target_index = native_display_contract::target_index(target_id);
		if (target_index < native_display_contract::target_count)
		{
			if (!view) return;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			view->GetResource(&resource);
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			if (resource && SUCCEEDED(resource.As(&texture)) && texture)
			{
				active.scene_targets[active.current_view][target_index] = std::move(texture);
			}
			return;
		}

		// The mip target is now already isolated on entry to each eye. Selecting
		// it here is a writer boundary, never an authorization to start late.
		if (active.production && target_id == engine_stereo_eye_resources::isolated_target_id &&
			!active.eye_resource_eye_active)
			fail_transaction(active, failure::eye_resource);
	}
	bool end_view(transaction& active, const std::uint32_t eye,
		bool (*const display_transform)(void*, native_display_contract::route,
			const native_post_aa::view_identity&)) noexcept
	{
		const auto restore_thermal=gsl::finally([&]() noexcept
		{if(eye==0 && GetCurrentThreadId()==active.owner_thread_id)active.thermal_world_lease.restore();});
		if (active.scene_batch_probe_eye_active)
		{
			if (!engine_stereo_scene_batch_probe::end_eye(
				active.records.pair_id, eye))
			{
				engine_stereo_scene_batch_probe::end_pair(active.records.pair_id, false);
				active.scene_batch_probe_pair_active = false;
			}
			active.scene_batch_probe_eye_active = false;
		}
		if (active.effect_timeline_eye_active && eye < 2)
		{
			const auto* const record = eye == 0 ?
				reinterpret_cast<const void*>(active.natural_record) :
				static_cast<const void*>(active.records.right.data());
			engine_stereo_effect_timeline::end_eye(active.records.pair_id, eye, record);
			active.effect_timeline_eye_active = false;
		}
		const auto close_eye_resources = [&]() noexcept
		{
			if (!active.eye_resource_eye_active) return true;
			h2_gpu_context_lock context_lock;
			const auto ended = context_lock && (eye==auxiliary_scene::view_index ?
				engine_stereo_eye_resources::end_auxiliary(active.records.pair_id) :
				engine_stereo_eye_resources::end_eye(active.records.pair_id, eye));
			active.eye_resource_eye_active = false;
			if (!ended) fail_transaction(active, failure::eye_resource);
			return ended;
		};
		// Keep both SSR and AA histories isolated through the final display
		// passes. Every early exit still restores native bindings on this owner.
		const auto eye_resources_exit = gsl::finally([&] { (void)close_eye_resources(); });
		if (active.ssr_consumer_probe_pair_active && eye < 2)
		{
			engine_stereo_ssr_consumer_probe::end_eye(active.records.pair_id, eye);
		}
		if (active.production && eye < auxiliary_scene::view_count)
		{
			const auto dynamic_elapsed_ns =
				active.production_dynamic_view_copy_timing_ns[eye];
			active.production_dynamic_view_copy_timing_ns[eye] = 0;
			record_elapsed_timing(production_dynamic_view_copy_timing_samples[eye],
				production_dynamic_view_copy_timing_last_us[eye],
				production_dynamic_view_copy_timing_max_us[eye],
				production_dynamic_view_copy_timing_total_us[eye], dynamic_elapsed_ns);
		}
		const auto timing_started_ns = active.production && eye < auxiliary_scene::view_count &&
			active.current_view == eye ? active.production_eye_timing_started_ns[eye] : 0;
		if (timing_started_ns != 0) active.production_eye_timing_started_ns[eye] = 0;
		const auto timing_scope = gsl::finally([timing_started_ns, eye]() noexcept
		{
			if (timing_started_ns == 0 || eye >= auxiliary_scene::view_count) return;
			record_timing(production_eye_timing_samples[eye],
				production_eye_timing_last_us[eye], production_eye_timing_max_us[eye],
				production_eye_timing_total_us[eye], timing_started_ns);
		});
		if (!(eye==auxiliary_scene::view_index ? active.dynamic_upload.finish_auxiliary() : active.dynamic_upload.finish_eye(eye)))
		{
			fail_transaction(active, failure::dynamic_mesh);
		}
		// The right owner consumed the corresponding left index origin at each
		// source-matching view-copy boundary. Return H2's arena descriptor to the
		// exact natural post-left state before any copy or transaction exit.
		if (eye != 0 && active.dynamic_index_scope_active)
		{
			const auto restore_started_ns = active.production ? timing_now_ns() : 0;
			const auto restore_timing_scope = gsl::finally([restore_started_ns]() noexcept
			{
				if (restore_started_ns == 0) return;
				record_timing(production_dynamic_restore_timing_samples,
					production_dynamic_restore_timing_last_us,
					production_dynamic_restore_timing_max_us,
					production_dynamic_restore_timing_total_us, restore_started_ns);
			});
			if (GetCurrentThreadId() != active.owner_thread_id ||
				!restore_dynamic_index_scope(active))
			{
				fail_transaction(active, failure::dynamic_mesh);
			}
		}
		if (eye != 0)
		{
			publish_dynamic_index_boundary_counts(active);
			if (active.dynamic_index_right_count != active.dynamic_index_left_count)
			{
				fail_dynamic_index(active,
					engine_stereo_dynamic_arena::failure::boundary_count);
			}
		}
		// This boundary is reached immediately after H2 returns from the eye owner.
		// The backend-view samples already captured the scoped values. Close the
		// read-only census before validation/copy work adds non-scene calls.
		if (active.gpu_census_eye_active)
		{
			const auto record = eye == 0 ? active.natural_record : eye == 1 ?
				reinterpret_cast<std::uintptr_t>(active.records.right.data()) : 0;
			engine_stereo_gpu_census::note_dynamic_fx_arena_boundary(
				active.records.pair_id, eye,
				engine_stereo_gpu_census::dynamic_fx_arena_phase::eye_end, record);
			(void)engine_stereo_gpu_census::end_eye(active.records.pair_id, eye);
			active.gpu_census_eye_active = false;
		}
		if (eye<2 && active.material_buffer_probe_pair_active &&
			!engine_stereo_material_buffer_probe::end_eye(
				active.records.pair_id, eye))
		{
			active.material_buffer_probe_pair_active = false;
		}
		if (eye<2 && active.particle_buffer_probe_pair_active &&
			!engine_stereo_particle_buffer_probe::end_eye(
				active.records.pair_id, eye))
		{
			active.particle_buffer_probe_pair_active = false;
		}
		if (!active || active.failed || active_transaction != &active || eye >= auxiliary_scene::view_count ||
			active.current_view != eye || GetCurrentThreadId() != active.owner_thread_id)
		{
			if (active) fail_transaction(active, failure::thread);
			return false;
		}
		auto* const record = static_cast<const std::uint8_t*>(active.record(eye));
		// Read-only census of H2's complete 68-entry 0x130 draw-list descriptor
		// array. viewOrigin is descriptor+0x110; unused entries remain all zero.
		// These fields are not yet mutated: the census distinguishes lists that H2
		// finalized per eye from lists that retained the natural center/rebase, so one
		// test run can decide whether a separate list-origin propagation is required.
		const auto list_census = census_model_list_origins(record,
			active.source_primary_origin);
		active.model_list_eye_origin_matches[eye] = list_census.eye_matches;
		active.model_list_center_origin_matches[eye] = list_census.center_matches;
		active.model_list_zero_origins[eye] = list_census.zeros;
		active.model_list_other_origins[eye] = list_census.other;
		engine_stereo_view::camera_model_origin_census camera_model_census{};
		if (!engine_stereo_view::census_camera_model_list_origins(record,
			camera_model_census))
		{
			fail_transaction(active, failure::model_state);
			return false;
		}
		active.camera_model_final_eye[eye] = camera_model_census.eye;
		active.camera_model_final_inactive[eye] = camera_model_census.inactive;
		active.camera_model_final_other[eye] = camera_model_census.other;
		if (camera_model_census.other != 0)
		{
			// The original owner has completed, so rejecting here cannot skip H2's
			// scene state. It only prevents an incoherent eye from reaching the HMD.
			fail_transaction(active, failure::model_state);
			return false;
		}
		std::uint32_t final_target{};
		std::uint32_t first_ping_pong{};
		std::uint32_t second_ping_pong{};
		std::uint32_t selector{};
		std::memcpy(&final_target, record + native_render_contract::record_target_0_offset,
			sizeof(final_target));
		std::memcpy(&first_ping_pong, record +
			native_render_contract::record_target_1_offset, sizeof(first_ping_pong));
		std::memcpy(&second_ping_pong, record +
			native_render_contract::record_target_2_offset, sizeof(second_ping_pong));
		std::memcpy(&selector, record + native_render_contract::record_target_selector_offset,
			sizeof(selector));
		const auto route = native_display_contract::resolve(final_target, first_ping_pong,
			second_ping_pong, selector);
		display_routes[eye].store((route.source << 16) | route.destination, std::memory_order_release);
		if (!route)
		{
			fail_transaction(active, failure::target_contract);
			return false;
		}
		active.sources[eye] = active.scene_targets[eye][native_display_contract::target_index(route.source)];
		if (!active.sources[eye])
		{
			fail_transaction(active, failure::target_missing);
			return false;
		}
		D3D11_TEXTURE2D_DESC description{};
		active.sources[eye]->GetDesc(&description);
		if (!engine_scene_resolution::accepts_source({description.Width, description.Height}))
		{
			fail_transaction(active, failure::source_contract);
			return false;
		}
		if (active.production)
		{
			if (!supports_readback_source(description))
			{
				fail_transaction(active, failure::source_contract);
				return false;
			}
			if (eye == 0) active.source_description = description;
			else if (std::memcmp(&description, &active.source_description,
				sizeof(description)) != 0)
			{
				fail_transaction(active, failure::target_contract);
				return false;
			}
			// H2 serializes every immediate-context path (including GetData,
			// Present, query issue, and its backend renderer) through this same
			// recursive kernel mutex. Join that proven ownership chain for the
			// MOD-owned ExecuteCommandList instead of enabling global D3D11
			// multithread protection or inventing a competing gate.
			h2_gpu_context_lock context_lock;
			production_context_lock_last_result.store(context_lock.result(),
				std::memory_order_release);
			production_context_lock_wait_last_us.store(context_lock.wait_us(),
				std::memory_order_release);
			production_context_lock_wait_total_us.fetch_add(context_lock.wait_us(),
				std::memory_order_relaxed);
			update_maximum(production_context_lock_wait_max_us, context_lock.wait_us());
			if (!context_lock)
			{
				production_context_lock_failures.fetch_add(1, std::memory_order_relaxed);
				fail_transaction(active, failure::copy);
				return false;
			}
			production_context_lock_acquires.fetch_add(1, std::memory_order_relaxed);
			// The registry's first field is a GfxImage, NOT a D3D view. Match
			// its texture/SRV to the selected RTV before original PostFX reads
			// this image at 0x1407B07AA; stale bindings cannot authorize an input.
			game::GfxImage* scene_image{};
			std::memcpy(&scene_image, reinterpret_cast<const void*>(
				native_render_contract::target_registry_base + route.source *
				native_render_contract::target_registry_stride), sizeof(scene_image));
			Microsoft::WRL::ComPtr<ID3D11Resource> scene_resource;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> scene_source;
			if (scene_image && scene_image->texture.shaderView)
				scene_image->texture.shaderView->GetResource(&scene_resource);
			if (!scene_resource || scene_image->texture.map != active.sources[eye].Get() ||
				FAILED(scene_resource.As(&scene_source)) ||
				scene_source.Get() != active.sources[eye].Get())
			{
				fail_transaction(active, failure::source_contract);
				return false;
			}
			// The outer scene owner ends BEFORE H2's final PostFX. Run the original
			// display transform for this eye into its native-sized ping-pong target.
			// Preserve the selected raw HDR source for the natural H2 tail.
			ID3D11RenderTargetView* display_view{};
			std::memcpy(&display_view, reinterpret_cast<const void*>(
				native_render_contract::target_registry_base + route.destination *
				native_render_contract::target_registry_stride + sizeof(void*)), sizeof(display_view));
			Microsoft::WRL::ComPtr<ID3D11Resource> display_resource;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> display_source;
			D3D11_TEXTURE2D_DESC display_description{};
			if (display_view) display_view->GetResource(&display_resource);
			if (display_resource && SUCCEEDED(display_resource.As(&display_source)))
				display_source->GetDesc(&display_description);
			Microsoft::WRL::ComPtr<ID3D11Device> display_device;
			Microsoft::WRL::ComPtr<ID3D11Device> scene_device;
			if (display_source) display_source->GetDevice(&display_device);
			active.sources[eye]->GetDevice(&scene_device);
			display_format.store(display_description.Format, std::memory_order_relaxed);
			display_bind_flags.store(display_description.BindFlags, std::memory_order_relaxed);
			const char* display_error = !display_source ? "target_missing" :
				display_source.Get() == active.sources[eye].Get() ? "source_alias" :
				display_device.Get() != scene_device.Get() ? "device" :
				!supports_display_source(display_description) ? "descriptor" :
				display_description.Width != description.Width ||
				display_description.Height != description.Height ? "extent" : nullptr;
			if (!display_error && (!display_transform ||
				!display_transform(const_cast<std::uint8_t*>(record), route,
					{active.records.pair_id, active.device_generation, eye,
						active.temporal_history.seeded ||
						(eye == auxiliary_scene::view_index && active.auxiliary_reset)})))
				display_error = "destination_route";
			if (display_error)
			{
				display_transform_error.store(display_error, std::memory_order_release);
				display_transform_failures.fetch_add(1, std::memory_order_relaxed);
				fail_transaction(active, failure::display_transform);
				return false;
			}
			display_transform_completions.fetch_add(1, std::memory_order_relaxed);
			if (!close_eye_resources()) return false;
			const auto native_copy_started_ns = timing_now_ns();
			eye_composition::event composition{active.claim.views,
				active.records.pair_id, active.device_generation, eye,
				display_description.Width, display_description.Height,
				eye_composition::model_origins_for(active.claim.views, active.records)};
			// H2's $scene target shares its DSV with the LDR targets. Borrow it
			// before the next eye reuses it; consumers may depth-test, never write.
			std::memcpy(&composition.scene_depth,reinterpret_cast<const void*>(native_render_contract::target_registry_base+
				4*native_render_contract::target_registry_stride+2*sizeof(void*)),sizeof(composition.scene_depth));
			if (eye==auxiliary_scene::view_index)
			{
				auto observed=diagnostics::screen::observation(composition,active.claim.views.screen_scope_epoch?active.claim.views.screen_scope_epoch:active.claim.views.weapon_display_epoch,GetTickCount64());
				observed.stage="auxiliary_copy";observed.owner_id=active.auxiliary.owner;observed.owner_generation=active.auxiliary.generation;
				observed.owner_revision=active.auxiliary.revision;observed.reference_id=active.auxiliary.reference;
				observed.success=active.auxiliary_image.capture(active.context.Get(),display_source.Get(),nullptr,active.device_generation);
				observed.auxiliary_has_image=bool(active.auxiliary_image.view);
				if(!observed.success)observed.rejected|=diagnostics::screen::auxiliary_copy;
				diagnostics::screen::auxiliary.record(observed);
				if (!observed.success)
					{fail_transaction(active,failure::copy);return false;}
				active.auxiliary_complete=true;
				active.auxiliary_history=active.auxiliary_next_history;
				active.current_view=auxiliary_scene::no_view;
				return true;
			}
			if (eye==0 && active.auxiliary.valid)
			{
				// Preserve left display/depth before the extra owner reuses native
				// scratch targets. Composition stays outside the native-session lock.
				if (!composition.scene_depth || !active.pending_left_image.capture(active.context.Get(),
					display_source.Get(),composition.scene_depth,active.device_generation))
					{fail_transaction(active,failure::copy);return false;}
				active.pending_left=true;active.pending_left_target=route.destination;
				active.pending_left_native=display_source;
				active.completed_eye_mask|=1u;active.current_view=auxiliary_scene::no_view;
				return true;
			}
			composition.auxiliary=&active.auxiliary;
			composition.auxiliary_image=active.auxiliary_complete ? active.auxiliary_image.view.Get() : nullptr;
			const auto copied = native_render_session::active().copy_eye(
				active.records.pair_id, eye, display_source.Get(), route.destination, active.context.Get(), &composition);
			record_timing(production_native_copy_timing_samples[eye],
				production_native_copy_timing_last_us[eye],
				production_native_copy_timing_max_us[eye],
				production_native_copy_timing_total_us[eye], native_copy_started_ns);
			if (!copied)
			{
				fail_transaction(active, failure::copy);
				return false;
			}
			active.completed_eye_mask |= 1u << eye;
			active.current_view = auxiliary_scene::no_view;
			production_eye_copies.fetch_add(1, std::memory_order_relaxed);
			return true;
		}
		if (eye == 0)
		{
			{
				std::lock_guard lock(state_mutex);
				evidence.final_target_id = route.source;
				evidence.width = description.Width;
				evidence.height = description.Height;
				evidence.format = static_cast<std::uint32_t>(description.Format);
				evidence.mip_levels = description.MipLevels;
				evidence.array_size = description.ArraySize;
				evidence.sample_count = description.SampleDesc.Count;
				evidence.sample_quality = description.SampleDesc.Quality;
				evidence.usage = static_cast<std::uint32_t>(description.Usage);
				evidence.bind_flags = description.BindFlags;
				evidence.cpu_access_flags = description.CPUAccessFlags;
				evidence.misc_flags = description.MiscFlags;
			}
			if (!supports_readback_source(description))
			{
				fail_transaction(active, failure::source_contract);
				return false;
			}
			if (!create_readback_resources(description))
			{
				fail_transaction(active, failure::resource_creation);
				return false;
			}
		}
		else if (std::memcmp(&description, &resources.description,
			sizeof(description)) != 0)
		{
			fail_transaction(active, failure::target_contract);
			return false;
		}
		if (!copy_source_to_staging(eye, active.sources[eye].Get()))
		{
			fail_transaction(active, failure::copy);
			return false;
		}
		active.completed_eye_mask |= 1u << eye;
		active.current_view = auxiliary_scene::no_view;
		{
			std::lock_guard lock(state_mutex);
			evidence.completed_eye_mask = active.completed_eye_mask;
			evidence.source_textures[eye] = reinterpret_cast<std::uintptr_t>(
				active.sources[eye].Get());
		}
		return true;
	}

	bool finish_pending_left(transaction& active) noexcept
	{
		if(!active || active.failed || !active.pending_left || !active.auxiliary_complete ||
			active.current_view!=auxiliary_scene::no_view || active.completed_eye_mask!=1 ||
			GetCurrentThreadId()!=active.owner_thread_id) return false;
		h2_gpu_context_lock lock;
		if(!lock) {fail_transaction(active,failure::copy);return false;}
		const auto copy_started=timing_now_ns();
		eye_composition::event event{active.claim.views,active.records.pair_id,active.device_generation,
			0,active.source_description.Width,active.source_description.Height,
			eye_composition::model_origins_for(active.claim.views,active.records)};
		event.scene_depth=active.pending_left_image.depth_view.Get();
		event.auxiliary=&active.auxiliary;event.auxiliary_image=active.auxiliary_image.view.Get();
		// Preserve transport's exact native-source identity and cached command
		// lists. The scope has already retained its own image before this restore.
		if(!active.pending_left_native || active.pending_left_native==active.pending_left_image.color)
			{fail_transaction(active,failure::copy);return false;}
		active.context->CopyResource(active.pending_left_native.Get(),active.pending_left_image.color.Get());
		if(!native_render_session::active().copy_eye(active.records.pair_id,0,active.pending_left_native.Get(),
			active.pending_left_target,active.context.Get(),&event))
			{fail_transaction(active,failure::copy);return false;}
		active.pending_left=false;
		record_timing(production_native_copy_timing_samples[0],production_native_copy_timing_last_us[0],
			production_native_copy_timing_max_us[0],production_native_copy_timing_total_us[0],copy_started);
		production_eye_copies.fetch_add(1,std::memory_order_relaxed);
		return true;
	}

	void end(transaction& active) noexcept
	{
		if (!active) return;
		if(GetCurrentThreadId()==active.owner_thread_id)active.thermal_world_lease.restore();
		if(active.failed) active.auxiliary_history.valid=false;
		finish_dynamic_upload(active);
		if (active.material_reuse_left_diagnostic)
		{
			const auto complete = active.material_reuse_left_snapshot_ready &&
				active.material_reuse_left_copy_counts[0] != 0 &&
				active.material_reuse_left_copy_counts[0] ==
					active.material_reuse_left_copy_counts[1];
			if (!complete && !active.material_reuse_left_failed)
			{
				active.material_reuse_left_failed = true;
				material_reuse_left_failures.fetch_add(1,
					std::memory_order_relaxed);
			}
		}
		const auto timing_started_ns = active.production ?
			active.production_transaction_timing_started_ns : 0;
		active.production_transaction_timing_started_ns = 0;
		const auto timing_scope = gsl::finally([timing_started_ns]() noexcept
		{
			record_timing(production_transaction_timing_samples,
				production_transaction_timing_last_us,
				production_transaction_timing_max_us,
				production_transaction_timing_total_us, timing_started_ns);
		});
		if (active.eye_resource_pair_active)
		{
			h2_gpu_context_lock context_lock;
			if (!context_lock)
			{
				fail_transaction(active, failure::eye_resource);
				engine_stereo_eye_resources::invalidate_device(active.context.Get(),
					active.device_generation);
			}
			else if (!active.failed && !active.eye_resource_eye_active &&
				active.completed_eye_mask == 0x3)
			{
				if (!engine_stereo_eye_resources::end_pair(active.records.pair_id))
				{
					fail_transaction(active, failure::eye_resource);
				}
			}
			else
			{
				engine_stereo_eye_resources::cancel_pair(active.records.pair_id);
			}
			active.eye_resource_pair_active = false;
			active.eye_resource_eye_active = false;
		}
		if (active.dynamic_index_scope_active)
		{
			if (GetCurrentThreadId() != active.owner_thread_id ||
				!restore_dynamic_index_scope(active))
			{
				fail_transaction(active, failure::dynamic_mesh);
			}
		}
		publish_dynamic_index_boundary_counts(active);
		if (active.scene_batch_probe_pair_active)
		{
			engine_stereo_scene_batch_probe::end_pair(active.records.pair_id,
				!active.failed && active.completed_eye_mask == 0x3);
			active.scene_batch_probe_pair_active = false;
			active.scene_batch_probe_eye_active = false;
		}
		if (active.gpu_census_pair_active)
		{
			// If an eye never reached end_view(), end_pair deliberately marks only
			// the census as failed; it cannot publish or cancel the native pair.
			(void)engine_stereo_gpu_census::end_pair(active.records.pair_id);
			active.gpu_census_pair_active = false;
			active.gpu_census_eye_active = false;
		}
		if (active.material_buffer_probe_pair_active)
		{
			(void)engine_stereo_material_buffer_probe::end_pair(
				active.records.pair_id);
			active.material_buffer_probe_pair_active = false;
		}
		if (active.particle_buffer_probe_pair_active)
		{
			(void)engine_stereo_particle_buffer_probe::end_pair(
				active.records.pair_id);
			active.particle_buffer_probe_pair_active = false;
		}
		if (active.effect_timeline_pair_active)
		{
			engine_stereo_effect_timeline::end_pair(active.records.pair_id,
				!active.failed && active.completed_eye_mask == 0x3);
			active.effect_timeline_pair_active = false;
			active.effect_timeline_eye_active = false;
		}
		if (active.production)
		{
			if (active_transaction != &active || active.current_view < auxiliary_scene::view_count ||
				active.completed_eye_mask != 0x3 ||
				!native_render_session::active().pair_published(active.records.pair_id))
			{
				fail_transaction(active, failure::copy);
			}
			if (!active.failed && active.temporal_history.valid)
			{
				std::lock_guard lock(temporal_history_mutex);
				if (engine_stereo_view::commit_temporal_history(temporal_history_state,
					active.temporal_history))
				{
					temporal_history_commits.fetch_add(1, std::memory_order_relaxed);
				}
				else
				{
					temporal_history_failures.fetch_add(1, std::memory_order_relaxed);
					fail_transaction(active, failure::record);
				}
			}
			if (active.ssr_consumer_probe_pair_active)
			{
				engine_stereo_ssr_consumer_probe::end_pair(active.records.pair_id,
					!active.failed);
				active.ssr_consumer_probe_pair_active = false;
			}
			if (active.failed)
			{
				publish_failure(active);
				// An incomplete renderer-owned pair has never been visible to the
				// compositor and must return to the ring. Only a pair already published
				// by end_view(1) needs quarantine while external Submit ownership drains.
				if (native_render_session::active().pair_published(
					active.records.pair_id))
				{
					native_render_session::active().quarantine_pair(
						active.records.pair_id);
				}
				else
				{
					if (!native_render_session::active().discard_unpublished_pair(
						active.records.pair_id))
					{
						// A concurrent or otherwise invalid state is not proven safe to reuse.
						native_render_session::active().quarantine_pair(
							active.records.pair_id);
					}
				}
				production_failures.fetch_add(1, std::memory_order_relaxed);
			}
			else
			{
				publish_model_list_census(active);
				production_completions.fetch_add(1, std::memory_order_relaxed);
			}
			native_post_aa::finish_pair(active.records.pair_id, !active.failed);
			engine_stereo_binding::release(active.claim);
			if (active_transaction == &active) active_transaction = nullptr;
			production_active.store(false, std::memory_order_release);
			retire_transaction_preserving_records(active);
			return;
		}
		if (active_transaction != &active || active.current_view < auxiliary_scene::view_count ||
			active.completed_eye_mask != 0x3 || !resources.context || !resources.query)
		{
			fail_transaction(active, failure::query);
		}
		if (!active.failed)
		{
			publish_model_list_census(active);
			resources.context->End(resources.query.Get());
			state.store(gate_state::gpu_pending, std::memory_order_release);
			std::lock_guard lock(state_mutex);
			evidence.state = gate_state::gpu_pending;
		}
		else
		{
			publish_failure(active);
			fail_global(active.error == failure::none ? failure::record : active.error);
		}
		engine_stereo_binding::release(active.claim);
		if (active_transaction == &active) active_transaction = nullptr;
		retire_transaction_preserving_records(active);
	}

	void on_present_pre(const d3d11::present_event& event) noexcept
	{
		if (state.load(std::memory_order_acquire) != gate_state::gpu_pending) return;
		std::lock_guard lock(state_mutex);
		if (!event.graphics || event.graphics.generation != resources.generation ||
			event.graphics.context.Get() != resources.context.Get())
		{
			fail_global_locked(failure::device);
			return;
		}
		++evidence.query_polls;
		evidence.query_poll_result = resources.context->GetData(resources.query.Get(),
			nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (evidence.query_poll_result == S_FALSE)
		{
			if (evidence.query_polls >= maximum_query_polls)
				fail_global_locked(failure::timeout);
			return;
		}
		if (FAILED(evidence.query_poll_result))
		{
			fail_global_locked(failure::query);
			return;
		}
		for (std::size_t eye{}; eye < resources.staging.size(); ++eye)
		{
			if (!hash_staging(resources.staging[eye].Get(), evidence.map_results[eye],
				evidence.content_hashes[eye], evidence.nonzero_bytes[eye]))
			{
				fail_global_locked(failure::readback);
				return;
			}
		}
		evidence.eyes_distinct = evidence.content_hashes[0] != evidence.content_hashes[1];
		if (!evidence.eyes_distinct || evidence.nonzero_bytes[0] == 0 ||
			evidence.nonzero_bytes[1] == 0)
		{
			fail_global_locked(failure::content);
			return;
		}
		evidence.error = failure::none;
		evidence.state = gate_state::complete;
		evidence.completions = completions.fetch_add(1, std::memory_order_relaxed) + 1;
		evidence.failures = failures.load(std::memory_order_relaxed);
		state.store(gate_state::complete, std::memory_order_release);
		resources = {};
	}

	void request_temporal_history_reset() noexcept
	{
		temporal_history_reset_requests.fetch_add(1, std::memory_order_relaxed);
		temporal_history_reset_pending.store(true, std::memory_order_release);
	}

	void set_temporal_history_current_frame_diagnostic(const bool enabled) noexcept
	{
		const auto previous = temporal_history_current_frame_diagnostic.exchange(
			enabled, std::memory_order_acq_rel);
		if (previous != enabled) request_temporal_history_reset();
	}

	void set_material_reuse_left_diagnostic(const bool enabled) noexcept
	{
		material_reuse_left_diagnostic.store(enabled, std::memory_order_release);
	}

	void observe_resource_operation(
		const engine_stereo_resource_ops::event& event) noexcept
	{
		engine_stereo_material_buffer_probe::observe_resource_operation(event);
		if (material_reuse_left_reentrant ||
			event.operation != engine_stereo_resource_ops::api::copy_resource ||
			event.caller != h2_material_buffer_copy_caller || event.context == nullptr ||
			event.destination == nullptr || event.source == nullptr)
		{
			return;
		}

		auto* const active = active_transaction;
		if (active == nullptr || !active->production || active->failed ||
			!active->material_reuse_left_diagnostic || active->current_view >= 2 ||
			active->context.Get() != event.context ||
			active->owner_thread_id != GetCurrentThreadId())
		{
			return;
		}

		D3D11_RESOURCE_DIMENSION destination_dimension{};
		D3D11_RESOURCE_DIMENSION source_dimension{};
		event.destination->GetType(&destination_dimension);
		event.source->GetType(&source_dimension);
		if (destination_dimension != D3D11_RESOURCE_DIMENSION_BUFFER ||
			source_dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
		{
			return;
		}
		auto* const destination = static_cast<ID3D11Buffer*>(event.destination);
		auto* const source = static_cast<ID3D11Buffer*>(event.source);
		D3D11_BUFFER_DESC destination_description{};
		D3D11_BUFFER_DESC source_description{};
		destination->GetDesc(&destination_description);
		source->GetDesc(&source_description);
		const auto exact_material_copy =
			destination_description.ByteWidth == h2_material_buffer_bytes &&
			(destination_description.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0 &&
			source_description.ByteWidth == h2_material_buffer_bytes &&
			(source_description.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0 &&
			(source_description.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0 &&
			source_description.StructureByteStride == 16;
		if (!exact_material_copy) return;

		const auto eye = active->current_view;
		if (eye == 0)
		{
			if (!active->material_reuse_left_snapshot)
			{
				Microsoft::WRL::ComPtr<ID3D11Device> device;
				event.context->GetDevice(device.GetAddressOf());
				if (!device || FAILED(device->CreateBuffer(&destination_description, nullptr,
					active->material_reuse_left_snapshot.GetAddressOf())))
				{
					active->material_reuse_left_failed = true;
					material_reuse_left_failures.fetch_add(1,
						std::memory_order_relaxed);
					return;
				}
			}
			material_reuse_left_reentrant = true;
			event.context->CopyResource(active->material_reuse_left_snapshot.Get(),
				destination);
			material_reuse_left_reentrant = false;
			active->material_reuse_left_snapshot_ready = true;
			++active->material_reuse_left_copy_counts[0];
			material_reuse_left_captures.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		if (!active->material_reuse_left_snapshot_ready ||
			!active->material_reuse_left_snapshot)
		{
			if (!active->material_reuse_left_failed)
			{
				active->material_reuse_left_failed = true;
				material_reuse_left_failures.fetch_add(1, std::memory_order_relaxed);
			}
			return;
		}
		material_reuse_left_reentrant = true;
		event.context->CopyResource(destination,
			active->material_reuse_left_snapshot.Get());
		material_reuse_left_reentrant = false;
		++active->material_reuse_left_copy_counts[1];
		material_reuse_left_replacements.fetch_add(1, std::memory_order_relaxed);
	}

	void invalidate_device(ID3D11DeviceContext* const context,
		const std::uint64_t generation) noexcept
	{
		native_post_aa::invalidate_device(context, generation);
		engine_stereo_material_buffer_probe::cancel(context, generation);
		engine_stereo_particle_buffer_probe::cancel(context, generation);
		engine_stereo_gpu_census::cancel();
		engine_stereo_constant_buffer_probe::set_history_tracking_enabled(false);
		gpu_census_delay_remaining.store(0, std::memory_order_release);
		gpu_census_launch_attempted.store(true, std::memory_order_release);
		gpu_census_applied_token.store(
			gpu_census_request_token.load(std::memory_order_acquire),
			std::memory_order_release);
		{
			std::lock_guard lock(temporal_history_mutex);
			temporal_history_state = {};
			temporal_history_reset_pending.store(false, std::memory_order_release);
		}
		if (resources.context.Get() != context || resources.generation != generation) return;
		fail_global(failure::device);
		resources = {};
		production_active.store(false, std::memory_order_release);
	}

	report get_report() noexcept
	{
		std::lock_guard lock(state_mutex);
		auto output = evidence;
		output.state = state.load(std::memory_order_acquire);
		output.error = error.load(std::memory_order_acquire);
		output.attempts = attempts.load(std::memory_order_acquire);
		output.completions = completions.load(std::memory_order_acquire);
		output.failures = failures.load(std::memory_order_acquire);
		output.production_attempts = production_attempts.load(std::memory_order_acquire);
		output.production_completions = production_completions.load(std::memory_order_acquire);
		output.production_failures = production_failures.load(std::memory_order_acquire);
		output.production_eye_copies = production_eye_copies.load(std::memory_order_acquire);
		for (std::size_t eye = 0; eye < output.display_routes.size(); ++eye)
		{
			const auto packed = display_routes[eye].load(std::memory_order_acquire);
			output.display_routes[eye] = {packed >> 16, packed & 0xffff};
		}
		output.display_transform_completions = display_transform_completions.load(std::memory_order_acquire);
		output.display_transform_failures = display_transform_failures.load(std::memory_order_acquire);
		output.display_transform_error = display_transform_error.load(std::memory_order_acquire);
		output.display_format = display_format.load(std::memory_order_relaxed);
		output.display_bind_flags = display_bind_flags.load(std::memory_order_relaxed);
		output.production_transaction_timing_samples =
			production_transaction_timing_samples.load(std::memory_order_acquire);
		output.production_transaction_timing_last_us =
			production_transaction_timing_last_us.load(std::memory_order_acquire);
		output.production_transaction_timing_max_us =
			production_transaction_timing_max_us.load(std::memory_order_acquire);
		output.production_transaction_timing_total_us =
			production_transaction_timing_total_us.load(std::memory_order_acquire);
		output.production_context_lock_acquires =
			production_context_lock_acquires.load(std::memory_order_acquire);
		output.production_context_lock_failures =
			production_context_lock_failures.load(std::memory_order_acquire);
		output.production_context_lock_wait_total_us =
			production_context_lock_wait_total_us.load(std::memory_order_acquire);
		output.production_context_lock_wait_max_us =
			production_context_lock_wait_max_us.load(std::memory_order_acquire);
		output.production_context_lock_wait_last_us =
			production_context_lock_wait_last_us.load(std::memory_order_acquire);
		output.production_context_lock_last_result =
			production_context_lock_last_result.load(std::memory_order_acquire);
		for (std::size_t eye{}; eye < auxiliary_scene::view_count; ++eye)
		{
			output.production_eye_timing_samples[eye] =
				production_eye_timing_samples[eye].load(std::memory_order_acquire);
			output.production_eye_timing_last_us[eye] =
				production_eye_timing_last_us[eye].load(std::memory_order_acquire);
			output.production_eye_timing_max_us[eye] =
				production_eye_timing_max_us[eye].load(std::memory_order_acquire);
			output.production_eye_timing_total_us[eye] =
				production_eye_timing_total_us[eye].load(std::memory_order_acquire);
			output.production_owner_invoke_timing_samples[eye] =
				production_owner_invoke_timing_samples[eye].load(std::memory_order_acquire);
			output.production_owner_invoke_timing_last_us[eye] =
				production_owner_invoke_timing_last_us[eye].load(std::memory_order_acquire);
			output.production_owner_invoke_timing_max_us[eye] =
				production_owner_invoke_timing_max_us[eye].load(std::memory_order_acquire);
			output.production_owner_invoke_timing_total_us[eye] =
				production_owner_invoke_timing_total_us[eye].load(std::memory_order_acquire);
			output.production_dynamic_view_copy_timing_samples[eye] =
				production_dynamic_view_copy_timing_samples[eye].load(
					std::memory_order_acquire);
			output.production_dynamic_view_copy_timing_last_us[eye] =
				production_dynamic_view_copy_timing_last_us[eye].load(
					std::memory_order_acquire);
			output.production_dynamic_view_copy_timing_max_us[eye] =
				production_dynamic_view_copy_timing_max_us[eye].load(
					std::memory_order_acquire);
			output.production_dynamic_view_copy_timing_total_us[eye] =
				production_dynamic_view_copy_timing_total_us[eye].load(
					std::memory_order_acquire);
			output.production_native_copy_timing_samples[eye] =
				production_native_copy_timing_samples[eye].load(std::memory_order_acquire);
			output.production_native_copy_timing_last_us[eye] =
				production_native_copy_timing_last_us[eye].load(std::memory_order_acquire);
			output.production_native_copy_timing_max_us[eye] =
				production_native_copy_timing_max_us[eye].load(std::memory_order_acquire);
			output.production_native_copy_timing_total_us[eye] =
				production_native_copy_timing_total_us[eye].load(std::memory_order_acquire);
		}
		output.production_dynamic_restore_timing_samples =
			production_dynamic_restore_timing_samples.load(std::memory_order_acquire);
		output.production_dynamic_restore_timing_last_us =
			production_dynamic_restore_timing_last_us.load(std::memory_order_acquire);
		output.production_dynamic_restore_timing_max_us =
			production_dynamic_restore_timing_max_us.load(std::memory_order_acquire);
		output.production_dynamic_restore_timing_total_us =
			production_dynamic_restore_timing_total_us.load(std::memory_order_acquire);
		output.temporal_history_preparations = temporal_history_preparations.load(
			std::memory_order_acquire);
		output.temporal_history_seeds = temporal_history_seeds.load(
			std::memory_order_acquire);
		output.temporal_history_commits = temporal_history_commits.load(
			std::memory_order_acquire);
		output.temporal_history_failures = temporal_history_failures.load(
			std::memory_order_acquire);
		output.temporal_history_reset_requests = temporal_history_reset_requests.load(
			std::memory_order_acquire);
		output.temporal_history_reset_applications =
			temporal_history_reset_applications.load(std::memory_order_acquire);
		output.temporal_history_reset_pending = temporal_history_reset_pending.load(
			std::memory_order_acquire);
		output.temporal_history_current_frame_diagnostic =
			temporal_history_current_frame_diagnostic.load(std::memory_order_acquire);
		output.temporal_history_current_frame_pairs =
			temporal_history_current_frame_pairs.load(std::memory_order_acquire);
		output.material_reuse_left_diagnostic =
			material_reuse_left_diagnostic.load(std::memory_order_acquire);
		output.material_reuse_left_pairs = material_reuse_left_pairs.load(
			std::memory_order_acquire);
		output.material_reuse_left_captures = material_reuse_left_captures.load(
			std::memory_order_acquire);
		output.material_reuse_left_replacements =
			material_reuse_left_replacements.load(std::memory_order_acquire);
		output.material_reuse_left_failures = material_reuse_left_failures.load(
			std::memory_order_acquire);
		output.material_reuse_left_last_pair = material_reuse_left_last_pair.load(
			std::memory_order_acquire);
		{
			std::lock_guard history_lock(temporal_history_mutex);
			output.temporal_history_last_pair = temporal_history_state.pair_id;
			output.temporal_history_ready = temporal_history_state.ready;
		}
		output.dynamic_index_captures = dynamic_index_captures.load(
			std::memory_order_acquire);
		output.dynamic_index_rebases = dynamic_index_rebases.load(
			std::memory_order_acquire);
		output.dynamic_index_restores = dynamic_index_restores.load(
			std::memory_order_acquire);
		output.dynamic_index_validations = dynamic_index_validations.load(
			std::memory_order_acquire);
		output.dynamic_index_failures = dynamic_index_failures.load(
			std::memory_order_acquire);
		output.dynamic_index_data_identity = dynamic_index_data_identity.load(
			std::memory_order_acquire);
		output.dynamic_index_last_left_boundaries =
			dynamic_index_last_left_boundaries.load(std::memory_order_acquire);
		output.dynamic_index_last_right_boundaries =
			dynamic_index_last_right_boundaries.load(std::memory_order_acquire);
		output.dynamic_index_last_failure = static_cast<
			engine_stereo_dynamic_arena::failure>(dynamic_index_last_failure.load(
				std::memory_order_acquire));
		{
			std::lock_guard upload_lock(dynamic_upload_mutex);
			output.dynamic_upload = dynamic_upload_evidence;
		}
		const auto census_request = gpu_census_request_token.load(
			std::memory_order_acquire);
		const auto census_applied = gpu_census_applied_token.load(
			std::memory_order_acquire);
		output.gpu_census_request_sequence =
			engine_stereo_gpu_census::request_control::sequence(census_request);
		output.gpu_census_applied_sequence =
			engine_stereo_gpu_census::request_control::sequence(census_applied);
		output.gpu_census_requested_delay =
			engine_stereo_gpu_census::request_control::delay(census_request);
		output.gpu_census_delay_remaining = gpu_census_delay_remaining.load(
			std::memory_order_acquire);
		output.gpu_census_reset_attempts = gpu_census_reset_attempts.load(
			std::memory_order_acquire);
		output.gpu_census_reset_failures = gpu_census_reset_failures.load(
			std::memory_order_acquire);
		output.gpu_census_launch_attempts = gpu_census_launch_attempts.load(
			std::memory_order_acquire);
		output.gpu_census_launch_failures = gpu_census_launch_failures.load(
			std::memory_order_acquire);
		output.gpu_census_last_capture_pair = gpu_census_last_capture_pair.load(
			std::memory_order_acquire);
		output.gpu_census_rearm_applying = gpu_census_rearm_applying.load(
			std::memory_order_acquire);
		output.gpu_census_launch_attempted = gpu_census_launch_attempted.load(
			std::memory_order_acquire);
		output.model_state_sync_calls = model_state_sync_calls.load(
			std::memory_order_acquire);
		output.model_state_sync_matches = model_state_sync_matches.load(
			std::memory_order_acquire);
		output.model_state_foreign_bypasses = model_state_foreign_bypasses.load(
			std::memory_order_acquire);
		output.model_state_contract_mismatches =
			model_state_contract_mismatches.load(std::memory_order_acquire);
		output.model_state_backend_registrations =
			model_state_backend_registrations.load(std::memory_order_acquire);
		output.model_state_backend_reuses = model_state_backend_reuses.load(
			std::memory_order_acquire);
		output.model_state_backend_maximum_active =
			model_state_backend_maximum_active.load(std::memory_order_acquire);
		output.model_state_last_failure_stage = static_cast<model_state_failure_stage>(
			model_state_last_failure_stage.load(std::memory_order_acquire));
		output.model_state_failure_backend = model_state_failure_backend.load(
			std::memory_order_acquire);
		output.model_state_failure_expected_backend =
			model_state_failure_expected_backend.load(std::memory_order_acquire);
		output.model_state_failure_thread = model_state_failure_thread.load(
			std::memory_order_acquire);
		output.model_state_failure_expected_thread =
			model_state_failure_expected_thread.load(std::memory_order_acquire);
		output.model_state_last_failure = static_cast<engine_stereo_view::
			backend_model_state_sync_failure>(model_state_last_failure.load(
				std::memory_order_acquire));
		output.model_state_last_failure_eye = model_state_last_failure_eye.load(
			std::memory_order_acquire);
		output.model_state_failure_primary_source =
			model_state_failure_primary_source.load(std::memory_order_acquire);
		output.model_state_failure_expected_primary_source =
			model_state_failure_expected_primary_source.load(std::memory_order_acquire);
		output.model_state_failure_rebase_source =
			model_state_failure_rebase_source.load(std::memory_order_acquire);
		output.model_state_failure_expected_rebase_source =
			model_state_failure_expected_rebase_source.load(std::memory_order_acquire);
		for (std::size_t component{}; component < 3; ++component)
		{
			output.model_state_failure_primary_origin[component] = float_from_bits(
				model_state_failure_primary_origin_bits[component].load(
					std::memory_order_acquire));
			output.model_state_failure_expected_primary_origin[component] =
				float_from_bits(model_state_failure_expected_primary_origin_bits[component].load(
					std::memory_order_acquire));
			output.model_state_failure_relative_eye_offset[component] = float_from_bits(
				model_state_failure_relative_eye_offset_bits[component].load(
					std::memory_order_acquire));
			output.model_state_failure_expected_relative_eye_offset[component] =
				float_from_bits(
					model_state_failure_expected_relative_eye_offset_bits[component].load(
						std::memory_order_acquire));
		}
		output.model_cache_invalidations = model_cache_invalidations.load(
			std::memory_order_acquire);
		output.model_cache_already_empty = model_cache_already_empty.load(
			std::memory_order_acquire);
		output.depth_hack_projection_calls = depth_hack_projection_calls.load(
			std::memory_order_acquire);
		output.depth_hack_projection_restores = depth_hack_projection_restores.load(
			std::memory_order_acquire);
		output.depth_hack_projection_bypasses = depth_hack_projection_bypasses.load(
			std::memory_order_acquire);
		output.depth_hack_projection_foreign_bypasses =
			depth_hack_projection_foreign_bypasses.load(std::memory_order_acquire);
		output.depth_hack_projection_contract_mismatches =
			depth_hack_projection_contract_mismatches.load(std::memory_order_acquire);
		output.model_list_pair_id = model_list_pair_id.load(std::memory_order_acquire);
		for (std::size_t eye{}; eye < auxiliary_scene::view_count; ++eye)
		{
			output.model_state_eye_matches[eye] = model_state_eye_matches[eye].load(
				std::memory_order_acquire);
			output.model_cache_eye_invalidations[eye] =
				model_cache_eye_invalidations[eye].load(std::memory_order_acquire);
			output.model_cache_last_values[eye] = model_cache_last_values[eye].load(
				std::memory_order_acquire);
			output.depth_hack_projection_eye_restores[eye] =
				depth_hack_projection_eye_restores[eye].load(std::memory_order_acquire);
			output.depth_hack_projection_near[eye] = float_from_bits(
				depth_hack_projection_near_bits[eye].load(std::memory_order_acquire));
			for (std::size_t term{}; term < 4; ++term)
			{
				output.depth_hack_projection_previous_terms[eye][term] = float_from_bits(
					depth_hack_projection_previous_bits[eye][term].load(
						std::memory_order_acquire));
				output.depth_hack_projection_restored_terms[eye][term] = float_from_bits(
					depth_hack_projection_restored_bits[eye][term].load(
						std::memory_order_acquire));
			}
			output.model_list_eye_origin_matches[eye] =
				model_list_eye_origin_matches[eye].load(std::memory_order_acquire);
			output.model_list_center_origin_matches[eye] =
				model_list_center_origin_matches[eye].load(std::memory_order_acquire);
			output.model_list_zero_origins[eye] = model_list_zero_origins[eye].load(
				std::memory_order_acquire);
			output.model_list_other_origins[eye] = model_list_other_origins[eye].load(
				std::memory_order_acquire);
			output.camera_model_boundary_rewrites[eye] =
				camera_model_boundary_rewrites[eye].load(
				std::memory_order_acquire);
			output.camera_model_boundary_foreign[eye] =
				camera_model_boundary_foreign[eye].load(
				std::memory_order_acquire);
			output.camera_model_final_eye[eye] = camera_model_final_eye[eye].load(
				std::memory_order_acquire);
			output.camera_model_final_inactive[eye] =
				camera_model_final_inactive[eye].load(
				std::memory_order_acquire);
			output.camera_model_final_other[eye] = camera_model_final_other[eye].load(
				std::memory_order_acquire);
			for (std::size_t component{}; component < 3; ++component)
			{
				output.model_primary_origins[eye][component] = float_from_bits(
					model_primary_origin_bits[eye][component].load(std::memory_order_acquire));
				output.model_relative_eye_offsets[eye][component] = float_from_bits(
					model_relative_eye_offset_bits[eye][component].load(
						std::memory_order_acquire));
			}
		}
		return output;
	}

	bool schedule_gpu_census(const std::uint32_t delay_pairs) noexcept
	{
		if (delay_pairs > 900) return false;
		auto observed = gpu_census_request_token.load(std::memory_order_acquire);
		for (;;)
		{
			const auto previous_sequence =
				engine_stereo_gpu_census::request_control::sequence(observed);
			if (previous_sequence == (std::numeric_limits<std::uint32_t>::max)())
			{
				return false;
			}
			const auto requested = engine_stereo_gpu_census::request_control::make_token(
				previous_sequence + 1, delay_pairs);
			if (gpu_census_request_token.compare_exchange_weak(observed, requested,
				std::memory_order_release, std::memory_order_acquire))
			{
				break;
			}
		}
		// The control thread publishes only. Reset, pre-roll countdown, and launch
		// are serialized by the next exact production scene-owner boundary.
		return true;
	}

	const char* to_string(const gate_state value) noexcept
	{
		switch (value)
		{
		case gate_state::waiting: return "waiting";
		case gate_state::active: return "active";
		case gate_state::gpu_pending: return "gpu_pending";
		case gate_state::complete: return "complete";
		case gate_state::failed: return "failed";
		default: return "unknown";
		}
	}

	const char* to_string(const failure value) noexcept
	{
		switch (value)
		{
		case failure::none: return "none";
		case failure::prerequisite: return "prerequisite";
		case failure::claim: return "claim";
		case failure::record: return "record";
		case failure::thread: return "thread";
		case failure::target_missing: return "target_missing";
		case failure::target_contract: return "target_contract";
		case failure::source_contract: return "source_contract";
		case failure::resource_creation: return "resource_creation";
		case failure::copy: return "copy";
		case failure::query: return "query";
		case failure::readback: return "readback";
		case failure::content: return "content";
		case failure::timeout: return "timeout";
		case failure::device: return "device";
		case failure::model_state: return "model_state";
		case failure::dynamic_mesh: return "dynamic_mesh";
		case failure::eye_resource: return "eye_resource";
		case failure::display_transform: return "display_transform";
		default: return "unknown";
		}
	}

	const char* to_string(const model_state_failure_stage value) noexcept
	{
		switch (value)
		{
		case model_state_failure_stage::none: return "none";
		case model_state_failure_stage::invalid_backend: return "invalid_backend";
		case model_state_failure_stage::thread: return "thread";
		case model_state_failure_stage::backend_capacity: return "backend_capacity";
		case model_state_failure_stage::sync_contract: return "sync_contract";
		case model_state_failure_stage::camera_model_rebase:
			return "camera_model_rebase";
		default: return "unknown";
		}
	}

}
