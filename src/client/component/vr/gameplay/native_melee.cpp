#include <std_include.hpp>
#include "native_melee.hpp"
#include "native_melee_events.hpp"
#include "official_cheats.hpp"
#include "native_weapon_read.hpp"
#include "native_scripted_control.hpp"
#include "trainer_policy.hpp"
#include "component/scheduler_context.hpp"
#include "game/game.hpp"
#include "game/scripting/entity.hpp"
#include "game/scripting/script_value.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::melee::native
{
	namespace
	{
		bool verified{};thread_local bool committing{};
		std::atomic_uint64_t hit_events{},blood_events{},shield_hit_events{},shield_blood_events{};
		template<class T> T read(const void* p,std::size_t offset) noexcept {T v{};std::memcpy(&v,static_cast<const std::byte*>(p)+offset,sizeof(v));return v;}
		template<std::size_t N> bool verify(std::uintptr_t at,const std::uint8_t (&bytes)[N])
		{std::array<std::uint8_t,N> mask;mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),{bytes,mask.data(),N}));}
		bool npc(int number) noexcept
		{
			if (number<=0 || number>=3998) return false;
			const auto* e=&game::g_entities[number];
			return read<std::uint8_t>(e,0xbc) && read<void*>(e,0x120) && read<void*>(e,0x128) && read<std::uint8_t>(e,0x149) && read<int>(e,0x184)>0;
		}
		bool training_target(int number,tool kind)
		{
			if (kind!=tool::knife || number<=0 || number>=3998) return false;
			const auto* map=game::Dvar_FindVar("mapname");
			if (!map || !map->current.string || std::string_view(map->current.string)!="trainer") return false;
			const auto* e=&game::g_entities[number];
			if (!read<std::uint8_t>(e,0xbc) || !read<std::uint8_t>(e,0x149) || !e->script_classname) return false;
			const auto* classname=game::SL_ConvertToString(e->script_classname);
			if (!classname || std::string_view(classname)!="script_model") return false;
			try
			{
				const scripting::entity target{game::scr_entref_t{static_cast<unsigned short>(number),0}};
				const auto noteworthy=target.get(std::string("script_noteworthy"));
				return noteworthy.is<std::string>() && trainer::knife_target(map->current.string,classname,noteworthy.as<std::string>(),true,kind);
			}
			catch (const std::exception&) {return false;}
		}
	}
	bool initialize()
	{
		constexpr std::uint8_t trace_entry[]{0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xec,0x78};
		constexpr std::uint8_t damage_body[]{0x41,0x54,0x41,0x56,0x48,0x81,0xec,0xd8,0x04,0,0};
		constexpr std::uint8_t damage_accessor[]{0x48,0x83,0xec,0x28,0x41,0xb8,0x08,0x07,0,0,0xe8,0xb1,0xb7,0xff,0xff};
		constexpr std::uint8_t native_call[]{0xe8,0xe4,0xfd,0xf9,0xff};
		constexpr std::uint8_t hit_call[]{0xe8,0xa3,0xb0,0xff,0xff};
		constexpr std::uint8_t blood_call[]{0xe8,0x69,0x8a,0xff,0xff};
		constexpr std::uint8_t temp_entry[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x50};
		constexpr std::uint8_t event_entry[]{0x40,0x53,0x48,0x83,0xec,0x20,0x41,0x8b,0xc0,0x44,0x8b,0xd2};
		// The generic notifies component already detours G_Damage. Validate its
		// untouched body and the native melee caller, then call the public entry
		// so existing script/Lua damage policy continues to participate.
		verified=verify(0x1404c91b0,trace_entry) && verify(0x1404bd2e5,damage_body) && verify(0x1406a45d0,damage_accessor) && verify(0x14051d4f7,native_call) &&
			verify(0x14051d338,hit_call) && verify(0x14051d532,blood_call) && verify(0x1405183e0,temp_entry) && verify(0x140515fa0,event_entry);
		return verified;
	}
	bool allowed() noexcept
	{
		if (!verified || committing || cheats::transitioning() || !scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized()) return false;
		const auto* ps=reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);
		const auto* paused=game::Dvar_FindVar("cl_paused");
		return ps && !(ps->e_flags&0x103000) && scripted_control::allowed(ps) && paused && !paused->current.integer && !*game::keyCatchers;
	}
	hit trace(vec from,vec to,vec head,float units,tool kind)
	{
		if (!allowed() || !std::isfinite(units) || units<=0) return {};
		for (auto v:{from,to,head}) for (float x:v) if (!std::isfinite(x) || std::abs(x)>1e7f) return {};
		if (length(sub(from,head))>1.5f*units || length(sub(to,head))>1.5f*units) return {};
		game::trace_t result{};
		// G_LocationalTrace is exactly the model/surface trace used by native
		// melee (0x14051c96e). No NPC origin sphere or head-forward auto targeting.
		utils::hook::invoke<void>(0x1404c91b0,&result,from.data(),to.data(),0,0x280e091,reinterpret_cast<const void*>(0x140bf4a80));
		if (!std::isfinite(result.fraction) || result.fraction<0 || result.fraction>=1 || result.hitType!=1 ||
			(result.surfaceFlags&0x10)) return {};
		const bool flesh=npc(result.hitId);
		if (!flesh && !training_target(result.hitId,kind)) return {};
		const auto point=add(from,scale(sub(to,from),result.fraction));
		game::Bounds bounds{};game::trace_t clearance{};
		game::G_TraceCapsule(&clearance,head.data(),point.data(),&bounds,0,0x280e831);
		if (!std::isfinite(clearance.fraction) || clearance.startsolid || clearance.allsolid ||
			(clearance.fraction<.999f && !(clearance.hitType==1 && clearance.hitId==result.hitId))) return {};
		const auto delta=sub(to,from);const auto distance=length(delta);if (distance<.0001f) return {};
		return {weapons::native_carry::entity_key(result.hitId),point,scale(delta,1/distance),read<unsigned>(&result,0x20),read<std::uint16_t>(&result,0x24),read<std::uint16_t>(&result,0x26),result.fraction,flesh};
	}
	int damage(const hit& contact,std::uint32_t weapon,tool kind,std::uint32_t tactical_knife)
	{
		if(!allowed() || !contact)return 0;
		const auto definition=weapons::native_weapon_read::get(weapon);
		if (!definition ||
			weapons::native_carry::entity_key(contact.target.entity).generation!=contact.target.generation || contact.hit_location>=19) return 0;
		if (contact.flesh ? !npc(contact.target.entity) : !training_target(contact.target.entity,kind)) return 0;
		const auto native_amount=utils::hook::invoke<int>(0x1406a45d0,weapon,false);
        const bool impact=cheats::ragdoll_impact();
        const auto knife_amount=impact && weapons::native_weapon_read::get(tactical_knife) ?
            utils::hook::invoke<int>(0x1406a45d0,tactical_knife,false):0;
        const auto amount=scaled_damage(native_amount,kind,impact,knife_amount);if (!amount) return 0;
		if(kind==tool::shield && definition.definition->weapType!=game::WEAPTYPE_RIOTSHIELD)return 0;
		auto* target=&game::g_entities[contact.target.entity];auto* player=&game::g_entities[0];const int before=read<int>(target,0x184);
		committing=true;const auto restore=gsl::finally([]{committing=false;});
		// Native melee sends a separate contact event before G_Damage. That
		// event owns weapon-specific impact audio/FX; G_Damage alone never sends it.
		// Use the physical contact, not the stock forward search/auto-lunge.
		if (bladed(kind) || kind==tool::shield)
		{
			const auto* ps=reinterpret_cast<const game::playerState_s*>(player->client);
			if (ps && events::valid_weapon_hit(target->s.entityNum,player->s.entityNum,weapon))
			{
				auto* event=utils::hook::invoke<game::gentity_s*>(0x1405183e0,contact.point.data(),events::hit);
				if (event && events::weapon_hit({reinterpret_cast<std::byte*>(event),sizeof(*event)},target->s.entityNum,player->s.entityNum,weapon,ps->pm_flags,bladed(kind)))
				{if(kind==tool::shield)++shield_hit_events;else ++hit_events;}
			}
		}
		// Same complete G_Damage ABI and MOD_MELEE=8 as 0x14051d4f7. Native
		// actor pain, death, invulnerability and mission callbacks own the
		// result; never subtract health or force an AI animation from the adapter.
		utils::hook::invoke<void>(0x1404bd2e0,target,player,player,contact.direction.data(),contact.point.data(),amount,0,8u,
			weapon,false,0u,int(contact.hit_location),contact.model_index,contact.part_name);
		// Trainer script models consume the native damage notification, not HP
		// loss. Their listener owns scoring, civilian penalties and target reset.
		if (!contact.flesh) return amount;
		if (weapons::native_carry::entity_key(contact.target.entity).generation!=contact.target.generation) return amount;
		const int accepted=int(std::clamp<std::int64_t>(std::int64_t(before)-read<int>(target,0x184),0,amount));
		// Stock 0x14051d50f sends EV_MELEE_BLOOD only for player -> actor damage.
		// Physical knife FX is queued by the equipment owner at its real socket.
		// Stock EV_MELEE_BLOOD reselects the current firearm and requires a
		// tag_knife_fx on its flat viewmodel; sending it too duplicates USP hits.
		// Shield retains its existing native event behavior.
		if (kind==tool::shield && accepted>0 && player->client && read<void*>(target,0x120))
		{utils::hook::invoke<void>(0x140515fa0,player,events::blood,0);++shield_blood_events;}
		return accepted;
	}
	feedback_counts feedback_status() noexcept {return {hit_events.load(),blood_events.load(),shield_hit_events.load(),shield_blood_events.load()};}
}
