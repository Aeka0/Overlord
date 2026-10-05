#include <std_include.hpp>

#include "component/vr/engine_stereo_gpu_timing.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace
{
	using vr::engine_stereo_gpu_timing::capture_state;
	using vr::engine_stereo_gpu_timing::failure;

	int fail(const char* const reason)
	{
		std::cerr << "vr-d3d11-gpu-timing-probe: FAIL; " << reason << '\n';
		return 1;
	}

	bool prepare(ID3D11Device* const device, ID3D11DeviceContext* const context,
		const std::uint64_t generation)
	{
		vr::engine_stereo_gpu_timing::reset_for_tests();
		return vr::engine_stereo_gpu_timing::prepare_device(device, context, generation);
	}

	bool record_segment(ID3D11DeviceContext* const context,
		ID3D11RenderTargetView* const target, const std::uint64_t pair_id,
		const std::uint64_t generation, const std::uint32_t eye,
		const bool owner, const float red)
	{
		const auto began = owner ?
			vr::engine_stereo_gpu_timing::begin_owner(pair_id, eye, context, generation) :
			vr::engine_stereo_gpu_timing::begin_native_conversion(
				pair_id, eye, context, generation);
		if (!began) return false;
		const FLOAT color[4]{red, static_cast<float>(eye), owner ? 0.0f : 1.0f, 1.0f};
		context->ClearRenderTargetView(target, color);
		return owner ?
			vr::engine_stereo_gpu_timing::end_owner(pair_id, eye, context, generation) :
			vr::engine_stereo_gpu_timing::end_native_conversion(
				pair_id, eye, context, generation);
	}
}

int main()
{
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL feature_level{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
		&device, &feature_level, &context)))
	{
		return fail("D3D11CreateDevice(WARP)");
	}

	D3D11_TEXTURE2D_DESC description{};
	description.Width = 16;
	description.Height = 16;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_RENDER_TARGET;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
	if (FAILED(device->CreateTexture2D(&description, nullptr, &texture)) ||
		FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &target)))
	{
		return fail("timing target creation");
	}

	constexpr std::uint64_t generation = 7;
	constexpr std::uint64_t pair_id = 101;
	if (!prepare(device.Get(), context.Get(), generation))
		return fail("query preparation");
	if (vr::engine_stereo_gpu_timing::begin_pair(pair_id, context.Get(), generation,
		false))
	{
		return fail("unstable pair was sampled");
	}
	const auto skipped = vr::engine_stereo_gpu_timing::get_report();
	if (skipped.state != capture_state::ready || skipped.eligibility_checks != 1 ||
		skipped.eligibility_skips != 1 || skipped.capture_attempts != 0)
	{
		return fail("ineligible pair preservation");
	}
	if (!vr::engine_stereo_gpu_timing::begin_pair(pair_id, context.Get(), generation,
		true))
	{
		return fail("stable pair begin");
	}
	if (!record_segment(context.Get(), target.Get(), pair_id, generation, 0, true, 0.1f) ||
		!record_segment(context.Get(), target.Get(), pair_id, generation, 0, false, 0.2f) ||
		!record_segment(context.Get(), target.Get(), pair_id, generation, 1, true, 0.3f) ||
		!record_segment(context.Get(), target.Get(), pair_id, generation, 1, false, 0.4f))
	{
		return fail("ordered marker recording");
	}
	vr::engine_stereo_gpu_timing::finish_pair(pair_id, context.Get(), generation, true);
	const auto pending = vr::engine_stereo_gpu_timing::get_report();
	if (pending.state != capture_state::pending || pending.marker_mask != 0xffu ||
		pending.retirement_observed)
	{
		return fail("pending complete-pair contract");
	}

	vr::engine_stereo_gpu_timing::poll_retired_pair(pair_id - 1,
		context.Get(), generation);
	const auto wrong_retirement = vr::engine_stereo_gpu_timing::get_report();
	if (wrong_retirement.state != capture_state::pending ||
		wrong_retirement.retirement_observed || wrong_retirement.retirement_polls != 0 ||
		wrong_retirement.get_data_calls != 0)
	{
		return fail("unrelated retirement isolation");
	}

	// The harness may make queued WARP work visible, but the implementation under
	// test never Flushes and every read below remains DONOTFLUSH and bounded.
	context->Flush();
	for (std::uint32_t poll{};
		poll < vr::engine_stereo_gpu_timing::maximum_retirement_polls; ++poll)
	{
		// WARP completes timestamp queries on its worker thread. Give that worker a
		// bounded scheduling opportunity between nonblocking production-style polls;
		// this wait exists only in the standalone test process.
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		const auto started = std::chrono::steady_clock::now();
		vr::engine_stereo_gpu_timing::poll_retired_pair(pair_id + poll,
			context.Get(), generation);
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count();
		if (elapsed >= 100) return fail("GetData poll blocked");
		const auto current = vr::engine_stereo_gpu_timing::get_report();
		if (current.state == capture_state::complete) break;
		if (current.state == capture_state::unavailable)
			return fail("WARP query became unavailable");
		std::this_thread::yield();
	}
	const auto complete = vr::engine_stereo_gpu_timing::get_report();
	if (complete.state != capture_state::complete || complete.error != failure::none ||
		complete.capture_attempts != 1 || complete.capture_completions != 1 ||
		complete.capture_failures != 0 || complete.pair_id != pair_id ||
		complete.retired_pair < pair_id || !complete.retirement_observed ||
		complete.retirement_polls == 0 || complete.get_data_calls < 9 ||
		complete.get_data_flags != D3D11_ASYNC_GETDATA_DONOTFLUSH ||
		complete.frequency == 0 || complete.disjoint || complete.marker_mask != 0xffu ||
		!std::is_sorted(complete.timestamps.begin(), complete.timestamps.end()))
	{
		std::cerr << "state=" << vr::engine_stereo_gpu_timing::to_string(complete.state)
			<< " error=" << vr::engine_stereo_gpu_timing::to_string(complete.error)
			<< " attempts=" << complete.capture_attempts
			<< " completions=" << complete.capture_completions
			<< " failures=" << complete.capture_failures
			<< " pair=" << complete.pair_id
			<< " retired=" << complete.retired_pair
			<< " polls=" << complete.retirement_polls
			<< " get_data=" << complete.get_data_calls
			<< " flags=" << complete.get_data_flags
			<< " frequency=" << complete.frequency
			<< " disjoint=" << complete.disjoint
			<< " mask=" << complete.marker_mask << '\n';
		return fail("completed timing evidence");
	}
	if (vr::engine_stereo_gpu_timing::begin_pair(pair_id + 100, context.Get(),
		generation, true) ||
		vr::engine_stereo_gpu_timing::prepare_device(device.Get(), context.Get(), generation))
	{
		return fail("completed generation rearmed");
	}

	constexpr std::uint64_t incomplete_pair = 201;
	if (!prepare(device.Get(), context.Get(), generation + 1) ||
		!vr::engine_stereo_gpu_timing::begin_pair(incomplete_pair, context.Get(),
			generation + 1, true) ||
		!record_segment(context.Get(), target.Get(), incomplete_pair, generation + 1,
			0, true, 0.5f))
	{
		return fail("incomplete setup");
	}
	vr::engine_stereo_gpu_timing::finish_pair(incomplete_pair, context.Get(),
		generation + 1, false);
	const auto incomplete = vr::engine_stereo_gpu_timing::get_report();
	if (incomplete.state != capture_state::unavailable ||
		incomplete.error != failure::incomplete_pair || incomplete.capture_failures != 1 ||
		vr::engine_stereo_gpu_timing::begin_pair(incomplete_pair + 1, context.Get(),
			generation + 1, true))
	{
		return fail("incomplete pair terminal state");
	}

	constexpr std::uint64_t sequence_pair = 301;
	if (!prepare(device.Get(), context.Get(), generation + 2) ||
		!vr::engine_stereo_gpu_timing::begin_pair(sequence_pair, context.Get(),
			generation + 2, true))
	{
		return fail("sequence setup");
	}
	if (vr::engine_stereo_gpu_timing::begin_owner(sequence_pair, 1, context.Get(),
		generation + 2))
	{
		return fail("out-of-order marker accepted");
	}
	const auto sequence = vr::engine_stereo_gpu_timing::get_report();
	if (sequence.state != capture_state::unavailable ||
		sequence.error != failure::sequence)
	{
		return fail("sequence terminal state");
	}

	if (!prepare(device.Get(), context.Get(), generation + 3))
		return fail("generation setup");
	if (vr::engine_stereo_gpu_timing::begin_pair(401, context.Get(), generation + 4,
		true))
	{
		return fail("wrong generation accepted");
	}
	const auto wrong_generation = vr::engine_stereo_gpu_timing::get_report();
	if (wrong_generation.state != capture_state::unavailable ||
		wrong_generation.error != failure::device_generation)
	{
		return fail("generation terminal state");
	}

	constexpr std::uint64_t ring_pair = 501;
	if (!prepare(device.Get(), context.Get(), generation + 4) ||
		!vr::engine_stereo_gpu_timing::begin_pair(ring_pair, context.Get(),
			generation + 4, true))
	{
		return fail("native-ring failure setup");
	}
	vr::engine_stereo_gpu_timing::fail_pair(ring_pair, context.Get(), generation + 4,
		failure::native_ring);
	const auto ring_failure = vr::engine_stereo_gpu_timing::get_report();
	if (ring_failure.state != capture_state::unavailable ||
		ring_failure.error != failure::native_ring || ring_failure.capture_failures != 1)
	{
		return fail("native-ring terminal state");
	}

	vr::engine_stereo_gpu_timing::reset_for_tests();
	std::cout << "vr-d3d11-gpu-timing-probe: PASS\n";
	return 0;
}
