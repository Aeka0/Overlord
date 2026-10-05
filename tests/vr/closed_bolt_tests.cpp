#include "component/vr/gameplay/native_closed_bolt_policy.hpp"
#include "component/vr/gameplay/weapons/de50/profile.hpp"
#include "component/vr/gameplay/weapons/g18/profile.hpp"
#include "component/vr/gameplay/weapons/m1911/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/m93r/profile.hpp"
#include "component/vr/gameplay/weapons/tmp/profile.hpp"
#include "component/vr/gameplay/weapons/usp/profile.hpp"
#include "component/vr/gameplay/weapon_mechanics_profiles.hpp"
#include <iostream>
#include <limits>

int main()
{
	using namespace vr::gameplay::weapons;
	using namespace closed_bolt;
	using native_closed_bolt::boundary;
	using native_closed_bolt::reload_kind;
	using native_closed_bolt::capacity;
	int failures{};
	const auto check = [&](bool value, const char* label) {
		if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
	};
	check(native_chamber_profile("beretta", 15) == &m9::chamber_rules, "M9 name/capacity opt-in");
	check(native_chamber_profile("tmp_reflex",32)==&tmp::chamber_rules &&
		!native_chamber_profile("tmp_reflex",33) && !native_chamber_profile("tmp_reflex_extra",32),
		"captured TMP reflex uses the same bounded native chamber identity as physical reload");
	check(native_chamber_profile("beretta393",20)==&m93r::chamber_rules &&
		native_chamber_profile("tmp",32)==&tmp::chamber_rules,"M93R/TMP explicit native candidates");
	check(!native_chamber_profile("m93r",20) && !native_chamber_profile("beretta393",15) &&
		!native_chamber_profile("beretta393_akimbo",20) && !native_chamber_profile("mp9",15) &&
		!native_chamber_profile("tmp_akimbo",32) && !native_chamber_profile("tmp",15) && !native_chamber_profile("tmp",33),
		"foregrip pistol aliases and altered capacities cannot opt in");
	check(native_chamber_profile("glock",32)==&g18::chamber_rules && !native_chamber_profile("g18",32) &&
		!native_chamber_profile("glock_akimbo",32) && !native_chamber_profile("glock",33),"G18 exact native name and observed base capacity opt-in");
	check(native_chamber_profile("usp",12) == &usp::chamber_rules &&
		native_chamber_profile("usp_silencer",12) == &usp::chamber_rules &&
		!native_chamber_profile("usp_silencer",15) && !native_chamber_profile("usp_silenced",12) &&
		!native_chamber_profile("usp",15) && !native_chamber_profile("usp_akimbo",12) &&
		!native_chamber_profile("usp_tactical",12),"USP candidate exact identity and capacity only");
	check(native_chamber_profile("colt45",7) == &m1911::chamber_rules &&
		native_chamber_profile("deserteagle",7) == &de50::chamber_rules,"authored single-wield pistol profiles");
	check(!native_chamber_profile("de50",7) && !native_chamber_profile("deserteagle_akimbo",7) &&
		!native_chamber_profile("colt45",15),"animation aliases, dual wield and wrong capacities are not native identities");
	check(!native_chamber_profile("m9",15) && !native_chamber_profile("beretta_akimbo",15) &&
		!native_chamber_profile("beretta",30) && !native_chamber_profile("ak47",30), "unknown identities/variants pass through");
	for (int size : {1, 7, 12, 15, 20, 30, 33, 1000})
		for (bool lock : {false, true})
			for (bool extra : {false, true})
			{
				const rules r{size, lock, extra};
				state s{};
				check(from_native_automatic(r, size, s) && s.chamber_loaded && s.magazine_rounds == size-1,
					"initial total partitions without free round");
				int shots{};
				while (fire(r,s)) ++shots;
				check(shots == size && s.action == (lock ? action_state::locked_open : action_state::closed),
					"one native round per shot; profile-specific follower lock");
				const auto empty = s;
				check(!fire(r,s) && s == empty, "empty fire unchanged");
				check(!ready(r,s), "empty is not ready");
				s = {false,true,0,action_state::closed};
				check(fire(r,s) && s.action == action_state::closed && !s.chamber_loaded,
					"detached magazine permits chamber-only shot without follower lock");
				s = {true,false,size,action_state::closed};
				check(valid(r,s) && !ready(r,s), "inserted loaded magazine does not imply chambered");
				int extracted = -1;
				check(cycle(r,s,extracted) && extracted == 0 && ready(r,s) && s.magazine_rounds == size-1,
					"full cycle feeds one from inserted magazine");
				const auto count = s.magazine_rounds+int(s.chamber_loaded);
				check(cycle(r,s,extracted) && extracted == 1 &&
					s.magazine_rounds+int(s.chamber_loaded)+extracted == count, "cycle returns live extraction to inventory policy");
				for (int loaded = 0; loaded <= size+int(extra); ++loaded)
					for (int reserve : {0,1,3,1500,std::numeric_limits<int>::max()-1001})
						for (auto kind : {reload_kind::tactical,reload_kind::empty,reload_kind::unknown})
						{
							const auto target = capacity(r,size,loaded,boundary::refill,kind);
							const auto moved = std::min(target-loaded,reserve);
							const auto after = loaded+moved, stock = reserve-moved;
							check(moved >= 0 && target <= size+int(extra) && stock >= 0 &&
								std::int64_t(loaded)+reserve == std::int64_t(after)+stock, "bounded conserved native transfer");
							if (kind != reload_kind::tactical && loaded <= size)
								check(after <= size, "empty/unknown reload never gains retained chamber");
							if (kind == reload_kind::empty)
								check(capacity(r,size,after,boundary::refill,kind) == target, "empty refill repetition retains original limit");
							// Server and each prediction replay use independent native inputs;
							// no shared spent flag, mutable global magazine or command cache.
							check(capacity(r,size,loaded,boundary::refill,kind) == target, "same replay input deterministic");
						}
				check(capacity(r,size,0,boundary::admission,reload_kind::unknown) == size, "empty admission base capacity");
				check(capacity(r,size,size,boundary::admission,reload_kind::unknown) == size+int(extra), "top off base loaded total");
				const state sentinel{false,true,0,action_state::closed};
				s = sentinel;
				check(!from_native_automatic(r,size+2,s) && s == sentinel, "invalid adoption atomic");
				check(!from_native_automatic(r,-1,s) && s == sentinel, "negative total rejected");
				check(capacity(r,size+4,1,boundary::admission,reload_kind::unknown) == size+4, "effective capacity modifier passes through");
			}
	const auto r = m9::chamber_rules;
	for (bool inserted : {false, true})
		for (int rounds : {0, 1, 15})
		{
			if (!inserted && rounds) continue;
			state locked{inserted, false, rounds, action_state::locked_open};
			const auto original = locked;
			const bool allowed = !inserted || rounds > 0;
			check(release(r, locked) == allowed, "lock release requires loaded magazine or absent follower");
			if (allowed)
				check(locked.action == action_state::closed && locked.chamber_loaded == (rounds > 0) &&
					locked.magazine_rounds + int(locked.chamber_loaded) == rounds,
					"release closes without inventing ammunition");
			else check(locked == original, "empty follower keeps lock and counts unchanged");
			int live = -1;
			check(extract(r, locked, live) && live == int(allowed && rounds > 0) &&
				locked.action == action_state::held_open && !ready(r, locked), "full extraction blocks fire until release");
			const auto held = locked;
			check(!extract(r, locked, live) && !cycle(r, locked, live) && !release(r, locked) && locked == held,
				"held action cannot extract twice or shortcut explicit release");
			check(finish_stroke(r, locked) && !finish_stroke(r, locked), "stroke finishes exactly once");
			check(locked.action == (inserted && rounds <= 1 ? action_state::locked_open : action_state::closed),
				"empty follower relocks; no-magazine cycle closes");
		}
	check(capacity(r,15,15,boundary::admission,reload_kind::unknown) == 16, "15 can be topped off");
	check(capacity(r,15,14,boundary::refill,reload_kind::tactical) == 16, "tactical M9 gets 15+1");
	check(capacity(r,15,0,boundary::refill,reload_kind::empty) == 15, "empty M9 gets 14+1");
	check(capacity(r,15,15,boundary::refill,reload_kind::empty) == 15, "repeat empty refill cannot become tactical");
	check(capacity(r,15,16,boundary::refill,reload_kind::unknown) == 16, "late callback cannot unfill +1");
	// Native refill arithmetic on independent server/prediction snapshots.
	const auto refill = [&](int& loaded, int& reserve, reload_kind kind) {
		const int limit = capacity(r,15,loaded,boundary::refill,kind);
		const int moved = std::min(limit-loaded,reserve);
		loaded += moved; reserve -= moved;
	};
	int server_loaded = 14, server_reserve = 60;
	refill(server_loaded,server_reserve,reload_kind::tactical);
	for (int replay = 0; replay < 4; ++replay)
	{
		int predicted_loaded = 14, predicted_reserve = 60;
		refill(predicted_loaded,predicted_reserve,reload_kind::tactical);
		check(predicted_loaded == server_loaded && predicted_reserve == server_reserve,
			"prediction replay agrees without spending authoritative reserve again");
	}
	check(server_loaded == 16 && server_reserve == 58, "tactical consumes two actual reserve rounds");
	refill(server_loaded,server_reserve,reload_kind::tactical);
	check(server_loaded == 16 && server_reserve == 58, "duplicate completed tactical transfer no-op");
	server_loaded = 0;
	refill(server_loaded,server_reserve,reload_kind::empty);
	refill(server_loaded,server_reserve,reload_kind::empty);
	check(server_loaded == 15 && server_reserve == 43, "empty fill twice remains 14+1");
	refill(server_loaded,server_reserve,reload_kind::tactical);
	check(server_loaded == 16 && server_reserve == 42, "separate tactical top-off after empty reload");
	check(capacity(r,15,std::numeric_limits<int>::max(),boundary::refill,reload_kind::tactical) == 15, "corrupt count passthrough");
	check(reload_capacity({0,true,true},true) == 0 && reload_capacity({1001,true,true},true) == 0, "invalid capacities rejected");
	state invalid{false,true,1,action_state::closed};
	const auto before = invalid;
	int extracted = 42;
	check(!cycle(r,invalid,extracted) && invalid == before && extracted == 42, "invalid cycle unchanged");
	check(!release(r,invalid) && invalid == before, "invalid release unchanged");
	std::cout << "closed bolt failures=" << failures << '\n';
	return failures ? 1 : 0;
}
