#pragma once
#include "hand_pose_solver.hpp"
#include "weapon_holding.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::hands
{
	// Presentation offsets only. Raw controller samples and mechanical contacts
	// never pass through this transition. Keys describe attachment changes, not
	// continuously changing travel/rotation within one grasp.
	enum class part_hand_attachment : unsigned
	{
		free, magazine, seated_magazine, action, belt_cover, belt_bridge,
		rack, shell, barrel, underbarrel_action, underbarrel_round, sensor, loader, rocket
	};
	class part_hand_transition
	{
		using clock=controller_input::clock;
		vec offset_{}, error_{};
		part_hand_attachment attachment_{};
		clock::time_point started_{}, last_{};
		std::uint64_t reference_{}, assembly_{}, revision_{}, grasp_{};
		weapons::weapon_identity weapon_{};
		bool observed_{};
	public:
		inline static constexpr float seconds=.09f;
		void reset() noexcept {*this={};}
		vec update(part_hand_attachment attachment,anchor tracked,vec desired,vec initial,
			const weapons::hold& owner,std::uint64_t assembly,std::uint64_t reference,
			clock::time_point now,float units,bool active,std::uint64_t grasp=0) noexcept
		{
			if(!active || !std::isfinite(units) || units<=0) {reset();return desired;}
			for(const auto& p:{tracked.position,desired,initial})for(float x:p)
				if(!std::isfinite(x)){reset();return desired;}
			float norm{};for(float x:tracked.rotation)norm+=x*x;
			if(!std::isfinite(norm) || norm<.5f || norm>1.5f){reset();return desired;}
			const auto inverse=conjugate(normalize(tracked.rotation));
			const auto local=[&](vec p){return scale(rotate(inverse,sub(p,tracked.position)),1/units);};
			const auto target=local(desired);
			if(!observed_ || weapon_!=owner.id() || revision_!=owner.rear_revision || assembly_!=assembly ||
				reference_!=reference || now<last_ || now-last_>std::chrono::milliseconds(150))
			{
				// Rebase from this frame's unmodified hand on discontinuity; never
				// animate an offset from a previous weapon or tracking universe.
				offset_=local(initial);attachment_=part_hand_attachment::free;error_={};observed_=true;
			}
			if(attachment!=attachment_ || (attachment!=part_hand_attachment::free && grasp!=grasp_))
			{error_=sub(offset_,target);started_=now;attachment_=attachment;}
			const float t=std::clamp(std::chrono::duration<float>(now-started_).count()/seconds,0.f,1.f);
			const float remaining=1-t*t*(3-2*t);
			offset_=add(target,scale(error_,remaining));
			weapon_=owner.id();revision_=owner.rear_revision;assembly_=assembly;reference_=reference;last_=now;grasp_=grasp;
			return add(tracked.position,rotate(tracked.rotation,scale(offset_,units)));
		}
	};

	// Stack-owned context passed explicitly to pose providers. Apply before a
	// provider freezes loose-item transforms, so hand and ammunition stay joined.
	class part_hand_frame
	{
		std::array<part_hand_transition,2>& motion_;
		const rig& rig_;
		const controller_input::frame& input_;
		const weapons::hold& owner_;
		const std::array<anchor,2>& tracked_;
		const std::array<vec,2>& shoulders_;
		const std::array<vec,3>& axes_;
		std::span<bone> solved_;
		std::array<vec,2> initial_{};
		std::uint64_t assembly_{};
		controller_input::clock::time_point now_{};
		float units_{};bool active_{};unsigned applied_{};
	public:
		part_hand_frame(std::array<part_hand_transition,2>& motion,const rig& r,const controller_input::frame& input,
			const weapons::hold& owner,const std::array<anchor,2>& tracked,const std::array<vec,2>& shoulders,
			const std::array<vec,3>& axes,std::span<bone> solved,std::uint64_t assembly,
			controller_input::clock::time_point now,float units,bool active) noexcept:
			motion_(motion),rig_(r),input_(input),owner_(owner),tracked_(tracked),shoulders_(shoulders),axes_(axes),
			solved_(solved),assembly_(assembly),now_(now),units_(units),active_(active)
		{for(int h=0;h<2;++h)initial_[h]=solved[r.arms[h].wrist].position;}
		void apply(int h,part_hand_attachment attachment) noexcept
		{
			if(h<0 || h>1 || (applied_&(1u<<h)))return;
			applied_|=1u<<h;
			if(owner_.rear==vr::hand(h) || owner_.support==vr::hand(h)) {motion_[h].reset();return;}
			const auto a=rig_.arms[h];const auto old=solved_[a.wrist].position;
			const bool active=active_ && input_.focused && input_.sequence && input_.reference_generation &&
				input_.grip[h].valid && input_.aim[h].valid && now_>=input_.sampled_at &&
				now_-input_.sampled_at<=std::chrono::milliseconds(150);
			const auto desired=motion_[h].update(attachment,tracked_[h],old,initial_[h],owner_,assembly_,
				input_.reference_generation,now_,units_,active,input_.trigger[h].presses);
			if(length(sub(desired,old))<.00001f)return;
			std::array<anchor,2> targets;
			for(int side=0;side<2;++side)targets[side]={solved_[rig_.arms[side].wrist].position,solved_[rig_.arms[side].wrist].rotation};
			targets[h].position=desired;
			std::array<bone,256> result{};std::array<bool,2> limited{};
			if(!solve_arms(rig_,solved_,targets,shoulders_,axes_,result,limited))return;
			const auto correction=sub(desired,result[a.wrist].position);
			for(int b=0;b<rig_.count;++b)if(!rig_.weapon_bones[b] && descendant(b,a.shoulder,rig_))
			{
				if(descendant(b,a.wrist,rig_))result[b].position=add(result[b].position,correction);
				solved_[b]=result[b];
			}
		}
		void finish() noexcept {for(int h=0;h<2;++h)apply(h,part_hand_attachment::free);}
	};
}
