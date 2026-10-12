#pragma once
#include <asmjit/x86/x86assembler.h>
#include <cstdint>
#include <array>

namespace vr::camera_bob
{
	inline constexpr std::uintptr_t horizontal = 0x1406AB290, vertical = 0x1406AB3C0;
	struct camera_call { std::uintptr_t address, target; };
	// The camera applies positional bob and two ADS angular contributions.
	// All seven calls share the option; idle sway, recoil and viewmodel bob
	// remain native. The angular compositor's only native caller is 3ADEBC.
	inline constexpr std::array camera_calls{
		camera_call{0x1403ADF80, vertical}, camera_call{0x1403ADF9D, horizontal},
		camera_call{0x1406AACC9, vertical}, camera_call{0x1406AACE9, horizontal},
		camera_call{0x1406AAE15, vertical}, camera_call{0x1406AAE31, horizontal},
		camera_call{0x1406AAE63, horizontal}};
	inline std::array<std::uint8_t, 5> call_bytes(camera_call site) noexcept
	{
		const auto relative = static_cast<std::uint32_t>(site.target - site.address - 5);
		return {0xe8, static_cast<std::uint8_t>(relative), static_cast<std::uint8_t>(relative >> 8),
			static_cast<std::uint8_t>(relative >> 16), static_cast<std::uint8_t>(relative >> 24)};
	}
	template <typename Native> float sample_camera(bool local_vr, bool enabled, Native&& native)
	{
		return local_vr && !enabled ? 0.f : native();
	}
	// The native weaponless factors default to zero. Preserve an explicit
	// nonzero native tuning; only the camera call's neutral fallback becomes 1.
	inline float weaponless_scale(float amplitude,float native_scale,bool camera_scope)noexcept
	{return amplitude*(camera_scope && native_scale==0.f?1.f:native_scale);}
	// Replaces one five-byte MULSS, not the native step/stance calculation.
	// Entry is an inserted CALL from an aligned Win64 native function. Preserve
	// every live volatile register, flags and the upper lanes of XMM6/XMM7.
	inline void emit_scale_bridge(asmjit::x86::Assembler& a,int lane,std::uintptr_t policy)
	{
		using namespace asmjit::x86;
		a.pushfq();a.sub(rsp,0x100);
		const asmjit::x86::Gp registers[]{rax,rcx,rdx,r8,r9,r10,r11};
		for(int i=0;i<7;++i)a.mov(qword_ptr(rsp,0x20+i*8),registers[i]);
		for(int i=0;i<6;++i)a.movdqu(xmmword_ptr(rsp,0x60+i*16),xmm(i));
		a.mov(rdx,rax);a.movss(xmm0,xmm(lane));
		a.mov(rax,policy);a.call(rax);a.movss(xmm(lane),xmm0);
		for(int i=0;i<6;++i)a.movdqu(xmm(i),xmmword_ptr(rsp,0x60+i*16));
		for(int i=0;i<7;++i)a.mov(registers[i],qword_ptr(rsp,0x20+i*8));
		a.push(qword_ptr(rsp,0x100));a.popfq();
		a.lea(rsp,ptr(rsp,0x108));a.ret();
	}
}
