#pragma once

#include "weapon_holding.hpp"
#include "closed_bolt.hpp"
#include "open_bolt.hpp"
#include "belt_feed.hpp"
#include "manual_bolt.hpp"
#include "ammunition_transfer.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace vr::gameplay::weapons::mechanics
{
	// Shared detachable-container ownership/transactions. Open-bolt action and
	// belt access are independent policies; cylinders and tubes keep separate ledgers.
	enum class feed_type { closed_bolt, open_bolt, manual_bolt };
	enum class magazine_release
	{
		button,
		physical_pull
	};
	using weapons::action_state;
	struct rules
	{
		int magazine_capacity{};
		magazine_release release{magazine_release::button};
		bool last_round_lock{}, release_control{}, plus_one{};
		feed_type feed{feed_type::closed_bolt};
		bool manual_catch{};
		bool belt_fed{};
		bool belt_bridge{};
		bool physical_catch_release{}; // Dedicated receiver contact, independent of the controller release button.
		bool discard_penalty{};
	};
	struct state
	{
		std::uint32_t weapon{};
		std::uint64_t instance_generation{}, revision{};
		bool magazine_inserted{}, chamber_loaded{};
		int magazine_rounds{}, reserve_rounds{}, held_rounds{};
		hand magazine_hand{hand::none};
		action_state action{action_state::closed};
		manual_bolt::state bolt{};
		belt_feed::state belt{};
	};
	using ammo_projection = ammunition::projection;
	inline constexpr closed_bolt::rules chamber_rules(const rules& r) noexcept
	{
		return r.feed==feed_type::closed_bolt ? closed_bolt::rules{r.magazine_capacity,r.last_round_lock,r.plus_one,r.manual_catch} : closed_bolt::rules{};
	}
	inline closed_bolt::state chamber_state(const state& s) noexcept
	{
		return {s.magazine_inserted, s.chamber_loaded, s.magazine_rounds, s.action};
	}
	inline void set_chamber(state& s, const closed_bolt::state& feed) noexcept
	{
		s.magazine_inserted = feed.magazine_inserted;
		s.chamber_loaded = feed.chamber_loaded;
		s.magazine_rounds = feed.magazine_rounds;
		s.action = feed.action;
	}
	inline open_bolt::state open_state(const state& s) noexcept
	{ return {s.magazine_inserted,s.magazine_rounds,s.action}; }
	template <typename Transition>
	inline bool transition_feed(const rules& r,state& s,Transition&& transition) noexcept
	{
		if (r.feed==feed_type::open_bolt)
		{
			auto feed=open_state(s);
			if (!transition(open_bolt::rules{r.magazine_capacity},feed)) return false;
			s.magazine_inserted=feed.magazine_inserted; s.magazine_rounds=feed.magazine_rounds;
			s.chamber_loaded=false; s.action=feed.action;
			return true;
		}
		if (r.feed!=feed_type::closed_bolt) return false;
		auto feed=chamber_state(s);
		if (!transition(chamber_rules(r),feed)) return false;
		set_chamber(s,feed);
		return true;
	}
	inline ammo_projection native_ammo(const state& s) noexcept
	{
		return {s.magazine_rounds + int(s.chamber_loaded) + int(s.bolt.feeding), s.reserve_rounds};
	}
	inline std::int64_t total_rounds(const state& s) noexcept
	{
		return std::int64_t(s.magazine_rounds) + (s.chamber_loaded ? 1 : 0) + s.reserve_rounds +
			   s.held_rounds + int(s.bolt.feeding);
	}
	inline bool valid(const rules& r, const state& s) noexcept
	{
		// Bounded integer domain; intermediate conservation sums use int64_t.
		const bool manual_valid=r.magazine_capacity>0 && r.magazine_capacity<=1000 &&
			s.magazine_rounds>=0 && s.magazine_rounds<=r.magazine_capacity && (s.magazine_inserted || !s.magazine_rounds) &&
			(r.plus_one || s.magazine_rounds+int(s.chamber_loaded)+int(s.bolt.feeding)<=r.magazine_capacity) &&
			!r.last_round_lock && !r.release_control && !r.manual_catch && manual_bolt::valid(s.bolt,s.chamber_loaded) &&
			s.action==(manual_bolt::locked(s.bolt) ? action_state::closed : action_state::manual_unlocked);
		const bool feed_valid=r.feed==feed_type::manual_bolt ? manual_valid : r.feed==feed_type::open_bolt ?
			(!s.chamber_loaded && !r.last_round_lock && !r.release_control && !r.plus_one && !r.manual_catch &&
			 open_bolt::valid({r.magazine_capacity},open_state(s))) :
			r.feed==feed_type::closed_bolt && closed_bolt::valid(chamber_rules(r),chamber_state(s));
		return feed_valid && (r.belt_fed ? r.feed==feed_type::open_bolt && r.release==magazine_release::physical_pull && belt_feed::valid(s.belt,s.magazine_inserted,s.magazine_rounds,r.belt_bridge) : !r.belt_bridge && s.belt.cover==0 && !s.belt.laid && s.belt.bridge==0) && (r.feed==feed_type::manual_bolt || (!s.bolt.spent_case && !s.bolt.feeding &&
			!s.bolt.feed_armed && s.bolt.lift==0 && s.bolt.travel==0)) &&
			   (r.release == magazine_release::button || r.release == magazine_release::physical_pull) &&
			   s.weapon != 0 && s.instance_generation != 0 && s.revision != 0 &&
			   s.reserve_rounds >= 0 && s.held_rounds >= 0 && s.held_rounds <= r.magazine_capacity &&
			   (s.magazine_hand == hand::none ? s.held_rounds == 0 : valid_hand(s.magazine_hand)) &&
			   (s.action != action_state::held_open || s.magazine_hand == hand::none) &&
			   total_rounds(s) <= std::numeric_limits<int>::max();
	}
	inline bool ready(const rules& r, const state& s, bool action_hand_busy = false) noexcept
	{
		return valid(r,s) && (!r.belt_fed || belt_feed::ready(s.belt)) && (r.feed==feed_type::manual_bolt ? (!action_hand_busy && manual_bolt::ready(s.bolt,s.chamber_loaded)) : r.feed==feed_type::open_bolt ?
			open_bolt::ready({r.magazine_capacity},open_state(s),action_hand_busy) :
			closed_bolt::ready(chamber_rules(r),chamber_state(s),action_hand_busy));
	}
	inline bool from_native_automatic(const rules& r,int loaded,state& output) noexcept
	{
		auto next=output;
		if(r.belt_fed)next.belt={0,loaded>0};
		if (r.feed==feed_type::manual_bolt)
		{
			if (loaded<0 || loaded>r.magazine_capacity+int(r.plus_one)) return false;
			next.magazine_inserted=true;next.chamber_loaded=loaded>0;next.magazine_rounds=std::max(0,loaded-1);
			next.action=action_state::closed;next.bolt={};
			if (!valid(r,next)) return false;
			output=next;return true;
		}
		if (!transition_feed(r,next,[&](const auto& policy,auto& feed) { return from_native_automatic(policy,loaded,feed); }) ||
			!valid(r,next)) return false;
		output=next;
		return true;
	}
	enum class operation
	{
		release_button, // Feedable/no-magazine lock release; empty magazine ejects instead.
		pull_magazine,
		draw_magazine,
		insert_magazine,
		cancel_magazine,
		cycle_action, // Only a validated full rearward stroke AND release can request this.
		accepted_shot, // Verified native shot consumption, never a trigger edge.
		extract_chamber, // Full stroke: closed-bolt extraction or open-bolt cocking; policy decides ammunition movement.
		finish_stroke, // Release after full stroke; feed/follower lock or cocked-open sear.
		knock_magazine, // Held spare contacts a physical latch; spare ownership is retained.
		dry_fire, // Deliberate empty trigger press, distinct from native shot consumption.
		latch_action,
		unlatch_action,
		slap_action,
		move_bolt,
		move_cover, lay_belt, move_bridge,
		release_catch, // Offhand receiver paddle; never falls through to magazine ejection.
		release_bridge, // Main-grip control opens an authored optic bridge.
		insert_external_magazine, // An independently held, compatible container enters this feed.
		quick_load // Waist supply goes directly into an absent magazine; no action movement.
	};
	enum class effect
	{
		none,
		magazine_out,
		magazine_draw,
		magazine_in,
		magazine_cancel,
		action_close,
		shot,
		action_rear,
		action_grab,
		magazine_take, // Physically removed into the offhand, not a cosmetic drop.
		dry_fire,
		action_latch,
		action_unlatch,
		bolt_unlock, bolt_lock, case_eject, live_eject, bolt_feed,
		cover_open, cover_close, belt_laid, bridge_open, bridge_close,
		count
	};
	enum class rejection
	{
		none,
		invalid_state,
		stale_identity,
		wrong_hand,
		precondition,
		unsupported,
		overflow
	};
	struct request
	{
		operation op{};
		std::uint32_t weapon{};
		std::uint64_t instance_generation{}, revision{};
		hand rear{hand::none}, actor{hand::none};
		manual_bolt::target bolt{};
		float cover{};
		hand magazine_receiver{hand::none}; // Validated offhand catch on a rear release-button edge.
		bool preserve_discard{}; // Adapter reserved a world-item slot before the native commit.
		int external_rounds{};
		ammunition::disposition_reason disposition{ammunition::disposition_reason::forced_cleanup};
		bool sustained{}; // Accepted shot with native player_sustainAmmo; no feed/resource change.
	};
	struct transaction
	{
		rejection error{rejection::invalid_state};
		state next{};
		ammo_projection before{}, after{};
		effect feedback{effect::none};
		bool silent{}; // Lifecycle cleanup preserves mechanics without player feedback.
		int rounds_spent{};
		bool item_released{};
		int rounds_to_item{},rounds_from_item{};
		explicit operator bool() const noexcept
		{
			return error == rejection::none;
		}
	};
	inline transaction plan(const rules& r, const state& current, const request& req) noexcept
	{
		transaction out;
		if (!valid(r, current))
			return out;
		out.error = rejection::stale_identity;
		if (req.weapon != current.weapon || req.instance_generation != current.instance_generation ||
			req.revision != current.revision)
			return out;
		out.error = rejection::wrong_hand;
		if (!valid_hand(req.rear) || !valid_hand(req.actor))
			return out;
		const bool rear_action = req.op == operation::release_button || req.op == operation::accepted_shot || req.op == operation::dry_fire || req.op==operation::release_bridge || req.op==operation::quick_load;
		if (rear_action ? req.actor != req.rear : req.actor == req.rear)
			return out;
		if (current.magazine_hand == req.rear)
			return out;
		if (req.magazine_receiver!=hand::none && (req.op!=operation::release_button ||
			!valid_hand(req.magazine_receiver) || req.magazine_receiver==req.rear || current.magazine_hand!=hand::none))
			return out;
		out.error = rejection::overflow;
		if (current.revision == std::numeric_limits<std::uint64_t>::max())
			return out;
		auto next = current;
		out.error = rejection::precondition;
		const auto take_magazine = [&](hand receiver) {
			next.held_rounds=next.magazine_rounds;
			next.magazine_rounds=0;next.magazine_inserted=false;next.magazine_hand=receiver;
			if(r.belt_fed)belt_feed::remove_box(next.belt);
			out.feedback=effect::magazine_take;
		};
		const auto eject_magazine = [&] {
			if(req.preserve_discard){out.item_released=true;out.rounds_to_item=next.magazine_rounds;}
			else
			{
				const auto disposed=ammunition::dispose(next.magazine_rounds,ammunition::disposition_reason::deliberate_discard,r.discard_penalty);
				next.reserve_rounds+=disposed.returned;out.rounds_spent+=disposed.lost;
			}
			next.magazine_rounds = 0;
			next.magazine_inserted = false;
			out.feedback = effect::magazine_out;
		};
		switch (req.op)
		{
		case operation::move_cover:
			if(!r.belt_fed || next.magazine_hand!=hand::none || next.action==action_state::held_open || !belt_feed::move_cover(next.belt,req.cover,r.belt_bridge))return out;
			if(current.belt.cover==0 && next.belt.cover>0)out.feedback=effect::cover_open;
			else if(next.belt.cover==0)out.feedback=effect::cover_close;
			break;
		case operation::lay_belt:
			if(!r.belt_fed || next.magazine_hand!=hand::none || next.action==action_state::held_open || !belt_feed::lay(next.belt,next.magazine_inserted,next.magazine_rounds))return out;
			out.feedback=effect::belt_laid;break;
		case operation::move_bridge:
		case operation::release_bridge:
			if(!r.belt_bridge || next.magazine_hand!=hand::none || next.action==action_state::held_open ||
				!belt_feed::move_bridge(next.belt,req.op==operation::release_bridge?1.f:req.cover))return out;
			if(current.belt.bridge==0 && next.belt.bridge>0)out.feedback=effect::bridge_open;
			else if(next.belt.bridge==0)out.feedback=effect::bridge_close;
			break;
		case operation::release_catch:
		{
			if(r.feed!=feed_type::closed_bolt || (!r.release_control && !r.physical_catch_release) || next.action!=action_state::locked_open ||
				!next.magazine_inserted || next.magazine_rounds<=0 || next.magazine_hand!=hand::none)return out;
			auto feed=chamber_state(next);
			if(!closed_bolt::release(chamber_rules(r),feed))return out;
			set_chamber(next,feed);out.feedback=effect::action_close;
			break;
		}
		case operation::release_button:
			if (next.action == action_state::held_open) return out;
			if (next.action == action_state::locked_open && r.release_control &&
				(!next.magazine_inserted || next.magazine_rounds > 0))
			{
				auto feed = chamber_state(next);
				if (!closed_bolt::release(chamber_rules(r), feed)) return out;
				set_chamber(next, feed);
				out.feedback = effect::action_close;
			}
			else
			{
				if (r.release != magazine_release::button)
				{
					out.error = rejection::unsupported;
					return out;
				}
				if (!next.magazine_inserted)
					return out;
				if(req.magazine_receiver!=hand::none)take_magazine(req.magazine_receiver);
				else eject_magazine();
			}
			break;
		case operation::pull_magazine:
			if(r.belt_fed && !belt_feed::accessible(next.belt))return out;
			if (r.release != magazine_release::physical_pull)
			{
				out.error = rejection::unsupported;
				return out;
			}
			if (!next.magazine_inserted || next.magazine_hand != hand::none ||
				next.action == action_state::held_open)
				return out;
			// Escrow the actual old magazine, including an empty one. A later
			// release settles its rounds once; putting it back never tops it up.
			take_magazine(req.actor);
			break;
		case operation::knock_magazine:
			if (r.release != magazine_release::physical_pull)
			{
				out.error = rejection::unsupported;
				return out;
			}
			if (!next.magazine_inserted || next.magazine_hand != req.actor ||
				next.action == action_state::held_open) return out;
			eject_magazine();
			break;
		case operation::draw_magazine:
			if (next.magazine_hand != hand::none || next.reserve_rounds == 0 ||
				next.action == action_state::held_open)
				return out;
			next.held_rounds = std::min(r.magazine_capacity, next.reserve_rounds);
			next.reserve_rounds -= next.held_rounds;
			next.magazine_hand = req.actor;
			out.feedback = effect::magazine_draw;
			break;
		case operation::quick_load:
		{
			if(next.magazine_inserted || next.reserve_rounds<=0 || r.belt_fed || r.feed==feed_type::manual_bolt)return out;
			const int capacity=r.magazine_capacity-int(!r.plus_one && next.chamber_loaded);
			const int rounds=std::min(capacity,next.reserve_rounds);
			if(rounds<=0)return out;
			next.magazine_inserted=true;next.magazine_rounds=rounds;next.reserve_rounds-=rounds;
			// Preserve the chamber, lock/cocked state and any independently held spare.
			out.feedback=effect::magazine_in;break;
		}
		case operation::insert_magazine:
		case operation::insert_external_magazine:
			if(r.belt_fed && !belt_feed::accessible(next.belt))return out;
			if(next.magazine_inserted)return out;
			if(req.op==operation::insert_external_magazine)
			{
				if(next.magazine_hand!=hand::none || req.external_rounds<0 || req.external_rounds>r.magazine_capacity)return out;
				next.magazine_rounds=req.external_rounds;out.rounds_from_item=req.external_rounds;
			}
			else
			{
				if(next.magazine_hand!=req.actor)return out;
				next.magazine_rounds=next.held_rounds;
			}
			if (!r.plus_one && next.chamber_loaded && next.magazine_rounds == r.magazine_capacity)
			{
				--next.magazine_rounds;
				++next.reserve_rounds;
			}
			next.magazine_inserted = true;
			next.magazine_hand = hand::none;
			next.held_rounds = 0;
			// Latch does NOT chamber or cock/release an action. Presentation
			// seating is separate and may not move the rear hand or whole weapon.
			out.feedback = effect::magazine_in;
			break;
		case operation::cancel_magazine:
			if (next.magazine_hand != req.actor)
				return out;
			if(req.preserve_discard){out.item_released=true;out.rounds_to_item=next.held_rounds;}
			else
			{
				const auto disposed=ammunition::dispose(next.held_rounds,req.disposition,r.discard_penalty);
				next.reserve_rounds+=disposed.returned;out.rounds_spent+=disposed.lost;
			}
			next.held_rounds = 0;
			next.magazine_hand = hand::none;
			out.feedback = effect::magazine_cancel;
			out.silent=req.disposition==ammunition::disposition_reason::waist_return;
			break;
		case operation::cycle_action:
		{
			if (next.magazine_hand != hand::none)
				return out;
			// Only closed-bolt live extraction spends a round. No recovered round
			// object is created; open-bolt cocking and partial pulls spend none.
			int extracted{};
			if (!transition_feed(r,next,[&](const auto& policy,auto& feed) { return cycle(policy,feed,extracted); })) return out;
			out.rounds_spent = extracted;
			out.feedback = effect::action_close;
			break;
		}
		case operation::accepted_shot:
		{
			if(req.sustained)
			{if(!ready(r,next))return out;out.feedback=effect::shot;break;}
			if(r.belt_fed && !belt_feed::ready(next.belt))return out;
			if (r.feed==feed_type::manual_bolt)
			{ if (!manual_bolt::fire(next.bolt,next.chamber_loaded)) return out; }
			else if (!transition_feed(r,next,[](const auto& policy,auto& feed) { return fire(policy,feed); })) return out;
			out.rounds_spent = 1;
			if(r.belt_fed && !next.magazine_rounds)next.belt.laid=false;
			out.feedback = effect::shot;
			break;
		}
		case operation::dry_fire:
		{
			if (r.feed==feed_type::manual_bolt)
			{
				if (!manual_bolt::locked(next.bolt) || !next.bolt.cocked || next.chamber_loaded) return out;
				next.bolt.cocked=false;out.feedback=effect::dry_fire;break;
			}
			if (r.feed!=feed_type::open_bolt) { out.error=rejection::unsupported; return out; }
			auto feed=open_state(next);
			if(r.belt_fed)
			{
				if(!next.belt.laid)feed.magazine_rounds=0;
			}
			if (!open_bolt::dry_fire({r.magazine_capacity},feed)) return out;
			next.action=feed.action;
			out.feedback=effect::dry_fire;
			break;
		}
		case operation::extract_chamber:
		case operation::finish_stroke:
		{
			if (next.magazine_hand != hand::none) return out;
			if (req.op == operation::extract_chamber)
			{
				if (!transition_feed(r,next,[&](const auto& policy,auto& feed) { return extract(policy,feed,out.rounds_spent); })) return out;
			}
			else if (!transition_feed(r,next,[](const auto& policy,auto& feed) { return finish_stroke(policy,feed); })) return out;
			out.feedback = req.op == operation::extract_chamber ? effect::action_rear : effect::action_close;
			break;
		}
		case operation::move_bolt:
		{
			if (r.feed!=feed_type::manual_bolt || next.magazine_hand!=hand::none) return out;
			manual_bolt::event event;
			if (!manual_bolt::move(next.bolt,next.chamber_loaded,next.magazine_rounds,next.magazine_inserted,
				req.bolt,event,out.rounds_spent)) return out;
			next.action=manual_bolt::locked(next.bolt) ? action_state::closed : action_state::manual_unlocked;
			switch (event)
			{
			case manual_bolt::event::unlock: out.feedback=effect::bolt_unlock;break;
			case manual_bolt::event::lock: out.feedback=effect::bolt_lock;break;
			case manual_bolt::event::eject_case: out.feedback=effect::case_eject;break;
			case manual_bolt::event::eject_live: out.feedback=effect::live_eject;break;
			case manual_bolt::event::feed: out.feedback=effect::bolt_feed;break;
			case manual_bolt::event::close: out.feedback=effect::action_close;break;
			default: break;
			}
			break;
		}
		case operation::latch_action:
		case operation::unlatch_action:
		case operation::slap_action:
		{
			if (r.feed!=feed_type::closed_bolt || !r.manual_catch)
			{ out.error=rejection::unsupported; return out; }
			if (next.magazine_hand!=hand::none) return out;
			auto feed=chamber_state(next);
			const bool changed=req.op==operation::latch_action ? closed_bolt::latch(chamber_rules(r),feed) :
				req.op==operation::unlatch_action ? closed_bolt::unlatch(chamber_rules(r),feed) :
				closed_bolt::slap_release(chamber_rules(r),feed);
			if (!changed) return out;
			set_chamber(next,feed);
			out.feedback=req.op==operation::latch_action ? effect::action_latch :
				req.op==operation::unlatch_action ? effect::action_unlatch : effect::action_close;
			break;
		}
		default:
			out.error = rejection::unsupported;
			return out;
		}
		++next.revision;
		if (!valid(r, next) || total_rounds(current)+out.rounds_from_item != total_rounds(next)+out.rounds_spent+out.rounds_to_item)
		{
			out.error = rejection::invalid_state;
			return out;
		}
		out.error = rejection::none;
		out.next = next;
		out.before = native_ammo(current);
		out.after = native_ammo(next);
		return out;
	}
	// This file only PLANS transactions. The adapter must compare native before,
	// commit on its owning simulation thread, and publish next only on success.
	// Do not assign next from a render hook, command poll, or prediction replay.
} // namespace vr::gameplay::weapons::mechanics
