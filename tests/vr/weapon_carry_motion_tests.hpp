#pragma once
#include "component/vr/gameplay/weapon_carry_motion.hpp"
#include "component/vr/gameplay/weapon_carry.hpp"
#include "component/vr/gameplay/grip_edges.hpp"

namespace weapon_carry_motion_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons;
		using namespace carry;
		using vr::hand;
		namespace hands = vr::gameplay::hands;
		using namespace std::chrono_literals;
		// Production motion history -> real release gate -> carry transaction.
		// The drop callback observes the impulse supplied to the native boundary.
		for (auto pipeline : {vr::controller_pose_pipeline::mode::legacy, vr::controller_pose_pipeline::mode::standard})
		for (unsigned h = 0; h < 2; ++h)
		for (bool released_while_missing : {false, true})
		{
			inventory held;
			const owned_weapon item[]{{900, {true, true}}};
			check(held.reconcile(item), "motion fixture admits an owned weapon");
			const auto id = held.find_definition(900)->id;
			check(held.equip(id, static_cast<hand>(h)), "motion fixture equips either physical hand");
			hand_motion_history motion;
			grip_edge_gate edges;
			vr::controller_input::digital_sampler buttons;
			vr::controller_input::frame input;
			input.pose_pipeline = pipeline;
			input.focused = true;
			input.reference_generation = input.continuity_generation = 1;
			for (unsigned n = 0; n < 2; ++n)
				input.grip[n].quality = input.aim[n].quality = {true, true, true, true, true};
			input.sampled_at = vr::controller_input::clock::time_point{} + 1s;
			std::array<vr::head_pose_bridge::world_pose, 2> poses;
			poses[h].position = {1000, 2000, 150};
			poses[1 - h].position = {1100, 2000, 150};
			unsigned drops{};
			hands::vec impulse{};
			const auto tick = [&](bool down, bool available, float displacement = 0.f)
			{
				const auto before = input;
				++input.sequence;
				input.sampled_at += 10ms;
				for (unsigned n = 0; n < 2; ++n)
					input.grip[n].valid = input.aim[n].valid = n != h || available;
				input.squeeze[h] = buttons.sample(true, down, input.sampled_at);
				if (vr::controller_input::producer_discontinuity(before, input)) ++input.continuity_generation;
				poses[h].position[0] += displacement;
				const auto velocity = motion.update(input, poses, available ? 3u : (1u << (1 - h)), 100);
				const auto event = edges.consume(input, true, input.sampled_at);
				if (event.released)
					held.release(id, event.released, location::absent, true, [&](const instance&)
					{
						++drops;
						impulse = velocity[h];
						return true;
					});
				return velocity;
			};
			check(hands::length(tick(true, true)[h]) == 0, "first valid sample seeds motion");
			check(std::abs(tick(true, true, 1)[h][0] - 100) < .01f, "continuous motion remains nonzero");
			tick(!released_while_missing, false);
			tick(!released_while_missing, false);
			check(drops == 0 && held.in_hand(static_cast<hand>(h)), "pose loss cannot drop a held weapon");
			const auto recovery = tick(!released_while_missing, true);
			check(hands::length(recovery[h]) == 0, "recovered pose cannot differ from an invalid world origin");
			check(drops == unsigned(released_while_missing) && hands::length(impulse) == 0,
				"a deferred real release reaches drop once with zero recovery impulse");
			check(std::abs(tick(!released_while_missing, true, 2)[h][0] - 200) < .01f,
				"the next continuous sample restores physical velocity");
			tick(false, true, 3);
			check(drops == 1 && (released_while_missing || std::abs(impulse[0] - 300) < .01f),
				"held-through-loss release preserves normal throwing and never replays a drop");
		}
		{
			hand_motion_history motion;
			vr::controller_input::frame input;
			input.focused = true;
			input.sequence = input.reference_generation = 1;
			for (unsigned n = 0; n < 2; ++n)
			{
				input.grip[n].valid = input.aim[n].valid = true;
				input.grip[n].quality = input.aim[n].quality = {true, true, true, true, true};
			}
			input.sampled_at = vr::controller_input::clock::time_point{} + 1s;
			std::array<vr::head_pose_bridge::world_pose, 2> poses;
			motion.update(input, poses, 3, 100);
			++input.sequence; input.sampled_at += 10ms; poses[1].position[0] = 1;
			motion.update(input, poses, 2, 100);
			++input.sequence; input.sampled_at += 10ms; poses[1].position[0] = 2;
			check(std::abs(motion.update(input, poses, 2, 100)[1][0] - 100) < .01f,
				"opposite hand loss does not invalidate the valid hand's local history");
			for (unsigned change = 0; change < 9; ++change)
			{
				++input.sequence; input.sampled_at += 10ms; poses[1].position[0] += 1000;
				if (change == 0) ++input.reference_generation;
				if (change == 1) ++input.pose_reference_generation;
				if (change == 2) ++input.continuity_generation;
				if (change == 3) input.pose_pipeline = vr::controller_pose_pipeline::mode::standard;
				if (change == 4) input.focused = false;
				if (change == 5) input.orientation_settling = true;
				if (change == 6) input.sampled_at += 200ms;
				if (change == 7) input.grip[1].quality.position_tracked = false;
				if (change == 8) input.grip[1].quality.position_tracked = true;
				check(hands::length(motion.update(input, poses, 3, 100)[1]) == 0,
					"basis changes, focus, calibration and stale samples cannot become a drop impulse");
				input.focused = true; input.orientation_settling = false;
			}
		}
	}
}
