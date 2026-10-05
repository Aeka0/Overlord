#pragma once
#include "component/vr/opaque_mesh_renderer.hpp"
#include "component/vr/gameplay/chambering_guide_pulse.hpp"

template<class Check> void opaque_mesh_tests(ID3D11Device* device,ID3D11DeviceContext* context,Check check)
{
	using namespace vr::opaque_mesh;
	using Microsoft::WRL::ComPtr;
	const matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
	using vr::gameplay::weapons::chambering_guide::pulse;
	const auto peak=pulse(.5),trough=pulse(1.5);
	check(peak.tint>trough.tint && peak.emission>trough.emission && trough.tint>0 && trough.emission>0,
		"sine guide remains subtly tinted throughout the cycle rather than flashing on/off");
	check(std::abs(pulse(2.5).tint-peak.tint)<.00001f && std::abs(pulse(1e12+.5).emission-peak.emission)<.00001f &&
		std::abs(pulse(-.001).tint-pulse(1.999).tint)<.00001f,"two-second pulse is periodic, continuous and stable at long uptime");
	for(const double seconds:{-1000.0,0.0,.5,1.5,1e12,(std::numeric_limits<double>::infinity)(),(std::numeric_limits<double>::quiet_NaN)()})
	{
		const auto p=pulse(seconds);
		check(std::isfinite(p.tint) && p.tint>=.10f && p.tint<=.18001f && p.emission>=.015f && p.emission<=.04001f,
			"guide modulation is bounded even for invalid time");
	}
	const auto placed=transform({{1,2,3},{0,0,.70710678f,.70710678f}},{10,0,0},{0,10,0},identity);
	check(std::abs(placed[12]-11)<.0001f && std::abs(placed[13]+8)<.0001f &&
		std::abs(placed[0])<.0001f && std::abs(placed[1]-1)<.0001f,"guide composes part rotation, current placement and eye origin");
	mesh geometry;const std::array<vr::spatial_math::vec,3> vertices{{{-1,-1,.5f},{-1,1,.5f},{1,0,.5f}}};
	const std::array<unsigned,3> indices{0,1,2};
	D3D11_BUFFER_DESC buffer{};buffer.Usage=D3D11_USAGE_IMMUTABLE;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;buffer.ByteWidth=sizeof(vertices);
	D3D11_SUBRESOURCE_DATA data{vertices.data(),0,0};
	check(SUCCEEDED(device->CreateBuffer(&buffer,&data,&geometry.vertices)),"guide test vertices");
	buffer.BindFlags=D3D11_BIND_INDEX_BUFFER;buffer.ByteWidth=sizeof(indices);data.pSysMem=indices.data();
	check(SUCCEEDED(device->CreateBuffer(&buffer,&data,&geometry.indices)),"guide test indices");geometry.index_count=3;
	D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=8;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
	 desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
	ComPtr<ID3D11Texture2D> color,depth,readback;ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> dsv;
	check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&color)) && SUCCEEDED(device->CreateRenderTargetView(color.Get(),nullptr,&target)),"guide test color target");
	desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
	check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)),"guide pixel readback");
	desc.Format=DXGI_FORMAT_D32_FLOAT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;
	check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&depth)) && SUCCEEDED(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv)),"guide native-depth target");
	if(!target || !dsv || !readback)return;
	renderer renderer;const draw drawing{&geometry,identity};
	const D3D11_VIEWPORT before{2,1,3,4,0,1};context->RSSetViewports(1,&before);
	const auto pixel=[&](unsigned x=3,unsigned y=4) {
		context->CopyResource(readback.Get(),color.Get());D3D11_MAPPED_SUBRESOURCE map{};std::array<unsigned char,4> rgba{};
		if(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&map)))
		{std::memcpy(rgba.data(),static_cast<const std::byte*>(map.pData)+y*map.RowPitch+x*4,4);context->Unmap(readback.Get(),0);}
		return rgba;
	};
	const auto seed=[&] {
		std::array<std::uint32_t,64> pixels;
		for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)pixels[y*8+x]=x<4?0x40996633u:0x40331a80u;
		context->UpdateSubresource(color.Get(),0,nullptr,pixels.data(),8*4,0);
		context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.5f,0);
	};
	seed();
	check(renderer.render(context,target.Get(),dsv.Get(),{&drawing,1},8,8,0,0) &&
		pixel()==std::array<unsigned char,4>{51,102,153,255} && pixel(4)==std::array<unsigned char,4>{128,26,51,255},
		"zero modulation preserves native material detail and lighting with opaque alpha");
	for(unsigned copies:{1u,2u})
	{
		seed();const std::array<draw,2> overlapping{drawing,drawing};
		check(renderer.render(context,target.Get(),dsv.Get(),{overlapping.data(),copies},8,8,peak.tint,peak.emission),"tinted guide draws at native surface depth");
		check(pixel()==std::array<unsigned char,4>{61,108,125,255} && pixel(4)==std::array<unsigned char,4>{138,34,42,255},
			"slight yellow tint retains per-pixel detail; overlapping parts do not accumulate emission");
	}
	const float black[]{0,0,0,.25f};
	for(const auto p:{trough,peak})
	{
		context->ClearRenderTargetView(target.Get(),black);
		check(renderer.render(context,target.Get(),dsv.Get(),{&drawing,1},8,8,p.tint,p.emission),"emission gently lifts unlit material");
		const auto rgba=pixel();check(rgba[0]>=3 && rgba[0]<=11 && rgba[1]>0 && rgba[2]==0 && rgba[3]==255,"dark material receives bounded golden emission, not a solid fill");
	}
	const float blue[]{0,0,1,1};context->ClearRenderTargetView(target.Get(),blue);
	context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.9f,0);
	check(renderer.render(context,target.Get(),dsv.Get(),{&drawing,1},8,8,peak.tint,peak.emission) && pixel()==std::array<unsigned char,4>{0,0,255,255},
		"nearer hands/receiver occlude guide under native reverse-Z");
	D3D11_VIEWPORT after{};UINT count=1;context->RSGetViewports(&count,&after);
	check(count==1 && std::memcmp(&before,&after,sizeof(before))==0,"guide restores native immediate-context state");
	check(!renderer.render(context,target.Get(),nullptr,{&drawing,1},8,8,peak.tint,peak.emission) &&
		!renderer.render(context,target.Get(),dsv.Get(),{&drawing,1},8,8,(std::numeric_limits<float>::quiet_NaN)(),peak.emission),"guide refuses depthless or nonfinite modulation");
}
