#pragma once

#include <cstdint>
#include <array>
#include "ammunition_transfer.hpp"
#include "weapon_identity.hpp"
#include "weapon_clip_ledger.hpp"

namespace vr::gameplay::weapons::native_ammunition
{
	// No guessed playerState extension and no writable pointers cross this API.
	struct snapshot
	{
		bool valid{};
		std::uint32_t weapon{};
		int loaded{}, reserve{}, weapon_state{}, weapon_time{};
		std::uint64_t instance_generation{};
		weapon_identity id() const noexcept { return {weapon,instance_generation}; }
		bool operator==(const snapshot&) const = default;
	};
	struct reload_snapshot
	{
		bool valid{};
		snapshot ammo{};
		std::array<char, 64> native_name{};
		int animation{}, base_capacity{}; // Action ID without restart bit; unmodified WeaponDef capacity.
		bool bolt_action{};
	};
	// Owned primary feed only; rejects native akimbo and unsupported reload
	// shapes. Segmented feeds require a registered tube descriptor.
	reload_snapshot observe_reload(const void* player_state) noexcept;
	bool initialize();
	bool reload_layout_ready() noexcept;
	snapshot observe(const void* player_state) noexcept;
	// Only for before/after observations around a native primary-ammo call.
	// Engine PMove may run outside scheduler callbacks: read its actual PS cells
	// while retaining projected identity. This grants no ammo-write authority.
	snapshot observe_native_boundary(const void* player_state,std::uint32_t weapon,bool alternate) noexcept;
	int local_role(const void* player_state) noexcept; // 0=server, 1=prediction, -1=unrelated
	// Owned base weapon, including holstered primary feeds. Used to cancel OLD
	// escrow before switching; still validates unique inventory and ammo keys.
	reload_snapshot observe_owned(const void* player_state, std::uint32_t weapon) noexcept;
	// Physical clips are server-owned; native reserves remain shared by ammo type.
	reload_snapshot observe_owned(const void* player_state, weapon_identity) noexcept;
	// Inventory/ammunition access has no detachable-magazine or reload-profile
	// requirement. Still rejects ambiguous keys and native akimbo inventory.
	// Missing clip/reserve cells mean zero. Only a positive compared write may
	// allocate a vacant cell; a full table never replaces another weapon's cell.
	snapshot observe_carried(const void* player_state, std::uint32_t weapon) noexcept;
	snapshot observe_carried(const void* player_state, weapon_identity) noexcept;
	// Validated native reserve-pool identity, without padding bytes. Zero means
	// unavailable. Independent reload items retain this within one timeline.
	std::uint64_t reserve_identity(const void* player_state,weapon_identity)noexcept;
	bool exclusive_loaded_feed(const void* player_state,std::uint32_t weapon) noexcept;
	// Preflight a world pickup before ownership exists; reject native akimbo
	// inventory, duplicate native records and clips aliased by another definition.
	bool exclusive_pickup_feed(const void* player_state,std::uint32_t weapon) noexcept;
	bool commit_carried(const snapshot& expected, int loaded, int reserve) noexcept;
	// Compared writes require the active G_RunFrame server-scheduler scope,
	// not a sticky OS thread ID. Outside callbacks (even on the same thread),
	// prediction/render/console workers cannot write through this API.
	bool commit_owned(const snapshot& expected, int loaded, int reserve) noexcept;
	bool synchronize_instances(const void* ps,std::span<const std::uint32_t> definitions,int time) noexcept;
	clip_ledger instances() noexcept; // Copied snapshot, never a writable native pointer.
	struct restored_clip {std::uint32_t weapon{};int loaded{},reserve{};};
	// Native definitions must already exist. Rebuild independent instances and
	// sparse ammo cells together, with fresh generations and current ownership epochs.
	bool restore_clips(std::span<const restored_clip>,std::span<weapon_identity> restored) noexcept;
	weapon_identity projected_identity(std::uint32_t weapon) noexcept;
	bool project(weapon_identity) noexcept;
	void invalidate_timeline() noexcept;
	std::uint64_t timeline() noexcept;
	void ownership_removed(const void* ps,std::uint32_t weapon) noexcept;
	bool can_admit_definition(const void* ps,std::uint32_t weapon) noexcept;
	// A pickup reserves capacity and isolates native clip writes until native
	// acceptance. No mutex spans the native call; failed admission restores only
	// the surviving projection, never rewinds script effects or shared reserves.
	struct pickup_transfer {weapon_identity id{},previous{};const void* player{};std::uint64_t epoch{},timeline{};int time{};bool active{};};
	pickup_transfer begin_pickup(std::uint32_t weapon,weapon_identity recovered={}) noexcept;
	bool finish_pickup(pickup_transfer&,bool accepted) noexcept;
	bool retire(weapon_identity) noexcept;
	inline bool compare_commit_owned(const void* ps,weapon_identity weapon,ammunition::projection before,ammunition::projection after) noexcept
	{
		const auto observed=observe_owned(ps,weapon);
		return observed.valid && observed.ammo.loaded==before.loaded && observed.ammo.reserve==before.reserve &&
			commit_owned(observed.ammo,after.loaded,after.reserve);
	}
} // namespace vr::gameplay::weapons::native_ammunition
