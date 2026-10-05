#include <std_include.hpp>
#include "native_grenade.hpp"
#include "component/scheduler_context.hpp"
#include <utils/native_memory.hpp>
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::grenades::native
{
	namespace
	{
		bool ready{};
		std::atomic_uint64_t attempts{},spawned{},actor_clearances{},world_clearances{};
		std::atomic<float> requested_speed{},native_speed{};std::atomic_int obstruction{-1};
		template<std::size_t N>bool verify(std::uintptr_t address,const std::uint8_t(&bytes)[N])
		{std::array<std::uint8_t,N>mask;mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));}
		bool server()noexcept{return ready && scheduler::is_executing(scheduler::pipeline::server) && game::CL_IsCgameInitialized() && game::g_entities[0].client;}
	}
	bool initialize()
	{
		constexpr std::uint8_t spawn[]{0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x68,0x04,0,0};
		constexpr std::uint8_t caller[]{0x89,0x74,0x24,0x28,0x4c,0x8d,0x44,0x24,0x40,0x48,0x8b,0xcf,0xc7,0x44,0x24,0x20,1,0,0,0,0xe8,0xa9,0x17,0xfb,0xff};
		constexpr std::uint8_t timer[]{0x03,0x0d,0x48,0x96,0x0f,0x05,0x89,0x8b,0x80,0x01,0,0};
		constexpr std::uint8_t fuse[]{0x48,0x83,0xec,0x28,0x45,0x84,0xc0,0x41,0xb8,0xd4,0x07,0,0,0x74,0x06,0x41,0xb8,0xd8,0x07,0,0};
		constexpr std::uint8_t client_gate[]{0x48,0x83,0xb9,0x18,0x01,0,0,0};
		ready=verify(0x1404cf130,spawn) && verify(0x14051d96e,caller) && verify(0x1404d3132,timer) &&
			verify(0x1406a4080,fuse) && verify(0x14051d85d,client_gate) && weapons::native_ammunition::initialize();
		return ready;
	}
	bool describe(std::uint32_t weapon,descriptor& out)noexcept
	{
		if(!ready || !weapon || weapon>=512)return false;
		const auto* d=game::weapon_defs[weapon];
		if(!d || !d->szInternalName || d->inventoryType!=game::WEAPINVENTORY_OFFHAND || int(d->weapType)!=2)return false;
		const auto world=d->worldModel && d->worldModel[0]?d->worldModel[0]->name:nullptr;
		const auto k=classify(d->szInternalName,world?world:"");if(!valid(k))return false;
		// Stock sets the third argument from (thrower->client == nullptr).
		// SDK timer field names are reversed for H2: false selects player +0x7d4.
		const int fuse=utils::hook::invoke<int>(0x1406a4080,weapon,false,false);
		if(fuse<0 || fuse>60000 || (behaviors[unsigned(k)].timed ? fuse==0 : fuse!=0))return false;
		float radius=1.5f;
		if(k==kind::football || k==kind::pomegranate)
		{
			const auto* model=d->projectileModel;
			if(!model || !model->name || std::string_view(model->name)!=behaviors[unsigned(k)].chest_model)return false;
			for(float value:model->bounds.halfSize){if(!std::isfinite(value) || value<=0 || value>32)return false;radius=std::max(radius,value);}
		}
		out={weapon,k,fuse,radius};return true;
	}
	int time()noexcept{return ready?*reinterpret_cast<const int*>(0x1455cc780):0;}
	game::gentity_s* spawn_projectile(std::uint32_t weapon,hands::vec position,hands::vec velocity,bool rotate,int fuse_ms)noexcept
	{
		if(!server() || !weapon || weapon>=512 || !game::weapon_defs[weapon] || int(game::weapon_defs[weapon]->weapType)!=2 || fuse_ms<0 || fuse_ms>60000)return nullptr;
		for(float x:position)if(!std::isfinite(x) || std::abs(x)>1e7f)return nullptr;
		for(float x:velocity)if(!std::isfinite(x) || std::abs(x)>11000)return nullptr;
		return utils::hook::invoke<game::gentity_s*>(0x1404cf130,&game::g_entities[0],position.data(),velocity.data(),weapon,int(rotate),fuse_ms);
	}
	bool available(std::uint32_t weapon)noexcept
	{
		if(!server())return false;
		const auto a=weapons::native_ammunition::observe_carried(game::g_entities[0].client,weapon);
		return a.valid && (a.loaded>0 || a.reserve>0);
	}
	bool debit(std::uint32_t weapon)noexcept
	{
		if(!server())return false;
		const auto a=weapons::native_ammunition::observe_carried(game::g_entities[0].client,weapon);
		return a.valid && (a.loaded>0 || a.reserve>0) && weapons::native_ammunition::commit_carried(a,
			a.loaded-(a.loaded>0?1:0),a.reserve-(a.loaded>0?0:1));
	}
	launch_diagnostics launch_status()noexcept
	{return {attempts.load(),spawned.load(),actor_clearances.load(),world_clearances.load(),requested_speed.load(),native_speed.load(),obstruction.load()};}
	bool launch(std::uint32_t weapon,hands::vec position,hands::vec velocity,int fuse_ms,bool& committed)noexcept
	{
		descriptor d;
		++attempts;obstruction=-1;requested_speed=0;native_speed=0;
		if(!server() || !describe(weapon,d) || fuse_ms<0 || fuse_ms>60000 ||
			(behaviors[unsigned(d.type)].timed ? fuse_ms==0 : fuse_ms!=0))return false;
		for(float x:position)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
		for(float x:velocity)if(!std::isfinite(x) || std::abs(x)>10000)return false;
		requested_speed=hands::length(velocity);
		auto* player=&game::g_entities[0];const auto* ps=reinterpret_cast<const game::playerState_s*>(player->client);
		int native_cook{};
		if(!utils::native_memory::read_bytes(&native_cook,reinterpret_cast<const std::byte*>(ps)+0x64,4) || native_cook)return false;
		// Keep hand releases outside walls. A blocked release drops at the last
		// reachable point instead of passing through geometry or returning live ammo.
		hands::vec eye{ps->origin[0],ps->origin[1],ps->origin[2]+ps->viewHeightCurrent};
		for(float x:eye)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
		if(hands::length(hands::sub(position,eye))>200.f){position=eye;velocity={};}
		game::trace_t trace{};game::Bounds bounds{};
		bounds.halfSize[0]=bounds.halfSize[1]=bounds.halfSize[2]=d.radius;
		game::G_TraceCapsule(&trace,eye.data(),position.data(),&bounds,0,0x280e831);
		if(!std::isfinite(trace.fraction) || trace.fraction<0 || trace.fraction>1 || trace.startsolid || trace.allsolid)return false;
		if(trace.fraction<1)
		{
			position=hands::add(eye,hands::scale(hands::sub(position,eye),std::max(0.f,trace.fraction-.02f)));
			const void* actor{};std::uint8_t active{};
			const bool npc=trace.hitType==1 && trace.hitId>0 && trace.hitId<3998 &&
				utils::native_memory::read_bytes(&actor,reinterpret_cast<const std::byte*>(&game::g_entities[trace.hitId])+0x120,sizeof(actor)) && actor &&
				utils::native_memory::read_bytes(&active,reinterpret_cast<const std::byte*>(&game::g_entities[trace.hitId])+0xbc,1) && active;
			obstruction=trace.hitType==1?trace.hitId:-1;
			// Start outside the obstruction. An NPC must still receive the real
			// moving projectile through native hitbox/damage/pain handling.
			if(npc)++actor_clearances;else{velocity={};++world_clearances;}
		}
		if(!committed){if(!debit(weapon))return false;committed=true;}
		// Witnessed stock ABI: parent, start, velocity, weapon, rotation flag,
		// fuse milliseconds. Native owns projectile physics, think/explosion,
		// damage attribution, AI grenade response and grenade_fire script notify.
		native_speed=hands::length(velocity);
		const bool ok=spawn_projectile(weapon,position,velocity,true,fuse_ms)!=nullptr;
		if(ok)++spawned;return ok;
	}
}
