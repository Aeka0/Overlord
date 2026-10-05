#pragma once
#include "component/vr/gameplay/native_use_range_bridge.hpp"

namespace use_range_bridge_tests
{
	inline bool extend{};
	inline float observed_radius{};
	inline float range_policy(float ordinary)
	{observed_radius=ordinary;return extend ? std::max(ordinary,88.f) : ordinary;}

	inline int run()
	{
		int failures{};
		const auto check=[&](bool ok,const char* name) {
			if (!ok) {++failures;std::cerr<<"use range bridge FAIL: "<<name<<'\n';}
		};
		asmjit::JitRuntime runtime;
		const auto jit=[&](auto emit) {
			asmjit::CodeHolder code;code.init(runtime.environment());
			utils::hook::assembler a(&code);emit(a);
			void* fn{};check(runtime.add(&fn,&code)==asmjit::kErrorOk,"JIT allocation");return fn;
		};
		using namespace asmjit::x86;
		auto* original=jit([](auto& a) {
			// A one-pointer native leaf with observable alignment and deliberately
			// live volatile results. xmm6 holds the caller's original range.
			a.movss(xmm0,dword_ptr(rcx));a.mov(dword_ptr(rcx),0x3f800000);
			a.mov(rax,rsp);a.and_(rax,15);a.mov(dword_ptr(rcx,4),eax);
			a.mov(rax,0x12345678);a.mov(rcx,0x11223344);a.mov(rdx,0x22334455);
			a.mov(r8,0x33445566);a.mov(r9,0x44556677);a.mov(r10,0x55667788);a.mov(r11,0x66778899);
			for (int i=1;i<6;++i) a.pcmpeqd(xmm(i),xmm(i));
			a.ret();
		});
		auto* hostile=jit([](auto& a) {
			a.sub(rsp,0x28);a.mov(rax,reinterpret_cast<std::uintptr_t>(range_policy));a.call(rax);a.add(rsp,0x28);
			for (const auto reg:{rax,rcx,rdx,r8,r9,r10,r11}) a.xor_(reg,reg);
			for (int i=1;i<6;++i) a.pxor(xmm(i),xmm(i));
			a.ret();
		});
		auto* bridge=jit([&](auto& a) {
			vr::gameplay::interaction::native::emit_range_bridge(a,
				reinterpret_cast<std::uintptr_t>(original),reinterpret_cast<std::uintptr_t>(hostile));
		});
		auto* harness=jit([](auto& a) {
			a.push(rbx);a.push(rsi);a.sub(rsp,0x38);
			a.movaps(ptr(rsp,0x20),xmm6);a.mov(qword_ptr(rsp,0x30),rcx);
			a.mov(rbx,r8);a.mov(rsi,rdx);a.mov(eax,0x41800000);a.movd(xmm6,eax); // 16.f
			a.mov(rcx,rsi);a.call(qword_ptr(rsp,0x30));
			const std::array registers{rax,rcx,rdx,r8,r9,r10,r11};
			for (unsigned i=0;i<registers.size();++i) a.mov(qword_ptr(rbx,i*8),registers[i]);
			for (int i=0;i<7;++i) a.movdqu(xmmword_ptr(rbx,64+16*i),xmm(i));
			a.movaps(xmm6,ptr(rsp,0x20));a.add(rsp,0x38);a.pop(rsi);a.pop(rbx);a.ret();
		});
		// This is a standalone test process, never a game process. Force the exact
		// new native callsite and an entry far outside its signed 32-bit range.
		auto* sites=static_cast<std::uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(0x140520000),
			0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
		void* far_entry{};
		for (std::uintptr_t attempt=0;attempt<32 && !far_entry;++attempt)
			far_entry=VirtualAlloc(reinterpret_cast<void*>(0x50000000000+attempt*0x10000),
				0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
		check(sites && far_entry,"bounded synthetic native/far addresses");
		if (sites && far_entry && original && hostile && bridge && harness)
		{
			auto* call=sites+0x6d4d;
			std::array<std::uint8_t,21> guard;guard.fill(0xcc);
			utils::hook::copy(call-8,guard.data(),guard.size());
			utils::hook::jump(far_entry,bridge,true);
			bool rejected{};
			try {utils::hook::call(call,far_entry);} catch (const std::runtime_error&) {rejected=true;}
			check(rejected && std::memcmp(call-8,guard.data(),guard.size())==0,"old route reproduces rel32 exception without corrupting instructions");
			auto* relay=utils::hook::create_far_jump<0x140000000>(far_entry);
			check(relay && !utils::hook::is_relatively_far(call,relay),"existing native relay reaches forced far code");
			if (relay && !utils::hook::is_relatively_far(call,relay))
			{
				utils::hook::call(call,relay);
				check(utils::hook::follow_branch(call)==relay &&
					std::memcmp(call-8,guard.data(),8)==0 && std::memcmp(call+5,guard.data()+13,8)==0,
					"only five CALL bytes replaced; neighboring comparison untouched");
				constexpr std::uint8_t enter[]{0x48,0x83,0xec,0x28},leave[]{0x48,0x83,0xc4,0x28,0xc3};
				utils::hook::copy(call-sizeof(enter),enter,sizeof(enter));utils::hook::copy(call+5,leave,sizeof(leave));
				using probe=void(*)(void*,void*,void*);
				const auto invoke=reinterpret_cast<probe>(harness);
				for (const bool active:{false,true})
				{
					extend=active;
					std::array<std::uint64_t,22> expected{},observed{};
					struct input {float distance;std::uint32_t alignment;} reference{80,0},actual=reference;
					invoke(original,&reference,expected.data());invoke(call-sizeof(enter),&actual,observed.data());
					check(actual.distance==1 && actual.alignment==8 && reference.alignment==8,"normalizer retains pointer writes and Win64 stack alignment through relay");
					check(observed_radius==16,"policy sees the original native per-type radius");
					const float radius=active ? 88.f : 16.f;
					std::memcpy(reinterpret_cast<std::byte*>(expected.data())+64+6*16,&radius,sizeof(radius));
					check(expected==observed,"real length and volatile outputs preserved; only requested xmm6 radius changes");
				}
			}
		}
		if (far_entry) VirtualFree(far_entry,0,MEM_RELEASE);
		if (sites) VirtualFree(sites,0,MEM_RELEASE);
		for (auto* fn:{harness,bridge,hostile,original}) if (fn) runtime.release(fn);
		if (!failures) std::cout<<"Use range bridge tests: PASS (forced far target, rel32 relay, native range/ABI)\n";
		return failures;
	}
}
