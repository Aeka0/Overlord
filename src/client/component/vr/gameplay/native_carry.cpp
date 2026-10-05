#include <std_include.hpp>
#include "native_carry.hpp"
#include "underbarrel_runtime.hpp"
#include "native_carry_model.hpp"
#include "drop_presentation.hpp"
#include "weapon_carry_profiles.hpp"
#include "native_scripted_control.hpp"
#include "special_equipment_policy.hpp"
#include "sequences/oilrig.hpp"
#include "cliffhanger_runtime.hpp"
#include "weapon_carry_runtime.hpp"
#include "world_pickup_policy.hpp"
#include "official_cheats.hpp"
#include "akimbo_world_split.hpp"
#include "physical_reload_runtime.hpp"
#include "cylinder_runtime.hpp"
#include "tube_runtime.hpp"
#include "break_action_runtime.hpp"
#include "launcher_runtime.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/game.hpp"
#include "game/scripting/entity.hpp"
#include "game/scripting/execution.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::native_carry
{
	namespace
	{
		constexpr std::size_t entity_limit=4096;
		bool installed{};
		std::array<std::atomic<std::uint64_t>,entity_limit> generations{};
		utils::hook::detour free_hook,pickup_hook,scavenge_hook,take_hook,give_hook,drop_hook,world_model_hook,hide_parts_hook;
		thread_local world_item acquiring{};
		struct split_context
		{
			world_item item;carry::akimbo_split payload;const void* player;std::uint64_t timeline;game::XModel* single_model{};bool retained{};
		};
		thread_local split_context* splitting{};
		thread_local carry::identity releasing{};
		const void* world_player{};int world_time{};
		std::uint64_t world_timeline{};
		std::atomic<const char*> reason{"native carry contracts unavailable"};
		struct record
		{
			world_key key{};carry::identity id{};
			physical_reload::presentation magazine{};
			cylinder::presentation cylinder{};
			tube::presentation tube{};
			break_action::presentation hinged{};
		};
		std::array<record,128> dropped{}; // Never evict a live instance to admit another drop.
		template<class T> T read(const void* p,std::size_t offset)
		{T result{};std::memcpy(&result,static_cast<const std::byte*>(p)+offset,sizeof(result));return result;}
		template<std::size_t N> bool verify(std::uintptr_t address,const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t,N> mask{};mask.fill(0xff);
			return static_cast<bool>(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));
		}
		int number(const game::gentity_s* entity) noexcept
		{
			const auto base=reinterpret_cast<std::uintptr_t>(&game::g_entities[0]);
			const auto p=reinterpret_cast<std::uintptr_t>(entity);
			if (p<base || (p-base)%sizeof(game::gentity_s) || (p-base)/sizeof(game::gentity_s)>=entity_limit) return -1;
			return static_cast<int>((p-base)/sizeof(game::gentity_s));
		}
		bool live(world_key key,std::uint32_t token) noexcept
		{
			if (key.entity<=0 || key.entity>=static_cast<int>(entity_limit) || generations[key.entity].load()!=key.generation) return false;
			const auto* entity=&game::g_entities[key.entity];
			return read<std::uint8_t>(entity,0xbc) && read<std::uint8_t>(entity,0)==2 && read<std::uint32_t>(entity,0x80)==token;
		}
		void free_stub(game::gentity_s* entity)
		{
			const auto index=number(entity);
			if (index>=0) drop_presentation::cancel({index,generations[index].load()});
			if (index>=0) ++generations[index];
			free_hook.invoke<void>(entity);
		}
		bool full_stub(const void* ps)
		{
			// Only the admitted VR pickup bypasses the TWO-primary-weapon replacement
			// branch. G_GivePlayerWeapon inside native pickup still owns the array bound.
			if (acquiring.weapon && ps==game::g_entities[0].client) return false;
			return utils::hook::invoke<bool>(0x1406A6EC0,ps);
		}
		bool owns(const void* ps,std::uint32_t token) noexcept
		{
			if (!ps || !token) return false;
			for (std::size_t i=0;i<15;++i) if (read<std::uint32_t>(ps,0x2f8+i*4)==token) return true;
			return false;
		}
		carry::world_ammo_payload item_ammo(const game::gentity_s* item)noexcept
		{return {read<int>(item,0x190),read<int>(item,0x194),read<int>(item,0x198),read<unsigned>(item,0x1a0)};}
		bool split_world_current(const split_context& s)noexcept
		{return game::CL_IsCgameInitialized() && game::g_entities[0].client==s.player && native_ammunition::timeline()==s.timeline && live(s.item.key,s.item.weapon);}
		void pickup_free_stub(game::gentity_s* item)
		{
			// Only Touch_Item's successful-pickup free call may retain the second
			// gun. Script deletion and all other G_FreeEntity callers remain native.
			if(splitting && !splitting->retained && number(item)==splitting->item.key.entity &&
				split_world_current(*splitting) && splitting->payload.may_restore(item_ammo(item)) && owns(splitting->player,splitting->item.weapon))
			{
				const auto& left=splitting->payload.remainder;
				std::memcpy(reinterpret_cast<std::byte*>(item)+0x190,&left.reserve,4);
				std::memcpy(reinterpret_cast<std::byte*>(item)+0x194,&left.loaded,4);
				std::memcpy(reinterpret_cast<std::byte*>(item)+0x198,&left.second,4);
				const auto flags=read<unsigned>(item,0x1a0)&~1u;
				std::memcpy(reinterpret_cast<std::byte*>(item)+0x1a0,&flags,4);
				// The authored pair's model binding survives the ammo-flag change.
				// Rebind this retained entity through the same SetModel/DObjUpdate/
				// hide-parts path as native single-weapon spawn (0x1404C5BA2..C24).
				utils::hook::invoke<void>(0x140518280,item,splitting->single_model->name);
				utils::hook::invoke<void>(0x1405166B0,item,1);
				if(auto* object=utils::hook::invoke<void*>(0x1405A6ED0,item))
					utils::hook::invoke<void>(0x1406A8FA0,object,splitting->item.weapon,0);
				splitting->retained=true;return;
			}
			utils::hook::invoke<void>(0x140517890,item);
		}
		bool server() noexcept
		{return installed && scheduler::is_executing(scheduler::pipeline::server) && game::CL_IsCgameInitialized() && game::g_entities[0].client;}
		bool eligible(std::uint32_t token) noexcept
		{
			if (!token || (token&~0x1ffu)) return false;
			const auto* def=game::weapon_defs[token];
			if(def && def->szInternalName && (equipment::special::cliffhanger::pick_definition(def->szInternalName) ||
				std::string_view(def->szInternalName)=="h2_cheatpickaxe"))return false;
			if(def && def->szInternalName && special_melee::supported(def->szInternalName))return true;
			// 0 is the native primary inventory class; grenades/mission props remain
			// in their existing authorities. Native akimbo is not a pair of instances.
			return utils::hook::invoke<int>(0x1406A5360,token,false)==0;
		}
		bool duplicate_supported(std::uint32_t token) noexcept
		{
			// The physical firing adapter owns primary bullet feeds. Native akimbo,
			// auxiliary launchers and aliased definition clips require their own feeds.
			return eligible(token) && utils::hook::invoke<int>(0x1406A5440,token,false)==1 &&
				!utils::hook::invoke<int>(0x1406A5610,token) && native_ammunition::exclusive_loaded_feed(game::g_entities[0].client,token);
		}
		bool split_supported(std::uint32_t token)noexcept
		{
			return eligible(token) && utils::hook::invoke<int>(0x1406A5440,token,false)==1 &&
				!utils::hook::invoke<int>(0x1406A5610,token) && native_ammunition::exclusive_pickup_feed(game::g_entities[0].client,token);
		}
		int take_stub(void* ps,std::uint32_t token)
		{
			if (owns(ps,token)) native_ammunition::ownership_removed(ps,token);
			return take_hook.invoke<int>(ps,token);
		}
		game::gentity_s* drop_stub(game::gentity_s* actor,std::uint32_t token)
		{
			if (actor==&game::g_entities[0] && !releasing && native_ammunition::projected_identity(token) &&
				native_ammunition::instances().count(token)>1)
			{reason="native definition drop is ambiguous; release a physical grip";return nullptr;}
			return drop_hook.invoke<game::gentity_s*>(actor,token);
		}
		int give_stub(void* ps,game::Weapon weapon,int dual,int a4,std::int64_t a5,int a6)
		{
			if (carry::active() && ps==game::g_entities[0].client && eligible(weapon.data) &&
				!native_ammunition::can_admit_definition(ps,weapon.data)) return 0;
			return give_hook.invoke<int>(ps,weapon,dual,a4,a5,a6);
		}
		void drop_take_stub(void* ps,std::uint32_t token)
		{
			// Only Drop_Weapon's exact call site is projected. Script takeweapon
			// still removes the definition and invalidates every physical copy.
			if (releasing.weapon==token && ps==game::g_entities[0].client && native_ammunition::instances().count(token)>1) return;
			utils::hook::invoke<void>(0x14051C280,ps,token);
		}
		int pickup_admission_stub(game::gentity_s* item,const void* ps,int automatic,int dual)
		{
			// The native manual gate rejects an already-owned definition with equal
			// akimbo metadata (0x140683678..698), even with an empty projected clip.
			// Retain its script restrictions; override only our admitted world copy.
			if (item && acquiring.weapon==read<std::uint32_t>(item,0x80) && acquiring.key==entity_key(number(item)) &&
				duplicate_pickup_admitted(acquiring.key,ps,automatic,dual,carry::pickup_context::grip_transfer)) return 1;
			return utils::hook::invoke<int>(0x14067EA40,item,ps,automatic,dual);
		}
		bool blocked_touch(game::gentity_s* item,game::gentity_s* actor)
		{
			if (!carry::active() || actor!=&game::g_entities[0] || !item) return false;
			const auto token=read<std::uint32_t>(item,0x80);
			const bool admitted=acquiring.weapon==token && acquiring.key==entity_key(number(item));
			const bool firearm=read<std::uint8_t>(item,0)==2 && (token&511u) &&
				utils::hook::invoke<int>(0x1406A5360,token,false)==0;
			return carry::suppress_weapon_touch(true,true,firearm,admitted);
		}
		void pickup_stub(game::gentity_s* item,game::gentity_s* actor,int automatic)
		{
			if (blocked_touch(item,actor)) return;
			pickup_hook.invoke<void>(item,actor,automatic);
		}
		bool scavenge_stub(game::gentity_s* item,game::gentity_s* actor,int owned,int* event,bool silent)
		{
			// Native scavenging drains BOTH the loose payload and the weapon's
			// loaded clip, then returns permission for the caller to free the item.
			// Protect this boundary too, including calls nested inside another pickup.
			if (blocked_touch(item,actor))
			{
				if (event) *event=0;
				return false;
			}
			return scavenge_hook.invoke<bool>(item,actor,owned,event,silent);
		}
		const char* name(std::uint32_t token) noexcept
		{return *reinterpret_cast<const char* const*>(game::weapon_defs[token&511]);}
		game::XModel* knife_world_model(std::uint32_t token) noexcept
		{
			if(!token || token>=512)return nullptr;
			const auto* def=game::weapon_defs[token];
			if(!def || !def->szInternalName)return nullptr;
			const auto expected=special_melee::receiver(def->szInternalName);
			auto* model=!expected.empty() && def->gunModel ? def->gunModel[0] : nullptr;
			if(!model || !model->name || model->name!=expected || model->numBones!=special_melee::bone_count(expected) ||
				model->numRootBones!=1 || !model->baseMat || !model->boneNames || !model->parentList || model->numCompositeModels)return nullptr;
			for(int i=0;i<model->numBones;++i)
			{
				const auto* bone=game::SL_ConvertToString(model->boneNames[i]);
				const auto expected_bone=i==0 ? special_melee::root(expected) : i==model->numBones-1 ? "tag_knife_fx" : "tag_clip";
				if(!bone || bone!=expected_bone || (i && model->parentList[i-1]!=i))return nullptr;
			}
			return model;
		}
		game::XModel* world_model_stub(std::uint32_t token,bool alternate,int variation)
		{
			// Rebuild the world binding from the engine-owned bare knife mesh.
			// Native drops, physics and pickup DObjs all see the same model/root.
			// Never mutate the shared WeaponDef (ending knives point at a USP).
			if(carry::active() && scripted_control::predicted_allowed())if(auto* knife=knife_world_model(token))return knife;
			return world_model_hook.invoke<game::XModel*>(token,alternate,variation);
		}
		void hide_parts_stub(void* object,std::uint32_t token,int mode)
		{
			hide_parts_hook.invoke<void>(object,token,mode);
			if(!object || !carry::active() || !scripted_control::predicted_allowed())return;
			const auto* source=knife_world_model(token);
			const int hidden=source ? special_melee::hidden_bone(source->name) : -1;
			if(hidden<0)return;
			const auto models=read<game::XModel* const*>(object,0xd8);
			const auto count=read<std::uint8_t>(object,15),bones=read<std::uint8_t>(object,16);
			if(!models || !count || count>32 || !bones || bones>254)return;
			unsigned begin{};
			for(unsigned i=0;i<count;++i)
			{
				if(!models[i] || !models[i]->numBones || models[i]->numBones>bones-begin)return;
				if(models[i]==source)
				{
					// This is the native DObj hide-tag owner, also used by dropped items.
					// Mask the sheath without modifying assets or rebuilding the entity.
					const unsigned bone=begin+unsigned(hidden),offset=0xb8+4*(bone/32);
					const auto mask=read<unsigned>(object,offset)|(0x80000000u>>(bone%32));
					std::memcpy(static_cast<std::byte*>(object)+offset,&mask,sizeof(mask));return;
				}
				begin+=models[i]->numBones;
			}
		}
		bool finite(const hands::vec& v) noexcept
		{return std::all_of(v.begin(),v.end(),[](float x){return std::isfinite(x) && std::abs(x)<1.e7f;});}
		game::XModel* model(std::uint32_t token)
		{
			int variation{};
			utils::hook::invoke<void>(0x1406A8100,game::weapon_defs[token&511],token,&variation);
			return utils::hook::invoke<game::XModel*>(0x1406A5510,token,false,variation);
		}
	}
	bool initialize()
	{
		if (installed) return true;
		constexpr std::uint8_t drop_entry[]{0x48,0x89,0x74,0x24,0x20,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
		constexpr std::uint8_t free_entry[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
		constexpr std::uint8_t pickup_entry[]{0x45,0x85,0xc0,0x74,0x15,0x48,0x8b,0x82,0x18,0x01,0x00,0x00};
		constexpr std::uint8_t scavenge_entry[]{0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x41,0x54,0x48,0x83,0xec,0x60};
		constexpr std::uint8_t full_call[]{0xe8,0x4e,0x06,0x1e,0x00};
		constexpr std::uint8_t select_entry[]{0x89,0x54,0x24,0x10,0x53,0x48,0x83,0xec,0x20};
		constexpr std::uint8_t physics_entry[]{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x88,0x00,0x00,0x00};
		constexpr std::uint8_t trace_entry[]{0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xec,0x78};
		constexpr std::uint8_t origin_entry[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0x02,0x48,0x8b,0xda,0x89,0x41,0x1c};
		constexpr std::uint8_t angles_entry[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0x02,0x48,0x8b,0xda,0x89,0x41,0x40};
		constexpr std::uint8_t model_entry[]{0x48,0x83,0xec,0x28,0x45,0x8b,0xc8,0x41,0xb8,0x70,0x04,0x00,0x00};
		constexpr std::uint8_t hide_entry[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20};
		constexpr std::uint8_t variation_entry[]{0x48,0x8b,0x81,0x70,0x04,0x00,0x00,0xc1,0xea,0x09,0x83,0xe2,0x01};
		constexpr std::uint8_t class_entry[]{0x48,0x83,0xec,0x28,0x41,0xb8,0xe0,0x05,0x00,0x00};
		constexpr std::uint8_t link_entry[]{0x48,0x89,0x5c,0x24,0x20,0x56,0x48,0x83,0xec,0x40};
		constexpr std::uint8_t take_entry[]{0x89,0x54,0x24,0x10,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9};
		constexpr std::uint8_t give_entry[]{0x89,0x54,0x24,0x10,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x48,0x83,0xec,0x30};
		constexpr std::uint8_t take_call[]{0xe8,0xa2,0x8d,0x05,0x00};
		constexpr std::uint8_t existing_give[]{0xb8,0x01,0x00,0x00,0x00};
		constexpr std::uint8_t manual_pickup[]{0xe8,0xc0,0x0f,0x00,0x00};
		constexpr std::uint8_t secondary_entry[]{0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0xc1};
		constexpr std::uint8_t admission_call[]{0xe8,0x61,0x8b,0x1b,0x00};
		constexpr std::uint8_t script_gate[]{0xf7,0x83,0xc0,0x03,0x00,0x00,0x00,0x80,0x00,0x00};
		constexpr std::uint8_t pickup_free_call[]{0xe8,0xa2,0x18,0x05,0x00};
		constexpr std::uint8_t dual_payload_loop[]{0x41,0xf6,0x87,0xa0,0x01,0x00,0x00,0x01,0x74,0x0b};
		constexpr std::uint8_t set_model_entry[]{0x40,0x53,0x48,0x83,0xec,0x20,0x80,0x3a,0x00,0x48,0x8b,0xd9};
		constexpr std::uint8_t dobj_update_call[]{0xe8,0x34,0x01,0x00,0x00};
		constexpr std::uint8_t entity_dobj_entry[]{0x48,0x8d,0x05,0xc9,0x6e,0xd3,0x04,0x48,0x2b,0xc8};
		if (!native_ammunition::initialize() || !verify(0x1404C33F0,drop_entry) || !verify(0x140517890,free_entry) ||
			!verify(0x1404C6010,pickup_entry) || !verify(0x1404C71B0,scavenge_entry) || !verify(0x1404C686D,full_call) || !verify(0x14051C0D0,select_entry) ||
			!verify(0x1404C3AE0,physics_entry) || !verify(0x1404CBFE0,trace_entry) ||
			!verify(0x1405182C0,origin_entry) || !verify(0x140518180,angles_entry) || !verify(0x1406A5510,model_entry) || !verify(0x1406A8FA0,hide_entry) ||
			!verify(0x1406A8100,variation_entry) || !verify(0x1406A5360,class_entry) || !verify(0x1406B6830,link_entry) ||
			!verify(0x1406A8E90,take_entry) || !verify(0x14051B660,give_entry) || !verify(0x1404C34D9,take_call) ||
			!verify(0x14051B78F,existing_give) || !verify(0x1404C5FAB,manual_pickup) || !verify(0x1406A5610,secondary_entry) ||
			!verify(0x1404C5EDA,admission_call) || !verify(0x140683678,script_gate) ||
			!verify(0x1404C5FE9,pickup_free_call) || !verify(0x1404C6697,dual_payload_loop) ||
			!verify(0x140518280,set_model_entry) || !verify(0x1405166D7,dobj_update_call) || !verify(0x1405A6ED0,entity_dobj_entry)) return false;
		free_hook.create(0x140517890,free_stub);
		world_model_hook.create(0x1406A5510,world_model_stub);
		hide_parts_hook.create(0x1406A8FA0,hide_parts_stub);
		pickup_hook.create(0x1404C6010,pickup_stub);
		scavenge_hook.create(0x1404C71B0,scavenge_stub);
		utils::hook::call(0x1404C686D,full_stub);
		take_hook.create(0x1406A8E90,take_stub);
		drop_hook.create(0x1404C33F0,drop_stub);
		give_hook.create(0x14051B660,give_stub);
		utils::hook::call(0x1404C34D9,drop_take_stub);
		utils::hook::call(0x1404C5EDA,pickup_admission_stub);
		utils::hook::call(0x1404C5FE9,pickup_free_stub);
		scripting::on_shutdown([](bool,bool after){if (!after) native_ammunition::invalidate_timeline();});
		scripting::on_level_start([]{native_ammunition::invalidate_timeline();});
		installed=true;reason="native carry contracts ready";return true;
	}
	snapshot observe() noexcept
	{
		snapshot out;
		if (!server()) return out;
		const auto* player=game::g_entities[0].client;const auto time=game::CG_GetGameTime(0);
		out.timeline=native_ammunition::timeline();
		if (world_player!=player || time<world_time || world_timeline!=out.timeline) reset_world();
		world_player=player;world_time=time;world_timeline=out.timeline;
		for (const auto& saved:dropped) if (saved.id && drop_presentation::pending(saved.key) && live(saved.key,saved.id.weapon))
		{
			const auto* entity=&game::g_entities[saved.key.entity];
			hands::anchor world;world.position=read<hands::vec>(entity,0xdc);
			const auto angles=read<hands::vec>(entity,0xe8);
			utils::hook::invoke<void>(0x140613590,angles.data(),world.rotation.data());
			if (finite(world.position)) drop_presentation::update(saved.key,world);
		}
		out.player=game::g_entities[0].client;out.time=game::CG_GetGameTime(0);
		out.selected=*reinterpret_cast<const std::uint32_t*>(0x141E8A628);
		std::array<std::uint32_t,15> definitions{};
		std::array<carry::rules,15> policies{};std::size_t count{};
		const auto action_slots=equipment::special::weapon_slots({static_cast<const std::byte*>(out.player),0x1fc0});
		const auto mission_equipment=sequences::oilrig::current_equipment();
		for (std::size_t i=0;i<15;++i)
		{
			const auto token=read<std::uint32_t>(out.player,0x2f8+i*4);
			if (!eligible(token)) continue;
			if (read<std::uint8_t>(out.player,0x334+i*8+1)) {reason="native akimbo inventory requires separate handling";return out;}
			const auto* n=name(token);const auto size=strnlen_s(n,128);
			if (!size || size>=128) return out;
			if(cheats::body_weapon({n,size}))continue;
			if(!sequences::oilrig::permits_equipment(mission_equipment,{n,size}))continue;
			definitions[count]=token;auto policy=carry::profile_for({n,size});
			for(const auto slot:action_slots)if(slot && slot.weapon==token){policy.abdominal=true;policy.waist=false;}
			policies[count++]=policy;
		}
		if (!native_ammunition::synchronize_instances(out.player,{definitions.data(),count},out.time))
		{reason="physical clip ledger reconciliation rejected";return out;}
		const auto instances=native_ammunition::instances();
		for (const auto& e:instances.entries()) if (e.id)
			for (std::size_t i=0;i<count;++i) if (definitions[i]==e.id.weapon) {out.owned[out.count++]={e.id,policies[i]};break;}
		out.valid=true;return out;
	}
	bool select(std::uint32_t token) noexcept
	{
		if (!server() || (token && !owns(game::g_entities[0].client,token))) return false;
		if(equipment::special::cliffhanger::preserve_native_selection(token))
		{reason="scripted C4 detonator owns selection";return false;}
		if (!token) {game::G_SelectWeapon(0,game::Weapon{});return true;}
		// Grip transfers on the same gun are not new equipment requests. Do not
		// restart its native action or interrupt its mechanical state.
		if (*reinterpret_cast<const std::uint32_t*>(0x141E8A628)==token &&
			(read<std::uint32_t>(game::g_entities[0].client,0x3bc)&511)==token) return true;
		const auto* native_name=name(token);const auto length=native_name ? strnlen_s(native_name,128) : 0;
		if (!length || length>=128) return false;
		try
		{
			// The selected native method owns weapon-state resets,
			// selection and notifications. Keep its isolated VM call on the server;
			// do not reproduce its private timer/flag writes in the carry arbiter.
			const scripting::entity actor(game::scr_entref_t{0,0});
			const auto result=actor.call("switchtoweaponimmediate",{std::string(native_name,length)});
			const bool accepted=result.is<int>() && result.as<int>()!=0;
			reason=accepted ? "native carry selection requested" : "native carry selection rejected";
			return accepted;
		}
		catch (const std::exception&) {reason="native carry selection failed";return false;}
	}
	game::XModel* world_model(std::uint32_t token) noexcept
	{
		if(!server() || !eligible(token))return nullptr;
		const auto* def=game::weapon_defs[token];
		if(def && def->szInternalName && special_melee::supported(def->szInternalName))return knife_world_model(token);
		if(def && def->szInternalName && std::string_view(def->szInternalName)=="usp_laserdesignator" && def->gunModel)return def->gunModel[0];
		return model(token);
	}
	bool clearance(std::uint32_t token,const hands::anchor& gun,const hands::vec& eye) noexcept
	{
		if (!server() || !eligible(token) || !finite(gun.position) || !finite(eye) || hands::length(hands::sub(gun.position,eye))>200) return false;
		float norm{};for (auto x:gun.rotation) {if (!std::isfinite(x)) return false;norm+=x*x;}
		if (std::abs(norm-1)>0.01f) return false;
		const auto world=geometry(token);if (!world.valid) {reason="native composite geometry unavailable";return false;}
		game::Bounds bounds{};
		const auto center=hands::scale(hands::add(world.low,world.high),.5f),extent=hands::scale(hands::sub(world.high,world.low),.5f);
		const auto offset=hands::rotate(gun.rotation,center);
		for (int i=0;i<3;++i)
		{
			if (!std::isfinite(extent[i]) || extent[i]<=0 || extent[i]>100 || !std::isfinite(center[i])) return false;
			bounds.midPoint[i]=offset[i];
			for (int j=0;j<3;++j) {hands::vec axis{};axis[j]=1;bounds.halfSize[i]+=std::abs(hands::rotate(gun.rotation,axis)[i])*extent[j];}
		}
		game::trace_t trace{};game::Bounds point{};
		game::G_TraceCapsule(&trace,eye.data(),gun.position.data(),&point,0,0x280e831);
		if (!std::isfinite(trace.fraction) || trace.fraction<1 || trace.startsolid || trace.allsolid) {reason="release point occluded from player";return false;}
		game::G_TraceCapsule(&trace,gun.position.data(),gun.position.data(),&bounds,0,0x280e831);
		const bool clear=std::isfinite(trace.fraction) && trace.fraction>=1 && !trace.startsolid && !trace.allsolid;
		reason=clear ? "native composite volume clear" : "weapon volume intersects world";return clear;
	}
	bool drop(const carry::instance& value,const hands::anchor& gun,const hands::vec& velocity,world_key& result)
	{
		if(cheats::transitioning()){reason="official cheat inventory transfer in progress";return false;}
		if(sequences::for_player(game::g_entities[0].client).block_carry){reason="story blocks player weapon drops";return false;}
		if(value.policy.abdominal){reason="mission equipment cannot be dropped into the world";return false;}
		if (!server() || !owns(game::g_entities[0].client,value.id.weapon) || !finite(gun.position) || !finite(velocity)) return false;
		if (!value.id || releasing || acquiring.weapon)
		{reason="stale or ambiguous carry drop identity";return false;}
		record* storage{};
		for (auto& r:dropped) if (!r.id || !live(r.key,r.id.weapon)) {storage=&r;break;}
		if (!storage) {reason="world instance ledger full; release retained";return false;}
		record saved;saved.id=value.id;
		if (!launcher::prepare_transfer(value.id) || !underbarrel::prepare_transfer(value.id) || !physical_reload::prepare_transfer(value.id,saved.magazine) || !cylinder::prepare_transfer(value.id,saved.cylinder) || !tube::prepare_transfer(value.id,saved.tube) || !break_action::prepare_transfer(value.id,saved.hinged))
		{reason="mechanical transfer preparation rejected";return false;}
		const auto before=native_ammunition::observe_carried(game::g_entities[0].client,value.id);
		const auto ledger=native_ammunition::instances();const auto* identity=ledger.find(value.id);
		const bool ammunitionless=identity && !identity->key;
		if (!before.valid && !ammunitionless) {reason="native ammunition identity rejected";return false;}
		const auto previous=native_ammunition::projected_identity(value.id.weapon);
		if (!native_ammunition::project(value.id)) return false;
		const bool duplicate=native_ammunition::instances().count(value.id.weapon)>1;
		const auto* transfer_player=game::g_entities[0].client;
		const auto transfer_timeline=native_ammunition::timeline();
		const auto same_world=[&]{return game::CL_IsCgameInitialized() && game::g_entities[0].client==transfer_player &&
			native_ammunition::timeline()==transfer_timeline;};
		releasing=value.id;
		const auto restore=gsl::finally([&]{releasing={};if (previous) native_ammunition::project(previous);});
		// Same spawn/ammunition/removal path as PlayerCmd_DropItem. Place before
		// physics creation, so no actor ever spawns at the old flat-screen muzzle.
		auto* entity=utils::hook::invoke<game::gentity_s*>(0x1404C33F0,&game::g_entities[0],value.id.weapon);
		if (!same_world()) {reason="world changed during native drop";return false;}
		const auto index=number(entity);
		if (index<=0) {reason="native drop did not return an entity";return false;}
		if (duplicate)
		{
			// No shared reserve leaves the player while another physical copy owns
			// this definition. Native payload copying itself does not debit it.
			const int zero=0;
			std::memcpy(reinterpret_cast<std::byte*>(entity)+0x190,&zero,sizeof(zero));
		}
		const auto forward=hands::rotate(gun.rotation,{1,0,0}),left=hands::rotate(gun.rotation,{0,1,0}),up=hands::rotate(gun.rotation,{0,0,1});
		constexpr float degrees=57.2957795131f;
		const hands::vec angles{std::atan2(-forward[2],std::hypot(forward[0],forward[1]))*degrees,
			std::atan2(forward[1],forward[0])*degrees,std::atan2(left[2],up[2])*degrees};
		utils::hook::invoke<void>(0x1405182C0,entity,gun.position.data());
		utils::hook::invoke<void>(0x140518180,entity,angles.data());
		const std::array<hands::vec,3> axis{forward,left,up};
		utils::hook::invoke<void>(0x1404C3AE0,entity,gun.position.data(),axis.data(),velocity.data());
		if (!read<void*>(entity,0x140) && read<int>(entity,0x10)==7)
			std::memcpy(reinterpret_cast<std::byte*>(entity)+0x28,velocity.data(),sizeof(velocity));
		utils::hook::invoke<void>(0x1406B6830,entity);
		if (!same_world()) {reason="world changed during drop placement";return false;}
		saved.key={index,generations[index].load()};*storage=saved;result=saved.key;
		drop_presentation::begin(saved.key,geometry(value.id.weapon),gun);
		const bool removed=(duplicate || !owns(game::g_entities[0].client,value.id.weapon)) && native_ammunition::retire(value.id);
		if (duplicate && !removed)
		{
			utils::hook::invoke<void>(0x140517890,entity);*storage={};
			reason="duplicate drop retirement rejected; world spawn cancelled";return false;
		}
		const bool preserved=ammunitionless || read<int>(entity,0x194)==before.loaded;
		reason=removed && preserved ? "native world drop committed" : "native drop projection mismatch";
		// Once native ownership transferred, never pretend that this gun is still
		// held. Keep the entity and saved mechanics even if a postcondition failed.
		return removed && live(saved.key,value.id.weapon);
	}
	bool pickup(world_item item,carry::identity& recovered)
	{
		if (!server() || cheats::transitioning() || !live(item.key,item.weapon) || acquiring.weapon || releasing) return false;
		if(const auto* n=name(item.weapon);n && cheats::body_weapon(n)){reason="cheat melee belongs to body equipment";return false;}
		const auto payload=item_ammo(&game::g_entities[item.key.entity]);
		const auto split=carry::split_akimbo(payload);
		if((payload.flags&1) && (!split || !split_supported(item.weapon))) {reason="akimbo world payload or feed unsupported";return false;}
		auto* single_model=split ? model(item.weapon) : nullptr;
		if(split && (!single_model || !single_model->name || !*single_model->name)) {reason="akimbo single world model unavailable";return false;}
		const bool duplicate=owns(game::g_entities[0].client,item.weapon);
		if (duplicate && !duplicate_supported(item.weapon)) {reason="duplicate weapon feed unsupported";return false;}
		unsigned count{};for (std::size_t i=0;i<15;++i) if (read<std::uint32_t>(game::g_entities[0].client,0x2f8+i*4)) ++count;
		if (!duplicate && count>=15) {reason="native owned array full";return false;}
		record saved;
		for (const auto& r:dropped) if (r.id.weapon==item.weapon && r.key==item.key) {saved=r;break;}
		auto transfer=native_ammunition::begin_pickup(item.weapon,saved.id);
		if (!transfer.active) {reason="physical instance capacity or pickup identity rejected";return false;}
		const auto cancel=gsl::finally([&]{if (transfer.active) native_ammunition::finish_pickup(transfer,false);});
		const auto world_loaded=read<int>(&game::g_entities[item.key.entity],0x194);
		const auto previous=acquiring;
		const auto clear=gsl::finally([&]{acquiring=previous;});acquiring=item;
		split_context split_state{item,split.value_or(carry::akimbo_split{}),transfer.player,transfer.timeline,single_model};
		const auto previous_split=splitting;
		const auto restore_split=gsl::finally([&] {
			if(split && !split_state.retained && split_world_current(split_state) && split->may_restore(item_ammo(&game::g_entities[item.key.entity])))
				std::memcpy(reinterpret_cast<std::byte*>(&game::g_entities[item.key.entity])+0x1a0,&split->original.flags,4);
			splitting=previous_split;
		});
		if(split)
		{
			// Native grant, clip import and pickup notifications see one weapon.
			// The second clip is untouched until the exact successful free boundary.
			std::memcpy(reinterpret_cast<std::byte*>(&game::g_entities[item.key.entity])+0x1a0,&split->pickup.flags,4);
			splitting=&split_state;
		}
		utils::hook::invoke<void>(0x1404C6010,&game::g_entities[item.key.entity],&game::g_entities[0],0);
		const bool accepted=(!live(item.key,item.weapon) || split_state.retained) && owns(game::g_entities[0].client,item.weapon);
		if (!native_ammunition::finish_pickup(transfer,accepted)) {reason="native pickup rejected or script ownership changed";return false;}
		recovered=transfer.id;
		if (!native_ammunition::project(recovered)) {reason="picked-up clip projection rejected";return true;}
		if (saved.id)
		{
			if (saved.magazine.active && !saved.magazine.fault && saved.magazine.definition)
			{
				const auto observed=native_ammunition::observe_owned(game::g_entities[0].client,item.weapon);
				const auto& rules=saved.magazine.definition->ammunition;
				if (observed.valid && mechanics::valid(rules,saved.magazine.ammo) &&
					saved.magazine.definition->matches_native(observed.native_name.data(),observed.base_capacity))
				{
					const auto restored=ammunition::restore_pickup({observed.ammo.loaded,observed.ammo.reserve},
						mechanics::native_ammo(saved.magazine.ammo).loaded,world_loaded,observed.base_capacity,rules.magazine_capacity+int(rules.plus_one));
					if (restored && restored->loaded!=observed.ammo.loaded)
						native_ammunition::commit_owned(observed.ammo,restored->loaded,restored->reserve);
				}
			}
			if(saved.tube.active && !saved.tube.fault && saved.tube.definition)
			{
				const auto observed=native_ammunition::observe_owned(game::g_entities[0].client,saved.id.weapon);
				const auto& rules=saved.tube.definition->ammunition;
				if(observed.valid && tube::valid(rules,saved.tube.ammo) && saved.tube.definition->matches_native(observed.native_name.data(),observed.base_capacity))
				{
					const auto restored=ammunition::restore_pickup({observed.ammo.loaded,observed.ammo.reserve},tube::native_ammo(saved.tube.ammo).loaded,
						world_loaded,observed.base_capacity,tube::loaded_capacity(rules));
					if(restored && restored->loaded!=observed.ammo.loaded)native_ammunition::commit_owned(observed.ammo,restored->loaded,restored->reserve);
				}
			}
			// Check all families even if one restore fails; no failure may
			// suppress the other's cleanup/publication after native ownership moved.
			if(saved.hinged.active && !saved.hinged.fault && saved.hinged.definition)
			{
				const auto observed=native_ammunition::observe_owned(game::g_entities[0].client,saved.id.weapon);
				const auto& rules=saved.hinged.definition->ammunition;
				if(observed.valid && break_action::valid(rules,saved.hinged.ammo) && saved.hinged.definition->matches_native(observed.native_name.data(),observed.base_capacity))
				{
					const auto restored=ammunition::restore_pickup({observed.ammo.loaded,observed.ammo.reserve},break_action::native_ammo(saved.hinged.ammo).loaded,
						world_loaded,observed.base_capacity,int(rules.capacity));
					if(restored && restored->loaded!=observed.ammo.loaded)native_ammunition::commit_owned(observed.ammo,restored->loaded,restored->reserve);
				}
			}
			const bool magazine_restored=physical_reload::restore_transfer(saved.magazine);
			const bool cylinder_restored=cylinder::restore_transfer(saved.cylinder);
			const bool tube_restored=tube::restore_transfer(saved.tube);
			const bool hinge_restored=break_action::restore_transfer(saved.hinged);
			const bool restored=magazine_restored && cylinder_restored && tube_restored && hinge_restored;
			recovered=saved.id;
			reason=restored ? (saved.magazine.active || saved.cylinder.active || saved.tube.active || saved.hinged.active ? "native pickup restored mechanical instance" : "native pickup recovered carry identity") : "native pickup mechanics restoration rejected";
		}
		else reason=split_state.retained ? "akimbo split: one physical gun picked up, single gun remains in world" : "native world pickup committed";
		if(!cheats::settle_pickup(item.weapon))reason="Green Beret pickup reserve settlement rejected";
		return true;
	}
	bool duplicate_pickup_admitted(world_key key,const void* ps,int automatic,int dual,carry::pickup_context context) noexcept
	{
		if (!server() || !carry::active() || !ps || ps!=game::g_entities[0].client) return false;
		const auto item=pickup_item(key);
		if(item.weapon && carry::admit_akimbo_split(context,(read<unsigned>(&game::g_entities[key.entity],0x1a0)&1)!=0,automatic,dual,read<std::uint32_t>(ps,0x3c0)))return true;
		return carry::admit_duplicate_pickup(context,item.weapon!=0,
			item.weapon && owns(ps,item.weapon),automatic,dual,read<std::uint32_t>(ps,0x3c0));
	}
	std::optional<bool> give_duplicate(std::uint32_t token,bool dual)
	{
		if (!carry::active() || !eligible(token) || !owns(game::g_entities[0].client,token) || dual) return std::nullopt;
		if (!server() || acquiring.weapon || releasing || !duplicate_supported(token))
		{reason="duplicate give feed or transfer unavailable";return false;}
		// Refresh native grants/removals that may have occurred earlier in this
		// server dispatch, before reserving the bounded physical slot.
		if (!observe().valid) {reason="duplicate give inventory reconciliation rejected";return false;}
		auto transfer=native_ammunition::begin_pickup(token);
		if (!transfer.active) {reason="duplicate give physical capacity or ammunition storage full";return false;}
		const auto cancel=gsl::finally([&]{if (transfer.active) native_ammunition::finish_pickup(transfer,false);});
		game::Weapon weapon{};weapon.data=token;
		// G_GivePlayerWeapon returns success without inserting an already-owned
		// definition. Keep BOTH that call and ammo initialization inside the clip
		// transaction, so give never refills the original physical gun's clip.
		const bool granted=game::G_GivePlayerWeapon(game::g_entities[0].client,weapon,0,0,0,0)!=0;
		const bool same_world=game::CL_IsCgameInitialized() && game::g_entities[0].client==transfer.player &&
			native_ammunition::timeline()==transfer.timeline;
		if (granted && same_world) game::G_InitializeAmmo(game::g_entities[0].client,weapon,0);
		const bool committed=native_ammunition::finish_pickup(transfer,granted && same_world);
		reason=committed ? "console give added physical weapon instance" : "duplicate give native transaction rejected";
		return committed;
	}
	world_key entity_key(int entity) noexcept
	{return entity>0 && entity<static_cast<int>(entity_limit) ? world_key{entity,generations[entity].load()} : world_key{};}
	bool tracks(carry::identity id) noexcept
	{
		if (!server() || !id) return false;
		for (const auto& r:dropped) if (r.id==id && live(r.key,id.weapon)) return true;
		return false;
	}
	world_item pickup_item(world_key key) noexcept
	{
		if (!server() || key.entity<=0 || key.entity>=static_cast<int>(entity_limit)) return {};
		const auto* entity=&game::g_entities[key.entity];
		const auto token=read<std::uint32_t>(entity,0x80);
		if (!live(key,token) || !eligible(token) || (owns(game::g_entities[0].client,token) && !duplicate_supported(token))) return {};
		if((read<unsigned>(entity,0x1a0)&1) && (!split_supported(token) || !carry::split_akimbo(item_ammo(entity))))return {};
		const auto position=read<hands::vec>(entity,0xdc);
		return finite(position) ? world_item{key,token,position} : world_item{};
	}
	void reset_world() noexcept {drop_presentation::clear();dropped={};reset_model_cache();}
	const char* status() noexcept {return reason.load();}
}
