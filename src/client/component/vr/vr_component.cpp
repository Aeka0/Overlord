#include <std_include.hpp>

#include "loader/component_loader.hpp"

#include "diagnostics.hpp"
#include "diagnostics/renderer_evidence.hpp"
#include "debug_options.hpp"
#include "desktop_mirror.hpp"
#include "desktop_mirror_layout.hpp"
#include "engine_backend_probe.hpp"
#include "engine_stereo_binding.hpp"
#include "engine_stereo_backend_target.hpp"
#include "engine_stereo_backend_view.hpp"
#include "engine_stereo_bridge.hpp"
#include "engine_stereo_draw_indexed.hpp"
#include "engine_stereo_eye_resources.hpp"
#include "engine_stereo_execution.hpp"
#include "engine_stereo_output_merger.hpp"
#include "engine_stereo_owner_pass.hpp"
#include "engine_stereo_probe.hpp"
#include "engine_stereo_resource_ops.hpp"
#include "engine_stereo_renderer.hpp"
#include "engine_view_probe.hpp"
#include "head_pose_bridge.hpp"
#include "native_menu.hpp"
#include "vr_runtime.hpp"

#include "component/command.hpp"
#include "component/console.hpp"
#include "component/d3d11.hpp"
#include "component/gui/gui.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "loader/target_identity.hpp"
#include "utils/hook_validation.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

namespace vr
{
	namespace
	{
		std::atomic_bool alive{false};
		std::atomic_bool accepting_work{false};
		std::atomic_bool shutdown_started{false};
		std::atomic_bool callbacks_finalized{false};
		game::dvar_t* vr_enable{};
		game::dvar_t* vr_engine_probe{};
		game::dvar_t* vr_engine_probe_trace{};
		game::dvar_t* vr_head_tracking{};
		game::dvar_t* vr_world_scale{};
		game::dvar_t* vr_engine_swap_eyes{};
		game::dvar_t* vr_desktop_fov{};

		std::mutex listener_mutex;
		std::atomic_bool soft_freeze_reported{false};
		std::atomic_bool stall_watchdog_stop{false};
		std::thread stall_watchdog;
		d3d11::listener_token device_created_token{};
		d3d11::listener_token device_destroying_token{};
		d3d11::listener_token present_pre_token{};
		d3d11::listener_token present_post_token{};
		d3d11::listener_token resize_before_token{};
		void finalize_component_shutdown_if_ready() noexcept;

		bool is_alive() noexcept
		{
			return alive.load(std::memory_order_acquire);
		}

		bool is_accepting_work() noexcept
		{
			return accepting_work.load(std::memory_order_acquire);
		}


		void on_device_created(const d3d11::device_snapshot& graphics)
		{
			if (!is_accepting_work()) return;
			engine_backend_probe::set_device_generation(graphics.generation);
			const auto output_merger_installed =
				engine_stereo_output_merger::install(graphics.context.Get(),
					graphics.generation);
			if (!output_merger_installed)
			{
				console::error("[VR] D3D11 output-merger observation hook was not installed; "
					"native stereo target evidence remains hard-unarmed\n");
			}
			const auto resource_ops_installed =
				engine_stereo_resource_ops::install(graphics.context.Get(),
					graphics.generation);
			if (!resource_ops_installed)
			{
				console::error("[VR] D3D11 resource-operation census hooks were not "
					"installed; intermediate-target isolation evidence remains hard-unarmed\n");
			}
			const auto draw_indexed_installed =
				engine_stereo_draw_indexed::install(graphics.context.Get(),
					graphics.generation);
			if (!draw_indexed_installed)
			{
				console::error("[VR] D3D11 DrawIndexed observation hook was not installed; "
					"backend replayability evidence remains hard-unarmed\n");
			}
			const auto eye_resources_installed = output_merger_installed &&
				resource_ops_installed && draw_indexed_installed &&
				engine_stereo_eye_resources::install(graphics.context.Get(),
					graphics.generation);
			if (!eye_resources_installed)
			{
				console::error("[VR] target-91 eye-local resource hooks were not installed; "
					"native stereo pairs will remain fail-closed\n");
			}
			auto* const execution_context = output_merger_installed &&
				draw_indexed_installed ? graphics.context.Get() : nullptr;
			const auto execution_installed = engine_stereo_execution::install(
				execution_context, graphics.generation);
			if (!execution_installed)
			{
				console::error("[VR] full-frame D3D11 execution census hooks were not "
					"installed; scene execution evidence remains hard-unarmed\n");
			}
			// Runtime initialization remains deferred to Present-pre.
		}

		void on_device_destroying(const d3d11::device_snapshot& graphics)
		{
			// Preserve the eye-local COM graph until any synchronous owner hook has
			// returned, before its shared output-merger/resource-op dependencies are
			// invalidated below.
			engine_stereo_eye_resources::invalidate_device(graphics.context.Get(),
				graphics.generation);
			if (!is_accepting_work())
			{
				return;
			}

			engine_stereo_probe::record_device_destroying(graphics.generation);
			engine_stereo_resource_ops::invalidate_device(graphics.context.Get(),
				graphics.generation);
			engine_stereo_execution::invalidate_device(graphics.context.Get(),
				graphics.generation);
			engine_stereo_owner_pass::invalidate_device(graphics.context.Get(),
				graphics.generation);
			engine_stereo_output_merger::invalidate_device(graphics.context.Get(),
				graphics.generation);
			engine_stereo_draw_indexed::invalidate_device(graphics.context.Get(),
				graphics.generation);
			engine_backend_probe::set_device_generation(0);
			engine_stereo_bridge::invalidate_views();
			head_pose_bridge::invalidate_pose();
			runtime::get().on_device_destroying(graphics);
		}

		void on_present_pre(const d3d11::present_event& event)
		{
			if (!is_accepting_work())
			{
				return;
			}

			engine_stereo_owner_pass::on_present_pre(event);
			engine_stereo_execution::on_present_pre(event.frame_index,
				event.graphics.generation, GetCurrentThreadId());
			engine_stereo_backend_target::on_present_pre(event.frame_index,
				event.graphics.generation, GetCurrentThreadId());
			diagnostics::record_present_pre(event);
			engine_stereo_probe::record_present(event.frame_index, event.graphics.generation);
			auto& vr_runtime = runtime::get();
			vr_runtime.capture_present(event);
			runtime::get().on_present(event);
		}

		void on_present_post(const d3d11::present_event& event, const HRESULT result)
		{
			if (is_alive())
			{
				engine_stereo_backend_target::on_present_post(event.frame_index,
					event.graphics.generation, GetCurrentThreadId(), result);
				diagnostics::record_present_post(event, result);
				engine_backend_probe::record_present_result(event.graphics.generation,
					event.frame_index, result);
				auto& vr_runtime = runtime::get();
				vr_runtime.on_present_post(event, result);
				engine_stereo_execution::on_present_post(event.frame_index,
					event.graphics.generation, GetCurrentThreadId(), result);
				finalize_component_shutdown_if_ready();
			}
		}

		void on_resize_before(const d3d11::resize_event& event)
		{
			if (is_accepting_work())
			{
				diagnostics::record_resize_before(event);
				engine_stereo_probe::record_resize();
				runtime::get().on_resize_before(event);
			}
		}

		void on_renderer_frame()
		{
			if (is_accepting_work())
			{
				diagnostics::record_renderer_frame();
			}
		}

		void on_gui_frame()
		{
			if (is_accepting_work())
			{
				diagnostics::record_gui_frame();
				if (vr_enable && vr_enable->current.enabled && vr_desktop_fov)
					desktop_mirror::draw(vr_desktop_fov->current.value);
			}
		}

		void monitor_present_progress()
		{
			const auto graphics = d3d11::get_graphics_status();
			if (!graphics.present_active || graphics.present_started == std::chrono::steady_clock::time_point{})
			{
				soft_freeze_reported.store(false, std::memory_order_release);
				return;
			}

			const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - graphics.present_started);
			if (age < std::chrono::seconds(5))
			{
				return;
			}

			const auto confirmed = d3d11::get_graphics_status();
			if (!confirmed.present_active || confirmed.generation != graphics.generation ||
				confirmed.present_thread_id != graphics.present_thread_id ||
				confirmed.last_present_frame_index != graphics.last_present_frame_index ||
				confirmed.present_started != graphics.present_started ||
				soft_freeze_reported.exchange(true, std::memory_order_acq_rel))
			{
				return;
			}

			const auto confirmed_age = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - confirmed.present_started);
			diagnostics::write_soft_freeze_report(confirmed, confirmed_age);
		}

		using terminal_status_signature = std::array<std::uint64_t, 320>;
		static_assert(128 + 5 * engine_stereo_execution::api_count <=
			terminal_status_signature{}.size());

		[[nodiscard]] terminal_status_signature get_terminal_status_signature(
			bool& any_terminal) noexcept
		{
			terminal_status_signature signature{};
			std::size_t field{};
			const auto backend_target_frame =
				engine_stereo_backend_target::get_frame_status();
			const bool backend_target_frame_terminal =
				backend_target_frame.state ==
					engine_stereo_backend_target::gate_state::complete ||
				backend_target_frame.state ==
					engine_stereo_backend_target::gate_state::failed;
			signature[field++] = backend_target_frame_terminal;
			if (backend_target_frame_terminal)
			{
				signature[field++] = static_cast<std::uint64_t>(backend_target_frame.state);
				signature[field++] = backend_target_frame.attempts;
				signature[field++] = backend_target_frame.completions;
				signature[field++] = backend_target_frame.failures;
				signature[field++] = backend_target_frame.select_calls;
				signature[field++] = backend_target_frame.invalid_observations;
				signature[field++] = backend_target_frame.application_mismatches;
				signature[field++] = backend_target_frame.entry_mutations;
				signature[field++] = backend_target_frame.route_overflows;
				signature[field++] = backend_target_frame.device_generation_mismatches;
				signature[field++] = backend_target_frame.view_copy_events;
				signature[field++] = backend_target_frame.start_publication_sequence;
				signature[field++] = backend_target_frame.start_record;
				signature[field++] = backend_target_frame.start_present_post_frame;
				signature[field++] = backend_target_frame.start_present_post_thread_id;
				signature[field++] = backend_target_frame.end_present_pre_frame;
				signature[field++] = backend_target_frame.device_generation;
				signature[field++] = backend_target_frame.boundary_thread_id;
				signature[field++] = backend_target_frame.active_writers;
				signature[field++] = backend_target_frame.maximum_active_writers;
				signature[field++] = backend_target_frame.unique_targets;
			}

			const auto output_merger = engine_stereo_output_merger::get_status();
			const bool output_merger_terminal =
				output_merger.state == engine_stereo_output_merger::gate_state::complete ||
				output_merger.state == engine_stereo_output_merger::gate_state::failed;
			signature[field++] = output_merger_terminal;
			if (output_merger_terminal)
			{
				signature[field++] = static_cast<std::uint64_t>(output_merger.state);
				signature[field++] = output_merger.hook_installed;
				signature[field++] = output_merger.extended_hooks_installed;
				signature[field++] = output_merger.hook_target;
				signature[field++] = output_merger.unordered_access_hook_target;
				signature[field++] = output_merger.clear_state_hook_target;
				signature[field++] = output_merger.expected_context;
				signature[field++] = output_merger.device_generation;
				signature[field++] = output_merger.hook_failures;
				signature[field++] = output_merger.attempts;
				signature[field++] = output_merger.completions;
				signature[field++] = output_merger.failures;
				signature[field++] = output_merger.bind_calls;
				signature[field++] = output_merger.nonnull_bind_calls;
				signature[field++] = output_merger.context_mismatches;
				signature[field++] = output_merger.thread_mismatches;
				signature[field++] = output_merger.query_failures;
				signature[field++] = output_merger.overflows;
				signature[field++] = output_merger.metadata_queries;
				signature[field++] = output_merger.unordered_access_calls;
				signature[field++] = output_merger.clear_state_calls;
				signature[field++] = output_merger.binding_invalidations;
				signature[field++] = output_merger.latest_publication_sequence;
				signature[field++] = output_merger.latest_record;
			}

			const auto draw_indexed = engine_stereo_draw_indexed::get_status();
			const bool draw_indexed_terminal =
				draw_indexed.state == engine_stereo_draw_indexed::gate_state::complete ||
				draw_indexed.state == engine_stereo_draw_indexed::gate_state::failed;
			signature[field++] = draw_indexed_terminal;
			if (draw_indexed_terminal)
			{
				signature[field++] = static_cast<std::uint64_t>(draw_indexed.state);
				signature[field++] = draw_indexed.hook_installed;
				signature[field++] = draw_indexed.hook_target;
				signature[field++] = draw_indexed.expected_context;
				signature[field++] = draw_indexed.device_generation;
				signature[field++] = draw_indexed.hook_failures;
				signature[field++] = draw_indexed.attempts;
				signature[field++] = draw_indexed.completions;
				signature[field++] = draw_indexed.failures;
				signature[field++] = draw_indexed.draw_calls;
				signature[field++] = draw_indexed.context_mismatches;
				signature[field++] = draw_indexed.thread_mismatches;
				signature[field++] = draw_indexed.invalid_arguments;
				signature[field++] = draw_indexed.overflows;
				signature[field++] = draw_indexed.latest_publication_sequence;
				signature[field++] = draw_indexed.latest_record;
			}

			const auto execution = engine_stereo_execution::get_status();
			const bool execution_terminal =
				execution.state == engine_stereo_execution::gate_state::complete ||
				execution.state == engine_stereo_execution::gate_state::failed;
			signature[field++] = execution_terminal;
			if (execution_terminal)
			{
				// report_ready is a lazy control-plane cache flag set by the evidence
				// read itself, not part of the immutable execution result. Persistence
				// success is proven separately by terminal_evidence_written.
				signature[field++] = static_cast<std::uint64_t>(execution.state);
				signature[field++] = static_cast<std::uint64_t>(execution.error);
				signature[field++] = execution.hooks_installed;
				signature[field++] = execution.draw_indexed_forwarding_external;
				signature[field++] = execution.targets_distinct;
				signature[field++] = execution.installation_permanently_failed;
				signature[field++] = execution.deferred_context_probe_attempted;
				signature[field++] = execution.deferred_context_probe_succeeded;
				signature[field++] = execution.deferred_execution_opaque;
				signature[field++] = execution.identity_truncated;
				signature[field++] = execution.installed_hook_count;
				for (const auto target : execution.hook_targets)
				{
					signature[field++] = target;
				}
				signature[field++] = execution.output_merger_target;
				signature[field++] = execution.output_merger_unordered_access_target;
				signature[field++] = execution.clear_state_target;
				for (const auto matches : execution.deferred_target_matches)
				{
					signature[field++] = matches;
				}
				signature[field++] = execution.deferred_output_merger_target_matches;
				signature[field++] = execution.expected_context;
				signature[field++] = execution.device_generation;
				signature[field++] = execution.hook_failures;
				signature[field++] = execution.attempts;
				signature[field++] = execution.completions;
				signature[field++] = execution.failures;
				signature[field++] = execution.event_reservations;
				signature[field++] = execution.recorded_events;
				signature[field++] = execution.classifier_events;
				signature[field++] = execution.frame_events;
				signature[field++] = execution.overflows;
				signature[field++] = execution.foreign_context_events;
				signature[field++] = execution.expected_context_frame_events;
				signature[field++] = execution.backend_scoped_events;
				signature[field++] = execution.backend_scoped_frame_events;
				signature[field++] = execution.backend_unscoped_frame_events;
				signature[field++] = execution.backend_thread_mismatches;
				signature[field++] = execution.distinct_backend_records;
				signature[field++] = execution.scene_owner_scoped_events;
				signature[field++] = execution.scene_owner_scoped_frame_events;
				signature[field++] = execution.scene_owner_unscoped_frame_events;
				signature[field++] = execution.scene_owner_thread_mismatches;
				signature[field++] = execution.distinct_scene_owner_records;
				signature[field++] = execution.call_stack_samples;
				signature[field++] = execution.call_stack_capture_failures;
				signature[field++] = execution.call_stack_key_overflows;
				signature[field++] = execution.admission_collisions;
				signature[field++] = execution.classifier_thread_mismatches;
				signature[field++] = execution.active_writers;
				signature[field++] = execution.maximum_active_writers;
				signature[field++] = execution.distinct_contexts;
				signature[field++] = execution.distinct_threads;
				signature[field++] = execution.draw_indexed_forwarded_calls;
				signature[field++] = execution.opaque_execute_command_lists;
				for (const auto count : execution.per_api)
				{
					signature[field++] = count;
				}
				for (const auto count : execution.classifier_per_api)
				{
					signature[field++] = count;
				}
				for (const auto count : execution.frame_per_api)
				{
					signature[field++] = count;
				}
				signature[field++] = execution.classifier_record;
				signature[field++] = execution.classifier_record_type;
				signature[field++] = execution.classifier_thread_id;
				signature[field++] = execution.classifier_begin_qpc;
				signature[field++] = execution.classifier_end_qpc;
				signature[field++] = execution.frame_start_present_post;
				signature[field++] = execution.frame_end_present_pre;
				signature[field++] = execution.frame_end_present_post;
				signature[field++] = static_cast<std::uint32_t>(
					execution.frame_present_result);
				signature[field++] = execution.frame_start_thread_id;
				signature[field++] = execution.frame_end_thread_id;
			}

			any_terminal = backend_target_frame_terminal && output_merger_terminal &&
				draw_indexed_terminal && execution_terminal;
			return signature;
		}

		void checkpoint_terminal_status_snapshot(terminal_status_signature& persisted,
			bool& has_persisted, const terminal_status_signature& before,
			const bool before_terminal, const terminal_status_signature& after,
			const bool after_terminal, const bool terminal_artifacts_written,
			const bool live_trace_written,
			const bool status_snapshot_written) noexcept
		{
			// A transition during status/evidence I/O is deliberately deferred to the
			// next watchdog round. Never authorize close from mixed pre-terminal,
			// terminal, or still-publishing snapshots.
			if (!before_terminal || !after_terminal || before != after || has_persisted)
			{
				return;
			}
			if (terminal_artifacts_written && live_trace_written &&
				status_snapshot_written)
			{
				persisted = before;
				has_persisted = true;
				console::info("[VR] terminal evidence, live trace, and vr_status snapshot "
					"were written; game and HMD may now be closed\n");
			}
		}

		void monitor_scene_hook_stall()
		{
			std::uint64_t reported_scene_sequence{};
			std::uint64_t reported_interop_sequence{};
			std::uint64_t reported_backend_sequence{};
			std::uint64_t last_live_trace_tick{};
			terminal_status_signature persisted_terminal_status{};
			bool has_persisted_terminal_status{};
			while (!stall_watchdog_stop.load(std::memory_order_acquire))
			{
				const auto scene = diagnostics::get_scene_hook_watchdog_status();
				if (scene.depth != 0 && scene.sequence != reported_scene_sequence)
				{
					const auto now = GetTickCount64();
					const auto age = now >= scene.entered_tick
						? std::chrono::milliseconds(now - scene.entered_tick)
						: std::chrono::milliseconds{};
					if (age >= std::chrono::seconds(5))
					{
						const auto confirmed = diagnostics::get_scene_hook_watchdog_status();
						if (confirmed.sequence == scene.sequence && confirmed.depth == scene.depth &&
							confirmed.entered_tick == scene.entered_tick)
						{
							reported_scene_sequence = scene.sequence;
							diagnostics::write_scene_hook_stall_report(
								d3d11::get_graphics_status(), confirmed, age);
						}
					}
				}

				const auto interop = diagnostics::get_gpu_interop_watchdog_status();
				if (interop.depth != 0 && interop.sequence != reported_interop_sequence)
				{
					const auto now = GetTickCount64();
					const auto age = now >= interop.entered_tick
						? std::chrono::milliseconds(now - interop.entered_tick)
						: std::chrono::milliseconds{};
					if (age >= std::chrono::seconds(1))
					{
						const auto confirmed = diagnostics::get_gpu_interop_watchdog_status();
						if (confirmed.sequence == interop.sequence && confirmed.depth == interop.depth &&
							confirmed.entered_tick == interop.entered_tick &&
							confirmed.source == interop.source &&
							confirmed.destination == interop.destination)
						{
							reported_interop_sequence = interop.sequence;
							diagnostics::write_gpu_interop_stall_report(
								d3d11::get_graphics_status(), confirmed, age);
						}
					}
				}

				const auto backend = engine_backend_probe::get_backend_watchdog_status();
				if (backend.depth != 0 && backend.sequence != reported_backend_sequence)
				{
					const auto now = GetTickCount64();
					const auto age = backend.last_progress_tick != 0 &&
						now >= backend.last_progress_tick
						? std::chrono::milliseconds(now - backend.last_progress_tick)
						: std::chrono::milliseconds{};
					if (age >= std::chrono::seconds(5))
					{
						const auto confirmed = engine_backend_probe::get_backend_watchdog_status();
						if (confirmed.sequence == backend.sequence && confirmed.depth == backend.depth &&
							confirmed.entered_tick == backend.entered_tick &&
							confirmed.last_progress_tick == backend.last_progress_tick &&
							confirmed.phase == backend.phase)
						{
							reported_backend_sequence = backend.sequence;
							diagnostics::write_backend_stall_report(
								d3d11::get_graphics_status(), confirmed, age);
						}
					}
				}

				const auto trace_tick = GetTickCount64();
				if (debug_options::enabled(debug_options::probe::snapshots) &&
					!has_persisted_terminal_status &&
					trace_tick - last_live_trace_tick >= 2000)
				{
					last_live_trace_tick = trace_tick;
					bool terminal_before{};
					const auto terminal_identity_before =
						get_terminal_status_signature(terminal_before);
					// Persist vr_status on every watchdog checkpoint, including menu,
					// initialization, and non-terminal census states. This is deliberately
					// silent; the manual command remains the only periodic console printer.
					// It runs before the larger evidence checkpoint so a slow or blocked
					// artifact write cannot prevent the status file from advancing.
					const bool enabled = vr_enable != nullptr &&
						vr_enable->current.enabled;
					const auto status_snapshot_written =
						diagnostics::write_status_snapshot(enabled);

					const auto graphics = d3d11::get_graphics_status();
					const auto live_trace_written = graphics.generation != 0 &&
						diagnostics::write_live_trace_snapshot(graphics);
					bool terminal_artifacts_written{};
					if (!has_persisted_terminal_status && graphics.generation != 0)
					{
						terminal_artifacts_written =
							diagnostics::renderer_evidence::checkpoint();
					}
					bool terminal_after{};
					const auto terminal_identity_after =
						get_terminal_status_signature(terminal_after);
					// A safe-close message requires current terminal evidence and the
					// status snapshot from this exact stable checkpoint. Once proven, the
					// process-lifetime bundle is immutable. Stop periodic full-status and
					// live-trace serialization at that point: format_status includes the
					// multi-megabyte terminal GPU census, and copying it every two seconds
					// is not an eligible production hot path. Manual vr_status, crash, and
					// stall paths still persist fresh evidence on demand.
					checkpoint_terminal_status_snapshot(persisted_terminal_status,
						has_persisted_terminal_status, terminal_identity_before,
						terminal_before, terminal_identity_after, terminal_after,
						terminal_artifacts_written, live_trace_written,
						status_snapshot_written);
				}
				Sleep(250);
			}
		}

		void unsubscribe_graphics_events() noexcept
		{
			d3d11::listener_token tokens[5]{};
			{
				const std::lock_guard lock(listener_mutex);
				tokens[0] = std::exchange(device_created_token, 0);
				tokens[1] = std::exchange(device_destroying_token, 0);
				tokens[2] = std::exchange(present_pre_token, 0);
				tokens[3] = std::exchange(present_post_token, 0);
				tokens[4] = std::exchange(resize_before_token, 0);
			}

			for (const auto token : tokens)
			{
				d3d11::unsubscribe(token);
			}
		}

		void finalize_component_shutdown_if_ready() noexcept
		{
			if (!shutdown_started.load(std::memory_order_acquire) ||
				!runtime::get().shutdown_complete() ||
				callbacks_finalized.exchange(true, std::memory_order_acq_rel))
			{
				return;
			}
			alive.store(false, std::memory_order_release);
			unsubscribe_graphics_events();
		}

		void subscribe_graphics_events()
		{
			const std::lock_guard lock(listener_mutex);
			device_created_token = d3d11::subscribe_device_created(on_device_created);
			device_destroying_token = d3d11::subscribe_device_destroying(on_device_destroying);
			present_pre_token = d3d11::subscribe_present_pre(on_present_pre);
			present_post_token = d3d11::subscribe_present_post(on_present_post);
			resize_before_token = d3d11::subscribe_resize_before(on_resize_before);
		}

		void initialize_watchdog_status_dependencies()
		{
			// Run every watchdog/status getter once on the component loader thread
			// after runtime construction and callback subscription, but before the
			// watchdog exists. The initial file also records this configured baseline.
			(void)d3d11::get_graphics_status();
			(void)diagnostics::get_scene_hook_watchdog_status();
			(void)diagnostics::get_gpu_interop_watchdog_status();
			(void)engine_backend_probe::get_backend_watchdog_status();
			(void)engine_stereo_execution::get_status();
			const bool enabled = vr_enable != nullptr && vr_enable->current.enabled;
			(void)diagnostics::write_status_snapshot(enabled);
		}

		void configure_engine_probe()
		{
			const auto identity = target_identity::get();
			const auto executable = utils::hook_validation::validate_executable_target(
				reinterpret_cast<const void*>(0x14076D7B0));
			const auto bridge_status = engine_stereo_bridge::get_status();
			engine_stereo_probe::configure_target({
				identity.original_sha256,
				identity.loaded_sha256,
				identity.loaded_pe_timestamp,
				identity.loaded_image_size,
				static_cast<bool>(executable),
				bridge_status.render_hook_installed,
			});
			const auto requested = vr_engine_probe != nullptr
				? static_cast<engine_stereo_probe::mode>(vr_engine_probe->current.integer)
				: engine_stereo_probe::mode::off;
			const auto selected = requested <= engine_stereo_probe::mode::observe
				? requested : engine_stereo_probe::mode::off;
			engine_stereo_probe::set_mode(selected);
			engine_stereo_probe::set_trace_enabled(vr_engine_probe_trace != nullptr &&
				vr_engine_probe_trace->current.enabled);
			engine_backend_probe::set_device_generation(
				d3d11::get_device_snapshot().generation);
			engine_view_probe::set_enabled(engine_stereo_probe::is_active());
			engine_backend_probe::set_enabled(engine_stereo_probe::is_active());
			const bool target_matched =
				engine_stereo_probe::get_status().gate == engine_stereo_probe::target_gate::matched;
			engine_stereo_bridge::configure_target(target_matched);
			head_pose_bridge::configure_target(target_matched);
		}

		void print_status()
		{
			if (!is_alive())
			{
				return;
			}

			diagnostics::print_status(vr_enable && vr_enable->current.enabled);
		}

		void reset_engine_probe()
		{
			// Production evidence is deliberately one coherent process-lifetime
			// transaction. Individual module reset APIs remain available to isolated
			// tests, but cannot safely reset the renderer's immutable artifact set.
			console::error("[VR] vr_engineProbe_reset is disabled: complete VR evidence "
				"is a process-lifetime one-shot; restart H2-MOD VR to sample again\n");
		}

		void dump_engine_probe()
		{
			std::array<engine_stereo_probe::trace_event, 32> events{};
			const auto count = engine_stereo_probe::read_recent_events(events.data(), events.size());
			console::info("[VR] engine probe trace events=%llu\n", static_cast<unsigned long long>(count));
			for (std::size_t index = 0; index < count; ++index)
			{
				const auto& event = events[index];
				console::info("[VR] trace seq=%llu phase=%s frame=%llu tid=%u depth=%u valid=%s hash=%llu\n",
					static_cast<unsigned long long>(event.sequence),
					engine_stereo_probe::to_string(event.phase),
					static_cast<unsigned long long>(event.renderer_frame_id), event.thread_id,
					event.recursion_depth, event.camera_valid ? "yes" : "no",
					static_cast<unsigned long long>(event.camera_hash));
			}
			const auto view_trace = engine_view_probe::format_recent(128);
			console::print_text(console::con_type_info, view_trace);
			const auto backend_trace = engine_backend_probe::format_recent(256);
			console::print_text(console::con_type_info, backend_trace);
		}


		void request_reinitialize()
		{
			if (!is_accepting_work())
			{
				return;
			}

			configure_engine_probe();
			const bool requested_enabled = vr_enable && vr_enable->current.enabled;
			const bool enabled = requested_enabled;

			engine_stereo_bridge::set_enabled(false);
			engine_stereo_bridge::invalidate_views();
			engine_stereo_owner_pass::request_temporal_history_reset();
			head_pose_bridge::set_enabled(false);
			head_pose_bridge::invalidate_pose();
			if (vr_world_scale != nullptr)
			{
				engine_stereo_bridge::set_world_scale(vr_world_scale->current.value);
				head_pose_bridge::set_world_scale(vr_world_scale->current.value);
			}
			engine_stereo_bridge::set_swap_eyes(vr_engine_swap_eyes != nullptr &&
				vr_engine_swap_eyes->current.enabled);

			auto& vr_runtime = runtime::get();
			vr_runtime.set_scene_mode(scene_mode::engine_stereo);
			vr_runtime.set_desired_enabled(enabled);
			vr_runtime.request_reinitialize();
			engine_stereo_bridge::invalidate_views();
			// H2's original scene call may continue for its desktop surface, but it is
			// never captured as a VR substitute when a native pair is unavailable.
			engine_stereo_bridge::set_enabled(enabled);
			const bool head_tracking_enabled = enabled && vr_head_tracking != nullptr &&
				vr_head_tracking->current.enabled;
			head_pose_bridge::set_enabled(head_tracking_enabled);
			if (head_tracking_enabled)
			{
				head_pose_bridge::request_recenter();
			}
			if (enabled)
			{
				const auto hooks_enabled = d3d11::enable_graphics_hooks();
				if (!hooks_enabled)
				{
					console::warn("[VR] graphics hooks are not active yet; waiting for a D3D11 device\n");
				}
			}
			console::info("[VR] reinitialize requested (desired=%s); pending until Present-pre\n",
				enabled ? "enabled" : "disabled");
		}

		void recenter_head_tracking()
		{
			if (!is_accepting_work()) return;
			engine_stereo_owner_pass::request_temporal_history_reset();
			head_pose_bridge::request_recenter();
			native_menu::recenter();
			console::info("[VR] head tracking recenter requested; the next valid runtime pose becomes neutral\n");
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			for (std::size_t i{}; i < debug_options::names.size(); ++i)
			{
				// The normal profile load applies saved values. Keep reset defaults
				// off even when this process loaded a probe; edits apply next launch.
				dvars::register_bool(debug_options::names[i], false,
					game::DVAR_FLAG_SAVED, "Optional VR diagnostic; requires game restart");
			}
			vr_enable = dvars::register_bool("vr_enable", true, game::DVAR_FLAG_SAVED,
				"Enable VR rendering after vr_reinit");
			static const char* probe_modes[]{"off", "observe", nullptr};
			vr_engine_probe = dvars::register_enum("vr_engineProbe", probe_modes, 1, game::DVAR_FLAG_NONE,
				"CPU-only H2 view transaction observation mode");
			vr_engine_probe_trace = dvars::register_bool("vr_engineProbeTrace", false, game::DVAR_FLAG_NONE,
				"Capture fixed-size engine stereo probe trace events");
			vr_head_tracking = dvars::register_bool("vr_headTracking", true, game::DVAR_FLAG_SAVED,
				"Apply HMD rotation and room-scale translation to the final game camera; apply with vr_reinit");
			vr_world_scale = dvars::register_float("vr_worldScale", 39.3700787f,
				0.01f, 10000.0f, game::DVAR_FLAG_SAVED,
				"Game units per VR meter for head tracking and stereo; apply with vr_reinit");
			vr_engine_swap_eyes = dvars::register_bool("vr_engineSwapEyes", false, game::DVAR_FLAG_SAVED,
				"Swap the engine stereo left and right eye assignment; apply with vr_reinit");
			vr_desktop_fov = dvars::register_float("vr_desktopFov", desktop_mirror::default_horizontal_fov,
				30.0f, 120.0f, game::DVAR_FLAG_SAVED,
				"Desktop horizontal FOV in degrees; limited to the right-eye image, applies immediately");

			callbacks_finalized.store(false, std::memory_order_release);
			shutdown_started.store(false, std::memory_order_release);
			accepting_work.store(true, std::memory_order_release);
			alive.store(true, std::memory_order_release);
			// runtime::get() owns a function-local singleton whose constructor may
			// create the runtime worker. Its first use must remain on this expected
			// main/loader thread, never the watchdog or a Present callback.
			(void)runtime::get();
			configure_engine_probe();
			subscribe_graphics_events();
			initialize_watchdog_status_dependencies();

			scheduler::loop(on_renderer_frame, scheduler::pipeline::renderer);
			scheduler::loop(monitor_present_progress, scheduler::pipeline::async, 250ms);
			gui::on_frame(on_gui_frame, true);
			command::add("vr_status", print_status);
			command::add("vr_reinit", request_reinitialize);
			command::add("vr_recenter", recenter_head_tracking);
			command::add("vr_engineProbe_reset", reset_engine_probe);
			command::add("vr_engineProbe_dump", dump_engine_probe);
			// Apply the saved VR configuration once the game main loop is alive. A
			// first-time h2v install defaults to enabled strict native stereo/head
			// tracking; users who save vr_enable=0 retain an automatic non-VR startup.
			scheduler::once(request_reinitialize, scheduler::pipeline::main);

			stall_watchdog_stop.store(false, std::memory_order_release);
			stall_watchdog = std::thread(monitor_scene_hook_stall);
		}

		void pre_destroy() override
		{
			accepting_work.store(false, std::memory_order_release);
			shutdown_started.store(true, std::memory_order_release);
			stall_watchdog_stop.store(true, std::memory_order_release);
			if (stall_watchdog.joinable()) stall_watchdog.join();
			engine_stereo_bridge::set_enabled(false);
			engine_stereo_bridge::invalidate_views();
			head_pose_bridge::set_enabled(false);
			head_pose_bridge::invalidate_pose();
			engine_stereo_probe::stop();
			engine_view_probe::set_enabled(false);
			engine_backend_probe::set_enabled(false);
			runtime::get().shutdown();
			finalize_component_shutdown_if_ready();
		}
	};
}

REGISTER_COMPONENT(vr::component)
