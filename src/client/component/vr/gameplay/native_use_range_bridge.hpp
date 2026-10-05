#pragma once
#include <array>
#include <utils/hook.hpp>

namespace vr::gameplay::interaction::native
{
	// The audited caller compares the real normalized length in xmm0 against
	// the per-type radius in xmm6 immediately after this call. Preserve every
	// volatile native result across the C++ policy; replace only xmm6's low lane.
	inline void emit_range_bridge(utils::hook::assembler& a,std::uintptr_t normalize,std::uintptr_t policy)
	{
		a.sub(rsp,0xc8);
		a.call(reinterpret_cast<void*>(normalize));
		const std::array vectors{xmm0,xmm1,xmm2,xmm3,xmm4,xmm5};
		const std::array registers{rax,rcx,rdx,r8,r9,r10,r11};
		for (unsigned i=0;i<vectors.size();++i) a.movaps(ptr(rsp,0x20+16*i),vectors[i]);
		for (unsigned i=0;i<registers.size();++i) a.mov(ptr(rsp,0x80+8*i),registers[i]);
		a.movaps(xmm0,xmm6);
		a.call_aligned(reinterpret_cast<void*>(policy));
		a.movss(xmm6,xmm0);
		for (unsigned i=0;i<registers.size();++i) a.mov(registers[i],ptr(rsp,0x80+8*i));
		for (unsigned i=0;i<vectors.size();++i) a.movaps(vectors[i],ptr(rsp,0x20+16*i));
		a.add(rsp,0xc8);
		a.ret();
	}
}
