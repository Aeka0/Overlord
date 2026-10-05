#pragma once
#include "underbarrel_rig.hpp"
#include "underbarrel_native.hpp"
#include "../controller_input.hpp"
#include "part_presentation.hpp"
#include "weapon_carry_runtime.hpp"
#include "body_supply_volume.hpp"
#include "hand_interaction/frame.hpp"
#include "hand_interaction/constraints.hpp"

namespace vr::gameplay::weapons::underbarrel
{
	using clock=controller_input::clock;
	using lease=contact_role;
	struct scene
	{
		controller_input::frame input{};hold owner{};kind type{};std::uint64_t assembly{};
		bool gameplay{};float firing_distance{10},facing{},action_distance{10},waist_distance{10},load_distance{10},load_alignment{};
		hands::vec local_hand{},axis{};float stroke{},units{};hands::anchor muzzle{};
		// Immutable local contacts are published by the renderer; the server
		// reconstructs geometry with the SAME input as ordinary carry arbitration.
		int contact_hand{-1};float support_facing{-1},waist_radius{.2f};body_supply_layout supply{};
		hands::quat wrist_basis{0,0,0,1}; // Visual round/hand placement only, never input-facing classification.
		hands::anchor firing_local{},action_local{},round_in_wrist{},loading_local{},muzzle_local{};
		float firing_forward_m{};
		hands::vec rear_local{},rest_span{};float hand_distance{},support_radius{.075f},support_release{.22f};
	};
	inline bool finite_contact(const scene& s)noexcept
	{
		if(!std::isfinite(s.units) || s.units<=0 || !std::isfinite(s.stroke) || s.stroke<=0 || s.stroke>.5f)return false;
		for(float d:{s.firing_distance,s.action_distance,s.waist_distance,s.load_distance})if(!std::isfinite(d)||d<0)return false;
		for(float f:s.local_hand)if(!std::isfinite(f)||std::abs(f)>1000)return false;
		const auto length=hands::length(s.axis);
		return std::isfinite(length) && length>.99f && length<1.01f && std::isfinite(s.facing) && std::isfinite(s.load_alignment);
	}
	struct action_display
	{
		std::uint64_t sequence{},assembly{},grip_generation{};clock::time_point sampled_at{};
		hands::vec span{},hand{},start{},previous{},axis{},rest{};
		float units{},distance{},start_distance{},previous_distance{},initial{},stroke{};
		part_limits tolerance{};
	};
	struct presentation
	{
		bool active{},fault{},owns_support{};state ammo{};hold owner{};lease grip{},support_role{};float travel{};
		std::uint64_t reference{};clock::time_point at{};
		action_display motion{};
	};
	inline hands::vec relative_hand_span(const std::array<hands::anchor,2>& wrists,int rear,float units)noexcept
	{
		return hands::scale(hands::rotate(hands::conjugate(wrists[rear].rotation),
			hands::sub(wrists[1-rear].position,wrists[rear].position)),1/units);
	}
	// Render-only projection from the same current controller targets used by
	// the hand solve. The server remains the sole action/ammo/event authority.
	inline float displayed_travel(const presentation& v,const hold& owner,const controller_input::frame& input,
		const std::array<hands::anchor,2>& wrists,float units,std::uint64_t assembly,bool gameplay,clock::time_point now)noexcept
	{
		const auto& m=v.motion;
		if(!gameplay || !v.active || v.fault || !v.owns_support || !owner.can_fire() || owner.id()!=v.owner.id() ||
			owner.rear_revision!=v.owner.rear_revision || owner.rear!=v.owner.rear || v.reference!=input.reference_generation ||
			!m.sequence || input.sequence<m.sequence || !assembly || assembly!=m.assembly || !input.focused ||
			!std::isfinite(units) || units<=0 || units!=m.units || now<m.sampled_at || now-m.sampled_at>std::chrono::milliseconds(150) ||
			now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150))return v.travel;
		const int rear=int(owner.rear),off=1-rear;
		if(!input.grip[rear].valid || !input.aim[rear].valid || !input.grip[off].valid || !input.aim[off].valid ||
			!input.squeeze[off].active || !input.squeeze[off].down || input.squeeze[off].generation!=m.grip_generation ||
			(v.grip!=lease::action && !(v.grip==lease::support && !v.ammo.chamber)) ||
			(v.ammo.id.type!=kind::m203 && v.ammo.id.type!=kind::shotgun))return v.travel;
		for(const auto& wrist:wrists)
		{
			float norm{};for(float q:wrist.rotation){if(!std::isfinite(q))return v.travel;norm+=q*q;}
			if(norm<.99f || norm>1.01f)return v.travel;
		}
		const auto span=relative_hand_span(wrists,rear,units);
		stroke_result projected;
		if(v.ammo.id.type==kind::m203)
		{
			const float distance=m.distance+hands::length(span)-hands::length(m.span);
			const auto result=hand_interaction::shared_slider(m.rest,m.axis,m.start_distance,m.previous_distance,
				distance,m.initial,m.stroke,m.tolerance.retention,m.tolerance.step);
			projected={result.valid,result.travel};
		}
		else projected=project_stroke(m.start,m.previous,hands::add(m.hand,hands::sub(span,m.span)),m.axis,m.initial,m.stroke,m.tolerance);
		if(!projected.valid)return v.travel;
		// Keep a still-open chamber visibly short of closure until the server
		// accepts it. No visual stroke may chamber a round or emit a sound.
		return v.ammo.open ? std::max(.001f,projected.travel) : projected.travel;
	}
	bool enabled()noexcept;
	void exchange_supply(const hand_interaction::frame&)noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void report_interactions()noexcept;
	presentation current(weapon_identity)noexcept;
	enum class support_handoff
	{
		unchanged,
		acquire,
		release
	};
	struct support_handoff_input
	{
		bool grip_down{}, hand_occupied{};
	};
	inline support_handoff carry_support_handoff(const presentation& module,
	                                             const hold& owner,
	                                             const support_handoff_input& input) noexcept
	{
		if (!owner.can_fire() || !module.owns_support)
			return support_handoff::unchanged;
		const auto offhand = hand(1 - int(owner.rear));
		if (module.grip != lease::none && input.grip_down && !input.hand_occupied)
			return support_handoff::acquire;
		if (module.grip == lease::none && owner.support == offhand)
			return support_handoff::release;
		return support_handoff::unchanged;
	}
	// Admitted contact uses the filtered frame; retained support uses the original
	// squeeze state. Ownership commits remain in the server-owned carry adapter.
	void settle_interactions(const hand_interaction::frame&,
	                         const controller_input::frame& raw_input) noexcept;
	bool ordinary_support_allowed(const carry::scene&,
	                              const hands::anchor&,
	                              hand,
	                              bool retaining = false) noexcept;
	void suspend()noexcept;
	bool blocks_native(const void* ps)noexcept;
	bool prepare_transfer(weapon_identity)noexcept;
	bool restore_transfer(const presentation&)noexcept;
	inline bool binding_current(const scene& local,const carry::scene& live,const controller_input::frame& input)noexcept
	{
		return local.assembly && local.assembly==live.assembly && live.authored && live.owner.can_fire() &&
			local.owner.id()==live.owner.id() && local.contact_hand==1-int(live.owner.rear) &&
			live.sequence==input.sequence && live.reference==input.reference_generation;
	}
	inline hands::anchor mechanical_frame(const carry::scene& local,const std::array<hands::anchor,2>& wrists)noexcept
	{
		const int rear=int(local.owner.rear);if(rear<0 || rear>1)return {};
		const auto& driver=wrists[rear];
		return {hands::sub(driver.position,hands::rotate(driver.rotation,local.wrists[rear].position)),driver.rotation};
	}
	inline scene sample_contact(scene s,const controller_input::frame& input,const hold& owner,const hands::anchor& gun,
		const hands::anchor& wrist,hands::vec head,const std::array<hands::vec,3>& body_axis,float units,float travel)noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		s.input=input;s.owner=owner;s.units=units;
		const auto local=compose(inverse(gun),{wrist.position,normalize(multiply(wrist.rotation,s.wrist_basis))});
		const auto physical=multiply(conjugate(gun.rotation),wrist.rotation);
		const auto scores=controller_facing(physical,s.contact_hand);s.support_facing=scores.support;
		s.firing_distance=underbarrel::firing_distance(scale(sub(local.position,s.firing_local.position),1/units),s.firing_forward_m);
		s.rest_span=scale(sub(s.action_local.position,s.rear_local),1/units);
		s.hand_distance=length(sub(wrist.position,compose(gun,{s.rear_local,{0,0,0,1}}).position))/units;
		if(directional_grips(s.type))s.facing=scores.firing;
		else
		{float q{};for(int i=0;i<4;++i)q+=local.rotation[i]*s.firing_local.rotation[i];s.facing=2*q*q-1;}
		const auto action=add(s.action_local.position,scale(s.axis,travel*units));
		s.action_distance=length(sub(local.position,action))/units;s.local_hand=scale(local.position,1/units);
		const auto round=compose(local,s.round_in_wrist);
		s.load_distance=length(sub(round.position,s.loading_local.position))/units;
		s.load_alignment=dot(rotate(round.rotation,{1,0,0}),rotate(s.loading_local.rotation,{1,0,0}));
		s.waist_distance=10;
		for(const auto& volume:body_supply_volumes(head,body_axis,units,s.supply,s.waist_radius))
			s.waist_distance=std::min(s.waist_distance,volume.distance(wrist.position)/units);
		s.muzzle=compose(gun,s.muzzle_local);return s;
	}
	hands::anchor support_anchor(const part_rig&,const hands::rig&,const hands::pose_library&,hands::anchor ordinary,
		int hand,const presentation&,float units)noexcept;
	part_presentation::result present(const part_rig&,const hands::rig&,const hands::pose_library&,const profile&,
		const std::array<hands::anchor,2>& ordinary_supports,
		const controller_input::frame&,const hold&,const presentation&,std::uint64_t,bool,
		const std::array<hands::anchor,2>&,const std::array<hands::vec,2>&,const std::array<hands::vec,3>&,
		hands::vec head,hands::vec offset,float units,std::span<hands::bone>,hands::part_hand_frame* hand_motion=nullptr)noexcept;
}
