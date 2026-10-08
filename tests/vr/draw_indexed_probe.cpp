#include "std_include.hpp"

#include "component/vr/engine_stereo_draw_indexed.hpp"
#include "component/vr/native_hud_blend.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace
{
	unsigned copy_calls{};
	bool reenter_copy{};
	unsigned forwarded_calls{};
	vr::engine_stereo_draw_indexed::draw_original forwarding_draw{};
	__declspec(noinline) void __stdcall alternate_draw(ID3D11DeviceContext* context,
		UINT count, UINT start, INT base)
	{
		++forwarded_calls;
		forwarding_draw(context, count, start, base);
	}
	void copy_draw(ID3D11DeviceContext* context, UINT count, UINT start, INT base,
		vr::engine_stereo_draw_indexed::draw_original original) noexcept
	{
		// A copy goes through the supplied trampoline, not the hooked vtable.
		// The transaction below must still see exactly one native draw.
		if (++copy_calls > 1 || !original) std::abort();
		if (reenter_copy) context->DrawIndexed(count, start, base);
		original(context, count, start, base);
		// A native fade may feed both the scene canvas and nearer title ink.
		// Neither trampoline copy may recursively notify the observer.
		original(context, count, start, base);
	}
	void emit_dynamic_batch(ID3D11DeviceContext* const context,
		const UINT start_index)
	{
		context->DrawIndexed(6, start_index, 0);
		context->DrawIndexed(54, start_index + 6, 0);
	}

	[[noreturn]] void fail(const char* const message)
	{
		std::cerr << "vr-d3d11-draw-indexed-probe: FAIL; " << message << '\n';
		std::exit(1);
	}

	[[nodiscard]] bool same_status(
		const vr::engine_stereo_draw_indexed::status& left,
		const vr::engine_stereo_draw_indexed::status& right) noexcept
	{
		return left.state == right.state &&
			left.hook_installed == right.hook_installed &&
			left.hook_target == right.hook_target &&
			left.expected_context == right.expected_context &&
			left.device_generation == right.device_generation &&
			left.hook_failures == right.hook_failures &&
			left.attempts == right.attempts && left.completions == right.completions &&
			left.failures == right.failures && left.draw_calls == right.draw_calls &&
			left.context_mismatches == right.context_mismatches &&
			left.thread_mismatches == right.thread_mismatches &&
			left.invalid_arguments == right.invalid_arguments &&
			left.overflows == right.overflows &&
			left.latest_publication_sequence == right.latest_publication_sequence &&
			left.latest_record == right.latest_record;
	}
}

int main()
{
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL feature_level{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &device, &feature_level, &context)))
	{
		fail("D3D11CreateDevice(WARP) failed");
	}

	if (!vr::engine_stereo_draw_indexed::install(context.Get(), 1))
	{
		fail("DrawIndexed observer installation failed");
	}

	D3D11_BLEND_DESC native_blend{};
	native_blend.IndependentBlendEnable = TRUE;
	auto& native_rgb = native_blend.RenderTarget[0];
	native_rgb = {TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA,
		D3D11_BLEND_OP_REV_SUBTRACT, D3D11_BLEND_SRC_ALPHA,
		D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, 15};
	D3D11_BLEND_DESC coverage{};
	if (vr::native_hud_capture::coverage_blend(native_blend, false, coverage))
		fail("colored/unknown reverse subtraction must not flatten to ordinary alpha");
	if(!vr::native_hud_capture::subtractive_coverage_blend(native_blend,coverage) ||
		coverage.RenderTarget[0].BlendOp!=D3D11_BLEND_OP_ADD || coverage.RenderTarget[0].SrcBlend!=D3D11_BLEND_SRC_ALPHA ||
		coverage.RenderTarget[0].DestBlend!=D3D11_BLEND_INV_SRC_ALPHA || coverage.RenderTarget[0].SrcBlendAlpha!=D3D11_BLEND_ONE)
		fail("native fixed-scope shadow must retain its positive subtractand and coverage in a separate layer");
	{
		auto flash=native_blend;auto& rgb=flash.RenderTarget[0];rgb.SrcBlend=rgb.DestBlend=D3D11_BLEND_ONE;rgb.BlendOp=D3D11_BLEND_OP_ADD;
		if(!vr::native_hud_capture::additive_coverage_blend(flash,coverage) || coverage.RenderTarget[0].SrcBlendAlpha!=D3D11_BLEND_ZERO ||
			coverage.RenderTarget[0].DestBlendAlpha!=D3D11_BLEND_ONE || coverage.RenderTarget[0].DestBlend!=D3D11_BLEND_ONE)
			fail("native additive scope flash must emit light without obscuring the background");
	}
	if (!vr::native_hud_capture::coverage_blend(native_blend, true, coverage) ||
		coverage.RenderTarget[0].BlendOp != native_rgb.BlendOp ||
		coverage.RenderTarget[0].SrcBlend != native_rgb.SrcBlend ||
		coverage.RenderTarget[0].DestBlend != native_rgb.DestBlend ||
		coverage.RenderTarget[0].SrcBlendAlpha != D3D11_BLEND_ONE)
		fail("audited black reverse subtraction must preserve RGB and accumulate coverage");
	Microsoft::WRL::ComPtr<ID3D11BlendState> coverage_state;
	if (FAILED(device->CreateBlendState(&coverage, &coverage_state)))
		fail("D3D rejects black-subtractive coverage state");
	auto attenuation=coverage;
	vr::native_hud_capture::attenuate_text_blend(attenuation);
	const auto& fade_blend=attenuation.RenderTarget[0];
	if(fade_blend.SrcBlend!=D3D11_BLEND_DEST_ALPHA || fade_blend.DestBlend!=D3D11_BLEND_INV_SRC_ALPHA ||
		fade_blend.SrcBlendAlpha!=D3D11_BLEND_ZERO || fade_blend.DestBlendAlpha!=D3D11_BLEND_ONE ||
		fade_blend.BlendOp!=D3D11_BLEND_OP_ADD || fade_blend.BlendOpAlpha!=D3D11_BLEND_OP_ADD)
		fail("announcement fade must dim RGB while preserving glyph coverage");
	Microsoft::WRL::ComPtr<ID3D11BlendState> attenuation_state;
	if(FAILED(device->CreateBlendState(&attenuation,&attenuation_state))) fail("D3D rejects announcement attenuation blend");
	// Existing captured content E + D*(1-A), followed by a black quad of
	// opacity b, is E*(1-b) + D*(1-A)*(1-b). Alpha union is exact for any D.
	for (int a = 0; a <= 255; a += 15)
		for (int b = 0; b <= 255; b += 15)
			for (int d = 0; d <= 255; d += 15)
			{
				const double A = a / 255.0, B = b / 255.0, D = d / 255.0;
				const double E = A * 0.7;
				const auto native = (E + D * (1 - A)) * (1 - B);
				const auto flattened = E * (1 - B) + D * (1 - (B + A * (1 - B)));
				if (std::abs(native - flattened) > 1e-12) fail("black coverage algebra mismatch");
				const auto separated=E*(1-B)+(D*(1-B))*(1-A);
				if(std::abs(native-separated)>1e-12) fail("near announcement plane applies global fade more than once");
				const auto white_native=B+(E+D*(1-A))*(1-B);
				const auto white_separated=E*(1-B)+B*A+(B+D*(1-B))*(1-A);
				if(std::abs(white_native-white_separated)>1e-12) fail("white fade changes split title coverage or native draw order");
			}
	native_rgb.BlendOp = D3D11_BLEND_OP_ADD;
	if (!vr::native_hud_capture::coverage_blend(native_blend, false, coverage))
		fail("ordinary native pip/text blend rejected");
	native_rgb.SrcBlend = D3D11_BLEND_ONE;
	if (!vr::native_hud_capture::coverage_blend(native_blend, false, coverage))
		fail("premultiplied native UI blend rejected");
	native_blend.AlphaToCoverageEnable = TRUE;
	if (vr::native_hud_capture::coverage_blend(native_blend, true, coverage))
		fail("alpha-to-coverage without multisample semantics accepted");
	native_blend.AlphaToCoverageEnable = FALSE;
	native_rgb.BlendOp = D3D11_BLEND_OP_SUBTRACT;
	if (vr::native_hud_capture::coverage_blend(native_blend, true, coverage))
		fail("unproven subtract effect accepted");
	native_rgb.BlendOp = D3D11_BLEND_OP_ADD;
	native_rgb.DestBlend = D3D11_BLEND_ONE;
	if (vr::native_hud_capture::coverage_blend(native_blend, true, coverage))
		fail("additive destination accepted as alpha layer");
	native_rgb.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	native_rgb.RenderTargetWriteMask = 1;
	if (vr::native_hud_capture::coverage_blend(native_blend, true, coverage))
		fail("partial RGB coverage accepted");

	const std::array<std::uint8_t, 7> commands{4, 0, 16, 0, 0, 0, 0};
	std::array<std::uint8_t, 0x170> record{};
	vr::engine_stereo_binding::backend_claim claim{};
	claim.publication_sequence = 1;
	claim.record = reinterpret_cast<std::uintptr_t>(record.data());

	auto transaction = std::make_unique<vr::engine_stereo_draw_indexed::transaction>();
	if (!vr::engine_stereo_draw_indexed::begin(*transaction, claim, claim.record))
	{
		fail("observation transaction did not begin");
	}
	vr::engine_stereo_draw_indexed::enter_dispatch(*transaction, commands.data());
	vr::engine_stereo_draw_indexed::set_draw_copy_observer(copy_draw);
	context->DrawIndexed(3, 2, -1);
	vr::engine_stereo_draw_indexed::set_draw_copy_observer(nullptr);
	if (copy_calls != 1) fail("native draw copy callback did not run exactly once");
	vr::engine_stereo_draw_indexed::leave_dispatch(*transaction, commands.data());
	vr::engine_stereo_draw_indexed::end(*transaction, true);

	const auto status = vr::engine_stereo_draw_indexed::get_status();
	auto report = std::make_unique<vr::engine_stereo_draw_indexed::report>();
	if (status.state != vr::engine_stereo_draw_indexed::gate_state::complete ||
		status.attempts != 1 || status.completions != 1 || status.failures != 0 ||
		status.draw_calls != 1 || status.context_mismatches != 0 ||
		status.thread_mismatches != 0 || status.invalid_arguments != 0 ||
		status.overflows != 0 ||
		!vr::engine_stereo_draw_indexed::read_report(*report))
	{
		fail("observer status did not close cleanly");
	}
	if (!report->before.valid || !report->after.valid ||
		report->before.address != reinterpret_cast<std::uintptr_t>(commands.data()) ||
		report->before.address != report->after.address ||
		report->before.byte_count != commands.size() ||
		report->before.byte_count != report->after.byte_count ||
		report->before.record_count != 1 ||
		report->before.record_count != report->after.record_count ||
		report->before.hash != report->after.hash || report->event_count != 1 ||
		report->boundary_draw_calls != 1 || report->boundary_group_count != 1 ||
		report->boundary_group_overflows != 0)
	{
		fail("command stream immutability evidence is incomplete");
	}
	const auto& event = report->events[0];
	if (!event.expected_context || !event.arguments_valid || event.index_count != 3 ||
		event.start_index_location != 2 || event.base_vertex_location != -1)
	{
		fail("captured DrawIndexed evidence does not match the submitted call");
	}
	const auto& boundary = report->boundary_groups[0];
	if (boundary.execution_phase !=
			vr::engine_stereo_draw_indexed::boundary_phase::dispatch ||
		boundary.draw_calls != 1 || boundary.index_count != 3 ||
		boundary.first_caller == 0 || boundary.last_caller != boundary.first_caller ||
		boundary.first_start_index != 2 || boundary.last_start_index != 2 ||
		boundary.first_base_vertex != -1 || boundary.last_base_vertex != -1)
	{
		fail("full backend-boundary draw grouping is incomplete");
	}

	vr::engine_stereo_draw_indexed::replay_counter replay{};
	if (!vr::engine_stereo_draw_indexed::begin_replay(replay, context.Get()))
	{
		fail("bounded replay counter did not begin");
	}
	context->DrawIndexed(6, 4, 2);
	if (!vr::engine_stereo_draw_indexed::end_replay(replay) ||
		replay.draw_calls != 1 || replay.context_mismatches != 0 ||
		replay.thread_mismatches != 0 || replay.invalid_arguments != 0 ||
		replay.overflows != 0 || replay.draw_call_hash == 0 ||
		replay.draw_shape_hash == 0 || replay.event_count != 1 ||
		replay.events[0].index_count != 6 ||
		replay.events[0].start_index_location != 4 ||
		replay.events[0].base_vertex_location != 2 ||
		replay.events[0].caller == 0)
	{
		fail("bounded replay counter did not capture the exact DrawIndexed call");
	}

	vr::engine_stereo_draw_indexed::replay_counter natural_batch{};
	if (!vr::engine_stereo_draw_indexed::begin_replay(natural_batch, context.Get()))
	{
		fail("natural dynamic-batch counter did not begin");
	}
	emit_dynamic_batch(context.Get(), 100);
	if (!vr::engine_stereo_draw_indexed::end_replay(natural_batch))
	{
		fail("natural dynamic-batch counter did not close");
	}

	vr::engine_stereo_draw_indexed::replay_counter appended_batch{};
	if (!vr::engine_stereo_draw_indexed::begin_replay(appended_batch, context.Get()))
	{
		fail("appended dynamic-batch counter did not begin");
	}
	emit_dynamic_batch(context.Get(), 160);
	if (!vr::engine_stereo_draw_indexed::end_replay(appended_batch))
	{
		fail("appended dynamic-batch counter did not close");
	}
	const auto appended = vr::engine_stereo_draw_indexed::compare_replay(
		natural_batch, appended_batch);
	if (!appended.trace_complete || !appended.shape_matches ||
		appended.exact_matches || !appended.dynamic_index_append_matches ||
		!appended.semantic_matches || appended.start_index_delta != 60 ||
		appended.natural_index_count != 60)
	{
		fail("valid contiguous dynamic-index append was rejected");
	}

	vr::engine_stereo_draw_indexed::replay_counter exact_batch{};
	if (!vr::engine_stereo_draw_indexed::begin_replay(exact_batch, context.Get()))
	{
		fail("exact dynamic-batch counter did not begin");
	}
	emit_dynamic_batch(context.Get(), 100);
	if (!vr::engine_stereo_draw_indexed::end_replay(exact_batch))
	{
		fail("exact dynamic-batch counter did not close");
	}
	const auto exact = vr::engine_stereo_draw_indexed::compare_replay(
		natural_batch, exact_batch);
	if (!exact.trace_complete || !exact.shape_matches || !exact.exact_matches ||
		exact.dynamic_index_append_matches || !exact.semantic_matches ||
		exact.start_index_delta != 0 || exact.natural_index_count != 60)
	{
		fail("exact dynamic-index replay was rejected");
	}

	vr::engine_stereo_draw_indexed::replay_counter invalid_batch{};
	if (!vr::engine_stereo_draw_indexed::begin_replay(invalid_batch, context.Get()))
	{
		fail("invalid dynamic-batch counter did not begin");
	}
	emit_dynamic_batch(context.Get(), 161);
	if (!vr::engine_stereo_draw_indexed::end_replay(invalid_batch))
	{
		fail("invalid dynamic-batch counter did not close");
	}
	const auto invalid = vr::engine_stereo_draw_indexed::compare_replay(
		natural_batch, invalid_batch);
	if (!invalid.trace_complete || !invalid.shape_matches ||
		invalid.exact_matches || invalid.dynamic_index_append_matches ||
		invalid.semantic_matches)
	{
		fail("invalid dynamic-index offset was accepted");
	}

	vr::engine_stereo_draw_indexed::invalidate_device(context.Get(), 1);
	Microsoft::WRL::ComPtr<ID3D11Device> replacement_device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> replacement_context;
	D3D_FEATURE_LEVEL replacement_feature_level{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
		nullptr, 0, D3D11_SDK_VERSION, &replacement_device,
		&replacement_feature_level, &replacement_context)) ||
		!vr::engine_stereo_draw_indexed::install(replacement_context.Get(), 2))
	{
		fail("terminal replacement-device target validation failed");
	}
	const auto immutable_status = vr::engine_stereo_draw_indexed::get_status();
	auto immutable_report =
		std::make_unique<vr::engine_stereo_draw_indexed::report>();
	if (!same_status(status, immutable_status) ||
		!vr::engine_stereo_draw_indexed::read_report(*immutable_report) ||
		std::memcmp(report.get(), immutable_report.get(), sizeof(*report)) != 0)
	{
		fail("terminal DrawIndexed identity changed after device replacement");
	}

	const auto live_before = vr::engine_stereo_draw_indexed::get_hook_status();
	if (!live_before.installed || live_before.context != reinterpret_cast<std::uintptr_t>(replacement_context.Get()) ||
		live_before.generation != 2)
		fail("continuous hook owner did not follow the replacement device");
	{
		// Give this real WARP context a private copy of its base interface vtable.
		// Slot 12 forwards into the retained native entry, reproducing an API mode
		// switch even on drivers which keep the same entry for all context modes.
		auto** const table_pointer = reinterpret_cast<void***>(replacement_context.Get());
		auto* const native_table = *table_pointer;
		std::array<void*, 115> alternate_table{}; // ID3D11DeviceContext base ABI.
		std::copy_n(native_table, alternate_table.size(), alternate_table.begin());
		forwarding_draw = reinterpret_cast<vr::engine_stereo_draw_indexed::draw_original>(native_table[12]);
		alternate_table[12] = reinterpret_cast<void*>(alternate_draw);
		*table_pointer = alternate_table.data();
		const auto restore = gsl::finally([&] { *table_pointer = native_table; });
		alternate_table[12] = nullptr;
		if (vr::engine_stereo_draw_indexed::install(replacement_context.Get(), 2) ||
			vr::engine_stereo_draw_indexed::install(replacement_context.Get(), 2) ||
			vr::engine_stereo_draw_indexed::get_hook_status().installed ||
			vr::engine_stereo_draw_indexed::get_hook_status().failures != live_before.failures + 1)
			fail("invalid entry was admitted or retried at every UI boundary");
		alternate_table[12] = reinterpret_cast<void*>(alternate_draw);
		if (!vr::engine_stereo_draw_indexed::install(replacement_context.Get(), 2))
			fail("changed DrawIndexed entry was rejected after terminal evidence");
		copy_calls = forwarded_calls = 0;
		reenter_copy = true;
		vr::engine_stereo_draw_indexed::set_draw_copy_observer(copy_draw);
		replacement_context->DrawIndexed(3, 2, -1);
		vr::engine_stereo_draw_indexed::set_draw_copy_observer(nullptr);
		reenter_copy = false;
		const auto switched = vr::engine_stereo_draw_indexed::get_hook_status();
		if (copy_calls != 1 || forwarded_calls != 4 || !switched.installed ||
			switched.target != reinterpret_cast<std::uintptr_t>(alternate_draw) ||
			switched.retained_targets != live_before.retained_targets + 1 ||
			switched.target_changes != live_before.target_changes + 1 ||
			switched.nested_draws <= live_before.nested_draws)
			fail("entry forwarding or observer re-entry lost native draws/duplicated capture");
		*table_pointer = native_table;
		if (!vr::engine_stereo_draw_indexed::install(replacement_context.Get(), 2) ||
			vr::engine_stereo_draw_indexed::get_hook_status().retained_targets != switched.retained_targets)
			fail("return to a retained entry replaced/duplicated its trampoline");
	}
	vr::engine_stereo_draw_indexed::invalidate_device(context.Get(), 1);
	if (vr::engine_stereo_draw_indexed::install(context.Get(), 1) ||
		!vr::engine_stereo_draw_indexed::get_hook_status().installed)
		fail("late retired-device callback displaced the current hook owner");
	vr::engine_stereo_draw_indexed::invalidate_device(replacement_context.Get(), 2);
	if (vr::engine_stereo_draw_indexed::get_hook_status().installed ||
		vr::engine_stereo_draw_indexed::install(replacement_context.Get(), 2))
		fail("invalidated device was re-admitted by a late frame callback");
	if (!same_status(status, vr::engine_stereo_draw_indexed::get_status()) ||
		!vr::engine_stereo_draw_indexed::read_report(*immutable_report) ||
		std::memcmp(report.get(), immutable_report.get(), sizeof(*report)) != 0)
		fail("live entry reconciliation changed the frozen evidence report");
	std::cout << "vr-d3d11-draw-indexed-probe: PASS\n";
	return 0;
}
