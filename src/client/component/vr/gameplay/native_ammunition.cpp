#include <std_include.hpp>
#include "native_ammunition.hpp"
#include "weapon_carry_runtime.hpp"
#include "native_ammunition_storage.hpp"
#include "weapon_clip_projection.hpp"
#include "native_reload_layout.hpp"
#include "weapon_native_traits.hpp"
#include "heartbeat_runtime.hpp"
#include "special_melee.hpp"
#include "component/scheduler_context.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::native_ammunition
{
	namespace
	{
		static_assert(offsetof(game::WeaponDef,clipSize)==reload_layout::capacity_offset);
		static_assert(offsetof(game::WeaponDef,reloadAmmoAdd)==reload_layout::add_offset);
		static_assert(offsetof(game::WeaponDef,noPartialReload)==reload_layout::no_partial_offset);
		static_assert(offsetof(game::WeaponDef,segmentedReload)==reload_layout::segmented_offset);
		bool verified{};
		constexpr std::uintptr_t clip_key_address = 0x14069D250;
		constexpr std::uintptr_t ammo_key_address = 0x14069D0B0;
		constexpr std::uintptr_t predicted_ps = 0x141BB3C30;
		constexpr std::uintptr_t weapon_definitions = 0x14CE01580;
		bool carried_verified{}, reload_verified{};
		bool has_ammunition(std::uint32_t weapon) noexcept
		{
			if(!weapon || weapon>=512 || !game::weapon_defs[weapon])return true;
			const auto* name=game::weapon_defs[weapon]->szInternalName;
			return special_melee::uses_ammunition(utils::hook::invoke<int>(0x1406A5440,weapon,false),name ? name : "");
		}
		std::mutex instance_mutex;
		clip_ledger clips;
		std::array<std::atomic_uint64_t,512> ownership_epochs{};
		std::atomic_uint64_t timeline_epoch{};
		std::uint64_t instance_timeline{};
		const void* instance_player{};int instance_time{};
		std::uint32_t transferring{};
		bool live(const clip_ledger::entry* e) noexcept
		{return e && instance_timeline==timeline_epoch.load() && e->id.weapon<ownership_epochs.size() && e->epoch==ownership_epochs[e->id.weapon].load();}
		template <typename T> T read(const void* source, size_t offset) noexcept
		{
			T value{};
			std::memcpy(&value, static_cast<const std::byte*>(source) + offset, sizeof(value));
			return value;
		}
		template <size_t N> bool verify(std::uintptr_t address, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t, N> mask{};
			mask.fill(0xff);
			return static_cast<bool>(utils::hook_validation::verify_masked_bytes(
				reinterpret_cast<void*>(address), {bytes, mask.data(), N}));
		}
		bool unique_inventory_record(const void* ps,std::uint32_t token) noexcept
		{
			if (!token || (token & ~0x1ffu)) return false;
			int matches{};
			for (std::size_t i=0;i<15;++i)
				if (read<std::uint32_t>(ps,0x2f8+i*4)==token)
				{
					if (read<std::uint8_t>(ps,0x334+i*8+1)) return false;
					++matches;
				}
			return matches==1;
		}
		// Destructor-free bounded read leaf. No engine calls under SEH; an invalid
		// optional asset prevents admission rather than inventing a weapon name.
		bool reload_details(const void* ps, reload_snapshot& out,reload_layout::shape& copied_shape) noexcept
		{
			__try
			{
				const auto token = out.ammo.weapon;
				if (!unique_inventory_record(ps,token)) return false;
				const auto definition = read<const void*>(reinterpret_cast<const void*>(weapon_definitions), token*8);
				if (reinterpret_cast<std::uintptr_t>(definition) < 0x10000) return false;
				const auto shape=reload_layout::decode({static_cast<const std::byte*>(definition),reload_layout::extent});
				if (!shape) return false;
				const auto name = read<const char*>(definition, 0);
				if (reinterpret_cast<std::uintptr_t>(name) < 0x10000) return false;
				out.animation = read<int>(ps, 0x2b0) & ~0x800;
				out.base_capacity = shape->capacity;copied_shape=*shape;
				if (!game::weapon_defs[token]) return false;
				out.bolt_action=game::weapon_defs[token]->boltAction;
				for (size_t i = 0; i < out.native_name.size(); ++i)
				{
					out.native_name[i] = name[i];
					if(!name[i])return i!=0;
				}
				return false; // missing terminator: never pass to string_view
			}
			__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
				GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
			{ return false; }
		}
		bool counts(const void* ps, std::uint32_t token, snapshot& out)
		{
			const auto clip_key = utils::hook::invoke<std::uint64_t>(clip_key_address, token, false);
			const auto ammo_key = utils::hook::invoke<std::uint64_t>(ammo_key_address, token, false);
			const auto cells=storage::observe({static_cast<const std::byte*>(ps),storage::extent},clip_key,ammo_key);
			out.loaded=cells.clip.count; out.reserve=cells.reserve.count;
			out.weapon = token;
			out.weapon_state = read<int>(ps, 0x2c0);
			out.weapon_time = read<int>(ps, 0x2b4);
			return out.valid = cells.valid();
		}
	} // namespace
	bool initialize()
	{
		// Separate key types: clip identity compares uint32 + byte; reserve also
		// compares byte 5. Padding bytes are NOT part of the identity.
		constexpr std::uint8_t clip[]{0x89, 0x4c, 0x24, 0x08, 0x53, 0x48, 0x83, 0xec, 0x20};
		constexpr std::uint8_t ammo[]{0x48, 0x89, 0x5c, 0x24, 0x10, 0x89, 0x4c,
									  0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20};
		constexpr std::uint8_t clip_array[]{0x48, 0x8d, 0x8b, 0xa4, 0x04, 0x00, 0x00};
		constexpr std::uint8_t reserve_array[]{0x48, 0x8d, 0x8f, 0xf0, 0x03, 0x00, 0x00};
		constexpr std::uint8_t prediction[]{0x48, 0x8d, 0x0d, 0x22, 0x3b, 0x85, 0x01};
		verified = verify(clip_key_address, clip) && verify(ammo_key_address, ammo) &&
				   verify(0x140691BBB, clip_array) && verify(0x140696555, reserve_array) &&
				   verify(0x140360107, prediction);
		constexpr std::uint8_t table[]{0x4c,0x8d,0x35,0x29,0x0d,0x76,0x0c};
		constexpr std::uint8_t dual[]{0x0f,0xb6,0x41,0x01};
		constexpr std::uint8_t no_partial[]{0x41,0xb8,0x95,0x0e,0x00,0x00};
		constexpr std::uint8_t segmented[]{0x41,0xb8,0x96,0x0e,0x00,0x00};
		constexpr std::uint8_t add[]{0x41,0xb8,0x34,0x0b,0x00,0x00};
		carried_verified = verified && verify(0x1406A6DC6, dual);
		reload_verified = carried_verified && verify(0x1406A0850, table) &&
			verify(0x1406A6BE4, no_partial) && verify(0x1406A7E94, segmented) && verify(0x1406A76F4, add);
		return verified;
	}
	int local_role(const void* ps) noexcept
	{
		if (!ps)
			return -1;
		if (ps == game::g_entities[0].client)
			return 0;
		return reinterpret_cast<std::uintptr_t>(ps) == predicted_ps ? 1 : -1;
	}
	bool reload_layout_ready() noexcept { return reload_verified; }
	reload_snapshot observe_reload(const void* ps) noexcept
	{
		reload_snapshot out;
		if (!reload_verified) return out;
		out.ammo = observe(ps);
		if(out.ammo.valid)
		{
			reload_layout::shape shape;
			out.valid=reload_details(ps,out,shape) && admits_native_reload(out.native_name.data(),shape.capacity,shape.no_partial,shape.segmented,shape.add);
		}
		return out;
	}
	reload_snapshot observe_owned(const void* ps, std::uint32_t weapon) noexcept
	{
		reload_snapshot out;
		if (!reload_verified || local_role(ps) < 0 || !weapon || (weapon & ~0x1ffu)) return out;
		out.ammo.weapon = weapon;
		reload_layout::shape shape;
		if(!reload_details(ps,out,shape) || !admits_native_reload(out.native_name.data(),shape.capacity,shape.no_partial,shape.segmented,shape.add))return out;
		out.valid = counts(ps, weapon, out.ammo);
		return out;
	}
	bool commit_owned(const snapshot& expected, int loaded, int reserve) noexcept
	{
		if (!reload_verified || !scheduler::is_executing(scheduler::pipeline::server) ||
			!game::CL_IsCgameInitialized() || !observe_owned(game::g_entities[0].client,expected.weapon).valid) return false;
		return commit_carried(expected,loaded,reserve);
	}
	reload_snapshot observe_owned(const void* ps,weapon_identity id) noexcept
	{
		auto out=observe_owned(ps,id.weapon);
		if (out.valid) {out.ammo=observe_carried(ps,id);out.valid=out.ammo.valid;}
		return out;
	}
	snapshot observe_carried(const void* ps,weapon_identity id) noexcept
	{
		auto out=observe_carried(ps,id.weapon);
		if (!out.valid) return out;
		if (!id.generation) return carry::active() ? snapshot{} : out;
		const std::lock_guard lock(instance_mutex);
		const auto* e=clips.find(id);
		if (!live(e) || instance_player!=game::g_entities[0].client || transferring==id.weapon) return {};
		// Native code can update the currently projected clip between server ticks.
		// Nonprojected copies never inherit that write (including prediction reads).
		if (!e->projected || local_role(ps)!=0 || !scheduler::is_executing(scheduler::pipeline::server)) out.loaded=e->loaded;
		out.instance_generation=id.generation;
		return out;
	}
	snapshot observe_carried(const void* ps,std::uint32_t weapon) noexcept
	{
		snapshot result;
		if (!carried_verified || local_role(ps)<0 || !unique_inventory_record(ps,weapon) || !has_ammunition(weapon)) return result;
		(void)counts(ps,weapon,result);
		return result;
	}
	snapshot observe_native_boundary(const void* ps,std::uint32_t weapon,bool alternate)noexcept
	{
		snapshot out;
		if(!carried_verified || alternate || local_role(ps)<0 || !unique_inventory_record(ps,weapon) || !has_ammunition(weapon))return out;
		const auto ck=utils::hook::invoke<std::uint64_t>(clip_key_address,weapon,false);
		const auto rk=utils::hook::invoke<std::uint64_t>(ammo_key_address,weapon,false);
		const bool physical=carry::active();
		const std::lock_guard lock(instance_mutex);
		const auto id=physical ? clips.projected(weapon) : weapon_identity{weapon,0};
		if(physical && (!live(clips.find(id)) || instance_player!=game::g_entities[0].client || transferring==weapon))return out;
		const auto value=projection::read_native_boundary(clips,{static_cast<const std::byte*>(ps),storage::extent},id,ck,rk);
		if(!value)return out;
		out.valid=true;out.weapon=weapon;out.instance_generation=id.generation;out.loaded=value->loaded;out.reserve=value->reserve;
		out.weapon_state=read<int>(ps,0x2c0);out.weapon_time=read<int>(ps,0x2b4);return out;
	}
	bool commit_carried(const snapshot& expected, int loaded, int reserve) noexcept
	{
		if (!carried_verified || !expected.valid || !scheduler::is_executing(scheduler::pipeline::server) ||
			loaded < 0 || loaded > 1001 || reserve < 0 || reserve > 1000000 || !game::CL_IsCgameInitialized()) return false;
		auto* ps = game::g_entities[0].client;
		const auto owned = observe_carried(ps, expected.weapon);
		if (!owned.valid || owned.reserve != expected.reserve) return false;
		const auto clip_key=utils::hook::invoke<std::uint64_t>(clip_key_address,expected.weapon,false);
		const auto ammo_key=utils::hook::invoke<std::uint64_t>(ammo_key_address,expected.weapon,false);
		const std::lock_guard lock(instance_mutex);
		if (transferring==expected.weapon) return false;
		const auto* e=expected.instance_generation ? clips.find(expected.id()) : nullptr;
		if (expected.instance_generation && (!live(e) || instance_player!=ps)) return false;
		// Owning server thread, bounded cells in the current live gclient, no
		// callbacks/engine reentry between compare and the two stores. Never cache
		// a writable address across a tick or try to write predicted PS here.
		return projection::commit(clips,{reinterpret_cast<std::byte*>(ps),storage::extent},expected.id(),clip_key,ammo_key,
			expected.loaded,expected.reserve,loaded,reserve);
	}
	bool synchronize_instances(const void* ps,std::span<const std::uint32_t> definitions,int time) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server) || local_role(ps)!=0 || definitions.size()>clip_ledger::capacity) return false;
		std::array<clip_ledger::definition,clip_ledger::capacity> observed{};
		for (std::size_t i=0;i<definitions.size();++i)
		{
			const auto token=definitions[i];
			if(!has_ammunition(token))
			{
				if(!unique_inventory_record(ps,token))return false;
				observed[i]={token,0,0,ownership_epochs[token].load()};continue;
			}
			const auto native=observe_carried(ps,token);if (!native.valid) return false;
			const auto key=utils::hook::invoke<std::uint64_t>(clip_key_address,token,false)&0xffffffffffull;
			if(!key)return false; // A malformed firearm key is not an ammunitionless weapon.
			observed[i]={token,native.loaded,key,ownership_epochs[token].load()};
		}
		const std::lock_guard lock(instance_mutex);if (transferring) return false;
		if (instance_player!=ps || time<instance_time || instance_timeline!=timeline_epoch.load()) clips.clear();
		instance_player=ps;instance_time=time;instance_timeline=timeline_epoch.load();
		return clips.reconcile({observed.data(),definitions.size()});
	}
	clip_ledger instances() noexcept {const std::lock_guard lock(instance_mutex);return clips;}
	weapon_identity projected_identity(std::uint32_t weapon) noexcept
	{
		const std::lock_guard lock(instance_mutex);const auto id=clips.projected(weapon);
		return transferring!=weapon && instance_player==game::g_entities[0].client && live(clips.find(id)) ? id : weapon_identity{};
	}
	bool project(weapon_identity id) noexcept
	{
		if (!id || !scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized()) return false;
		auto* ps=game::g_entities[0].client;
		if(!has_ammunition(id.weapon))
		{
			if(!unique_inventory_record(ps,id.weapon))return false;
			const std::lock_guard lock(instance_mutex);const auto* e=clips.find(id);
			return instance_player==ps && !transferring && live(e) && !e->key && clips.select(id);
		}
		const auto native=observe_carried(ps,id.weapon);if (!native.valid) return false;
		const auto ck=utils::hook::invoke<std::uint64_t>(clip_key_address,id.weapon,false);
		const auto rk=utils::hook::invoke<std::uint64_t>(ammo_key_address,id.weapon,false);
		const std::lock_guard lock(instance_mutex);
		const auto* next=clips.find(id);if (instance_player!=ps || transferring || !live(next)) return false;
		const auto previous=clips.projected(id.weapon);const auto* old=clips.find(previous);if (!live(old)) return false;
		return projection::select(clips,{reinterpret_cast<std::byte*>(ps),storage::extent},id,ck,rk);
	}
	void ownership_removed(const void* ps,std::uint32_t weapon) noexcept
	{if (local_role(ps)==0 && weapon && weapon<ownership_epochs.size()) ++ownership_epochs[weapon];}
	bool restore_clips(std::span<const restored_clip> saved,std::span<weapon_identity> restored) noexcept
	{
		if(!scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized() ||
			saved.size()>clip_ledger::capacity || restored.size()!=saved.size())return false;
		auto* ps=game::g_entities[0].client;if(!ps)return false;
        std::array<projection::restored_clip,clip_ledger::capacity> entries{};
        for(std::size_t i=0;i<saved.size();++i)
        {
            const auto& s=saved[i];if(!unique_inventory_record(ps,s.weapon))return false;
            auto& entry=entries[i];entry={s.weapon,s.loaded,s.reserve,0,0,ownership_epochs[s.weapon].load()};
            if(has_ammunition(s.weapon))
            {
                entry.clip_key=utils::hook::invoke<std::uint64_t>(clip_key_address,s.weapon,false)&0xffffffffffull;
                entry.reserve_key=utils::hook::invoke<std::uint64_t>(ammo_key_address,s.weapon,false)&0xffffffffffffull;
                if(!entry.clip_key || !entry.reserve_key)return false;
            }
        }
        const std::lock_guard lock(instance_mutex);
        if(instance_player!=ps || instance_timeline!=timeline_epoch.load() || transferring)return false;
        return projection::restore(clips,{reinterpret_cast<std::byte*>(ps),storage::extent},{entries.data(),saved.size()},restored);
    }
	void invalidate_timeline() noexcept {++timeline_epoch;}
	std::uint64_t timeline() noexcept {return timeline_epoch.load();}
	std::uint64_t reserve_identity(const void* ps,weapon_identity id)noexcept
	{
		if(!observe_carried(ps,id).valid)return 0;
		return utils::hook::invoke<std::uint64_t>(ammo_key_address,id.weapon,false)&0xffffffffffffull;
	}
	bool can_admit_definition(const void* ps,std::uint32_t weapon) noexcept
	{
		if (local_role(ps)!=0) return true;
		const auto key=has_ammunition(weapon) ? utils::hook::invoke<std::uint64_t>(clip_key_address,weapon,false)&0xffffffffffull : 0;
		const std::lock_guard lock(instance_mutex);
		for (const auto& e:clips.entries()) if (key && e.id && live(&e) && e.id.weapon!=weapon && e.key==key && clips.count(e.id.weapon)>1) return false;
		std::size_t count=transferring && transferring!=weapon && !clips.count(transferring) ? 1 : 0;
		for (const auto& e:clips.entries()) if (live(&e) && e.id) ++count;
		for (unsigned i=0;i<15;++i)
		{
			const auto token=read<std::uint32_t>(ps,0x2f8+i*4);if (!token) continue;
			if (token==weapon) return true;
			if (!live(clips.find(clips.projected(token)))) ++count;
		}
		return count<clip_ledger::capacity;
	}
	pickup_transfer begin_pickup(std::uint32_t weapon,weapon_identity recovered) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized() || !weapon || weapon>=512) return {};
		auto* ps=game::g_entities[0].client;
		if(!has_ammunition(weapon))
		{
			const std::lock_guard lock(instance_mutex);
			if(instance_player!=ps || instance_timeline!=timeline_epoch.load() || transferring || clips.count(weapon) ||
				clips.count()>=clip_ledger::capacity || (recovered && (recovered.weapon!=weapon || clips.find(recovered))))return {};
			const auto id=recovered ? recovered : clips.allocate(weapon);if(!id)return {};
			transferring=weapon;return {id,{},ps,ownership_epochs[weapon].load(),instance_timeline,instance_time,true};
		}
		const auto native=observe_carried(ps,weapon);
		const auto ck=utils::hook::invoke<std::uint64_t>(clip_key_address,weapon,false);
		const auto rk=utils::hook::invoke<std::uint64_t>(ammo_key_address,weapon,false);
		const std::lock_guard lock(instance_mutex);
		if (instance_player!=ps || instance_timeline!=timeline_epoch.load() || transferring || clips.count()>=clip_ledger::capacity || (recovered && (recovered.weapon!=weapon || clips.find(recovered)))) return {};
		const auto previous=clips.projected(weapon);const auto* old=clips.find(previous);
		if (old && (!live(old) || !native.valid)) return {};
		const auto id=recovered ? recovered : clips.allocate(weapon);if (!id) return {};
		if (!projection::begin_pickup(clips,{reinterpret_cast<std::byte*>(ps),storage::extent},weapon,ck,rk)) return {};
		transferring=weapon;return {id,previous,ps,ownership_epochs[weapon].load(),instance_timeline,instance_time,true};
	}
	bool finish_pickup(pickup_transfer& transfer,bool accepted) noexcept
	{
		if (!transfer.active || !scheduler::is_executing(scheduler::pipeline::server)) return false;
		const auto token=transfer.id.weapon;auto* ps=game::g_entities[0].client;
		if (!game::CL_IsCgameInitialized() || ps!=transfer.player || transfer.timeline!=timeline_epoch.load())
		{const std::lock_guard lock(instance_mutex);transfer.active=false;transferring=0;return false;}
		const auto time=game::CG_GetGameTime(0);
		if(!has_ammunition(token))
		{
			const bool owned=accepted && unique_inventory_record(ps,token);
			const std::lock_guard lock(instance_mutex);transfer.active=false;transferring=0;
			return owned && ps==transfer.player && transfer.timeline==timeline_epoch.load() && time>=transfer.time &&
				ownership_epochs[token].load()==transfer.epoch && clips.add(transfer.id,0,0,transfer.epoch);
		}
		const auto native=ps==transfer.player ? observe_carried(ps,token) : snapshot{};
		const auto ck=utils::hook::invoke<std::uint64_t>(clip_key_address,token,false);
		const auto rk=utils::hook::invoke<std::uint64_t>(ammo_key_address,token,false);
		const std::lock_guard lock(instance_mutex);transfer.active=false;transferring=0;
		if (ps!=transfer.player || transfer.timeline!=timeline_epoch.load() || time<transfer.time || ownership_epochs[token].load()!=transfer.epoch || !native.valid) return false;
		return projection::finish_pickup(clips,{reinterpret_cast<std::byte*>(ps),storage::extent},transfer.id,transfer.previous,ck,rk,transfer.epoch,accepted);
	}
	bool retire(weapon_identity id) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server)) return false;
		auto* ps=game::g_entities[0].client;
		if(!has_ammunition(id.weapon))
		{
			const std::lock_guard lock(instance_mutex);
			return instance_player==ps && instance_timeline==timeline_epoch.load() && !transferring && clips.erase(id);
		}
		const auto native=observe_carried(ps,id.weapon);
		const auto ck=utils::hook::invoke<std::uint64_t>(clip_key_address,id.weapon,false);
		const auto rk=utils::hook::invoke<std::uint64_t>(ammo_key_address,id.weapon,false);
		const std::lock_guard lock(instance_mutex);
		if (!clips.find(id) || instance_player!=ps || instance_timeline!=timeline_epoch.load() || transferring) return false;
		if (clips.count(id.weapon)>1 && !native.valid) return false;
		return projection::retire(clips,{reinterpret_cast<std::byte*>(ps),storage::extent},id,ck,rk);
	}
	bool exclusive_loaded_feed(const void* ps,std::uint32_t weapon) noexcept
	{
		return carried_verified && local_role(ps)>=0 && unique_inventory_record(ps,weapon) && exclusive_pickup_feed(ps,weapon);
	}
	bool exclusive_pickup_feed(const void* ps,std::uint32_t weapon) noexcept
	{
		if (!carried_verified || local_role(ps)<0 || !weapon || (weapon&~0x1ffu)) return false;
		if(!has_ammunition(weapon))return false;
		const auto key=utils::hook::invoke<std::uint64_t>(clip_key_address,weapon,false)&0xffffffffffull;
		unsigned matches{};
		for (unsigned i=0;i<15;++i)
		{
			const auto other=read<std::uint32_t>(ps,0x2f8+i*4);if (!other) continue;
			if(!has_ammunition(other))continue;
			if(other==weapon){if(++matches>1 || read<std::uint8_t>(ps,0x335+i*8))return false;continue;}
			if ((utils::hook::invoke<std::uint64_t>(clip_key_address,other,false)&0xffffffffffull)==key) return false;
		}
		return true;
	}
	snapshot observe(const void* ps) noexcept
	{
		snapshot out;
		if (!verified || local_role(ps) < 0)
			return out;
		out.weapon = read<std::uint32_t>(ps, 0x3bc);
		if (!(out.weapon & 0x1ff) || ((read<std::uint32_t>(ps, 0x3c0) & 0x4000) &&
			!(heartbeat::enabled() && heartbeat::equivalent_mode(out.weapon))))
			return out;
		if (carry::active())
		{
			const auto id=projected_identity(out.weapon);if (!id) return {};
			return observe_carried(ps,id);
		}
		if(has_ammunition(out.weapon))(void)counts(ps, out.weapon, out);
		return out;
	}
} // namespace vr::gameplay::weapons::native_ammunition
