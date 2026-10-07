#pragma once
#include "hands/pose_solver.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::mounted
{
	using hands::vec;
	using hands::anchor;
	inline bool attached(unsigned entity_flags) noexcept {return (entity_flags&0x3000)!=0;}
	struct controls
	{
		bool valid{},calibrated{};
		unsigned tracked{};
		std::array<vec,2> wrists{},handles{}; // In the vehicle's stable root space.
		anchor tracking_frame{}; // Tracking-to-root transform; identity for aim-only mounts.
		vec pivot{},yaw_pivot{}; // Pitch joint and base yaw joint are distinct.
		float units{39.3701f};
		std::array<float,2> low{},high{}; // Native relative pitch/yaw limits.
	};
	inline bool finite(vec v) noexcept
	{return std::all_of(v.begin(),v.end(),[](float x){return std::isfinite(x) && std::abs(x)<1e7f;});}
	inline std::array<float,2> direction_angles(vec v) noexcept
	{return {std::atan2(-v[2],std::hypot(v[0],v[1]))*57.295779513f,std::atan2(v[1],v[0])*57.295779513f};}
	inline float stick(float x) noexcept
	{return std::isfinite(x) && std::abs(x)>.2f ? std::copysign((std::min(std::abs(x),1.f)-.2f)/.8f,x) : 0.f;}
	inline float bounded_aim(float value,int axis,const controls& c) noexcept
	{
		if(axis==1 && c.low[axis]<=-180.f && c.high[axis]>=180.f)value=std::remainder(value,360.f);
		return std::clamp(value,c.low[axis],c.high[axis]);
	}
	inline std::array<float,2> hand_angles(vec wrist,const controls& c) noexcept
	{
		return {direction_angles(hands::scale(hands::sub(wrist,c.pivot),-1.f))[0],
			direction_angles(hands::sub(wrist,c.yaw_pivot))[1]};
	}
	inline unsigned world_scene_flags(unsigned flags) noexcept {return flags&~1u;}
	struct view_pose
	{
		std::array<float,2> aim{};
		float yaw{};
	};
	inline anchor orbit_seat(anchor root,anchor seat,vec pivot,float yaw) noexcept
	{
		const float radians=yaw*.00872664625997f;
		const hands::quat turn{0,0,std::sin(radians),std::cos(radians)};
		auto local=spatial_math::compose(spatial_math::inverse(root),seat);
		local.position=hands::add(pivot,hands::rotate(turn,hands::sub(local.position,pivot)));
		local.rotation=hands::multiply(turn,local.rotation);
		return spatial_math::compose(root,local);
	}
	// Server-owned gesture state. Input counters retain taps between server ticks;
	// a new attachment/reference/action generation always requires neutral input.
	class controller
	{
	public:
		void reset(std::array<float,2> aim={}) noexcept {*this={};angles=aim;}
		bool free_hands{};
		unsigned gripped{},fire_armed{};
		std::array<float,2> angles{};
		float stick_yaw{}; // Wrapped sum of accepted stick yaw, excluding physical aim.
		std::array<float,2> stick_delta(const controller_input::frame& f,float seconds) const noexcept
		{
			if(!free_hands || !turn_armed || !f.turn_active || f.reference_generation!=reference ||
				!std::isfinite(f.turn[0]) || !std::isfinite(f.turn[1]))return {};
			return {-stick(f.turn[1])*60.f*seconds,-stick(f.turn[0])*90.f*seconds};
		}
		controller native_presentation(std::array<float,2> solved_angles) const noexcept
		{
			// Native vehicle aim converges from the barrel toward an eye-ray target.
			// Publish its result with the consumed input history, without feeding
			// convergence or slew back into the persistent user aim request.
			auto result=*this;result.angles=solved_angles;return result;
		}
		std::array<float,2> displayed_angles(const controller_input::frame& f,const controls& geometry) const noexcept
		{
			if(!free_hands)return angles;
			auto visual=*this;auto sampled=geometry;
			sampled.calibrated=false; // Presentation cannot acquire a new handle or take over.
			visual.update(f,sampled);return visual.angles;
		}
		view_pose displayed_view(const controller_input::frame& f,const controls& geometry,
			controller_input::clock::time_point now) const noexcept
		{
			auto visual=*this;auto sampled=geometry;auto input=f;
			// Project from the acknowledged state to this camera frame. A fresh
			// held analog sample can span multiple render frames; neither native
			// command time nor a repeated input sequence should quantize the view.
			input.sampled_at=now;
			if(input.sequence==sequence)++input.sequence;
			sampled.calibrated=false;
			if(free_hands)visual.update(input,sampled);
			return {visual.angles,visual.stick_yaw};
		}
		unsigned held_hands(const controller_input::frame& f) const noexcept
		{
			if(!free_hands || !f.focused || f.reference_generation!=reference)return 0;
			unsigned result{};
			for(unsigned h=0;h<2;++h)if((gripped&(1u<<h)) && f.grip[h].valid && f.aim[h].valid &&
				f.squeeze[h].active && f.squeeze[h].down && f.squeeze[h].generation==seen[h].generation)result|=1u<<h;
			return result;
		}
		bool firing(const controller_input::frame& f) const noexcept
		{
			const auto held=held_hands(f);
			for (unsigned h=0;h<2;++h) if ((held&fire_armed&(1u<<h)) && f.trigger[h].active && f.trigger[h].down &&
				f.trigger[h].generation==trigger_generation[h]) return true;
			return false;
		}
		void update(const controller_input::frame& f,const controls& c) noexcept
		{
			const bool fresh=c.valid && f.focused && f.sequence && std::isfinite(c.units) && c.units>1 && c.units<1000;
			bool limits=true;for (int a=0;a<2;++a) limits&=std::isfinite(c.low[a]) && std::isfinite(c.high[a]) && c.low[a]<=c.high[a];
			const bool epoch=!initialized || f.reference_generation!=reference || f.sampled_at<at || f.sampled_at-at>std::chrono::milliseconds(150);
			if (!fresh || !limits || epoch)
			{
				free_hands=false;gripped=fire_armed=armed=takeover=0;turn_armed=false;
				seen=f.squeeze;for (int h=0;h<2;++h) trigger_generation[h]=f.trigger[h].generation;
				initialized=fresh && limits;reference=f.reference_generation;at=f.sampled_at;sequence=f.sequence;return;
			}
			if (sequence==f.sequence) return;
			const float dt=std::clamp(std::chrono::duration<float>(f.sampled_at-at).count(),0.f,.15f);
			sequence=f.sequence;at=f.sampled_at;
			if (!f.turn_active) turn_armed=false;
			else if (std::isfinite(f.turn[0]) && std::isfinite(f.turn[1]) && std::abs(f.turn[0])<=.2f && std::abs(f.turn[1])<=.2f) turn_armed=true;
			std::array<float,2> hand_delta{};unsigned aiming{};
			const bool was_free=free_hands;
			for (unsigned h=0;h<2;++h)
			{
				const auto bit=1u<<h;const auto s=f.squeeze[h];const auto t=f.trigger[h];
				const bool tracking=(c.tracked&bit) && finite(c.wrists[h]) && finite(c.handles[h]) && finite(c.pivot) && finite(c.yaw_pivot);
				if (!tracking || !s.active || s.generation!=seen[h].generation || s.presses<seen[h].presses || s.releases<seen[h].releases)
				{armed&=~bit;takeover&=~bit;gripped&=~bit;fire_armed&=~bit;seen[h]=s;continue;}
				const bool press=s.presses!=seen[h].presses,release=s.releases!=seen[h].releases;
				seen[h]=s;
				if (!was_free)
				{
					if (c.calibrated && press && (armed&bit)) takeover|=bit;
					if (c.calibrated && (takeover&bit) && release && !s.down) free_hands=true;
					if (!s.down && !press) armed|=bit;
					continue; // Takeover never doubles as a handle grab.
				}
				if (!t.active || t.generation!=trigger_generation[h]) fire_armed&=~bit;
				trigger_generation[h]=t.generation;
				if (!s.down || release)
				{gripped&=~bit;fire_armed&=~bit;}
				const auto radial=hands::sub(c.wrists[h],c.pivot);
				const auto yaw_radial=hands::sub(c.wrists[h],c.yaw_pivot);
				const bool away_from_axes=hands::length(radial)>=.1f*c.units && std::hypot(yaw_radial[0],yaw_radial[1])>=.1f*c.units;
				const auto heading=hand_angles(c.wrists[h],c);
				if (gripped&bit)
				{
					if (t.active && !t.down) fire_armed|=bit;
					// Compare both samples around the SAME current joints. Gun/vehicle
					// animation moving a pivot must not masquerade as controller input.
					// Re-express the previous physical wrist in THIS tracking frame.
					// Turning the player/seat cannot become another hand aim delta.
					const auto previous=spatial_math::compose(c.tracking_frame,{previous_[h]}).position;
					const auto before=hand_angles(previous,c);
					const auto previous_yaw=hands::sub(previous,c.yaw_pivot);
					// Singular or discontinuous hand motion cannot steer this sample,
					// but only a squeeze release (or invalid input) gives up the grip.
					std::array<float,2> delta{};bool bounded=away_from_axes &&
						hands::length(hands::sub(previous,c.pivot))>=.1f*c.units &&
						std::hypot(previous_yaw[0],previous_yaw[1])>=.1f*c.units;
					for (int a=0;a<2;++a) {delta[a]=std::remainder(heading[a]-before[a],360.f);bounded&=std::abs(delta[a])<=45.f;}
					if (bounded) {for (int a=0;a<2;++a) hand_delta[a]+=delta[a];++aiming;}
				}
				if (press && !release && s.down && (armed&bit) && c.calibrated &&
					hands::length(hands::sub(c.wrists[h],c.handles[h]))<=.12f*c.units && away_from_axes)
				{
					gripped|=bit;fire_armed&=~bit;
					if (t.active && !t.down) fire_armed|=bit;
				}
				previous_[h]=spatial_math::compose(spatial_math::inverse(c.tracking_frame),{c.wrists[h]}).position;
				if (press) armed&=~bit;
				if (!s.down && !press) armed|=bit;
			}
			if (!was_free && free_hands) {armed=takeover=0;return;}
			if (free_hands)
			{
				const auto delta=stick_delta(f,dt);
				for (int a=0;a<2;++a)
				{
					const auto physical=angles[a]+(aiming ? hand_delta[a]/aiming : 0.f);
					angles[a]=bounded_aim(physical+delta[a],a,c);
					if(a==1)stick_yaw=std::remainder(stick_yaw+std::remainder(angles[a]-bounded_aim(physical,a,c),360.f),360.f);
				}
			}
		}
	private:
		bool initialized{},turn_armed{};
		unsigned armed{},takeover{};
		std::array<controller_input::digital_action,2> seen{};
		std::array<std::uint64_t,2> trigger_generation{};
		std::array<vec,2> previous_{};
		std::uint64_t reference{},sequence{};
		controller_input::clock::time_point at{};
	};
	// Render-owned pose: camera orbit and gun presentation share one projection.
	// Server publications acknowledge the baseline, not the display cadence.
	class view_motion
	{
		std::uint64_t instance_{},reference_{},continuity_{},sequence_{};
		float stick_yaw_{};
		bool following_{},turn_armed_{};
		view_pose pose_{};
		controller_input::clock::time_point at_{};
	public:
		view_pose update(const controller& state,const controls& geometry,std::uint64_t instance,const controller_input::frame& input,
			bool allowed,controller_input::clock::time_point now) noexcept
		{
			if(!instance){*this={};return {};}
			if(instance_!=instance){*this={};instance_=instance;pose_={state.angles,state.stick_yaw};}
			if(!allowed || !geometry.valid || !state.free_hands || !input.focused || input.orientation_settling ||
				!input.sequence || now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150) ||
				!std::isfinite(state.stick_yaw)) {following_=turn_armed_=false;return pose_;}
			const auto projected=state.displayed_view(input,geometry,now);
			const bool continuous=following_ && reference_==input.reference_generation &&
				continuity_==input.continuity_generation && input.sequence>=sequence_ && now>=at_ &&
				now-at_<=std::chrono::milliseconds(150);
			if(!continuous){stick_yaw_=projected.yaw;turn_armed_=false;}
			if(!input.turn_active)turn_armed_=false;
			else if(std::isfinite(input.turn[0]) && std::isfinite(input.turn[1]) &&
				std::abs(input.turn[0])<=.2f && std::abs(input.turn[1])<=.2f)turn_armed_=true;
			const float seconds=continuous?std::chrono::duration<float>(now-at_).count():0.f;
			const float delta=turn_armed_?state.stick_delta(input,seconds)[1]:0.f;
			// Strip the server/predicted stick component before adding this
			// frame's continuous total. Acknowledgements, release and reversal
			// cannot snap the view back to the previous server angle.
			const float physical=projected.aim[1]-std::remainder(projected.yaw-state.stick_yaw,360.f);
			const float before=bounded_aim(physical+std::remainder(stick_yaw_-state.stick_yaw,360.f),1,geometry);
			pose_.aim=projected.aim;pose_.aim[1]=bounded_aim(before+delta,1,geometry);
			const float accepted=std::remainder(pose_.aim[1]-before,360.f);
			pose_.yaw=std::remainder(pose_.yaw+accepted,360.f);
			stick_yaw_=std::remainder(stick_yaw_+accepted,360.f);following_=true;
			reference_=input.reference_generation;continuity_=input.continuity_generation;
			sequence_=input.sequence;at_=now;
			return pose_;
		}
	};
}
