#pragma once
#include "component/vr/native_post_aa_frame_thunk.hpp"
#include "component/vr/native_post_aa_depth.hpp"

inline bool native_post_aa_frame_tests()
{
	using namespace vr::native_post_aa;
	struct result { std::uint64_t gp[7], flags; std::uint8_t xmm[6][16]; };
	constexpr std::uint64_t sentinels[]{0x1234567800000001,0x1234567800000002,0x1234567800000003,
		0x1234567800000004,0x1234567800000005,0x1234567800000006,0x1234567800000007};
	const auto reader = utils::hook::assemble([](utils::hook::assembler& a)
	{
		// Deliberately clobber every volatile register and flags, including
		// unspecified upper return bits. A displaced MOV may change only r32.
		for (const auto reg : {rcx,rdx,r8,r9,r10,r11}) a.xor_(reg,reg);
		for (unsigned i = 0; i < 6; ++i) a.pxor(asmjit::x86::xmm(i),asmjit::x86::xmm(i));
		a.mov(rax,0x9999999912345678ull); a.stc(); a.ret();
	});
	for (const auto destination : {frame_register::eax, frame_register::ecx})
	{
		auto* thunk = make_frame_read_thunk(reader, destination);
		auto* caller = utils::hook::assemble([&](utils::hook::assembler& a)
		{
			a.push(r12); a.sub(rsp,0x20); a.mov(r12,rcx);
			a.xor_(eax,eax); // ZF=1, CF=0.
			unsigned i{};
			for (const auto reg : {rax,rcx,rdx,r8,r9,r10,r11}) a.mov(reg,sentinels[i++]);
			for (unsigned lane = 0; lane < 6; ++lane) a.pcmpeqd(asmjit::x86::xmm(lane),asmjit::x86::xmm(lane));
			a.call(thunk);
			i=0;
			for (const auto reg : {rax,rcx,rdx,r8,r9,r10,r11}) a.mov(qword_ptr(r12,i++*8),reg);
			a.pushfq(); a.pop(rax); a.mov(qword_ptr(r12,offsetof(result,flags)),rax);
			for (unsigned lane = 0; lane < 6; ++lane)
				a.movdqu(xmmword_ptr(r12,offsetof(result,xmm)+lane*16),asmjit::x86::xmm(lane));
			a.add(rsp,0x20); a.pop(r12); a.ret();
		});
		if (!reader || !thunk || !caller) return false;
		result observed{};
		reinterpret_cast<void(*)(result*)>(caller)(&observed);
		for (unsigned i = 0; i < 7; ++i)
			if (observed.gp[i] != (i == (destination == frame_register::eax ? 0u : 1u) ?
				0x12345678u : sentinels[i])) return false;
		if ((observed.flags & 0x41) != 0x40) return false;
		for (const auto& lane : observed.xmm) for (auto byte : lane) if (byte != 0xFF) return false;
	}
	return true;
}

inline bool native_post_aa_depth_tests(ID3D11Device* device, ID3D11DeviceContext* context)
{
	using Microsoft::WRL::ComPtr;
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width=desc.Height=8;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
	desc.Format=DXGI_FORMAT_R24G8_TYPELESS;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
	ComPtr<ID3D11Texture2D> depth,staging;
	ComPtr<ID3D11DepthStencilView> view;
	D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
	dsv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dsv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
	if (FAILED(device->CreateTexture2D(&desc,nullptr,&depth)) ||
		FAILED(device->CreateDepthStencilView(depth.Get(),&dsv,&view))) return false;
	desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
	if (FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) return false;
	const auto pixel=[&](std::uint32_t& value)
	{
		context->CopyResource(staging.Get(),depth.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;
		std::memcpy(&value,mapped.pData,4);context->Unmap(staging.Get(),0);return true;
	};
	vr::native_post_aa::depth_preserver saved;
	for (bool success : {true,false})
	{
		context->ClearDepthStencilView(view.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.75f,0x36);
		std::uint32_t before{},after{};
		if (!pixel(before)) return false;
		const bool result=saved.preserve(context,view.Get(),[&]
		{
			context->ClearDepthStencilView(view.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0,0xFF);
			return success;
		});
		if (result!=success || !pixel(after) || before!=after) return false;
		try
		{
			saved.preserve(context,view.Get(),[&]() -> bool
			{context->ClearDepthStencilView(view.Get(),D3D11_CLEAR_DEPTH,0,0);throw 1;});
		}
		catch(int) {}
		if (!pixel(after) || before!=after) return false;
	}
	return true;
}
