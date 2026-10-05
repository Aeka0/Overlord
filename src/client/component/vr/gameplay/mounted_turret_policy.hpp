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
	inline std::array<float,2> hand_angles(vec wrist,const controls& c) noexcept
	{
		return {direction_angles(hands::scale(hands::sub(wrist,c.pivot),-1.f))[0],
			direction_angles(hands::sub(wrist,c.yaw_pivot))[1]};
	}
	inline unsigned world_scene_flags(unsigned flags) noexcept {return flags&~1u;}
	// Server-owned gesture state. Input counters retain taps between server ticks;
	// a new attachment/reference/action generation always requires neutral input.
	class controller
	{
	public:
		void reset(std::array<float,2> aim={}) noexcept {*this={};angles=aim;}
		bool free_hands{};
		unsigned gripped{},fire_armed{};
		std::array<float,2> angles{};
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
			const float dt=std::clamp(std::chrono::duration<float>(f.sampled_at-at).count(),0.f,.05f);
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
					const auto before=hand_angles(previous[h],c);
					const auto previous_yaw=hands::sub(previous[h],c.yaw_pivot);
					// Singular or discontinuous hand motion cannot steer this sample,
					// but only a squeeze release (or invalid input) gives up the grip.
					std::array<float,2> delta{};bool bounded=away_from_axes &&
						hands::length(hands::sub(previous[h],c.pivot))>=.1f*c.units &&
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
				previous[h]=c.wrists[h];
				if (press) armed&=~bit;
				if (!s.down && !press) armed|=bit;
			}
			if (!was_free && free_hands) {armed=takeover=0;return;}
			if (free_hands)
			{
				const std::array<float,2> delta{f.turn_active && turn_armed ? -stick(f.turn[1])*60.f*dt : 0.f,
					f.turn_active && turn_armed ? -stick(f.turn[0])*90.f*dt : 0.f};
				for (int a=0;a<2;++a)
				{
					auto value=angles[a]+delta[a]+(aiming ? hand_delta[a]/aiming : 0.f);
					if (a==1 && c.low[a]<=-180.f && c.high[a]>=180.f) value=std::remainder(value,360.f);
					angles[a]=std::clamp(value,c.low[a],c.high[a]);
				}
			}
		}
	private:
		bool initialized{},turn_armed{};
		unsigned armed{},takeover{};
		std::array<controller_input::digital_action,2> seen{};
		std::array<std::uint64_t,2> trigger_generation{};
		std::array<vec,2> previous{};
		std::uint64_t reference{},sequence{};
		controller_input::clock::time_point at{};
	};
}
