#pragma once
#include "grenade_state.hpp"
#include "native_ammunition.hpp"
namespace game {struct gentity_s;}

namespace vr::gameplay::grenades::native
{
	struct descriptor {std::uint32_t weapon{};kind type{};int fuse{};float radius{1.5f};};
	struct launch_diagnostics {std::uint64_t attempts{},spawned{},actor_clearances{},world_clearances{};float requested_speed{},native_speed{};int obstruction{-1};};
	launch_diagnostics launch_status()noexcept;
	bool initialize();
	bool describe(std::uint32_t,descriptor&)noexcept;
	int time()noexcept;
	bool available(std::uint32_t)noexcept;
	bool debit(std::uint32_t)noexcept;
	// Preflight before reserving unpaid ammo (football); committed remains true
	// across failed spawn retries, so no duplicated debit is possible.
	bool launch(std::uint32_t,hands::vec position,hands::vec velocity,int fuse_ms,bool& committed)noexcept;
	// Low-level type-2 native projectile route; callers own ammo and placement.
	game::gentity_s* spawn_projectile(std::uint32_t,hands::vec,hands::vec,bool rotate,int fuse_ms)noexcept;
}
