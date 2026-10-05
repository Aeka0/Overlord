#pragma once
#include "weapon_carry.hpp"
#include "hand_pose_solver.hpp"
#include "world_pickup_policy.hpp"
#include <optional>
namespace game {struct XModel;}

namespace vr::gameplay::weapons::native_carry
{
	struct snapshot
	{
		bool valid{};
		const void* player{};
		int time{};
		std::uint64_t timeline{};
		std::uint32_t selected{};
		std::array<carry::owned_instance,carry::inventory::capacity> owned{};
		std::size_t count{};
	};
	struct world_key { int entity{-1}; std::uint64_t generation{}; bool operator==(const world_key&) const = default; };
	struct world_item { world_key key{}; std::uint32_t weapon{}; hands::vec position{}; };
	bool initialize();
	snapshot observe() noexcept;
	bool select(std::uint32_t token) noexcept;
	game::XModel* world_model(std::uint32_t token) noexcept;
	bool clearance(std::uint32_t token,const hands::anchor& gun,const hands::vec& eye) noexcept;
	bool drop(const carry::instance&,const hands::anchor& gun,const hands::vec& velocity,world_key& result);
	world_key entity_key(int entity) noexcept;
	world_item pickup_item(world_key key) noexcept;
	bool pickup(world_item,carry::identity& recovered);
	// Read-only admission shared by hand candidate discovery and the exact grip
	// transfer. It never permits a native/automatic touch to consume the item.
	bool duplicate_pickup_admitted(world_key,const void* ps,int automatic,int dual,carry::pickup_context) noexcept;
	// Explicit console give adds a physical copy. Ordinary script giveweapon
	// remains definition-based; nullopt delegates the ordinary command path.
	std::optional<bool> give_duplicate(std::uint32_t token,bool dual);
	bool tracks(carry::identity) noexcept; // Server-only world-instance lifecycle query.
	void reset_world() noexcept;
	const char* status() noexcept;
}
