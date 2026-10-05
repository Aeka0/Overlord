#pragma once
#include "component/vr/h2/entrypoints.hpp"
#include <array>
#include <type_traits>

namespace native_binding_tests
{
	struct call_record
	{
		const void* player{};
		std::uint32_t weapon{};
		bool alternate{};
		unsigned calls{};
	};
	inline call_record observed;
	inline int capacity_query(const void* player, std::uint32_t weapon, bool alternate)
	{
		observed = {player, weapon, alternate, observed.calls + 1};
		return alternate ? 7 : 30;
	}
	inline vr::h2::dobj* entity_query(const game::gentity_s* entity)
	{
		return reinterpret_cast<vr::h2::dobj*>(const_cast<game::gentity_s*>(entity));
	}
	inline void rotation_query(const float* angles, float* output)
	{
		for (unsigned i = 0; i < 3; ++i)
			output[i] = angles[i];
		output[3] = 1;
	}
	template <class Check> void run(Check check)
	{
		using query = vr::h2::function<int(const void*, std::uint32_t, bool)>;
		static_assert(!std::is_invocable_v<query, const void*, std::uint32_t>);
		static_assert(!std::is_invocable_v<query, const void*, const char*, bool>);
		static_assert(!std::is_invocable_v<decltype(vr::h2::sp::server_entity_dobj), int, int>);
		static_assert(!std::is_invocable_v<decltype(vr::h2::sp::client_entity_dobj), const game::gentity_s*>);
		static_assert(
		    std::is_same_v<decltype(vr::h2::sp::weapon_selection_request.get()), const std::uint32_t*>);
		unsigned player{};
		observed = {};
		const query capacity{reinterpret_cast<std::uintptr_t>(&capacity_query)};
		check(
		    capacity(&player, 0x1234, false) == 30 && observed.player == &player &&
		        observed.weapon == 0x1234 && !observed.alternate && observed.calls == 1,
		    "canonical native query passes the original player pointer, token and alternate flag exactly once");
		check(capacity(&player, 0x4321, true) == 7 && observed.weapon == 0x4321 && observed.alternate &&
		          observed.calls == 2,
		      "alternate query is live rather than a cached WeaponDef capacity");
		const vr::h2::function<vr::h2::dobj*(const game::gentity_s*)> entity{
		    reinterpret_cast<std::uintptr_t>(&entity_query)};
		const auto* address = reinterpret_cast<const game::gentity_s*>(&player);
		check(entity(address) == reinterpret_cast<vr::h2::dobj*>(&player),
		      "opaque DObj lookup preserves identity without exposing an unvalidated layout");
		const vr::h2::function<void(const float*, float*)> rotation{
		    reinterpret_cast<std::uintptr_t>(&rotation_query)};
		const std::array<float, 3> angles{10, 20, 30};
		std::array<float, 4> quaternion{};
		rotation(angles.data(), quaternion.data());
		check(quaternion == std::array<float, 4>{10, 20, 30, 1},
		      "native rotation binding writes the caller's original output buffer");
		std::uint32_t request = 17;
		const vr::h2::read_only_global<std::uint32_t> requested{reinterpret_cast<std::uintptr_t>(&request)};
		check(requested.read() == 17 && requested.get() == &request,
		      "native request is a read-only typed view");
		request = 29;
		check(requested.read() == 29, "native selection request is reread at use time rather than cached");
	}
}
