#pragma once
#include "component/vr/gameplay/launcher_aim_bridge.hpp"
#include <asmjit/core/jitruntime.h>
#include <iostream>
namespace launcher_aim_bridge_tests
{
	inline std::array<std::uint64_t,3> captured{};
	inline int policy(std::uint32_t weapon,bool alternate,const void* player)
	{captured={weapon,alternate,reinterpret_cast<std::uintptr_t>(player)};return 73;}
	inline int run()
	{
		using namespace asmjit::x86;int failures{};asmjit::JitRuntime runtime;
		const auto check=[&](bool ok,const char* why){if(!ok){++failures;std::cerr<<"launcher bridge FAIL: "<<why<<'\n';}};
		std::array<void*,4> callers{};std::array<std::uintptr_t,3> returns{};
		for(size_t n=0;n<callers.size();++n)
		{
			asmjit::CodeHolder code;code.init(runtime.environment());Assembler a(&code);
			a.push(rbx);a.push(rsi);a.sub(rsp,0x28);a.mov(rax,rcx);
			a.mov(rbx,0x1122334455667788ull);a.mov(rsi,0x8877665544332211ull);
			a.mov(ecx,29);a.mov(edx,1);a.call(rax);const auto offset=code.code_size();
			a.add(rsp,0x28);a.pop(rsi);a.pop(rbx);a.ret();
			check(runtime.add(&callers[n],&code)==asmjit::kErrorOk,"caller allocation");
			if(n<3)returns[n]=reinterpret_cast<std::uintptr_t>(callers[n])+offset;
		}
		asmjit::CodeHolder code;code.init(runtime.environment());Assembler a(&code);
		vr::gameplay::weapons::launcher::emit_aim_bridge(a,reinterpret_cast<std::uintptr_t>(policy),returns);
		void* bridge{};check(runtime.add(&bridge,&code)==asmjit::kErrorOk,"bridge allocation");
		if(bridge)for(size_t n=0;n<callers.size();++n)if(callers[n])
		{
			check(reinterpret_cast<int(*)(void*)>(callers[n])(bridge)==73,"tail dispatch preserves return path and value");
			check(captured[0]==29 && captured[1]==1,"native weapon and alternate arguments survive dispatch");
			check(captured[2]==(n<2?0x1122334455667788ull:n==2?0x8877665544332211ull:0),"only witnessed callers provide RBX/RSI player context");
		}
		if(bridge)runtime.release(bridge);for(auto* caller:callers)if(caller)runtime.release(caller);
		return failures;
	}
}
