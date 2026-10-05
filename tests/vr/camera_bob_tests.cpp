#include <std_include.hpp>
#include "component/vr/camera_bob_bridge.hpp"
#include <utils/hook.hpp>
#include <asmjit/core/jitruntime.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
	bool camera_scope{};
	struct native_dvar { std::byte header[16]{}; float value{}; };
	float policy(float amplitude,const native_dvar* native)
	{return vr::camera_bob::weaponless_scale(amplitude,native->value,camera_scope);}
}

int main()
{
	try
	{
		const auto check=[](bool ok,const char* name){if(!ok)throw std::runtime_error(name);};
		using namespace asmjit::x86;
		asmjit::JitRuntime runtime;
		const auto jit=[&](auto emit) {
			asmjit::CodeHolder code;code.init(runtime.environment());
			Assembler a(&code);emit(a);void* fn{};
			check(runtime.add(&fn,&code)==asmjit::kErrorOk,"JIT allocation");return fn;
		};
		std::uint64_t alignment{};
		auto* hostile=jit([&](auto& a) {
			a.mov(rax,reinterpret_cast<std::uintptr_t>(&alignment));
			a.mov(r10,rsp);a.and_(r10,15);a.mov(qword_ptr(rax),r10);
			a.sub(rsp,0x28);a.mov(rax,reinterpret_cast<std::uintptr_t>(policy));a.call(rax);a.add(rsp,0x28);
			for(const auto reg:{rax,rcx,rdx,r8,r9,r10,r11})a.xor_(reg,reg);
			for(int i=1;i<6;++i)a.pxor(xmm(i),xmm(i));
			a.clc();a.ret();
		});
		const std::array<std::uint32_t,4> seed{0x40400000,0x11223344,0x55667788,0xaabbccdd};
		auto* harness=jit([&](auto& a) {
			a.push(rbx);a.push(rsi);a.sub(rsp,0x58);
			a.movdqu(xmmword_ptr(rsp,0x30),xmm6);a.movdqu(xmmword_ptr(rsp,0x40),xmm7);
			a.mov(qword_ptr(rsp,0x20),rcx);a.mov(rsi,rdx);a.mov(rbx,r8);
			a.mov(r11,reinterpret_cast<std::uintptr_t>(seed.data()));
			for(int i=0;i<8;++i)a.movdqu(xmm(i),xmmword_ptr(r11));
			a.mov(rax,rsi);a.mov(rcx,11);a.mov(rdx,22);a.mov(r8,33);a.mov(r9,44);a.mov(r10,55);a.mov(r11,66);
			a.cmp(rax,rax);a.stc();a.call(qword_ptr(rsp,0x20));
			const Gp regs[]{rax,rcx,rdx,r8,r9,r10,r11};
			for(int i=0;i<7;++i)a.mov(qword_ptr(rbx,i*8),regs[i]);
			a.pushfq();a.pop(qword_ptr(rbx,56));
			for(int i=0;i<8;++i)a.movdqu(xmmword_ptr(rbx,64+i*16),xmm(i));
			a.movdqu(xmm6,xmmword_ptr(rsp,0x30));a.movdqu(xmm7,xmmword_ptr(rsp,0x40));
			a.add(rsp,0x58);a.pop(rsi);a.pop(rbx);a.ret();
		});
		using probe=void(*)(void*,const native_dvar*,void*);
		const auto invoke=reinterpret_cast<probe>(harness);
		for(int lane:{6,7})
		{
			auto* original=jit([&](auto& a){a.mulss(xmm(lane),dword_ptr(rax,16));a.ret();});
			auto* bridge=jit([&](auto& a){vr::camera_bob::emit_scale_bridge(a,lane,reinterpret_cast<std::uintptr_t>(hostile));});
			for(bool scope:{false,true})for(float factor:{0.f,0.25f,1.f,2.f})
			{
				camera_scope=scope;native_dvar native{};native.value=factor;
				std::array<std::uint64_t,24> expected{},observed{};
				invoke(original,&native,expected.data());invoke(bridge,&native,observed.data());
				const float result=3.f*(scope && factor==0.f?1.f:factor);
				std::memcpy(reinterpret_cast<std::byte*>(expected.data())+64+lane*16,&result,sizeof(result));
				check(expected==observed,"Bridge must preserve GPRs, flags, every other SIMD lane, and stack");
				check(alignment==8,"Policy must enter with standard Win64 stack alignment");
			}
			// Exercise the installed path, including the near relay. The generic
			// mov-RAX relay previously destroyed the live dvar pointer before the
			// otherwise-correct bridge could save it.
			auto* site=static_cast<std::uint8_t*>(utils::memory::allocate_near(0x140000000,0x1000,PAGE_EXECUTE_READWRITE));
			auto* relay=utils::hook::create_preserving_near_jump(0x140000000,bridge);
			check(site && relay,"Native CALL and preserving relay allocation");
			MEMORY_BASIC_INFORMATION region{};
			check(VirtualQuery(relay,&region,sizeof(region)) && region.Protect==PAGE_EXECUTE_READ,
				"Preserving relay is executable and read-only after publication");
			// LEA aligns the stack without changing the incoming flags.
			const std::uint8_t enter[]{0x48,0x8d,0x64,0x24,0xd8};
			const std::uint8_t leave[]{0x48,0x8d,0x64,0x24,0x28,0xc3};
			utils::hook::copy(site,enter,sizeof(enter));
			utils::hook::call(site+5,relay);
			utils::hook::copy(site+10,leave,sizeof(leave));
			for(bool scope:{false,true})for(float factor:{0.f,0.25f,1.f,2.f})
			{
				camera_scope=scope;native_dvar native{};native.value=factor;
				std::array<std::uint64_t,24> expected{},observed{};
				invoke(bridge,&native,expected.data());invoke(site,&native,observed.data());
				check(expected==observed,"Installed CALL -> near relay -> bridge must preserve the dvar pointer and full ABI");
			}
			// Keep the original failure reproducible: the ordinary relay overwrites
			// RAX with readable bridge code, so this is safe but observably wrong.
			utils::hook::call(site+5,utils::hook::create_far_jump<0x140000000>(bridge));
			camera_scope=true;native_dvar native{};native.value=1.f;
			std::array<std::uint64_t,24> expected{},broken{};
			invoke(bridge,&native,expected.data());invoke(site,&native,broken.data());
			check(expected!=broken && broken[0]==reinterpret_cast<std::uintptr_t>(bridge),
				"Regression reproduces the old relay destroying the native dvar pointer");
			VirtualFree(site,0,MEM_RELEASE);VirtualFree(relay,0,MEM_RELEASE);
			runtime.release(bridge);runtime.release(original);
		}
		runtime.release(harness);runtime.release(hostile);
		std::cout<<"Camera bob tests passed: both native lanes, full rel32 relay route, old failure reproduced, hostile callback ABI and native tuning\n";
		return 0;
	}
	catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
