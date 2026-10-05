#pragma once
#include <asmjit/x86/x86assembler.h>
#include <cstdint>

namespace vr::native_hud_capture
{
	// Bracket the native global 2D loop without replacing/replaying its handlers.
	// The original owner has an aligned stack at both sites. Save all volatile
	// integer/SIMD state and flags because neither site was originally a CALL.
	inline void emit_ui_dispatch_marker(asmjit::x86::Assembler& a,std::uintptr_t observer,
		std::uintptr_t continuation,bool begin,std::uintptr_t end_load_address=0)
	{
		using namespace asmjit::x86;
		a.pushfq();
		for(const auto reg:{rax,rcx,rdx,r8,r9,r10,r11})a.push(reg);
		a.sub(rsp,0x80);
		for(int i=0;i<6;++i)a.movdqu(xmmword_ptr(rsp,0x20+i*16),xmm(i));
		a.mov(rcx,qword_ptr(rsp,0xb0)); // Native RAX contains the command-stream pointer at begin.
		a.call(observer);
		for(int i=0;i<6;++i)a.movdqu(xmm(i),xmmword_ptr(rsp,0x20+i*16));
		a.add(rsp,0x80);
		for(const auto reg:{r11,r10,r9,r8,rdx,rcx,rax})a.pop(reg);
		a.popfq();
		if(begin)a.mov(qword_ptr(rsp,0xa8),rax);
		else {a.mov(rax,end_load_address);a.mov(eax,dword_ptr(rax));}
		a.jmp(continuation);
	}
}
