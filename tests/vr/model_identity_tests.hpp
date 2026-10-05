#pragma once
#include "component/scene_model_identity.hpp"
#include "component/scene_model_identity_bridge.hpp"
#include "game/xmodel_index_contract.hpp"
#include <asmjit/core/jitruntime.h>
#include <thread>

namespace model_identity_tests
{
	using namespace scene_models::identity;
	inline registry<96> bindings;
	inline pool native{0x100000,5120,696};
	inline std::uintptr_t resolve(std::uintptr_t descriptor,std::uintptr_t first) noexcept
	{
		if (first!=native.first) return descriptor;
		return bindings.source(native,descriptor).value_or(descriptor);
	}
	inline int run()
	{
		int failures{};
		const auto check=[&](bool ok,const char* label) { if (!ok) { ++failures; std::cerr<<"identity FAIL: "<<label<<'\n'; } };
		const auto source=native.first+71*native.stride;
		constexpr std::uintptr_t loader=0x1bac979000, casing=0x1bac979620, tip=0x1bac97a000;
		const std::array<alias,3> parts{{{loader,source},{casing,source},{tip,source}}};
		check(native.index(native.first)==0 && native.index(native.first+5119*696)==5119,"native first/last model");
		check(!native.index(native.first-1) && !native.index(native.first+1) && !native.index(native.first+5120*696),"header/interior/one-past rejected");
		check(!pool{UINTPTR_MAX-7,2,696}.valid() && !pool{1,0,696}.valid() && !pool{1,2,0}.valid(),"overflow/empty pool rejected");
		check(!bindings.source(native,casing),"unregistered crash descriptor blocked at submit");
		check(bindings.publish(native,parts),"loaded source provenance published atomically");
		for (const auto part:parts) check(bindings.source(native,part.descriptor)==source,"loader/case/tip share their actual source identity");
		check(!bindings.publish(native,parts) && bindings.size()==3,"no overwrite/rebind of queued descriptor");
		const std::array<alias,2> invalid{{{0x1bac97b000,source},{0x1bac97c000,source+1}}};
		check(!bindings.publish(native,invalid) && bindings.size()==3 && !bindings.source(native,invalid[0].descriptor),"failed batch does not partly publish");
		const std::array<alias,1> alias_chain{{{0x1bac97b000,casing}}};
		check(!bindings.publish(native,alias_chain),"alias chains are not native provenance");
		const std::array<alias,1> hijack{{{source,native.first}}};
		check(!bindings.publish(native,hijack),"native asset cannot be replaced by a subset");
		const std::array<alias,1> interior{{{source+1,native.first}}};
		check(!bindings.publish(native,interior),"misaligned native-pool interior cannot become a subset");
		check(!bindings.source({native.first+8,native.count,native.stride},casing),"relocated pool invalidates registration");
		registry<2> limited;
		check(!limited.publish(native,parts) && !limited.size(),"capacity failure without eviction or partial output");
		check(bindings.source(native,source)==source,"native source preserves identity");
		registry<96> concurrent;
		std::atomic_bool finished{}; std::atomic_bool torn{};
		std::thread reader([&] {
			while (!finished.load(std::memory_order_acquire))
				for (std::uintptr_t i=0;i<96;++i)
					if (const auto result=concurrent.source(native,0x20000000+i*696);result && *result!=source) torn=true;
		});
		for (std::uintptr_t i=0;i<96;i+=3)
		{
			const std::array<alias,3> batch{{{0x20000000+i*696,source},{0x20000000+(i+1)*696,source},{0x20000000+(i+2)*696,source}}};
			check(concurrent.publish(native,batch),"bounded concurrent publication");
		}
		finished.store(true,std::memory_order_release); reader.join();
		check(!torn && concurrent.size()==96,"reader observes only complete immutable entries");
		// Each unloaded zone has drained its readers and destroyed the borrowed
		// descriptors. Repeated loads must not exhaust a process-lifetime ledger.
		for (std::uintptr_t epoch=0;epoch<1000;++epoch)
		{
			concurrent.reset_after_drain();
			check(concurrent.size()==0 && !concurrent.source(native,0x20000000),"retirement removes every old alias");
			const auto current_source=native.first+(epoch%native.count)*native.stride;
			const std::array<alias,1> reused{{{0x20000000,current_source}}};
			check(concurrent.publish(native,reused) && concurrent.source(native,reused[0].descriptor)==current_source,
				"descriptor addresses may be reused only after the drained asset boundary");
		}

		// Regression for the neighbouring off-frustum relocation bug: old +5
		// writes destroy the SIB and retain the old high displacement byte.
		using namespace game::xmodel_index_contract;
		for (bool store:{false,true})
		{
			std::array<std::uint8_t,10> instruction{0xf3,0x41,0x0f,std::uint8_t(store?0x11:0x10),0x84,0x88,0x10,0x20,0x30,0x04};
			check(relocate_table(instruction,store,0x232c71d0),"validated indexed-float relocation");
			check(instruction[5]==0x88 && instruction[6]==0xd0 && instruction[9]==0x23,"preserve RCX*4 SIB; replace all four displacement bytes");
			instruction[5]=0x10;
			check(!relocate_table(instruction,store,0),"already corrupt address mode rejected");
		}

		// Execute the SAME emitted bridge on synthetic data, never game code or a
		// live process. Deliberately clobber every Win64 volatile SIMD/GPR in the
		// resolver and compare with the verified native leaf, including RCX/RDX.
		asmjit::JitRuntime runtime;
		const auto jit=[&](auto emit) {
			asmjit::CodeHolder code; code.init(runtime.environment());
			asmjit::x86::Assembler a(&code); emit(a);
			void* fn{}; check(runtime.add(&fn,&code)==asmjit::kErrorOk,"JIT bridge allocation"); return fn;
		};
		using namespace asmjit::x86;
		auto* hostile=jit([](auto& a) {
			a.sub(rsp,0x28); a.mov(rax,reinterpret_cast<std::uintptr_t>(resolve)); a.call(rax); a.add(rsp,0x28);
			a.xor_(rcx,rcx); a.xor_(rdx,rdx); a.xor_(r8,r8); a.xor_(r9,r9); a.xor_(r10,r10); a.xor_(r11,r11);
			for (int i=0;i<6;++i) a.pxor(xmm(i),xmm(i)); a.ret();
		});
		auto* baseline=jit([](auto& a) { a.mov(rax,native.first); a.embed(getter_tail.data(),getter_tail.size()); });
		auto* adapted=jit([&](auto& a) { a.mov(rax,native.first); emit_bridge(a,reinterpret_cast<std::uintptr_t>(hostile)); });
		auto* harness=jit([](auto& a) {
			a.push(rbx); a.sub(rsp,0x30); a.mov(rbx,r8); a.mov(qword_ptr(rsp,0x20),rcx); a.mov(rcx,rdx);
			a.mov(r8,0x1234); a.mov(r9,0x5678); a.mov(r10,0x90ab); a.mov(r11,0xcdef);
			for (int i=0;i<6;++i) a.pcmpeqd(xmm(i),xmm(i));
			a.call(qword_ptr(rsp,0x20));
			a.mov(qword_ptr(rbx,0),rax); a.mov(qword_ptr(rbx,8),rcx); a.mov(qword_ptr(rbx,16),rdx);
			a.mov(qword_ptr(rbx,24),r8); a.mov(qword_ptr(rbx,32),r9); a.mov(qword_ptr(rbx,40),r10); a.mov(qword_ptr(rbx,48),r11);
			a.pushfq(); a.pop(rax); a.mov(qword_ptr(rbx,56),rax);
			for (int i=0;i<6;++i) a.movdqu(xmmword_ptr(rbx,64+i*16),xmm(i));
			a.add(rsp,0x30); a.pop(rbx); a.ret();
		});
		if (hostile && baseline && adapted && harness)
		{
			using probe=void(*)(void*,std::uintptr_t,void*);
			std::array<std::uint64_t,20> expected{},observed{},unsafe{};
			const auto invoke=reinterpret_cast<probe>(harness);
			invoke(baseline,source,expected.data());
			for (const auto part:parts)
			{
				invoke(adapted,part.descriptor,observed.data());
				check(expected==observed && observed[0]==71,"mapped native index with all leaf registers/flags/SIMD preserved");
			}
			invoke(baseline,casing,unsafe.data());
			check(unsafe[0]>=native.count,"old heap-model route reproduces an out-of-range index without a table access");
			invoke(adapted,source,observed.data()); check(expected==observed,"ordinary native call unchanged");
			invoke(adapted,0xdeadbeef,observed.data()); invoke(baseline,0xdeadbeef,expected.data());
			check(expected==observed,"foreign unregistered engine calls not clamped to an unrelated asset");
		}
		for (auto* fn:{harness,adapted,baseline,hostile}) if (fn) runtime.release(fn);
		return failures;
	}
}
