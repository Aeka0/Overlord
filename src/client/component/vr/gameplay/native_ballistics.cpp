#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "native_scripted_control.hpp"
#include "native_ballistics.hpp"
#include "official_cheats.hpp"
#include "native_shield.hpp"
#include "muzzle_clearance.hpp"
#include "weapon_carry_runtime.hpp"
#include "component/scheduler_context.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::native_ballistics
{
	namespace
	{
		bool verified{};
		thread_local bool emitting{};
		constexpr std::uintptr_t definitions = 0x14CE01580;
		template <std::size_t N> bool verify(std::uintptr_t address, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t,N> mask{}; mask.fill(0xff);
			return static_cast<bool>(utils::hook_validation::verify_masked_bytes(
				reinterpret_cast<void*>(address), {bytes,mask.data(),N}));
		}
		bool valid(const shot_geometry& g)
		{
			muzzle_frame muzzle;
			muzzle.position=g.origin;
			muzzle.axis={g.forward,hands::scale(g.right,-1),g.up};
			return valid_geometry(muzzle);
		}
	}
	bool initialize()
	{
		if (verified) return true;
		constexpr std::uint8_t wrapper[]{0x48,0x83,0xec,0x48,0xf3,0x0f,0x10,0x44,0x24,0x78,
			0x8b,0x44,0x24,0x70,0xf3,0x0f,0x11,0x44,0x24,0x30,0xc6,0x44,0x24,0x28,0x00,
			0x89,0x44,0x24,0x20,0xe8,0x3e,0x0a,0x00,0x00};
		constexpr std::uint8_t descriptor[]{0x0f,0xb6,0x53,0x44,0x8b,0x4b,0x3c};
		constexpr std::uint8_t spread[]{0x48,0x83,0xec,0x28,0x45,0x0f,0xb6,0xc8,0x8b,0xc2,
			0x4c,0x8d,0x05,0xff,0xfe,0x75,0x0c};
		constexpr std::uint8_t type_entry[]{0x40,0x53,0x48,0x83,0xec,0x50,0x48,0x8b,0x05,0x13,0xff,0x56,0x00};
		constexpr std::uint8_t trace[]{0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xec,0x78};
		constexpr std::uint8_t player_gate[]{0x41,0xf7,0x46,0x58,0x00,0x30,0x10,0x00};
		verified=verify(bullet_address,wrapper) && verify(0x1404AAE27,descriptor) &&
			verify(ads_spread_address,spread) && verify(vr::h2::sp::weapon_type.address(),type_entry) &&
			verify(0x1404CBFE0,trace) && verify(0x140518A7F,player_gate) && native_ammunition::initialize();
		return verified;
	}
	bool firing_timing(std::uint32_t weapon,int shot_count,independent_fire::timing& out)
	{
		// Same descriptor-based mode and timing accessors as PM_Weapon_Fire,
		// independent of selected PS state and native akimbo timers.
		constexpr std::uint8_t mode[]{0x48,0x83,0xec,0x28,0x41,0xb8,0xe4,0x05,0,0};
		constexpr std::uint8_t timing[]{0x89,0x4c,0x24,0x08,0x55,0x57,0x48,0x83,0xec,0x28};
		constexpr std::uint8_t burst[]{0x48,0x83,0xec,0x28,0x41,0xb8,0xf0,0x05,0,0,0xe8,0x81,0xc1,0xff,0xff};
		static const bool timing_ready=verify(0x1406A5180,mode) && verify(0x1406A3EF0,timing) && verify(0x1406A39F0,burst);
		if (!verified || !timing_ready || !weapon || weapon>=512 || !game::weapon_defs[weapon]) return false;
		out.mode=utils::hook::invoke<int>(0x1406A5180,weapon,false);
		utils::hook::invoke<void>(0x1406A3EF0,weapon,false,false,std::clamp(shot_count,0,31),&out.interval,&out.delay);
		// Native PM_Weapon (0x14069C3AA) rounds this accessor's float and stores
		// it in weaponTime without a seconds conversion. Keep attachment overrides.
		const auto pause=out.mode>=2 && out.mode<=4 ? utils::hook::invoke<float>(0x1406A39F0,weapon,false) : 0.f;
		return independent_fire::decode_native_timing(out.mode,out.interval,out.delay,pause,out);
	}
	result fire_owned(std::uint32_t weapon,const shot_geometry& geometry,int command_time,debit_observer settled)
	{ return fire_owned(carry::native_identity(weapon),geometry,command_time,settled); }
	result fire_owned(weapon_identity id,const shot_geometry& geometry,int command_time,debit_observer settled)
	{
		const auto weapon=id.weapon;
		result out;
		if (!verified) return out;
		out.status=outcome::invalid_context;
		if (emitting || cheats::transitioning() || !scheduler::is_executing(scheduler::pipeline::server) ||
			!game::CL_IsCgameInitialized()) return out;
		auto* entity=&game::g_entities[0];
		auto* ps=reinterpret_cast<game::playerState_s*>(entity->client);
		if (!ps || ps->commandTime!=command_time || (ps->e_flags & 0x103000) || !scripted_control::firing_allowed(ps)) return out;
		const auto* paused=game::Dvar_FindVar("cl_paused");
		if (!paused || paused->current.integer || *game::keyCatchers) return out;
		out.status=outcome::invalid_geometry;
		if (!valid(geometry)) return out;
		out.status=outcome::unsupported_weapon;
		const auto owned=native_ammunition::observe_carried(ps,id);
		if (!owned.valid || vr::h2::sp::weapon_type(weapon,false)!=1) return out;
		out.before=owned;
		out.status=outcome::empty;
		if (out.before.loaded<=0) return out;
		hands::vec eye{ps->origin[0],ps->origin[1],ps->origin[2]+ps->viewHeightCurrent};
		out.status=outcome::invalid_geometry;
		for (float x : eye) if (!std::isfinite(x)) return out;
		if (hands::length(hands::sub(eye,geometry.origin))>200.f) return out;
		out.status=outcome::obstruction;
		if(!shield::firing_clear(id,geometry.origin))return out;
		game::trace_t sweep{};
		game::Bounds point{};
		game::G_TraceCapsule(&sweep,eye.data(),geometry.origin.data(),&point,entity->s.entityNum,muzzle_clearance_mask);
		if (!std::isfinite(sweep.fraction) || sweep.fraction<1 || sweep.allsolid || sweep.startsolid) return out;
		parameters packet{};
		packet.shot=geometry;
		packet.weapon=weapon;
		packet.definition=reinterpret_cast<void* const*>(definitions)[weapon];
		const auto spread=utils::hook::invoke<float>(ads_spread_address,ps,weapon,false,0);
		out.status=outcome::unsupported_weapon;
		if (!packet.definition || !std::isfinite(spread) || spread<0 || spread>90) return out;
		const auto previous=native_ammunition::projected_identity(weapon);
		if (id && !native_ammunition::project(id)) return out;
		const auto restore_projection=gsl::finally([&]{if (previous) native_ammunition::project(previous);});
		out.status=outcome::ammunition_changed;
		const bool sustained=cheats::sustain_ammo();
		auto debited=out.before;debited.loaded-=int(!sustained);
		if (entity->client!=reinterpret_cast<game::gclient_s*>(ps) || ps->commandTime!=command_time || !scripted_control::firing_allowed(ps) ||
			!native_ammunition::commit_carried(out.before,debited.loaded,out.before.reserve)) return out;
		// Settle the existing mechanical owner before ballistics can run damage
		// or script callbacks. The observer never writes native ammunition.
		if (settled && !settled(out.before,debited,sustained)) {out.status=outcome::mechanical_changed;return out;}
		// Never refund after entry: damage/scripts may have committed effects.
		emitting=true;
		const auto restore=gsl::finally([] {emitting=false;});
		utils::hook::invoke<void>(bullet_address,entity,spread,&packet,entity,command_time,-1.f);
		out.status=outcome::emitted;
		if (entity->client==reinterpret_cast<game::gclient_s*>(ps))
			out.after=native_ammunition::observe_carried(ps,id);
		return out;
	}
}
