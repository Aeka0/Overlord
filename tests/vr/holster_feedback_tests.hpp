#pragma once
#include "component/vr/gameplay/holster_feedback.hpp"

namespace holster_feedback_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons::carry;
		using vr::hand;
		vr::head_pose_bridge::spatial_frame body{};
		body.units_per_meter = 40;
		body.head_position = {10, 20, 70};
		body.head_yaw_axis = {{{1,0,0}, {0,1,0}, {0,0,1}}};
		const auto slots = locate_holsters(body);
		const auto point = slots.centers[2];
		for (auto actor : {hand::left, hand::right})
		{
			inventory owned;
			const owned_weapon items[]{{101, {}}, {102, {}}};
			owned.reconcile(items);
			const auto first = owned.next_back()->id;
			check(back_slot_candidate(owned, actor, slots, point, true).subject == first,
			      "empty hand receives a back draw cue for the actual next instance");
			owned.draw(location::back, actor);
			check(back_slot_candidate(owned, actor, slots, point, true).action == outcome::stowed,
			      "overflow draw leaves an empty ordinary back slot eligible for storage");
			check(back_slot_candidate(owned, actor, slots, point, false).action == outcome::unchanged &&
			      back_slot_candidate(owned, actor, slots, body.head_position, true).action == outcome::unchanged,
			      "storage restriction and hand outside the slot suppress readiness");
			const auto other = hand(1 - unsigned(actor));
			owned.draw(location::back, other);
			const auto second = owned.in_hand(other)->id;
			owned.release(second, 1u << unsigned(other), location::back, true, [](const auto&) { return false; });
			const auto exchange = back_slot_candidate(owned, actor, slots, point, true);
			check(exchange.action == outcome::exchanged && exchange.received == second &&
			      owned.in_hand(actor)->id == first && owned.in_slot(location::back)->id == second,
			      "exchange preview keeps both original owners unchanged");
			owned.support(first, other);
			check(back_slot_candidate(owned, actor, slots, point, true).action == outcome::unchanged &&
			      back_slot_candidate(owned, other, slots, point, true).action == outcome::unchanged,
			      "releasing only one of two holding hands cannot advertise storage");
		}
		inventory empty;
		check(back_slot_candidate(empty, hand::right, slots, point, true).action == outcome::unchanged,
		      "empty back and overflow never advertise a draw");
		inventory device;
		const owned_weapon mission[]{{64, {false, true, true}}};
		device.reconcile(mission); device.equip_definition(64, hand::right);
		check(back_slot_candidate(device, hand::right, slots, point, true).action == outcome::unchanged,
		      "abdominal mission equipment cannot advertise a back stow");

		vr::controller_input::frame input;
		input.focused = true; input.sequence = input.reference_generation = input.continuity_generation = 1;
		for (unsigned h = 0; h < 2; ++h) input.grip[h].valid = input.aim[h].valid = true;
		back_slot_feedback cue;
		unsigned probes{};
		const auto ready = [&](hand) { ++probes; return true; };
		check(cue.update(input, 1, 0, ready) == 1 && cue.update(input, 3, 0, ready) == 2,
		      "each physical hand receives one independent entry pulse");
		check(!cue.update(input, 3, 0, ready) && probes == 2,
		      "lingering, duplicate frames and exit-margin jitter never re-probe or repeat feedback");
		cue.update(input, 0, 0, ready);
		check(!cue.update(input, 1, 0, [](hand) { return false; }) && cue.update(input, 1, 0, ready) == 1,
		      "blocked contact may become ready after the obstruction clears");
		cue.update(input, 0, 0, ready);
		check(!cue.update(input, 1, 1, ready) && !cue.update(input, 1, 0, ready),
		      "draw or exchange confirmation consumes the visit without an extra readiness pulse");
		input.focused = false;
		check(!cue.update(input, 3, 0, ready), "unfocused input emits no slot cues");
		input.focused = true; input.grip[0].valid = false;
		check(cue.update(input, 3, 0, ready) == 2, "one lost hand cannot suppress the other hand's cue");
		input.grip[0].valid = true; ++input.reference_generation;
		check(cue.update(input, 3, 0, ready) == 3, "recenter invalidates previous spatial contact latches");
		++input.continuity_generation;
		check(cue.update(input, 3, 0, ready) == 3, "new tracking continuity cannot retain old contact latches");
	}
}
