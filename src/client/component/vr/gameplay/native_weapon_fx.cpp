#include <std_include.hpp>
#include "native_weapon_fx.hpp"
#include "native_fx_world_space.hpp"
#include "tube_profile.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <mutex>
#include <sstream>

namespace vr::gameplay::weapons::native_weapon_fx
{
	namespace
	{
		constexpr std::uintptr_t flash_query=0x1406A9440,brass_query=0x1406A95F0,last_brass_query=0x1406A9490;
		constexpr std::uintptr_t play_oriented=0x140453F50;
		std::atomic_bool ready{};
		struct counters {std::uint64_t shots{},flashes{},shells{},no_flash{},no_brass{},extractions{};std::string flash,brass;};
		std::array<counters,512> counts{};
		std::mutex mutex;
		native_fx::world_space_cache shell_effects;
		std::mutex shell_mutex;
		std::atomic_uint64_t shell_depth_rejections{};
		game::FxEffectDef* world_shell(game::FxEffectDef* source) noexcept
		{
			if(!source)return nullptr;
			const std::lock_guard lock(shell_mutex);
			auto* effect=shell_effects.get(source);
			if(!effect)++shell_depth_rejections;
			return effect;
		}
		template<std::size_t N> bool verify(std::uintptr_t address,const std::array<std::uint8_t,N>& bytes) noexcept
		{
			std::array<std::uint8_t,N> mask;mask.fill(255);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes.data(),mask.data(),N}));
		}
		bool valid(const hands::anchor& anchor) noexcept
		{
			for (const auto x:anchor.position) if (!std::isfinite(x) || std::abs(x)>1.e7f) return false;
			float length{};for (const auto x:anchor.rotation) {if (!std::isfinite(x)) return false;length+=x*x;}
			return std::abs(length-1.f)<.01f;
		}
		void emit(game::FxEffectDef* effect,int time,const hands::anchor& pose)
		{
			const std::array<hands::vec,3> axis{hands::rotate(pose.rotation,{1,0,0}),
				hands::rotate(pose.rotation,{0,1,0}),hands::rotate(pose.rotation,{0,0,1})};
			// Native oriented one-shot wrapper queues a copied position and axis
			// in the FX system. No temporary entity/DObj, shared asset edit, selected
			// weapon lookup or pointers retained to this stack. Native FX owns life,
			// collision and shell physics, including its bounded spawn budget.
			utils::hook::invoke<void>(play_oriented,0,effect,time,pose.position.data(),axis.data());
		}
		std::string name(const game::FxEffectDef* effect)
		{return effect && effect->name ? std::string(effect->name,strnlen_s(effect->name,256)) : std::string{};}

	}
	bool initialize() noexcept
	{
		if(ready)return true;
		static_assert(offsetof(game::WeaponDef,viewFlashEffect)==0xf8);
		static_assert(offsetof(game::WeaponDef,viewShellEjectEffect)==0x438);
		static_assert(offsetof(game::WeaponDef,viewLastShotEjectEffect)==0x448);
		const std::array<std::uint8_t,9> query{0x89,0x4c,0x24,0x08,0x53,0x48,0x83,0xec,0x20};
		ready=verify(flash_query,query) && verify(brass_query,query) && verify(last_brass_query,query) &&
			verify(flash_query+0x38,std::array<std::uint8_t,6>{0x41,0xb8,0xf8,0,0,0}) &&
			verify(brass_query+0x38,std::array<std::uint8_t,6>{0x41,0xb8,0x38,4,0,0}) &&
			verify(last_brass_query+0x38,std::array<std::uint8_t,6>{0x41,0xb8,0x48,4,0,0}) &&
			verify(play_oriented,std::array<std::uint8_t,12>{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48}) &&
			// Native model FX maps element flag 0x800 to scene depth-hack bit 1.
			verify(0x14042EC71,std::array<std::uint8_t,19>{0x41,0x8b,0x01,0x8b,0xd1,0x83,0xca,0x01,
				0x25,0x00,0x08,0x00,0x00,0x41,0x8b,0xc0,0x0f,0x44,0xd1});
		if(ready)fastfiles::on_pre_unload([] {
			// Same drained native zone boundary as native_followed_fx; queued
			// particles must finish before their private descriptors are retired.
			const std::lock_guard lock(shell_mutex);shell_effects.clear();
		});
		return ready;
	}
	void play(const feedback::event& event) noexcept
	{
		const auto token=event.secondary_definition?event.secondary_definition:event.owner.weapon;
		const bool extraction=event.kind==mechanics::effect::case_eject && event.mechanical_instance &&
			((event.definition && event.definition->ammunition.feed==mechanics::feed_type::manual_bolt) ||
			(event.tube_definition && tube::manual(event.tube_definition->ammunition)));
		if (!ready || !scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized() ||
			(!extraction && (!event.independent_shot || event.kind!=mechanics::effect::shot)) || !token || token>=counts.size() ||
			(!extraction && !valid(event.muzzle))) return;
		auto* flash=extraction ? nullptr : utils::hook::invoke<game::FxEffectDef*>(flash_query,token,false);
		game::FxEffectDef* brass{};
		const auto* def=game::weapon_defs[token];
		if (event.has_brass && valid(event.brass) && def && (extraction ? def->boltAction : !def->boltAction))
		{
			if (event.last_shot) brass=utils::hook::invoke<game::FxEffectDef*>(last_brass_query,token,false);
			if (!brass) brass=utils::hook::invoke<game::FxEffectDef*>(brass_query,token,false);
		}
		const int time=game::CG_GetGameTime(0);
		if (flash) emit(flash,time,event.muzzle);
		brass=world_shell(brass);
		if (brass) emit(brass,time,event.brass);
		// Counters mean calls admitted to the native bounded queue, not a claim
		// that every particle survived native culling/resource limits.
		const std::lock_guard lock(mutex);auto& c=counts[token];
		if (extraction) ++c.extractions; else ++c.shots;
		if (flash) {++c.flashes;c.flash=name(flash);} else if (!extraction) ++c.no_flash;
		if (brass) {++c.shells;c.brass=name(brass);} else ++c.no_brass;
	}
	bool play_frontend(game::FxEffectDef* effect,const hands::anchor& pose,int start_time) noexcept
	{
		if(!ready || !effect || !game::CL_IsCgameInitialized() || !valid(pose))return false;
		const int now=game::CG_GetGameTime(0);if(start_time<0)start_time=now;
		if(start_time>now || std::int64_t(now)-start_time>60000)return false;
		emit(effect,start_time,pose);return true;
	}
	bool play_shell_frontend(game::FxEffectDef* effect,const hands::anchor& pose) noexcept
	{
		if(!ready || !game::CL_IsCgameInitialized() || !valid(pose))return false;
		effect=world_shell(effect);
		if(!effect)return false;
		emit(effect,game::CG_GetGameTime(0),pose);return true;
	}
	std::string status()
	{
		const std::lock_guard lock(mutex);std::ostringstream out;out << "native_fx_ready=" << ready << '\n';
		{const std::lock_guard shell_lock(shell_mutex);out << "world_shell_definitions=" << shell_effects.size()
			<< " shell_depth_rejections=" << shell_depth_rejections.load() << '\n';}
		for (std::size_t i=1;i<counts.size();++i) if (const auto& c=counts[i];c.shots || c.extractions)
			out << "weapon=" << i << " shots=" << c.shots << " flash_requests=" << c.flashes << " shell_requests=" << c.shells
				<< " manual_extractions=" << c.extractions << " no_flash=" << c.no_flash << " no_shell=" << c.no_brass << " flash=" << c.flash << " shell=" << c.brass << '\n';
		return out.str();
	}
}
