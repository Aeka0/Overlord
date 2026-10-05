#pragma once
#include "detachable_magazine.hpp"
#include "../controller_haptics.hpp"
#include "physical_reload_profile.hpp"
#include "weapon_sound_reference.hpp"

namespace vr::gameplay::weapons::feedback
{
	using clock = controller_input::clock;
	struct event
	{
		mechanics::effect kind{};
		hold owner{};
		std::uint64_t reference{};
		clock::time_point at{};
		std::array<float, 3> position{};
		const reload_profile* definition{};
		std::uint64_t mechanical_instance{}; // Zero for native firing; reject physical events after level reset.
		const cylinder_profile* cylinder_definition{};
		sound_reference explicit_sound{};
		bool independent_shot{}; // Native +attack audio is suppressed for this committed shot.
		hands::anchor muzzle{},brass{}; // Frozen confirmed-shot model anchors; never aim-assist-corrected.
		bool has_brass{},last_shot{};
		const tube_profile* tube_definition{};
		bool melee_impact{}; // Confirmed firearm/NPC impact; native blunt sound, not reload/shot audio.
		const break_action_profile* break_definition{};
		std::uint32_t secondary_definition{}; // Feedback resource token, never chassis/hand ownership.
		const launcher_profile* launcher_definition{};
		bool quick_reload_start{};
		bool vehicle{}; // Driver gun uses script ownership instead of carried inventory.
	};
	inline bool fresh(const event& e, const hold& owner, const controller_input::frame& input,
		bool gameplay, clock::time_point now) noexcept
	{
		if (!gameplay || !valid_hand(owner.holding_hand()) || e.owner.id() != owner.id() ||
			e.owner.holding_hand()!=owner.holding_hand() || e.owner.rear != owner.rear ||
			((e.kind==mechanics::effect::shot || e.kind==mechanics::effect::dry_fire) && !owner.can_fire()) ||
			e.owner.rear_revision != owner.rear_revision || e.reference != input.reference_generation ||
			!input.focused || !input.sequence || now < e.at || now-e.at > std::chrono::milliseconds(100) ||
			now < input.sampled_at || now-input.sampled_at > std::chrono::milliseconds(100)) return false;
		for (int h = 0; h < 2; ++h)
			if (((!e.quick_reload_start && !e.vehicle) || h==int(owner.holding_hand())) && (!input.grip[h].valid || !input.aim[h].valid)) return false;
		for (float x : e.position) if (!std::isfinite(x) || std::abs(x) > 1e7f) return false;
		return true;
	}
	struct response
	{
		float rear_amplitude{}, off_amplitude{}, seconds{}, frequency{};
	};
	inline response pattern(mechanics::effect kind) noexcept
	{
		using enum mechanics::effect;
		switch (kind)
		{
		case magazine_out: return {.18f, 0, .025f, 110};
		case magazine_draw: return {0, .16f, .022f, 110};
		case magazine_take: return {.12f, .24f, .030f, 110};
		case magazine_in: return {.18f, .40f, .045f, 140};
		case action_grab: return {0, .12f, .018f, 100};
		case action_rear: return {.12f, .24f, .030f, 100};
		case action_close: return {.32f, 0, .035f, 150};
		case action_latch: return {.12f, .22f, .025f, 130};
		case action_unlatch: return {.08f, .16f, .020f, 120};
		case bolt_unlock: case bolt_lock: return {.05f, .18f, .022f, 130};
		case case_eject: case live_eject: return {.04f, .13f, .018f, 110};
		case bolt_feed: return {0, .08f, .015f, 100};
		case bridge_open: case cover_open: return {0,.16f,.020f,110};
		case bridge_close: case cover_close: return {.10f,.24f,.030f,140};
		case belt_laid: return {0,.18f,.022f,120};
		case dry_fire: return {.22f, 0, .025f, 140};
		case shot: return {.65f, .16f, .045f, 170};
		default: return {};
		}
	}
	// Successful transitions only. Bounded copied events; native sound and
	// SteamVR calls execute outside simulation/render locks on their owners.
	void publish(event value) noexcept;
	// A confirmed inventory operation may leave the hand empty. Its pulse is
	// hand-owned, not validated against a weapon's continuing held lease.
	void carry_confirmation(hand actor,const controller_input::frame& input) noexcept;
	void melee_confirmation(const hold& owner,const controller_input::frame& input,const std::array<float,3>& position) noexcept;
}
