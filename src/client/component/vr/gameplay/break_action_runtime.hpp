#pragma once
#include "break_action_profile.hpp"
#include "native_ammunition.hpp"
#include "hand_interaction/frame.hpp"
#include "mechanical_display_time.hpp"
#include "quick_reload.hpp"

namespace vr::gameplay::weapons::break_action
{
	struct scene_frame
	{
		controller_input::frame input{};hold owner{};geometry contact{};std::uint64_t assembly{};bool gameplay{};
		hands::anchor shell_world{};std::array<hands::anchor,2> chambers_world{};float units{};
		const break_action_profile* definition{};bool manipulation{};
		hands::anchor gun_world{};
		hand_interaction::hand_binding binding{};
	};
	struct presentation
	{
		bool active{},fault{},fire_armed{},barrel_held{};state ammo{};hold owner{};
		quick_reload::dwell quick_load{};
		const break_action_profile* definition{};clock::time_point sampled_at{},shot_at{};
		std::uint64_t reference{},input_sequence{},event_sequence{};
		struct event{std::uint64_t sequence{};effect kind{};clock::time_point at{};hands::anchor world{};float units{};unsigned ejected{},chamber{};std::array<hands::anchor,2> chambers{};};
		std::array<event,8> events{};
		bool allows_trigger(const hold& requested,std::uint64_t ref,clock::time_point now)const noexcept
		{
			// Keep empty-chamber dry clicks native, but never start a projectile
			// while the breech is open or a held trigger still needs neutral rearm.
			return !active || (!fault && fire_armed && ammo.phase==action::closed && !barrel_held &&
				owner.id()==requested.id() && owner.rear_revision==requested.rear_revision && owner.rear==requested.rear &&
				reference==ref && now>=sampled_at && now-sampled_at<=std::chrono::milliseconds(150));
		}
		void resume_transfer()noexcept{events={};reference=0;input_sequence=0;fire_armed=barrel_held=false;quick_load.reset();}
	};
	inline float displayed_hinge(const presentation& v,const hold& owner,std::uint64_t reference,clock::time_point now)noexcept
	{
		float hinge=v.active?v.ammo.hinge:0;
		if(!v.active || v.fault || !v.definition || v.barrel_held || owner.id()!=v.owner.id() ||
			owner.rear_revision!=v.owner.rear_revision || reference!=v.reference)return hinge;
		const auto seconds=mechanical_display_seconds(v.sampled_at,now);
		if(v.ammo.phase==action::opening)hinge=std::min(1.f,hinge+seconds/v.definition->interaction.opening_seconds);
		if(v.ammo.phase==action::closing)hinge=std::max(.001f,hinge-seconds/v.definition->interaction.closing_seconds);
		return hinge;
	}
	presentation current(weapon_identity)noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void update_interactions();
	// Identity/reconciliation/settlement only; never acquires from cached input.
	void update_lifecycle(bool suspended);
	void report_interactions()noexcept;
	bool allows_trigger(const hold&,std::uint64_t reference,clock::time_point now)noexcept;
	bool prepare_transfer(weapon_identity,presentation&)noexcept;
	bool restore_transfer(const presentation&)noexcept;
	void publish_scene(const scene_frame&)noexcept;
	void set_boundary_ready(unsigned)noexcept;
	bool blocks_reload(const void*,int)noexcept;
	bool allow_fire(const void*,int,int)noexcept;
	bool allow_owned_shot(const hold&,const native_ammunition::snapshot&)noexcept;
	void consumed(const void*,int,std::uint32_t,bool,int,int,const native_ammunition::snapshot&,const native_ammunition::snapshot&,bool sustained=false)noexcept;
}
