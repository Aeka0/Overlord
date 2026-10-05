#pragma once
#include "component/vr/gameplay/native_animation_index_bridge.hpp"
#include <asmjit/core/jitruntime.h>
#include <array>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <utils/hook.hpp>

namespace animation_index_bridge_tests
{
	using vr::gameplay::weapons::equip_presentation::index_query;
	inline index_query captured;
	inline std::uint64_t policy(const index_query* query)
	{
		captured=*query;
		return query->result+64;
	}
	inline int run()
	{
		int failures{};
		const auto check=[&](bool ok,const char* name) {
			if (!ok) { ++failures; std::cerr<<"animation bridge FAIL: "<<name<<'\n'; }
		};
		asmjit::JitRuntime runtime;
		const auto jit=[&](auto emit) {
			asmjit::CodeHolder code; code.init(runtime.environment());
			asmjit::x86::Assembler a(&code); emit(a);
			void* fn{}; check(runtime.add(&fn,&code)==asmjit::kErrorOk,"JIT allocation"); return fn;
		};
		using namespace asmjit::x86;
		// The synthetic six-argument native function leaves deliberate nonstandard
		// volatile outputs. Include entry stack alignment in the result.
		auto* original=jit([](auto& a) {
			a.mov(rax,rcx); a.add(rax,rdx); a.add(rax,r8); a.add(rax,r9);
			a.add(rax,qword_ptr(rsp,0x28)); a.add(rax,qword_ptr(rsp,0x30));
			a.mov(r11,rsp); a.and_(r11,15); a.add(rax,r11);
			a.mov(rcx,0x11223344); a.mov(rdx,0x22334455); a.mov(r8,0x33445566);
			a.mov(r9,0x44556677); a.mov(r10,0x55667788); a.mov(r11,0x66778899);
			for (int i=0;i<6;++i) a.pcmpeqd(xmm(i),xmm(i));
			a.cmp(rax,rax); a.ret();
		});
		auto* hostile=jit([](auto& a) {
			a.sub(rsp,0x28); a.mov(rax,reinterpret_cast<std::uintptr_t>(policy)); a.call(rax); a.add(rsp,0x28);
			a.xor_(rcx,rcx); a.xor_(rdx,rdx); a.xor_(r8,r8); a.xor_(r9,r9); a.xor_(r10,r10); a.xor_(r11,r11);
			for (int i=0;i<6;++i) a.pxor(xmm(i),xmm(i));
			a.stc(); a.ret();
		});
		auto* bridge=jit([&](auto& a) {
			vr::gameplay::weapons::equip_presentation::emit_index_bridge(a,
				reinterpret_cast<std::uintptr_t>(original),reinterpret_cast<std::uintptr_t>(hostile));
		});
		auto* harness=jit([](auto& a) {
			a.push(rbx); a.push(rsi); a.sub(rsp,0x38);
			a.mov(rbx,r8); a.mov(rsi,rdx); a.mov(qword_ptr(rsp,0x30),rcx);
			a.mov(rax,qword_ptr(rsi,32)); a.mov(qword_ptr(rsp,0x20),rax);
			a.mov(rax,qword_ptr(rsi,40)); a.mov(qword_ptr(rsp,0x28),rax);
			a.mov(rcx,qword_ptr(rsi,0)); a.mov(rdx,qword_ptr(rsi,8));
			a.mov(r8,qword_ptr(rsi,16)); a.mov(r9,qword_ptr(rsi,24));
			a.call(qword_ptr(rsp,0x30));
			a.mov(qword_ptr(rbx,0),rax); a.mov(qword_ptr(rbx,8),rcx); a.mov(qword_ptr(rbx,16),rdx);
			a.mov(qword_ptr(rbx,24),r8); a.mov(qword_ptr(rbx,32),r9); a.mov(qword_ptr(rbx,40),r10); a.mov(qword_ptr(rbx,48),r11);
			a.pushfq(); a.pop(rax); a.mov(qword_ptr(rbx,56),rax);
			for (int i=0;i<6;++i) a.movdqu(xmmword_ptr(rbx,64+i*16),xmm(i));
			a.add(rsp,0x38); a.pop(rsi); a.pop(rbx); a.ret();
		});
		if (original && hostile && bridge && harness)
		{
			using probe=void(*)(void*,const index_query*,void*);
			const auto invoke=reinterpret_cast<probe>(harness);
			for (const auto input: {index_query{11,23,37,41,53,67},
				index_query{0x1111222233334444,0x1122334400000002,130,0,0,1}})
			{
				std::array<std::uint64_t,20> expected{},observed{};
				invoke(original,&input,expected.data()); invoke(bridge,&input,observed.data());
				check(expected[0]==input.ps+input.action+input.weapon+input.alternate+input.side+input.option+8,
					"six native arguments and aligned Win64 callee entry");
				index_query query=input; query.result=expected[0];
				check(std::memcmp(&captured,&query,sizeof(query))==0,"policy sees original arguments and native result");
				expected[0]+=64;
				check(expected==observed,"only RAX changes; native GPR/flags/SIMD survive hostile policy");
			}
			// Reproduce the deployment-only failure: five-byte native CALLs live
			// near 0x140000000 while generated code may be many GB away. Allocate
			// synthetic sites at the real offsets in THIS TEST PROCESS only. A far
			// entry guarantees the failure condition even if JIT allocation changes.
			auto* sites=static_cast<std::uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(0x1403b0000),
				0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
			void* far_entry{};
			for (std::uintptr_t attempt=0;attempt<32 && !far_entry;++attempt)
				far_entry=VirtualAlloc(reinterpret_cast<void*>(0x40000000000+attempt*0x10000),
					0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
			check(sites && far_entry,"bounded synthetic native sites / far entry allocation");
			if (sites && far_entry)
			{
				utils::hook::jump(far_entry,bridge,true);
				auto* relay=utils::hook::create_far_jump<0x140000000>(far_entry);
				const index_query input{11,23,37,41,53,67};
				std::array<std::uint64_t,20> expected{},observed{};
				invoke(original,&input,expected.data()); expected[0]+=64;
				for (const auto offset:{0xac7e,0xb8c6,0x14c53,0x15688,0x15c21})
				{
					auto* call=sites+offset;
					std::memset(call,0xcc,5);
					bool rejected{};
					try { utils::hook::call(call,far_entry); }
					catch (const std::runtime_error&) { rejected=true; }
					check(rejected && call[0]==0xcc && call[4]==0xcc,"old direct rel32 route rejected before writing");
					check(relay && !utils::hook::is_relatively_far(call,relay),"shared relay reachable from every native callsite");
					if (!relay || utils::hook::is_relatively_far(call,relay)) continue;
					utils::hook::call(call,relay);
					check(utils::hook::follow_branch(call)==relay,"patched rel32 resolves to near relay");
					// Turn the synthetic instruction into a six-argument forwarding
					// function. Its prologue copies both stack arguments, aligns CALL,
					// and its LEA/RET epilogue preserves the native flags and outputs.
					asmjit::CodeHolder code; code.init(runtime.environment());
					asmjit::x86::Assembler a(&code);
					a.sub(rsp,0x38);
					a.mov(rax,qword_ptr(rsp,0x60)); a.mov(qword_ptr(rsp,0x20),rax);
					a.mov(rax,qword_ptr(rsp,0x68)); a.mov(qword_ptr(rsp,0x28),rax);
					const auto length=code.code_size();
					utils::hook::copy(call-length,code.text_section()->data(),length);
					constexpr std::uint8_t leave[]{0x48,0x8d,0x64,0x24,0x38,0xc3};
					utils::hook::copy(call+5,leave,sizeof(leave));
					invoke(call-length,&input,observed.data());
					check(expected==observed,"real rel32 CALL -> near relay -> far bridge preserves six-argument native contract");
				}
			}
			if (far_entry) VirtualFree(far_entry,0,MEM_RELEASE);
			if (sites) VirtualFree(sites,0,MEM_RELEASE);
		}
		for (auto* fn:{harness,bridge,hostile,original}) if (fn) runtime.release(fn);
		if (!failures) std::cout<<"Animation index bridge tests: PASS (native ABI, five rel32 sites, forced far target)\n";
		return failures;
	}
}
