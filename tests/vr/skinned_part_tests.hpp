#pragma once
#include "component/vr/gameplay/skinned_part_visibility.hpp"
#include "component/scene_surface_indices.hpp"
#include <iostream>
#include <limits>

namespace skinned_part_tests
{
	inline int run(ID3D11Device* device,ID3D11DeviceContext* context)
	{
		using namespace vr::gameplay::weapons;
		using Microsoft::WRL::ComPtr;
		int failed{}; const auto check=[&](bool ok,const char* why) { if (!ok) { ++failed; std::cerr<<"FAIL: "<<why<<'\n'; } };
		std::array<std::int16_t,8> counts{6,1};
		// Three magazine vertices, three receiver vertices, one mixed gun/cover
		// vertex. All original vertex/weight order is preserved by index filtering.
		std::array<std::uint16_t,9> blend{64,64,64,0,0,0,0,128,32767};
		std::array<std::uint16_t,9> faces{0,1,2,3,4,5,3,5,6};
		auto p=partition_skin(counts,blend,faces,7,3,1);
		check(p.valid && p.removed==1 && p.retained==std::vector<std::uint16_t>({3,4,5,3,5,6}),"mixed receiver/cover retained while magazine-only triangle removed");
		faces[0]=3; check(!partition_skin(counts,blend,faces,7,3,1).valid,"cross-part triangle rejected"); faces[0]=0;
		blend[7]=64; check(!partition_skin(counts,blend,faces,7,3,1).valid,"mixed selected-part vertex rejected"); blend[7]=128;
		blend[0]=65; check(!partition_skin(counts,blend,faces,7,3,1).valid,"unaligned bone offset rejected"); blend[0]=64;
		faces[8]=7; check(!partition_skin(counts,blend,faces,7,3,1).valid,"out-of-range triangle rejected"); faces[8]=6;
		counts[0]=-1; check(!partition_skin(counts,blend,faces,7,3,1).valid,"negative blend count rejected"); counts[0]=6;
		check(!partition_skin(counts,std::span(blend).first(8),faces,7,3,1).valid,"truncated blend stream rejected");
		std::array<bool,256> arm{};arm[0]=arm[2]=true;
		auto hand=partition_skin(counts,blend,faces,7,3,arm);
		check(hand.valid && hand.removed==2 && hand.retained==std::vector<std::uint16_t>({0,1,2}),
			"a whole arm may contain multiple weighted bones without hiding the opposite arm");
		arm[1]=true;hand=partition_skin(counts,blend,faces,7,3,arm);
		check(hand.valid && hand.removed==3 && hand.retained.empty(),"whole-arm surface is recognized explicitly");
		const std::array<std::int16_t,8> body_counts{9};
		const std::array<std::uint16_t,9> body_blend{0,0,0,64,64,64,128,128,128},body_faces{0,1,2,3,4,5,6,7,8};
		std::array<bool,256> both_arms{};both_arms[1]=both_arms[2]=true;
		const auto body=partition_skin(body_counts,body_blend,body_faces,9,3,both_arms);
		check(body.valid && body.removed==2 && body.retained==std::vector<std::uint16_t>({0,1,2}),"combined scripted arm selection preserves torso triangles and original vertex indices");
		std::vector<std::byte> packet(8+56+128+56);
		const auto header=[&](size_t offset,std::int32_t value) { std::memcpy(packet.data()+offset,&value,4); };
		header(0,-3); header(4,0x7fffffff); header(8,0); header(64,-5); header(192,4159);
		auto plan=plan_skin_packets(packet,4);
		check(plan.valid && plan.count==2 && plan.skinned[0]==8 && plan.skinned[1]==192,"expanded hidden/skinned/rigid stream skips padding and locates both skin records");
		check(!plan_skin_packets(std::span(packet).first(packet.size()-1),4).valid && !plan_skin_packets(packet,3).valid,
			"packet truncation and mismatched surface count reject before any patch");
		header(64,std::numeric_limits<std::int32_t>::min()); check(!plan_skin_packets(packet,4).valid,"packet negative overflow rejected");
		header(64,-2); check(!plan_skin_packets(packet,4).valid,"unknown sentinel rejected");
		for (int mode=0;mode<4;++mode)
		{
			D3D11_BUFFER_DESC d{}; d.ByteWidth=20; d.Usage=D3D11_USAGE_IMMUTABLE;
			d.BindFlags=D3D11_BIND_INDEX_BUFFER|(mode ? D3D11_BIND_SHADER_RESOURCE : 0);
			if (mode==3) d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			std::array<std::uint16_t,10> data{}; std::copy(faces.begin(),faces.end(),data.begin());
			D3D11_SUBRESOURCE_DATA initial{data.data(),0,0}; ComPtr<ID3D11Buffer> source;
			check(SUCCEEDED(device->CreateBuffer(&d,&initial,&source)),"skin test source index buffer");
			ComPtr<ID3D11ShaderResourceView> source_view;
			if (mode)
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC s{};
				s.Format=mode==1 ? DXGI_FORMAT_R16_UINT : mode==2 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R32_TYPELESS;
				s.ViewDimension=mode==3 ? D3D11_SRV_DIMENSION_BUFFEREX : D3D11_SRV_DIMENSION_BUFFER;
				s.Buffer.NumElements=mode==1 ? 10 : 5; if (mode==3) s.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
				check(SUCCEEDED(device->CreateShaderResourceView(source.Get(),&s,&source_view)),"skin test source index view");
			}
			ComPtr<ID3D11Buffer> subset; ComPtr<ID3D11ShaderResourceView> view;
			check(scene_models::create_surface_indices(device,source_view.Get(),std::as_bytes(std::span(p.retained)),subset,view),"skin immutable subset buffer allocation");
			if (!subset) continue;
			subset->GetDesc(&d); check(d.ByteWidth==12,"skin subset has exact retained triangle bytes");
			d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.MiscFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
			ComPtr<ID3D11Buffer> staging; check(SUCCEEDED(device->CreateBuffer(&d,nullptr,&staging)),"skin test staging buffer");
			if (!staging) continue;
			context->CopyResource(staging.Get(),subset.Get()); D3D11_MAPPED_SUBRESOURCE mapped{};
			if (SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))
			{ check(!std::memcmp(mapped.pData,p.retained.data(),12),"GPU skin subset agrees with CPU triangle order"); context->Unmap(staging.Get(),0); }
			else check(false,"skin index buffer readback");
		}
		std::cout<<"Skinned part partition/packet/WARP tests: "<<(failed ? "FAIL" : "PASS")<<'\n';
		return failed;
	}
}
