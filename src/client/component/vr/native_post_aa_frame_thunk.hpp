#pragma once
#include <utils/hook.hpp>

namespace vr::native_post_aa
{
	enum class frame_register { eax, ecx };
	// Replace one native MOV with an inserted CALL, preserving its exact
	// register/flags contract. Both verified sites have a 16-byte aligned RSP
	// before the call; the thunk reserves Windows x64 home space and saves all
	// volatile registers, including SIMD registers, around the C++ reader.
	inline void* make_frame_read_thunk(void* reader, frame_register destination)
	{
		return utils::hook::assemble([=](utils::hook::assembler& a)
		{
			a.pushfq();
			a.push(rax); a.push(rcx); a.push(rdx);
			a.push(r8); a.push(r9); a.push(r10); a.push(r11);
			a.sub(rsp, 0x88);
			for (unsigned i = 0; i < 6; ++i) a.movdqu(xmmword_ptr(rsp, 0x20 + i * 16), asmjit::x86::xmm(i));
			a.mov(rax, reinterpret_cast<std::uint64_t>(reader));
			a.call(rax);
			for (unsigned i = 0; i < 6; ++i) a.movdqu(asmjit::x86::xmm(i), xmmword_ptr(rsp, 0x20 + i * 16));
			a.add(rsp, 0x88);
			a.mov(eax, eax); // Native MOV r32 also clears the upper 32 bits.
			a.mov(qword_ptr(rsp, destination == frame_register::ecx ? 0x28 : 0x30), rax);
			a.pop(r11); a.pop(r10); a.pop(r9); a.pop(r8);
			a.pop(rdx); a.pop(rcx); a.pop(rax);
			a.popfq();
			a.ret();
		});
	}
}
