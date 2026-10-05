#pragma once
#include "swept_impact.hpp"
#include "hands/contact.hpp"
#include "slap_diagnostic.hpp"
#include "palm_contact.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	struct handle_catch
	{
		float turn_angle{.40f}, lift_distance{.025f}, engage{.85f}, disengage{.20f};
		vec up{0,0,1};
		impact_profile slap{.06f,.10f,.50f,.025f,{0,0,-1},.35f,true};
		float max_slap_speed{8.f};
	};
	inline bool unit_quaternion(hands::quat q) noexcept
	{
		float square{};
		for (float x:q) { if (!std::isfinite(x)) return false; square+=x*x; }
		return std::abs(square-1)<.01f;
	}
	inline bool valid(const handle_catch& p) noexcept
	{
		for (float x:{p.turn_angle,p.lift_distance,p.engage,p.disengage,p.slap.radius,
			p.slap.rearm_radius,p.slap.min_speed,p.slap.min_travel,p.max_slap_speed})
			if (!std::isfinite(x) || x<=0) return false;
		for (auto v:{p.up,p.slap.direction})
		{
			for (float x:v) if (!std::isfinite(x)) return false;
			if (std::abs(hands::dot(v,v)-1)>.001f) return false;
		}
		return std::isfinite(p.slap.direction_cosine) && p.slap.direction_cosine>0 && p.slap.direction_cosine<=1 &&
			p.turn_angle<1.6f && p.lift_distance<.1f && p.disengage<p.engage && p.engage<=1 &&
			p.slap.radius<p.slap.rearm_radius && p.slap.rearm_radius<.3f && p.slap.min_travel<p.slap.rearm_radius &&
			p.max_slap_speed>p.slap.min_speed && p.max_slap_speed<=15;
	}
	struct handle_catch_input
	{
		bool valid{};
		hands::quat rotation{0,0,0,1}; // corrected raw wrist, gun-local
		std::array<vec,hands::hand_contact_count> slap_points{}; // whole hand relative to raised tab, metres
		vec hand_world{}; // raw world position, not IK or a snapped contact
		std::optional<contact_box> palm; // Receiver-only solid palm, target-relative metres.
	};
	inline bool valid(const handle_catch_input& in) noexcept
	{
		if (!in.valid || !unit_quaternion(in.rotation)) return false;
		for (auto v:in.slap_points) for (float x:v) if (!std::isfinite(x) || std::abs(x)>100) return false;
		for (float x:in.hand_world) if (!std::isfinite(x)) return false;
		return !in.palm || valid_palm_meters(*in.palm);
	}
	struct handle_catch_grip
	{
		hands::quat rotation{0,0,0,1};
		float initial_amount{};
	};
	inline float catch_amount(const handle_catch& p,const handle_catch_grip& grip,
		vec hand_delta,vec forward,hands::quat rotation) noexcept
	{
		if (!unit_quaternion(rotation) || !unit_quaternion(grip.rotation)) return grip.initial_amount;
		auto q=hands::normalize(hands::multiply(rotation,hands::conjugate(grip.rotation)));
		if (q[3]<0) for (auto& x:q) x=-x;
		const float turn=2*std::atan2(hands::dot({q[0],q[1],q[2]},forward),q[3])/p.turn_angle;
		const float lift=hands::dot(hand_delta,p.up)/p.lift_distance;
		// Either lift or roll can operate the catch in either direction. Using
		// max() would prevent a negative release whenever the other input is zero.
		const float delta=std::abs(turn)>std::abs(lift) ? turn : lift;
		return std::clamp(grip.initial_amount+delta,0.f,1.f);
	}
	template<size_t ContactCount> class hand_slap_sweep
	{
	public:
		using clock=std::chrono::steady_clock;
		void reset() noexcept { *this={}; }
		bool update(const impact_profile& impact,float max_speed,const std::array<vec,ContactCount>& points,vec hand_world,clock::time_point at,float max_step,
			slap_observation* observed=nullptr) noexcept
		{
			// Shared raw-hand sweep for raised charging handles and receiver paddles.
			const float dt=std::chrono::duration<float>(at-at_).count();
			const auto delta=hands::sub(hand_world,previous_);
			const float distance=hands::length(delta);
			// Moving the gun underneath a stationary hand is not a hand slap.
			const bool moving=sampled_ && dt>0 && dt<=.15f && distance>=impact.min_speed*dt && distance<=max_speed*dt;
			// Simulation can consume tracking at uneven intervals (observed 33-68
			// ms). A fixed spatial cap otherwise rejects a normal fast hand solely
			// because one sample arrived later. Keep the existing short-frame cap,
			// and allow only the configured speed * elapsed time on longer frames.
			const float step_limit=dt>0 && dt<=.15f ? std::max(max_step,max_speed*dt) : max_step;
			const bool contact=contact_.update(impact,points,at,step_limit,observed ? &observed->impact : nullptr);
			if (observed)
			{
				observed->examined=true; observed->eligible=true; observed->world_speed_ok=moving;
				observed->world_speed=sampled_ && dt>0 ? distance/dt : 0;
				const auto& i=observed->impact;
				observed->reason=!i.sampled ? slap_reason::first_sample :
					i.dt<=0 || i.dt>.15f ? slap_reason::sample_gap : i.max_step>i.step_limit ? slap_reason::jump :
					!i.armed_before ? slap_reason::separate : !i.radius_ok ? slap_reason::outside :
					!i.speed_ok ? slap_reason::speed : !i.direction_ok ? slap_reason::direction :
					!i.travel_ok ? slap_reason::travel : !i.above ? slap_reason::below :
					!moving ? (observed->world_speed<impact.min_speed ? slap_reason::world_slow : slap_reason::world_fast) : slap_reason::candidate;
			}
			at_=at; previous_=hand_world; sampled_=true;
			return contact && moving;
		}
	private:
		swept_impact<ContactCount> contact_{};
		vec previous_{};
		clock::time_point at_{};
		bool sampled_{};
	};
	class handle_slap
	{
	public:
		using clock=std::chrono::steady_clock;
		void reset() noexcept { sweep_.reset(); }
		bool update(const handle_catch& p,const handle_catch_input& in,clock::time_point at,float max_step,slap_observation* observed=nullptr) noexcept
		{return update(p.slap,p.max_slap_speed,in,at,max_step,observed);}
		bool update(const impact_profile& impact,float max_speed,const handle_catch_input& in,clock::time_point at,float max_step,slap_observation* observed=nullptr) noexcept
		{return sweep_.update(impact,max_speed,in.slap_points,in.hand_world,at,max_step,observed);}
	private:
		hand_slap_sweep<hands::hand_contact_count> sweep_;
	};
}
