#include <std_include.hpp>
#include "component/vr/gameplay/ads_sprint_bridge.hpp"
#include <utils/hook.hpp>
#include <asmjit/core/jitruntime.h>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace vr::gameplay::ads_sprint;
	bool enabled{};
	const void* expected_ps{};
	bool arguments_valid{true};
	std::uint32_t policy(const void* ps, std::uint32_t mask, std::uint32_t value, boundary point)
	{
		arguments_valid = arguments_valid && ps == expected_ps && static_cast<unsigned>(point) < 4;
		return filter_mask(mask, point, enabled) & value;
	}
	struct sample { std::uint32_t mask{}, value{}; };
	utils::hook::detour slow_walk_hook;
	void slow_walk_policy(void* pm)
	{
		std::byte* ps{}; std::memcpy(&ps, pm, sizeof(ps));
		std::uint32_t flags{}; std::memcpy(&flags, ps + 0x54, sizeof(flags));
		flags = movement_flags(flags, enabled);
		std::memcpy(ps + 0x54, &flags, sizeof(flags));
	}
	struct result
	{
		std::array<std::uint64_t, 7> registers{};
		std::uint64_t flags{};
		std::array<std::array<std::uint32_t, 4>, 6> xmm{};
		std::uint64_t route{};
		bool operator==(const result&) const = default;
	};
}

int main()
{
	try
	{
		const auto check = [](bool ok, const char* why) { if (!ok) throw std::runtime_error(why); };
		using namespace asmjit::x86;
		asmjit::JitRuntime runtime;
		const auto jit = [&](auto emit) {
			asmjit::CodeHolder code; code.init(runtime.environment());
			Assembler a(&code); emit(a); void* fn{};
			check(runtime.add(&fn, &code) == asmjit::kErrorOk, "JIT allocation"); return fn;
		};
		std::uint64_t alignment{};
		auto* hostile = jit([&](auto& a) {
			a.mov(rax, reinterpret_cast<std::uintptr_t>(&alignment));
			a.mov(r10, rsp); a.and_(r10, 15); a.mov(qword_ptr(rax), r10);
			a.sub(rsp, 0x28); a.mov(rax, reinterpret_cast<std::uintptr_t>(policy)); a.call(rax); a.add(rsp, 0x28);
			for (const auto reg : {rcx, rdx, r8, r9, r10, r11}) a.xor_(reg, reg);
			for (int i = 0; i < 6; ++i) a.pxor(xmm(i), xmm(i));
			a.stc(); a.ret();
		});
		const std::array<std::uint32_t, 4> seed{0x40400000, 0x11223344, 0x55667788, 0xaabbccdd};
		std::array<std::byte, 0x300> ps{};
		const int weapon_state = 27; std::memcpy(ps.data() + 0x2c0, &weapon_state, sizeof(weapon_state));
		expected_ps = ps.data();
		std::array<void*, 3> exits{};
		for (unsigned route = 0; route < exits.size(); ++route) exits[route] = jit([&](auto& a) {
			const Gp regs[]{rax, rcx, rdx, r8, r9, r10, r11};
			for (int i = 0; i < 7; ++i) a.mov(qword_ptr(rsi, i * 8), regs[i]);
			a.pushfq(); a.pop(qword_ptr(rsi, 56));
			for (int i = 0; i < 6; ++i) a.movdqu(xmmword_ptr(rsi, 64 + i * 16), xmm(i));
			a.mov(qword_ptr(rsi, offsetof(result, route)), route);
			a.lea(rsp, ptr(rsp, 8)); a.ret();
		});
		unsigned cases{};
		for (unsigned index = 0; index < sites.size(); ++index)
		{
			const auto point = static_cast<boundary>(index);
			auto* bridge = jit([&](auto& a) { emit_bridge(a, point, reinterpret_cast<std::uintptr_t>(hostile),
				reinterpret_cast<std::uintptr_t>(exits[0]), reinterpret_cast<std::uintptr_t>(exits[1]),
				reinterpret_cast<std::uintptr_t>(exits[2])); });
			// Execute the native TEST/branch/displaced instructions as the oracle.
			auto* original = jit([&](auto& a) {
				const auto blocked = a.new_label(), other = a.new_label();
				if (point == boundary::sustain_flags) a.test(ecx, r9d);
				if (point == boundary::sustain_buttons) a.test(edi, edx);
				if (point == boundary::start_buttons) a.test(edi, ecx);
				if (point == boundary::start_flags) a.test(ecx, edx);
				a.jnz(blocked);
				if (point == boundary::sustain_buttons) a.mov(ecx, dword_ptr(rbx, 0x2c0));
				if (point == boundary::start_buttons) a.mov(ecx, 0x8018);
				if (point == boundary::start_flags) { a.bt(edx, 13); a.jnc(other); }
				const auto jump = [&](void* to) { const auto label = a.new_label(); a.jmp(qword_ptr(label)); a.bind(label); a.embed_uint64(reinterpret_cast<std::uintptr_t>(to)); };
				jump(exits[0]); a.bind(blocked); jump(exits[1]);
				if (point == boundary::start_flags) { a.bind(other); jump(exits[2]); }
			});
			auto* relay = utils::hook::create_preserving_near_jump(0x140000000, bridge);
			auto* site = static_cast<std::uint8_t*>(utils::memory::allocate_near(0x140000000, 0x1000, PAGE_EXECUTE_READWRITE));
			check(relay && site, "Installed native JMP/relay allocation");
			utils::hook::jump(site, relay);
			auto* harness = jit([&](auto& a) {
				a.push(rbx); a.push(rdi); a.push(rsi); a.sub(rsp, 0x30);
				a.mov(qword_ptr(rsp, 0x20), rcx); a.mov(rsi, r8);
				a.mov(rbx, reinterpret_cast<std::uintptr_t>(ps.data()));
				a.mov(r11, reinterpret_cast<std::uintptr_t>(seed.data()));
				for (int i = 0; i < 6; ++i) a.movdqu(xmm(i), xmmword_ptr(r11));
				a.mov(edi, dword_ptr(rdx, 4)); a.mov(ecx, dword_ptr(rdx));
				a.mov(edx, point == boundary::sustain_buttons ? ecx : edi);
				a.mov(r9d, edi); a.mov(rax, 111); a.mov(r8, 222); a.mov(r10, 333); a.mov(r11, 444);
				// Emulate a native aligned instruction boundary reached through JMP.
				const auto continuation = a.new_label(); a.lea(rdi, ptr(continuation)); a.push(rdi);
				a.mov(edi, r9d); a.lea(rsp, ptr(rsp, -8)); a.jmp(qword_ptr(rsp, 0x30));
				a.bind(continuation); a.add(rsp, 0x30); a.pop(rsi); a.pop(rdi); a.pop(rbx); a.ret();
			});
			using invoke_type = void(*)(void*, const sample*, result*);
			const auto invoke = reinterpret_cast<invoke_type>(harness);
			const auto ads = button_boundary(point) ? ads_button : ads_flag;
			const auto mask = button_boundary(point) ? (point == boundary::start_buttons ? 0x4c3du : 0xcf3du) : 0x8018u;
			for (bool on : {false, true}) for (unsigned bit = 0; bit < 32; ++bit) for (bool with_ads : {false, true})
			{
				enabled = on;
				const sample actual{mask, (1u << bit) | (with_ads ? ads : 0u)};
				const sample filtered{filter_mask(mask, point, on), actual.value};
				result expected{}, observed{};
				invoke(original, &filtered, &expected); invoke(site, &actual, &observed);
				// The oracle's incoming mask was filtered; the installed hook must
				// preserve the real register unless native continuation overwrites it.
				if (point == boundary::sustain_buttons) expected.registers[2] = mask;
				else if (point != boundary::start_buttons || expected.route == 1) expected.registers[1] = mask;
				if (point == boundary::sustain_buttons) expected.registers[1] = expected.route == 1 ? mask : weapon_state;
				expected.flags &= ~0x10ull; observed.flags &= ~0x10ull; // TEST leaves AF undefined.
				check(expected == observed, "Native predicates, branch paths and full volatile ABI must match");
				check(alignment == 8 && arguments_valid, "Win64 callback alignment and arguments");
				++cases;
			}
			VirtualFree(site, 0, MEM_RELEASE); VirtualFree(relay, 0, MEM_RELEASE);
			runtime.release(harness); runtime.release(original); runtime.release(bridge);
		}
		// Full captured H2 slow-walk producer at 14068D1C0; it has no external
		// references, so replay its actual ADS/stance/weapon/perk branches.
		constexpr std::uint8_t slow_walk_code[]{
			0x48,0x8b,0x11,0x83,0x62,0x54,0xbf,0x80,0x7a,0x02,0x05,0x44,0x8b,0x42,0x54,
			0x73,0x40,0xf7,0x41,0x0c,0x00,0x08,0x00,0x00,0x74,0x37,0x41,0xf6,0xc0,0x01,
			0x75,0x31,0x41,0xf6,0xc0,0x10,0x74,0x2b,0xf7,0x42,0x58,0x00,0x30,0x00,0x00,
			0x75,0x22,0x8b,0x82,0xc0,0x02,0x00,0x00,0x83,0xe8,0x09,0x83,0xf8,0x04,0x76,
			0x14,0xf7,0x82,0x90,0x1f,0x00,0x00,0x00,0x00,0x10,0x00,0x75,0x08,0x41,0x83,
			0xc8,0x40,0x44,0x89,0x42,0x54,0xc3};
		auto* native_slow_walk = jit([&](auto& a) { a.embed(slow_walk_code, sizeof(slow_walk_code)); });
		// Reproduce the observed C++ policy aftermath: RCX becomes PS rather than
		// pmove, and all other volatile lanes/flags may be destroyed by callees.
		auto* hostile_slow_walk = jit([&](auto& a) {
			a.sub(rsp, 0x38); a.mov(qword_ptr(rsp, 0x20), rcx);
			a.mov(rax, reinterpret_cast<std::uintptr_t>(slow_walk_policy)); a.call(rax);
			a.mov(rcx, qword_ptr(rsp, 0x20)); a.mov(rcx, qword_ptr(rcx));
			for (const auto reg : {rax, rdx, r8, r9, r10, r11}) a.xor_(reg, reg);
			for (int i = 0; i < 6; ++i) a.pxor(xmm(i), xmm(i));
			a.stc(); a.lea(rsp, ptr(rsp, 0x38)); a.ret();
		});
		std::size_t slow_walk_slot{};
		auto* protected_slow_walk = jit([&](auto& a) {
			slow_walk_slot = emit_slow_walk_bridge(a, reinterpret_cast<std::uintptr_t>(hostile_slow_walk));
		});
		auto* leaf_harness = jit([&](auto& a) {
			a.push(rsi); a.sub(rsp, 0x30);
			a.mov(qword_ptr(rsp, 0x20), rcx); a.mov(rsi, r8); a.mov(rcx, rdx);
			a.mov(r11, reinterpret_cast<std::uintptr_t>(seed.data()));
			for (int i = 0; i < 6; ++i) a.movdqu(xmm(i), xmmword_ptr(r11));
			a.mov(rax, 111); a.mov(rdx, 222); a.mov(r8, 333); a.mov(r9, 444); a.mov(r10, 555); a.mov(r11, 666);
			a.call(qword_ptr(rsp, 0x20));
			const Gp regs[]{rax, rcx, rdx, r8, r9, r10, r11};
			for (int i = 0; i < 7; ++i) a.mov(qword_ptr(rsi, i * 8), regs[i]);
			a.pushfq(); a.pop(qword_ptr(rsi, offsetof(result, flags)));
			for (int i = 0; i < 6; ++i) a.movdqu(xmmword_ptr(rsi, offsetof(result, xmm) + i * 16), xmm(i));
			a.add(rsp, 0x30); a.pop(rsi); a.ret();
		});
		using leaf_invoke_type = void(*)(void*, void*, result*);
		const auto invoke_leaf = reinterpret_cast<leaf_invoke_type>(leaf_harness);
		struct fixture
		{
			std::uint32_t flags, buttons, entity_flags, weapon_state, perks;
			std::uint8_t mode;
			bool native_slow;
		};
		const fixture samples[]{
			{0x4010, 0x802, 0, 27, 0, 0, true}, // ADS + real sprint: original defect
			{0x10, 0x802, 0, 0, 0, 0, true},   // sprint requested but rejected: keep ADS speed
			{0x10, 0x800, 0, 0, 0, 0, true},   // ordinary ADS walking
			{0x4000, 2, 0, 27, 0, 0, false},
			{0x4011, 0x802, 0, 27, 0, 0, false}, // prone / mounted / reload / perk / non-gameplay
			{0x4010, 0x802, 0x3000, 27, 0, 0, false},
			{0x4010, 0x802, 0, 9, 0, 0, false},
			{0x4010, 0x802, 0, 27, 0x100000, 0, false},
			{0x4010, 0x802, 0, 27, 0, 5, false}};
		for (const auto& fixture : samples) for (bool on : {false, true})
		{
			std::array<std::byte, 0x2000> player{};
			const auto put = [&](std::size_t at, auto value) { std::memcpy(player.data() + at, &value, sizeof(value)); };
			put(2, fixture.mode); put(0x54, fixture.flags | 0x40u); put(0x58, fixture.entity_flags);
			put(0x2c0, fixture.weapon_state); put(0x1f90, fixture.perks); put(0x3cc, 1.f);
			struct { void* ps; int time; std::uint32_t buttons; } pm{player.data(), 1234, fixture.buttons};
			const auto before = player;
			result native_out{}; invoke_leaf(native_slow_walk, &pm, &native_out);
			std::uint32_t flags{}; std::memcpy(&flags, player.data() + 0x54, sizeof(flags));
			check(bool(flags & 0x40) == fixture.native_slow, "Native replay reproduces independent ADS slow-walk flag");
			auto expected = player;
			if (on && (fixture.flags & sprint_flag))
			{
				flags &= ~0x40u; std::memcpy(expected.data() + 0x54, &flags, sizeof(flags));
			}
			player = before; enabled = on;
			slow_walk_hook.create_disabled(native_slow_walk, protected_slow_walk);
			utils::hook::set(reinterpret_cast<std::uintptr_t>(protected_slow_walk) + slow_walk_slot,
				reinterpret_cast<std::uintptr_t>(slow_walk_hook.get_original()));
			slow_walk_hook.enable();
			result hooked_out{}; invoke_leaf(native_slow_walk, &pm, &hooked_out);
			native_out.flags &= ~0x10ull; hooked_out.flags &= ~0x10ull;
			check(native_out == hooked_out && hooked_out.registers[1] == reinterpret_cast<std::uintptr_t>(&pm),
				"Installed slow-walk detour preserves the native leaf outputs and pmove RCX for the following duck update");
			check(player == expected && pm.buttons == fixture.buttons,
				"Installed producer hook only suppresses ADS speed during accepted sprint; ADS fraction and commands stay intact");
			check(slow_walk_hook.clear(), "Restore native replay between cases");
			++cases;
		}
		runtime.release(leaf_harness); runtime.release(protected_slow_walk); runtime.release(hostile_slow_walk);
		runtime.release(native_slow_walk);
		std::cout << cases << " ADS sprint cases passed: predicates, native slow-walk replay, installed hooks and ABI\n";
		return 0;
	}
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
