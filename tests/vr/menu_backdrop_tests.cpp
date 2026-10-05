#include <std_include.hpp>
#include "component/vr/spatial_panel_renderer.hpp"
#include "test_support.hpp"
#include <iostream>

namespace
{
	using Microsoft::WRL::ComPtr;
	using vr::tests::require;
	void run()
	{
		constexpr unsigned size=32;
		ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
		require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)),"WARP device");
		D3D11_TEXTURE2D_DESC desc{size,size,1,1,DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,0,0};
		std::array<std::uint32_t,size*size> original{},mask{};
		for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)
		{
			original[y*size+x]=(x&1)?0xff000000:0xffffffff;
			const unsigned alpha=x<8?0:x<16?128:255;
			mask[y*size+x]=(alpha<<24)|0x000000ff; // Red RGB must never tint the scene.
		}
		ComPtr<ID3D11Texture2D> output,mask_texture,staging;
		D3D11_SUBRESOURCE_DATA data{original.data(),size*4,0};
		require(SUCCEEDED(device->CreateTexture2D(&desc,&data,&output)),"scene texture");
		data.pSysMem=mask.data();require(SUCCEEDED(device->CreateTexture2D(&desc,&data,&mask_texture)),"mask texture");
		ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11ShaderResourceView> ink;
		require(SUCCEEDED(device->CreateRenderTargetView(output.Get(),nullptr,&target))&&SUCCEEDED(device->CreateShaderResourceView(mask_texture.Get(),nullptr,&ink)),"views");
		desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
		require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"readback texture");
		auto read=[&]{std::array<std::uint32_t,size*size> pixels;context->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
			require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"readback map");
			for(unsigned y=0;y<size;++y)std::memcpy(pixels.data()+y*size,static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,size*4);
			context->Unmap(staging.Get(),0);return pixels;};
		vr::spatial_panel::renderer renderer;
		const vr::spatial_panel::projected_quad quad{{{-.5f,.5f,.5f,1},{.5f,.5f,.5f,1},{-.5f,-.5f,.5f,1},{.5f,-.5f,.5f,1}}};
		require(renderer.draw_masked_blur(context.Get(),ink.Get(),target.Get(),quad,size,size,1,1),"masked blur rejected");
		const auto blurred=read();
		for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)
			if(x<8||x>=24||y<8||y>=24)require(blurred[y*size+x]==original[y*size+x],"blur changed a pixel outside the projected panel");
		require(blurred[16*size+10]==original[16*size+10],"transparent mask padding blurred scene");
		const auto full=blurred[16*size+20]&255,half=blurred[16*size+14]&255;
		require(full>20&&full<235&&half>full&&half<255,"mask coverage did not control blur strength");
		require((blurred[16*size+20]&255)==((blurred[16*size+20]>>16)&255),"mask RGB tinted scene");
		context->UpdateSubresource(output.Get(),0,nullptr,original.data(),size*4,0);
		require(renderer.draw_masked_blur(context.Get(),ink.Get(),target.Get(),quad,size,size,14,3),"strong mask rejected");
		const auto strong=read();const auto strong_half=strong[16*size+14]&255;
		require(strong_half>80&&strong_half<180&&strong_half<half,"wide even-radius blur aliased fine detail or strength was clamped to one");
		require(strong[16*size+10]==original[16*size+10],"strong blur escaped native mask");
		context->UpdateSubresource(output.Get(),0,nullptr,original.data(),size*4,0);
		require(renderer.draw_panel_blur(context.Get(),target.Get(),quad,size,size,14),"uniform panel blur rejected without a mask");
		const auto panel=read();
		for(unsigned x:{10u,14u,20u})require((panel[16*size+x]&255)>80&&(panel[16*size+x]&255)<180,
			"panel blur still depends on transparent/half/opaque vignette coverage");
		require(panel[16*size+4]==original[16*size+4],"uniform panel blur escaped its geometry");
		context->UpdateSubresource(output.Get(),0,nullptr,original.data(),size*4,0);
		const vr::spatial_panel::projected_quad crossing{{{-.5f,.5f,-.25f,-.5f},{.5f,.5f,.5f,1},{-.5f,-.5f,-.25f,-.5f},{.5f,-.5f,.5f,1}}};
		require(renderer.draw_panel_blur(context.Get(),target.Get(),crossing,size,size,14),"partly visible menu blur rejected the whole panel");
		require(read()[16*size+16]!=original[16*size+16],"GPU-clipped menu stopped blurring its still-visible area");
		// The second eye uses a different scene and the same renderer/resources.
		for(auto& p:original)p=0xffff0000|((p&255)<<8);
		context->UpdateSubresource(output.Get(),0,nullptr,original.data(),size*4,0);
		require(renderer.draw_masked_blur(context.Get(),ink.Get(),target.Get(),quad,size,size,1,1),"second eye blur rejected");
		const auto second=read()[16*size+20];
		require((second&255)==0&&((second>>16)&255)==255&&((second>>8)&255)>20&&((second>>8)&255)<235,"stale first-eye scene contaminated second eye");
		// Reverse-Z depth must leave objects in front of the menu clear.
		std::array<float,size*size> depth;depth.fill(.1f);depth[16*size+20]=.9f;
		desc={size,size,1,1,DXGI_FORMAT_D32_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_DEPTH_STENCIL,0,0};
		data={depth.data(),size*4,0};ComPtr<ID3D11Texture2D> z;ComPtr<ID3D11DepthStencilView> zview;
		require(SUCCEEDED(device->CreateTexture2D(&desc,&data,&z))&&SUCCEEDED(device->CreateDepthStencilView(z.Get(),nullptr,&zview)),"depth fixture");
		context->UpdateSubresource(output.Get(),0,nullptr,original.data(),size*4,0);
		require(renderer.draw_masked_blur(context.Get(),ink.Get(),target.Get(),quad,size,size,1,1,zview.Get()),"depth-aware mask rejected");
		const auto occluded=read();require(occluded[16*size+20]==original[16*size+20]&&occluded[16*size+22]!=original[16*size+22],"foreground was blurred or background remained sharp");
		context->UpdateSubresource(output.Get(),0,nullptr,original.data(),size*4,0);
		require(renderer.draw_panel_blur(context.Get(),target.Get(),quad,size,size,14,zview.Get()),"uniform depth-aware blur rejected");
		const auto panel_depth=read();require(panel_depth[16*size+20]==original[16*size+20]&&panel_depth[16*size+10]!=original[16*size+10],
			"uniform blur changed foreground or retained transparent vignette holes");
		// Non-UNORM linear eye targets retain HDR luminance without an encode/clamp.
		std::array<std::array<float,4>,size*size> hdr{};
		for(unsigned i=0;i<hdr.size();++i)hdr[i]={(i%size)&1?0.f:4.f,2.f,3.f,1.f};
		desc={size,size,1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET,0,0};
		data={hdr.data(),size*16,0};ComPtr<ID3D11Texture2D> hdr_output,hdr_read;ComPtr<ID3D11RenderTargetView> hdr_target;
		require(SUCCEEDED(device->CreateTexture2D(&desc,&data,&hdr_output))&&SUCCEEDED(device->CreateRenderTargetView(hdr_output.Get(),nullptr,&hdr_target)),"HDR fixture");
		require(renderer.draw_masked_blur(context.Get(),ink.Get(),hdr_target.Get(),quad,size,size,1,1),"HDR mask rejected");
		desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
		require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&hdr_read)),"HDR staging");context->CopyResource(hdr_read.Get(),hdr_output.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(context->Map(hdr_read.Get(),0,D3D11_MAP_READ,0,&mapped)),"HDR map");
		std::array<float,4> pixel;std::memcpy(pixel.data(),static_cast<const char*>(mapped.pData)+16*mapped.RowPitch+20*16,16);context->Unmap(hdr_read.Get(),0);
		require(pixel[0]>1&&pixel[0]<4&&std::abs(pixel[1]-2)<.001f&&std::abs(pixel[2]-3)<.001f,"blur clipped or gamma-shifted HDR scene");
		// The completed native UI is copied to 16:9 after capture. Its pixels,
		// alpha, native target and viewport must all survive unchanged.
		std::array<std::uint32_t,16*16> native_pixels;native_pixels.fill(0x80402010);
		desc={16,16,1,1,DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET,0,0};
		data={native_pixels.data(),16*4,0};ComPtr<ID3D11Texture2D> native_ui,canvas,canvas_read,native_read;
		ComPtr<ID3D11RenderTargetView> native_target,canvas_target;ComPtr<ID3D11ShaderResourceView> native_view;
		require(SUCCEEDED(device->CreateTexture2D(&desc,&data,&native_ui))&&SUCCEEDED(device->CreateRenderTargetView(native_ui.Get(),nullptr,&native_target))&&
			SUCCEEDED(device->CreateShaderResourceView(native_ui.Get(),nullptr,&native_view)),"native canvas fixture");
		desc.Width=32;desc.Height=18;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&canvas))&&
			SUCCEEDED(device->CreateRenderTargetView(canvas.Get(),nullptr,&canvas_target)),"independent canvas target");
		const D3D11_VIEWPORT sentinel{3,5,11,9,.2f,.9f};context->RSSetViewports(1,&sentinel);
		auto* native_rt=native_target.Get();context->OMSetRenderTargets(1,&native_rt,nullptr);
		require(renderer.draw_canvas(context.Get(),native_view.Get(),canvas_target.Get(),vr::ui_canvas::fit(16,16,32,18)),"independent canvas copy rejected");
		UINT count=1;D3D11_VIEWPORT restored{};context->RSGetViewports(&count,&restored);ComPtr<ID3D11RenderTargetView> restored_target;
		context->OMGetRenderTargets(1,&restored_target,nullptr);require(std::memcmp(&restored,&sentinel,sizeof(sentinel))==0&&restored_target==native_target,"canvas changed native render state");
		desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
		require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&canvas_read)),"canvas readback");context->CopyResource(canvas_read.Get(),canvas.Get());
		require(SUCCEEDED(context->Map(canvas_read.Get(),0,D3D11_MAP_READ,0,&mapped)),"canvas map");
		for(unsigned y=0;y<18;++y)for(unsigned x=0;x<32;++x)
		{
			std::uint32_t p;std::memcpy(&p,static_cast<const char*>(mapped.pData)+y*mapped.RowPitch+x*4,4);
			require(p==(x>=7&&x<25?0x80402010u:0u),"canvas stretched native content or lost alpha/transparent padding");
		}
		context->Unmap(canvas_read.Get(),0);desc.Width=desc.Height=16;
		require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&native_read)),"native readback");context->CopyResource(native_read.Get(),native_ui.Get());
		require(SUCCEEDED(context->Map(native_read.Get(),0,D3D11_MAP_READ,0,&mapped)),"native map");
		for(unsigned y=0;y<16;++y)require(std::memcmp(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,native_pixels.data()+y*16,16*4)==0,"VR canvas mutated desktop/native pixels");
		context->Unmap(native_read.Get(),0);
	}
}
int main(){try{run();std::cout<<"menu backdrop WARP tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
