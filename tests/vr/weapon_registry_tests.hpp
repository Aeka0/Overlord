#pragma once
#include "component/vr/gameplay/weapon_registry.hpp"
#include "component/vr/gameplay/weapons/aa12/profile.hpp"
#include "component/vr/gameplay/weapons/acr/profile.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/aug/profile.hpp"
#include "component/vr/gameplay/weapons/cheytac/profile.hpp"
#include "component/vr/gameplay/weapons/de50/profile.hpp"
#include "component/vr/gameplay/weapons/dragunov/profile.hpp"
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/famas/profile.hpp"
#include "component/vr/gameplay/weapons/fn2000/profile.hpp"
#include "component/vr/gameplay/weapons/g18/profile.hpp"
#include "component/vr/gameplay/weapons/l86/profile.hpp"
#include "component/vr/gameplay/weapons/m1014/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
#include "component/vr/gameplay/weapons/m16/profile.hpp"
#include "component/vr/gameplay/weapons/m1911/profile.hpp"
#include "component/vr/gameplay/weapons/m240/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/m82/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/m93r/profile.hpp"
#include "component/vr/gameplay/weapons/mg4/profile.hpp"
#include "component/vr/gameplay/weapons/miniuzi/profile.hpp"
#include "component/vr/gameplay/weapons/model1887/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/p90/profile.hpp"
#include "component/vr/gameplay/weapons/pp2000/profile.hpp"
#include "component/vr/gameplay/weapons/rpd/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapons/spas12/profile.hpp"
#include "component/vr/gameplay/weapons/striker/profile.hpp"
#include "component/vr/gameplay/weapons/tavor/profile.hpp"
#include "component/vr/gameplay/weapons/tmp/profile.hpp"
#include "component/vr/gameplay/weapons/ump/profile.hpp"
#include "component/vr/gameplay/weapons/usp/profile.hpp"
#include "component/vr/gameplay/weapons/vector/profile.hpp"
#include "component/vr/gameplay/weapons/wa2000/profile.hpp"
#include "component/vr/gameplay/weapons/winchester1200/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/tube_profiles.hpp"
#include <iostream>

namespace weapon_registry_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		int failures{};
		const auto check = [&](bool value, const char* message) {
			if (!value) { std::cerr << "FAIL: registry: " << message << '\n'; ++failures; }
		};

		// Regression snapshot: preserve every supported recipe and its default
		// precedence when multiple skins admit the same native instance name.
		const std::array<const reload_profile*, 60> expected_reload{
			&m9::physical, &m1911::physical, &de50::physical, &de50::gold_physical, &usp::physical,
			&usp::silenced_physical, &m4::physical, &m4::arctic_physical, &ak47::physical, &ak47::arctic,
			&ak47::digital, &ak47::desert, &ak47::woodland, &g18::physical, &m93r::physical,
			&tmp::physical, &miniuzi::physical, &acr::physical, &acr::black,
			&acr::digital, &acr::arctic, &vector::physical, &vector::black,
			&mp5::physical, &mp5::arctic, &aug::physical, &aug::plain, &ump::physical,
			&ump::arctic, &ump::digital, &fal::physical, &pp2000::physical,
			&l86::physical, &famas::physical, &famas::tape, &famas::woodland, &m16::physical,
			&m14ebr::physical, &m14ebr::arctic, &m82::physical, &wa2000::physical,
			&cheytac::physical, &cheytac::desert, &scar::physical, &tavor::physical, &tavor::digital, &tavor::woodland,
			&fn2000::physical, &aa12::physical, &dragunov::physical, &dragunov::arctic_physical, &dragunov::woodland_physical, &p90::physical,
			&p90::arctic, &m240::physical, &m240::arctic_physical, &mg4::physical, &mg4::arctic_physical, &rpd::physical, &rpd::digital_physical};
		check(reload_profiles.size() == expected_reload.size(), "detachable feed coverage preserved");
		for (size_t i = 0; i < expected_reload.size(); ++i)
		{
			const auto* p = expected_reload[i];
			check(i < reload_profiles.size() && reload_profiles[i] == p, "detachable recipe order preserved");
			check(reload_profile_index(p) == i, "shared recipes retain one cache index");
			const auto capacity = p->ammunition.magazine_capacity;
			check(native_reload_profile(p->native_name, capacity, p) == p, "registered scene recipe admitted");
			check(!native_reload_profile(p->native_name, capacity + 1, p), "wrong capacity rejected");
			const auto copy = *p;
			check(!native_reload_profile(p->native_name, capacity, &copy), "unregistered recipe copy rejected");
			const reload_profile* default_recipe{};
			for (const auto* candidate : expected_reload)
				if (candidate->matches_native(p->native_name, capacity)) { default_recipe = candidate; break; }
			check(native_reload_profile(p->native_name, capacity) == default_recipe, "native-only default preserved");
		}
		check(reload_profile_index(nullptr) == reload_profiles.size(), "null recipe has no cache index");
		check(!native_reload_profile("unknown_weapon", 30), "unknown native name rejected");

		const std::array<const tube_profile*, 8> expected_tube{
			&m1014::feed, &m1014::arctic_feed, &spas12::feed, &spas12::arctic_feed, &winchester1200::feed, &model1887::feed, &striker::feed, &striker::woodland_feed};
		check(tube_definitions.size() == expected_tube.size(), "individual-shell feed coverage preserved");
		for (size_t i = 0; i < expected_tube.size(); ++i)
		{
			const auto* p = expected_tube[i];
			check(i < tube_definitions.size() && tube_definitions[i] == p, "individual-shell recipe order preserved");
			for (const auto name : p->native_variants)
			{
				check(native_tube_profile(name, p->ammunition.capacity, p) == p, "individual-shell scene admitted");
				check(!native_tube_profile(name, p->ammunition.capacity + 1, p), "individual-shell capacity checked");
				const auto copy = *p;
				check(!native_tube_profile(name, p->ammunition.capacity, &copy), "unregistered tube recipe rejected");
				const tube_profile* default_recipe{};
				for (const auto* candidate : expected_tube)
					if (candidate->matches_native(name, p->ammunition.capacity)) { default_recipe = candidate; break; }
				check(native_tube_profile(name, p->ammunition.capacity) == default_recipe, "native-only tube default preserved");
			}
		}
		check(!native_tube_profile("unknown_weapon", 7), "unknown individual-shell feed rejected");

		for (size_t i = 0; i < registered_profiles.size(); ++i)
		{
			const auto& entry = registered_profiles[i];
			check(entry.value && !entry.value->receiver.empty(), "registration has a receiver");
			if (!entry.value) continue;
			const auto& p = *entry.value;
			check(int(p.reload != nullptr) + int(p.tube != nullptr) + int(p.cylinder != nullptr) + int(p.break_open != nullptr) <= 1,
				"an assembly has at most one primary feed");
			for (size_t j = 0; j < i; ++j)
			{
				const auto& other = registered_profiles[j];
				check(other.value != entry.value, "assembly registered once");
				if (other.value->receiver == p.receiver)
					check(entry.select && other.select == entry.select,
						"shared receiver has one explicit variant selector");
			}
		}

		// Adding only assembly registrations makes their capabilities available.
		// Duplicate references share slots, null capabilities do not create slots,
		// and an independently authored recipe remains distinct despite equal data.
		auto first = m9::base;
		auto shared = first;
		auto independent_recipe = *first.reload;
		auto independent = first; independent.reload = &independent_recipe;
		auto no_feed = first; no_feed.reload = nullptr;
		const auto synthetic = register_profiles(nullptr, no_feed, first, shared, independent);
		const profile_capabilities feeds{synthetic, &profile::reload};
		check(feeds.size() == 2 && feeds[0] == first.reload && feeds[1] == &independent_recipe,
			"capabilities follow registered assemblies and recipe identity");
		const profile_capabilities empty{register_profiles(nullptr, no_feed), &profile::reload};
		check(empty.size() == 0 && empty.begin() == empty.end(), "unsupported feed exposes an empty range");
		const profile_capabilities<reload_profile,1> overflow{synthetic,&profile::reload};
		check(overflow.size()==0,"insufficient capability storage rejects the whole view instead of admitting a partial catalog");
		const std::array<profile_registration,1> invalid{{{nullptr,nullptr}}};
		const profile_capabilities rejected{invalid,&profile::reload};
		check(rejected.size()==0,"invalid registrations fail closed before dereferencing a profile");
		return failures;
	}
}
