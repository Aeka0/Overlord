#include <std_include.hpp>

#include "component/vr/engine_stereo_eye_resources.hpp"
#include "component/vr/engine_stereo_pointer_cache.hpp"
#include "component/vr/engine_stereo_output_merger.hpp"
#include "component/vr/engine_stereo_resource_ops.hpp"
#include "native_post_aa_boundary_tests.hpp"

#include <array>
#include <iostream>
#include <thread>

namespace
{
	int fail(const char* const reason)
	{
		std::cerr << "vr-d3d11-eye-resource-isolation-probe: FAIL; "
			<< reason << '\n';
		return 1;
	}

	template <typename View>
	Microsoft::WRL::ComPtr<ID3D11Resource> resource_of(View* const view)
	{
		Microsoft::WRL::ComPtr<ID3D11Resource> output;
		if (view != nullptr) view->GetResource(output.GetAddressOf());
		return output;
	}

	bool read_first_pixel(ID3D11Device* const device,
		ID3D11DeviceContext* const context, ID3D11Texture2D* const source,
		std::uint32_t& output, const UINT mip = 0)
	{
		D3D11_TEXTURE2D_DESC description{};
		source->GetDesc(&description);
		if (mip >= description.MipLevels) return false;
		description.Width = (std::max)(1u, description.Width >> mip);
		description.Height = (std::max)(1u, description.Height >> mip);
		description.MipLevels = 1;
		description.BindFlags = 0;
		description.MiscFlags = 0;
		description.Usage = D3D11_USAGE_STAGING;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
		if (FAILED(device->CreateTexture2D(&description, nullptr, &staging))) return false;
		context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source, mip, nullptr);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
			return false;
		std::memcpy(&output, mapped.pData, sizeof(output));
		context->Unmap(staging.Get(), 0);
		return true;
	}
}

int main()
{
	if (!native_post_aa_frame_tests()) return fail("AA frame reader changed native registers, flags or stack");
	vr::stereo_pointer_index<1> mappings;
	if (mappings.find(0) || mappings.find(0x1000)) return fail("empty view index");
	mappings.insert(0x1000, 0);
	if (!mappings.find(0x1000) || *mappings.find(0x1000) != 0)
		return fail("mapping zero is a valid index");
	mappings.insert(0x2000, 63);
	if (mappings.find(0x1000) || !mappings.find(0x2000) || *mappings.find(0x2000) != 63)
		return fail("view index collision must fall back, never alias");
	mappings.clear();
	if (mappings.find(0x2000)) return fail("view index resource generation reset");
	mappings.insert(0x2000, 7);
	if (!mappings.find(0x2000) || *mappings.find(0x2000) != 7)
		return fail("view address reuse after resource reset");
	// Force eviction with one slot. A collision must cause a real re-query,
	// never classify a different pointer as foreign. Reset is still required at
	// the known pointer-lifetime boundary, as in the production pair lifecycle.
	vr::stereo_pointer_cache<1> collision_cache;
	if (collision_cache.contains(0) || collision_cache.contains(0x1000))
		return fail("empty foreign-view cache");
	collision_cache.insert(0x1000);
	if (!collision_cache.contains(0x1000) || collision_cache.contains(0x2000))
		return fail("foreign-view exact identity");
	collision_cache.insert(0x2000);
	if (collision_cache.contains(0x1000) || !collision_cache.contains(0x2000))
		return fail("foreign-view collision eviction");
	collision_cache.clear();
	if (collision_cache.contains(0x2000)) return fail("foreign-view pair reset");
	vr::stereo_pointer_cache<1024> churn_cache;
	vr::stereo_pointer_cache<4> bucket_cache;
	for (std::uintptr_t i = 1; i <= 4; ++i) bucket_cache.insert(i * 16);
	for (std::uintptr_t i = 1; i <= 4; ++i)
		if (!bucket_cache.contains(i * 16)) return fail("colliding foreign views coexist");
	bucket_cache.insert(80);
	if (bucket_cache.contains(16) || !bucket_cache.contains(80) || !bucket_cache.contains(32))
		return fail("bounded foreign bucket eviction");
	bucket_cache.clear();
	if (bucket_cache.contains(80)) return fail("foreign bucket lifetime reset");
	for (std::uintptr_t index = 1; index <= 8192; ++index)
	{
		const auto identity = index * 16;
		if (churn_cache.contains(identity)) return fail("foreign-view false positive");
		churn_cache.insert(identity);
		if (!churn_cache.contains(identity)) return fail("foreign-view insertion");
	}
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL feature_level{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
		&device, &feature_level, &context)))
	{
		return fail("D3D11CreateDevice(WARP)");
	}
	if (!vr::engine_stereo_output_merger::install(context.Get(), 1) ||
		!vr::engine_stereo_resource_ops::install(context.Get(), 1) ||
		!vr::engine_stereo_eye_resources::install(context.Get(), 1))
	{
		return fail("hook installation");
	}

	D3D11_TEXTURE2D_DESC description{};
	description.Width = 16;
	description.Height = 16;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE |
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> auxiliary;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_rtv;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> target_srv;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> target_uav;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> auxiliary_rtv;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> auxiliary_srv;
	if (FAILED(device->CreateTexture2D(&description, nullptr, &target)) ||
		FAILED(device->CreateTexture2D(&description, nullptr, &auxiliary)) ||
		FAILED(device->CreateRenderTargetView(target.Get(), nullptr, &target_rtv)) ||
		FAILED(device->CreateShaderResourceView(target.Get(), nullptr, &target_srv)) ||
		FAILED(device->CreateUnorderedAccessView(target.Get(), nullptr, &target_uav)) ||
		FAILED(device->CreateRenderTargetView(auxiliary.Get(), nullptr, &auxiliary_rtv)) ||
		FAILED(device->CreateShaderResourceView(auxiliary.Get(), nullptr, &auxiliary_srv)))
	{
		return fail("target creation");
	}

	const FLOAT black[4]{0.0f, 0.0f, 0.0f, 1.0f};
	const FLOAT green[4]{0.0f, 1.0f, 0.0f, 1.0f};
	const FLOAT blue[4]{0.0f, 0.0f, 1.0f, 1.0f};
	const FLOAT red[4]{1.0f, 0.0f, 0.0f, 1.0f};
	context->ClearRenderTargetView(target_rtv.Get(), black);
	context->ClearRenderTargetView(auxiliary_rtv.Get(), blue);

	const std::array<vr::engine_stereo_eye_resources::source_binding,
		vr::engine_stereo_eye_resources::isolated_target_count> bindings{{
		{vr::engine_stereo_eye_resources::isolated_target_id,
			vr::engine_stereo_eye_resources::role::color, target_rtv.Get()},
	}};
	if (!vr::engine_stereo_eye_resources::begin_pair(1, device.Get(), context.Get(),
		1, bindings) ||
		!vr::engine_stereo_eye_resources::set_exact_read_proof(1, true))
	{
		return fail("first pair begin");
	}
	const auto prepared = vr::engine_stereo_eye_resources::get_status();
	if (!prepared.resources_ready || prepared.resource_rebuilds != 1 ||
		prepared.seed_copies != 2 || prepared.targets[0].target_id !=
		vr::engine_stereo_eye_resources::isolated_target_id)
	{
		return fail("both-eye target91 preparation contract");
	}

	if (!vr::engine_stereo_eye_resources::begin_eye(1, 0))
		return fail("left eye begin");
	context->ClearRenderTargetView(target_rtv.Get(), green);
	ID3D11ShaderResourceView* shader_view = target_srv.Get();
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &shader_view);
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> left_alias;
	context->PSGetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot, 1, &left_alias);
	// Exercise the whole setter prefix: nulls must be explicit, the last slot
	// must be assigned, and an untouched foreign view must retain its identity.
	std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> mixed{};
	mixed[0] = auxiliary_srv.Get();
	mixed[vr::engine_stereo_eye_resources::exact_read_ps_slot] = target_srv.Get();
	mixed.back() = left_alias.Get();
	context->PSSetShaderResources(0, static_cast<UINT>(mixed.size()), mixed.data());
	std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> observed{};
	context->PSGetShaderResources(0, static_cast<UINT>(observed.size()), observed.data());
	bool mixed_valid = observed[0] == auxiliary_srv.Get();
	for (std::size_t i = 1; i < observed.size(); ++i)
	{
		const bool isolated = i == vr::engine_stereo_eye_resources::exact_read_ps_slot || i == observed.size() - 1;
		const bool slot_valid = isolated ? resource_of(observed[i]).Get() ==
			reinterpret_cast<ID3D11Resource*>(prepared.targets[0].left_resource) : observed[i] == nullptr;
		mixed_valid &= slot_valid;
	}
	for (auto* view : observed) if (view) view->Release();
	if (!mixed_valid) return fail("bounded SRV batch preserves null, foreign and isolated slots");
	ID3D11ShaderResourceView* clear_slot{};
	context->PSSetShaderResources(0, 1, &clear_slot);
	context->PSSetShaderResources(static_cast<UINT>(mixed.size() - 1), 1, &clear_slot);
	vr::engine_stereo_eye_resources::note_draw_indexed(context.Get(),
		vr::engine_stereo_eye_resources::exact_read_draw_indexed_caller);
	if (!vr::engine_stereo_eye_resources::end_eye(1, 0))
		return fail("left eye end");

	if (!vr::engine_stereo_eye_resources::begin_auxiliary(1,true))
		return fail("auxiliary view begin");
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> scope_alias;
	context->PSGetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,1,&scope_alias);
	const auto scope_resource=resource_of(scope_alias.Get());
	if (!scope_resource || scope_resource.Get()==target.Get() ||
		scope_resource.Get()==resource_of(left_alias.Get()).Get() ||
		scope_resource.Get()==reinterpret_cast<ID3D11Resource*>(prepared.targets[0].right_resource))
		return fail("scope owns a distinct inherited history resource");
	const float yellow[4]{1,1,0,1};
	context->ClearRenderTargetView(target_rtv.Get(),yellow);
	if (!vr::engine_stereo_eye_resources::end_auxiliary(1))
		return fail("auxiliary view restore");
	Microsoft::WRL::ComPtr<ID3D11Texture2D> scope_texture;
	std::uint32_t scope_pixel{};
	if (FAILED(scope_resource.As(&scope_texture)) ||
		!read_first_pixel(device.Get(),context.Get(),scope_texture.Get(),scope_pixel) || scope_pixel!=0xFF00FFFF)
		return fail("scope writes its own history");

	if (!vr::engine_stereo_eye_resources::begin_eye(1, 1))
		return fail("right eye begin");
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> bound_srv;
	context->PSGetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &bound_srv);
	if (resource_of(bound_srv.Get()).Get() != reinterpret_cast<ID3D11Resource*>(
		prepared.targets[0].right_resource))
	{
		return fail("inherited original-to-right rewrite");
	}
	const auto right_alias = bound_srv;
	// Warm all three positive aliases and the negative cache. Repeated foreign
	// binds must bypass isolation; either eye alias must select the active eye.
	for (auto* input : std::array<ID3D11ShaderResourceView*, 7>{target_srv.Get(),
		left_alias.Get(), right_alias.Get(), auxiliary_srv.Get(), auxiliary_srv.Get(),
		left_alias.Get(), target_srv.Get()})
	{
		context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot, 1, &input);
		bound_srv.Reset();
		context->PSGetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot, 1, &bound_srv);
		auto* expected = input == auxiliary_srv.Get() ? auxiliary.Get() :
			reinterpret_cast<ID3D11Texture2D*>(prepared.targets[0].right_resource);
		if (resource_of(bound_srv.Get()).Get() != expected)
			return fail("indexed original/left/right/foreign SRV routing");
	}
	vr::engine_stereo_eye_resources::note_draw_indexed(context.Get(),
		vr::engine_stereo_eye_resources::exact_read_draw_indexed_caller);

	ID3D11ShaderResourceView* null_srv{};
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &null_srv);
	ID3D11RenderTargetView* render_target = target_rtv.Get();
	context->OMSetRenderTargets(1, &render_target, nullptr);
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> bound_rtv;
	context->OMGetRenderTargets(1, &bound_rtv, nullptr);
	if (resource_of(bound_rtv.Get()).Get() != reinterpret_cast<ID3D11Resource*>(
		prepared.targets[0].right_resource))
	{
		return fail("explicit render-target rewrite");
	}
	context->OMSetRenderTargets(0, nullptr, nullptr);

	ID3D11UnorderedAccessView* unordered = target_uav.Get();
	context->CSSetUnorderedAccessViews(0, 1, &unordered, nullptr);
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> bound_uav;
	context->CSGetUnorderedAccessViews(0, 1, &bound_uav);
	if (resource_of(bound_uav.Get()).Get() != reinterpret_cast<ID3D11Resource*>(
		prepared.targets[0].right_resource))
	{
		return fail("explicit unordered-access rewrite");
	}
	ID3D11UnorderedAccessView* null_uav{};
	context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

	context->CopyResource(target.Get(), auxiliary.Get());
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &shader_view);
	bound_srv.Reset();
	context->PSGetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &bound_srv);
	if (resource_of(bound_srv.Get()).Get() != reinterpret_cast<ID3D11Resource*>(
		prepared.targets[0].right_resource))
	{
		return fail("explicit shader-resource rewrite");
	}
	vr::engine_stereo_eye_resources::note_draw_indexed(context.Get(),
		vr::engine_stereo_eye_resources::exact_read_draw_indexed_caller);
	ID3D11ShaderResourceView* foreign_srv = auxiliary_srv.Get();
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &foreign_srv);
	vr::engine_stereo_eye_resources::note_draw_indexed(context.Get(),
		vr::engine_stereo_eye_resources::exact_read_draw_indexed_caller);
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &null_srv);
	vr::engine_stereo_eye_resources::note_draw_indexed(context.Get(),
		vr::engine_stereo_eye_resources::exact_read_draw_indexed_caller);
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &shader_view);
	vr::engine_stereo_eye_resources::note_draw_indexed(context.Get(),
		vr::engine_stereo_eye_resources::exact_read_draw_indexed_caller);

	if (!vr::engine_stereo_eye_resources::end_eye(1, 1))
		return fail("right eye end/restore");
	bound_srv.Reset();
	context->PSGetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &bound_srv);
	if (resource_of(bound_srv.Get()).Get() != target.Get())
		return fail("right-to-original end boundary restore");
	if (!vr::engine_stereo_eye_resources::end_pair(1))
		return fail("first pair end");

	std::uint32_t original_pixel{};
	std::uint32_t left_pixel{};
	std::uint32_t right_pixel{};
	if (!read_first_pixel(device.Get(), context.Get(), target.Get(), original_pixel) ||
		!read_first_pixel(device.Get(), context.Get(),
			reinterpret_cast<ID3D11Texture2D*>(prepared.targets[0].left_resource), left_pixel) ||
		!read_first_pixel(device.Get(), context.Get(),
			reinterpret_cast<ID3D11Texture2D*>(
				prepared.targets[0].right_resource),
			right_pixel) || original_pixel != 0xFF000000u || left_pixel != 0xFF00FF00u ||
		right_pixel != 0xFFFF0000u)
	{
		return fail("persistent per-eye history content");
	}
	const auto first = vr::engine_stereo_eye_resources::get_status();
	if (first.pair_completions != 1 || first.pair_failures != 0 ||
		first.boundary_rebind_attempts != 3 ||
		first.boundary_rebind_completions != 3 ||
		first.boundary_original_views_remaining != 0 ||
		first.restore_rebind_attempts != 3 ||
		first.restore_rebind_completions != 3 ||
		first.restore_rebind_failures != 0 ||
		first.restore_isolated_views_remaining != 0 ||
		first.render_target_replacements == 0 ||
		first.shader_resource_replacements[0] == 0 ||
		first.unordered_access_replacements == 0 ||
		first.resource_replacements == 0 ||
		first.exact_read_bindings[0][static_cast<std::size_t>(
			vr::engine_stereo_eye_resources::exact_read_binding::left)] == 0 ||
		first.exact_read_bindings[1][static_cast<std::size_t>(
			vr::engine_stereo_eye_resources::exact_read_binding::right)] == 0 ||
		first.exact_read_bindings[1][static_cast<std::size_t>(
			vr::engine_stereo_eye_resources::exact_read_binding::foreign)] != 1 ||
		first.exact_read_bindings[1][static_cast<std::size_t>(
			vr::engine_stereo_eye_resources::exact_read_binding::null_view)] != 1)
	{
		return fail("rewrite/restore/exact-read counters");
	}

	context->ClearRenderTargetView(target_rtv.Get(), red);
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> alternative_owner;
	if (FAILED(device->CreateRenderTargetView(target.Get(), nullptr, &alternative_owner)))
		return fail("alternative RTV for unchanged history texture");
	auto alternative_bindings = bindings;
	alternative_bindings[0].owner_view = alternative_owner.Get();
	if (!vr::engine_stereo_eye_resources::begin_pair(2, device.Get(), context.Get(),
		1, alternative_bindings)) return fail("second pair begin");
	const auto persistent = vr::engine_stereo_eye_resources::get_status();
	if (persistent.resource_rebuilds != 1 || persistent.seed_copies != 2 ||
		persistent.targets[0].left_resource != prepared.targets[0].left_resource ||
		persistent.targets[0].right_resource != prepared.targets[0].right_resource ||
		!read_first_pixel(device.Get(), context.Get(),
			reinterpret_cast<ID3D11Texture2D*>(prepared.targets[0].left_resource),
			left_pixel) || left_pixel != 0xFF00FF00u ||
		!read_first_pixel(device.Get(), context.Get(),
			reinterpret_cast<ID3D11Texture2D*>(
				prepared.targets[0].right_resource),
			right_pixel) || right_pixel != 0xFFFF0000u)
	{
		return fail("natural H2 write must not contaminate either eye history");
	}
	if (!vr::engine_stereo_eye_resources::begin_eye(2, 0) ||
		!vr::engine_stereo_eye_resources::end_eye(2, 0) ||
		!vr::engine_stereo_eye_resources::begin_auxiliary(2,false))
	{
		return fail("second pair eye begin");
	}
	if (!read_first_pixel(device.Get(),context.Get(),scope_texture.Get(),scope_pixel) || scope_pixel!=0xFF00FFFF ||
		!vr::engine_stereo_eye_resources::end_auxiliary(2) ||
		!vr::engine_stereo_eye_resources::begin_eye(2,1))
		return fail("scope history survives native and main-eye writes");
	render_target = target_rtv.Get();
	context->OMSetRenderTargets(1, &render_target, nullptr);
	if (!vr::engine_stereo_eye_resources::end_eye(2, 1))
		return fail("second right eye end");
	bound_rtv.Reset();
	context->OMGetRenderTargets(1, &bound_rtv, nullptr);
	if (resource_of(bound_rtv.Get()).Get() != target.Get() ||
		!vr::engine_stereo_eye_resources::end_pair(2))
	{
		return fail("second pair inverse boundary");
	}
	const auto second = vr::engine_stereo_eye_resources::get_status();
	if (second.resource_rebuilds != 1 || second.seed_copies != 2 ||
		second.restore_rebind_attempts != 6 ||
		second.restore_rebind_completions != 6 ||
		second.restore_isolated_views_remaining != 0)
	{
		return fail("persistent history/restore counters");
	}

	// Regression for H2's actual admission shape: natural owners continue
	// writing scene mips between accepted stereo pairs. Neither persistent eye
	// may inherit those writes, including the left eye's very first sample.
	for (std::uint64_t cycle{}; cycle < 32; ++cycle)
	{
		context->OMSetRenderTargets(0, nullptr, nullptr);
		context->ClearRenderTargetView(target_rtv.Get(), cycle % 2 ? red : black);
		const auto pair_id = 100 + cycle;
		if (!vr::engine_stereo_eye_resources::begin_pair(pair_id, device.Get(),
			context.Get(), 1, bindings)) return fail("interleaved pair begin");
		for (std::uint32_t eye{}; eye < 2; ++eye)
		{
			if (!vr::engine_stereo_eye_resources::begin_eye(pair_id, eye))
				return fail("interleaved eye begin");
			context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
				1, &shader_view);
			bound_srv.Reset();
			context->PSGetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
				1, &bound_srv);
			const auto expected = eye == 0 ? prepared.targets[0].left_resource :
				prepared.targets[0].right_resource;
			std::uint32_t pixel{};
			if (resource_of(bound_srv.Get()).Get() != reinterpret_cast<ID3D11Resource*>(expected) ||
				!read_first_pixel(device.Get(), context.Get(),
					reinterpret_cast<ID3D11Texture2D*>(expected), pixel) ||
				pixel != (eye == 0 ? 0xFF00FF00u : 0xFFFF0000u))
				return fail("first SSR read inherited natural/other-eye history");
			if (!vr::engine_stereo_eye_resources::end_eye(pair_id, eye))
				return fail("interleaved eye restore");
		}
		if (!vr::engine_stereo_eye_resources::end_pair(pair_id))
			return fail("interleaved pair end");
	}

	if (!vr::engine_stereo_eye_resources::begin_pair(500, device.Get(), context.Get(),
		1, bindings) || !vr::engine_stereo_eye_resources::begin_eye(500, 0))
		return fail("left cancellation setup");
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &shader_view);
	vr::engine_stereo_eye_resources::cancel_pair(500);
	bound_srv.Reset();
	context->PSGetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &bound_srv);
	if (resource_of(bound_srv.Get()).Get() != target.Get() ||
		vr::engine_stereo_eye_resources::get_status().pair_active)
		return fail("left cancellation must restore natural bindings");

	if (!vr::engine_stereo_eye_resources::begin_pair(3, device.Get(), context.Get(),
		1, bindings) || !vr::engine_stereo_eye_resources::begin_eye(3, 0) ||
		!vr::engine_stereo_eye_resources::end_eye(3, 0) ||
		!vr::engine_stereo_eye_resources::begin_eye(3, 1))
	{
		return fail("cancel pair setup");
	}
	context->OMSetRenderTargets(0, nullptr, nullptr);
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &shader_view);
	vr::engine_stereo_eye_resources::cancel_pair(3);
	bound_srv.Reset();
	context->PSGetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &bound_srv);
	if (resource_of(bound_srv.Get()).Get() != target.Get())
		return fail("cancel must restore right bindings");

	// Negative control: bind a fresh view of the isolated right resource.  It is
	// intentionally absent from the original->right mapping cache, so inverse
	// cleanup must detect the residual rather than releasing its backing ComPtrs.
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> untracked_right_srv;
	if (FAILED(device->CreateShaderResourceView(
		reinterpret_cast<ID3D11Texture2D*>(
			prepared.targets[0].right_resource),
		nullptr, &untracked_right_srv)))
	{
		return fail("untracked right SRV creation");
	}
	if (!vr::engine_stereo_eye_resources::begin_pair(4, device.Get(), context.Get(),
		1, bindings) || !vr::engine_stereo_eye_resources::begin_eye(4, 0) ||
		!vr::engine_stereo_eye_resources::end_eye(4, 0) ||
		!vr::engine_stereo_eye_resources::begin_eye(4, 1))
	{
		return fail("quarantine pair setup");
	}
	ID3D11ShaderResourceView* residual_right = untracked_right_srv.Get();
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &residual_right);
	if (vr::engine_stereo_eye_resources::end_eye(4, 1))
		return fail("residual right view must fail cleanup");

	const auto quarantined = vr::engine_stereo_eye_resources::get_status();
	if (!quarantined.cleanup_quarantined || !quarantined.pair_active ||
		!quarantined.resources_ready || quarantined.quarantine_pair != 4 ||
		quarantined.quarantine_context != reinterpret_cast<std::uintptr_t>(
			context.Get()) || quarantined.quarantine_generation != 1 ||
		quarantined.quarantine_owner_thread != GetCurrentThreadId() ||
		quarantined.quarantine_events != 1 ||
		quarantined.restore_isolated_views_remaining == 0)
	{
		return fail("cleanup quarantine publication");
	}
	if (vr::engine_stereo_eye_resources::begin_pair(5, device.Get(), context.Get(),
		1, bindings))
	{
		return fail("quarantined generation admitted a new pair");
	}

	// A foreign invalidation is allowed to mark the generation unusable, but it
	// must not retire the owner pair or release its cache/ComPtr graph.
	std::thread foreign_invalidator([&]
	{
		vr::engine_stereo_eye_resources::invalidate_device(context.Get(), 1);
	});
	foreign_invalidator.join();
	const auto deferred = vr::engine_stereo_eye_resources::get_status();
	if (!deferred.invalidation_pending || deferred.deferred_invalidations != 1 ||
		!deferred.pair_active || !deferred.resources_ready ||
		deferred.targets[0].right_resource != prepared.targets[0].right_resource)
	{
		return fail("foreign invalidation must retain quarantined resources");
	}

	// The owner can replace the residual with the natural H2 view and retry the
	// inverse cleanup.  Recovery retires the pair, but the exact generation stays
	// hard-quarantined until an exact owner-thread invalidation resets it.
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &shader_view);
	vr::engine_stereo_eye_resources::cancel_pair(4);
	bound_srv.Reset();
	context->PSGetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &bound_srv);
	const auto recovered = vr::engine_stereo_eye_resources::get_status();
	if (resource_of(bound_srv.Get()).Get() != target.Get() || recovered.pair_active ||
		!recovered.cleanup_quarantined || !recovered.resources_ready ||
		recovered.cleanup_retries != 1 || recovered.cleanup_recoveries != 1 ||
		recovered.restore_isolated_views_remaining !=
			quarantined.restore_isolated_views_remaining)
	{
		return fail("owner cleanup recovery contract");
	}
	if (vr::engine_stereo_eye_resources::begin_pair(6, device.Get(), context.Get(),
		1, bindings) || vr::engine_stereo_eye_resources::get_status().
			quarantine_rejections < 2)
	{
		return fail("recovered generation must remain quarantined");
	}

	vr::engine_stereo_eye_resources::invalidate_device(context.Get(), 1);
	const auto invalidated = vr::engine_stereo_eye_resources::get_status();
	if (invalidated.expected_context != 0 || invalidated.device_generation != 0 ||
		invalidated.resources_ready || invalidated.cleanup_quarantined ||
		invalidated.invalidation_pending)
	{
		return fail("device invalidation contract");
	}
	if (!vr::engine_stereo_eye_resources::install(context.Get(), 1) ||
		!vr::engine_stereo_eye_resources::begin_pair(7, device.Get(), context.Get(),
			1, bindings))
	{
		return fail("exact invalidation must reset admission and TLS identity");
	}
	vr::engine_stereo_eye_resources::cancel_pair(7);
	vr::engine_stereo_eye_resources::invalidate_device(context.Get(), 1);

	// Changed-generation install may run on another renderer thread. It must not
	// reclaim a borrowed right view while the old owner pair is active. The old
	// owner publishes the sole quiescence boundary; only the retry after that
	// boundary may release the old cache and admit generation 2.
	if (!vr::engine_stereo_output_merger::install(context.Get(), 1) ||
		!vr::engine_stereo_resource_ops::install(context.Get(), 1) ||
		!vr::engine_stereo_eye_resources::install(context.Get(), 1) ||
		!vr::engine_stereo_eye_resources::begin_pair(8, device.Get(), context.Get(),
			1, bindings) || !vr::engine_stereo_eye_resources::begin_eye(8, 0) ||
		!vr::engine_stereo_eye_resources::end_eye(8, 0) ||
		!vr::engine_stereo_eye_resources::begin_eye(8, 1))
	{
		return fail("changed-generation quiescence setup");
	}
	context->PSSetShaderResources(vr::engine_stereo_eye_resources::exact_read_ps_slot,
		1, &shader_view);
	bool foreign_output_merger_install{};
	bool foreign_resource_ops_install{};
	bool foreign_eye_install{};
	std::thread changed_generation_installer([&]
	{
		foreign_output_merger_install =
			vr::engine_stereo_output_merger::install(context.Get(), 2);
		foreign_resource_ops_install =
			vr::engine_stereo_resource_ops::install(context.Get(), 2);
		foreign_eye_install =
			vr::engine_stereo_eye_resources::install(context.Get(), 2);
	});
	changed_generation_installer.join();
	const auto generation_deferred = vr::engine_stereo_eye_resources::get_status();
	if (!foreign_output_merger_install || !foreign_resource_ops_install ||
		foreign_eye_install || !generation_deferred.pair_active ||
		!generation_deferred.cleanup_quarantined ||
		!generation_deferred.invalidation_pending ||
		!generation_deferred.resources_ready ||
		generation_deferred.targets[0].right_resource == 0)
	{
		return fail("active generation release must be deferred");
	}
	vr::engine_stereo_eye_resources::cancel_pair(8);
	if (vr::engine_stereo_eye_resources::get_status().pair_active ||
		!vr::engine_stereo_eye_resources::install(context.Get(), 2))
	{
		return fail("quiescent generation retry");
	}
	const auto generation_replaced = vr::engine_stereo_eye_resources::get_status();
	if (generation_replaced.expected_context !=
			reinterpret_cast<std::uintptr_t>(context.Get()) ||
		generation_replaced.device_generation != 2 ||
		generation_replaced.resources_ready ||
		generation_replaced.cleanup_quarantined ||
		generation_replaced.invalidation_pending)
	{
		return fail("changed generation release contract");
	}
	vr::engine_stereo_eye_resources::invalidate_device(context.Get(), 2);

	// A moving optical near plane can reset every pair. Cache clear RTVs while
	// still clearing every mip, and discard that cache at device invalidation.
	context->ClearState();
	auto mip_description = description;
	mip_description.MipLevels = 3;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> mip_target;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> mip_owner;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mip_source;
	if (FAILED(device->CreateTexture2D(&mip_description, nullptr, &mip_target)) ||
		FAILED(device->CreateRenderTargetView(mip_target.Get(), nullptr, &mip_owner)) ||
		FAILED(device->CreateShaderResourceView(mip_target.Get(), nullptr, &mip_source)))
		return fail("multi-mip auxiliary history setup");
	auto mip_bindings = bindings;
	mip_bindings[0].owner_view = mip_owner.Get();
	Microsoft::WRL::ComPtr<ID3D11Texture2D> previous_scope;
	for (std::uint64_t generation = 3; generation <= 4; ++generation)
	{
		if (!vr::engine_stereo_output_merger::install(context.Get(), generation) ||
			!vr::engine_stereo_resource_ops::install(context.Get(), generation) ||
			!vr::engine_stereo_eye_resources::install(context.Get(), generation))
			return fail("auxiliary cache generation installation");
		Microsoft::WRL::ComPtr<ID3D11Texture2D> history;
		std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, 3> history_views;
		std::uint64_t warmed_view_creations{};
		for (std::uint64_t iteration = 0; iteration < 2; ++iteration)
		{
			const auto pair_id = generation * 10 + iteration;
			if (!vr::engine_stereo_eye_resources::begin_pair(pair_id, device.Get(), context.Get(),
				generation, mip_bindings) || !vr::engine_stereo_eye_resources::begin_eye(pair_id, 0) ||
				!vr::engine_stereo_eye_resources::end_eye(pair_id, 0) ||
				!vr::engine_stereo_eye_resources::begin_auxiliary(pair_id, true))
				return fail("repeated auxiliary history reset admission");
			auto* source = mip_source.Get();
			context->PSSetShaderResources(13, 1, &source);
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> selected;
			context->PSGetShaderResources(13, 1, &selected);
			Microsoft::WRL::ComPtr<ID3D11Texture2D> selected_texture;
			if (!selected || FAILED(resource_of(selected.Get()).As(&selected_texture)) ||
				selected_texture == mip_target || selected_texture == previous_scope)
				return fail("auxiliary clear cache retained a stale resource generation");
			if (iteration == 0)
			{
				history = selected_texture;
				for (UINT mip = 0; mip < history_views.size(); ++mip)
				{
					D3D11_RENDER_TARGET_VIEW_DESC view_description{};
					view_description.Format = mip_description.Format;
					view_description.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
					view_description.Texture2D.MipSlice = mip;
					if (FAILED(device->CreateRenderTargetView(history.Get(), &view_description,
						history_views[mip].GetAddressOf())))
						return fail("auxiliary mip test view creation");
				}
				warmed_view_creations = vr::engine_stereo_eye_resources::get_status().view_creations;
			}
			else if (selected_texture != history ||
				vr::engine_stereo_eye_resources::get_status().view_creations != warmed_view_creations)
				return fail("consecutive resets recreated auxiliary textures or clear views");
			for (UINT mip = 0; mip < history_views.size(); ++mip)
			{
				std::uint32_t pixel{};
				if (!read_first_pixel(device.Get(), context.Get(), history.Get(), pixel, mip) || pixel != 0)
					return fail("cached history reset failed to clear an auxiliary mip");
				context->ClearRenderTargetView(history_views[mip].Get(), blue);
			}
			if (!vr::engine_stereo_eye_resources::end_auxiliary(pair_id) ||
				!vr::engine_stereo_eye_resources::begin_eye(pair_id, 1) ||
				!vr::engine_stereo_eye_resources::end_eye(pair_id, 1) ||
				!vr::engine_stereo_eye_resources::end_pair(pair_id))
				return fail("repeated auxiliary history reset retirement");
		}
		context->ClearState();
		vr::engine_stereo_eye_resources::invalidate_device(context.Get(), generation);
		if (vr::engine_stereo_eye_resources::get_status().resources_ready)
			return fail("auxiliary cache survived exact generation invalidation");
		previous_scope = history;
	}
	// AA's six optional ring images use the same routing and restoration as
	// SSR. Verify persistence for both eyes and the auxiliary view, without
	// touching the natural desktop histories or allocating them when disabled.
	context->ClearState();
	if (!vr::engine_stereo_output_merger::install(context.Get(), 5) ||
		!vr::engine_stereo_resource_ops::install(context.Get(), 5) ||
		!vr::engine_stereo_eye_resources::install(context.Get(), 5))
		return fail("AA history generation installation");
	auto aa_bindings = bindings;
	std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 6> aa_originals;
	std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, 6> aa_views;
	for (std::size_t i = 0; i < aa_originals.size(); ++i)
	{
		if (FAILED(device->CreateTexture2D(&description, nullptr, &aa_originals[i])) ||
			FAILED(device->CreateRenderTargetView(aa_originals[i].Get(), nullptr, &aa_views[i])))
			return fail("AA history test resources");
		context->ClearRenderTargetView(aa_views[i].Get(), black);
		aa_bindings[i + 1] = {vr::native_post_aa::history_targets[i],
			vr::engine_stereo_eye_resources::role::color, aa_views[i].Get()};
	}
	const float aa_colors[][4]{{0.25f,0.5f,0.75f,0.125f}, {0.75f,0.25f,0.5f,0.5f}, {0.5f,0.75f,0.25f,0.75f}};
	const std::uint32_t pixels[]{0x20BF8040u, 0x808040BFu, 0xBF40BF80u};
	// Actual native LDR input: typeless texture, UNORM RTV/SRV. History
	// remains typed UNORM, so initialization must preserve RGBA across the
	// compatible storage-family copy rather than reject the input descriptor.
	auto input_desc = description;
	input_desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
	D3D11_RENDER_TARGET_VIEW_DESC input_rtv_desc{};
	input_rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	input_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	D3D11_SHADER_RESOURCE_VIEW_DESC input_srv_desc{};
	input_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	input_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	input_srv_desc.Texture2D.MipLevels = 1;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> aa_input;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> aa_input_view;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> aa_input_source;
	if (FAILED(device->CreateTexture2D(&input_desc, nullptr, &aa_input)) ||
		FAILED(device->CreateRenderTargetView(aa_input.Get(), &input_rtv_desc, &aa_input_view)) ||
		FAILED(device->CreateShaderResourceView(aa_input.Get(), &input_srv_desc, &aa_input_source)))
		return fail("native typeless LDR fixture");
	if (!vr::native_post_aa::accepts_ldr(input_desc, input_rtv_desc, input_srv_desc, 16, 16) ||
		!vr::native_post_aa::accepts_ldr(description, input_rtv_desc, input_srv_desc, 16, 16))
		return fail("native typed/typeless LDR descriptor admission");
	auto srgb = input_srv_desc;
	srgb.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	if (vr::native_post_aa::accepts_ldr(input_desc, input_rtv_desc, srgb, 16, 16) ||
		vr::native_post_aa::accepts_ldr(input_desc, input_rtv_desc, input_srv_desc, 8, 16))
		return fail("incompatible LDR encoding/extent accepted");
	for (std::uint64_t pair_id : {100u, 101u})
	{
		if (!vr::engine_stereo_eye_resources::begin_pair(pair_id, device.Get(), context.Get(), 5, aa_bindings))
			return fail("AA history pair admission");
		for (const auto eye : {0u, 2u, 1u})
		{
			if (!(eye == 2 ? vr::engine_stereo_eye_resources::begin_auxiliary(pair_id, pair_id == 100) :
				vr::engine_stereo_eye_resources::begin_eye(pair_id, eye)))
				return fail("AA history view admission");
			for (std::size_t i = 0; i < aa_originals.size(); ++i)
			{
				auto* isolated = vr::engine_stereo_eye_resources::isolated_color_view(pair_id, eye,
					vr::native_post_aa::history_targets[i]);
				Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
				if (!isolated || FAILED(resource_of(isolated).As(&texture)) || texture == aa_originals[i])
					return fail("AA history was not isolated");
				std::uint32_t pixel{};
				if (pair_id == 101 && (!read_first_pixel(device.Get(), context.Get(), texture.Get(), pixel) || pixel != pixels[eye]))
					return fail("AA history borrowed another eye or lost the previous pair");
				context->ClearRenderTargetView(aa_input_view.Get(), aa_colors[eye]);
				context->CopyResource(texture.Get(), aa_input.Get());
			}
			if (!(eye == 2 ? vr::engine_stereo_eye_resources::end_auxiliary(pair_id) :
				vr::engine_stereo_eye_resources::end_eye(pair_id, eye)))
				return fail("AA history view restoration");
		}
		if (!vr::engine_stereo_eye_resources::end_pair(pair_id)) return fail("AA history pair retirement");
	}
	for (const auto& texture : aa_originals)
	{
		std::uint32_t pixel{};
		if (!read_first_pixel(device.Get(), context.Get(), texture.Get(), pixel) || pixel != 0xFF000000u)
			return fail("AA replay changed the natural desktop history");
	}
	const auto ssr_identity = vr::engine_stereo_eye_resources::get_status().targets[0].left_resource;
	if (!vr::engine_stereo_eye_resources::begin_pair(102, device.Get(), context.Get(), 5, bindings))
		return fail("AA disable resource transition");
	const auto disabled = vr::engine_stereo_eye_resources::get_status();
	if (disabled.targets[0].left_resource != ssr_identity)
		return fail("AA mode change discarded unrelated SSR history");
	for (std::size_t i = 1; i < disabled.targets.size(); ++i)
		if (disabled.targets[i].left_resource || disabled.targets[i].right_resource)
			return fail("disabled AA retained unused history allocations");
	vr::engine_stereo_eye_resources::cancel_pair(102);
	vr::engine_stereo_eye_resources::invalidate_device(context.Get(), 5);
	if (!native_post_aa_depth_tests(device.Get(),context.Get()))
		return fail("native SMAA depth scratch escaped into subsequent VR optics");
	std::cout << "vr-d3d11-eye-resource-isolation-probe: PASS\n";
	return 0;
}
