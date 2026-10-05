#pragma once
#include "cylinder_feed.hpp"
#include "component/vr/digital_button_gate.hpp"
#include "hands/pose_solver.hpp"
#include "hand_interaction/access.hpp"

namespace vr::gameplay::weapons::cylinder
{
	using clock = controller_input::clock;
	struct tuning
	{
		float opening_seconds{.22f}, closing_seconds{.16f};
		float down_cosine{.5f}, up_cosine{.35f}, orientation_hysteresis{.08f};
		float waist_radius{.22f}, contact_radius{.09f}, contact_behind{.16f}, contact_inside{.04f};
		float alignment_cosine{.55f};
		float twist_angle{.35f}, twist_speed{12.f}, twist_window{.22f}, twist_max_speed{60.f};
		float twist_dominance{.5f};
		float twist_fast_angle{.25f}; // travel actually completed above the speed threshold
	};
	struct geometry
	{
		bool valid{};
		std::uint32_t weapon{};
		std::uint64_t instance_generation{}, reference_generation{}, input_sequence{};
		float waist_distance{}, opening_up{}, alignment{};
		hands::vec loader_in_face{}; // metres; +Z into cylinder, -Z behind its opening
	};
	inline bool finite(hands::vec v) noexcept
	{ return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
	inline bool valid(const tuning& p) noexcept
	{
		const float positive[]{p.opening_seconds,p.closing_seconds,p.waist_radius,p.contact_radius,
			p.contact_behind,p.contact_inside,p.twist_angle,p.twist_speed,p.twist_window,p.twist_fast_angle};
		for (auto v : positive) if (!std::isfinite(v) || v <= 0 || v > 30) return false;
		return p.up_cosine > 0 && p.up_cosine <= 1 && p.down_cosine > 0 && p.down_cosine <= 1 &&
			p.orientation_hysteresis >= 0 && p.orientation_hysteresis < std::min(p.up_cosine,p.down_cosine) &&
			p.alignment_cosine > 0 && p.alignment_cosine <= 1 &&
			std::isfinite(p.twist_max_speed) && p.twist_max_speed > p.twist_speed && p.twist_max_speed<=100 &&
			p.twist_dominance>=.5f && p.twist_dominance<=1 && p.twist_window<=.5f && p.twist_fast_angle<=p.twist_angle;
	}
	inline bool contact(const tuning& p, const geometry& g) noexcept
	{
		const auto v = g.loader_in_face;
		return finite(v) && g.alignment >= p.alignment_cosine &&
			v[0]*v[0]+v[1]*v[1] <= p.contact_radius*p.contact_radius &&
			v[2] >= -p.contact_behind && v[2] <= p.contact_inside;
	}
	// Raw OpenVR/OpenXR active matrix (columns are basis vectors), deliberately
	// independent of H2 camera, artificial turn, controller translation and IK.
	inline bool tracking_rotation(const head_pose_bridge::matrix3& matrix, hands::quat& result) noexcept
	{
		std::array<hands::vec,3> axes{};
		for (int i=0;i<3;++i) for (int j=0;j<3;++j) axes[i][j] = matrix[j][i];
		for (const auto a : axes) if (!finite(a) || std::abs(hands::dot(a,a)-1) > .01f) return false;
		if (std::abs(hands::dot(axes[0],axes[1])) > .01f ||
			hands::length(hands::sub(hands::cross(axes[0],axes[1]),axes[2])) > .02f) return false;
		result = hands::from_axis(axes); return true;
	}
	class twist_gate
	{
		struct motion { clock::time_point at{}; float roll{}, travel{}, rate{}, seconds{}; };
		std::array<motion,64> window_{};
		std::size_t cursor_{}, count_{};
		hands::quat previous_{0,0,0,1};
		clock::time_point at_{};
		bool sampled_{}, eligible_{}, armed_{};
		std::uint64_t continuity_{};
		void clear_window() noexcept { count_=cursor_=0; }
	public:
		void reset() noexcept { *this = {}; }
		bool sample(const tuning& p, hands::quat q, clock::time_point now, bool eligible,hands::vec axis={0,0,-1},std::uint64_t continuity=0) noexcept
		{
			using namespace hands;
			float norm{};
			for (float x:q) { if (!std::isfinite(x)) { reset(); return false; } norm+=x*x; }
			if (!valid(p) || norm<.99f || norm>1.01f || !finite(axis) || std::abs(dot(axis,axis)-1)>.001f) { reset(); return false; }
			q=normalize(q);
			const float dt = std::chrono::duration<float>(now-at_).count();
			const bool changed=continuity_!=continuity;continuity_=continuity;
			if (!changed && sampled_ && eligible && eligible_ && dt==0) return false;
			// Generation records tracking continuity independently of server rate.
			// Unannotated inputs keep the original conservative gap contract.
			const bool baseline=changed || !sampled_ || !eligible_ || dt<=0 || dt>(continuity ? .5f : .15f);
			auto delta = normalize(multiply(conjugate(previous_),q));
			previous_ = q; at_ = now; sampled_=true; eligible_=eligible;
			if (!eligible || baseline)
			{ clear_window(); armed_=eligible; return false; }
			if (delta[3] < 0) for (auto& v : delta) v = -v;
			// A rightward roll looking along tracking aim -Z has the same sign for
			// both physical hands. Evaluate a bounded motion WINDOW, not a run of
			// individually pure/fast frames: real wrist flicks accelerate, mix a
			// little swing, decelerate, and may be consumed at a lower server rate.
			const float roll = 2*std::atan2(dot({delta[0],delta[1],delta[2]},axis),delta[3]);
			const float rate = roll/dt;
			const float full = 2*std::atan2(length({delta[0],delta[1],delta[2]}),delta[3]);
			if (!std::isfinite(rate) || full/dt > p.twist_max_speed)
			{ armed_ = false; clear_window(); return false; }
			// Rearm from quiet TOTAL motion before considering axis dominance.
			// Directionless low-amplitude tracking noise must not disarm the gate.
			if (full/dt < .8f) armed_=true;
			if (!armed_) return false;
			if (roll < -.02f) { clear_window(); return false; }
			window_[cursor_++%window_.size()]={now,roll,full,rate,dt};
			count_=std::min(count_+1,window_.size());
			float angle{}, travel{}, fast_angle{};
			for (std::size_t n=0;n<count_;++n)
			{
				const auto& m=window_[(cursor_-1-n)%window_.size()];
				const float age=std::chrono::duration<float>(now-m.at).count();
				if (age>=p.twist_window) break;
				const float fraction=std::clamp((p.twist_window-age)/m.seconds,0.f,1.f);
				angle+=m.roll*fraction; travel+=m.travel*fraction;
				if (m.rate>=p.twist_speed) fast_angle+=m.roll*fraction;
			}
			// Require a meaningful FAST segment and fire only while it is fast.
			// A tiny speed spike followed by a slow tilt cannot fund a later close.
			if (angle<p.twist_angle || rate<p.twist_speed || fast_angle<p.twist_fast_angle || angle<travel*p.twist_dominance) return false;
			armed_=false; clear_window(); return true;
		}
	};
	class controller
	{
		controller_input::digital_press_gate release_{}, draw_{};
		twist_gate twist_{};
		std::uint64_t sequence_{}, reference_{}, instance_{}, rear_revision_{};
		hand rear_{hand::none};
		clock::time_point action_at_{};
		controller_input::consumer_continuity continuity_;
		bool fire_armed_{}, down_{}, up_{};
		const char* decision_{"waiting for tracked scene"};
	public:
		bool fire_armed() const noexcept { return fire_armed_; }
		bool opening_up()const noexcept{return up_;}
		clock::time_point action_at() const noexcept { return action_at_; }
		const char* decision() const noexcept { return decision_; }
		float openness(const tuning& p, action phase, clock::time_point now) const noexcept
		{
			const float t = std::max(0.f,std::chrono::duration<float>(now-action_at_).count());
			return phase == action::open ? 1.f : phase == action::opening ? std::clamp(t/p.opening_seconds,0.f,1.f) :
				phase == action::closing ? 1.f-std::clamp(t/p.closing_seconds,0.f,1.f) : 0.f;
		}
		template<class Commit> bool interrupt(rules r, state& s, hand rear, Commit&& write)
		{
			release_ = {}; draw_ = {}; twist_.reset(); sequence_ = 0; fire_armed_ = down_ = up_ = false;
			if (s.loader_hand == hand::none) return true;
			const auto tx = plan(r,s,{operation::cleanup,s.weapon,s.instance_generation,s.revision,rear,s.loader_hand});
			if (!ammunition::commit(tx,write)) return false;
			s = tx.next; return true;
		}
		template<class Commit> void update(const tuning& p, rules r, state& s, const controller_input::frame& input,
			const hold& owner, const geometry& g, bool gameplay, clock::time_point now, Commit&& write, hand_interaction::access access = {})
		{
			const bool manipulation=access.manipulation,acquire=access.acquire;
			if (!valid(r,s) || !valid_hand(owner.holding_hand()) || owner.weapon != s.weapon)
			{ fire_armed_ = false; decision_ = "invalid owner/state"; return; }
			const int rear = static_cast<int>(owner.holding_hand()), off = 1-rear;
			auto take = input.trigger[off];if(access.release)take.down=false;
			const auto& fire = input.trigger[rear];
			hands::quat rotation{};
			const bool fresh = valid(p) && gameplay && input.focused && input.sequence && input.reference_generation &&
				now >= input.sampled_at && now-input.sampled_at <= std::chrono::milliseconds(150) &&
				input.grip[rear].valid && input.aim[rear].valid &&
				fire.active && input.secondary[rear].active &&
				tracking_rotation(input.aim[rear].tracking.orientation,rotation) &&
				g.valid && g.weapon == s.weapon && g.instance_generation == s.instance_generation &&
				g.reference_generation == input.reference_generation && g.input_sequence == input.sequence &&
				std::isfinite(g.waist_distance) && g.waist_distance >= 0 &&
				std::isfinite(g.opening_up) && std::abs(g.opening_up) <= 1.001f &&
				std::isfinite(g.alignment) && std::abs(g.alignment) <= 1.001f && finite(g.loader_in_face);
			if (!fresh)
			{ decision_ = "stale/inactive tracking or geometry"; (void)interrupt(r,s,owner.holding_hand(),write); return; }
			const bool changed = continuity_.update(input,now) || instance_ != s.instance_generation || rear_ != owner.holding_hand() || rear_revision_ != owner.rear_revision ||
				reference_ != input.reference_generation || input.sequence < sequence_ ||
				fire.generation != fire_generation_;
			if (changed && !interrupt(r,s,owner.holding_hand(),write)) return;
			if (sequence_ == input.sequence) return;
			sequence_ = input.sequence; reference_ = input.reference_generation; instance_ = s.instance_generation;
			rear_ = owner.holding_hand(); rear_revision_ = owner.rear_revision; fire_generation_ = fire.generation;
			const bool open_pressed = release_.consume(input.secondary[rear]) && owner.can_fire();
			const bool offhand_available = manipulation && input.grip[off].valid && input.aim[off].valid && take.active;
			const bool local_press=draw_.consume(take);
			const bool take_pressed = offhand_available && access.pinch.value_or(local_press);
			if (!offhand_available) draw_ = {};
			const auto apply = [&](operation op, hand actor) {
				auto request=cylinder::request{op,s.weapon,s.instance_generation,s.revision,owner.holding_hand(),actor};
				request.disposition=!offhand_available ? ammunition::disposition_reason::forced_cleanup :
					g.waist_distance<=p.waist_radius ? ammunition::disposition_reason::waist_return : ammunition::disposition_reason::deliberate_discard;
				request.preserve_discard=access.preserve_discard && op==operation::discard && offhand_available && !take.down &&
					request.disposition==ammunition::disposition_reason::deliberate_discard;
				const auto tx = plan(r,s,request);
				if (!ammunition::commit(tx,write)) { decision_ = "native compare/precondition rejected"; return false; }
				if (tx.next.phase != s.phase) action_at_ = now;
				s = tx.next; return true;
			};
			decision_ = "idle";
			if (open_pressed && s.phase == action::closed) (void)apply(operation::open,owner.holding_hand());
			const float elapsed = std::chrono::duration<float>(now-action_at_).count();
			if (s.phase == action::opening && elapsed >= p.opening_seconds) (void)apply(operation::opened,owner.holding_hand());
			if (s.phase == action::closing && elapsed >= p.closing_seconds) (void)apply(operation::closed,owner.holding_hand());
			down_ = g.opening_up <= -(p.down_cosine-(down_ ? p.orientation_hysteresis : 0));
			up_ = g.opening_up >= p.up_cosine-(up_ ? p.orientation_hysteresis : 0);
			// Current orientation only, AFTER full mechanical opening. No queued
			// gravity event and no dependency on animation/render/notetrack cadence.
			if (s.phase == action::open && down_ && !empty(s)) (void)apply(operation::clear,owner.holding_hand());
			if (s.loader_hand != hand::none && (!offhand_available || !take.down)) (void)apply(operation::discard,static_cast<hand>(off));
			else if (acquire && access.supply && take_pressed && take.down && s.loader_hand == hand::none && owner.support != static_cast<hand>(off) &&
				g.waist_distance <= p.waist_radius) (void)apply(operation::draw,static_cast<hand>(off));
			if (offhand_available && s.loader_hand != hand::none && s.held_rounds && take.down)
			{
				decision_ = s.phase != action::open ? "cylinder not fully open" : !empty(s) ? "live rounds or cases remain" :
					!up_ ? "opening not upward" : !contact(p,g) ? "loader outside/alignment rejected" : "loader contact eligible";
				if (s.phase == action::open && empty(s) && up_ && contact(p,g))
					(void)apply(operation::fill,static_cast<hand>(off));
			}
			if (twist_.sample(p,rotation,input.sampled_at,s.phase == action::open,{0,0,-1},input.continuity_generation)) (void)apply(operation::close,owner.holding_hand());
			if (!owner.can_fire() || s.phase != action::closed) fire_armed_ = false;
			else if (!fire.down) fire_armed_ = true;
		}
	private:
		std::uint64_t fire_generation_{};
	};
}
