#include "std_include.hpp"
#include "component/vr/desktop_mirror_layout.hpp"
#include "component/vr/recording_frame_layout.hpp"
#include "component/vr/native_render_session.hpp"
#include <d3dcompiler.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	void require(const bool condition, const char* const message)
	{
		if (!condition) throw std::runtime_error(message);
	}
	void check(const HRESULT result, const char* const message) { require(SUCCEEDED(result), message); }

	void test_layout()
	{
		using vr::desktop_mirror::project;
		using projection = vr::engine_stereo_bridge::eye_projection;
		const projection asymmetric{-1.5f, 1.2f, -1.1f, .9f};
		const auto wide = project(asymmetric, 2528, 2704, 2560, 1440, 90);
		require(wide && !wide.limited && std::abs(wide.horizontal_fov - 90) < .001f, "requested FOV when it fits");
		require(std::abs((wide.u0 + wide.u1) * .5f - 1.5f / 2.7f) < .00001f &&
			std::abs((wide.v0 + wide.v1) * .5f - .45f) < .00001f, "center is optical forward, not UV 0.5");
		const auto half = project(asymmetric, 2528, 2704, 1280, 720, 90);
		require(wide.u0 == half.u0 && wide.v0 == half.v0 && wide.u1 == half.u1 && wide.v1 == half.v1,
			"DPI/logical-size changes must not change composition");
		const auto different_texture = project(asymmetric, 4096, 1024, 2560, 1440, 90);
		require(wide.u0 == different_texture.u0 && wide.v0 == different_texture.v0 &&
			wide.u1 == different_texture.u1 && wide.v1 == different_texture.v1,
			"texture pixel aspect must not distort optical geometry");
		for (const auto& bounds : {asymmetric, projection{-1.15f, .93f, -1, 1}, projection{-1, 1, -.7f, 1.4f}})
			for (const auto& size : {std::array<float, 2>{1920, 1080}, {900, 1600}, {3440, 1440}, {1000, 1000}})
				for (const float requested : {30.f, 90.f, 95.f, 120.f})
				{
					const auto crop = project(bounds, 2528, 2704, size[0], size[1], requested);
					require(crop && crop.u0 >= .5f / 2528 && crop.v0 >= .5f / 2704 &&
						crop.u1 <= 1.f - .5f / 2528 && crop.v1 <= 1.f - .5f / 2704, "bilinear footprint stays inside source");
					const auto left = bounds.tan_left + crop.u0 * (bounds.tan_right - bounds.tan_left);
					const auto right = bounds.tan_left + crop.u1 * (bounds.tan_right - bounds.tan_left);
					const auto up = bounds.tan_up - crop.v0 * (bounds.tan_up - bounds.tan_down);
					const auto down = bounds.tan_up - crop.v1 * (bounds.tan_up - bounds.tan_down);
					require(std::abs(left + right) < .00001f && std::abs(up + down) < .00001f,
						"four crop edges must describe a symmetric camera");
					require(std::abs((right - left) / (up - down) - size[0] / size[1]) < .00001f,
						"equal tangent displacements must have equal screen pixel scale");
					require(crop.horizontal_fov <= requested + .0001f, "bounds can narrow but never widen requested FOV");
				}
		const auto limited = project({-1.15f, .93f, -1, 1}, 2528, 2704, 1920, 1080, 95);
		require(limited.limited && limited.horizontal_fov > 85 && limited.horizontal_fov < 86,
			"95 degrees cannot fit a source whose shorter horizontal side ends at tangent .93");
		for (const auto invalid : {0.0f, -1.0f, (std::numeric_limits<float>::infinity)(),
			(std::numeric_limits<float>::quiet_NaN)()})
		{
			require(!project(asymmetric, 100, 100, invalid, 100), "invalid/minimized desktop width");
			require(!project(asymmetric, 100, 100, 100, invalid), "invalid/minimized desktop height");
			require(!project(asymmetric, 100, 100, 100, 100, invalid), "invalid FOV");
			require(!project({-1, invalid, -1, 1}, 100, 100, 100, 100), "invalid projection");
		}
		require(!project(asymmetric, 0, 100, 100, 100) && !project(asymmetric, 100, 1, 100, 100), "empty source footprint");
		require(!project(asymmetric, 100, 100, 100, 100, 180), "singular perspective FOV");
		require(!project({-.001f, 1, -1, 1}, 16, 16, 100, 100), "optical axis outside safe sample bounds");
		require(!project(asymmetric, 100, 100, 1e-35f, 1e35f), "unrepresentable UV extent must not reach draw list");
	}
	void test_recording_layout()
	{
		using namespace vr;
		const engine_stereo_bridge::eye_projection projection{-1.5f,1.2f,-1.1f,.9f};
		const engine_stereo_bridge::eye_projection other{-1.2f,1.5f,-.9f,1.1f};
		for (const auto projections : {std::array{projection,other},std::array{other,projection}})
		{
			const auto& selected=projections[0];
			const auto& paired=projections[1];
			const auto captured=desktop_mirror::project(selected,2528,2704,1920,1080,95);
			const auto matched=recording_frame::other_eye_crop(captured,selected,paired);
			require(matched && std::abs(matched.u0-captured.u0)>.01f,"either selected eye uses its own asymmetric UVs");
			for (const auto uv : {std::array{captured.u0,matched.u0},std::array{captured.u1,matched.u1}})
				require(std::abs(selected.tan_left+uv[0]*(selected.tan_right-selected.tan_left)-
					(paired.tan_left+uv[1]*(paired.tan_right-paired.tan_left)))<.00001f,"both eyes mark the same horizontal angles");
			for (const auto uv : {std::array{captured.v0,matched.v0},std::array{captured.v1,matched.v1}})
				require(std::abs(selected.tan_up-uv[0]*(selected.tan_up-selected.tan_down)-
					(paired.tan_up-uv[1]*(paired.tan_up-paired.tan_down)))<.00001f,"both eyes mark the same vertical angles");
			const auto clipped=recording_frame::other_eye_crop(captured,selected,{-.2f,.2f,-.2f,.2f});
			require(clipped && clipped.u0==0 && clipped.v0==0 && clipped.u1==1 && clipped.v1==1,
				"narrow other-eye frustum clips safely without shrinking the captured eye");
			require(!recording_frame::other_eye_crop({},selected,paired) &&
				!recording_frame::other_eye_crop(captured,selected,{-1,0,-1,1}) &&
				!recording_frame::other_eye_crop({0,0,1,(std::numeric_limits<float>::quiet_NaN)()},selected,paired),
				"invalid paired-eye metadata has no guide");
		}
		for (const unsigned size:{32u,1024u,2704u,8192u})
			for (const auto display:{std::array<float,2>{1920,1080},{1080,1920},{3440,1440}})
				for (float fov:{30.f,95.f,120.f})
				{
					const auto crop=desktop_mirror::project(projection,size,size,display[0],display[1],fov);
					const auto frame=recording_frame::make_layout(crop,size,size);
					require(frame.valid,"recording frame accepts production optical crop");
					for (float u:{crop.u0,(crop.u0+crop.u1)*.5f,crop.u1})
						for (float v:{crop.v0,(crop.v0+crop.v1)*.5f,crop.v1})
						{
							const auto x=std::floor(u*size-.5f),y=std::floor(v*size-.5f);
							for (int dx=0;dx<2;++dx) for (int dy=0;dy<2;++dy)
								require(x+dx+.5f>=frame.protected_pixels[0] && x+dx+.5f<=frame.protected_pixels[2] &&
									y+dy+.5f>=frame.protected_pixels[1] && y+dy+.5f<=frame.protected_pixels[3],
									"desktop bilinear footprint never touches guide pixels");
						}
					require(frame.line_pixels>=1 && frame.line_pixels<=8,"frame thickness scales with eye resolution");
					const auto label=recording_frame::caption_bounds(frame,size,size,640,32);
					if (label[2]>0)
						require(label[0]>=0 && label[1]>frame.protected_pixels[3]+frame.line_pixels*4 &&
							label[0]+label[2]<=size && label[1]+label[3]<=size &&
							std::abs(label[2]/label[3]-20)<.001f,"caption stays below border, fits eye and retains aspect");
				}
		require(!recording_frame::make_layout({},1024,1024).valid,"missing desktop crop has no guide");
		require(!recording_frame::make_layout({-.1f,0,1,1},1024,1024).valid,"out-of-image recording crop rejected");
		require(!recording_frame::make_layout({0,0,1,1},0,1024).valid,"empty recording texture rejected");
		const auto full=recording_frame::make_layout({0,0,1,1},1024,1024);
		require(recording_frame::caption_bounds(full,1024,1024,640,32)[2]==0,
			"no bottom margin must not put caption into captured image");
		const auto wide=recording_frame::make_layout({.2f,.2f,.8f,.7f},2704,2704);
		const auto caption=recording_frame::caption_bounds(wide,2704,2704,1024,48);
		require(caption[2]==1024 && caption[3]==48 &&
			std::abs(caption[0]+512-(wide.protected_pixels[0]+wide.protected_pixels[2])*.5f)<.01f,
			"ordinary landscape caption is visible and centered beneath frame");
	}
	void publish_test_recording_crop(const vr::eye_composition::event& event,ID3D11DeviceContext*,
		ID3D11ShaderResourceView*,ID3D11RenderTargetView*) noexcept
	{
		if (event.recording_crop) *event.recording_crop=event.eye==0 ?
			vr::desktop_mirror::crop{.1f,.2f,.7f,.6f,70,false} :
			vr::desktop_mirror::crop{.2f,.3f,.8f,.7f,80,false};
	}

	vr::engine_stereo_view::slot_pair make_views(const std::uint64_t id, const bool swap = false)
	{
		std::array<std::uint8_t, vr::engine_stereo_view::h2_view_slot_size> natural{};
		const float one = 1;
		for (const auto offset : {0x10C, 0x11C, 0x12C, 0x148})
			std::memcpy(natural.data() + offset, &one, sizeof(one));
		std::array<vr::engine_stereo_bridge::render_config, 2> configs{};
		for (unsigned eye = 0; eye < 2; ++eye)
		{
			auto& config = configs[eye];
			config.pair_id = config.frame_id = id;
			config.publication = id + 100;
			config.output_eye = eye; config.view_eye = swap ? 1 - eye : eye;
			config.swap_eyes = swap; config.half_eye_offset_units = 1.25f;
			config.eyes = {{{-.9f, 1.3f, -1.2f, .8f}, {-1.15f, .93f, -1.1f, 1.f}}};
		}
		vr::engine_stereo_view::slot_pair views;
		require(vr::engine_stereo_view::derive(natural.data(), configs, views), "derive production eye projections");
		return views;
	}

	void test_projection_metadata()
	{
		using vr::engine_stereo_view::read_projection;
		vr::engine_stereo_bridge::eye_projection projection;
		for (const bool swapped : {false, true})
		{
			auto views = make_views(10, swapped);
			require(read_projection(views.eyes[1], projection), "read exact derived eye slot");
			require(std::abs(projection.tan_left - (swapped ? -.9f : -1.15f)) < .00001f &&
				std::abs(projection.tan_up - (swapped ? .8f : 1.f)) < .00001f, "output eye uses its actual view mapping");
			const float shift = .2f;
			std::memcpy(views.eyes[1].bytes.data() + 0x40 + 8 * sizeof(float), &shift, sizeof(shift));
			require(read_projection(views.eyes[1], projection), "read modified raster projection");
			require(std::abs(-projection.tan_left / (projection.tan_right - projection.tan_left) - .6f) < .00001f,
				"raster matrix shift must override runtime optical defaults");
			const float invalid = (std::numeric_limits<float>::quiet_NaN)();
			std::memcpy(views.eyes[1].bytes.data() + 0x40, &invalid, sizeof(invalid));
			require(!read_projection(views.eyes[1], projection) && projection.tan_left == 0, "invalid matrix clears metadata");
		}
		require(!read_projection({}, projection), "missing metadata is explicit");
	}

	void test_color(const d3d11::device_snapshot& graphics, ID3D11Texture2D* const source, bool stabilized=false)
	{
		using Microsoft::WRL::ComPtr;
		const char vs_source[] = R"(
struct vertex { float4 position : SV_POSITION; float4 color : COLOR0; float2 uv : TEXCOORD0; };
vertex main(uint id : SV_VertexID) {
    float2 positions[3] = {float2(-1,-1),float2(-1,3),float2(3,-1)};
    vertex v; v.position = float4(positions[id],0,1); v.color = 1; v.uv = 0.5; return v;
})";
		ComPtr<ID3DBlob> code, errors;
		ComPtr<ID3D11VertexShader> vs;
		ComPtr<ID3D11PixelShader> ps;
		check(D3DCompile(vs_source, sizeof(vs_source) - 1, nullptr, nullptr, nullptr, "main", "vs_4_0",
			0, 0, &code, &errors), "compile test vertex shader");
		check(graphics.device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs), "create VS");
		code.Reset(); errors.Reset();
		const D3D_SHADER_MACRO macros[]{{"STABILIZED","1"},{nullptr,nullptr}};
		const auto shader_result = D3DCompile(vr::desktop_mirror::pixel_shader, sizeof(vr::desktop_mirror::pixel_shader) - 1,
			nullptr, stabilized?macros:nullptr, nullptr, "main", "ps_4_0", 0, 0, &code, &errors);
		if (FAILED(shader_result) && errors) std::cerr.write(
			static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
		check(shader_result, "compile production mirror shader");
		check(graphics.device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps), "create PS");
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11Texture2D> gradient;
		ComPtr<ID3D11Buffer> constants;
		if(stabilized)
		{
			std::array<std::array<float,4>,16> pixels{};
			for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)pixels[y*4+x]={x*.25f,y*.25f,.75f,1};
			D3D11_TEXTURE2D_DESC g{};g.Width=g.Height=4;g.MipLevels=g.ArraySize=g.SampleDesc.Count=1;
			g.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;g.BindFlags=D3D11_BIND_SHADER_RESOURCE;
			const D3D11_SUBRESOURCE_DATA data{pixels.data(),64,0};
			check(graphics.device->CreateTexture2D(&g,&data,&gradient),"create reprojection gradient");
			// Includes a perspective divide: UV (.5,.5) -> (.375,.625), the
			// centre of gradient texel (1,2). A wrong matrix/CB layout is visible.
			const std::array<std::array<float,4>,3> rows{{{2,0,-.25f,0},{0,2,.25f,0},{0,0,2,0}}};
			D3D11_BUFFER_DESC cb{};cb.ByteWidth=48;cb.Usage=D3D11_USAGE_IMMUTABLE;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
			const D3D11_SUBRESOURCE_DATA cb_data{rows.data(),0,0};
			check(graphics.device->CreateBuffer(&cb,&cb_data,&constants),"create homography constants");
		}
		check(graphics.device->CreateShaderResourceView(stabilized?gradient.Get():source, nullptr, &srv), "create right-eye SRV");
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		ComPtr<ID3D11Texture2D> output, staging;
		ComPtr<ID3D11RenderTargetView> rtv;
		check(graphics.device->CreateTexture2D(&desc, nullptr, &output), "create desktop test surface");
		check(graphics.device->CreateRenderTargetView(output.Get(), nullptr, &rtv), "create desktop RTV");
		desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		check(graphics.device->CreateTexture2D(&desc, nullptr, &staging), "create offline readback");
		D3D11_SAMPLER_DESC sample{};
		sample.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sample.MaxLOD = D3D11_FLOAT32_MAX;
		ComPtr<ID3D11SamplerState> sampler;
		check(graphics.device->CreateSamplerState(&sample, &sampler), "create sampler");
		const D3D11_VIEWPORT viewport{0, 0, 1, 1, 0, 1};
		auto* const context = graphics.context.Get();
		context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
		context->RSSetViewports(1, &viewport);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vs.Get(), nullptr, 0); context->PSSetShader(ps.Get(), nullptr, 0);
		context->PSSetShaderResources(0, 1, srv.GetAddressOf());
		context->PSSetSamplers(0, 1, sampler.GetAddressOf());
		if(stabilized)context->PSSetConstantBuffers(0,1,constants.GetAddressOf());
		context->Draw(3, 0);
		context->OMSetRenderTargets(0, nullptr, nullptr);
		context->CopyResource(staging.Get(), output.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "read mirror shader pixel");
		std::array<unsigned char, 4> pixel{};
		std::memcpy(pixel.data(), mapped.pData, pixel.size());
		context->Unmap(staging.Get(), 0);
		context->ClearState();
		require(std::abs(int(pixel[0]) - 137) <= 2 && std::abs(int(pixel[1]) - 188) <= 2 &&
			std::abs(int(pixel[2]) - 225) <= 2 && pixel[3] == 255, "linear right-eye RGB must be encoded to SDR sRGB");
	}

	void test_copy_projection(const d3d11::device_snapshot& graphics)
	{
		vr::native_render_session::session session;
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 256;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
		check(graphics.device->CreateTexture2D(&desc, nullptr, &source), "create native display source");
		check(graphics.device->CreateRenderTargetView(source.Get(), nullptr, &rtv), "create native display view");
		const float color[]{.25f, .5f, .75f, 1};
		graphics.context->ClearRenderTargetView(rtv.Get(), color);
		std::string error;
		require(session.ensure_copy_ring(graphics, desc, DXGI_FORMAT_R11G11B10_FLOAT, error), error.c_str());
		for (std::uint64_t id = 301; id <= 306; ++id)
		{
			auto recording_registration = id == 301 ? vr::eye_composition::register_consumer(publish_test_recording_crop, vr::eye_composition::layer::recording_frame) : vr::eye_composition::consumer_registration{};
			require(session.admit_pair(id), "admit projection test pair");
			auto views = make_views(id, id == 302);
			const auto camera_time=views.camera_sampled_at;
			views.stabilization_epoch=id;
			auto camera_axes=std::array{vr::pose_filter::identity,vr::pose_filter::identity};
			camera_axes[0]={{{0,1,0},{-1,0,0},{0,0,1}}};
			for (unsigned eye=0;eye<2;++eye)
				std::memcpy(views.eyes[eye].bytes.data()+0x10C,camera_axes[eye].data(),sizeof(camera_axes[eye]));
			const auto invalid_eye=static_cast<unsigned>(id%2);
			if (id == 304) ++views.eyes[invalid_eye].pair_id;
			if (id == 305) views.eyes[invalid_eye].output_eye = 1-invalid_eye;
			if (id == 306) views.eyes[invalid_eye].bytes.fill(0);
			vr::eye_composition::event event{views, id, graphics.generation, 1, 256, 256};
			// Right-first completion also has to retain its metadata until left completes.
			require(session.copy_eye(id, 1, source.Get(), 5, graphics.context.Get(), id == 303 ? nullptr : &event),
				"copy right eye with optional preview metadata");
			views.eyes[1].bytes.fill(0); // No retained pointer to the producer's slot.
			for (unsigned eye=0;eye<2;++eye)
			{
				const auto partial = session.eye_for_desktop(eye);
				require(!partial.texture || partial.pair_id < id, "unpublished image and metadata must stay hidden for either eye");
			}
			event.eye = 0;
			require(session.copy_eye(id, 0, source.Get(), 5, graphics.context.Get(), &event), "complete metadata pair");
			std::array<vr::native_render_session::eye_target, 2> submitted;
			require(session.acquire_published_pair(id, submitted), "desktop metadata must not affect HMD publication");
			std::array<vr::native_render_session::desktop_eye,2> mirrors;
			for (unsigned eye=0;eye<2;++eye)
			{
				const auto& mirror=mirrors[eye]=session.eye_for_desktop(eye);
				const bool metadata_valid=id<=302 || eye!=invalid_eye;
				require(mirror.texture.Get()==submitted[eye].color.Get() && mirror.texture.Get()!=submitted[1-eye].color.Get() &&
					mirror.pair_id==id,"each preview must match its own published eye texture");
				require(mirror.camera.valid==metadata_valid,"invalid metadata in one eye does not affect the other camera");
				if(metadata_valid)require(mirror.camera.at==camera_time && mirror.camera.epoch==id && mirror.camera.axes==camera_axes[eye],
					"each preview freezes its own camera orientation with the scene timestamp and epoch");
				require(bool(mirror.recording_crop)==(id==301),"each guide crop freezes with its image and clears on mode-off or slot reuse");
				if(id==301)require(mirror.recording_crop.u0==(eye==0?.1f:.2f) && mirror.recording_crop.v1==(eye==0?.6f:.7f),
					"each eye retains its own guide crop regardless of completion order");
				if(metadata_valid)
				{
					const auto view_eye=id==302 ? 1-eye : eye;
					require(std::abs(mirror.projection.tan_left-(view_eye==0?-.9f:-1.15f))<.00001f &&
						std::abs(mirror.projection.tan_up-(view_eye==0?.8f:1.f))<.00001f,
						"each projection snapshot follows the exact image, including eye swap");
				}
				else require(mirror.projection.tan_left==0 && mirror.projection.tan_right==0,
					"absent, foreign or invalid metadata must not reuse a prior ring projection");
			}
			require(session.release_pair(id), "desktop metadata must not affect HMD retirement");
			for (unsigned eye=0;eye<2;++eye)
			{
				const auto retired=session.eye_for_desktop(eye);
				require(retired.texture.Get()==mirrors[eye].texture.Get() && retired.pair_id==id &&
					retired.recording_crop.u0==mirrors[eye].recording_crop.u0 &&
					retired.projection.tan_left==mirrors[eye].projection.tan_left && retired.camera.axes==mirrors[eye].camera.axes,
					"retired images retain their own projection, camera and guide crop until reuse");
			}
		}
		session.invalidate();
		for (unsigned eye=0;eye<2;++eye)
			require(!session.eye_for_desktop(eye).texture, "device invalidation removes both images and metadata");
	}

	void test_ring()
	{
		d3d11::device_snapshot graphics;
		const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
		check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1,
			D3D11_SDK_VERSION, &graphics.device, &graphics.feature_level, &graphics.context), "create WARP only");
		graphics.generation = 73;
		vr::native_render_session::session session;
		std::string error;
		require(session.ensure(graphics, 256, 256, error), error.c_str());
		for (unsigned eye=0;eye<2;++eye)
			require(!session.eye_for_desktop(eye).texture, "unrendered ring has no mirror image for either eye");
		auto render = [&](const std::uint64_t id, const std::uint32_t eye)
		{
			vr::native_render_session::eye_target target;
			require(session.acquire_target(id, eye, target), "lease eye");
			const float left_color[]{1, 0, 0, 1}, right_color[]{0.25f, 0.5f, 0.75f, 1};
			graphics.context->ClearRenderTargetView(target.color_view.Get(), eye == 1 ? right_color : left_color);
			require(session.complete_rendered_eye(id, eye, target.color.Get()), "complete eye");
			return target;
		};
		const auto left = render(101, 0);
		for (unsigned eye=0;eye<2;++eye)
			require(!session.eye_for_desktop(eye).texture, "partial pair must not expose either eye");
		const auto right = render(101, 1);
		const auto before = session.get_status();
		const auto mirrors=std::array{session.eye_for_desktop(0),session.eye_for_desktop(1)};
		for (unsigned eye=0;eye<2;++eye)
			require(mirrors[eye].texture.Get()==(eye==0?left.color.Get():right.color.Get()) &&
				mirrors[eye].pair_id==101 && mirrors[eye].generation==73,"mirror is the exact selected output eye");
		require(!session.eye_for_desktop(2).texture &&
			!session.eye_for_desktop((std::numeric_limits<std::uint32_t>::max)()).texture,
			"invalid eye indices are rejected without touching either published eye");
		bool foreign_visible{};
		std::thread reader([&] { foreign_visible=!!session.eye_for_desktop(0).texture || !!session.eye_for_desktop(1).texture; });
		reader.join();
		require(!foreign_visible, "cross-thread mirror access must be refused for both eyes");
		const auto after = session.get_status();
		require(after.pair_acquires == before.pair_acquires && after.pair_releases == before.pair_releases &&
			after.pair_rejections == before.pair_rejections && session.pair_published(101), "mirror must not alter ownership");
		test_color(graphics, mirrors[1].texture.Get());
		test_color(graphics, mirrors[1].texture.Get(),true);
		require(session.release_pair(101), "retire first pair");
		for (unsigned eye=0;eye<2;++eye)
			require(session.eye_for_desktop(eye).pair_id == 101, "both retired eye previews persist without a copy");
		render(102, 0);
		for (unsigned eye=0;eye<2;++eye)
			require(session.eye_for_desktop(eye).pair_id == 101, "partial next pair does not displace either previous image");
		render(102, 1);
		for (unsigned eye=0;eye<2;++eye)
			require(session.eye_for_desktop(eye).pair_id == 102, "new complete pair replaces both mirror choices");
		session.quarantine_pair(102);
		for (unsigned eye=0;eye<2;++eye)
			require(session.eye_for_desktop(eye).pair_id == 101, "neither eye of a quarantined pair can be displayed");
		vr::native_render_session::eye_target writing;
		require(session.acquire_target(103, 0, writing), "reuse retired slot");
		for (unsigned eye=0;eye<2;++eye)
			require(!session.eye_for_desktop(eye).texture, "leased slot cannot leave either eye readable");
		session.cancel_pair(103);
		session.invalidate();
		for (unsigned eye=0;eye<2;++eye)
			require(!session.eye_for_desktop(eye).texture, "invalidation clears both mirror choices");
		test_copy_projection(graphics);
	}
}

int main()
{
	try
	{
			test_layout(); test_recording_layout(); test_projection_metadata(); test_ring();
		std::cout << "PASS: desktop mirror eye selection, optical crop, FOV bounds, per-eye metadata, ownership and sRGB shader (CPU/WARP only; no H2/HMD acceptance)\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
