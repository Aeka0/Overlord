#pragma once
#include "ammunition_transfer.hpp"
#include "weapon_holding.hpp"
#include <algorithm>
#include <limits>

namespace vr::gameplay::weapons::cylinder
{
	enum class action { closed, opening, open, closing };
	struct rules { int capacity{}; bool discard_penalty{}; };
	struct state
	{
		std::uint32_t weapon{};
		std::uint64_t instance_generation{}, revision{};
		int live{}, spent{}, reserve{}, held_rounds{};
		hand loader_hand{hand::none}; // Empty used loader remains an owned object.
		action phase{action::closed};
	};
	inline ammunition::projection native_ammo(const state& s) noexcept { return {s.live,s.reserve}; }
	inline std::int64_t total_rounds(const state& s) noexcept
	{ return std::int64_t(s.live)+s.reserve+s.held_rounds; }
	inline bool valid(rules r, const state& s) noexcept
	{
		return r.capacity > 0 && r.capacity <= 64 && s.weapon && s.instance_generation && s.revision &&
			s.live >= 0 && s.live <= r.capacity && s.spent >= 0 && s.spent <= r.capacity-s.live &&
			s.reserve >= 0 && s.held_rounds >= 0 && s.held_rounds <= r.capacity &&
			(s.loader_hand == hand::none ? s.held_rounds == 0 : valid_hand(s.loader_hand)) &&
			(s.phase == action::closed || s.phase == action::opening || s.phase == action::open || s.phase == action::closing) &&
			total_rounds(s) <= std::numeric_limits<int>::max();
	}
	inline bool ready(rules r, const state& s) noexcept
	{ return valid(r,s) && s.phase == action::closed && s.live > 0; }
	inline bool empty(const state& s) noexcept { return s.live == 0 && s.spent == 0; }
	// Native counts cannot distinguish a partial fresh load from a fired full
	// cylinder. Initial admission conservatively imports missing rounds as cases;
	// one deliberate gravity clear establishes a known physical empty state.
	inline state import_native(rules r, std::uint32_t weapon, std::uint64_t generation,
		ammunition::projection observed) noexcept
	{
		state result{weapon,generation,1,observed.loaded,r.capacity-observed.loaded,observed.reserve};
		return valid(r,result) ? result : state{};
	}
	enum class operation { open, opened, clear, draw, fill, discard, cleanup, close, closed, accepted_shot, fill_external, quick_load };
	enum class effect { none, open, clear, draw, fill, discard, close, shot };
	enum class rejection { none, invalid_state, stale_identity, wrong_hand, precondition, overflow };
	struct request
	{
		operation op{};
		std::uint32_t weapon{};
		std::uint64_t instance_generation{}, revision{};
		hand rear{hand::none}, actor{hand::none};
		bool preserve_discard{};
		int external_rounds{};
		ammunition::disposition_reason disposition{ammunition::disposition_reason::deliberate_discard};
		bool sustained{};
	};
	struct transaction
	{
		rejection error{rejection::invalid_state};
		state next{};
		ammunition::projection before{}, after{};
		effect feedback{};
		int rounds_spent{}, cleared_live{}, cleared_spent{};
		bool silent{};
		bool item_released{};
		int rounds_to_item{},rounds_from_item{};
		explicit operator bool() const noexcept { return error == rejection::none; }
	};
	inline transaction plan(rules r, const state& current, request req) noexcept
	{
		transaction out;
		if (!valid(r,current)) return out;
		out.error = rejection::stale_identity;
		if (req.weapon != current.weapon || req.instance_generation != current.instance_generation ||
			req.revision != current.revision) return out;
		out.error = rejection::wrong_hand;
		if (!valid_hand(req.rear) || !valid_hand(req.actor) || current.loader_hand == req.rear) return out;
		const bool off = req.op == operation::draw || req.op == operation::fill ||
			req.op == operation::discard || req.op == operation::cleanup || req.op==operation::fill_external;
		if (off ? req.actor == req.rear : req.actor != req.rear) return out;
		out.error = rejection::overflow;
		if (current.revision == UINT64_MAX) return out;
		out.error = rejection::precondition;
		auto next = current;
		switch (req.op)
		{
		case operation::open:
			if (next.phase != action::closed) return out;
			next.phase = action::opening; out.feedback = effect::open; break;
		case operation::opened:
			if (next.phase != action::opening) return out;
			next.phase = action::open; out.silent = true; break;
		case operation::clear:
			if (next.phase != action::open || empty(next)) return out;
			out.cleared_live = next.live; out.cleared_spent = next.spent;
			{
				const auto disposed=ammunition::dispose(next.live,ammunition::disposition_reason::deliberate_discard,r.discard_penalty);
				next.reserve+=disposed.returned;out.rounds_spent+=disposed.lost;
			}
			next.live = next.spent = 0; out.feedback = effect::clear; break;
		case operation::draw:
			if (next.loader_hand != hand::none || !next.reserve) return out;
			next.loader_hand = req.actor; next.held_rounds = std::min(r.capacity,next.reserve);
			next.reserve -= next.held_rounds; out.feedback = effect::draw; break;
		case operation::fill:
			if (next.phase != action::open || !empty(next) || next.loader_hand != req.actor || !next.held_rounds) return out;
			next.live = next.held_rounds; next.held_rounds = 0;
			out.feedback = effect::fill; break; // Keep empty loader; no withdrawal state.
		case operation::quick_load:
			if(next.phase!=action::open || !empty(next) || next.reserve<=0)return out;
			next.live=std::min(r.capacity,next.reserve);next.reserve-=next.live;
			out.feedback=effect::fill;break; // No closure, clearing or loader consumption.
		case operation::fill_external:
			if(next.phase!=action::open || !empty(next) || next.loader_hand!=hand::none || req.external_rounds<=0 || req.external_rounds>r.capacity)return out;
			next.live=req.external_rounds;out.rounds_from_item=req.external_rounds;
			out.feedback=effect::fill;break; // The independent item keeps its now-empty loader.
		case operation::discard:
		case operation::cleanup:
			if (next.loader_hand != req.actor) return out;
			if(req.op==operation::discard && req.preserve_discard){out.item_released=true;out.rounds_to_item=next.held_rounds;}
			else
			{
				const auto disposed=ammunition::dispose(next.held_rounds,req.op==operation::cleanup ?
					ammunition::disposition_reason::forced_cleanup : req.disposition,r.discard_penalty);
				next.reserve+=disposed.returned;out.rounds_spent+=disposed.lost;
			}
			next.held_rounds = 0; next.loader_hand = hand::none;
			out.feedback = effect::discard; out.silent = req.op == operation::cleanup || req.disposition==ammunition::disposition_reason::waist_return; break;
		case operation::close:
			if (next.phase != action::open) return out;
			next.phase = action::closing; out.feedback = effect::close; break;
		case operation::closed:
			if (next.phase != action::closing) return out;
			next.phase = action::closed; out.silent = true; break;
			case operation::accepted_shot:
				if (!ready(r,next)) return out;
				if(req.sustained){out.feedback=effect::shot;break;}
			--next.live; ++next.spent; out.rounds_spent = 1; out.feedback = effect::shot; break;
		default: return out;
		}
		++next.revision;
		if (!valid(r,next) || total_rounds(current)+out.rounds_from_item != total_rounds(next)+out.rounds_spent+out.rounds_to_item)
		{ out.error = rejection::invalid_state; return out; }
		out.error = rejection::none; out.next = next;
		out.before = native_ammo(current); out.after = native_ammo(next);
		return out;
	}
}
