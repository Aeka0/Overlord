#include <std_include.hpp>
#include "native_shield.hpp"
#include "muzzle_clearance.hpp"
#include "underbarrel_native.hpp"
#include "official_cheats.hpp"
#include "underbarrel_binding.hpp"
#include "native_ammunition.hpp"
#include "native_ammunition_storage.hpp"
#include "native_ballistics.hpp"
#include "native_scripted_control.hpp"
#include "weapon_carry_runtime.hpp"
#include <utils/native_memory.hpp>
#include "component/scheduler_context.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::underbarrel::native
{
	namespace
	{
		bool verified{};thread_local bool firing{};
		template<class T>bool read(const void* p,std::size_t offset,T& value)noexcept
		{return p && utils::native_memory::read_bytes(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));}
		const game::WeaponDef* definition(std::uint32_t token)noexcept
		{const game::WeaponDef* out{};return token && token<512 && read(reinterpret_cast<const void*>(0x14CE01580),token*8,out) ? out : nullptr;}
		std::string_view name(const game::WeaponDef* d,std::array<char,128>& buffer)noexcept
		{const char* text{};if(!read(d,0,text)||!utils::native_memory::read_bytes(buffer.data(),text,buffer.size()))return {};auto n=strnlen_s(buffer.data(),buffer.size());return n<buffer.size() ? std::string_view(buffer.data(),n) : std::string_view{};}
		std::uint64_t key(std::uint32_t host,bool clip,bool alternate=true)
		{return utils::hook::invoke<std::uint64_t>(clip?0x14069D250:0x14069D0B0,host,alternate)&(clip?0xffffffffffull:0xffffffffffffull);}
		bool scope()noexcept{return verified && scheduler::is_executing(scheduler::pipeline::server) && game::CL_IsCgameInitialized();}
		template<std::size_t N>bool match(std::uintptr_t at,const std::uint8_t(&bytes)[N])
		{std::array<std::uint8_t,N> mask{};mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),{bytes,mask.data(),N}));}
	}
	bool initialize()
	{
		// Witnessed class-6 G_FireWeapon path: entity, resolved launcher token,
		// 0x50-byte fire packet. Helper retains native speed, fuse and attacker.
		constexpr std::uint8_t launcher[]{0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x50};
		constexpr std::uint8_t packet[]{0x0f,0xb6,0x57,0x44,0x8b,0x4f,0x3c};
		constexpr std::uint8_t call[]{0xe8,0x9f,0x40,0x00,0x00};
		verified=native_ballistics::initialize() && match(0x14051CD40,launcher) && match(0x14051CD69,packet) && match(0x140518C9C,call);
		return verified;
	}
	binding resolve(weapon_identity host)noexcept
	{
		// A freshly accepted pickup or inventory restore precedes carry's public
		// pose snapshot. The native ledger/ownership epoch is the ammo authority.
		if(!scope() || !host || native_ammunition::projected_identity(host.weapon)!=host ||
			!native_ammunition::observe_carried(game::g_entities[0].client,host).valid)return {};
		const auto ledger=native_ammunition::instances();
		// Composite duplicate projections need paired clip transfer, not a second
		// shadow of the same native cell. Reject explicitly until both are owned.
		if(ledger.count(host.weapon)!=1)return {};
		const auto* d=definition(host.weapon);std::uint32_t alt{};
		if(!read(d,offsetof(game::WeaponDef,altWeapon),alt) || !alt || alt>=512 || alt==host.weapon)return {};
		const auto* child=definition(alt);std::array<char,128> hn{},sn{};const auto h=name(d,hn),s=name(child,sn);
		int cap{},type{},cls{};
		if(!read(child,offsetof(game::WeaponDef,clipSize),cap) || !read(child,offsetof(game::WeaponDef,weapType),type) ||
			!read(child,offsetof(game::WeaponDef,weapClass),cls))return {};
		const auto k=classify(h,s,type,cls,cap);if(k==kind::none)return {};
		if(k==kind::shotgun)
		{
			bool pump{},segmented{};int pellets{},add{};
			if(!read(child,offsetof(game::WeaponDef,boltAction),pump) || !pump ||
				!read(child,offsetof(game::WeaponDef,segmentedReload),segmented) || !segmented ||
				!read(child,offsetof(game::WeaponDef,shotCount),pellets) || pellets!=8 ||
				!read(child,offsetof(game::WeaponDef,reloadAmmoAdd),add) || add!=1)return {};
		}
		const auto ck=key(host.weapon,true),rk=key(host.weapon,false);
		if(!ck || !rk || ck==key(host.weapon,true,false) || rk==key(host.weapon,false,false) ||
			ck!=key(alt,true,false) || rk!=key(alt,false,false))return {};
		const auto* ps=game::g_entities[0].client;
		unsigned hosts{};
		for(unsigned i=0;i<15;++i)
		{
			std::uint32_t other{};std::uint8_t dual{};if(!read(ps,0x2f8+i*4,other)||!read(ps,0x335+i*8,dual))return {};
			if(!other)continue;if(other>=512 || dual)return {};
			if(other==host.weapon){++hosts;continue;}
			if(key(other,true,false)==ck)return {};
			std::uint32_t other_alt{};if(read(definition(other),offsetof(game::WeaponDef,altWeapon),other_alt) && other_alt && key(other,true)==ck)return {};
		}
		return hosts==1 ? binding{{host,alt,k},ck,rk,native_ammunition::timeline()} : binding{};
	}
	observation observe(const binding& b)noexcept
	{
		if(!b || resolve(b.id.host)!=b)return {};
		const auto cells=native_ammunition::storage::observe({reinterpret_cast<const std::byte*>(game::g_entities[0].client),native_ammunition::storage::extent},b.clip,b.reserve);
		if(!cells.valid() || cells.clip.count>capacity(b.id.type))return {};
		return {true,b,{cells.clip.count,cells.reserve.count}};
	}
	bool commit(const observation& expected,ammunition::projection after)noexcept
	{
		if(!scope() || !expected.valid || after.loaded<0 || after.loaded>capacity(expected.module.id.type))return false;
		const auto current=observe(expected.module);if(!current.valid || current.ammo!=expected.ammo)return false;
		return native_ammunition::storage::commit({reinterpret_cast<std::byte*>(game::g_entities[0].client),native_ammunition::storage::extent},
			expected.module.clip,expected.module.reserve,expected.ammo.loaded,expected.ammo.reserve,after.loaded,after.reserve);
	}
	int interval(const binding& b)noexcept
	{
		independent_fire::timing t;
		return scope() && resolve(b.id.host)==b && native_ballistics::firing_timing(b.id.definition,0,t) ? std::max(1,t.interval+t.delay) : -1;
	}
	bool exchange_reserves(const observation& expected,const native_ammunition::snapshot& primary,int primary_reserve,int secondary_reserve)noexcept
	{
		if(!scope() || !expected.valid || !primary.valid || primary.id()!=expected.module.id.host)return false;
		const auto* ps=game::g_entities[0].client;
		const auto host=native_ammunition::observe_owned(ps,primary.id());const auto module=observe(expected.module);
		if(!host.valid || host.ammo!=primary || !module.valid || module.ammo!=expected.ammo)return false;
		const auto reserve=native_ammunition::reserve_identity(ps,primary.id());
		// Loaded cells and the physical clip ledger stay unchanged. No callbacks
		// or partial commits can occur between the two native reserve writes.
		return native_ammunition::storage::commit_reserve_pair(
			{reinterpret_cast<std::byte*>(game::g_entities[0].client),native_ammunition::storage::extent},
			{{{reserve,primary.reserve,primary_reserve},{expected.module.reserve,expected.ammo.reserve,secondary_reserve}}});
	}
	bool fire(const observation& expected,ammunition::projection after,const shot_geometry& g,int command,settle_fn settle,void* context)noexcept
	{
		if(!scope() || firing || cheats::transitioning() || !settle || !expected.valid || !expected.ammo.loaded || after.reserve!=expected.ammo.reserve ||
			(after.loaded!=expected.ammo.loaded-1 && after.loaded!=expected.ammo.loaded))return false;
		auto* entity=&game::g_entities[0];auto* ps=reinterpret_cast<game::playerState_s*>(entity->client);
		const auto* paused=game::Dvar_FindVar("cl_paused");
		if(!ps || ps->commandTime!=command || !scripted_control::allowed(ps) || (ps->e_flags&0x103000) || !paused || paused->current.integer || *game::keyCatchers)return false;
		muzzle_frame m;m.position=g.origin;m.axis={g.forward,hands::scale(g.right,-1),g.up};if(!valid_geometry(m))return false;
		if(!shield::firing_clear(expected.module.id.host,g.origin))return false;
		hands::vec eye{ps->origin[0],ps->origin[1],ps->origin[2]+ps->viewHeightCurrent};
		if(!std::isfinite(hands::length(hands::sub(eye,g.origin))) || hands::length(hands::sub(eye,g.origin))>200)return false;
		game::trace_t trace{};game::Bounds point{};game::G_TraceCapsule(&trace,eye.data(),g.origin.data(),&point,entity->s.entityNum,muzzle_clearance_mask);
		if(!std::isfinite(trace.fraction)||trace.fraction<1||trace.allsolid||trace.startsolid)return false;
		native_ballistics::parameters packet{};packet.shot=g;packet.weapon=expected.module.id.definition;
		packet.definition=const_cast<game::WeaponDef*>(definition(packet.weapon));
		if(!packet.definition)return false;
		const auto spread=utils::hook::invoke<float>(native_ballistics::ads_spread_address,ps,packet.weapon,false,0);
		if(!std::isfinite(spread)||spread<0||spread>90)return false;
		if(entity->client!=reinterpret_cast<game::gclient_s*>(ps) || ps->commandTime!=command || !scripted_control::allowed(ps) ||
			!commit(expected,after))return false;
		settle(context);firing=true;const auto release=gsl::finally([]{firing=false;});
		if(expected.module.id.type==kind::shotgun)
			utils::hook::invoke<void>(native_ballistics::bullet_address,entity,spread,&packet,entity,command,-1.f);
		else utils::hook::invoke<game::gentity_s*>(0x14051CD40,entity,packet.weapon,&packet);
		return true; // Never refund after entering native damage/scripts.
	}
}
