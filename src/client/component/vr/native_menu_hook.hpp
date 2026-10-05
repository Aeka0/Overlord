#pragma once
#include <utils/hook.hpp>
#include <cstdint>

namespace vr::native_menu
{
	inline void emit_render_bridge(asmjit::x86::Assembler& a,std::uintptr_t observer,std::uintptr_t continuation)
	{
		// Entry is a JMP from an already aligned native CALL site. Its existing
		// shadow space and fifth/sixth arguments belong to this same stack frame.
		a.call(observer);
		a.mov(asmjit::x86::rbx,asmjit::x86::qword_ptr(asmjit::x86::rsp,0xd8));
		a.jmp(continuation);
	}
	inline bool install_render_bridge(std::uintptr_t site,const void* bridge)
	{
		// JIT allocation is not constrained to rel32 range of H2. Reuse the
		// register-preserving near relay; a 12-byte far jump would overrun this
		// ten-byte native window. Prepare everything before touching the site.
		const auto relay=utils::hook::create_preserving_near_jump(site,bridge);
		if(!relay)return false;
		utils::hook::jump(site,relay);
		utils::hook::nop(site+5,5);
		return true;
	}
}
