#pragma once
#include "component/vr/gameplay/interaction_schedule.hpp"

namespace interaction_schedule_tests
{
	inline void callback()
	{
	}
	struct binding
	{
		vr::gameplay::interaction_schedule::provider_id id;
		void (*report)(){};
		void (*collect)(){};
		void (*settle)(){};
		void (*lifecycle)(){};
	};
	template <class Check> void run(Check check)
	{
		namespace schedule = vr::gameplay::interaction_schedule;
		using id = schedule::provider_id;
		std::array<binding, schedule::provider_count> bindings{};
		for (std::size_t i = 0; i < bindings.size(); ++i)
			bindings[i] = {.id = static_cast<id>(i), .report = callback, .collect = callback};
		for (auto provider : schedule::settlements)
			bindings[std::size_t(provider)].settle = callback;
		for (auto provider : schedule::continuous_lifecycle)
			bindings[std::size_t(provider)].lifecycle = callback;
		for (auto provider : schedule::idle_weapon_lifecycle)
			bindings[std::size_t(provider)].lifecycle = callback;
		check(schedule::valid_bindings(bindings),
		      "complete provider capabilities bind without numeric ranks");
		const auto valid = bindings;
		bindings[std::size_t(id::world)].collect = nullptr;
		check(!schedule::valid_bindings(bindings), "missing collection cannot silently omit a domain");
		bindings = valid;
		bindings[std::size_t(id::world)].settle = callback;
		check(!schedule::valid_bindings(bindings),
		      "an undeclared settlement callback cannot be silently ignored");
		bindings = valid;
		bindings[std::size_t(id::nightvision)].lifecycle = nullptr;
		check(!schedule::valid_bindings(bindings),
		      "continuous cleanup remains mandatory without fresh weapon input");
		bindings = valid;
		bindings[std::size_t(id::underbarrel)].id = id::equipment;
		check(!schedule::valid_bindings(bindings),
		      "binding index cannot dispatch another provider's callbacks");
		check(!schedule::valid_phase(std::array{id::world, id::world}),
		      "duplicate phase entries cannot replay a domain");
		check(!schedule::valid_phase(std::array{id::count}),
		      "invalid dispatch keys fail before indexing a binding");
	}
}
