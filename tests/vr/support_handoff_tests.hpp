#pragma once
#include "component/vr/gameplay/physical_reload_runtime.hpp"
#include "component/vr/gameplay/underbarrel_runtime.hpp"

namespace support_handoff_tests
{
	template <class Check> void run(Check check)
	{
		namespace w = vr::gameplay::weapons;
		namespace reload = w::physical_reload;
		namespace module = w::underbarrel;
		for (auto rear : {vr::hand::left, vr::hand::right})
		{
			const auto offhand = vr::hand(1 - int(rear));
			w::hold owner{7, 1, rear, offhand, w::hold_source::interaction, 1};
			w::reload_profile definition{};
			definition.interaction.support_magazine_catch = true;
			reload::presentation magazine;
			magazine.active = true;
			magazine.definition = &definition;
			magazine.ammo.magazine_hand = offhand;
			check(reload::takes_carry_support(magazine, owner),
			      "settled offhand magazine takes its pistol support lease");
			magazine.ammo.magazine_hand = rear;
			check(!reload::takes_carry_support(magazine, owner),
			      "another hand's magazine cannot remove carry support");
			magazine.ammo.magazine_hand = offhand;
			definition.interaction.support_magazine_catch = false;
			check(!reload::takes_carry_support(magazine, owner),
			      "ordinary magazine families retain their authored support policy");
			definition.interaction.support_magazine_catch = true;
			magazine.active = false;
			check(!reload::takes_carry_support(magazine, owner),
			      "inactive mechanical state cannot seize support");
			magazine.active = true;
			magazine.definition = nullptr;
			check(!reload::takes_carry_support(magazine, owner),
			      "missing recipe never grants a support handoff");
			magazine.definition = &definition;
			owner.support = vr::hand::none;
			check(!reload::takes_carry_support(magazine, owner),
			      "absent support does not repeat an earlier handoff");

			module::presentation secondary;
			secondary.owns_support = true;
			secondary.grip = module::lease::action;
			check(module::carry_support_handoff(secondary, owner, {.grip_down = true}) ==
			          module::support_handoff::acquire,
			      "a held secondary grasp requests the matching free offhand");
			check(
			    module::carry_support_handoff(secondary, owner, {.grip_down = true, .hand_occupied = true}) ==
			        module::support_handoff::unchanged,
			    "secondary support cannot replace another carried object's hand");
			check(module::carry_support_handoff(secondary, owner, {}) == module::support_handoff::unchanged,
			      "released squeeze does not create a new secondary support owner");
			owner.support = offhand;
			secondary.grip = module::lease::none;
			check(
			    module::carry_support_handoff(secondary, owner, {.grip_down = true, .hand_occupied = true}) ==
			        module::support_handoff::release,
			    "ended secondary lease releases its former support even while Grip remains down");
			secondary.owns_support = false;
			check(module::carry_support_handoff(secondary, owner, {}) == module::support_handoff::unchanged,
			      "ordinary foregrip ownership is outside the secondary module's handoff");
			secondary.owns_support = true;
			owner.rear = vr::hand::none;
			check(module::carry_support_handoff(secondary, owner, {.grip_down = true}) ==
			          module::support_handoff::unchanged,
			      "carry-only support cannot manufacture a firing control owner");
		}
		for (auto rear : {vr::hand::left, vr::hand::right})
			for (auto type : {module::kind::m203, module::kind::gp25, module::kind::shotgun})
				for (auto grasp : {module::lease::support, module::lease::firing, module::lease::action})
				{
					if (type == module::kind::gp25 && grasp != module::lease::support)
						continue;
					const auto off = vr::hand(1 - int(rear));
					w::carry::inventory inventory;
					const std::array<w::carry::owned_weapon, 1> items{{{53, {}}}};
					check(inventory.reconcile(items) && inventory.equip_definition(53, rear),
					      "underbarrel regrasp fixture equips a host");
					const auto id = inventory.find_definition(53)->id;
					check(inventory.support(id, off), "module hand holds the same physical host");
					module::presentation v;
					v.active = v.owns_support = true;
					v.owner = inventory.find(id)->owner;
					v.grip = v.support_role = grasp;
					v.reference = 3;
					v.travel = grasp == module::lease::action ? .04f : 0.f;
					v.ammo = module::import_native({id, 54, type}, module::capacity(type), 9);
					if (grasp == module::lease::action)
					{
						v.ammo = module::import_native({id, 54, type}, 0, 9);
						const auto opened = module::plan(
						    v.ammo, {module::operation::open, v.ammo.id, v.ammo.revision, rear, off});
						check(
						    bool(opened),
						    "partially closing fixture starts from an open empty launcher or shotgun action");
						v.ammo = opened.next;
					}
					const auto before = v.ammo;
					const auto travel = v.travel;
					const vr::controller_input::digital_action acquired{true, true, 4, 2, 1};
					vr::controller_input::frame input;
					input.focused = true;
					input.reference_generation = v.reference;
					input.squeeze[int(off)] = acquired;
					input.grip[int(off)].valid = input.aim[int(off)].valid = true;
					const unsigned release_control = 1u << int(rear);
					const unsigned release_support = 1u << int(off);
					// Production prepares mechanical state BEFORE inventory.release.
					// Clearing this lease here caused the later regrasp to detach it,
					// despite continuous module Grip and a valid carry-only owner.
					const bool keep_module = module::retain_grip_on_control_release(
					    v, v.owner, release_control, input, acquired);
					check(keep_module, "control release preparation preserves the existing module lease");
					check(!module::retain_grip_on_control_release(v, v.owner, 3u, input, acquired) &&
					          !module::retain_grip_on_control_release(v, v.owner, release_support, input, acquired),
					      "releasing the module hand or both hands still prepares interruption and escrow cleanup");
					if (!keep_module)
						v.grip = module::lease::none;
					const auto released = inventory.release(id,
					                                        release_control,
					                                        w::carry::location::absent,
					                                        false,
					                                        [](const auto&) { return false; });
					const auto carried = inventory.find(id)->owner;
					check(
					    released.action == w::carry::outcome::carry_only &&
					        module::can_retain_module_grip(v, carried, input, acquired),
					    "releasing only the host control grip retains the original module Grip press for every mechanism");
					v.owner = carried;
					check(!module::retain_grip_on_control_release(v, carried, release_support, input, acquired),
					      "releasing the sole remaining module hand cannot retain a carry-only weapon");
					check(module::module_grip_hand(v) == off && !v.owner.can_fire() &&
					          module::carry_support_handoff(
					              v, carried, {.grip_down = true, .hand_occupied = true}) ==
					              module::support_handoff::unchanged,
					      "module hand remains a non-firing carrier while the host control is absent");
					check(inventory.control(id, rear), "original hand can regrasp the actual host control");
					const auto restored = inventory.find(id)->owner;
					check(module::can_retain_module_grip(v, restored, input, acquired),
					      "control regrasp preserves the continuously held module lease");
					v.owner = restored;
					check(
					    module::carry_support_handoff(
					        v, restored, {.grip_down = true, .hand_occupied = true}) ==
					            module::support_handoff::unchanged &&
					        inventory.find(id)->owner.support == off && v.grip == grasp &&
					        v.support_role == grasp && v.travel == travel &&
					        v.ammo.revision == before.revision && v.ammo.open == before.open &&
					        v.ammo.chamber == before.chamber &&
					        module::total(v.ammo) == module::total(before),
					    "host regrasp cannot detach the module hand, change its grasp, or advance an action/ammo transaction");
					for (int fault = 0; fault < 8; ++fault)
					{
						auto broken = input;
						auto wrong = restored;
						switch (fault)
						{
						case 0:
							broken.squeeze[int(off)].down = false;
							break;
						case 1:
							++broken.squeeze[int(off)].generation;
							break;
						case 2:
							++broken.squeeze[int(off)].presses;
							break;
						case 3:
							++broken.squeeze[int(off)].releases;
							break;
						case 4:
							++broken.reference_generation;
							break;
						case 5:
							broken.grip[int(off)].valid = false;
							break;
						case 6:
							broken.focused = false;
							break;
						case 7:
							++wrong.instance_generation;
							break;
						}
						check(
						    !module::can_retain_module_grip(v, wrong, broken, acquired),
						    "released, reacquired, stale or foreign module hands cannot inherit the retained grip");
						check(!module::retain_grip_on_control_release(v, wrong, release_control, broken, acquired),
						      "release preparation rejects a broken module Grip witness before changing ownership");
					}
				}
	}
}
