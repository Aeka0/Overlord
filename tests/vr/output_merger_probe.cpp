#include "std_include.hpp"

#include "component/vr/engine_stereo_output_merger.hpp"
#include "component/vr/hidden_area_mask.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
	std::uint32_t observed_color_clears{};
	std::uint32_t observed_depth_clears{};
	std::uint32_t observed_ssr_color_clears{};
	std::uint32_t observed_ssr_depth_clears{};
	std::uint32_t observed_color_target{0xFFFFFFFFu};
	std::uint32_t observed_depth_target{0xFFFFFFFFu};
	std::uintptr_t observed_color_view{};
	std::uintptr_t observed_depth_view{};
	std::uint32_t observed_clear_states{};
	std::uintptr_t observed_clear_state_caller{};
	ID3D11DeviceContext* observed_clear_state_context{};
	std::uint32_t observed_post_clear_calls{};
	ID3D11DeviceContext* observed_post_clear_context{};
	ID3D11DepthStencilView* observed_post_clear_view{};
	UINT observed_post_clear_flags{};
	FLOAT observed_post_clear_depth{};

	void observe_post_clear_depth(ID3D11DeviceContext* const context,
		ID3D11DepthStencilView* const view, const UINT flags,
		const FLOAT depth) noexcept
	{
		++observed_post_clear_calls;
		observed_post_clear_context = context;
		observed_post_clear_view = view;
		observed_post_clear_flags = flags;
		observed_post_clear_depth = depth;
	}

	void observe_color_clear(ID3D11DeviceContext*, ID3D11RenderTargetView* const view,
		const std::uintptr_t, const std::uint32_t target_id, const std::uintptr_t,
		const std::uint64_t) noexcept
	{
		++observed_color_clears;
		observed_color_target = target_id;
		observed_color_view = reinterpret_cast<std::uintptr_t>(view);
	}

	void observe_depth_clear(ID3D11DeviceContext*, ID3D11DepthStencilView* const view,
		const std::uint32_t, const std::uintptr_t, const std::uint32_t target_id,
		const std::uintptr_t, const std::uint64_t) noexcept
	{
		++observed_depth_clears;
		observed_depth_target = target_id;
		observed_depth_view = reinterpret_cast<std::uintptr_t>(view);
	}

	void observe_ssr_color_clear(ID3D11DeviceContext*, ID3D11RenderTargetView*,
		std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint64_t) noexcept
	{
		++observed_ssr_color_clears;
	}

	void observe_ssr_depth_clear(ID3D11DeviceContext*, ID3D11DepthStencilView*,
		std::uint32_t, std::uintptr_t, std::uint32_t, std::uintptr_t,
		std::uint64_t) noexcept
	{
		++observed_ssr_depth_clears;
	}

	void observe_clear_state(ID3D11DeviceContext* const context,
		const std::uintptr_t caller) noexcept
	{
		++observed_clear_states;
		observed_clear_state_context = context;
		observed_clear_state_caller = caller;
	}

	[[noreturn]] void fail(const char* const message)
	{
		std::cerr << "vr-d3d11-output-merger-probe: FAIL; " << message << '\n';
		std::exit(1);
	}

	[[nodiscard]] bool same_status(
		const vr::engine_stereo_output_merger::status& left,
		const vr::engine_stereo_output_merger::status& right) noexcept
	{
		return left.state == right.state &&
			left.hook_installed == right.hook_installed &&
			left.extended_hooks_installed == right.extended_hooks_installed &&
			left.hook_target == right.hook_target &&
			left.unordered_access_hook_target == right.unordered_access_hook_target &&
			left.clear_state_hook_target == right.clear_state_hook_target &&
			left.clear_render_target_hook_target ==
				right.clear_render_target_hook_target &&
			left.clear_depth_stencil_hook_target ==
				right.clear_depth_stencil_hook_target &&
			left.expected_context == right.expected_context &&
			left.device_generation == right.device_generation &&
			left.hook_failures == right.hook_failures &&
			left.attempts == right.attempts && left.completions == right.completions &&
			left.failures == right.failures && left.bind_calls == right.bind_calls &&
			left.nonnull_bind_calls == right.nonnull_bind_calls &&
			left.context_mismatches == right.context_mismatches &&
			left.thread_mismatches == right.thread_mismatches &&
			left.query_failures == right.query_failures &&
			left.overflows == right.overflows &&
			left.metadata_queries == right.metadata_queries &&
			left.unordered_access_calls == right.unordered_access_calls &&
			left.clear_state_calls == right.clear_state_calls &&
			left.clear_render_target_calls == right.clear_render_target_calls &&
			left.clear_depth_stencil_calls == right.clear_depth_stencil_calls &&
			left.binding_invalidations == right.binding_invalidations &&
			left.latest_publication_sequence == right.latest_publication_sequence &&
			left.latest_record == right.latest_record;
	}

	void verify_hidden_area_mask_and_post_clear_hook(ID3D11Device* const device,
		ID3D11DeviceContext* const context, ID3D11DepthStencilView* const depth_stencil)
	{
		vr::engine_stereo_output_merger::set_post_clear_depth_hook(&observe_post_clear_depth);

		vr::engine_stereo_output_merger::replay_guard guard{};
		if (!vr::engine_stereo_output_merger::begin_replay_guard(guard, context))
		{
			fail("post-clear replay guard did not begin");
		}
		context->ClearDepthStencilView(depth_stencil, D3D11_CLEAR_DEPTH, 0.0f, 0);
		if (!vr::engine_stereo_output_merger::end_replay_guard(guard))
		{
			fail("ClearDepthStencilView triggered output-merger replay guard");
		}
		if (observed_post_clear_calls != 0)
		{
			fail("post_clear_depth_hook fired during active replay_guard");
		}

		context->ClearDepthStencilView(depth_stencil, D3D11_CLEAR_DEPTH, 0.0f, 0);
		if (observed_post_clear_calls != 1 ||
			observed_post_clear_context != context ||
			observed_post_clear_view != depth_stencil ||
			observed_post_clear_flags != D3D11_CLEAR_DEPTH ||
			observed_post_clear_depth != 0.0f)
		{
			fail("post_clear_depth_hook did not observe expected ClearDepthStencilView arguments");
		}

		vr::engine_stereo_output_merger::set_post_clear_depth_hook(nullptr);
		context->ClearDepthStencilView(depth_stencil, D3D11_CLEAR_DEPTH, 0.0f, 0);
		if (observed_post_clear_calls != 1)
		{
			fail("unhooking post_clear_depth_hook did not stop callbacks");
		}

		D3D11_TEXTURE2D_DESC depth64_desc{};
		depth64_desc.Width = 64;
		depth64_desc.Height = 64;
		depth64_desc.MipLevels = 1;
		depth64_desc.ArraySize = 1;
		depth64_desc.Format = DXGI_FORMAT_R32_TYPELESS;
		depth64_desc.SampleDesc.Count = 1;
		depth64_desc.Usage = D3D11_USAGE_DEFAULT;
		depth64_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> depth64;
		if (FAILED(device->CreateTexture2D(&depth64_desc, nullptr, &depth64)) || !depth64)
		{
			fail("64x64 typeless depth texture creation failed");
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC dsv64_desc{};
		dsv64_desc.Format = DXGI_FORMAT_D32_FLOAT;
		dsv64_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsv64_desc.Texture2D.MipSlice = 0;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv64;
		if (FAILED(device->CreateDepthStencilView(depth64.Get(), &dsv64_desc, &dsv64)) || !dsv64)
		{
			fail("64x64 D32_FLOAT DSV creation failed");
		}

		D3D11_TEXTURE2D_DESC staging_desc = depth64_desc;
		staging_desc.Format = DXGI_FORMAT_R32_FLOAT;
		staging_desc.Usage = D3D11_USAGE_STAGING;
		staging_desc.BindFlags = 0;
		staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> staging64;
		if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, &staging64)) || !staging64)
		{
			fail("64x64 R32_FLOAT staging texture creation failed");
		}

		context->ClearDepthStencilView(dsv64.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
		context->OMSetRenderTargets(0, nullptr, depth_stencil);

		vr::hidden_area_mask::set_eye_projection(0, {-1.0f, 1.0f, -1.0f, 1.0f});
		const std::array<vr::hidden_area_mask::vertex2, 3> tri{{
			{-1.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}
		}};
		const std::array<std::uint32_t, 3> indices{{0u, 1u, 2u}};
		vr::hidden_area_mask::set_eye_mesh(0, tri.data(), tri.size(), indices.data(), indices.size());

		if (vr::hidden_area_mask::stamp_eye_depth(context, dsv64.Get(), 0, 32, 32))
		{
			fail("stamp_eye_depth accepted mismatched expected extent");
		}
		if (!vr::hidden_area_mask::stamp_eye_depth(context, dsv64.Get(), 0, 64, 64))
		{
			fail("stamp_eye_depth failed for valid 64x64 DSV");
		}

		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> restored_dsv;
		context->OMGetRenderTargets(0, nullptr, &restored_dsv);
		if (restored_dsv.Get() != depth_stencil)
		{
			fail("stamp_eye_depth did not restore immediate-context DSV binding");
		}

		context->CopyResource(staging64.Get(), depth64.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(staging64.Get(), 0, D3D11_MAP_READ, 0, &mapped)) || !mapped.pData)
		{
			fail("staging depth readback Map failed");
		}
		const auto* const bytes = static_cast<const std::uint8_t*>(mapped.pData);
		const auto read_depth = [&](const UINT x, const UINT y) noexcept -> float
		{
			float value{};
			std::memcpy(&value,
				bytes + static_cast<std::size_t>(y) * mapped.RowPitch +
					static_cast<std::size_t>(x) * sizeof(float),
				sizeof(float));
			return value;
		};
		const float covered = read_depth(4, 4);
		const float uncovered = read_depth(48, 48);
		context->Unmap(staging64.Get(), 0);
		if (covered != 1.0f || uncovered != 0.0f)
		{
			fail("hidden area mask depth values did not match reverse-Z expectation");
		}

		const auto before_clear = vr::hidden_area_mask::get_diagnostics();
		if (before_clear.stamps != 1 || !before_clear.has_mesh[0] || before_clear.has_mesh[1] ||
			before_clear.eye_triangles[0] != 1 || before_clear.eye_triangles[1] != 0)
		{
			fail("hidden area mask diagnostics mismatch after stamp");
		}

		vr::hidden_area_mask::invalidate_device();
		if (!vr::hidden_area_mask::stamp_eye_depth(context, dsv64.Get(), 0, 64, 64))
		{
			fail("stamp_eye_depth failed to rebuild after invalidate_device");
		}

		vr::hidden_area_mask::clear();
		vr::hidden_area_mask::invalidate_device();
		const auto after_clear = vr::hidden_area_mask::get_diagnostics();
		if (after_clear.has_mesh[0] || after_clear.has_mesh[1] ||
			after_clear.eye_triangles[0] != 0 || after_clear.eye_triangles[1] != 0 ||
			vr::hidden_area_mask::stamp_eye_depth(context, dsv64.Get(), 0, 64, 64))
		{
			fail("hidden area mask clear/invalidate_device did not reset state cleanly");
		}
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

	if (!vr::engine_stereo_output_merger::install(context.Get(), 1))
	{
		fail("OMSetRenderTargets observer installation failed");
	}
	vr::engine_stereo_output_merger::set_clear_observers(
		vr::engine_stereo_output_merger::clear_observer_channel::gpu_census,
		observe_color_clear, observe_depth_clear);
	vr::engine_stereo_output_merger::set_clear_observers(
		vr::engine_stereo_output_merger::clear_observer_channel::ssr_consumer,
		observe_ssr_color_clear, observe_ssr_depth_clear);
	vr::engine_stereo_output_merger::set_clear_state_observer(observe_clear_state);
	std::array<std::uint8_t, 0x170> record{};
	vr::engine_stereo_binding::backend_claim claim{};
	claim.publication_sequence = 1;
	claim.record = reinterpret_cast<std::uintptr_t>(record.data());
	vr::engine_stereo_output_merger::transaction transaction{};
	if (!vr::engine_stereo_output_merger::begin(transaction, claim, claim.record))
	{
		fail("observation transaction did not begin");
	}

	vr::engine_stereo_output_merger::note_view_copy(transaction, 1, 1, 0);
	vr::engine_stereo_output_merger::note_target(transaction, 13);
	auto* color_view = render_target.Get();
	context->OMSetRenderTargets(1, &color_view, depth_stencil.Get());
	const auto initial_binding =
		vr::engine_stereo_output_merger::get_current_binding(context.Get());
	if (!initial_binding.valid || initial_binding.render_target_count != 1 ||
		initial_binding.target_id != 13 ||
		initial_binding.render_target_0 !=
			reinterpret_cast<std::uintptr_t>(render_target.Get()) ||
		initial_binding.depth_stencil !=
			reinterpret_cast<std::uintptr_t>(depth_stencil.Get()))
	{
		fail("current output binding snapshot did not match the natural bind");
	}
	const FLOAT clear_color[4]{0.25f, 0.5f, 0.75f, 1.0f};
	context->ClearRenderTargetView(render_target.Get(), clear_color);
	context->ClearDepthStencilView(depth_stencil.Get(),
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	if (observed_color_clears != 1 || observed_depth_clears != 1 ||
		observed_ssr_color_clears != 1 || observed_ssr_depth_clears != 1 ||
		observed_color_target != 13 || observed_depth_target != 13 ||
		observed_color_view != reinterpret_cast<std::uintptr_t>(render_target.Get()) ||
		observed_depth_view != reinterpret_cast<std::uintptr_t>(depth_stencil.Get()))
	{
		fail("clear observers did not preserve exact view and target identity");
	}
	vr::engine_stereo_output_merger::set_clear_observers(
		vr::engine_stereo_output_merger::clear_observer_channel::gpu_census,
		nullptr, nullptr);
	context->ClearRenderTargetView(render_target.Get(), clear_color);
	context->ClearDepthStencilView(depth_stencil.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
	if (observed_color_clears != 1 || observed_depth_clears != 1 ||
		observed_ssr_color_clears != 2 || observed_ssr_depth_clears != 2)
	{
		fail("detaching one clear-observer channel displaced another channel");
	}
	vr::engine_stereo_output_merger::enter_dispatch(transaction);
	vr::engine_stereo_output_merger::note_target(transaction, 1);
	context->OMSetRenderTargets(0, nullptr, depth_stencil.Get());
	vr::engine_stereo_output_merger::leave_dispatch(transaction);
	vr::engine_stereo_output_merger::end(transaction, true);

	const auto status = vr::engine_stereo_output_merger::get_status();
	vr::engine_stereo_output_merger::report report{};
	if (status.state != vr::engine_stereo_output_merger::gate_state::complete ||
		status.attempts != 1 || status.completions != 1 || status.failures != 0 ||
		status.bind_calls != 2 || status.nonnull_bind_calls != 2 ||
		status.context_mismatches != 0 || status.thread_mismatches != 0 ||
		status.query_failures != 0 || status.overflows != 0 ||
		status.metadata_queries == 0 ||
		status.clear_render_target_calls != 2 ||
		status.clear_depth_stencil_calls != 2 ||
		!vr::engine_stereo_output_merger::read_report(report) || report.event_count != 2)
	{
		fail("observer status did not close cleanly");
	}
	const auto& before_dispatch = report.events[0];
	const auto& dispatch = report.events[1];
	if (before_dispatch.execution_phase != vr::engine_stereo_output_merger::phase::before_dispatch ||
		before_dispatch.latest_target_id != 13 || before_dispatch.render_target_count != 1 ||
		!before_dispatch.render_targets[0].valid ||
		before_dispatch.render_targets[0].width != color_description.Width ||
		before_dispatch.render_targets[0].height != color_description.Height ||
		before_dispatch.render_targets[0].resource_format != color_description.Format ||
		!before_dispatch.depth_stencil.valid ||
		dispatch.execution_phase != vr::engine_stereo_output_merger::phase::dispatch ||
		dispatch.latest_target_id != 1 || dispatch.render_target_count != 0 ||
		!dispatch.depth_stencil.valid)
	{
		fail("captured output-merger evidence does not match the submitted views");
	}

	// Production owner passes run after the one-shot observation transaction.
	// Target publication must therefore remain exact even when the diagnostic
	// transaction object is inactive.
	vr::engine_stereo_output_merger::note_target(transaction, 17);
	context->OMSetRenderTargets(1, &color_view, depth_stencil.Get());
	const auto production_binding =
		vr::engine_stereo_output_merger::get_current_binding(context.Get());
	if (!production_binding.valid || production_binding.target_id != 17 ||
		production_binding.render_target_0 !=
			reinterpret_cast<std::uintptr_t>(render_target.Get()))
	{
		fail("production target identity was not published after probe completion");
	}

	vr::engine_stereo_output_merger::replay_guard replay_guard{};
	if (!vr::engine_stereo_output_merger::begin_replay_guard(replay_guard,
		context.Get()))
	{
		fail("replay output-binding guard did not begin");
	}
	auto* natural_view = render_target.Get();
	context->OMSetRenderTargets(1, &natural_view, depth_stencil.Get());
	if (vr::engine_stereo_output_merger::end_replay_guard(replay_guard) ||
		replay_guard.output_bind_calls != 1 || replay_guard.invalid_calls != 0)
	{
		fail("replay output-binding guard did not reject an unexpected bind");
	}
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> bound_view;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> bound_depth;
	context->OMGetRenderTargets(1, &bound_view, &bound_depth);
	if (bound_view.Get() != render_target.Get() || bound_depth.Get() != depth_stencil.Get())
	{
		fail("replay output-binding guard modified the natural binding");
	}
	vr::engine_stereo_output_merger::replay_guard clean_replay_guard{};
	if (!vr::engine_stereo_output_merger::begin_replay_guard(clean_replay_guard,
		context.Get()) ||
		!vr::engine_stereo_output_merger::end_replay_guard(clean_replay_guard))
	{
		fail("zero-bind replay output-binding guard did not close cleanly");
	}
	context->OMSetRenderTargets(0, nullptr, nullptr);
	const auto cleared_binding =
		vr::engine_stereo_output_merger::get_current_binding(context.Get());
	if (!cleared_binding.valid ||
		cleared_binding.sequence <= initial_binding.sequence ||
		cleared_binding.render_target_count != 0 ||
		cleared_binding.render_target_0 != 0 || cleared_binding.depth_stencil != 0)
	{
		fail("current output binding snapshot did not observe the clear");
	}
	context->ClearState();
	if (observed_clear_states != 1 ||
		observed_clear_state_context != context.Get() ||
		observed_clear_state_caller == 0)
	{
		fail("ClearState observer did not preserve context/caller identity");
	}

	// Production device callbacks may arrive after the one-shot report closed.
	// They may validate the process-wide vtable targets, but must not splice the
	// old report onto a replacement context or device generation.
	vr::engine_stereo_output_merger::invalidate_device(context.Get(), 1);
	Microsoft::WRL::ComPtr<ID3D11Device> replacement_device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> replacement_context;
	D3D_FEATURE_LEVEL replacement_feature_level{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
		nullptr, 0, D3D11_SDK_VERSION, &replacement_device,
		&replacement_feature_level, &replacement_context)) ||
		!vr::engine_stereo_output_merger::install(replacement_context.Get(), 2))
	{
		fail("terminal replacement-device target validation failed");
	}
	const auto immutable_status = vr::engine_stereo_output_merger::get_status();
	vr::engine_stereo_output_merger::report immutable_report{};
	if (!same_status(status, immutable_status) ||
		!vr::engine_stereo_output_merger::read_report(immutable_report) ||
		std::memcmp(&report, &immutable_report, sizeof(report)) != 0)
	{
		fail("terminal output-merger identity changed after device replacement");
	}

	verify_hidden_area_mask_and_post_clear_hook(device.Get(), context.Get(), depth_stencil.Get());

	std::cout << "vr-d3d11-output-merger-probe: PASS\n";
	return 0;
}
