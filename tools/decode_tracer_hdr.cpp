// Offline BC6H decoder for the optional tracer asset build. Uses WARP, never
// attaches to the game or its D3D device. Output is linear RGBA32F, all mips.
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
using Microsoft::WRL::ComPtr;

static void checked(HRESULT result) { if (FAILED(result)) throw std::runtime_error("D3D11 operation failed: " + std::to_string(result)); }

int wmain(int argc, wchar_t** argv)
{
	try
	{
		if (argc != 6) throw std::runtime_error("Usage: decode_tracer_hdr input.bc6 output.rgba32f width height mips");
		const unsigned width=std::stoul(argv[3]), height=std::stoul(argv[4]), mips=std::stoul(argv[5]);
		if (!width || !height || width>1024 || height>1024 || !mips || mips>11) throw std::runtime_error("Invalid image extent");
		if (std::filesystem::file_size(argv[1]) > 16*1024*1024) throw std::runtime_error("Input exceeds bounded image budget");
		std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
		const std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
		std::vector<D3D11_SUBRESOURCE_DATA> data;
		std::size_t offset{};
		for (unsigned i=0,w=width,h=height; i<mips; ++i,w=std::max(1u,w/2),h=std::max(1u,h/2))
		{
			const unsigned pitch=std::max(1u,(w+3)/4)*16, size=pitch*std::max(1u,(h+3)/4);
			if (offset+size>bytes.size()) throw std::runtime_error("Truncated BC6H mip chain");
			data.push_back({bytes.data()+offset,pitch,size}); offset+=size;
		}
		if (offset!=bytes.size()) throw std::runtime_error("Unexpected BC6H payload extent");
		ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
		checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
		D3D11_TEXTURE2D_DESC desc{}; desc.Width=width;desc.Height=height;desc.MipLevels=mips;desc.ArraySize=1;
		desc.Format=DXGI_FORMAT_BC6H_UF16;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
		ComPtr<ID3D11Texture2D> texture;checked(device->CreateTexture2D(&desc,data.data(),&texture));
		constexpr char vs_source[]=R"(float4 main(uint i:SV_VertexID):SV_Position {float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[i],0,1);})";
		constexpr char ps_source[]=R"(Texture2D<float4> image:register(t0);float4 main(float4 p:SV_Position):SV_Target{return image.Load(int3(int2(p.xy),0));})";
		ComPtr<ID3DBlob> vs_code,ps_code,error;
		checked(D3DCompile(vs_source,sizeof(vs_source)-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vs_code,&error));
		checked(D3DCompile(ps_source,sizeof(ps_source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&ps_code,&error));
		ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
		checked(device->CreateVertexShader(vs_code->GetBufferPointer(),vs_code->GetBufferSize(),nullptr,&vs));
		checked(device->CreatePixelShader(ps_code->GetBufferPointer(),ps_code->GetBufferSize(),nullptr,&ps));
		context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
		ComPtr<ID3D11RasterizerState> state;checked(device->CreateRasterizerState(&raster,&state));context->RSSetState(state.Get());
		std::ofstream output(std::filesystem::path(argv[2]),std::ios::binary);
		for (unsigned i=0,w=width,h=height;i<mips;++i,w=std::max(1u,w/2),h=std::max(1u,h/2))
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=desc.Format;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
			view.Texture2D.MostDetailedMip=i;view.Texture2D.MipLevels=1;
			ComPtr<ID3D11ShaderResourceView> srv;checked(device->CreateShaderResourceView(texture.Get(),&view,&srv));
			D3D11_TEXTURE2D_DESC target=desc;target.Width=w;target.Height=h;target.MipLevels=1;target.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
			target.Usage=D3D11_USAGE_DEFAULT;target.BindFlags=D3D11_BIND_RENDER_TARGET;
			ComPtr<ID3D11Texture2D> color,staging;checked(device->CreateTexture2D(&target,nullptr,&color));
			ComPtr<ID3D11RenderTargetView> rtv;checked(device->CreateRenderTargetView(color.Get(),nullptr,&rtv));
			target.Usage=D3D11_USAGE_STAGING;target.BindFlags=0;target.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
			checked(device->CreateTexture2D(&target,nullptr,&staging));
			ID3D11RenderTargetView* render=rtv.Get();context->OMSetRenderTargets(1,&render,nullptr);
			ID3D11ShaderResourceView* sample=srv.Get();context->PSSetShaderResources(0,1,&sample);
			D3D11_VIEWPORT viewport{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&viewport);context->Draw(3,0);
			context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(staging.Get(),color.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};checked(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
			for(unsigned y=0;y<h;++y)output.write(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,w*16);
			context->Unmap(staging.Get(),0);
		}
		if(!output)throw std::runtime_error("Output write failed");
		std::cout<<"Decoded "<<mips<<" BC6H mips with WARP\n";
	}
	catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
