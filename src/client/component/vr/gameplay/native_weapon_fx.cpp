#include <std_include.hpp>
#include "native_weapon_fx.hpp"
#include "native_fx_world_space.hpp"
#include "native_fx_checkpoint.hpp"
#include "native_spent_case_assets.hpp"
#include "native_weapon_read.hpp"
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
		constexpr std::uintptr_t surface_impact = 0x1404608C0, impact_row = 0x140460940,
		                         direct_impact = 0x140460810, impact_table = 0x141B95A38;
		utils::hook::detour surface_impact_hook;
		std::atomic_bool ready{};
		struct counters {std::uint64_t shots{},flashes{},shells{},no_flash{},no_brass{},extractions{};std::string flash,brass;};
		std::array<counters,512> counts{};
		std::mutex mutex;
		native_fx::world_space_cache shell_effects;
		spent_case::assets case_assets;
		std::size_t published_shell_definitions{};
		std::mutex shell_mutex;
		std::atomic_uint64_t shell_depth_rejections{};
		game::FxEffectDef* surface_effect(const game::FxEffectDef* root, unsigned surface)
		{
			if (!root || !root->name || surface >= native_fx::world_space_cache::surface_count)
				return nullptr;
			auto name = std::string_view(
			    root->name, strnlen_s(root->name, native_fx::checkpoint::source_name_limit + 64));
			if (const auto id = native_fx::checkpoint::parse(name))
				name = id->source;
			const int row = utils::hook::invoke<int>(impact_row, name.data());
			if (row < 0 || row >= 8)
				return nullptr;
			const void* table{};
			game::FxEffectDef** entries{};
			game::FxEffectDef* effect{};
			if (!utils::native_memory::read_bytes(
			        &table, reinterpret_cast<const void*>(impact_table), sizeof(table)) ||
			    !table || !utils::native_memory::read_at(table, 8, entries) || !entries ||
			    !utils::native_memory::read_at(
			        entries, (std::size_t(row) * 53 + surface) * sizeof(void*), effect))
				return nullptr;
			return effect;
		}
		game::FxEffectDef* world_shell(game::FxEffectDef* source, unsigned case_variant = 0) noexcept
		{
			if(!source)return nullptr;
			const std::lock_guard lock(shell_mutex);
			native_fx::world_space_cache::model_variant replacement;
			if (case_variant)
			{
				replacement = {case_variant, case_assets.peek(case_variant)};
				if (!replacement.model && scheduler::is_executing(scheduler::pipeline::main))
					replacement.model = case_assets.get(case_variant);
				if (!replacement.model)
				{
					++shell_depth_rejections;
					return nullptr;
				}
			}
			auto* effect = shell_effects.get(source, replacement, surface_effect);
			if(effect && published_shell_definitions!=shell_effects.size())
			{
				const auto definitions=shell_effects.checkpoint_definitions();
				if(native_fx::checkpoint::publish({definitions.data()+published_shell_definitions,
					shell_effects.size()-published_shell_definitions}))published_shell_definitions=shell_effects.size();
				else effect=nullptr;
			}
			if(!effect)++shell_depth_rejections;
			return effect;
		}
		void surface_impact_stub(void* system,
		                         void* element,
		                         game::FxEffectDef* root,
		                         int time,
		                         const float* normal,
		                         unsigned surface)
		{
			const auto id =
			    root && root->name
			        ? native_fx::checkpoint::parse(
			              {root->name, strnlen_s(root->name, native_fx::checkpoint::source_name_limit + 64)})
			        : std::nullopt;
			if (!id || id->kind == native_fx::checkpoint::variant::followed)
			{
				surface_impact_hook.invoke<void>(system, element, root, time, normal, surface);
				return;
			}
			// Preserve H2's material choice/sound, but carry the same world-depth
			// and case identity into the table-selected resting effect.
			auto* chosen = surface_effect(root, surface);
			auto* effect =
			    world_shell(chosen, id->kind == native_fx::checkpoint::variant::casing ? id->attached : 0);
			if (effect)
				utils::hook::invoke<void>(direct_impact, system, element, effect, time, normal);
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
		ready = verify(flash_query, query) && verify(brass_query, query) && verify(last_brass_query, query) &&
		        verify(flash_query + 0x38, std::array<std::uint8_t, 6>{0x41, 0xb8, 0xf8, 0, 0, 0}) &&
		        verify(brass_query + 0x38, std::array<std::uint8_t, 6>{0x41, 0xb8, 0x38, 4, 0, 0}) &&
		        verify(last_brass_query + 0x38, std::array<std::uint8_t, 6>{0x41, 0xb8, 0x48, 4, 0, 0}) &&
		        verify(play_oriented,
		               std::array<std::uint8_t, 12>{
		                   0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48}) &&
		        // Native model FX maps element flag 0x800 to scene depth-hack bit 1.
		        verify(0x14042EC71,
		               std::array<std::uint8_t, 19>{0x41,
		                                            0x8b,
		                                            0x01,
		                                            0x8b,
		                                            0xd1,
		                                            0x83,
		                                            0xca,
		                                            0x01,
		                                            0x25,
		                                            0x00,
		                                            0x08,
		                                            0x00,
		                                            0x00,
		                                            0x41,
		                                            0x8b,
		                                            0xc0,
		                                            0x0f,
		                                            0x44,
		                                            0xd1}) &&
		        verify(surface_impact,
		               std::array<std::uint8_t, 12>{
		                   0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48}) &&
		        verify(impact_row,
		               std::array<std::uint8_t, 10>{
		                   0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10}) &&
		        verify(surface_impact + 0x26, std::array<std::uint8_t, 4>{0x4c, 0x6b, 0xc0, 0x35}) &&
		        verify(surface_impact + 0x32,
		               std::array<std::uint8_t, 7>{0x48, 0x8b, 0x05, 0x3f, 0x51, 0x73, 0x01}) &&
		        verify(direct_impact,
		               std::array<std::uint8_t, 9>{0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x78}) &&
		        native_fx::checkpoint::initialize();
		if (ready)
			surface_impact_hook.create(surface_impact, surface_impact_stub);
		if(ready)fastfiles::on_pre_unload([] {
			// Same drained native zone boundary as native_followed_fx; queued
			// particles must finish before their private descriptors are retired.
			const std::lock_guard lock(shell_mutex);
			native_fx::checkpoint::remove(native_fx::checkpoint::variant::world);
			native_fx::checkpoint::remove(native_fx::checkpoint::variant::casing);
			shell_effects.clear();
			published_shell_definitions = 0;
			case_assets.clear();
		});
		if (ready)
			scheduler::loop(
			    []
			    {
				    if (!game::CL_IsCgameInitialized())
					    return;
				    const std::lock_guard lock(shell_mutex);
				    for (unsigned id = 1; id <= spent_case::sources.size(); ++id)
					    (void)case_assets.get(id);
			    },
			    scheduler::pipeline::main,
			    250ms);
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
		unsigned case_variant{};
		if (!event.secondary_definition && !event.vehicle)
		{
			const auto weapon = native_weapon_read::get(token);
			if (weapon)
				case_variant =
				    spent_case::variant_for(weapon.name.data(),
				                            event.definition && event.definition->rigid_magazine_source
				                                ? event.definition->rigid_magazine_source
				                                : "");
		}
		brass = world_shell(brass, case_variant);
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
	game::FxEffectDef* restore_definition(game::FxEffectDef* source, unsigned case_variant) noexcept
	{
		if (!case_variant)
			return world_shell(source);
		bool prepared{};
		{
			const std::lock_guard lock(shell_mutex);
			prepared = case_assets.peek(case_variant) != nullptr;
		}
		if (prepared)
			return world_shell(source, case_variant);
		// Native checkpoint loading can precede CL initialization/main
		// scheduler dispatch. Admit GPU/model reconstruction only on H2's
		// actual main thread; never impersonate it from an archive worker.
		if (!game::Sys_IsMainThread())
			return nullptr;
		const scheduler::detail::execution_scope main(scheduler::pipeline::main);
		return world_shell(source, case_variant);
	}
	    std::string status()
	{
			const std::lock_guard lock(mutex);std::ostringstream out;out << "native_fx_ready=" << ready << '\n';
			out << native_fx::checkpoint::status();
		    {
			    const std::lock_guard shell_lock(shell_mutex);
			    out << "world_shell_definitions=" << shell_effects.size()
			        << " shell_depth_rejections=" << shell_depth_rejections.load() << '\n';
			    for (unsigned id = 1; id <= spent_case::sources.size(); ++id)
				    out << "spent_case_asset(" << spent_case::sources[id - 1].source
				        << ")=" << case_assets.status(id) << '\n';
		    }
		for (std::size_t i=1;i<counts.size();++i) if (const auto& c=counts[i];c.shots || c.extractions)
			out << "weapon=" << i << " shots=" << c.shots << " flash_requests=" << c.flashes << " shell_requests=" << c.shells
				<< " manual_extractions=" << c.extractions << " no_flash=" << c.no_flash << " no_shell=" << c.no_brass << " flash=" << c.flash << " shell=" << c.brass << '\n';
		return out.str();
	}
}
