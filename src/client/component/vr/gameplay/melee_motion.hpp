#pragma once
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::melee
{
	using namespace hands;
	using clock=controller_input::clock;
	enum class tool { fist, firearm, knife, shield, pickaxe };
	inline constexpr bool bladed(tool kind) noexcept {return kind==tool::knife || kind==tool::pickaxe;}
	inline constexpr auto cooldown=std::chrono::milliseconds(400);
	inline constexpr float minimum_speed(tool kind) noexcept
	{return bladed(kind) ? 1.f : kind==tool::fist ? 2.8f : kind==tool::firearm ? 3.5f : 3.f;}
	inline constexpr float minimum_travel(tool kind) noexcept
	{return bladed(kind) ? .04f : kind==tool::fist ? .08f : kind==tool::firearm ? .12f : .10f;}
	inline int scaled_damage(int native,tool kind,bool ragdoll_impact=false,int tactical_knife=0) noexcept
	{
		if(native<=0 || native>100000)return 0;
		// Use the actual chest knife's native budget for EVERY player melee tool.
		// This per-hit policy never mutates WeaponDef or latches a boosted value.
		if(ragdoll_impact && tactical_knife>0 && tactical_knife<=100000)return tactical_knife;
		return (bladed(kind) || kind==tool::shield)?native:std::max(1,(native+1)/3);
	}
	struct sample
	{
		std::array<vec,9> points{}; // Metres in the current calibrated body frame.
		std::size_t count{};
		std::uint64_t sequence{},reference{},identity{};
		clock::time_point at{};
		tool kind{};
		std::uint32_t weapon{}; // Separate carried blades from the chest knife even if lease counters coincide.
		anchor hand{}; // Body-relative metres/rotation; validates tracking independently of weapon length.
		std::uint64_t continuity{},pose_revision{};
	};
	enum class rejection {none, sample, history, tracking};
	struct sweep
	{
		sample from{},to{};float speed{},path{};bool valid{},armed{};
		unsigned armed_points{};
		rejection rejected{rejection::none};
		bool armed_at(std::size_t point) const noexcept {return point<to.count && (armed_points&(1u<<point));}
	};
	class motion
	{
		sample previous_{};
		float path_{};
		std::array<vec,9> strokes_{};
		void clear_stroke() noexcept {path_=0;strokes_={};}
	public:
		void reset() noexcept {previous_={};clear_stroke();}
		sweep advance(const sample& current) noexcept
		{
			sweep out;
			if (!current.count || current.count>current.points.size() || !current.sequence || !current.reference)
			{reset();out.rejected=rejection::sample;return out;}
			for (std::size_t i=0;i<current.count;++i) for (float x:current.points[i])
				if (!std::isfinite(x) || std::abs(x)>2.5f) {reset();out.rejected=rejection::sample;return out;}
			if (current.kind!=tool::knife)
			{
				for (float x:current.hand.position)
					if (!std::isfinite(x) || std::abs(x)>2.5f) {reset();out.rejected=rejection::sample;return out;}
				float norm{};for (float x:current.hand.rotation) norm+=x*x;
				if (!std::isfinite(norm) || std::abs(norm-1.f)>.01f) {reset();out.rejected=rejection::sample;return out;}
			}
			if (current.sequence==previous_.sequence) return out;
			const auto before=previous_;previous_=current;
			const float dt=std::chrono::duration<float>(current.at-before.at).count();
			if (before.count!=current.count || before.reference!=current.reference || before.identity!=current.identity || before.kind!=current.kind || before.weapon!=current.weapon ||
				before.continuity!=current.continuity || before.pose_revision!=current.pose_revision ||
				current.sequence<before.sequence || dt<.001f || dt>.12f) {clear_stroke();out.rejected=rejection::history;return out;}
			float distance{};
			for (std::size_t i=0;i<current.count;++i) distance=std::max(distance,length(sub(current.points[i],before.points[i])));
			const float speed=distance/dt;
			out.speed=speed;
			const float threshold=minimum_speed(current.kind);
			if (current.kind==tool::knife)
			{
				// Preserve the existing blade stroke and tracking policy.
				if (!std::isfinite(speed) || speed>12.f || distance>.45f) {clear_stroke();out.rejected=rejection::tracking;return out;}
				path_=speed>=threshold ? std::min(.5f,path_+distance) : 0;
				out={before,current,speed,path_,true,speed>=threshold && path_>=minimum_travel(current.kind)};
				if (out.armed) out.armed_points=(1u<<current.count)-1;
				return out;
			}
			// A long gun's tip can move much faster/further than its holding hand.
			// Reject discontinuous hand translation/rotation, not a fast tip or a
			// fixed per-tick distance (which rejects legitimate lower-rate sweeps).
			float rotation_dot{};
			const auto before_rotation=normalize(before.hand.rotation),current_rotation=normalize(current.hand.rotation);
			for (unsigned i=0;i<4;++i) rotation_dot+=before_rotation[i]*current_rotation[i];
			const float angle=2*std::acos(std::clamp(std::abs(rotation_dot),0.f,1.f));
			if (length(sub(current.hand.position,before.hand.position))>16.f*dt || angle>35.f*dt)
			{clear_stroke();out.rejected=rejection::tracking;return out;}
			out.from=before;out.to=current;out.speed=speed;out.valid=true;
			for (std::size_t i=0;i<current.count;++i)
			{
				const auto delta=sub(current.points[i],before.points[i]);
				if (length(delta)/dt<threshold) {strokes_[i]={};continue;}
				// Track each contact's useful displacement. Back-and-forth jitter
				// and the faster muzzle cannot arm a slow receiver or stock.
				if (dot(strokes_[i],delta)<0) strokes_[i]={};
				strokes_[i]=add(strokes_[i],delta);
				const float travel=length(strokes_[i]);out.path=std::max(out.path,travel);
				if (travel>=minimum_travel(current.kind)) out.armed_points|=1u<<i;
			}
			out.armed=out.armed_points!=0;return out;
		}
	};
	// One player-wide deadline closes same-frame and alternating-hand bypasses.
	// Contact persists independently of elapsed cooldown: withdraw before re-hit.
	class hit_gate
	{
		clock::time_point next_{};
		std::array<std::uint64_t,2> contact_{};
	public:
		void reset() noexcept {next_={};contact_={};}
		void clear_contacts() noexcept {contact_={};}
		bool touching(unsigned hand) const noexcept {return hand<2 && contact_[hand]!=0;}
		bool contact(unsigned hand,std::uint64_t target,bool armed,clock::time_point now) noexcept
		{
			if (hand>=2) return false;
			const auto previous=contact_[hand];contact_[hand]=target;
			if (!target || target==previous || !armed || now<next_) return false;
			next_=now+cooldown;return true;
		}
	};
}
