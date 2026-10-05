#include <std_include.hpp>
#include "component/vr/native_menu_hook.hpp"
#include "component/vr/native_ui_dispatch_bridge.hpp"
#include "component/vr/native_hud_visibility_bridge.hpp"
#include "component/vr/native_script_image_bridge.hpp"
#include <asmjit/core/jitruntime.h>

namespace
{
	void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
	struct observation
	{
		std::uint64_t client{},element{},root{};
		float alpha{};std::uint32_t flags{};
		std::uint64_t vm{},alignment{},rax{},restored_rbx{};
	};
	template<class Emit> void publish(void* destination,Emit emit)
	{
		asmjit::JitRuntime runtime;asmjit::CodeHolder code;
		require(code.init(runtime.environment())==asmjit::kErrorOk,"code initialization");
		asmjit::x86::Assembler assembler(&code);emit(assembler);
		require(code.flatten()==asmjit::kErrorOk,"code flattening");
		require(code.relocate_to_base(reinterpret_cast<std::uintptr_t>(destination))==asmjit::kErrorOk,"code relocation");
		require(code.copy_flattened_data(destination,4096)==asmjit::kErrorOk,"code publication");
		require(FlushInstructionCache(GetCurrentProcess(),destination,4096)!=0,"instruction publication");
	}
}

int main()
{
	try
	{
		using namespace asmjit::x86;
		auto* native=static_cast<std::uint8_t*>(utils::memory::allocate_near(0x140000000,8192,PAGE_EXECUTE_READWRITE));
		require(native!=nullptr,"native-range allocation");
		void* distant{};
		for(std::uintptr_t at=0x20000000000;!distant&&at<0x20001000000;at+=0x10000)
			distant=VirtualAlloc(reinterpret_cast<void*>(at),0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
		require(distant!=nullptr,"forced distant allocation");
		const auto cleanup=gsl::finally([&]{VirtualFree(native,0,MEM_RELEASE);VirtualFree(distant,0,MEM_RELEASE);});
		auto* observer=native+0x1000;
		auto* bridge=static_cast<std::uint8_t*>(distant);
		observation seen;
		publish(observer,[&](auto& a){
			a.mov(r10,reinterpret_cast<std::uintptr_t>(&seen));
			a.mov(qword_ptr(r10,offsetof(observation,rax)),rax);
			a.mov(qword_ptr(r10,offsetof(observation,client)),rcx);
			a.mov(qword_ptr(r10,offsetof(observation,element)),rdx);
			a.mov(qword_ptr(r10,offsetof(observation,root)),r8);
			a.movss(dword_ptr(r10,offsetof(observation,alpha)),xmm3);
			a.mov(eax,dword_ptr(rsp,0x28));a.mov(dword_ptr(r10,offsetof(observation,flags)),eax);
			a.mov(rax,qword_ptr(rsp,0x30));a.mov(qword_ptr(r10,offsetof(observation,vm)),rax);
			a.mov(rax,rsp);a.and_(rax,15);a.mov(qword_ptr(r10,offsetof(observation,alignment)),rax);
			a.xor_(rbx,rbx);a.ret(); // The displaced load must restore RBX even after this hostile callback.
		});
		std::size_t offset{},continuation{};
		constexpr std::uint64_t rax_seed=0x123456789abcdef0,rbx_seed=0xfedcba9876543210;
		publish(native,[&](auto& a){
			a.push(rbx);a.sub(rsp,0x100);
			a.mov(rax,qword_ptr(rsp,0x140));a.mov(qword_ptr(rsp,0xd0),rax);
			a.mov(rax,rbx_seed);a.mov(qword_ptr(rsp,0xd8),rax);
			a.mov(eax,dword_ptr(rsp,0x130));a.mov(dword_ptr(rsp,0x20),eax);
			a.mov(rax,qword_ptr(rsp,0x138));a.mov(qword_ptr(rsp,0x28),rax);
			a.mov(rbx,reinterpret_cast<std::uintptr_t>(observer));a.mov(rax,rax_seed);
			offset=a.offset();a.call(rbx);a.mov(rbx,qword_ptr(rsp,0xd8));continuation=a.offset();
			a.mov(rdx,qword_ptr(rsp,0xd0));a.mov(qword_ptr(rdx,offsetof(observation,restored_rbx)),rbx);
			a.add(rsp,0x100);a.pop(rbx);a.ret();
		});
		require(continuation-offset==10,"native patch window changed");
		publish(bridge,[&](auto& a){vr::native_menu::emit_render_bridge(a,reinterpret_cast<std::uintptr_t>(observer),reinterpret_cast<std::uintptr_t>(native+continuation));});
		const auto site=reinterpret_cast<std::uintptr_t>(native+offset);
		require(utils::hook::is_relatively_far(reinterpret_cast<void*>(site),bridge),"regression must cross rel32 range");
		std::array<std::uint8_t,10> original{};std::memcpy(original.data(),native+offset,original.size());
		bool old_failure{};
		try{utils::hook::jump(site,bridge);}catch(const std::runtime_error& e){old_failure=std::string(e.what())=="Too far away to create 32bit relative branch";}
		require(old_failure,"original startup failure not reproduced");
		require(!vr::native_menu::install_render_bridge(site,nullptr),"null bridge accepted");
		require(!std::memcmp(original.data(),native+offset,original.size()),"failed preparation modified native instructions");
		require(vr::native_menu::install_render_bridge(site,bridge),"near relay installation failed");
		auto* relay=static_cast<std::uint8_t*>(utils::hook::follow_branch(native+offset));
		const auto free_relay=gsl::finally([&]{VirtualFree(relay,0,MEM_RELEASE);});
		require(relay[0]==0xff&&relay[1]==0x25,"relay clobbers a register");
		MEMORY_BASIC_INFORMATION info{};
		require(VirtualQuery(relay,&info,sizeof(info))&&info.Protect==PAGE_EXECUTE_READ,"relay protection");
		for(std::size_t i=5;i<10;++i)require(native[offset+i]==0x90,"displaced instruction padding");
		using call=void(*)(int,void*,void*,float,int,void*,observation*);
		for(float alpha:{0.f,.25f,1.f})for(int flags:{0,0x40,0x7fffffff})
		{
			seen={};reinterpret_cast<call>(native)(7,reinterpret_cast<void*>(0x11110000),reinterpret_cast<void*>(0x22220000),alpha,flags,reinterpret_cast<void*>(0x33330000),&seen);
			require(seen.client==7&&seen.element==0x11110000&&seen.root==0x22220000&&seen.alpha==alpha&&seen.flags==unsigned(flags)&&seen.vm==0x33330000,"native render arguments changed");
			require(seen.alignment==8&&seen.rax==rax_seed&&seen.restored_rbx==rbx_seed,"relay/bridge changed Win64 register or stack contract");
		}
		struct marker_observation
		{
			std::array<std::uint64_t,7> registers{};
			std::array<std::array<std::uint32_t,4>,6> simd{};
			std::uint64_t flags{},cursor{},argument{},alignment{};
		} marker;
		const std::array<std::uint32_t,4> simd_seed{0x3f800000,0x11223344,0x55667788,0xaabbccdd};
		std::uint32_t load_value=0xcabb1234;
		publish(observer,[&](auto& a){
			a.mov(r10,reinterpret_cast<std::uintptr_t>(&marker));
			a.mov(qword_ptr(r10,offsetof(marker_observation,argument)),rcx);
			a.mov(rax,rsp);a.and_(rax,15);a.mov(qword_ptr(r10,offsetof(marker_observation,alignment)),rax);
			for(const auto reg:{rax,rcx,rdx,r8,r9,r10,r11})a.xor_(reg,reg);
			for(int i=0;i<6;++i)a.pxor(xmm(i),xmm(i));
			a.clc();a.ret();
		});
		for(const bool begin:{true,false})
		{
			publish(native,[&](auto& a){
				a.push(rbx);a.sub(rsp,0xc0);a.mov(rbx,rcx);
				a.mov(rax,0x5555);a.mov(qword_ptr(rsp,0xa8),rax);
				a.mov(rax,reinterpret_cast<std::uintptr_t>(simd_seed.data()));
				for(int i=0;i<6;++i)a.movdqu(xmm(i),xmmword_ptr(rax));
				a.mov(rax,rax_seed);a.mov(rcx,2);a.mov(rdx,3);a.mov(r8,4);a.mov(r9,5);a.mov(r10,6);a.mov(r11,7);
				a.cmp(rax,rax);a.stc();offset=a.offset();
				for(unsigned i=0;i<(begin?8u:6u);++i)a.nop();continuation=a.offset();
				const Gp regs[]{rax,rcx,rdx,r8,r9,r10,r11};
				for(int i=0;i<7;++i)a.mov(qword_ptr(rbx,offsetof(marker_observation,registers)+i*8),regs[i]);
				a.pushfq();a.pop(qword_ptr(rbx,offsetof(marker_observation,flags)));
				for(int i=0;i<6;++i)a.movdqu(xmmword_ptr(rbx,offsetof(marker_observation,simd)+i*16),xmm(i));
				a.mov(rax,qword_ptr(rsp,0xa8));a.mov(qword_ptr(rbx,offsetof(marker_observation,cursor)),rax);
				a.add(rsp,0xc0);a.pop(rbx);a.ret();
			});
			publish(bridge,[&](auto& a){vr::native_hud_capture::emit_ui_dispatch_marker(a,reinterpret_cast<std::uintptr_t>(observer),reinterpret_cast<std::uintptr_t>(native+continuation),begin,reinterpret_cast<std::uintptr_t>(&load_value));});
			const auto marker_site=reinterpret_cast<std::uintptr_t>(native+offset);
			auto* marker_relay=utils::hook::create_preserving_near_jump(marker_site,bridge);
			require(marker_relay!=nullptr,"UI marker relay allocation");
			const auto free_marker=gsl::finally([&]{VirtualFree(marker_relay,0,MEM_RELEASE);});
			utils::hook::jump(marker_site,marker_relay);
			marker={};reinterpret_cast<void(*)(marker_observation*)>(native)(&marker);
			const std::array<std::uint64_t,7> expected{begin?rax_seed:load_value,2,3,4,5,6,7};
			require(marker.registers==expected,"UI marker changed live integer state");
			for(const auto& value:marker.simd)require(value==simd_seed,"UI marker changed live SIMD state");
			require((marker.flags&0x41)==0x41&&marker.alignment==8,"UI marker changed flags or callback alignment");
			require(marker.argument==rax_seed&&marker.cursor==(begin?rax_seed:0x5555),"UI marker lost stream pointer/displaced instruction");
		}
		// HUD read substitution changes RAX only, across a forced far relay.
		std::uint8_t hidden{};std::uintptr_t native_dvar=0x12345000;
		constexpr std::uintptr_t disabled_dvar=0x98765000;
		publish(native,[&](auto& a){
			a.push(rbx);a.mov(rbx,rcx);
			a.mov(rcx,2);a.mov(rdx,3);a.mov(r8,4);a.mov(r9,5);a.mov(r10,6);a.mov(r11,7);
			a.cmp(rax,rax);a.stc();offset=a.offset();for(unsigned i=0;i<7;++i)a.nop();continuation=a.offset();
			const Gp regs[]{rax,rcx,rdx,r8,r9,r10,r11};
			for(int i=0;i<7;++i)a.mov(qword_ptr(rbx,offsetof(marker_observation,registers)+i*8),regs[i]);
			a.pushfq();a.pop(qword_ptr(rbx,offsetof(marker_observation,flags)));a.pop(rbx);a.ret();
		});
		publish(bridge,[&](auto& a){vr::presentation_options::emit_hud_read(a,reinterpret_cast<std::uintptr_t>(&hidden),
			disabled_dvar,reinterpret_cast<std::uintptr_t>(&native_dvar),reinterpret_cast<std::uintptr_t>(native+continuation));});
		const auto hud_site=reinterpret_cast<std::uintptr_t>(native+offset);
		auto* hud_relay=utils::hook::create_preserving_near_jump(hud_site,bridge);
		require(hud_relay!=nullptr,"HUD read relay allocation");
		const auto free_hud_relay=gsl::finally([&]{VirtualFree(hud_relay,0,MEM_RELEASE);});
		utils::hook::jump(hud_site,hud_relay);
		for(auto pointer:{std::uintptr_t(0x12345000),std::uintptr_t(0xabcdef00)})for(auto hide:{false,true})
		{
			native_dvar=pointer;hidden=hide;marker={};reinterpret_cast<void(*)(marker_observation*)>(native)(&marker);
			const std::array<std::uint64_t,7> expected{hide?disabled_dvar:pointer,2,3,4,5,6,7};
			require(marker.registers==expected && (marker.flags&0x41)==0x41,"HUD filter clobbered live state or cached native dvar");
			require(native_dvar==pointer,"HUD visibility wrote the native setting");
		}
		vr::native_waypoints::image_layout seen_image{};
		std::array<std::byte,0x240> parameters{};std::array<std::byte,0xc0> element{};
		const float image_x=205,image_y=310,image_w=1229.25f,image_h=64,image_alpha=.9f;
		std::memcpy(parameters.data(),&image_x,4);std::memcpy(parameters.data()+0x23c,&image_alpha,4);
		const unsigned image_flag=0x4000;std::memcpy(element.data()+0xb8,&image_flag,4);
		publish(observer,[&](auto& a){
			a.mov(r10,reinterpret_cast<std::uintptr_t>(&seen_image));
			a.movdqu(xmm0,xmmword_ptr(rcx));a.movdqu(xmmword_ptr(r10),xmm0);
			a.movdqu(xmm1,xmmword_ptr(rcx,16));a.movdqu(xmmword_ptr(r10,16),xmm1);
			for(const auto reg:{rax,rcx,rdx,r8,r9,r10,r11})a.xor_(reg,reg);
			for(int i=0;i<6;++i)a.pxor(xmm(i),xmm(i));a.stc();a.ret();
		});
		publish(native,[&](auto& a){
			for(const auto reg:{rbx,rsi,rdi,r12})a.push(reg);a.sub(rsp,0x88);
			a.movdqu(xmmword_ptr(rsp,0x50),xmm6);a.movdqu(xmmword_ptr(rsp,0x60),xmm7);
			a.mov(rbx,reinterpret_cast<std::uintptr_t>(parameters.data()));a.mov(rsi,reinterpret_cast<std::uintptr_t>(element.data()));
			a.mov(rdi,0x12340000);a.mov(r12,reinterpret_cast<std::uintptr_t>(&marker));
			a.mov(rax,reinterpret_cast<std::uintptr_t>(&image_y));a.movss(xmm0,dword_ptr(rax));
			a.mov(rax,reinterpret_cast<std::uintptr_t>(&image_w));a.movss(xmm7,dword_ptr(rax));
			a.mov(rax,reinterpret_cast<std::uintptr_t>(&image_h));a.movss(xmm6,dword_ptr(rax));
			a.mov(rax,rax_seed);a.mov(rcx,2);a.mov(rdx,3);a.mov(r8,4);a.mov(r9,5);a.mov(r10,6);a.mov(r11,7);
			offset=a.offset();a.test(dword_ptr(rsi,0xb8),0x4000);continuation=a.offset();
			const Gp regs[]{rax,rcx,rdx,r8,r9,r10,r11};
			for(int i=0;i<7;++i)a.mov(qword_ptr(r12,offsetof(marker_observation,registers)+i*8),regs[i]);
			a.pushfq();a.pop(qword_ptr(r12,offsetof(marker_observation,flags)));
			a.movdqu(xmmword_ptr(r12,offsetof(marker_observation,simd)),xmm0);
			a.movdqu(xmm6,xmmword_ptr(rsp,0x50));a.movdqu(xmm7,xmmword_ptr(rsp,0x60));
			a.add(rsp,0x88);for(const auto reg:{r12,rdi,rsi,rbx})a.pop(reg);a.ret();
		});
		require(continuation-offset==10,"image layout patch window changed");
		publish(bridge,[&](auto& a){vr::native_waypoints::emit_image_layout_marker(a,reinterpret_cast<std::uintptr_t>(observer),reinterpret_cast<std::uintptr_t>(native+continuation));});
		const auto image_site=reinterpret_cast<std::uintptr_t>(native+offset);
		auto* image_relay=utils::hook::create_preserving_near_jump(image_site,bridge);require(image_relay!=nullptr,"image relay allocation");
		const auto free_image=gsl::finally([&]{VirtualFree(image_relay,0,MEM_RELEASE);});utils::hook::jump(image_site,image_relay);
		marker={};reinterpret_cast<void(*)()>(native)();
		require(seen_image.material==0x12340000&&seen_image.x==image_x&&seen_image.y==image_y&&seen_image.width==image_w&&seen_image.height==image_h&&seen_image.alpha==image_alpha,
			"image marker lost common native layout before branch scaling");
		require(marker.registers==std::array<std::uint64_t,7>{rax_seed,2,3,4,5,6,7}&&!(marker.flags&0x41),"image marker changed registers or displaced TEST flags");
		float restored_y{};std::memcpy(&restored_y,marker.simd[0].data(),4);require(restored_y==image_y,"image marker destroyed the native Y result");
		std::cout<<"vr-native-menu-hook-tests: PASS (far relays, native UI/image layout, integer/SIMD state, flags)\n";return 0;
	}
	catch(const std::exception& e){std::cerr<<"vr-native-menu-hook-tests: FAIL: "<<e.what()<<'\n';return 1;}
}
