#pragma once
#include <asmjit/x86/x86assembler.h>
#include <array>
#include <cstdint>
namespace vr::gameplay::weapons::launcher
{
	inline void emit_aim_bridge(asmjit::x86::Assembler& a,std::uintptr_t policy,
		std::array<std::uintptr_t,3> returns={0x140694449,0x14069B971,0x14069BC84})
	{
		using namespace asmjit::x86;const auto from_bx=a.new_label(),from_si=a.new_label(),dispatch=a.new_label();
		a.mov(r8,qword_ptr(rsp));
		a.mov(rax,returns[0]);a.cmp(r8,rax);a.je(from_bx);
		a.mov(rax,returns[1]);a.cmp(r8,rax);a.je(from_bx);
		a.mov(rax,returns[2]);a.cmp(r8,rax);a.je(from_si);
		a.xor_(r8d,r8d);a.jmp(dispatch);
		a.bind(from_bx);a.mov(r8,rbx);a.jmp(dispatch);
		a.bind(from_si);a.mov(r8,rsi);
		a.bind(dispatch);a.mov(rax,policy);a.jmp(rax);
	}
}
