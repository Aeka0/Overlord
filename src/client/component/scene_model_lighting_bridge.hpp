#pragma once
#include <cstdint>
#include <asmjit/x86.h>

namespace scene_model_lighting
{
	// The native reuse path has loaded the 16-bit handle into R11D. Its next
	// instruction overwrites R10D; no other register or stack slot is scratch.
	// The two adjacent globals are the static partition size and total slots.
	template<typename Assembler,typename Target>
	void emit_dynamic_handle_guard(Assembler& a,std::uintptr_t limits,
		const Target& reuse,const Target& allocate)
	{
		using namespace asmjit::x86;
		const auto miss=a.new_label();
		a.mov(r10,limits);
		a.cmp(r11d,dword_ptr(r10));
		a.jbe(miss);
		a.cmp(r11d,dword_ptr(r10,4));
		a.ja(miss);
		a.jmp(reuse);
		a.bind(miss);
		a.jmp(allocate);
	}
}
