#pragma once
#include <asmjit/x86/x86assembler.h>
#include <array>
#include <cstdint>

namespace scene_models::identity
{
	// Verified position-independent tail of H2's index getter, after its pool LEA.
	// Keep native RCX/RDX, flags and XMM behavior, not only the public RAX result:
	// whole-program optimized native callers rely on this leaf's narrow clobbers.
	inline constexpr std::array<std::uint8_t,34> getter_tail{
		0x48,0x2b,0xc8,0x48,0xb8,0xbd,0x40,0x26,0xc5,0x0b,0x64,0x52,0xbc,
		0x48,0xf7,0xe9,0x48,0x03,0xd1,0x48,0xc1,0xfa,0x09,0x48,0x8b,0xc2,
		0x48,0xc1,0xe8,0x3f,0x48,0x03,0xc2,0xc3};
	inline void emit_bridge(asmjit::x86::Assembler& a,std::uintptr_t resolver)
	{
		using namespace asmjit::x86;
		// At the original leaf boundary: RAX = pool+8, RCX = model; RSP is 8 mod16.
		// Includes Win64 shadow space. No pointers into this frame escape.
		a.sub(rsp,0xb8);
		a.mov(qword_ptr(rsp,0x20),rax); a.mov(qword_ptr(rsp,0x28),rdx);
		a.mov(qword_ptr(rsp,0x30),r8); a.mov(qword_ptr(rsp,0x38),r9);
		a.mov(qword_ptr(rsp,0x40),r10); a.mov(qword_ptr(rsp,0x48),r11);
		for (int i=0;i<6;++i) a.movdqu(xmmword_ptr(rsp,0x50+i*16),xmm(i));
		a.mov(rdx,rax); a.mov(rax,resolver); a.call(rax);
		a.mov(rcx,rax);
		a.mov(rax,qword_ptr(rsp,0x20)); a.mov(rdx,qword_ptr(rsp,0x28));
		a.mov(r8,qword_ptr(rsp,0x30)); a.mov(r9,qword_ptr(rsp,0x38));
		a.mov(r10,qword_ptr(rsp,0x40)); a.mov(r11,qword_ptr(rsp,0x48));
		for (int i=0;i<6;++i) a.movdqu(xmm(i),xmmword_ptr(rsp,0x50+i*16));
		a.add(rsp,0xb8);
		a.embed(getter_tail.data(),getter_tail.size());
	}
}
