#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/weapons/m9/mechanics.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/miniuzi/profile.hpp"
#include "component/vr/gameplay/weapons/ak47/mechanics.hpp"
#include "open_bolt_tests.hpp"
#include <iostream>
#include <limits>

int main()
{
	using namespace vr::gameplay::weapons;
	using namespace vr::gameplay::weapons::mechanics;
	int failures{};
	const auto check = [&](bool condition, const char* label) {
		if (!condition)
		{
			++failures;
			std::cerr << "FAIL: " << label << '\n';
		}
	};
	const auto rules = m9::reload_rules;
	open_bolt_tests::run(check);
	state s{49, 1, 1, true, true, 14, 90};
	const auto initial = s;
	const auto request_for = [&](operation op, hand actor) {
		return request{op, s.weapon, s.instance_generation, s.revision, hand::right, actor};
	};
	const auto apply = [&](operation op, hand actor) {
		const auto old = s;
		const auto tx = plan(rules, s, request_for(op, actor));
		if (!tx)
			return false;
		check(tx.before == native_ammo(old), "native compare-before is exact");
		check(total_rounds(old) == total_rounds(tx.next) + tx.rounds_spent, "conservation per commit");
		check(!plan(rules, tx.next, request_for(op, actor)), "same revision cannot commit twice");
		s = tx.next;
		return true;
	};
	check(valid(rules, s) && ready(rules, s), "initial closed-bolt partition 14+1");
	check(!ready(rules, s, true), "action manipulation prevents firing");
	check(!apply(operation::release_button, hand::left), "off hand cannot press rear release");
	check(apply(operation::release_button, hand::right), "tactical magazine ejection");
	check(!s.magazine_inserted && s.chamber_loaded && s.reserve_rounds == 104,
		  "ejection preserves chamber and refunds magazine");
	check(ready(rules, s), "chamber-only shot remains possible");
	check(!apply(operation::release_button, hand::right), "double eject cannot refund twice");
	check(!apply(operation::draw_magazine, hand::right), "rear cannot spawn a magazine");
	check(apply(operation::draw_magazine, hand::left) && s.held_rounds == 15 && s.reserve_rounds == 89,
		  "draw reserves rounds once");
	check(!apply(operation::draw_magazine, hand::left), "cannot draw two magazines in one hand");
	check(apply(operation::insert_magazine, hand::left), "latch commits magazine");
	check(native_ammo(s).loaded == 16 && s.chamber_loaded, "full magazine plus chamber");
	check(!apply(operation::insert_magazine, hand::left), "repeat contact cannot insert again");
	check(apply(operation::accepted_shot, hand::right) && s.magazine_rounds == 14 && s.chamber_loaded,
		  "shot consumes one and cycles feed");
	for (int i = 0; i < 15; ++i)
		check(apply(operation::accepted_shot, hand::right), "remaining shots");
	check(s.action == action_state::locked_open && !s.chamber_loaded && !ready(rules, s), "last-round lock");
	check(!apply(operation::accepted_shot, hand::right), "empty fire cannot spend ammunition");
	check(apply(operation::cycle_action, hand::left) && s.action == action_state::locked_open,
		"racking over empty follower remains locked");
	check(apply(operation::release_button, hand::right) && !s.magazine_inserted &&
			  s.action == action_state::locked_open,
		  "empty follower prevents release; B ejects empty magazine instead");
	check(apply(operation::release_button, hand::right) && !s.chamber_loaded &&
			  s.action == action_state::closed,
		  "B without magazine closes slide but does not chamber");
	check(!apply(operation::release_button, hand::right), "closed slide cannot be released again");
	check(apply(operation::draw_magazine, hand::left) && apply(operation::insert_magazine, hand::left),
		  "empty-chamber reload latch");
	check(!ready(rules, s) && !s.chamber_loaded && s.magazine_rounds == 15,
		  "insertion alone cannot auto-chamber, even after prior empty-lock release");
	const auto inserted = s;
	check(apply(operation::release_button,hand::right) && !s.magazine_inserted && !s.chamber_loaded,
		"B on closed slide ejects, not a hidden chamber operation");
	s = inserted;
	check(apply(operation::cycle_action, hand::left) && ready(rules, s) && s.magazine_rounds == 14,
		  "full pull-and-release feeds exactly one");
	const auto total = total_rounds(s);
	const auto reserve_before_extraction = s.reserve_rounds;
	check(apply(operation::cycle_action, hand::left) && total_rounds(s) == total-1 &&
		  s.reserve_rounds == reserve_before_extraction,
		  "complete live extraction loses one round without a reserve refund");
	const auto pre_stroke = total_rounds(s);
	check(apply(operation::extract_chamber,hand::left) && s.action == action_state::held_open &&
		!s.chamber_loaded && total_rounds(s) == pre_stroke-1, "full rearward stroke extracts immediately");
	check(!apply(operation::extract_chamber,hand::left), "holding rearward cannot extract twice");
	check(!apply(operation::release_button,hand::right), "B cannot override physically held slide");
	check(!apply(operation::accepted_shot,hand::right), "held-open slide cannot fire");
	check(!apply(operation::draw_magazine,hand::left), "slide hand cannot draw magazine until release");
	check(apply(operation::finish_stroke,hand::left) && ready(rules,s) && total_rounds(s) == pre_stroke-1,
		"release feeds once without spending another round");
	check(apply(operation::draw_magazine, hand::left), "can stage fresh magazine before ejecting");
	check(!apply(operation::insert_magazine, hand::left), "occupied well rejects insertion");
	check(!apply(operation::cycle_action, hand::left), "magazine hand cannot also rack slide");
	const int held = s.held_rounds, reserve = s.reserve_rounds;
	check(apply(operation::cancel_magazine, hand::left) && s.reserve_rounds == reserve + held,
		  "cancellation refunds reserved magazine");
	check(!apply(operation::cancel_magazine, hand::left), "second cancellation cannot duplicate ammunition");

	s = initial;
	check(apply(operation::release_button, hand::right) && apply(operation::accepted_shot, hand::right),
		  "fire remaining chamber after ejection");
	check(!ready(rules, s) && s.action == action_state::closed,
		  "no magazine follower means no last-round lock");
	s = {49, 2, 1, false, false, 0, 3};
	check(apply(operation::draw_magazine, hand::left) && s.held_rounds == 3 && s.reserve_rounds == 0,
		  "low reserve draws partial magazine");
	check(apply(operation::cancel_magazine, hand::left) && s.reserve_rounds == 3, "partial refund");
	s.reserve_rounds = 0;
	check(!apply(operation::draw_magazine, hand::left), "empty reserve creates no magazine");
	s = {49, 3, 1, false, false, 0, 30, 0, hand::none, action_state::locked_open};
	check(apply(operation::draw_magazine, hand::left) && apply(operation::insert_magazine, hand::left),
		  "insert while still locked open");
	check(!ready(rules, s) && s.action == action_state::locked_open, "latch preserves lock");
	check(apply(operation::release_button, hand::right) && s.magazine_rounds == 14 && ready(rules, s),
		  "release lock feeds one without changing total loaded");

	s = initial;
	auto stale = request_for(operation::release_button, hand::right);
	++stale.instance_generation;
	check(!plan(rules, s, stale), "checkpoint/instance generation mismatch");
	stale = request_for(operation::release_button, hand::right);
	++stale.weapon;
	check(!plan(rules, s, stale), "another weapon cannot consume transaction");
	stale = request_for(operation::release_button, hand::left);
	stale.rear = hand::left;
	check(bool(plan(rules, s, stale)), "rear semantics permit future authored left-handed profiles");
	auto pull_rules = rules;
	pull_rules.release = magazine_release::physical_pull;
	check(!plan(pull_rules, s, request_for(operation::release_button, hand::right)),
		  "latch-fed profile denies B eject");
	check(bool(plan(pull_rules, s, request_for(operation::pull_magazine, hand::left))),
		  "physical-pull profile ejects");
	check(!plan(rules, s, request_for(operation::pull_magazine, hand::left)),
		  "M9 button-only ejection policy");
	s.reserve_rounds = std::numeric_limits<int>::max();
	check(!valid(rules, s) && !apply(operation::release_button, hand::right), "overflow budget rejected");
	s = initial;
	s.magazine_rounds = -1;
	check(!valid(rules, s), "negative feed rejected");
	s = initial;
	s.revision = std::numeric_limits<std::uint64_t>::max();
	check(!apply(operation::release_button, hand::right), "revision cannot wrap");
	s = initial;
	s.action = action_state::locked_open;
	check(!valid(rules, s), "loaded chamber cannot coexist with locked-open action");
	s = initial;
	s.magazine_inserted = false;
	check(!valid(rules, s), "detached feed cannot contain rounds");

	// Bounded exhaustive short sequences: every accepted transition conserves
	// rounds or spends one shot/extraction; include both split-stroke events.
	// Closed/open bolt policies, eleven operations, five steps, including physical pull,
	// spare strikes, rejected commits and held-open interruptions.
	for (const auto& policy : {rules,ak47::reload_rules,miniuzi::reload_rules})
	for (int mask = 0; mask < 161051; ++mask)
	{
		s = initial;
		check(from_native_automatic(policy,policy.magazine_capacity,s),"sequence begins with policy-specific native admission");
		int bits = mask;
		for (int i = 0; i < 5; ++i, bits /= 11)
		{
			const auto op = static_cast<operation>(bits % 11);
			const auto actor=op==operation::release_button || op==operation::accepted_shot || op==operation::dry_fire ? hand::right : hand::left;
			const auto request=request_for(op,actor);
			const auto tx=plan(policy,s,request);
			if (!tx) continue;
			check(tx.before==native_ammo(s) && total_rounds(s)==total_rounds(tx.next)+tx.rounds_spent,"cross-policy exact native snapshot and ammo conservation");
			check(valid(policy,tx.next) && !plan(policy,tx.next,request),"cross-policy state validity and revision replay rejection");
			if (!policy.last_round_lock) check(tx.next.action!=action_state::locked_open,"operation sequence cannot synthesize unsupported last-round lock");
			if (policy.feed==feed_type::open_bolt) check(!tx.next.chamber_loaded && tx.rounds_spent==(op==operation::accepted_shot ? 1 : 0),
				"open-bolt sequences never chamber ammunition or extract a live round");
			s=tx.next;
		}
	}
	std::cout << "weapon mechanics failures=" << failures << '\n';
	return failures ? 1 : 0;
}
