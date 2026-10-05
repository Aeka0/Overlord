#pragma once
#include <asmjit/x86/x86assembler.h>
#include <cstdint>

namespace vr::native_waypoints
{
	struct image_layout {std::uintptr_t material;float x,y,width,height,alpha;};
	inline void emit_image_layout_marker(asmjit::x86::Assembler& a,std::uintptr_t observer,std::uintptr_t continuation)
	{
		using namespace asmjit::x86;
		// CG_DrawHudElemImage's common layout boundary, before its scalar/quad/
		// rotated branches. Stack is aligned here. Native Y is still in XMM0.
		a.pushfq();for(const auto reg:{rax,rcx,rdx,r8,r9,r10,r11})a.push(reg);
		a.sub(rsp,0xa0);
		for(int i=0;i<6;++i)a.movdqu(xmmword_ptr(rsp,0x20+i*16),xmm(i));
		a.mov(qword_ptr(rsp,0x80),rdi);
		a.movss(xmm1,dword_ptr(rbx));a.movss(dword_ptr(rsp,0x88),xmm1);
		a.movss(dword_ptr(rsp,0x8c),xmm0);
		a.movss(dword_ptr(rsp,0x90),xmm7);a.movss(dword_ptr(rsp,0x94),xmm6);
		a.movss(xmm1,dword_ptr(rbx,0x23c));a.movss(dword_ptr(rsp,0x98),xmm1);
		a.lea(rcx,ptr(rsp,0x80));a.call(observer);
		for(int i=0;i<6;++i)a.movdqu(xmm(i),xmmword_ptr(rsp,0x20+i*16));
		a.add(rsp,0xa0);for(const auto reg:{r11,r10,r9,r8,rdx,rcx,rax})a.pop(reg);a.popfq();
		a.test(dword_ptr(rsi,0xb8),0x4000); // displaced native instruction, including flags
		a.jmp(continuation);
	}
}
