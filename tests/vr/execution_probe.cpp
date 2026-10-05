#include "std_include.hpp"

#include "component/vr/engine_stereo_execution.hpp"
#include "component/vr/engine_stereo_draw_indexed.hpp"
#include "component/vr/engine_stereo_output_merger.hpp"
#include "component/vr/engine_stereo_renderer.hpp"
#include "component/vr/native_conversion_command_list.hpp"
#include "utils/hook_validation.hpp"

#include <cstdlib>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

namespace
{
	std::atomic_uint64_t opaque_state_observations{};
	std::atomic_uintptr_t opaque_state_context{};
	std::atomic_uintptr_t opaque_state_command_list{};
	std::atomic_bool opaque_state_restore{};
	std::atomic_uintptr_t opaque_state_caller{};
	std::atomic_uint64_t gpu_invocation_observations{};
	std::atomic_uint64_t ssr_invocation_observations{};

	void observe_opaque_state(ID3D11DeviceContext* const context,
		ID3D11CommandList* const commands, const bool restore_context_state,
		const std::uintptr_t caller) noexcept
	{
		opaque_state_context.store(reinterpret_cast<std::uintptr_t>(context),
			std::memory_order_release);
		opaque_state_command_list.store(reinterpret_cast<std::uintptr_t>(commands),
			std::memory_order_release);
		opaque_state_restore.store(restore_context_state, std::memory_order_release);
		opaque_state_caller.store(caller, std::memory_order_release);
		opaque_state_observations.fetch_add(1, std::memory_order_relaxed);
	}

	void observe_gpu_invocation(ID3D11DeviceContext*,
		vr::engine_stereo_execution::api, std::uintptr_t, std::uint32_t,
		std::uintptr_t, std::uint64_t, std::uint8_t,
		const std::array<std::uint64_t, 6>&) noexcept
	{
		gpu_invocation_observations.fetch_add(1, std::memory_order_relaxed);
	}

	void observe_ssr_invocation(ID3D11DeviceContext*,
		vr::engine_stereo_execution::api, std::uintptr_t, std::uint32_t,
		std::uintptr_t, std::uint64_t, std::uint8_t,
		const std::array<std::uint64_t, 6>&) noexcept
	{
		ssr_invocation_observations.fetch_add(1, std::memory_order_relaxed);
	}

	[[noreturn]] void fail(const char* const message)
	{
		std::cerr << "vr-d3d11-execution-probe: FAIL; " << message << '\n';
		std::exit(1);
	}

	[[nodiscard]] bool same_output_status(
		const vr::engine_stereo_output_merger::status& left,
		const vr::engine_stereo_output_merger::status& right) noexcept
	{
		return left.state == right.state &&
			left.hook_installed == right.hook_installed &&
			left.extended_hooks_installed == right.extended_hooks_installed &&
			left.hook_target == right.hook_target &&
			left.unordered_access_hook_target == right.unordered_access_hook_target &&
			left.clear_state_hook_target == right.clear_state_hook_target &&
			left.expected_context == right.expected_context &&
			left.device_generation == right.device_generation &&
			left.hook_failures == right.hook_failures && left.attempts == right.attempts &&
			left.completions == right.completions && left.failures == right.failures &&
			left.bind_calls == right.bind_calls &&
			left.nonnull_bind_calls == right.nonnull_bind_calls &&
			left.context_mismatches == right.context_mismatches &&
			left.thread_mismatches == right.thread_mismatches &&
			left.query_failures == right.query_failures &&
			left.overflows == right.overflows &&
			left.metadata_queries == right.metadata_queries &&
			left.unordered_access_calls == right.unordered_access_calls &&
			left.clear_state_calls == right.clear_state_calls &&
			left.binding_invalidations == right.binding_invalidations &&
			left.latest_publication_sequence == right.latest_publication_sequence &&
			left.latest_record == right.latest_record;
	}

	[[nodiscard]] bool same_draw_status(
		const vr::engine_stereo_draw_indexed::status& left,
		const vr::engine_stereo_draw_indexed::status& right) noexcept
	{
		return left.state == right.state && left.hook_installed == right.hook_installed &&
			left.hook_target == right.hook_target &&
			left.expected_context == right.expected_context &&
			left.device_generation == right.device_generation &&
			left.hook_failures == right.hook_failures && left.attempts == right.attempts &&
			left.completions == right.completions && left.failures == right.failures &&
			left.draw_calls == right.draw_calls &&
			left.context_mismatches == right.context_mismatches &&
			left.thread_mismatches == right.thread_mismatches &&
			left.invalid_arguments == right.invalid_arguments &&
			left.overflows == right.overflows &&
			left.latest_publication_sequence == right.latest_publication_sequence &&
			left.latest_record == right.latest_record;
	}

	[[nodiscard]] bool same_execution_status(
		const vr::engine_stereo_execution::status& left,
		const vr::engine_stereo_execution::status& right) noexcept
	{
		return left.state == right.state && left.error == right.error &&
			left.hooks_installed == right.hooks_installed &&
			left.draw_indexed_forwarding_external ==
				right.draw_indexed_forwarding_external &&
			left.report_ready == right.report_ready &&
			left.targets_distinct == right.targets_distinct &&
			left.installation_permanently_failed ==
				right.installation_permanently_failed &&
			left.deferred_context_probe_attempted ==
				right.deferred_context_probe_attempted &&
			left.deferred_context_probe_succeeded ==
				right.deferred_context_probe_succeeded &&
			left.deferred_execution_opaque == right.deferred_execution_opaque &&
			left.identity_truncated == right.identity_truncated &&
			left.installed_hook_count == right.installed_hook_count &&
			left.hook_targets == right.hook_targets &&
			left.output_merger_target == right.output_merger_target &&
			left.output_merger_unordered_access_target ==
				right.output_merger_unordered_access_target &&
			left.clear_state_target == right.clear_state_target &&
			left.deferred_target_matches == right.deferred_target_matches &&
			left.deferred_output_merger_target_matches ==
				right.deferred_output_merger_target_matches &&
			left.expected_context == right.expected_context &&
			left.device_generation == right.device_generation &&
			left.hook_failures == right.hook_failures && left.attempts == right.attempts &&
			left.completions == right.completions && left.failures == right.failures &&
			left.event_reservations == right.event_reservations &&
			left.recorded_events == right.recorded_events &&
			left.classifier_events == right.classifier_events &&
			left.frame_events == right.frame_events && left.overflows == right.overflows &&
			left.foreign_context_events == right.foreign_context_events &&
			left.expected_context_frame_events == right.expected_context_frame_events &&
			left.backend_scoped_events == right.backend_scoped_events &&
			left.backend_scoped_frame_events == right.backend_scoped_frame_events &&
			left.backend_unscoped_frame_events == right.backend_unscoped_frame_events &&
			left.backend_thread_mismatches == right.backend_thread_mismatches &&
			left.distinct_backend_records == right.distinct_backend_records &&
			left.scene_owner_scoped_events == right.scene_owner_scoped_events &&
			left.scene_owner_scoped_frame_events ==
				right.scene_owner_scoped_frame_events &&
			left.scene_owner_unscoped_frame_events ==
				right.scene_owner_unscoped_frame_events &&
			left.scene_owner_thread_mismatches ==
				right.scene_owner_thread_mismatches &&
			left.distinct_scene_owner_records ==
				right.distinct_scene_owner_records &&
			left.call_stack_samples == right.call_stack_samples &&
			left.call_stack_capture_failures ==
				right.call_stack_capture_failures &&
			left.call_stack_key_overflows == right.call_stack_key_overflows &&
			left.admission_collisions == right.admission_collisions &&
			left.classifier_thread_mismatches == right.classifier_thread_mismatches &&
			left.active_writers == right.active_writers &&
			left.maximum_active_writers == right.maximum_active_writers &&
			left.distinct_contexts == right.distinct_contexts &&
			left.distinct_threads == right.distinct_threads &&
			left.draw_indexed_forwarded_calls == right.draw_indexed_forwarded_calls &&
			left.opaque_execute_command_lists == right.opaque_execute_command_lists &&
			left.known_conversion_recordings == right.known_conversion_recordings &&
			left.known_conversion_executions == right.known_conversion_executions &&
			left.known_conversion_replays == right.known_conversion_replays &&
			left.per_api == right.per_api &&
			left.classifier_per_api == right.classifier_per_api &&
			left.frame_per_api == right.frame_per_api &&
			left.classifier_record == right.classifier_record &&
			left.classifier_record_type == right.classifier_record_type &&
			left.classifier_thread_id == right.classifier_thread_id &&
			left.classifier_begin_qpc == right.classifier_begin_qpc &&
			left.classifier_end_qpc == right.classifier_end_qpc &&
			left.frame_start_present_post == right.frame_start_present_post &&
			left.frame_end_present_pre == right.frame_end_present_pre &&
			left.frame_end_present_post == right.frame_end_present_post &&
			left.frame_present_result == right.frame_present_result &&
			left.frame_start_thread_id == right.frame_start_thread_id &&
			left.frame_end_thread_id == right.frame_end_thread_id;
	}

	void emit_all(ID3D11DeviceContext* const context, ID3D11Buffer* const arguments,
		ID3D11CommandList* const command_list)
	{
		context->DrawIndexed(3, 2, -1);
		context->Draw(4, 5);
		context->DrawIndexedInstanced(6, 7, 8, -9, 10);
		context->DrawInstanced(11, 12, 13, 14);
		context->DrawAuto();
		context->DrawIndexedInstancedIndirect(arguments, 0);
		context->DrawInstancedIndirect(arguments, 0);
		context->Dispatch(2, 3, 4);
		context->DispatchIndirect(arguments, 0);
		context->ExecuteCommandList(command_list, FALSE);
	}

	void wait_for_admission_pause()
	{
		const auto deadline = GetTickCount64() + 5000;
		while (!vr::engine_stereo_execution::test_admission_pause_entered())
		{
			if (GetTickCount64() >= deadline)
			{
				vr::engine_stereo_execution::test_release_admission_pause();
				fail("timed out waiting for the deterministic admission window");
			}
			SwitchToThread();
		}
	}
}

int main()
{
	const vr::engine_stereo_renderer::execution_evidence_acceptance
		failed_companion_evidence{true, true, false, true, true, true};
	const vr::engine_stereo_renderer::execution_evidence_acceptance
		incomplete_observation_evidence{false, true, true, true, true, true};
	const vr::engine_stereo_renderer::execution_evidence_acceptance
		complete_evidence{true, true, true, true, true, true};
	if (vr::engine_stereo_renderer::accepts_complete_execution_evidence(
			failed_companion_evidence) ||
		vr::engine_stereo_renderer::accepts_complete_execution_evidence(
			incomplete_observation_evidence) ||
		!vr::engine_stereo_renderer::accepts_complete_execution_evidence(
			complete_evidence))
	{
		fail("aggregate acceptance ignored a failed companion observer");
	}

	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL feature_level{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr,
		0, D3D11_SDK_VERSION, &device, &feature_level, &context)))
	{
		fail("D3D11CreateDevice(WARP) failed");
	}

	D3D11_TEXTURE2D_DESC color_description{};
	color_description.Width = 64;
	color_description.Height = 32;
	color_description.MipLevels = 1;
	color_description.ArraySize = 1;
	color_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	color_description.SampleDesc.Count = 1;
	color_description.Usage = D3D11_USAGE_DEFAULT;
	color_description.BindFlags = D3D11_BIND_RENDER_TARGET;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> color;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target;
	if (FAILED(device->CreateTexture2D(&color_description, nullptr, &color)) ||
		FAILED(device->CreateRenderTargetView(color.Get(), nullptr, &render_target)))
	{
		fail("color target creation failed");
	}

	D3D11_TEXTURE2D_DESC depth_description = color_description;
	depth_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth_description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil;
	if (FAILED(device->CreateTexture2D(&depth_description, nullptr, &depth)) ||
		FAILED(device->CreateDepthStencilView(depth.Get(), nullptr, &depth_stencil)))
	{
		fail("depth target creation failed");
	}

	const std::array<UINT, 5> indirect_data{3, 1, 0, 0, 0};
	D3D11_BUFFER_DESC indirect_description{};
	indirect_description.ByteWidth = sizeof(indirect_data);
	indirect_description.Usage = D3D11_USAGE_DEFAULT;
	indirect_description.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
	D3D11_SUBRESOURCE_DATA indirect_initial{};
	indirect_initial.pSysMem = indirect_data.data();
	Microsoft::WRL::ComPtr<ID3D11Buffer> indirect_arguments;
	if (FAILED(device->CreateBuffer(&indirect_description, &indirect_initial,
		&indirect_arguments)))
	{
		fail("indirect argument buffer creation failed");
	}

	Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_context;
	Microsoft::WRL::ComPtr<ID3D11CommandList> empty_command_list;
	if (FAILED(device->CreateDeferredContext(0, &deferred_context)) ||
		FAILED(deferred_context->FinishCommandList(FALSE, &empty_command_list)))
	{
		fail("empty deferred command list creation failed");
	}
	if (!vr::native_conversion_command_list::mark(empty_command_list.Get()) ||
		!vr::native_conversion_command_list::is_marked(empty_command_list.Get()))
	{
		fail("native conversion command-list marker round trip failed");
	}
	// Production never creates a deferred context for observation. The isolated
	// WARP probe may do so and proves whether the public API slots share stable
	// implementation identities without involving the game or a hardware driver.
	constexpr std::array<std::size_t, 13> observed_slots{
		12, 13, 20, 21, 33, 34, 38, 39, 40, 41, 42, 58, 110};
	auto* const immediate_vtable = *reinterpret_cast<void***>(context.Get());
	auto* const deferred_vtable = *reinterpret_cast<void***>(deferred_context.Get());
	if (immediate_vtable == nullptr || deferred_vtable == nullptr)
	{
		fail("D3D11 context vtable is unavailable");
	}
	std::uint32_t deferred_target_match_mask{};
	for (std::size_t index{}; index < observed_slots.size(); ++index)
	{
		const auto slot = observed_slots[index];
		if (immediate_vtable[slot] == nullptr || deferred_vtable[slot] == nullptr ||
			!utils::hook_validation::validate_executable_target(immediate_vtable[slot]) ||
			!utils::hook_validation::validate_executable_target(deferred_vtable[slot]))
		{
			fail("WARP immediate/deferred observer target is not executable");
		}
		if (immediate_vtable[slot] == deferred_vtable[slot])
		{
			deferred_target_match_mask |= 1u << index;
		}
	}

	if (!vr::engine_stereo_output_merger::install(context.Get(), 1) ||
		!vr::engine_stereo_draw_indexed::install(context.Get(), 1) ||
		!vr::engine_stereo_execution::install(context.Get(), 1))
	{
		fail("D3D11 observer installation failed");
	}
	vr::engine_stereo_execution::set_opaque_state_observer(observe_opaque_state);
	vr::engine_stereo_execution::set_invocation_observer(
		vr::engine_stereo_execution::invocation_observer_channel::gpu_census,
		observe_gpu_invocation);
	vr::engine_stereo_execution::set_invocation_observer(
		vr::engine_stereo_execution::invocation_observer_channel::ssr_consumer,
		observe_ssr_invocation);
	context->Draw(1, 0);
	if (gpu_invocation_observations.load(std::memory_order_acquire) != 1 ||
		ssr_invocation_observations.load(std::memory_order_acquire) != 1)
	{
		fail("invocation observer channels did not both receive the natural draw");
	}
	vr::engine_stereo_execution::set_invocation_observer(
		vr::engine_stereo_execution::invocation_observer_channel::gpu_census,
		nullptr);
	context->Draw(1, 0);
	if (gpu_invocation_observations.load(std::memory_order_acquire) != 1 ||
		ssr_invocation_observations.load(std::memory_order_acquire) != 2)
	{
		fail("detaching one invocation channel displaced another channel");
	}
	const auto installed = vr::engine_stereo_execution::get_status();
	if (!installed.draw_indexed_forwarding_external ||
		installed.installed_hook_count != 9 ||
		!installed.targets_distinct || installed.installation_permanently_failed ||
		installed.output_merger_target == 0 ||
		installed.output_merger_unordered_access_target == 0 ||
		installed.clear_state_target == 0 ||
		installed.hook_targets[static_cast<std::size_t>(
			vr::engine_stereo_execution::api::draw_indexed)] == 0 ||
		installed.draw_indexed_forwarded_calls != 0)
	{
		fail("slot 12 external DrawIndexed forwarding contract is not explicit");
	}
	auto* const color_view = render_target.Get();
	context->OMSetRenderTargets(1, &color_view, depth_stencil.Get());
	const auto initial_binding =
		vr::engine_stereo_output_merger::get_current_binding(context.Get());
	context->OMSetRenderTargetsAndUnorderedAccessViews(
		D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 0, 0,
		nullptr, nullptr);
	const auto retained_binding =
		vr::engine_stereo_output_merger::get_current_binding(context.Get());
	if (!initial_binding.valid || !retained_binding.valid ||
		retained_binding.sequence != initial_binding.sequence ||
		retained_binding.render_target_0 != initial_binding.render_target_0 ||
		retained_binding.depth_stencil != initial_binding.depth_stencil)
	{
		fail("slot 34 KEEP semantics corrupted the RTV/DSV snapshot");
	}
	context->ClearState();
	if (vr::engine_stereo_output_merger::get_current_binding(context.Get()).valid)
	{
		fail("ClearState did not invalidate the output binding snapshot");
	}
	context->OMSetRenderTargets(1, &color_view, depth_stencil.Get());
	const auto output_merger_status =
		vr::engine_stereo_output_merger::get_status();
	if (!output_merger_status.extended_hooks_installed ||
		output_merger_status.unordered_access_calls == 0 ||
		output_merger_status.clear_state_calls == 0 ||
		output_merger_status.binding_invalidations == 0)
	{
		fail("extended output binding observers did not execute");
	}

	// Close the two externally owned observers on the same device identity that
	// the execution census will use. This makes the replacement-device section
	// exercise the production callback order across all three terminal modules.
	std::array<std::uint8_t, 0x170> observer_record{};
	vr::engine_stereo_binding::backend_claim observer_claim{};
	observer_claim.publication_sequence = 1;
	observer_claim.record = reinterpret_cast<std::uintptr_t>(observer_record.data());
	auto output_transaction =
		std::make_unique<vr::engine_stereo_output_merger::transaction>();
	if (!vr::engine_stereo_output_merger::begin(*output_transaction, observer_claim,
		observer_claim.record))
	{
		fail("output-merger terminal integration transaction did not begin");
	}
	vr::engine_stereo_output_merger::note_view_copy(*output_transaction, 1, 1, 0);
	vr::engine_stereo_output_merger::note_target(*output_transaction, 13);
	context->OMSetRenderTargets(1, &color_view, depth_stencil.Get());
	vr::engine_stereo_output_merger::enter_dispatch(*output_transaction);
	vr::engine_stereo_output_merger::note_target(*output_transaction, 1);
	context->OMSetRenderTargets(1, &color_view, depth_stencil.Get());
	vr::engine_stereo_output_merger::leave_dispatch(*output_transaction);
	vr::engine_stereo_output_merger::end(*output_transaction, true);

	const std::array<std::uint8_t, 7> command_bytes{4, 0, 16, 0, 0, 0, 0};
	auto draw_transaction =
		std::make_unique<vr::engine_stereo_draw_indexed::transaction>();
	if (!vr::engine_stereo_draw_indexed::begin(*draw_transaction, observer_claim,
		observer_claim.record))
	{
		fail("DrawIndexed terminal integration transaction did not begin");
	}
	vr::engine_stereo_draw_indexed::enter_dispatch(*draw_transaction,
		command_bytes.data());
	context->DrawIndexed(3, 2, -1);
	vr::engine_stereo_draw_indexed::leave_dispatch(*draw_transaction,
		command_bytes.data());
	vr::engine_stereo_draw_indexed::end(*draw_transaction, true);
	const auto terminal_output_merger =
		vr::engine_stereo_output_merger::get_status();
	const auto terminal_draw_indexed = vr::engine_stereo_draw_indexed::get_status();
	auto terminal_output_report =
		std::make_unique<vr::engine_stereo_output_merger::report>();
	auto terminal_draw_report =
		std::make_unique<vr::engine_stereo_draw_indexed::report>();
	if (terminal_output_merger.state !=
			vr::engine_stereo_output_merger::gate_state::complete ||
		terminal_draw_indexed.state !=
			vr::engine_stereo_draw_indexed::gate_state::complete ||
		terminal_output_merger.expected_context !=
			terminal_draw_indexed.expected_context ||
		terminal_output_merger.device_generation !=
			terminal_draw_indexed.device_generation ||
		!vr::engine_stereo_output_merger::read_report(*terminal_output_report) ||
		!vr::engine_stereo_draw_indexed::read_report(*terminal_draw_report))
	{
		fail("external terminal observer identity is not coherent");
	}

	const auto owner_thread = static_cast<std::uint32_t>(GetCurrentThreadId());
	const vr::engine_stereo_execution::backend_record_context classifier_backend{
		11, 21, 31, 0x12340000, 0x12341000, 0x12342000, 1, 4, 13,
		owner_thread, true,
	};
	const vr::engine_stereo_execution::backend_record_context frame_backend{
		12, 22, 32, 0x22340000, 0x22341000, 0x22342000, 2, 7, 14,
		owner_thread, true,
	};
	const vr::engine_stereo_execution::scene_owner_context classifier_owner{
		41, 0x32340000, 0x32341000, 0x32342000, 3, 4, {13, 14, 15}, 0,
		owner_thread, true,
	};
	const vr::engine_stereo_execution::scene_owner_context frame_owner{
		42, 0x42340000, 0x42341000, 0x42342000, 4, 4, {23, 24, 25}, 1,
		owner_thread, true,
	};

	vr::engine_stereo_execution::set_backend_record_context(classifier_backend);
	vr::engine_stereo_execution::set_scene_owner_context(classifier_owner);
	vr::engine_stereo_execution::classifier_scope classifier{};
	if (!vr::engine_stereo_execution::begin_classifier(classifier, 0x12340000, 4))
	{
		fail("classifier scope did not begin");
	}
	bool cross_thread_reset{};
	std::thread reset_attempt([&]
	{
		cross_thread_reset = vr::engine_stereo_execution::reset();
	});
	reset_attempt.join();
	if (cross_thread_reset)
	{
		fail("cross-thread reset entered an active classifier scope");
	}
	emit_all(context.Get(), indirect_arguments.Get(), empty_command_list.Get());
	if (opaque_state_observations.load(std::memory_order_acquire) == 0 ||
		opaque_state_context.load(std::memory_order_acquire) !=
			reinterpret_cast<std::uintptr_t>(context.Get()) ||
		opaque_state_command_list.load(std::memory_order_acquire) !=
			reinterpret_cast<std::uintptr_t>(empty_command_list.Get()) ||
		opaque_state_restore.load(std::memory_order_acquire) ||
		opaque_state_caller.load(std::memory_order_acquire) == 0)
	{
		fail("independent ExecuteCommandList opaque-state observer did not execute");
	}
	if (vr::engine_stereo_output_merger::get_current_binding(context.Get()).valid)
	{
		fail("ExecuteCommandList(FALSE) left a trusted output binding snapshot");
	}
	if (!vr::engine_stereo_execution::end_classifier(classifier))
	{
		fail("classifier scope did not end");
	}
	vr::engine_stereo_execution::clear_backend_record_context();
	vr::engine_stereo_execution::clear_scene_owner_context();
	auto state = vr::engine_stereo_execution::get_status();
	if (state.state != vr::engine_stereo_execution::gate_state::frame_pending ||
		vr::engine_stereo_execution::bootstrap_complete() ||
		state.classifier_events != vr::engine_stereo_execution::api_count)
	{
		fail("classifier evidence did not transition to frame pending");
	}

	context->OMSetRenderTargets(1, &color_view, depth_stencil.Get());
	vr::engine_stereo_execution::on_present_post(100, 1, GetCurrentThreadId(), S_OK);
	state = vr::engine_stereo_execution::get_status();
	if (state.state != vr::engine_stereo_execution::gate_state::frame_active)
	{
		fail("Present-post did not begin the frame scope");
	}
	vr::engine_stereo_execution::set_backend_record_context(frame_backend);
	vr::engine_stereo_execution::set_scene_owner_context(frame_owner);
	// Loading/menu canvas composition can overlap the first native frame. Its
	// private recording and tagged state-restoring execution must not permanently
	// block bootstrap; the existing FALSE executions below must stay opaque.
	if(vr::native_conversion_command_list::is_recording_context(deferred_context.Get()) ||
		vr::native_conversion_command_list::mark_recording_context(context.Get()) ||
		!vr::native_conversion_command_list::mark_recording_context(deferred_context.Get()) ||
		!vr::native_conversion_command_list::is_recording_context(deferred_context.Get()))
		fail("conversion recording identity must belong to a private deferred context");
	const auto observer_before=ssr_invocation_observations.load();
	deferred_context->Draw(0,0);
	Microsoft::WRL::ComPtr<ID3D11CommandList> canvas_commands;
	if(FAILED(deferred_context->FinishCommandList(FALSE,&canvas_commands)) ||
		vr::native_conversion_command_list::is_marked(canvas_commands.Get()) ||
		!vr::native_conversion_command_list::mark(canvas_commands.Get()))
		fail("recording context identity must not implicitly trust its finished list");
	context->ExecuteCommandList(canvas_commands.Get(),TRUE);
	const auto canvas=vr::engine_stereo_execution::get_status();
	const unsigned expected_recordings=(deferred_target_match_mask&(1u<<1))?1u:0u;
	if(canvas.foreign_context_events || canvas.opaque_execute_command_lists!=1 ||
		canvas.known_conversion_recordings!=expected_recordings || canvas.known_conversion_executions!=1 ||
		canvas.frame_events || canvas.known_conversion_replays>1 ||
		ssr_invocation_observations.load()!=observer_before+1+expected_recordings+canvas.known_conversion_replays)
	{
		std::cerr << "canvas foreign/opaque/recording/execute/frame/observer/expected=" << canvas.foreign_context_events << '/'
			<< canvas.opaque_execute_command_lists << '/' << canvas.known_conversion_recordings << '/' << canvas.known_conversion_executions
			<< '/' << canvas.frame_events << '/' << ssr_invocation_observations.load()-observer_before << '/' << expected_recordings << '\n';
		fail("known menu conversion polluted native bootstrap or hid calls from independent observers");
	}
	emit_all(context.Get(), indirect_arguments.Get(), empty_command_list.Get());
	vr::engine_stereo_execution::clear_backend_record_context();
	vr::engine_stereo_execution::clear_scene_owner_context();
	vr::engine_stereo_execution::on_present_pre(101, 1, GetCurrentThreadId());

	state = vr::engine_stereo_execution::get_status();
	auto first = std::make_unique<vr::engine_stereo_execution::report>();
	if (state.state !=
			vr::engine_stereo_execution::gate_state::awaiting_end_present_post ||
		state.report_ready || vr::engine_stereo_execution::read_report(*first))
	{
		fail("Present-pre terminated before the matching Present-post");
	}
	vr::engine_stereo_execution::on_present_post(101, 1, GetCurrentThreadId(), S_OK);
	state = vr::engine_stereo_execution::get_status();
	if (state.state != vr::engine_stereo_execution::gate_state::complete ||
		!vr::engine_stereo_execution::bootstrap_complete() ||
		state.report_ready || !vr::engine_stereo_execution::read_report(*first))
	{
		fail("terminal state or deferred report publication is incorrect");
	}
	state = vr::engine_stereo_execution::get_status();
	if(state.known_conversion_recordings!=expected_recordings || state.known_conversion_executions!=1 ||
		first->known_conversion_recordings!=expected_recordings || first->known_conversion_executions!=1 ||
		state.known_conversion_replays!=canvas.known_conversion_replays || first->known_conversion_replays!=canvas.known_conversion_replays)
		fail("known conversions were not retained separately in the terminal report");
	if (state.state != vr::engine_stereo_execution::gate_state::complete ||
		state.error != vr::engine_stereo_execution::failure::none ||
		!state.hooks_installed || !state.report_ready ||
		state.installed_hook_count != 9 ||
		state.hook_failures != 0 || state.attempts != 1 || state.completions != 1 ||
		state.failures != 0 || state.recorded_events != 2 *
			vr::engine_stereo_execution::api_count ||
		state.classifier_events != vr::engine_stereo_execution::api_count ||
		state.frame_events != vr::engine_stereo_execution::api_count ||
		state.overflows != 0 || state.foreign_context_events != 0 ||
		state.expected_context_frame_events != vr::engine_stereo_execution::api_count ||
		state.backend_scoped_events != 2 * vr::engine_stereo_execution::api_count ||
		state.backend_scoped_frame_events != vr::engine_stereo_execution::api_count ||
		state.backend_unscoped_frame_events != 0 ||
		state.backend_thread_mismatches != 0 || state.distinct_backend_records != 2 ||
		state.scene_owner_scoped_events !=
			2 * vr::engine_stereo_execution::api_count ||
		state.scene_owner_scoped_frame_events !=
			vr::engine_stereo_execution::api_count ||
		state.scene_owner_unscoped_frame_events != 0 ||
		state.scene_owner_thread_mismatches != 0 ||
		state.distinct_scene_owner_records != 2 ||
		state.call_stack_samples != 2 * vr::engine_stereo_execution::api_count ||
		state.call_stack_capture_failures != 0 || state.call_stack_key_overflows != 0 ||
		state.admission_collisions != 0 ||
		state.classifier_thread_mismatches != 0 || state.active_writers != 0 ||
		state.distinct_contexts != 1 || state.distinct_threads != 1 ||
		state.draw_indexed_forwarded_calls != 2 ||
		state.opaque_execute_command_lists != 2 || !state.deferred_execution_opaque ||
		state.identity_truncated)
	{
		fail("execution census did not complete cleanly");
	}

	if (first->classifier_record != 0x12340000 ||
		first->classifier_record_type != 4 || first->classifier_begin_qpc == 0 ||
		first->classifier_end_qpc < first->classifier_begin_qpc ||
		first->frame_start_present_post != 100 || first->frame_end_present_pre != 101 ||
		first->frame_end_present_post != 101 || FAILED(first->frame_present_result) ||
		first->event_count != 2 * vr::engine_stereo_execution::api_count ||
		first->classifier_event_count != vr::engine_stereo_execution::api_count ||
		first->frame_event_count != vr::engine_stereo_execution::api_count ||
		first->overflow_count != 0 || first->distinct_contexts != 1 ||
		first->expected_context_frame_events != vr::engine_stereo_execution::api_count ||
		first->backend_scoped_events != 2 * vr::engine_stereo_execution::api_count ||
		first->backend_scoped_frame_events != vr::engine_stereo_execution::api_count ||
		first->backend_unscoped_frame_events != 0 ||
		first->backend_thread_mismatches != 0 || first->distinct_backend_records != 2 ||
		first->scene_owner_scoped_events !=
			2 * vr::engine_stereo_execution::api_count ||
		first->scene_owner_scoped_frame_events !=
			vr::engine_stereo_execution::api_count ||
		first->scene_owner_unscoped_frame_events != 0 ||
		first->scene_owner_thread_mismatches != 0 ||
		first->distinct_scene_owner_records != 2 ||
		first->call_stack_samples != 2 * vr::engine_stereo_execution::api_count ||
		first->call_stack_capture_failures != 0 || first->call_stack_key_overflows != 0 ||
		first->admission_collisions != 0 ||
		first->distinct_threads != 1 || first->opaque_execute_command_lists != 2 ||
		!first->deferred_execution_opaque || first->identity_truncated)
	{
		fail("published scope metadata is incomplete");
	}

	for (std::size_t index{}; index < vr::engine_stereo_execution::api_count; ++index)
	{
		if (state.hook_targets[index] == 0 || state.per_api[index] != 2 ||
			state.classifier_per_api[index] != 1 || state.frame_per_api[index] != 1 ||
			first->per_api[index] != 2 || first->classifier_per_api[index] != 1 ||
			first->frame_per_api[index] != 1)
		{
			fail("per-API census is incomplete");
		}
	}
	for (std::uint32_t index{}; index < first->event_count; ++index)
	{
		const auto& event = first->events[index];
		const auto classifier_event = index < vr::engine_stereo_execution::api_count;
		const auto expected_scope = classifier_event
			? vr::engine_stereo_execution::classifier_scope_flag
			: vr::engine_stereo_execution::frame_scope_flag;
		const auto& expected_backend = classifier_event
			? classifier_backend : frame_backend;
		const auto& expected_owner = classifier_event
			? classifier_owner : frame_owner;
		if (event.sequence != static_cast<std::uint64_t>(index) + 1 ||
			event.timestamp_qpc == 0 || event.context !=
				reinterpret_cast<std::uintptr_t>(context.Get()) ||
			event.caller == 0 || event.thread_id != GetCurrentThreadId() ||
			event.scope_flags != expected_scope || !event.expected_context ||
			!event.output_binding.valid || event.output_binding.render_target_0 !=
				reinterpret_cast<std::uintptr_t>(render_target.Get()) ||
			event.output_binding.depth_stencil !=
				reinterpret_cast<std::uintptr_t>(depth_stencil.Get()) ||
			!event.backend || !event.backend_thread_match || event.call_stack_depth == 0 ||
			event.call_stack[0] == 0 ||
			event.backend.backend_id != expected_backend.backend_id ||
			event.backend.frontend_epoch != expected_backend.frontend_epoch ||
			event.backend.frontend_transaction_id !=
				expected_backend.frontend_transaction_id ||
			event.backend.record != expected_backend.record ||
			event.backend.frontend != expected_backend.frontend ||
			event.backend.command_stream != expected_backend.command_stream ||
			event.backend.record_index != expected_backend.record_index ||
			event.backend.record_type != expected_backend.record_type ||
			event.backend.target_id != expected_backend.target_id ||
			event.backend.owner_thread_id != expected_backend.owner_thread_id ||
			event.backend.dispatch_entered != expected_backend.dispatch_entered ||
			!event.scene_owner || !event.scene_owner_thread_match ||
			event.scene_owner.observation_id != expected_owner.observation_id ||
			event.scene_owner.record != expected_owner.record ||
			event.scene_owner.frontend != expected_owner.frontend ||
			event.scene_owner.caller != expected_owner.caller ||
			event.scene_owner.record_index != expected_owner.record_index ||
			event.scene_owner.record_type != expected_owner.record_type ||
			event.scene_owner.target_ids != expected_owner.target_ids ||
			event.scene_owner.target_selector != expected_owner.target_selector ||
			event.scene_owner.owner_thread_id != expected_owner.owner_thread_id ||
			event.scene_owner.record_valid != expected_owner.record_valid)
		{
			std::cerr << "event=" << index << " api=" <<
				vr::engine_stereo_execution::to_string(event.operation) <<
				" context=0x" << std::hex << event.context << " caller=0x" <<
				event.caller << " binding_valid=" << std::dec <<
				event.output_binding.valid << " rtv=0x" << std::hex <<
				event.output_binding.render_target_0 << " dsv=0x" <<
				event.output_binding.depth_stencil << std::dec << '\n';
			fail("event identity or output binding is incomplete");
		}
		if ((event.scope_flags & vr::engine_stereo_execution::classifier_scope_flag) != 0 &&
			event.timestamp_qpc > first->classifier_end_qpc)
		{
			fail("classifier event was timestamped after its closed input gate");
		}
	}
	if (first->events[0].operation != vr::engine_stereo_execution::api::draw_indexed ||
		first->events[0].argument_count != 3 || first->events[0].arguments[0] != 3 ||
		first->events[0].arguments[1] != 2 ||
		static_cast<std::int64_t>(first->events[0].arguments[2]) != -1)
	{
		fail("DrawIndexed normalized arguments are incorrect");
	}

	// Terminal evidence must be immutable even though natural D3D calls continue
	// and the matching device later announces destruction.
	const auto terminal = state;
	Microsoft::WRL::ComPtr<ID3D11Device> replacement_device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> replacement_context;
	D3D_FEATURE_LEVEL replacement_feature_level{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
		nullptr, 0, D3D11_SDK_VERSION, &replacement_device,
		&replacement_feature_level, &replacement_context)) ||
		!vr::engine_stereo_output_merger::install(replacement_context.Get(), 2) ||
		!vr::engine_stereo_draw_indexed::install(replacement_context.Get(), 2) ||
		!vr::engine_stereo_execution::install(replacement_context.Get(), 2))
	{
		fail("production-order terminal target verification unexpectedly failed");
	}
	const auto after_terminal_install = vr::engine_stereo_execution::get_status();
	const auto after_output_install = vr::engine_stereo_output_merger::get_status();
	const auto after_draw_install = vr::engine_stereo_draw_indexed::get_status();
	auto after_output_install_report =
		std::make_unique<vr::engine_stereo_output_merger::report>();
	auto after_draw_install_report =
		std::make_unique<vr::engine_stereo_draw_indexed::report>();
	auto after_execution_install_report =
		std::make_unique<vr::engine_stereo_execution::report>();
	if (!same_execution_status(terminal, after_terminal_install) ||
		!same_output_status(terminal_output_merger, after_output_install) ||
		!same_draw_status(terminal_draw_indexed, after_draw_install) ||
		!vr::engine_stereo_output_merger::read_report(*after_output_install_report) ||
		!vr::engine_stereo_draw_indexed::read_report(*after_draw_install_report) ||
		!vr::engine_stereo_execution::read_report(*after_execution_install_report) ||
		std::memcmp(terminal_output_report.get(), after_output_install_report.get(),
			sizeof(*terminal_output_report)) != 0 ||
		std::memcmp(terminal_draw_report.get(), after_draw_install_report.get(),
			sizeof(*terminal_draw_report)) != 0 ||
		std::memcmp(first.get(), after_execution_install_report.get(),
			sizeof(*first)) != 0)
	{
		fail("production-order callback mixed terminal device identities");
	}
	emit_all(context.Get(), indirect_arguments.Get(), empty_command_list.Get());
	// Mirror vr_component::on_device_destroying exactly.
	vr::engine_stereo_execution::invalidate_device(context.Get(), 1);
	vr::engine_stereo_output_merger::invalidate_device(context.Get(), 1);
	vr::engine_stereo_draw_indexed::invalidate_device(context.Get(), 1);
	const auto after_invalidate = vr::engine_stereo_execution::get_status();
	const auto output_after_invalidate =
		vr::engine_stereo_output_merger::get_status();
	const auto draw_after_invalidate = vr::engine_stereo_draw_indexed::get_status();
	auto second = std::make_unique<vr::engine_stereo_execution::report>();
	auto output_after_invalidate_report =
		std::make_unique<vr::engine_stereo_output_merger::report>();
	auto draw_after_invalidate_report =
		std::make_unique<vr::engine_stereo_draw_indexed::report>();
	if (!same_execution_status(terminal, after_invalidate) ||
		!same_output_status(terminal_output_merger, output_after_invalidate) ||
		!same_draw_status(terminal_draw_indexed, draw_after_invalidate) ||
		!vr::engine_stereo_execution::read_report(*second) ||
		!vr::engine_stereo_output_merger::read_report(*output_after_invalidate_report) ||
		!vr::engine_stereo_draw_indexed::read_report(*draw_after_invalidate_report) ||
		std::memcmp(first.get(), second.get(), sizeof(*first)) != 0 ||
		std::memcmp(terminal_output_report.get(), output_after_invalidate_report.get(),
			sizeof(*terminal_output_report)) != 0 ||
		std::memcmp(terminal_draw_report.get(), draw_after_invalidate_report.get(),
			sizeof(*terminal_draw_report)) != 0)
	{
		fail("terminal report was mutated by later natural execution");
	}

	// The gate intentionally opens while its owner still publishes the arming ->
	// active transition. A call admitted in that opening window belongs to the
	// new scope and must not be misclassified as a close collision.
	if (!vr::engine_stereo_execution::reset())
	{
		fail("terminal census did not reset before opening-window test");
	}
	vr::engine_stereo_execution::classifier_scope opening_classifier{};
	if (!vr::engine_stereo_execution::begin_classifier(opening_classifier,
			0x2A340000, 4) ||
		!vr::engine_stereo_execution::end_classifier(opening_classifier))
	{
		fail("opening-window classifier did not complete");
	}
	vr::engine_stereo_execution::test_arm_admission_pause(
		vr::engine_stereo_execution::admission_test_stage::after_gate_open);
	std::thread frame_opening_owner([&]
	{
		const auto owner_thread = GetCurrentThreadId();
		vr::engine_stereo_execution::on_present_post(180, 1, owner_thread, S_OK);
		vr::engine_stereo_execution::on_present_pre(181, 1, owner_thread);
		vr::engine_stereo_execution::on_present_post(181, 1, owner_thread, S_OK);
	});
	wait_for_admission_pause();
	context->Draw(1, 0);
	const auto opening_armed = vr::engine_stereo_execution::get_status();
	if (opening_armed.state != vr::engine_stereo_execution::gate_state::frame_arming ||
		opening_armed.frame_events != 1 || opening_armed.admission_collisions != 0)
	{
		vr::engine_stereo_execution::test_release_admission_pause();
		frame_opening_owner.join();
		fail("valid opening-window call was rejected");
	}
	vr::engine_stereo_execution::test_release_admission_pause();
	frame_opening_owner.join();
	const auto opening_complete = vr::engine_stereo_execution::get_status();
	auto opening_report = std::make_unique<vr::engine_stereo_execution::report>();
	if (opening_complete.state != vr::engine_stereo_execution::gate_state::complete ||
		opening_complete.error != vr::engine_stereo_execution::failure::none ||
		opening_complete.admission_collisions != 0 ||
		opening_complete.recorded_events != 1 || opening_complete.frame_events != 1 ||
		!vr::engine_stereo_execution::read_report(*opening_report) ||
		opening_report->admission_collisions != 0 ||
		opening_report->frame_event_count != 1 ||
		opening_report->events[0].scope_flags !=
			vr::engine_stereo_execution::frame_scope_flag)
	{
		fail("opening-window evidence did not complete cleanly");
	}

	// Hold a natural call after it has authoritatively observed classifier_active
	// and registered its writer, then close the classifier gate. The call must be
	// rejected as an explicit admission collision; it cannot disappear from a
	// nominally complete census.
	if (!vr::engine_stereo_execution::reset())
	{
		fail("terminal census did not reset before classifier admission test");
	}
	vr::engine_stereo_execution::classifier_scope admission_classifier{};
	if (!vr::engine_stereo_execution::begin_classifier(admission_classifier,
		0x32340000, 4))
	{
		fail("classifier admission test did not begin");
	}
	vr::engine_stereo_execution::test_arm_admission_pause(
		vr::engine_stereo_execution::admission_test_stage::after_state_check);
	std::thread classifier_admission_call([&]
	{
		context->Draw(1, 0);
	});
	wait_for_admission_pause();
	if (!vr::engine_stereo_execution::end_classifier(admission_classifier))
	{
		vr::engine_stereo_execution::test_release_admission_pause();
		classifier_admission_call.join();
		fail("classifier gate did not close around the paused admission");
	}
	const auto classifier_closing = vr::engine_stereo_execution::get_status();
	if (classifier_closing.state !=
			vr::engine_stereo_execution::gate_state::classifier_closing ||
		classifier_closing.active_writers != 1)
	{
		vr::engine_stereo_execution::test_release_admission_pause();
		classifier_admission_call.join();
		fail("classifier close did not wait for the admission writer");
	}
	vr::engine_stereo_execution::test_release_admission_pause();
	classifier_admission_call.join();
	const auto classifier_admission = vr::engine_stereo_execution::get_status();
	auto classifier_admission_report =
		std::make_unique<vr::engine_stereo_execution::report>();
	if (classifier_admission.state !=
			vr::engine_stereo_execution::gate_state::failed ||
		classifier_admission.error !=
			vr::engine_stereo_execution::failure::admission_collision ||
		classifier_admission.admission_collisions != 1 ||
		classifier_admission.active_writers != 0 ||
		classifier_admission.completions != 0 ||
		classifier_admission.failures != 1 ||
		!vr::engine_stereo_execution::read_report(*classifier_admission_report) ||
		classifier_admission_report->admission_collisions != 1 ||
		classifier_admission_report->event_count != 0)
	{
		fail("classifier admission collision was not a bounded hard failure");
	}

	// Repeat across a complete Present boundary. Present-post must remain
	// nonterminal while the admission writer is paused, and the last writer must
	// publish failed rather than complete.
	if (!vr::engine_stereo_execution::reset())
	{
		fail("classifier admission failure did not reset");
	}
	vr::engine_stereo_execution::classifier_scope frame_classifier{};
	if (!vr::engine_stereo_execution::begin_classifier(frame_classifier,
			0x42340000, 4) ||
		!vr::engine_stereo_execution::end_classifier(frame_classifier))
	{
		fail("frame admission classifier did not complete");
	}
	vr::engine_stereo_execution::on_present_post(200, 1, GetCurrentThreadId(), S_OK);
	vr::engine_stereo_execution::test_arm_admission_pause(
		vr::engine_stereo_execution::admission_test_stage::after_gate_claim);
	std::thread frame_admission_call([&]
	{
		context->Draw(1, 0);
	});
	wait_for_admission_pause();
	vr::engine_stereo_execution::on_present_pre(201, 1, GetCurrentThreadId());
	vr::engine_stereo_execution::on_present_post(201, 1, GetCurrentThreadId(), S_OK);
	const auto frame_closing = vr::engine_stereo_execution::get_status();
	if (frame_closing.state != vr::engine_stereo_execution::gate_state::frame_closing ||
		frame_closing.active_writers != 1)
	{
		vr::engine_stereo_execution::test_release_admission_pause();
		frame_admission_call.join();
		fail("Present-post published terminal before admission resolved");
	}
	vr::engine_stereo_execution::test_release_admission_pause();
	frame_admission_call.join();
	const auto frame_admission = vr::engine_stereo_execution::get_status();
	auto frame_admission_report =
		std::make_unique<vr::engine_stereo_execution::report>();
	if (frame_admission.state != vr::engine_stereo_execution::gate_state::failed ||
		frame_admission.error !=
			vr::engine_stereo_execution::failure::admission_collision ||
		frame_admission.admission_collisions != 1 ||
		frame_admission.active_writers != 0 || frame_admission.completions != 0 ||
		frame_admission.failures != 1 ||
		!vr::engine_stereo_execution::read_report(*frame_admission_report) ||
		frame_admission_report->admission_collisions != 1 ||
		frame_admission_report->frame_start_present_post != 200 ||
		frame_admission_report->frame_end_present_pre != 201 ||
		frame_admission_report->frame_end_present_post != 201)
	{
		fail("Present-boundary admission collision was not a bounded hard failure");
	}

	// A second claimed classifier scope is a hard collision, including when both
	// callers race from classifier_pending. No scope may be silently merged.
	if (!vr::engine_stereo_execution::reset())
	{
		fail("terminal census did not reset exclusively");
	}
	if (vr::engine_stereo_execution::bootstrap_complete())
	{
		fail("reset retained completed bootstrap admission");
	}
	std::atomic_uint32_t collision_ready{};
	std::atomic_uint32_t collision_begun{};
	std::array<bool, 2> collision_results{};
	std::array<std::thread, 2> collision_threads;
	for (std::size_t index{}; index < collision_threads.size(); ++index)
	{
		collision_threads[index] = std::thread([&, index]
		{
			vr::engine_stereo_execution::classifier_scope scope{};
			collision_ready.fetch_add(1, std::memory_order_release);
			while (collision_ready.load(std::memory_order_acquire) != 2)
			{
				SwitchToThread();
			}
			collision_results[index] = vr::engine_stereo_execution::begin_classifier(
				scope, 0x22340000 + index, 4);
			collision_begun.fetch_add(1, std::memory_order_release);
			while (collision_begun.load(std::memory_order_acquire) != 2)
			{
				SwitchToThread();
			}
			if (scope.active)
			{
				(void)vr::engine_stereo_execution::end_classifier(scope);
			}
		});
	}
	for (auto& thread : collision_threads) thread.join();
	const auto collision = vr::engine_stereo_execution::get_status();
	if (collision.state != vr::engine_stereo_execution::gate_state::failed ||
		collision.error != vr::engine_stereo_execution::failure::classifier_collision ||
		(collision_results[0] && collision_results[1]))
	{
		fail("racing classifier scopes were not rejected as a hard collision");
	}

	std::cout << "deferred_target_match_mask=0x" << std::hex <<
		deferred_target_match_mask << std::dec << " observed_slots=" <<
		observed_slots.size() << '\n';
	std::cout << "vr-d3d11-execution-probe: PASS\n";
	return 0;
}
