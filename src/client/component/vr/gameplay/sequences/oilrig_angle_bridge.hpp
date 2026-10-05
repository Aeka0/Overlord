#pragma once
#include <utils/hook.hpp>

namespace vr::gameplay::sequences::oilrig
{
	// Existing CALL supplies ECX=index and a Windows x64 call frame. The
	// respective native callers retain gentity in RSI (link) / RBX (lerp).
	// Tail-call the adapter without changing the return address or stack layout.
	inline void emit_angle_bridge(utils::hook::assembler& a, bool link, void* callback)
	{
		a.mov(rdx, link ? rsi : rbx);
		// The link helper is shared with other link modes. R14D is its mode;
		// 1 is playerlinktodelta. Lerp uses a separate marker, not helper mode 0.
		if (link)
			a.mov(r8d, r14d);
		else
			a.mov(r8d, 3);
		a.mov(rax, reinterpret_cast<std::uintptr_t>(callback));
		a.jmp(rax);
	}
}
