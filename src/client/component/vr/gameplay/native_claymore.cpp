#include <std_include.hpp>
#include "native_claymore.hpp"
#include "native_grenade.hpp"
#include "native_scripted_control.hpp"
#include "designator_events.hpp"
#include "native_action_slots.hpp"
#include "sequences/oilrig.hpp"
#include <utils/native_memory.hpp>
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "game/scripting/execution.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::equipment::special::native
{
	namespace
	{
		bool ready{};thread_local bool committing{};std::atomic<const char*> reason{"not initialized"};
		template<class T>T field(const void* p,unsigned at){T value{};std::memcpy(&value,static_cast<const std::byte*>(p)+at,sizeof(value));return value;}
		template<std::size_t N>bool verify(std::uintptr_t address,const std::uint8_t(&bytes)[N])
		{std::array<std::uint8_t,N> mask;mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));}
		bool server(){const auto* ps=reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);return ready && scheduler::is_executing(scheduler::pipeline::server) && game::CL_IsCgameInitialized() && ps && !(ps->e_flags&0x103000) && scripted_control::allowed(ps);}
		bool claymore(unsigned token){return token && token<512 && game::weapon_defs[token] && game::weapon_defs[token]->szInternalName && std::string_view(game::weapon_defs[token]->szInternalName)=="claymore";}
		bool current(weapons::native_carry::world_key key,unsigned weapon)
		{
			if(key.entity<=0 || key.entity>=3998 || weapons::native_carry::entity_key(key.entity)!=key)return false;
			const auto* e=&game::g_entities[key.entity];
			return field<std::uint8_t>(e,0xbc) && field<std::uint8_t>(e,0)==3 && field<unsigned>(e,0x80)==weapon && field<unsigned short>(e,0x178)==1;
		}
		bool terrain(const game::trace_t& t)
		{
			if(!std::isfinite(t.fraction) || t.fraction<0 || t.fraction>=1 || t.startsolid || t.allsolid)return false;
			for(float x:t.normal)if(!std::isfinite(x))return false;
			if(t.hitType==1 && static_cast<unsigned>(t.hitId)<3998)
			{
				const auto* e=&game::g_entities[t.hitId];
				if(e->client || field<void*>(e,0x120) || field<std::uint8_t>(e,0)==3)return false;
			}
			return t.normal[2]>=.7071067f;
		}
	}
	bool initialize()
	{
		constexpr std::uint8_t angles[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0x02,0x48,0x8b,0xda,0x89,0x41,0x40};
		constexpr std::uint8_t binding[]{0x40,0x53,0x55,0x57,0x48,0x81,0xec,0x30,0x04,0,0};
		// native_carry already validates and detours G_FreeEntity for generation
		// tracking. Reuse that owner instead of rejecting its installed entry hook.
		ready=grenades::native::initialize() && weapons::native_carry::initialize() && verify(0x140518180,angles) && verify(0x1403d3a90,binding);
		reason=ready?"native claymore ready":"native claymore contract rejected";return ready;
	}
	selection selected()
	{
		selection result;if(!server())return result;
		std::array<std::byte,0x1fc0> bytes{};
		if(!utils::native_memory::read_bytes(bytes.data(),game::g_entities[0].client,bytes.size()))return result;
		const auto mission_equipment=sequences::oilrig::current_equipment();
		for(const auto slot:weapon_slots(bytes))
		{
			if(!slot)continue;const auto* def=game::weapon_defs[slot.weapon];if(!def || !def->szInternalName)continue;
			const bool mine=claymore(slot.weapon);
			const std::string_view name(def->szInternalName);
			if(!sequences::oilrig::permits_equipment(mission_equipment,name))continue;
			const bool tool=name.find("designator")!=name.npos || name.find("predator")!=name.npos || name.find("agm")!=name.npos || name.find("detonator")!=name.npos;
			if(!mine && !tool && def->inventoryType!=game::WEAPINVENTORY_ITEM && def->inventoryType!=game::WEAPINVENTORY_EXCLUSIVE)continue;
			const auto ammo=weapons::native_ammunition::observe_carried(game::g_entities[0].client,slot.weapon);
			if(mine && !ammo.valid)continue;
			if(result.slot)return {}; // Ambiguous mission inventory must not choose arbitrarily.
			// Exodus binds usp_laserdesignator to a generic pistol world model.
			// Its authored view model is the actual device shown on the abdominal slot.
			const bool notebook=name=="remote_missile_detonator" || name=="remote_missile_detonator_finite";
			const bool designator=name.find("designator")!=name.npos;
			const auto* models=(designator || notebook) && def->gunModel && def->gunModel[0]?def->gunModel:
				def->worldModel && def->worldModel[0]?def->worldModel:def->gunModel;
			auto* model=mine && def->projectileModel?def->projectileModel:models?models[0]:nullptr;
			// The designator view asset is an empty container around one base model.
			// Unwrap only single-child containers; never silently drop assembly parts.
			for(unsigned depth=0;model && depth<4 && !model->numsurfs && model->numCompositeModels==1 && model->compositeModels;++depth)
				model=model->compositeModels[0];
			result={slot,mine,model,mine?ammo.loaded+ammo.reserve:1,notebook,designator,def->inventoryType==game::WEAPINVENTORY_PRIMARY};
		}
		return result;
	}
	placement trace(const anchor& held,vec head,float units,float bottom)
	{
		placement out;if(!server() || !std::isfinite(units) || units<=0 || !std::isfinite(bottom))return out;
		const auto forward=rotate(held.rotation,{1,0,0});const auto end=add(held.position,scale(forward,units*2.f));
		game::trace_t t{};game::Bounds point{};
		game::G_TraceCapsule(&t,held.position.data(),end.data(),&point,0,0x280e831);if(!terrain(t))return out;
		const vec normal{t.normal[0],t.normal[1],t.normal[2]};const auto ground=add(held.position,scale(sub(end,held.position),t.fraction));
		game::trace_t visible{};game::G_TraceCapsule(&visible,head.data(),ground.data(),&point,0,0x280e831);
		if(visible.startsolid || visible.allsolid || visible.fraction<.97f)return out;
		auto direction=sub(forward,scale(normal,dot(forward,normal)));
		if(length(direction)<.05f)direction=cross(rotate(held.rotation,{0,1,0}),normal);
		direction=unit(direction);const auto left=unit(cross(normal,direction));
		for(const auto offset:std::array<vec,5>{{{0,0,0},{.07f,.14f,0},{.07f,-.14f,0},{-.07f,.14f,0},{-.07f,-.14f,0}}})
		{
			const auto sample=add(ground,scale(add(scale(direction,offset[0]),scale(left,offset[1])),units));
			const auto from=add(sample,scale(normal,4)),to=sub(sample,scale(normal,4));game::trace_t support{};
			game::G_TraceCapsule(&support,from.data(),to.data(),&point,0,0x280e831);
			if(!terrain(support) || std::abs(support.fraction-.5f)>.3f)return out;
			const auto low=add(sample,scale(normal,.5f)),high=add(sample,scale(normal,10.f));game::trace_t clearance{};
			game::G_TraceCapsule(&clearance,low.data(),high.data(),&point,0,0x280e831);
			if(clearance.startsolid || clearance.allsolid || clearance.fraction<1.f)return out;
		}
		out={true,{add(ground,scale(normal,-bottom+.2f)),from_axis({direction,left,normal})},ground,normal};return out;
	}
	bool place(unsigned weapon,const placement& p)
	{
		if(!server() || committing || !claymore(weapon) || !p.valid)return false;
		committing=true;const auto done=gsl::finally([]{committing=false;});
		if(field<int>(game::g_entities[0].client,0x64)!=0)return false; // Never steal a native cooked grenade's timer.
		const auto a=weapons::native_ammunition::observe_carried(game::g_entities[0].client,weapon);
		if(!a.valid || a.loaded+a.reserve<=0)return false;
		const int load=a.loaded-(a.loaded>0),reserve=a.reserve-(a.loaded==0);
		if(!weapons::native_ammunition::commit_carried(a,load,reserve))return false;
		const auto forward=rotate(p.pose.rotation,{1,0,0});const auto velocity=add(scale(p.normal,-10000),scale(forward,50));
		auto* entity=grenades::native::spawn_projectile(weapon,add(p.pose.position,scale(p.normal,2)),velocity,false,0);
		if(!entity)
		{
			auto expected=a;expected.loaded=load;expected.reserve=reserve;
			(void)weapons::native_ammunition::commit_carried(expected,a.loaded,a.reserve);reason="native claymore spawn failed";return false;
		}
		const auto left=rotate(p.pose.rotation,{0,1,0}),up=rotate(p.pose.rotation,{0,0,1});constexpr float degrees=57.2957795131f;
		const vec angles{std::atan2(-forward[2],std::hypot(forward[0],forward[1]))*degrees,std::atan2(forward[1],forward[0])*degrees,std::atan2(left[2],up[2])*degrees};
		utils::hook::invoke<void>(0x140518180,entity,angles.data());reason="native claymore placed";return true;
	}
	weapons::native_carry::world_key target(vec origin,vec forward,float units)
	{
		if(!server() || !std::isfinite(units) || units<=0)return {};
		weapons::native_carry::world_key chosen;float nearest=units*2.2f;
		// Player traces may ignore player-owned missiles. Intersect their linked
		// bounds explicitly, then use the native world trace only for occlusion.
		// This bounded scan runs on a new free-hand grip edge, not every frame.
		for(int i=1;i<3998;++i)
		{
			const auto* e=&game::g_entities[i];if(!field<std::uint8_t>(e,0xbc) || field<std::uint8_t>(e,0)!=3)continue;
			const auto token=field<unsigned>(e,0x80);if(!claymore(token))continue;
			const auto key=weapons::native_carry::entity_key(i);if(!current(key,token))continue;
			const vec center{e->absBox.midPoint[0],e->absBox.midPoint[1],e->absBox.midPoint[2]};
			const vec half{e->absBox.halfSize[0]+units*.03f,e->absBox.halfSize[1]+units*.03f,e->absBox.halfSize[2]+units*.03f};
			const float distance=ray_box(origin,forward,center,half,nearest);if(!std::isfinite(distance))continue;
			game::Bounds point{};game::trace_t visible{};game::G_TraceCapsule(&visible,origin.data(),center.data(),&point,0,0x280e831);
			if(!(visible.hitType==1 && visible.hitId==i) && (visible.startsolid || visible.allsolid || visible.fraction<.99f))continue;
			chosen=key;nearest=distance;
		}
		return chosen;
	}
	bool recover(weapons::native_carry::world_key key,unsigned weapon)
	{
		if(!weapon && key.entity>0 && key.entity<3998)weapon=field<unsigned>(&game::g_entities[key.entity],0x80);
		if(!server() || committing || !claymore(weapon) || !current(key,weapon))return false;
		committing=true;const auto done=gsl::finally([]{committing=false;});
		const auto a=weapons::native_ammunition::observe_carried(game::g_entities[0].client,weapon);const auto* d=game::weapon_defs[weapon];
		if(!a.valid || a.loaded>=d->clipSize){reason="claymore inventory full";return false;}
		bool credited{};
		try
		{
			const scripting::entity object(game::scr_entref_t{static_cast<unsigned short>(key.entity),0});
			const scripting::entity player(game::scr_entref_t{0,0});const auto owner=object.get("owner");
			if(!owner.is<scripting::entity>() || owner.as<scripting::entity>()!=player)return false;
			// Admit the credit before removing the object, including sparse-table
			// capacity. A cleanup failure rolls back only this compared credit.
			if(!weapons::native_ammunition::commit_carried(a,a.loaded+1,a.reserve))return false;
			credited=true;
			// Death terminates detonation and wakes the native trigger cleanup thread.
			// G_FreeEntity alone only sends entitydeleted and leaves that wait pending.
			scripting::notify(object,"death",{});
			if(current(key,weapon))utils::hook::invoke<void>(0x140517890,&game::g_entities[key.entity]);
			if(current(key,weapon))throw std::runtime_error("native entity survived cleanup");
			reason="claymore recovered";return true;
		}
		catch(...)
		{
			if(credited && !current(key,weapon)){reason="claymore recovered";return true;}
			if(credited){auto expected=a;++expected.loaded;(void)weapons::native_ammunition::commit_carried(expected,a.loaded,a.reserve);}
			reason="claymore script cleanup rejected";return false;
		}
	}
	void activate(action_slot slot,std::function<bool()> authorized)
	{
		if(!server() || !slot)return;
		const auto laser=designator_events::activation(slot.weapon);
		if(laser.device)
		{
			if(!authorized || authorized())(void)designator_events::request_activation(slot.weapon);
			return;
		}
		action_slots::request(slot.index,1,slot.weapon,std::move(authorized));
	}
	agm_state observe_agm()noexcept
	{
		if(!ready || !scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized() || !*game::levelEntityId || !game::g_entities[0].client)return {};
		try{const scripting::entity player{game::scr_entref_t{0,0}};
			const auto using_uav=player.get("using_uav"),remote=player.get("is_controlling_uav");
			return {true,using_uav.is<int>() && using_uav.as<int>()!=0,remote.is<int>() && remote.as<int>()!=0};}
		catch(...){return {};}
	}
	const char* status()noexcept{return reason.load();}
}
