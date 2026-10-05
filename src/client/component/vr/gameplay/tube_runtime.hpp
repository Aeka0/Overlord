#pragma once
#include "tube_profile.hpp"
#include "native_ammunition.hpp"
#include "hand_interaction/frame.hpp"
#include "mechanical_display_time.hpp"
namespace vr::gameplay::weapons::tube
{
	struct scene_frame
	{
		controller_input::frame input{};hold owner{};geometry contact{};std::uint64_t assembly{};bool gameplay{};
		hands::anchor ejection_world{},shell_world{};float units{};const tube_profile* definition{};bool manipulation{};
		hand_interaction::hand_binding binding{};
		std::array<hand_interaction::hand_binding,2> shell_bindings{};bool has_shell_bindings{};
	};
	struct presentation
	{
		bool active{},fault{},fire_armed{},rack_held{};state ammo{};hold owner{};const tube_profile* definition{};
		lever::pose_state lever{};
		clock::time_point sampled_at{},shot_at{},load_at{};float travel{};physical_reload::slide_constraint rack_grip{};
		std::uint64_t reference{},input_sequence{},event_sequence{};
		struct event{std::uint64_t sequence{};effect kind{};clock::time_point at{};hands::anchor world{};float units{};int live{};};
		std::array<event,8> events{};
		void resume_transfer()noexcept{events={};reference=0;input_sequence=0;fire_armed=false;rack_held=false;lever.spinning=false;lever.spin=0;lever.operating=false;lever.returning=false;}
	};
	inline hand interaction_hand(const presentation& v)noexcept
	{
		if(!v.active || !valid_hand(v.owner.holding_hand()))return hand::none;
		const auto actor=hand(1-int(v.owner.holding_hand()));
		return v.ammo.loader_hand==actor || (v.rack_held && v.owner.support!=actor)?actor:hand::none;
	}

	inline lever::pose_state held_lever_pose(const presentation& v,const hold& owner)noexcept
	{
		auto motion=v.lever;
		if(v.definition && (owner.rear_revision!=v.owner.rear_revision || owner.rear!=v.owner.rear))
		{
			motion.grasped=owner.can_fire() && (owner.attachment==control_attachment::moving || motion.open<v.definition->interaction.lever.grip_separation);
			motion.spinning=motion.operating=motion.returning=false;motion.spin=0;
		}
		return motion;
	}
	inline lever::pose_state displayed_lever_pose(const presentation& v,const hold& owner,std::uint64_t reference,clock::time_point now)noexcept
	{
		auto motion=held_lever_pose(v,owner);
		if(!v.active || v.fault || !v.definition || !v.definition->lever || !motion.grasped ||
			owner.id()!=v.owner.id() || owner.rear_revision!=v.owner.rear_revision || reference!=v.reference)return motion;
		const auto& p=v.definition->interaction.lever;
		const float seconds=mechanical_display_seconds(v.sampled_at,now);
		if(motion.returning)motion.open=std::max(.001f,motion.open-seconds/p.return_seconds);
		if(!motion.spinning || p.spin_open.size()<2)return motion;
		// Never predict through the empty-gun stop or the unconfirmed catch.
		float limit=motion.spin<lever::catch_progress?lever::catch_progress:motion.spin==lever::catch_progress?motion.spin:1.f;
		if(!v.ammo.chamber && !v.ammo.stored)
			for(size_t i=1;i<p.spin_open.size();++i)if(p.spin_open[i]>=1){limit=std::min(limit,float(i)/float(p.spin_open.size()-1));break;}
		motion.spin=std::max(motion.spin,std::min(limit,motion.spin+seconds/p.spin_seconds));
		const float frame=motion.spin*float(p.spin_open.size()-1);const auto i=std::min(size_t(frame),p.spin_open.size()-2);
		motion.open=p.spin_open[i]+(p.spin_open[i+1]-p.spin_open[i])*(frame-float(i));
		motion.open=std::max(motion.open,motion.entry_open*std::max(0.f,1-motion.spin/(6.f/35.f)));
		return motion;
	}
	presentation current(weapon_identity id)noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void update_interactions();
	// Identity/reconciliation/settlement only; never acquires from cached input.
	void update_lifecycle(bool suspended);
	void report_interactions()noexcept;
	bool prepare_transfer(weapon_identity id,presentation& saved)noexcept;
	bool restore_transfer(const presentation& saved)noexcept;
	void publish_scene(const scene_frame&)noexcept;
	void set_boundary_ready(unsigned bit)noexcept;
	bool blocks_reload(const void* ps,int side)noexcept;
	bool allow_fire(const void* ps,int command,int side)noexcept;
	bool allow_owned_shot(const hold&,const native_ammunition::snapshot&)noexcept;
	void consumed(const void* ps,int command,std::uint32_t weapon,bool alternate,int amount,int side,
		const native_ammunition::snapshot& before,const native_ammunition::snapshot& after,bool sustained=false)noexcept;
}
