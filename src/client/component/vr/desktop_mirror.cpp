#include <std_include.hpp>
#include "component/vr/native_render_contract.hpp"
#include "desktop_mirror.hpp"
#include "desktop_mirror_layout.hpp"
#include "recording_frame.hpp"
#include "stabilization.hpp"
#include "native_render_session.hpp"
#include "engine_backend_probe.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include <d3dcompiler.h>
#include <atomic>

namespace vr::desktop_mirror
{
	namespace
	{
		std::atomic<state> last_state{state::idle};
		std::atomic_uint64_t draws{}, last_pair{};
		std::atomic_int32_t last_result{};
		std::atomic<float> requested_fov{}, effective_fov{};
		std::atomic_bool fov_limited{};
		std::atomic<float> smoothing_strength{},correction_fraction{1};
		thread_local pose_filter::filter camera_filter;
		thread_local std::uint64_t camera_generation{};
		thread_local std::array<float,3> camera_viewport{};
		struct source_view
		{
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		};
		struct mirror_resources
		{
			std::uint64_t generation{};
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> stabilized_shader;
			Microsoft::WRL::ComPtr<ID3D11Buffer> constants,previous_constants;
			bool stabilized{};
			std::array<source_view, 2> sources;
			std::size_t next_source{};
			HRESULT error{S_OK};
		};
		// ImGui executes these draw callbacks synchronously on this same owner
		// before returning to H2. No SRV in the draw list can be evicted mid-draw.
		thread_local mirror_resources resources;

		void bind_linear_eye_shader(const ImDrawList*, const ImDrawCmd*)
		{
			resources.context->PSSetShader(resources.stabilized?resources.stabilized_shader.Get():resources.shader.Get(), nullptr, 0);
			if(resources.stabilized)
			{
				resources.context->PSGetConstantBuffers(0,1,&resources.previous_constants);
				auto* buffer=resources.constants.Get();resources.context->PSSetConstantBuffers(0,1,&buffer);
			}
		}
		void restore_constants(const ImDrawList*, const ImDrawCmd*)
		{
			if(resources.stabilized){auto* previous=resources.previous_constants.Get();resources.context->PSSetConstantBuffers(0,1,&previous);resources.previous_constants.Reset();}
		}
		bool prepare_stabilization(ID3D11ShaderResourceView* view,const stabilized_view& transform)
		{
			resources.stabilized=false;
			if(!transform.valid)return true;
			Microsoft::WRL::ComPtr<ID3D11Device> device;view->GetDevice(&device);
			if(!resources.stabilized_shader)
			{
				Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
				const D3D_SHADER_MACRO macros[]{{"STABILIZED","1"},{nullptr,nullptr}};
				resources.error=D3DCompile(pixel_shader,sizeof(pixel_shader)-1,"h2v-stabilized-mirror",macros,nullptr,"main","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
				if(SUCCEEDED(resources.error))resources.error=device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&resources.stabilized_shader);
				if(FAILED(resources.error))return false;
				D3D11_BUFFER_DESC desc{};desc.ByteWidth=48;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
				resources.error=device->CreateBuffer(&desc,nullptr,&resources.constants);
				if(FAILED(resources.error))return false;
			}
			std::array<std::array<float,4>,3> rows{};
			for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)rows[i][j]=transform.uv_transform[i][j];
			resources.context->UpdateSubresource(resources.constants.Get(),0,nullptr,rows.data(),0,0);
			resources.stabilized=true;return true;
		}

		ID3D11ShaderResourceView* prepare(const native_render_session::desktop_eye& eye)
		{
			if (resources.generation != eye.generation)
			{
				resources = {};
				resources.generation = eye.generation;
			}
			if (FAILED(resources.error)) return nullptr;
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			eye.texture->GetDevice(&device);
			if (!resources.shader)
			{
				Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
				resources.error = D3DCompile(pixel_shader, sizeof(pixel_shader) - 1,
					"h2v-desktop-right-eye", nullptr, nullptr, "main", "ps_4_0",
					D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
				if (FAILED(resources.error) && errors) console::error("[VR] desktop mirror shader: %s\n",
					static_cast<const char*>(errors->GetBufferPointer()));
				if (SUCCEEDED(resources.error)) resources.error = device->CreatePixelShader(
					code->GetBufferPointer(), code->GetBufferSize(), nullptr, &resources.shader);
				if (FAILED(resources.error)) return nullptr;
				device->GetImmediateContext(&resources.context);
			}
			for (const auto& source : resources.sources)
				if (source.texture.Get() == eye.texture.Get()) return source.view.Get();
			auto& source = resources.sources[resources.next_source++ % resources.sources.size()];
			source = {};
			source.texture = eye.texture;
			resources.error = device->CreateShaderResourceView(source.texture.Get(), nullptr, &source.view);
			return source.view.Get();
		}
	}

	void draw(const float horizontal_fov)
	{
		const auto* viewport = ImGui::GetMainViewport();
		bool mirror_ready{};
		const auto publish = gsl::finally([&] {
			recording_frame::publish_viewport(mirror_ready ? viewport->Size.x : 0,
				mirror_ready ? viewport->Size.y : 0,horizontal_fov);
		});
		// Native menus/console are still needed on the desktop. They are not a
		// third scene render and must remain usable while the HMD is unavailable.
		if (!game::CL_IsCgameInitialized() || *game::keyCatchers != 0)
		{
			camera_filter.reset();
			last_state.store(state::ui, std::memory_order_relaxed);
			return;
		}
		static const game::dvar_t* paused{};
		if (!paused) paused = game::Dvar_FindVar("cl_paused");
		if (!paused || paused->current.integer != 0)
		{
			camera_filter.reset();
			last_state.store(state::ui, std::memory_order_relaxed);
			return;
		}
		const auto eye = native_render_session::active().right_eye_for_desktop();
		if (!eye.texture)
		{
			camera_filter.reset();
			last_state.store(state::waiting_eye, std::memory_order_relaxed);
			return;
		}

		const auto crop = eye.recording_crop ? eye.recording_crop : project(eye.projection, eye.width, eye.height,
			viewport->Size.x, viewport->Size.y, horizontal_fov);
		requested_fov.store(horizontal_fov, std::memory_order_relaxed);
		effective_fov.store(crop.horizontal_fov, std::memory_order_relaxed);
		fov_limited.store(crop.limited, std::memory_order_relaxed);
		if (!crop)
		{
			camera_filter.reset();
			last_state.store(state::invalid_view, std::memory_order_relaxed);
			return;
		}
		const float amount=stabilization::settings().desktop.amount();
		smoothing_strength.store(amount,std::memory_order_relaxed);
		stabilized_view stabilized;
		const std::array<float,3> current_viewport{viewport->Size.x,viewport->Size.y,horizontal_fov};
		if(camera_generation!=eye.generation || camera_viewport!=current_viewport)
		{camera_filter.reset();camera_generation=eye.generation;camera_viewport=current_viewport;}
		if(std::isfinite(amount) && amount>0)
		{
			if(!eye.camera.valid){camera_filter.reset();last_state.store(state::invalid_view);return;}
			const auto filtered=camera_filter.update({{},pose_filter::transpose(eye.camera.axes)},eye.pair_id,
				eye.camera.epoch,eye.camera.at,amount,pose_filter::desktop);
			stabilized=stabilize(eye.projection,crop,eye.width,eye.height,eye.camera.axes,
				pose_filter::transpose(filtered.orientation),eye.recording_crop);
			if(!stabilized.valid){camera_filter.reset();last_state.store(state::invalid_view);return;}
			effective_fov.store(stabilized.horizontal_fov,std::memory_order_relaxed);
			correction_fraction.store(stabilized.correction_fraction,std::memory_order_relaxed);
		}
		else {camera_filter.reset();correction_fraction.store(1,std::memory_order_relaxed);}
		const auto previous_error = resources.error;
		auto* const view = prepare(eye);
		if (!view || !prepare_stabilization(view,stabilized))
		{
			last_result.store(resources.error, std::memory_order_relaxed);
			last_state.store(state::failed, std::memory_order_relaxed);
			if (SUCCEEDED(previous_error)) console::error(
				"[VR] desktop right-eye mirror failed: HRESULT=0x%08X (HMD transport unchanged)\n",
				static_cast<unsigned>(resources.error));
			return;
		}
		// The existing GUI hook must still be drawing H2's desktop target 1.
		// Refuse other destinations (especially eye/offscreen targets), and drop
		// this temporary backbuffer reference immediately; never cache it across ResizeBuffers.
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
		resources.context->OMGetRenderTargets(1, &target, nullptr);
		ID3D11RenderTargetView* desktop_target{};
		std::memcpy(&desktop_target, reinterpret_cast<const void*>(
			native_render_contract::target_registry_base + native_render_contract::target_registry_stride + 8),
			sizeof(desktop_target));
		D3D11_RENDER_TARGET_VIEW_DESC description{};
		if (target) target->GetDesc(&description);
		if (!target || target.Get() != desktop_target || description.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			last_state.store(state::wrong_target, std::memory_order_relaxed);
			return;
		}

		auto* const draw_list = ImGui::GetBackgroundDrawList();
		const auto origin = viewport->Pos;
		draw_list->PushClipRect(origin,
			{origin.x + viewport->Size.x, origin.y + viewport->Size.y}, true);
		draw_list->AddCallback(bind_linear_eye_shader, nullptr);
		draw_list->AddImage(view, origin,
			{origin.x + viewport->Size.x, origin.y + viewport->Size.y},
			stabilized.valid?ImVec2{0,0}:ImVec2{crop.u0,crop.v0}, stabilized.valid?ImVec2{1,1}:ImVec2{crop.u1,crop.v1});
		draw_list->AddCallback(restore_constants,nullptr);
		draw_list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		draw_list->PopClipRect();
		last_pair.store(eye.pair_id, std::memory_order_relaxed);
		draws.fetch_add(1, std::memory_order_relaxed);
		last_result.store(S_OK, std::memory_order_relaxed);
		last_state.store(state::drawing, std::memory_order_relaxed);
		mirror_ready=true;
	}

	report get_report() noexcept
	{
		return {last_state.load(std::memory_order_relaxed), draws.load(std::memory_order_relaxed),
			last_pair.load(std::memory_order_relaxed), last_result.load(std::memory_order_relaxed),
			requested_fov.load(std::memory_order_relaxed), effective_fov.load(std::memory_order_relaxed),
			fov_limited.load(std::memory_order_relaxed),smoothing_strength.load(std::memory_order_relaxed),
			correction_fraction.load(std::memory_order_relaxed)};
	}
	const char* to_string(const state value) noexcept
	{
		switch (value)
		{
		case state::idle: return "idle";
		case state::ui: return "desktop_ui";
		case state::waiting_eye: return "waiting_right_eye";
		case state::invalid_view: return "invalid_projection_or_extent";
		case state::wrong_target: return "not_sdr_desktop_target";
		case state::failed: return "failed";
		case state::drawing: return "drawing";
		default: return "unknown";
		}
	}
}
