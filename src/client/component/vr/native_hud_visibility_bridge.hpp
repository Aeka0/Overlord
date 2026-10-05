#pragma once
#include <asmjit/x86/x86assembler.h>
#include <cstdint>

namespace vr::presentation_options
{
	// Replace only `mov rax,[cg_drawHUD]`. The following native byte test and
	// branch remain intact. No calls, stack alignment changes or dvar writes.
	inline void emit_hud_read(asmjit::x86::Assembler& a,std::uintptr_t flag,
		std::uintptr_t disabled,std::uintptr_t native,std::uintptr_t continuation)
	{
		using namespace asmjit::x86;
		const auto original=a.new_label(),done=a.new_label();
		a.pushfq();a.mov(rax,flag);a.cmp(byte_ptr(rax),0);a.je(original);
		a.mov(rax,disabled);a.jmp(done);
		a.bind(original);a.mov(rax,native);a.mov(rax,qword_ptr(rax));
		a.bind(done);a.popfq();a.jmp(continuation);
	}
}
