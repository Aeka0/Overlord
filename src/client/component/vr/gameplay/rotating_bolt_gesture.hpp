#pragma once
#include "manual_bolt.hpp"
#include "hands/pose_solver.hpp"
#include "weapon_holding.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::weapons::rotating_bolt
{
	struct profile
	{
		// Metres, gun-local pivot. Source j_bolt rotates around its local X axis.
		hands::vec pivot{};
		float radians{}, stroke{};
		hand actor{hand::right}; // none permits either hand; hardware stays on its actual side.
		bool release_assist{};
	};
	inline bool valid(const profile& p) noexcept
	{
		for (float x : p.pivot)
			if (!std::isfinite(x) || std::abs(x) > 2)
				return false;
		return std::isfinite(p.radians) && std::abs(p.radians) > .2f && std::abs(p.radians) < 2 &&
		       std::isfinite(p.stroke) && p.stroke > .01f && p.stroke < 1 && (valid_hand(p.actor) || p.actor==hand::none);
	}
	inline bool permits(const profile& p,hand actor)noexcept
	{return valid_hand(actor) && (p.actor==hand::none || p.actor==actor);}
	inline manual_bolt::target project(
	    const profile& p, const manual_bolt::state& s, hands::vec previous, hands::vec current) noexcept
	{
		manual_bolt::target out{s.lift, s.travel};
		if (!valid(p))
			return out;
		for (float x : previous)
			if (!std::isfinite(x))
				return out;
		for (float x : current)
			if (!std::isfinite(x))
				return out;
		const auto a = hands::sub(previous, p.pivot), b = hands::sub(current, p.pivot);
		if (s.travel == 0 && std::hypot(a[1], a[2]) > .015f && std::hypot(b[1], b[2]) > .015f)
		{
			const auto angle = std::atan2(a[1] * b[2] - a[2] * b[1], a[1] * b[1] + a[2] * b[2]);
			out.lift = std::clamp(s.lift + angle / p.radians, 0.f, 1.f);
			if (out.lift < .04f)
				out.lift = 0;
			if (out.lift > .96f)
				out.lift = 1;
		}
		if (s.lift == 1 && out.lift == 1)
		{
			out.travel = std::clamp(s.travel + (previous[0] - current[0]) / p.stroke, 0.f, 1.f);
			if (out.travel < .015f)
				out.travel = 0;
		}
		return out;
	}
	inline hands::anchor pose(hands::anchor rest, const profile& p, manual_bolt::target t, float units) noexcept
	{
		const float half = p.radians * t.lift * .5f;
		rest.rotation = hands::normalize(hands::multiply(rest.rotation, {std::sin(half), 0, 0, std::cos(half)}));
		rest.position[0] -= p.stroke * t.travel * units;
		return rest;
	}
	// Contact velocity is measured in gun space, so moving the whole rifle does
	// not count as closing intent. A recent sample survives one unchanged input
	// sample at release, but a pause, reversal or tracking jump cannot auto-lock.
	class release_motion
	{
		using clock=controller_input::clock;
		hands::vec previous_{},velocity_{};
		clock::time_point at_{},moving_at_{};
		bool valid_{};
	public:
		inline static constexpr float maximum_remaining_radians=40.f*3.14159265359f/180.f;
		inline static constexpr float minimum_release_speed=.25f; // metres/second at the handle contact
		void begin(hands::vec contact,clock::time_point now) noexcept
		{*this={};previous_=contact;at_=now;valid_=true;}
		void sample(hands::vec contact,clock::time_point now) noexcept
		{
			const float dt=std::chrono::duration<float>(now-at_).count();
			const auto delta=hands::sub(contact,previous_);const float distance=hands::length(delta);
			if(!valid_ || !std::isfinite(distance) || dt<.001f || dt>.15f || distance>.15f)
			{begin(contact,now);return;}
			if(distance>.000001f){velocity_=hands::scale(delta,1/dt);moving_at_=now;}
			previous_=contact;at_=now;
		}
		bool completes(const profile& p,const manual_bolt::state& s,clock::time_point now)const noexcept
		{
			if(!valid(p) || !p.release_assist || !s.returned || s.travel!=0 || !std::isfinite(s.lift) || s.lift<0 || s.lift>1 ||
				s.lift*std::abs(p.radians)>maximum_remaining_radians ||
				!valid_ || now<moving_at_ || now-moving_at_>std::chrono::milliseconds(35))return false;
			const float speed=hands::length(velocity_);
			const auto radius=hands::sub(previous_,p.pivot);
			const auto tangent=hands::scale(hands::vec{0,-radius[2],radius[1]},p.radians>0 ? -1.f : 1.f);
			const float arm=hands::length(tangent);
			return std::isfinite(speed) && speed>=minimum_release_speed && speed<=8.f && arm>.015f &&
				hands::dot(velocity_,tangent)>=.1f*speed*arm;
		}
	};
} // namespace vr::gameplay::weapons::rotating_bolt
