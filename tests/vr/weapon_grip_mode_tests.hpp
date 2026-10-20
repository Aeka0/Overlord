#pragma once
#include "component/vr/gameplay/grip_edges.hpp"
#include "component/vr/gameplay/weapon_carry_grip.hpp"

namespace weapon_grip_mode_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons;
		using namespace carry;
		using namespace std::chrono_literals;
		for (unsigned h = 0; h < 2; ++h)
		{
			const auto actor = hand(h);
			const auto bit = 1u << h;
			inventory owned;
			const owned_weapon items[]{{101, {true, true}}, {102, {true, true}}, {64, {false, false, true}}};
			check(owned.reconcile(items), "grip mode fixture has ordinary and mission equipment");
			grip_edge_gate gate;
			vr::controller_input::digital_sampler sampler;
			vr::controller_input::frame f;
			f.focused = true;
			f.reference_generation = 1;
			f.sampled_at = vr::controller_input::clock::time_point{} + 1s;
			bool toggle = true;
			const auto tick = [&](bool down, bool pose = true, bool active = true)
			{
				++f.sequence;
				f.sampled_at += 10ms;
				f.grip[h].valid = pose;
				f.squeeze[h] = sampler.sample(active, down, f.sampled_at);
				return gate.consume(f, true, f.sampled_at, toggle_grip_owners(owned, toggle));
			};
			tick(false);
			check(tick(true).pressed == bit, "empty hand still acquires only on a physical press");
			owned.equip_definition(101, actor);
			const auto id = owned.in_hand(actor)->id;
			check(tick(false).released == 0 && tick(false).released == 0,
			      "toggle pickup ignores physical release and repeated neutral input");
			const auto release = tick(true);
			check(release.released == bit && release.pressed == 0 && release.consumed == bit,
			      "second press releases the weapon and cannot also acquire another object");
			check(tick(true).released == 0, "a held second press cannot repeat a failed drop");
			tick(false);
			auto retry = tick(true);
			const auto exchanged = owned.release(id, retry.released, owned.find_definition(102)->at,
			                                    true, [](const auto&) { return true; });
			check(exchanged.action == outcome::exchanged, "toggle release uses the existing holster exchange");
			gate.latch(actor);
			check(tick(false).released == 0 && tick(true).released == bit,
			      "exchanged weapon stays held until another press");

			tick(false);
			const auto lost = tick(true, false);
			check(lost.released == 0 && lost.deferred == bit && lost.consumed == bit,
			      "toggle release waits for valid geometry while reserving its press");
			tick(false, false);
			check(tick(false).released == bit && tick(false).released == 0,
			      "a quick second tap during pose loss settles once on recovery");
			tick(true, false);
			++f.reference_generation;
			check(tick(false).released == 0, "recenter cancels deferred toggle release");
			tick(true, false);
			f.focused = false;
			tick(true);
			f.focused = true;
			check(tick(true).released == 0 && tick(false).released == 0,
			      "focus recovery cannot drop a toggled weapon");
			check(tick(true).released == bit, "next fresh press works after focus recovery");
			tick(false);
			tick(true, false);
			owned.equip_definition(101, actor);
			check(tick(false).released == 0, "deferred press cannot drop a replacement instance");
			tick(true, false);
			toggle = false;
			check(tick(true).released == 0, "changing to hold cancels a pending toggle press");
			tick(false);
			tick(true);
			check(tick(false).released == bit, "hold mode still releases on button up");
			toggle = true;
			tick(true, true, false);
			check(tick(true).released == 0 && tick(false).released == 0,
			      "inactive controller recovery requires a new toggle press");

			owned.equip_definition(64, actor);
			tick(true);
			check(tick(false).released == bit, "mission equipment keeps physical release in toggle mode");
			owned.put_away();
			tick(true);
			check(!owned.in_hand(actor) && tick(false).released == bit,
			      "failed pickup never creates logical weapon ownership");

			owned.equip_definition(101, actor);
			const auto other = hand(1 - h);
			owned.support(owned.in_hand(actor)->id, other);
			const auto normal = toggle_grip_owners(owned, true);
			const auto mechanical = toggle_grip_owners(owned, true, 1u << unsigned(other));
			check(normal[h] && normal[1 - h] && mechanical[h] && !mechanical[1 - h],
			      "ordinary support toggles while mechanical module grips retain physical input");
		}
		{
			inventory owned;
			const owned_weapon items[]{{101, {true, true}}};
			owned.reconcile(items);
			owned.equip_definition(101, hand::right);
			owned.support(owned.in_hand(hand::right)->id, hand::left);
			grip_edge_gate gate;
			vr::controller_input::frame f;
			f.focused = true;
			f.sequence = f.reference_generation = 1;
			f.sampled_at = vr::controller_input::clock::now();
			for (unsigned h = 0; h < 2; ++h)
			{
				f.grip[h].valid = f.squeeze[h].active = true;
				f.squeeze[h].generation = 1;
			}
			gate.consume(f, true, f.sampled_at, toggle_grip_owners(owned, true));
			++f.sequence;
			for (auto& button : f.squeeze)
			{
				++button.presses;
				button.down = true;
			}
			const auto edges = gate.consume(f, true, f.sampled_at, toggle_grip_owners(owned, true));
			unsigned drops{};
			const auto released = owned.release(owned.in_hand(hand::right)->id, edges.released,
			                                    location::absent, true, [&](const auto&) { ++drops; return true; });
			check(edges.released == 3 && edges.consumed == 3 && drops == 1 &&
			      released.action == outcome::dropped && owned.invariant(),
			      "simultaneous toggle presses drop once without transient hand promotion");
		}
	}
}
