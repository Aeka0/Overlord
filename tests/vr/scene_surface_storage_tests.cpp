#include "component/scene_surface_layout.hpp"
#include "component/scene_surface_storage_contract.hpp"
#include "component/scene_static_surface_layout.hpp"
#include "component/scene_model_lighting_policy.hpp"
#include "component/scene_model_lighting_bridge.hpp"
#include "component/vr/gameplay/skinned_part_visibility.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <cstring>

namespace
{
	bool reused_handle(){return true;}
	bool allocated_handle(){return false;}
}

int main()
{
	{
		namespace lighting=scene_model_lighting;
		// Execute the actual bridge emitter against every 16-bit handle. Native
		// savegames retain transient lighting handles across partition changes.
		std::array<std::uint32_t,2> limits{};
		asmjit::JitRuntime runtime;
		asmjit::CodeHolder code;
		assert(code.init(runtime.environment())==asmjit::Error::kOk);
		asmjit::x86::Assembler a(&code);
		a.mov(asmjit::x86::r11d,asmjit::x86::ecx);
		lighting::emit_dynamic_handle_guard(a,reinterpret_cast<std::uintptr_t>(limits.data()),
			reinterpret_cast<std::uintptr_t>(reused_handle),reinterpret_cast<std::uintptr_t>(allocated_handle));
		using guard_fn=bool(*)(unsigned);
		guard_fn can_reuse{};
		assert(runtime.add(&can_reuse,&code)==asmjit::Error::kOk);
		for(unsigned clients=0;clients<8;++clients)
		{
			const unsigned reserved=clients*4096,total=lighting::total_slots(reserved),cache=total-reserved;
			assert(total && !(total&(total-1)) && cache>=4096 && cache<=lighting::static_table_capacity);
			if(clients>2)assert(lighting::minimum(reserved)==lighting::native_minimum);
			limits={cache,total};
			for(unsigned handle=0;handle<=UINT16_MAX;++handle)
				assert(can_reuse(handle)==(handle>cache && handle<=total));
		}
		// The crash dump: 0x1848 - 0x3000 - 1 became 0xffffe847.
		limits={4096,8192};assert(can_reuse(0x1848));
		limits={12288,16384};assert(!can_reuse(0x1848));
		assert(can_reuse(0x3001) && can_reuse(0x4000));
		assert(!can_reuse(0x3000) && !can_reuse(0x4001) && !can_reuse(0));
		assert(runtime.release(can_reuse)==asmjit::Error::kOk);
		assert(lighting::total_slots(4096)==16384 && lighting::total_slots(4096)-4096==12288);
		assert(!lighting::total_slots(8*4096));
	}
	{
		namespace sm=scene_static_surfaces;
		constexpr std::uintptr_t base=0x170000000;
		constexpr auto displacement=base-scene_surface_storage::native_first_frontend;
		unsigned members{},inverse{};
		for(const auto& item:sm::operands)
		{
			std::int32_t original{};std::memcpy(&original,item.bytes.data()+item.field,4);
			const auto mapped=sm::relocate(item,displacement);assert(mapped);
			assert(!sm::relocate(item,0) && !sm::relocate(item,displacement+1) && !sm::relocate(item,UINT32_MAX));
			if(item.kind==sm::operand_kind::stack_base)
			{
				++inverse;
				assert(item.size==8 && item.field==4);
				// Execute the patched native LEA, including its SIB byte and signed
				// disp32. Both shadow initializers must retain their stack arrays.
				const auto stack_addend=item.address==0x1407116CD ? 0x5880 : 0x5830;
				assert(original+static_cast<std::int32_t>(sm::native_offset)==stack_addend);
				for(const auto moved:std::array<std::uintptr_t,4>{sm::native_offset,0x15390c10,displacement,0x7fffdffc})
				{
					const auto immediate=sm::relocate(item,moved);assert(immediate);
					auto bytes=item.bytes;std::memcpy(bytes.data()+item.field,&*immediate,4);
					asmjit::JitRuntime runtime;
					asmjit::CodeHolder code;assert(code.init(runtime.environment())==asmjit::Error::kOk);
					asmjit::x86::Assembler a(&code);
					a.push(asmjit::x86::rbp);
					a.mov(asmjit::x86::rbp,asmjit::x86::rdx);
					a.mov(asmjit::x86::rax,asmjit::x86::rcx);
					a.embed(bytes.data(),item.size);
					a.mov(asmjit::x86::rax,asmjit::x86::r10);
					a.pop(asmjit::x86::rbp);a.ret();
					using sort_base_fn=std::uintptr_t(*)(std::uintptr_t,std::uintptr_t);
					sort_base_fn sort_base{};assert(runtime.add(&sort_base,&code)==asmjit::Error::kOk);
					for(unsigned bank=0;bank<2;++bank)
						for(const auto reserved:std::array<std::uintptr_t,3>{0,0x2d000,sm::capacity-0x8000})
						{
							const auto frontend=scene_surface_storage::native_first_frontend+
								bank*scene_surface_storage::native_frontend_stride;
							constexpr std::uintptr_t frame=0x35ad1690;
							const auto begin=frontend+moved+reserved;
							// Native mode-2 initialization: recover the displacement,
							// then combine the stack sort base and relative output end.
							const auto recovered=begin-reserved-frontend;
							const auto sort=sort_base(recovered,frame);
							const auto end_bias=frontend+moved+0x2000+reserved-(frame+stack_addend);
							for(unsigned type=0;type<4;++type)
							{
								const auto step=type*0x2000;
								assert(sort+step==frame+stack_addend+step);
								assert(sort+step+end_bias==begin+step+0x2000);
							}
							// The retained minidump delta is reproduced by the old LEA.
							if(moved==0x15390c10)
								assert(frame+recovered+original-sort==0x1463b510);
						}
					assert(runtime.release(sort_base)==asmjit::Error::kOk);
				}
				continue;
			}
			assert(scene_surface_storage::native_first_frontend+*mapped==base+original-sm::native_offset);
			assert(scene_surface_storage::native_first_frontend+scene_surface_storage::native_frontend_stride+*mapped==
				base+scene_surface_storage::native_frontend_stride+original-sm::native_offset);
			members+=original!=sm::native_offset;
		}
		assert(members==2 && inverse==2 && sm::capacity<scene_surface_storage::native_frontend_stride);
		// A full native transparent sorting batch can contain 128 unique keys,
		// each requiring 8 key bytes + 4 instance bytes. The old list truncates it.
		const auto& region=sm::native_budgets[sm::transparent_region];
		assert(region[1]*12>region[0] && region[1]*12<=sm::transparent_bytes);
		assert(sm::transparent_bytes<=region[0]*4 && sm::capacity==sm::native_capacity*4);
	}
	using namespace scene_surface_storage;
	static_assert(capacity==524288 && hidden_bytes==8 && brush_prefix_bytes==32);
	assert(!encode(4) && !encode(capacity) && !encode(capacity+8));
	for(std::size_t offset=0;offset<capacity;offset+=index_unit)
	{
		const auto id=encode(offset);assert(id && decode(*id)==offset);
		// Native draw keys keep the SAME 16-bit field at bits 20..35. Only the
		// unit changes. Other sorting/material bits must not enter the address.
		const auto packed=(0xfedcba9876543210ull & ~(0xffffull<<20)) |
			(static_cast<std::uint64_t>(*id)<<20);
		assert(((packed>>17)&0x7fff8)==offset);
		constexpr std::uint64_t displacement=0x213eb780;
		const auto shadow_key=((displacement+offset+((std::uint64_t{0}-displacement)&(capacity-1)))<<17)&0xffff00000ull;
		assert(((shadow_key>>17)&0x7fff8)==offset);
	}
	assert(*encode(262144)==32768 && *encode(capacity-8)==65535);
	assert(!record_bytes(-2) && !record_bytes(-1) && !record_bytes(INT32_MIN));
	std::size_t cursor{};
	for(const auto tag:std::array<std::int32_t,7>{-3,0,-4,-3,8192,-8,-3})
	{
		const auto bytes=record_bytes(tag);assert(bytes && *bytes%8==0);
		const auto id=encode(cursor);assert(id && decode(*id)==cursor);
		cursor+=*bytes;
	}
	for(std::size_t count=0;count<=65535;++count)
		assert((brush_prefix_bytes+count*brush_surface_bytes)%8==0);
	// The arena must follow the selected frontend, never reuse one bank while
	// native backend workers still read the preceding frame's immutable data.
	constexpr std::uintptr_t storage=0x170000000;
	constexpr auto displacement=storage-native_first_frontend;
	static_assert(displacement<=INT32_MAX && displacement%8==0);
	assert(native_first_frontend+displacement==storage);
	assert(native_first_frontend+native_frontend_stride+displacement==storage+native_frontend_stride);
	assert(capacity<native_frontend_stride);
	std::uintptr_t end{};unsigned relocated{},folded{},compound{};bool brush_crash{},model_crash{};
	for(const auto& p:contract::patches)
	{
		assert(p.size && p.size<=15 && p.address>=end);
		// This neighboring shift belongs to the separate motion-history arena,
		// not surfId. A global replacement of four-byte units would corrupt it.
		assert(!(p.address<=0x14071e594 && p.address+p.size>0x14071e594));
		end=p.address+p.size;
		if(p.dynamic)
		{
			assert(p.field+4<=p.size && p.dynamic<=3);++relocated;if(p.dynamic==2)++folded;
			std::uint32_t original{};std::memcpy(&original,p.before.data()+p.field,4);
			const auto moved=relocate_operand(p.dynamic,original,displacement);assert(moved);
			if(p.dynamic==1 && original!=native_arena_offset)++compound;
			if(p.address==0x1407b9584){brush_crash=true;assert(*moved==displacement+8);}
			if(p.address==0x1407bb588){model_crash=true;assert(*moved==displacement+0x28);}
		}
	}
	assert(relocated==91 && folded==1 && compound==24 && contract::patches.size()==242);
	assert(brush_crash && model_crash);
	for(std::uint32_t field:{0u,4u,6u,8u,0x10u,0x18u,0x28u,0x30u,0x5cu})
		for(std::uint16_t id:{std::uint16_t{0},std::uint16_t{0x8000},std::uint16_t{0xffff}})
		{
			const auto operand=relocate_operand(1,native_arena_offset+field,displacement);assert(operand);
			assert(native_first_frontend+decode(id)+*operand==storage+decode(id)+field);
			assert(native_first_frontend+native_frontend_stride+decode(id)+*operand==storage+native_frontend_stride+decode(id)+field);
		}
	assert(!relocate_operand(1,native_arena_offset-1,displacement));
	assert(!relocate_operand(1,native_arena_offset+0x40000,displacement));
	assert(!relocate_operand(1,native_arena_offset+0x28,0x7ffffff8));
	{
		// Regression: native writer is expanded but the MOD skin-hide reader used
		// the old hidden stride. No valid plan meant no magazine substitution,
		// although the separately spawned falling magazine still rendered.
		std::array<std::byte,248> stream{};
		const auto header=[&](std::size_t at,std::int32_t tag){std::memcpy(stream.data()+at,&tag,4);};
		header(0,-3);header(4,INT32_MAX);header(8,0);header(64,-5);header(192,42);
		const auto plan=vr::gameplay::weapons::plan_skin_packets(stream,4);
		assert(plan.valid && plan.count==2 && plan.skinned[0]==8 && plan.skinned[1]==192);
		std::uint64_t magazine=0x1111222233334444,receiver=0x5555666677778888,hidden=0x9999aaaabbbbcccc;
		std::memcpy(stream.data()+48,&magazine,8);std::memcpy(stream.data()+232,&receiver,8);
		for(std::size_t i=0;i<plan.count;++i)
		{
			std::uint64_t surface{};std::memcpy(&surface,stream.data()+plan.skinned[i]+0x28,8);
			if(surface==magazine)std::memcpy(stream.data()+plan.skinned[i]+0x28,&hidden,8);
		}
		std::uint64_t result{};std::memcpy(&result,stream.data()+48,8);assert(result==hidden);
		std::memcpy(&result,stream.data()+232,8);assert(result==receiver);
		assert(!vr::gameplay::weapons::plan_skin_packets(std::span(stream).first(247),4).valid);
		std::array<std::byte,60> old_format{};std::int32_t tag=-3;std::memcpy(old_format.data(),&tag,4);
		assert(!vr::gameplay::weapons::plan_skin_packets(old_format,2).valid);
	}
	{
		// Hidden records may occur in the middle or at the end, not only before
		// the magazine. The same parser serves omitted left/right arm subtrees.
		std::array<std::byte,232> stream{};
		const auto header=[&](std::size_t at,std::int32_t tag){std::memcpy(stream.data()+at,&tag,4);};
		header(0,4);header(56,-3);header(60,INT32_MAX);header(64,-3);header(68,INT32_MAX);
		header(72,-4);header(168,99);header(224,-3);header(228,INT32_MAX);
		const auto plan=vr::gameplay::weapons::plan_skin_packets(stream,6);
		assert(plan.valid && plan.count==2 && plan.skinned[0]==0 && plan.skinned[1]==168);
		assert(!vr::gameplay::weapons::plan_skin_packets(stream,5).valid);
		std::array<std::byte,512*hidden_bytes> hidden_stream{};
		for(std::size_t at=0;at<hidden_stream.size();at+=hidden_bytes)
		{
			const std::int32_t tag=-3,padding=INT32_MAX;
			std::memcpy(hidden_stream.data()+at,&tag,4);std::memcpy(hidden_stream.data()+at+4,&padding,4);
		}
		const auto all_hidden=vr::gameplay::weapons::plan_skin_packets(hidden_stream,512);
		assert(all_hidden.valid && all_hidden.count==0);
		assert(!vr::gameplay::weapons::plan_skin_packets(hidden_stream,513).valid);
		std::array<std::byte,1120> too_many_groups{};const std::int32_t tag=-36;
		std::memcpy(too_many_groups.data(),&tag,4);
		assert(!vr::gameplay::weapons::plan_skin_packets(too_many_groups,1).valid);
	}
	std::cout<<"scene-surface-storage-tests: PASS (full ID range, packet alignment, bank separation, ABI coverage)\n";
}
