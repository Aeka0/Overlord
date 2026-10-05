#pragma once

#include "detachable_magazine.hpp"
#include "component/vr/digital_button_gate.hpp"
#include "hand_pose_solver.hpp"
#include "physical_reload_geometry.hpp"
#include "part_grip_pose.hpp"
#include "magazine_manipulation.hpp"
#include "magazine_well_contact.hpp"
#include "handle_catch.hpp"
#include "receiver_bolt_release.hpp"
#include "rotating_bolt_gesture.hpp"
#include "belt_gesture.hpp"
#include "hand_interaction/access.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	using clock = controller_input::clock;
	using hands::vec;
	enum class action_motion { reciprocating_slide, charging_handle, rotating_bolt };
	struct geometry
	{
		bool valid{};
		std::uint32_t weapon{};
		std::uint64_t instance_generation{}, reference_generation{}, input_sequence{};
		// All values are metres. The producer must use the SAME solved gun pose
		// and controller frame for these quantities, never a screen-space proxy.
		float waist_distance{}, slide_distance{}, insertion_alignment{};
		vec hand_in_gun{}, magazine_top_in_well{}; // well +Z points into the gun
		float seated_hand_distance{}; // raw tracked wrist to seated magazine grip, metres
		std::uint8_t slide_pose{}; // Candidate only; the simulation freezes it on acquisition.
		magazine_contact magazine{};
		handle_catch_input catch_input{};
		bool diagnose_slap{}; // Opt-in observation only; never an interaction gate.
		vec bolt_hand{}; // Raw contact point around the bolt axis, gun-local metres.
		bool magazine_facing{}; // Raw wrist closer to magazine grasp; only resolves fresh overlaps.
		belt_feed::contact belt{};
		bool knife_held{}; // Knife occupancy; only explicitly authored part co-grasps are allowed.
		std::uint8_t magazine_pose{};
		std::uint8_t attached_magazine_pose{};
	};
	struct profile
	{
		float waist_radius{}, slide_radius{}, slide_stroke{}, locked_travel{}, full_stroke{};
		float slide_lateral_limit{}, well_radius{}, well_contact_depth{}, insertion_cosine{}, max_contact_step{};
		vec slide_axis{}; // gun-local unit rearward direction
		float well_capture_below{.012f}, part_release_distance{.35f};
		float well_release_margin{.04f}; // hysteresis only AFTER a valid mouth contact
		float close_travel{.003f}; // forward completion threshold, below full_stroke
		std::uint8_t slide_pose_count{1};
		action_motion motion{action_motion::reciprocating_slide};
		const magazine_manipulation* manual_magazine{};
		const handle_catch* manual_catch{};
		const rotating_bolt::profile* manual_bolt{};
		const belt_feed::profile* belt{};
		std::uint8_t knife_slide_pose_count{}; // Separate pose set, latched at acquisition like ordinary styles.
		const receiver_bolt_release* receiver_release{};
		std::uint8_t magazine_pose_count{1};
		float button_magazine_radius{.05f};
		bool support_magazine_catch{}; // Enclosed pistol: exchange Grip support for a Trigger-maintained magazine.
		float well_withdraw_margin{.015f}; // Exit/reentry gate, separate from staged-contact hysteresis.
	};
	// Explicit opt-in for the reviewed box-magazine families. Acquisition grows
	// by 1 cm per boundary; alignment, retention and discontinuity rules retain
	// their original meanings, and unrelated feeds keep their authored volumes.
	inline constexpr profile with_box_magazine_well(profile p) noexcept
	{
		p.well_radius=.055f;p.well_contact_depth=.070f;p.well_capture_below=.070f;
		return p;
	}
	inline bool finite(vec v) noexcept
	{
		return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
	}
	inline bool valid(const profile& p) noexcept
	{
		const float values[]{p.waist_radius, p.slide_radius, p.slide_stroke, p.full_stroke,
			p.slide_lateral_limit, p.well_radius, p.well_contact_depth, p.max_contact_step,
			p.well_capture_below,p.part_release_distance,p.well_release_margin,p.close_travel,p.button_magazine_radius,p.well_withdraw_margin};
		for (auto v : values) if (!std::isfinite(v) || v <= 0 || v > 1) return false;
		return p.magazine_pose_count>0 && p.magazine_pose_count<=max_part_grips && (!p.manual_magazine || valid(*p.manual_magazine)) &&
			(!p.support_magazine_catch || (!p.manual_magazine && !p.belt)) &&
			(!p.receiver_release || (valid(*p.receiver_release) && !p.manual_bolt && !p.belt)) &&
			(!p.manual_catch || (valid(*p.manual_catch) && p.motion==action_motion::charging_handle)) &&
			p.slide_pose_count > 0 && p.slide_pose_count <= max_part_grips &&
			p.knife_slide_pose_count <= max_part_grips && (!p.knife_slide_pose_count ||
				(p.motion==action_motion::reciprocating_slide && !p.manual_bolt && !p.manual_catch && !p.belt)) &&
			(p.motion==action_motion::reciprocating_slide || p.motion==action_motion::charging_handle || p.motion==action_motion::rotating_bolt) &&
			((p.motion==action_motion::rotating_bolt)==bool(p.manual_bolt)) &&
			(!p.manual_bolt || (rotating_bolt::valid(*p.manual_bolt) && p.manual_bolt->stroke==p.slide_stroke && p.locked_travel==0 && !p.manual_catch)) &&
			(p.motion!=action_motion::charging_handle || p.locked_travel==0) &&
			finite(p.slide_axis) && std::abs(hands::dot(p.slide_axis, p.slide_axis) - 1) < .001f &&
			std::isfinite(p.locked_travel) && p.locked_travel >= 0 && p.locked_travel < p.full_stroke &&
			p.close_travel < p.full_stroke && p.full_stroke <= p.slide_stroke && std::isfinite(p.insertion_cosine) &&
			p.insertion_cosine > -1 && p.insertion_cosine <= 1;
	}
	// Shared by authoritative return detection and current-pose presentation.
	inline float minimum_slide_travel(const profile& p, const mechanics::rules& rules, const mechanics::state& s) noexcept
	{
		if (p.manual_bolt) return s.bolt.travel*p.slide_stroke;
		if (p.manual_catch && s.action==mechanics::action_state::latched_open) return p.slide_stroke;
		// A non-reciprocating handle is not the bolt. It can return fully forward
		// while the feed remains locked open, and does not inherit shot recoil.
		if (p.motion==action_motion::charging_handle) return 0;
		return s.action == mechanics::action_state::locked_open ||
			(rules.last_round_lock && s.action == mechanics::action_state::held_open &&
			 s.magazine_inserted && s.magazine_rounds == 0) ? p.locked_travel : 0;
	}
	inline constexpr bool native_action_recoil(const profile& p) noexcept
	{ return p.motion==action_motion::reciprocating_slide; }
	inline float attached_magazine_radius(const profile& p) noexcept
	{return p.manual_magazine?p.manual_magazine->grab_radius:p.support_magazine_catch?0.f:p.button_magazine_radius;}
	inline bool attached_magazine_contact(const profile& p,const mechanics::state& s,const geometry& g) noexcept
	{
		return attached_magazine_radius(p)>0 && valid(g.magazine) && s.magazine_inserted &&
			(!p.belt || belt_feed::accessible(s.belt)) && g.magazine.grip_distance<=attached_magazine_radius(p);
	}
	inline bool retained_internal_bolt(const mechanics::rules& rules,const mechanics::state& s) noexcept
	{
		return s.action==mechanics::action_state::locked_open || s.action==mechanics::action_state::cocked_open ||
			s.action==mechanics::action_state::latched_open ||
			(s.action==mechanics::action_state::held_open &&
			 (rules.feed==mechanics::feed_type::open_bolt ||
			  (rules.last_round_lock && s.magazine_inserted && s.magazine_rounds==0)));
	}
	struct slide_constraint
	{
		vec start{};
		float initial_travel{};
		std::uint8_t pose{};
	};
	inline float constrained_slide_travel(const profile& p, const slide_constraint& grip, vec raw_hand) noexcept
	{
		return std::clamp(grip.initial_travel + hands::dot(hands::sub(raw_hand,grip.start),p.slide_axis),0.f,p.slide_stroke);
	}
	inline bool support_available(const controller_input::frame& input, const hold& owner, bool part_busy) noexcept
	{
		if (!owner.can_fire() || part_busy) return false;
		const auto& trigger = input.trigger[1-static_cast<int>(owner.rear)];
		// Do not acquire support over a pinch in the same input frame. Existing
		// support remains exclusive; releasing it still requires a new pinch.
		return owner.support != hand::none || !trigger.active || !trigger.down;
	}
	// Runtime-independent interaction owner. Called only by the simulation
	// adapter, NEVER by a render callback or prediction replay. commit(tx) must
	// compare native ammo, write on its owning thread and return true on success.
	// Only then is mechanical state published. A rejected commit consumes the
	// input edge; it must not become a queued future eject/draw/extraction.
	class controller
	{
	public:
		bool slide_held() const noexcept { return slide_held_; }
		belt_feed::grasp belt_grip()const noexcept{return belt_.current();}
		const char* belt_push_reason()const noexcept{return belt_.push_reason();}
		bool magazine_seated() const noexcept { return magazine_seated_; }
		bool magazine_grabbed() const noexcept { return magazine_grabbed_; }
		bool knife_magazine_grasp() const noexcept { return knife_magazine_grasp_; }
		std::uint8_t magazine_pose() const noexcept {return magazine_pose_;}
		bool knife_slide_grasp() const noexcept { return knife_slide_grasp_; }
		vec magazine_grab_start() const noexcept { return magazine_start_; }
		float slide_travel() const noexcept { return travel_; }
		float handle_amount() const noexcept { return catch_amount_; }
		handle_catch_grip handle_grip() const noexcept { return catch_grip_; }
		slide_constraint slide_grip() const noexcept { return {slide_start_,start_travel_,slide_pose_}; }
		const char* decision() const noexcept { return decision_; }
		bool well_contact() const noexcept { return well_contact_; }
		bool requires_withdrawal() const noexcept { return require_withdrawal_; }
		clock::time_point insertion_after()const noexcept{return insert_after_;}
		const geometry& examined() const noexcept { return examined_; }
		const slap_trace& slap_diagnostics() const noexcept { return slap_trace_; }
		mechanics::effect feedback_effect() const noexcept { return feedback_; }
		void seat_external(const geometry& g,const controller_input::frame& input,const hold& owner,clock::time_point now)noexcept
		{
			resume_input(g,input,owner,now);
			magazine_seated_=true;magazine_grabbed_=false;knife_magazine_grasp_=false;
			magazine_pose_=g.attached_magazine_pose;previous_hand_=g.hand_in_gun;
			previous_tip_=g.magazine_top_in_well;well_contact_=require_withdrawal_=false;
			decision_="recovered magazine inserted";
		}
		void exchange_supply(const profile& p,const geometry& g,const controller_input::frame& input,const hold& owner,clock::time_point now,bool drawn)noexcept
		{
			resume_input(g,input,owner,now);
			magazine_seated_=magazine_grabbed_=knife_magazine_grasp_=well_contact_=false;
			latch_contact_.reset();feedback_=mechanics::effect::none;
			(void)release_.consume(input.secondary[int(rear_)]);(void)fire_.consume(input.trigger[int(rear_)]);
			magazine_pose_=drawn?g.magazine_pose:0;previous_hand_=g.hand_in_gun;previous_tip_=g.magazine_top_in_well;
			require_withdrawal_=drawn && sweep_well(previous_tip_,previous_tip_,p.well_radius,p.well_contact_depth,p.well_capture_below);
			decision_=drawn?"primary supply selected":"primary supply returned for secondary";
		}
		bool offhand_busy(const mechanics::state& s) const noexcept
		{
			return belt_.busy() || slide_held_ || magazine_seated_ || magazine_grabbed_ || s.magazine_hand != hand::none;
		}

		// Call against the OLD instance before switching/reassigning rear hand.
		// Losing tracking closes a held stroke but never undoes a live extraction.
		// A load/checkpoint boundary instead discards this gesture object; do not
		// refund pre-load escrow into newly restored native ammunition.
		template <typename Commit>
		bool interrupt(const mechanics::rules& rules, mechanics::state& s, hand rear, Commit&& commit)
		{
			const bool previous_grasp=knife_magazine_grasp_;
			const auto previous_pose=magazine_pose_;
			reset_gesture();
			if (!valid_hand(rear) || !mechanics::valid(rules, s)) return false;
			const auto apply = [&](mechanics::operation op, hand actor) {
				auto tx = mechanics::plan(rules, s, {op, s.weapon, s.instance_generation, s.revision, rear, actor});
				tx.silent = true;
				if (!tx || !commit(tx)) return false;
				s = tx.next;
				return true;
			};
			if (s.magazine_hand != hand::none && !apply(mechanics::operation::cancel_magazine, s.magazine_hand))
			{knife_magazine_grasp_=previous_grasp;magazine_pose_=previous_pose;return false;}
			return s.action != mechanics::action_state::held_open ||
				apply(mechanics::operation::finish_stroke, static_cast<hand>(1 - static_cast<int>(rear)));
		}

		template <typename Commit>
		void update(const profile& p, const mechanics::rules& rules, mechanics::state& s,
			const controller_input::frame& input, const hold& owner, const geometry& g,
			bool gameplay, clock::time_point now, Commit&& commit, hand_interaction::access access = {})
		{
			const bool manipulation=access.manipulation,acquire_supply=access.supply,acquire_parts=access.acquire;
			feedback_ = mechanics::effect::none;
			// Invalid owner/state must be reconciled by the adapter against the old
			// instance, not by applying an old refund to an unrelated weapon.
			if (!mechanics::valid(rules, s) || !valid_hand(owner.holding_hand()) || owner.weapon != s.weapon)
			{
				decision_ = "invalid owner/state";
				reset_gesture();
				return;
			}
			const auto rear = static_cast<int>(owner.holding_hand()), off = 1 - rear;
			auto trigger = input.trigger[off];if(access.release)trigger.down=false;
			auto grip=input.squeeze[off];if(access.grip_release)grip.down=false;
			const auto& release = input.secondary[rear];
			const bool fresh = gameplay && (!magazine_grabbed_ || rules.release==mechanics::magazine_release::button || p.manual_magazine) &&
				physical_reload::valid(p) && input.focused && input.sequence &&
				(rules.belt_fed==bool(p.belt)) && (!p.belt || belt_feed::finite_contact(g.belt)) &&
				now >= input.sampled_at && now - input.sampled_at <= std::chrono::milliseconds(150) &&
				input.grip[rear].valid && input.aim[rear].valid &&
				g.valid && g.weapon == s.weapon && g.instance_generation == s.instance_generation &&
				g.input_sequence == input.sequence && g.reference_generation == input.reference_generation &&
				finite(g.hand_in_gun) && finite(g.magazine_top_in_well) &&
				std::isfinite(g.seated_hand_distance) && g.seated_hand_distance >= 0 &&
				std::isfinite(g.waist_distance) && g.waist_distance >= 0 &&
				std::isfinite(g.slide_distance) && g.slide_distance >= 0 &&
				std::isfinite(g.insertion_alignment) && std::abs(g.insertion_alignment) <= 1.001f &&
				(!p.manual_magazine || (rules.release==mechanics::magazine_release::physical_pull && valid(g.magazine))) &&
				(rules.manual_catch==bool(p.manual_catch)) && (!p.manual_catch || valid(g.catch_input)) &&
				((rules.feed==mechanics::feed_type::manual_bolt)==bool(p.manual_bolt)) && (!p.manual_bolt || finite(g.bolt_hand));
			if (!fresh)
			{
				decision_ = "stale/inactive tracking or geometry";
				(void)interrupt(rules, s, owner.holding_hand(), commit);
				if (g.diagnose_slap)
				{
					slap_observation observed; observed.sequence=input.sequence; observed.at=input.sampled_at;
					observed.reason=slap_reason::invalid; slap_trace_.record(observed);
				}
				return;
			}
			const bool changed = continuity_.update(input,now) || s.weapon != weapon_ || s.instance_generation != instance_ ||
				owner.holding_hand() != rear_ || owner.rear_revision != rear_revision_ ||
				input.reference_generation != reference_ || input.sequence < sequence_;
			if(s.weapon!=weapon_ || s.instance_generation!=instance_ || now<last_time_)insert_after_={};
			if (changed && !interrupt(rules, s, owner.holding_hand(), commit)) return;
			if (input.sequence == sequence_) return; // both eyes / same frame cannot repeat an operation
			const bool offhand_available = manipulation && input.grip[off].valid && input.aim[off].valid && trigger.active;
			// Losing the other hand cancels only its part lease. Rear-hand buttons
			// keep their edge history, including while that hand holds another gun.
			if (!offhand_available || (g.knife_held && slide_held_ && !knife_slide_grasp_) ||
				trigger.generation!=trigger_.generation || trigger.presses<trigger_.presses)
			{
				const auto release_gate = release_, fire_gate = fire_;
				const bool cleaned = interrupt(rules, s, owner.holding_hand(), commit);
				release_ = release_gate; fire_ = fire_gate;
				if (!cleaned) return;
			}
			weapon_ = s.weapon; instance_ = s.instance_generation;
			rear_ = owner.holding_hand(); rear_revision_ = owner.rear_revision;
			reference_ = input.reference_generation; sequence_ = input.sequence; last_time_ = now;
			examined_ = g; // only a newly consumed input, never a duplicate render sample
			const bool local_press=trigger_.consume(trigger);
			const bool pressed=access.pinch.value_or(local_press);
			const bool local_release=release_.consume(release);
			const bool release_pressed = access.secondary.value_or(local_release) && owner.can_fire();
			const bool fire_pressed = fire_.consume(input.trigger[rear]) && owner.can_fire();
			decision_ = "waiting for fresh trigger press";
			const auto apply_with_receiver = [&](mechanics::operation op, hand actor, hand receiver) {
				auto request=mechanics::request{op,s.weapon,s.instance_generation,s.revision,owner.holding_hand(),actor};
				request.magazine_receiver=receiver;
				request.disposition=!offhand_available ? ammunition::disposition_reason::forced_cleanup :
					g.waist_distance<=p.waist_radius ? ammunition::disposition_reason::waist_return : ammunition::disposition_reason::deliberate_discard;
				request.preserve_discard=access.preserve_discard && (op==mechanics::operation::release_button ||
					op==mechanics::operation::knock_magazine || (op==mechanics::operation::cancel_magazine && offhand_available && !trigger.down &&
					request.disposition==ammunition::disposition_reason::deliberate_discard));
				const auto tx = mechanics::plan(rules, s, request);
				if (!tx || !commit(tx)) return false;
				s = tx.next;
				return true;
			};
			const auto apply = [&](mechanics::operation op, hand actor) {
				return apply_with_receiver(op,actor,hand::none);
			};
			const auto offhand = static_cast<hand>(off);
			const bool magazine_down=trigger.down;
			// Validate the existing seated lease BEFORE a simultaneous rear release.
			// Pulling a button-locked magazine never moves it or changes its payload.
			if(rules.release==mechanics::magazine_release::button && (magazine_grabbed_ || magazine_seated_) &&
				(!magazine_down || !s.magazine_inserted || g.seated_hand_distance>p.part_release_distance ||
				 hands::length(hands::sub(g.hand_in_gun,previous_hand_))>p.max_contact_step))
			{magazine_grabbed_=magazine_seated_=false;}
			const bool prefer_magazine=p.manual_magazine && p.manual_magazine->prefer_grasp_facing && g.magazine_facing;
			const auto grasp_magazine=[&] {
				if(g.attached_magazine_pose>=p.magazine_pose_count){decision_="magazine grip unavailable";return false;}
				magazine_grabbed_=true;magazine_pose_=g.attached_magazine_pose;
				knife_magazine_grasp_=g.knife_held;magazine_start_=previous_hand_=g.hand_in_gun;
				feedback_=mechanics::effect::action_grab;decision_="seated magazine acquired";return true;
			};
			// A centrally granted pinch and rear release may share one tracked
			// sample. Establish that real contact before choosing catch versus drop.
			if(release_pressed && rules.release==mechanics::magazine_release::button && offhand_available &&
				acquire_parts && access.parts && pressed && trigger.down && owner.support!=offhand && !offhand_busy(s) &&
				attached_magazine_contact(p,s,g) && (g.slide_distance>p.slide_radius || prefer_magazine))
				(void)grasp_magazine();
			// The native shot boundary never sees an empty shot. Process only a
			// fresh tracked rear trigger edge here, without native fire/ammo writes.
			// Resolve it before same-frame ejection/insertion; a live press must
			// not turn into a dry press merely because B/Y removes the magazine.
			if (fire_pressed && !slide_held_ && !magazine_grabbed_) (void)apply(mechanics::operation::dry_fire,owner.holding_hand());
			// B/Y acts only once per edge. An empty follower ejects; a loaded or
			// absent magazine permits lock release. Partial hand-held slide wins.
			if(release_pressed && !slide_held_)
			{
				if(p.belt && p.belt->bridge && p.belt->bridge->rear_secondary_release)
				{
					// A real rear-hand command, not a fabricated offhand manipulation.
					// Consume a conflicting/rejected press; never queue it for later.
					const auto held=belt_.current().part;
					if(held!=belt_feed::lease::bridge && held!=belt_feed::lease::bridge_release)
						(void)apply(mechanics::operation::release_bridge,owner.holding_hand());
				}
				else
				{
					const bool support_catch=offhand_available && access.support_catch && p.support_magazine_catch &&
						owner.support==offhand && grip.active && grip.down && trigger.down && !g.knife_held && !offhand_busy(s) &&
						g.seated_hand_distance<=p.part_release_distance && g.attached_magazine_pose<p.magazine_pose_count;
					const bool catch_magazine=offhand_available && s.magazine_hand==hand::none &&
						((magazine_grabbed_ || magazine_seated_) && magazine_down || support_catch);
					if(apply_with_receiver(mechanics::operation::release_button,owner.holding_hand(),catch_magazine?offhand:hand::none) &&
						catch_magazine && s.magazine_hand==offhand)
					{
						if(support_catch){knife_magazine_grasp_=false;magazine_pose_=g.attached_magazine_pose;}
						magazine_grabbed_=magazine_seated_=well_contact_=false;require_withdrawal_=true;
						feedback_=mechanics::effect::none; // The committed take already supplies removal feedback.
						previous_tip_=g.magazine_top_in_well;latch_contact_.reset();
						decision_="button released magazine into hand; withdraw before insertion";
						return;
					}
				}
			}
			if (!offhand_available) { trigger_ = {}; decision_ = "rear controls active; other hand unavailable"; return; }
			if(p.belt)
			{
				const bool available=!slide_held_ && !magazine_seated_ && !magazine_grabbed_ && s.magazine_hand==hand::none && owner.support!=offhand;
				const bool closer=belt_feed::preferred(*p.belt,s.belt,s.magazine_inserted,s.magazine_rounds,g.belt,g.slide_distance,g.magazine.grip_distance);
				if(belt_.update(*p.belt,s.belt,s.magazine_inserted,s.magazine_rounds,g.belt,pressed && closer && acquire_parts && access.parts,trigger.down,available,
					[&](float amount){auto q=mechanics::request{mechanics::operation::move_cover,s.weapon,s.instance_generation,s.revision,owner.holding_hand(),offhand};q.cover=amount;
						const auto tx=mechanics::plan(rules,s,q);if(!tx || !commit(tx))return false;s=tx.next;return true;},
					[&]{return apply(mechanics::operation::lay_belt,offhand);},
					[&](float amount){auto q=mechanics::request{mechanics::operation::move_bridge,s.weapon,s.instance_generation,s.revision,owner.holding_hand(),offhand};q.cover=amount;
						const auto tx=mechanics::plan(rules,s,q);if(!tx || !commit(tx))return false;s=tx.next;return true;},
					input.sampled_at,!g.knife_held && !trigger.down && !pressed && input.squeeze[off].active && !input.squeeze[off].down))
				{decision_="belt feed cover/chain/bridge manipulation";return;}
			}
			if(p.receiver_release)
			{
				// Grip+Trigger is the existing empty-hand fist gesture. An already
				// clenched, unoccupied hand may slap; a new Trigger press still
				// belongs to part acquisition, and Trigger-only pinch is excluded.
				const bool fist=grip.active && grip.down && trigger.down;
				const bool eligible=s.action==mechanics::action_state::locked_open && s.magazine_inserted && s.magazine_rounds>0 &&
					!offhand_busy(s) && !g.knife_held && owner.support!=offhand && (!trigger.down || fist) && !pressed && valid(g.catch_input);
				if(eligible && receiver_slap_.update(p.receiver_release->impact,p.receiver_release->max_speed,g.catch_input,input.sampled_at,p.max_contact_step))
				{
					const bool accepted=apply(mechanics::operation::release_catch,offhand);
					decision_=accepted ? "receiver bolt catch slapped" : "receiver bolt catch transaction rejected";
					travel_=minimum_slide_travel(p,rules,s);
					return;
				}
				if(!eligible)receiver_slap_.reset();
			}
			const bool can_slap=p.manual_catch && s.action==mechanics::action_state::latched_open &&
				!offhand_busy(s) && owner.support!=offhand && !trigger.down && !pressed;
			slap_observation observation;
			if (g.diagnose_slap)
			{
				observation.sequence=input.sequence; observation.at=input.sampled_at;
				observation.reason=!p.manual_catch ? slap_reason::disabled : s.action!=mechanics::action_state::latched_open ? slap_reason::not_latched :
					offhand_busy(s) ? slap_reason::busy : owner.support==offhand ? slap_reason::support : slap_reason::trigger;
			}
			else slap_trace_={};
			if (can_slap)
			{
				if (slap_.update(*p.manual_catch,g.catch_input,input.sampled_at,p.max_contact_step,g.diagnose_slap ? &observation : nullptr))
				{
					const bool accepted=apply(mechanics::operation::slap_action,offhand);
					decision_=accepted ? "handle slapped forward" : "handle slap transaction rejected";
					observation.reason=accepted ? slap_reason::accepted : slap_reason::rejected;
					if (g.diagnose_slap) slap_trace_.record(observation);
					travel_=minimum_slide_travel(p,rules,s);
					catch_amount_=s.action==mechanics::action_state::latched_open ? 1.f : 0.f;
					return;
				}
			}
			else slap_.reset();
			if (g.diagnose_slap) slap_trace_.record(observation);
			if (magazine_grabbed_)
			{
				if(rules.release==mechanics::magazine_release::button)
				{
					previous_hand_=g.hand_in_gun;
					decision_="magazine locked; waiting for release button";
					return;
				}
				const auto delta=hands::sub(g.hand_in_gun,magazine_start_);
				const auto& manual=*p.manual_magazine;
				const auto pull=hands::dot(delta,manual.pull_axis);
				if (!trigger.down || !s.magazine_inserted ||
					hands::length(hands::sub(delta,hands::scale(manual.pull_axis,pull)))>manual.pull_lateral_limit ||
					hands::length(hands::sub(g.hand_in_gun,previous_hand_))>p.max_contact_step ||
					pull<-.02f || pull>manual.pull_travel+p.part_release_distance)
				{
					magazine_grabbed_=false;
					decision_="magazine pull released or tracking separated";
					return;
				}
				previous_hand_=g.hand_in_gun;
				decision_="magazine held; pull outward to unlatch";
				if (pull>=manual.pull_travel)
				{
					const bool taken=apply(mechanics::operation::pull_magazine,offhand);
					magazine_grabbed_=false; // A failed compare consumes this grasp too.
					decision_=taken ? "magazine pulled into hand" : "magazine pull transaction rejected";
					previous_tip_=g.magazine_top_in_well;
					well_contact_=false; require_withdrawal_=taken;
					latch_contact_.reset();
				}
				return;
			}
			if (magazine_seated_)
			{
				magazine_seated_ = magazine_down && s.magazine_inserted && g.seated_hand_distance <= p.part_release_distance;
				previous_hand_=g.hand_in_gun;
				decision_ = magazine_seated_ ? "hand held on seated magazine" : "seated magazine hand released";
				return; // never reacquire another part on a release or breakaway frame
			}
			if (s.magazine_hand != hand::none)
			{
				if (!magazine_down)
				{
					latch_contact_.reset();
					decision_ = g.waist_distance<=p.waist_radius ? "held magazine returned to waist" : "held magazine discarded";
					if (apply(mechanics::operation::cancel_magazine, offhand)) well_contact_=false;
					return;
				}
				if (p.manual_magazine && p.manual_magazine->spare_strike && s.magazine_inserted)
				{
					if (latch_contact_.update(*p.manual_magazine,g.magazine,input.sampled_at,p.max_contact_step))
					{
						const bool knocked=apply(mechanics::operation::knock_magazine,offhand);
						decision_=knocked ? "spare magazine released latch" : "latch transaction rejected";
						well_contact_=false; previous_tip_=g.magazine_top_in_well;
						if (knocked){require_withdrawal_=true;insert_after_=now+std::chrono::milliseconds(300);}
						return; // Never eject and insert on the same contact.
					}
				}
				else latch_contact_.reset();
				const auto tip = g.magazine_top_in_well;
				well_motion motion{previous_tip_,well_contact_,require_withdrawal_};
				const auto contact=advance_well(p,motion,tip,g.insertion_alignment,s.magazine_inserted,now<insert_after_);
				previous_tip_=motion.previous;well_contact_=motion.contact;require_withdrawal_=motion.withdraw;decision_=contact.reason;
				if (contact.insert)
				{
					magazine_seated_ = apply(mechanics::operation::insert_magazine, offhand);
					if(magazine_seated_){magazine_pose_=g.attached_magazine_pose;previous_hand_=g.hand_in_gun;}
					decision_ = magazine_seated_ ? "magazine inserted" : "insert transaction rejected";
					well_contact_ = false;
					require_withdrawal_ = !magazine_seated_;
					previous_tip_ = tip;
					return;
				}
				return;
			}
			well_contact_ = require_withdrawal_ = false;
			latch_contact_.reset();
			if (slide_held_)
			{
				if (p.manual_bolt)
				{
					// A hand lease can end anywhere. Only a qualified closing release may finish the rotation.
					if (!rotating_bolt::permits(*p.manual_bolt,offhand) ||
						hands::length(hands::sub(g.bolt_hand,slide_start_))>p.max_contact_step ||
						g.slide_distance>p.part_release_distance)
					{ slide_held_=false;decision_="manual bolt released; position retained";return; }
					bolt_release_.sample(g.bolt_hand,input.sampled_at);
					if(!trigger.down)
					{
						slide_held_=false;decision_="manual bolt released; position retained";
						// Include the final physical sample in the angle gate; release can
						// arrive on the same input as crossing into the assisted range.
						auto released=s.bolt;
						released.lift=rotating_bolt::project(*p.manual_bolt,s.bolt,slide_start_,g.bolt_hand).lift;
						if(s.bolt.lift>0 && bolt_release_.completes(*p.manual_bolt,released,input.sampled_at))
						{
							const auto tx=mechanics::plan(rules,s,{mechanics::operation::move_bolt,s.weapon,s.instance_generation,
								s.revision,owner.holding_hand(),offhand,{0,0}});
							if(tx && commit(tx)){s=tx.next;decision_="manual bolt release assisted lock";}
							else decision_="manual bolt assisted lock rejected";
						}
						return;
					}
					const auto target=rotating_bolt::project(*p.manual_bolt,s.bolt,slide_start_,g.bolt_hand);
					slide_start_=g.bolt_hand;
					decision_="hand driving manual bolt";
					if (target.lift!=s.bolt.lift || target.travel!=s.bolt.travel)
					{
						const auto tx=mechanics::plan(rules,s,{mechanics::operation::move_bolt,s.weapon,s.instance_generation,
							s.revision,owner.holding_hand(),offhand,target});
						if (!tx || !commit(tx)) {slide_held_=false;decision_="manual bolt transaction rejected; regrip required";return;}
						s=tx.next;
					}
					travel_=s.bolt.travel*p.slide_stroke;
					return;
				}
				decision_ = "slide held; awaiting full stroke/forward return/release";
				if (!trigger.down)
				{
					decision_ = "slide released";
					if (s.action != mechanics::action_state::held_open && s.action!=mechanics::action_state::latched_open &&
						travel_ > minimum_slide_travel(p, rules, s) + p.close_travel)
						feedback_ = mechanics::effect::action_close; // Short spring return, no ammo transaction.
					if (s.action == mechanics::action_state::held_open &&
						!apply(mechanics::operation::finish_stroke, offhand)) return;
					slide_held_ = forward_armed_ = false;
					travel_ = minimum_slide_travel(p, rules, s);
					catch_amount_=s.action==mechanics::action_state::latched_open ? 1.f : 0.f;
					return;
				}
				const auto delta = hands::sub(g.hand_in_gun, slide_start_);
				const auto pull = hands::dot(delta, p.slide_axis);
				if (hands::length(hands::sub(delta, hands::scale(p.slide_axis, pull))) > p.slide_lateral_limit ||
					std::abs(start_travel_+pull-std::clamp(start_travel_+pull,0.f,p.slide_stroke)) > p.part_release_distance ||
					hands::length(hands::sub(g.hand_in_gun, previous_hand_)) > p.max_contact_step)
				{
					decision_ = "slide lateral separation/teleport";
					(void)interrupt(rules, s, owner.holding_hand(), commit);
					return;
				}
				previous_hand_ = g.hand_in_gun;
				travel_ = constrained_slide_travel(p,slide_grip(),g.hand_in_gun);
				if (p.manual_catch)
					catch_amount_=catch_amount(*p.manual_catch,catch_grip_,delta,hands::scale(p.slide_axis,-1),g.catch_input.rotation);
				if (travel_ >= p.full_stroke)
				{
					if (s.action != mechanics::action_state::held_open && s.action!=mechanics::action_state::latched_open &&
						!apply(mechanics::operation::extract_chamber, offhand))
					{
						(void)interrupt(rules, s, owner.holding_hand(), commit); // compare failure is not a deferred extraction
						return;
					}
					forward_armed_ = true;
				}
				if (p.manual_catch)
				{
					if (s.action==mechanics::action_state::latched_open && catch_amount_<=p.manual_catch->disengage)
					{
						if (!apply(mechanics::operation::unlatch_action,offhand))
						{ decision_="handle unlatch transaction rejected"; (void)interrupt(rules,s,owner.holding_hand(),commit); return; }
						forward_armed_=true;
					}
					else if (s.action==mechanics::action_state::held_open && travel_>=p.full_stroke && catch_amount_>=p.manual_catch->engage)
					{
						if (!apply(mechanics::operation::latch_action,offhand))
						{ decision_="handle latch transaction rejected"; (void)interrupt(rules,s,owner.holding_hand(),commit); return; }
					}
				}
				if (travel_ <= std::max(p.close_travel, minimum_slide_travel(p, rules, s)) &&
					s.action == mechanics::action_state::held_open && forward_armed_)
				{
					// Keep the original grip/locked-travel offset through every cycle.
					// A rejected forward commit consumes this stroke: only another full
					// pull can arm a held retry. Do not interrupt and retry feeding here.
					forward_armed_ = false;
					decision_ = apply(mechanics::operation::finish_stroke, offhand) ?
						"slide returned forward" : "forward transaction rejected";
				}
				travel_ = std::max(travel_, minimum_slide_travel(p, rules, s));
				return;
			}
			travel_ = minimum_slide_travel(p, rules, s);
			catch_amount_=s.action==mechanics::action_state::latched_open ? 1.f : 0.f;
			if (!pressed || !trigger.down) return;
			if(!acquire_parts){decision_="new pinch reserved by another interaction";return;}
			// A squeezed controller is not itself a support lease. Keep a real
			// support owner exclusive, but permit an otherwise free hand to pinch
			// a part while also squeezing. Acquisition still requires a NEW press.
			if (owner.support == offhand)
			{ decision_ = "offhand support blocks acquisition"; return; }
			decision_ = "trigger outside slide/waist regions";
			const bool magazine_candidate=access.parts && (rules.release==mechanics::magazine_release::button || p.manual_magazine) && attached_magazine_contact(p,s,g);
			// A deliberate press over the gun takes precedence over an overlapping
			// waist sphere. Hold-while-entering never acquires either interaction.
			if (access.parts && (!g.knife_held || p.knife_slide_pose_count) && g.slide_distance <= p.slide_radius && !(magazine_candidate && prefer_magazine))
			{
				if (p.manual_bolt && !rotating_bolt::permits(*p.manual_bolt,offhand))
				{decision_="manual bolt hand unavailable";return;}
				if (g.slide_pose >= (g.knife_held ? p.knife_slide_pose_count : p.slide_pose_count))
				{ decision_ = "slide grip pose unavailable"; return; }
				decision_ = "slide acquired";
				feedback_ = mechanics::effect::action_grab;
				slide_held_ = true;
				knife_slide_grasp_=g.knife_held;
				forward_armed_ = false;
				slide_start_ = previous_hand_ = g.hand_in_gun;
				if (p.manual_bolt) {slide_start_=g.bolt_hand;bolt_release_.begin(g.bolt_hand,input.sampled_at);}
				start_travel_ = travel_;
				slide_pose_ = g.slide_pose;
				catch_grip_={g.catch_input.rotation,catch_amount_};
			}
			else if (magazine_candidate)
			{
				(void)grasp_magazine();
			}
			else if (g.waist_distance <= p.waist_radius)
			{
				if(g.magazine_pose>=p.magazine_pose_count){decision_="magazine grip unavailable";return;}
				if(!acquire_supply){decision_="waist supply reserved by secondary module";return;}
				const bool drawn = apply(mechanics::operation::draw_magazine, offhand);
				if (drawn){knife_magazine_grasp_=g.knife_held;magazine_pose_=g.magazine_pose;}
				decision_ = drawn ? "magazine drawn" : "draw transaction rejected";
				previous_tip_ = g.magazine_top_in_well;
				// A magazine spawned already intersecting the well must leave once;
				// ordinary waist draws need no invented below-only approach gesture.
				require_withdrawal_ = drawn && sweep_well(previous_tip_,previous_tip_,p.well_radius,
					p.well_contact_depth,p.well_capture_below);
				well_contact_ = false;
			}
		}

	private:
		void resume_input(const geometry& g,const controller_input::frame& input,const hold& owner,clock::time_point now)noexcept
		{
			weapon_=owner.weapon;instance_=g.instance_generation;rear_=owner.holding_hand();rear_revision_=owner.rear_revision;
			reference_=input.reference_generation;sequence_=input.sequence;last_time_=now;(void)continuity_.update(input,now);
			trigger_={};(void)trigger_.consume(input.trigger[1-int(rear_)]);
		}
		void reset_gesture() noexcept
		{
			belt_.reset();bolt_release_={};
			knife_magazine_grasp_=false;
			magazine_pose_=0;
			knife_slide_grasp_=false;
			trigger_ = {}; release_ = {}; fire_ = {}; sequence_ = 0;
			examined_ = {};
			feedback_ = mechanics::effect::none;
			slide_pose_ = 0;
			latch_contact_.reset();
			slap_.reset();receiver_slap_.reset(); slap_trace_={}; catch_grip_={}; catch_amount_=0;
			slide_held_ = forward_armed_ = magazine_seated_ = magazine_grabbed_ = require_withdrawal_ = well_contact_ = false; travel_ = start_travel_ = 0;
		}
		controller_input::digital_press_gate trigger_{}, release_{}, fire_{};
		std::uint32_t weapon_{};
		std::uint64_t instance_{}, reference_{}, sequence_{}, rear_revision_{};
		hand rear_{hand::none};
		clock::time_point last_time_{},insert_after_{};
		controller_input::consumer_continuity continuity_;
		bool slide_held_{}, forward_armed_{}, magazine_seated_{}, magazine_grabbed_{}, require_withdrawal_{}, well_contact_{};
		bool knife_magazine_grasp_{}; // Frozen until release: returning the knife cannot move a held magazine.
		std::uint8_t magazine_pose_{};
		bool knife_slide_grasp_{}; // Likewise, returning the knife cannot reselect the held slide style.
		magazine_latch_contacts latch_contact_{};
		handle_slap slap_{};
		receiver_paddle_slap receiver_slap_{};
		slap_trace slap_trace_{};
		handle_catch_grip catch_grip_{};
		float catch_amount_{};
		vec magazine_start_{};
		float travel_{}, start_travel_{};
		std::uint8_t slide_pose_{};
		vec slide_start_{}, previous_hand_{}, previous_tip_{};
		const char* decision_{"not sampled"};
		mechanics::effect feedback_{};
		geometry examined_{}; // observation only; never consulted by gesture rules
		belt_feed::controller belt_{};
		rotating_bolt::release_motion bolt_release_{};
	};
}
