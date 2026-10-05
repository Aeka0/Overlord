#pragma once
#include "component/vr/gameplay/sequences/oilrig_angle_bridge.hpp"
#include <intrin.h>

namespace oilrig_angle_bridge_tests
{
	inline unsigned observed_index{};
	inline unsigned observed_mode{};
	inline const void* observed_owner{};
	inline std::uintptr_t observed_alignment{};
	inline float callback(unsigned index,const void* owner,unsigned mode)
	{observed_index=index;observed_owner=owner;observed_mode=mode;observed_alignment=reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress())&15;return index==3 ? 95.f : 83.f;}
	inline int run()
	{
		int failures{};asmjit::JitRuntime runtime;
		const auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<"oilrig bridge FAIL: "<<message<<'\n';}};
		const auto jit=[&](auto emit){asmjit::CodeHolder code;code.init(runtime.environment());utils::hook::assembler a(&code);
			emit(a);void* result{};check(runtime.add(&result,&code)==asmjit::kErrorOk,"JIT allocation");return result;};
		using namespace asmjit::x86;
		for(const bool link:{true,false})
		{
			auto* bridge=jit([&](auto& a){vr::gameplay::sequences::oilrig::emit_angle_bridge(a,link,reinterpret_cast<void*>(callback));});
			auto* harness=jit([&](auto& a){
				a.push(rbx);a.push(rsi);a.push(r14);a.sub(rsp,0x30);a.mov(qword_ptr(rsp,0x20),r9);a.mov(r14d,1);
				a.mov(rax,rcx);a.mov(rsi,link ? r8 : rdx);a.mov(rbx,link ? rdx : r8);a.mov(ecx,edx);a.call(rax);
				a.mov(rax,qword_ptr(rsp,0x20));a.mov(qword_ptr(rax),rsi);a.mov(qword_ptr(rax,8),rbx);
				a.add(rsp,0x30);a.pop(r14);a.pop(rsi);a.pop(rbx);a.ret();});
			auto* relay=utils::hook::create_preserving_near_jump(0x1404FC856,bridge);
			check(relay && !utils::hook::is_relatively_far(reinterpret_cast<void*>(0x1404FC856),relay),"relay is within native CALL range");
			if(relay && harness)
			{
				using invoke=float(*)(void*,unsigned,void*,void*);
				for(unsigned index:{3u,4u})
				{
					std::array<std::uintptr_t,2> preserved{};int owner{};
					const float result=reinterpret_cast<invoke>(harness)(relay,index,&owner,preserved.data());
					check(result==(index==3 ? 95.f : 83.f) && observed_index==index && observed_owner==&owner,"owner/index forwarding and float return across near relay");
					check(observed_alignment==8,"Windows x64 callback stack alignment");
					check(observed_mode==(link ? 1u : 3u),"shared helper mode and separate lerp identity forwarded");
					check(preserved[link ? 0 : 1]==reinterpret_cast<std::uintptr_t>(&owner) && preserved[link ? 1 : 0]==index,"native nonvolatile owner registers preserved");
				}
			}
			if(relay)VirtualFree(relay,0,MEM_RELEASE);
		}
		if(!failures)std::cout<<"Oilrig angle bridges: PASS (owner, float result, Win64 ABI, near relay)\n";
		return failures;
	}
}
