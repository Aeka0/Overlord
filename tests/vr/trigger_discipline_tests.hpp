#pragma once
#include "component/vr/gameplay/hand_rig_builder.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/designator/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/riot_shield/profile.hpp"
#include "component/vr/gameplay/weapons/striker/profile.hpp"
#include "component/vr/gameplay/trigger_discipline.hpp"
#include "component/vr/gameplay/weapon_carry_pose.hpp"
#include "component/vr/gameplay/weapon_registry.hpp"
#include "marine_sniper_hand_data.hpp"
#include <cstring>
#include <limits>

namespace trigger_discipline_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay;
		using namespace hands;
		using vr::hand;
		namespace td = trigger_discipline;
		weapons::hold owner{1, 1, hand::right};
		check(!td::eligible(nullptr, owner), "unbound weapons do not get trigger discipline");
		check(!td::eligible(&weapons::riot_shield::base, owner) && td::eligible(&weapons::designator::base, owner) &&
			trigger_index::find(weapons::designator::base.id)==trigger_index::find("usp"),"designator reuses its USP grip discipline while shields stay excluded");
		check(trigger_index::find(weapons::striker::base.id) == trigger_index::find(weapons::striker::woodland.id) &&
			trigger_index::find(weapons::striker::base.id) != trigger_index::find(weapons::m4::foregrip.id),
			"Striker finishes share their measured drum-side pose independently of other guns");
		check(!trigger_index::find("unknown"), "unmeasured weapons cannot inherit a universal safe angle");
		check(td::eligible(&weapons::m4::foregrip, owner, 1) && !td::eligible(&weapons::m4::foregrip, owner, 2),
			"support manipulation keeps rear discipline; rear manipulation takes precedence");
		owner.rear = hand::none; owner.support = hand::left;
		check(!td::eligible(&weapons::m4::foregrip, owner), "support-only hold has no trigger hand");
		for (auto actor : {hand::left, hand::right}) for (int hz : {60, 72, 90, 120, 144})
		{
			owner = {1, 1, actor};
			const unsigned h = unsigned(actor);
			vr::controller_input::frame input{};
			input.sequence = input.reference_generation = 1; input.focused = true;
			input.grip[h].valid = true; input.trigger_touch[h].active = true;
			input.trigger_touch[1-h] = {true, true};
			td::controller state;
			check(state.update(input, owner, 1, true) == 0, "first sample starts from authored trigger pose");
			const auto advance = [&] {
				++input.sequence;
				input.sampled_at += std::chrono::duration_cast<vr::controller_input::clock::duration>(std::chrono::duration<double>(1.0/hz));
			};
			float weight{};
			for (int i = 0; i < hz; ++i)
			{
				advance(); weight = state.update(input, owner, 1, true);
				check(weight == state.update(input, owner, 1, true), "second stereo eye never advances finger smoothing");
				if (i == hz/2-1) check(weight > .9999f, "untouched trigger reaches extended pose at every refresh rate");
			}
			input.trigger_touch[h].down = true; advance();
			const float returning = state.update(input, owner, 1, true);
			check(returning > 0 && returning < weight, "contact without a click smoothly returns to the trigger");
			check(returning < .68f, "first touched frame returns promptly even at 144 Hz");
			input.trigger_touch[h].down = false; advance();
			check(state.update(input, owner, 1, true) > returning, "reversal continues from current interpolation");
			input.trigger[h] = {true, true};
			for (int i = 0; i < hz/2; ++i) { advance(); weight = state.update(input, owner, 1, true); }
			check(weight < .0001f, "pressed trigger overrides contradictory untouched sensor state");
			input.trigger[h].down = false;
			for (int i = 0; i < hz/2; ++i) { advance(); weight = state.update(input, owner, 1, true); }
			input.sampled_at += std::chrono::seconds(2); ++input.sequence;
			check(state.update(input, owner, 1, true) == weight, "long render gap rebases time without advancing unseen motion");
			++input.reference_generation; advance();
			check(state.update(input, owner, 1, true) == weight, "recenter rebases smoothing");
			++input.continuity_generation; advance();
			check(state.update(input, owner, 1, true) == weight, "producer discontinuity rebases smoothing");
			input.trigger_touch[h].active = false;
			check(state.update(input, owner, 1, true) == 0, "missing touch is unknown, never an extended finger request");
			input.trigger_touch[h].active = true; advance();
			check(state.update(input, owner, 1, true) == 0, "reconnected touch starts cleanly");
			advance(); check(state.update(input, owner, 1, true) > 0, "reconnected touch resumes");
			++owner.instance_generation;
			check(state.update(input, owner, 1, true) == 0, "new weapon instance cannot inherit old finger state");
			advance(); (void)state.update(input, owner, 1, true);
			check(state.update(input, owner, 2, true) == 0, "changed binding resets interpolation");
			advance(); (void)state.update(input, owner, 2, true); input.focused = false;
			check(state.update(input, owner, 2, true) == 0, "lost focus disables discipline");
			input.focused = true; input.grip[h].valid = false;
			check(state.update(input, owner, 2, true) == 0, "lost hand tracking disables discipline");
			input.grip[h].valid = true;
			check(state.update(input, owner, 2, false) == 0, "paused gameplay disables discipline");
		}

		// A captured native glove supplies actual articulated axes and lengths.
		const model_definition model{"viewhands_marine_sniper", 0, 67};
		auto rig = resolve_rig({&model, 1}, marine_sniper_data::bones, rig_kind::hands_only).layout;
		rig.gun = rig.count++; rig.parent[rig.gun] = -1; rig.weapon_bones[rig.gun] = true;
		std::array<bone_definition, 68> definitions{};
		std::copy(marine_sniper_data::bones.begin(), marine_sniper_data::bones.end(), definitions.begin());
		definitions.back() = {"j_gun", -1, {{0,0,0,1}, {}, 2}};
		unsigned profiles{};
		for (const auto& registration : weapons::registered_profiles)
		{
			owner = {1, 1, hand::right};
			if (!td::eligible(registration.value, owner)) continue;
			++profiles;
			auto source = *registration.value; source.equip_rest = {};
			const auto library = bind_weapon_poses(rig, definitions, source);
			check(library.valid, "registered firearm fingers bind to captured glove");
			check(library.safe_index != nullptr, "every registered firearm resolves a measured per-hand index profile");
			if (!library.valid || !library.safe_index) continue;
			for (auto actor : {hand::left, hand::right}) for (const quat rotation : {quat{0,0,0,1}, normalize(quat{.3f,.4f,.1f,.8f})})
			{
				owner.rear = actor;
				weapons::carry::pose_profile adapted(source, owner, rig, library);
				std::array<bone, 68> pose{};
				for (unsigned i = 0; i < pose.size(); ++i) pose[i] = definitions[i].bind;
				const auto& profile = adapted.value;
				std::array<anchor, 2> targets{{{{},rotation},{{},rotation}}};
				check(apply_poses(rig, library, profile, targets, {1,1}, false, pose), "authored and mirrored grip pose applied");
				for (int h = 0; h < 2; ++h)
					vr::gameplay::hands::pose_math::move_part(rig, rig.arms[h].wrist,
						{rotate(rotation, profile.wrists[h].position), normalize(multiply(rotation, profile.wrists[h].rotation))}, pose);
				pose[rig.gun].rotation = rotation;
				const auto before = pose;
				check(td::apply(rig, library, profile, actor, 1, pose), "registered firearm accepts safe index pose for both hands");
				const auto direction = rotate(rotation, td::safe_direction(unsigned(actor), library));
				unsigned changed{};
				for (int i = 0; i < rig.count; ++i)
				{
					const auto index = library.finger[i];
					const bool selected = index >= 0 && profile.fingers[index].name.starts_with(actor == hand::left ? "j_index_le_" : "j_index_ri_");
					if (!selected)
						check(std::memcmp(&before[i], &pose[i], sizeof(bone)) == 0, "wrist, other fingers, opposite hand and weapon stay bit-identical");
					else
					{
						++changed;
						if (registration.value == &weapons::striker::base || registration.value == &weapons::striker::woodland)
						{
							// Beyond j_clip's rear face, stay on the exterior side of its drum.
							const auto local_position = rotate(conjugate(rotation), pose[i].position);
							check(local_position[0] < 10.439f/2.54f || std::abs(local_position[1]) > 6.54f/2.54f,
								"Striker index follows the outside of the drum instead of entering its rear face");
						}
						check(std::abs(length(sub(pose[i].position, pose[rig.parent[i]].position)) - length(library.rest_local[i].position)) < .00002f,
							"safe finger retains native joint lengths");
						if (profile.fingers[index].name.ends_with("_1"))
							check(dot(unit(sub(pose[i].position, pose[rig.parent[i]].position)), direction) > .9999f,
								"proximal index follows this weapon and physical hand's measured receiver fit");
						if (rig.parent[i] != rig.arms[unsigned(actor)].wrist)
						{
							const auto incoming = unit(library.rest_local[i].position);
							vec axis{};
							if (profile.fingers[index].name.ends_with("_1"))
							{
								for (int k = 0; k < rig.count; ++k) if (rig.parent[k] == i) axis = library.rest_local[k].position;
							}
							else axis = library.rest_local[rig.parent[i]].position;
							const auto old_local = multiply(conjugate(before[rig.parent[i]].rotation), before[i].rotation);
							const auto new_local = multiply(conjugate(pose[rig.parent[i]].rotation), pose[i].rotation);
							const auto old_out = unit(rotate(old_local, axis)), new_out = unit(rotate(new_local, axis));
							check(dot(cross(incoming, old_out), cross(incoming, new_out)) >= -.00001f &&
								dot(incoming, new_out) >= dot(incoming, old_out)-.00001f,
								"distal joints open only in their native curl plane and never bend backwards");
						}
					}
				}
				check(changed == 3, "only three index joints are selected");
				pose = before;
				check(!td::apply(rig, library, profile, actor, 0, pose) && std::memcmp(pose.data(), before.data(), sizeof(pose)) == 0,
					"touching trigger preserves exact original articulation");
				check(!td::apply(rig, library, profile, actor, std::numeric_limits<float>::quiet_NaN(), pose), "nonfinite blend rejected");
				auto unmeasured = library; unmeasured.safe_index = nullptr;
				check(!td::apply(rig, unmeasured, profile, actor, 1, pose), "unmeasured pose is left authored instead of silently using a generic angle");
				auto broken = rig; broken.parent[3] = 999;
				check(!td::apply(broken, library, profile, actor, 1, pose), "malformed skeleton rejected before walking parents");
			}
		}
		check(profiles > 50, "discipline geometry audited across registered firearms");
	}
}
