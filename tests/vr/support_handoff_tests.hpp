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
	}
}
