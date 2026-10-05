#pragma once
#include <asmjit/x86/x86assembler.h>
#include <cstdint>

namespace vr::gameplay::weapons::equip_presentation
{
	struct index_query
	{
		std::uint64_t ps{}, action{}, weapon{}, alternate{}, side{}, option{}, result{};
	};
	static_assert(sizeof(index_query)==56);
	// Preserve the ORIGINAL native callee's post-call registers/flags/SIMD, not
	// merely the public Win64 ABI. H2 uses whole-program optimized callers. The
	// policy is allowed to replace RAX only; it never receives writable game data.
	inline void emit_index_bridge(asmjit::x86::Assembler& a,std::uintptr_t original,std::uintptr_t policy)
	{
		using namespace asmjit::x86;
		a.sub(rsp,0x108);
		a.mov(qword_ptr(rsp,0x30),rcx); a.mov(qword_ptr(rsp,0x38),rdx);
		a.mov(qword_ptr(rsp,0x40),r8); a.mov(qword_ptr(rsp,0x48),r9);
		a.mov(rax,qword_ptr(rsp,0x130)); a.mov(qword_ptr(rsp,0x20),rax); a.mov(qword_ptr(rsp,0x50),rax);
		a.mov(rax,qword_ptr(rsp,0x138)); a.mov(qword_ptr(rsp,0x28),rax); a.mov(qword_ptr(rsp,0x58),rax);
		a.mov(rax,original); a.call(rax);
		a.mov(qword_ptr(rsp,0x60),rax); a.mov(qword_ptr(rsp,0x68),rcx); a.mov(qword_ptr(rsp,0x70),rdx);
		a.mov(qword_ptr(rsp,0x78),r8); a.mov(qword_ptr(rsp,0x80),r9);
		a.mov(qword_ptr(rsp,0x88),r10); a.mov(qword_ptr(rsp,0x90),r11);
		a.pushfq(); a.pop(qword_ptr(rsp,0x98));
		for (int i=0;i<6;++i) a.movdqu(xmmword_ptr(rsp,0xa0+i*16),xmm(i));
		a.lea(rcx,ptr(rsp,0x30)); a.mov(rax,policy); a.call(rax);
		a.mov(rcx,qword_ptr(rsp,0x68)); a.mov(rdx,qword_ptr(rsp,0x70));
		a.mov(r8,qword_ptr(rsp,0x78)); a.mov(r9,qword_ptr(rsp,0x80));
		a.mov(r10,qword_ptr(rsp,0x88)); a.mov(r11,qword_ptr(rsp,0x90));
		for (int i=0;i<6;++i) a.movdqu(xmm(i),xmmword_ptr(rsp,0xa0+i*16));
		a.push(qword_ptr(rsp,0x98)); a.popfq();
		a.lea(rsp,ptr(rsp,0x108)); a.ret();
	}
}
