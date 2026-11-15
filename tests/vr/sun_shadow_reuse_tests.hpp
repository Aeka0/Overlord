#pragma once
#include "component/vr/engine_stereo_sun_shadow.hpp"
#include "component/vr/engine_stereo_resource_ops.hpp"
#include <thread>

namespace sun_shadow_reuse_tests
{
	template<class Check> void run(ID3D11Device* device, ID3D11DeviceContext* context,
		ID3D11Texture2D* depth, ID3D11DepthStencilView* view, ID3D11RenderTargetView* color, Check check)
	{
		namespace sun = vr::engine_stereo_sun_shadow;
		sun::range range{};
		range.valid = true; range.begin = 0; range.end = 2; range.left.data = 1; range.left.resource = 2;
		auto right = range.left;
		check(range.accepts(right, 0, 4, false), "sun exact pair was refused");
		right.sun_visibility[3] = 1;
		check(!range.accepts(right, 0, 4, false), "sun visibility mismatch accepted"); right = range.left;
		right.partitions[1][16] = 1;
		check(!range.accepts(right, 0, 4, false), "sun light-view mismatch accepted"); right = range.left;
		check(!range.accepts(right, 1, 4, false) && !range.accepts(right, 0, 1, false), "sun ordinal range crossed native boundary");
		check(!range.accepts(right, 0, 4, true), "modified sun image accepted");
		range.open = true; check(!range.accepts(right, 0, 4, false), "unfinished left sun accepted");

		check(vr::engine_stereo_execution::install(context, 321) && vr::engine_stereo_output_merger::install(context, 321) &&
			vr::engine_stereo_resource_ops::install(context, 321), "sun shared D3D observers not installed");
		Microsoft::WRL::ComPtr<ID3D11Texture2D> other;
		D3D11_TEXTURE2D_DESC d{}; depth->GetDesc(&d);
		check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &other)), "sun alternate depth creation failed");
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> alias;
		check(SUCCEEDED(device->CreateDepthStencilView(depth, nullptr, &alias)), "sun alias DSV creation failed");
		sun::watch(depth, context);
		context->ClearDepthStencilView(view, D3D11_CLEAR_DEPTH, 1, 0);
		check(!sun::modified(), "own left rendering invalidated sun image");
		std::thread foreign([&] { context->ClearDepthStencilView(alias.Get(), D3D11_CLEAR_DEPTH, .5f, 0); });
		foreign.join();
		check(sun::modified(), "foreign-thread alias clear was missed during left rendering");
		sun::unwatch(); sun::watch(depth, context); sun::finish_left();
		const float black[4]{}; context->ClearRenderTargetView(color, black);
		context->CopyResource(other.Get(), depth);
		check(!sun::modified(), "unrelated clear or shadow read invalidated sun image");
		context->CopyResource(depth, other.Get());
		check(sun::modified(), "copy into shared sun resource was missed");
		sun::unwatch(); sun::watch(depth, context); sun::finish_left();
		context->ClearDepthStencilView(alias.Get(), D3D11_CLEAR_DEPTH, .25f, 0);
		check(sun::modified(), "alternate DSV writer was missed");
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred;
		Microsoft::WRL::ComPtr<ID3D11CommandList> commands;
		check(SUCCEEDED(device->CreateDeferredContext(0, &deferred)) && SUCCEEDED(deferred->FinishCommandList(FALSE, &commands)), "sun command list creation failed");
		sun::unwatch(); sun::watch(depth, context); sun::finish_left();
		context->ExecuteCommandList(commands.Get(), TRUE);
		check(sun::modified(), "opaque command list did not preserve native right path");
		check(vr::native_conversion_command_list::mark(commands.Get()), "private conversion marker failed");
		sun::unwatch(); sun::watch(depth, context); sun::finish_left();
		context->ExecuteCommandList(commands.Get(), TRUE);
		check(!sun::modified(), "known state-restoring conversion invalidated sun image");
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> other_view;
		check(SUCCEEDED(device->CreateDepthStencilView(other.Get(), nullptr, &other_view)), "sun second DSV creation failed");
		deferred->OMSetRenderTargets(0, nullptr, other_view.Get()); commands.Reset();
		check(SUCCEEDED(deferred->FinishCommandList(FALSE, &commands)) && vr::native_conversion_command_list::mark(commands.Get()), "sun restoring-list creation failed");
		context->OMSetRenderTargets(0, nullptr, view);
		sun::unwatch(); sun::watch(depth, context); sun::finish_left();
		context->ExecuteCommandList(commands.Get(), TRUE);
		context->Draw(0, 0);
		check(sun::modified(), "restored native DSV was hidden by a private command-list binding observation");
		sun::unwatch();
		check(!sun::watching(reinterpret_cast<std::uintptr_t>(depth), context), "retired sun ownership stayed armed");
	}
}
