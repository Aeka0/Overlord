#pragma once
#include "frame.hpp"
#include "pose_plan.hpp"

namespace vr::gameplay::hand_interaction
{
	// These functions are the single simulation owner. Domain providers expose
	// candidates and settled state; they never ask one another who owns a hand.
	void begin(const frame&,bool weapons_suspended=false)noexcept;
	void offer(candidate)noexcept;
	void select_supply(hand,domain,object_identity)noexcept;
	using supply_exchange_fn=bool(*)(void*)noexcept;
	bool exchange_supply(hand,target from,grasp to,supply_exchange_fn,void*)noexcept;
	void resolve()noexcept;
	void finish()noexcept;
	void synchronize(bool release_inputs=false)noexcept;
	void suspend(bool preserve_use_input=false)noexcept;
	const frame* simulation()noexcept;
	edges input(hand,button)noexcept;
	bool granted(hand,domain,object_identity,button=button::none,role=role::none)noexcept;
	bool permits(hand,domain,object_identity,recipe=recipe::single,role=role::part)noexcept;
	bool free(hand)noexcept;
	bool has(hand,domain)noexcept;
	bool allows_melee(hand)noexcept;
	std::array<session,8> current()noexcept;
	pose_plan pose(hand)noexcept;
	// Providers publish their actual post-transaction relation. Missing entries
	// end sessions; a failed native cleanup keeps its existing escrow relation.
	void observed(hand,grasp)noexcept;
	void completed(hand,target)noexcept; // Accepted one-shot command without a retained physical grasp.
	void observe_carry(std::span<const weapons::carry::instance>)noexcept;
	void publish()noexcept;
	bool weapons_suspended()noexcept;
	std::uint64_t input_sequence()noexcept;
	std::uint64_t input_reference()noexcept;
	void begin_reconciliation()noexcept;
	void end_reconciliation()noexcept;
	void invalidate_input()noexcept;
	inline target object(domain provider,object_identity id,unsigned component=0,std::uint64_t binding=0)noexcept
	{return {provider,id,component,binding};}
}
