#include <std_include.hpp>
#include "weapon_hud_source.hpp"
#include "../controller_input.hpp"
#include "weapon_carry_runtime.hpp"
#include "native_ammunition.hpp"
#include "underbarrel_native.hpp"
#include "physical_reload_runtime.hpp"
#include "cylinder_runtime.hpp"
#include "break_action_runtime.hpp"
#include "tube_runtime.hpp"
#include "underbarrel_runtime.hpp"
#include "weapon_hud_warning.hpp"
#include "component/scheduler.hpp"
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapon_hud
{
	namespace
	{
		std::mutex mutex;
		std::array<source_snapshot,source_count> snapshots{},queried{},rendered{};
		bool ready{};
		template<class T> T field(const void* p,size_t offset)
		{T value{};std::memcpy(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));return value;}
		// HUD admission needs native inventory and display metadata, not a
		// registered physical-reload recipe. Keep the risky asset read in a
		// destructor-free leaf so a bad optional definition cannot fault the UI.
		bool display_definition(const game::WeaponDef* def,std::array<char,64>& name,
			int& capacity,int& maximum_reserve,float& low_threshold) noexcept
		{
			if (!def) return false;
			__try
			{
				capacity=def->clipSize;
				maximum_reserve=field<int>(def,0x6e8);
				low_threshold=field<float>(def,0xbc0);
				const auto* native_name=def->szInternalName;
				if (reinterpret_cast<std::uintptr_t>(native_name)<0x10000) return false;
				for (std::size_t i=0;i<name.size();++i)
				{
					name[i]=native_name[i];
					if (!name[i]) return i!=0;
				}
				return false;
			}
			__except (GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ||
				GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
			{return false;}
		}
		source_snapshot display_source(const weapons::hold& owner,std::uint32_t definition,feed channel,
			int loaded,int reserve,std::uint64_t reference)
		{
			if (!definition || definition>=512) return {};
			std::array<char,64> name{};
			source_snapshot out;
			if (!display_definition(game::weapon_defs[definition],name,out.capacity,out.maximum_reserve,out.low_threshold) ||
				out.capacity<=0 || out.capacity>1024) return {};
			out.clip_type=utils::hook::invoke<int>(0x1406A1BF0,definition,false);
			if (!std::isfinite(out.low_threshold) || out.low_threshold<0 || out.low_threshold>1 || out.maximum_reserve<0 ||
				out.clip_type<0 || out.clip_type>32) return {};
			out.owner=owner;out.definition=definition;out.channel=channel;out.reference=reference;
			out.loaded=loaded;out.reserve=reserve;out.name=name.data();
			return out;
		}
		void sample()
		{
			std::array<source_snapshot,source_count> batch{};
			if (ready && weapons::carry::active() && game::CL_IsCgameInitialized() && game::g_entities[0].client)
			{
				const auto held=weapons::carry::held_instances();
				const auto input=controller_input::latest();
				const auto reference=input.reference_generation;
				const auto now=controller_input::clock::now();
				for (unsigned h=0;h<2;++h)
				{
					const auto& v=held[h];if (!v.id || v.id.weapon>=512) continue;
					const auto ammo=weapons::native_ammunition::observe_carried(game::g_entities[0].client,v.id);
					if (ammo.valid)
					{
						auto& source=batch[source_index(h,feed::primary)];
						source=display_source(v.owner,v.id.weapon,feed::primary,ammo.loaded,ammo.reserve,reference);
						const auto reload=weapons::physical_reload::current(v.id);
						const auto loading=[&](const auto& view) {
							return view.active && !view.fault && view.owner.id()==v.id && view.quick_load.active(v.owner,input,now);
						};
						source.quick_loading=loading(reload);
						if (reload.active && !reload.fault && reload.definition && reload.owner.id()==v.id &&
							weapons::mechanics::native_ammo(reload.ammo).loaded==ammo.loaded)
							source.needs_chamber=needs_chamber(reload.definition->ammunition,reload.ammo,reload.slide_held);
						else if (!reload.active)
						{
							source.quick_loading=loading(weapons::cylinder::current(v.id)) || loading(weapons::break_action::current(v.id));
							const auto tube=weapons::tube::current(v.id);
							if (tube.active && !tube.fault && tube.definition && tube.owner.id()==v.id &&
								weapons::tube::native_ammo(tube.ammo).loaded==ammo.loaded)
								source.needs_chamber=needs_chamber(tube.definition->ammunition,tube.ammo,tube.rack_held || tube.lever.operating);
						}
					}
					// Resolve the existing module authority on the server thread. The
					// child uses its own native clip/reserve keys, never the host's.
					const auto module=weapons::underbarrel::native::resolve(v.id);
					if (!module) continue;
					const auto secondary=weapons::underbarrel::native::observe(module);
					if (secondary.valid)
					{
						auto& source=batch[source_index(h,feed::underbarrel)];
						source=display_source(v.owner,module.id.definition,feed::underbarrel,
							secondary.ammo.loaded,secondary.ammo.reserve,reference);
						const auto reload=weapons::underbarrel::current(v.id);
						if (reload.active && !reload.fault && reload.owner.id()==v.id && reload.ammo.id==module.id &&
							reload.ammo.loaded==secondary.ammo.loaded) source.needs_chamber=needs_chamber(reload.ammo,reload.grip==weapons::underbarrel::lease::action);
					}
				}
			}
			const std::lock_guard lock(mutex);snapshots=std::move(batch);
		}
	}
	source_snapshot source(unsigned index) noexcept
	{
		if(index>=source_count)return {};
		const std::lock_guard lock(mutex);
		// The server publisher explicitly clears absent owners. A wall-clock
		// timeout made low frame rates retire valid Lua trees while the last
		// completed frame still belonged to the same held weapon.
		queried[index]=snapshots[index];
		return queried[index];
	}
	void source_rendered(unsigned index,bool visible) noexcept
	{if(index<source_count){const std::lock_guard lock(mutex);rendered[index]=visible ? queried[index] : source_snapshot{};}}
	std::array<source_snapshot,source_count> source_owners() noexcept {const std::lock_guard lock(mutex);return rendered;}
	class source_component final:public component_interface
	{
		void post_unpack() override
		{
			constexpr std::uint8_t reserve[]{0x66,0x0f,0x6e,0x80,0xe8,0x06,0,0},low[]{0x8b,0x88,0xc0,0x0b,0,0};
			std::array<std::uint8_t,8> mask{};mask.fill(0xff);
			ready=utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140346B5D),{reserve,mask.data(),sizeof(reserve)}) &&
				utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x14034682D),{low,mask.data(),sizeof(low)}) &&
				utils::hook_validation::validate_executable_target(reinterpret_cast<void*>(0x1406A1BF0));
			scheduler::loop(sample,scheduler::pipeline::server);
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapon_hud::source_component)
