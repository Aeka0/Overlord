#pragma once
#include <asmjit/x86/x86assembler.h>
#include <array>
#include <cstdint>

namespace vr::gameplay::ads_sprint
{
	enum class boundary : std::uint32_t { sustain_flags, sustain_buttons, start_buttons, start_flags };
	inline constexpr std::uint32_t ads_flag = 0x10, ads_button = 0x800;
	inline constexpr std::uint32_t sprint_flag = 0x4000, ads_slow_walk_flag = 0x40;
	// PM updates sprint before deriving the ADS slow-walk flag. Suppress only
	// that derived penalty while native sprint is actually active; a sprint
	// button alone must not bypass ordinary aiming speed or native eligibility.
	inline constexpr std::uint32_t movement_flags(std::uint32_t native_flags, bool enabled) noexcept
	{
		return enabled && (native_flags & sprint_flag)
			? native_flags & ~ads_slow_walk_flag : native_flags;
	}
	inline constexpr bool button_boundary(boundary point) noexcept
	{ return point == boundary::sustain_buttons || point == boundary::start_buttons; }
	inline constexpr std::uint32_t filter_mask(std::uint32_t mask, boundary point, bool enabled) noexcept
	{ return enabled ? mask & ~(button_boundary(point) ? ads_button : ads_flag) : mask; }

	struct native_site
	{
		std::uintptr_t address, resume, blocked, alternate;
		std::array<std::uint8_t, 10> bytes;
		std::size_t size;
	};
	// H2 SP: PM sprint continuation/start predicates. Verify every site before
	// installing any of them. +0x2ac is lean, not ADS; it is deliberately untouched.
	inline constexpr std::array sites{
		native_site{0x14068C112, 0x14068C117, 0x14068C18A, 0, {0x44,0x85,0xc9,0x75,0x73}, 5},
		native_site{0x14068C15C, 0x14068C166, 0x14068C18A, 0, {0x85,0xd7,0x75,0x2a,0x8b,0x8b,0xc0,0x02,0,0}, 10},
		native_site{0x14068C1FB, 0x14068C204, 0x14068C1B6, 0, {0x85,0xcf,0x75,0xb7,0xb9,0x18,0x80,0,0}, 9},
		native_site{0x14068C21C, 0x14068C226, 0x14068C1B6, 0x14068C239, {0x85,0xd1,0x75,0x96,0x0f,0xba,0xe2,0x0d,0x73,0x13}, 10}};

	// Entered by JMP with the native RSP aligned to 16. The callback receives
	// (PS, original mask, tested value, boundary), returns their filtered AND.
	// No PS/command writes. Preserve all GPRs/SIMD lanes; only the original TEST
	// flags and explicitly displaced native instructions may change state.
	inline void emit_bridge(asmjit::x86::Assembler& a, boundary point, std::uintptr_t policy,
		std::uintptr_t resume, std::uintptr_t blocked, std::uintptr_t alternate = 0)
	{
		using namespace asmjit::x86;
		const auto reject = a.new_label(), done = a.new_label(), other = a.new_label();
		const auto resume_address = a.new_label(), blocked_address = a.new_label(), alternate_address = a.new_label();
		a.sub(rsp, 0xc0);
		const Gp registers[]{rax, rcx, rdx, r8, r9, r10, r11};
		for (int i = 0; i < 7; ++i) a.mov(qword_ptr(rsp, 0x20 + i * 8), registers[i]);
		for (int i = 0; i < 6; ++i) a.movdqu(xmmword_ptr(rsp, 0x60 + i * 16), xmm(i));
		if (button_boundary(point)) a.mov(r8d, edi);
		else a.mov(r8d, point == boundary::sustain_flags ? r9d : edx);
		if (point != boundary::sustain_buttons) a.mov(edx, ecx);
		a.mov(rcx, rbx);
		a.mov(r9d, static_cast<std::uint32_t>(point));
		a.mov(rax, policy); a.call(rax); a.test(eax, eax);
		for (int i = 0; i < 6; ++i) a.movdqu(xmm(i), xmmword_ptr(rsp, 0x60 + i * 16));
		for (int i = 0; i < 7; ++i) a.mov(registers[i], qword_ptr(rsp, 0x20 + i * 8));
		a.lea(rsp, ptr(rsp, 0xc0));
		a.jnz(reject);
		if (point == boundary::sustain_buttons) a.mov(ecx, dword_ptr(rbx, 0x2c0));
		if (point == boundary::start_buttons) a.mov(ecx, 0x8018);
		if (point == boundary::start_flags) { a.bt(edx, 13); a.jnc(other); }
		a.bind(done); a.jmp(qword_ptr(resume_address));
		a.bind(reject); a.jmp(qword_ptr(blocked_address));
		if (point == boundary::start_flags) { a.bind(other); a.jmp(qword_ptr(alternate_address)); }
		a.bind(resume_address); a.embed_uint64(resume);
		a.bind(blocked_address); a.embed_uint64(blocked);
		if (point == boundary::start_flags) { a.bind(alternate_address); a.embed_uint64(alternate); }
	}
}
