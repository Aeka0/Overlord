#pragma once
#include "hand_interaction/pose_plan.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"

namespace vr::gameplay::hands::empty_hand
{
	// Head gestures reserve input but retain the tracked wrist and standard
	// pinch/fist presentation; they do not supply an authored item-hand pose.
	inline bool authored_pose(const hand_interaction::pose_plan& plan)noexcept
	{return plan.driver && plan.driver.provider!=hand_interaction::domain::gesture;}
	// Empty wrists have the current hand model's neutral basis. A held weapon's
	// grip/forearm/roll never owns an unoccupied hand; interactions retain theirs.
	inline bool orient_wrist(const rig& r,vr::hand actor,const hand_interaction::pose_plan& plan,bool occupied,
		quat tracked,quat neutral,std::span<bone> solved) noexcept
	{
		if(occupied || authored_pose(plan) || plan.knife_attachment || !vr::valid_hand(actor) || r.count<=0 || r.count>256 || solved.size()<std::size_t(r.count))return false;
		const auto wrist=r.arms[unsigned(actor)].wrist;if(wrist<0 || wrist>=r.count || r.weapon_bones[wrist])return false;
		for(const auto q:{tracked,neutral}){float n{};for(float x:q){if(!std::isfinite(x))return false;n+=x*x;}if(n<.5f || n>1.5f)return false;}
		for(int i=0;i<r.count;++i)if(r.parent[i]<-1 || r.parent[i]>=i)return false;
		hands::pose_math::move_part(r,wrist,{solved[wrist].position,normalize(multiply(tracked,neutral))},solved);
		return true;
	}
	// Empty-hand gestures use clicks. Optional trigger contact belongs to the
	// held weapon's index pose and does not change these four defaults.
	enum class gesture { relaxed, point, pinch, fist };
	struct curls { float thumb{}, index{}, remaining{}; };
	struct presentation
	{
		bool enabled{};
		gesture requested{gesture::relaxed};
		curls fingers{};
	};
	inline gesture select(bool grip, bool trigger) noexcept
	{return grip ? (trigger ? gesture::fist : gesture::point) : (trigger ? gesture::pinch : gesture::relaxed);}
	inline curls target(gesture value) noexcept
	{
		switch (value)
		{
		case gesture::point: return {.65f, 0, 1};
		case gesture::pinch: return {1, 1, 0};
		case gesture::fist: return {1, 1, 1};
		default: return {};
		}
	}
	class controller
	{
		using clock = controller_input::clock;
		struct state
		{
			curls fingers{};
			std::uint64_t sequence{}, reference{};
			clock::time_point at{};
			bool tracked{};
		};
		std::array<state, 2> hands_{};
	public:
		// Time-based exponential blending: 90% of a change takes about 150 ms,
		// independent of headset refresh rate. No input edge is consumed here.
		inline static constexpr float half_life_seconds = .045f;
		presentation update(const controller_input::frame& input, vr::hand actor,
			const hand_interaction::pose_plan& plan = {}, bool occupied = false) noexcept
		{
			if (!vr::valid_hand(actor)) return {};
			const auto h = unsigned(actor);
			auto& s = hands_[h];
			const bool tracked = input.sequence && input.reference_generation && input.focused && input.grip[h].valid;
			const auto requested = select(input.squeeze[h].active && input.squeeze[h].down,
				input.trigger[h].active && input.trigger[h].down);
			if (!tracked)
			{
				// Freeze the last visible articulation while tracking is missing.
				// Recovery rebases time; an unseen multi-second gap cannot snap it.
				s.tracked = false;
				return {false, requested, s.fingers};
			}
			const bool continuity = s.tracked && s.reference == input.reference_generation &&
				input.sequence > s.sequence && input.sampled_at >= s.at &&
				input.sampled_at - s.at <= std::chrono::milliseconds(150);
			if (continuity)
			{
				const float dt = std::chrono::duration<float>(input.sampled_at - s.at).count();
				const float weight = -std::expm1(-.6931471805599453f * dt / half_life_seconds);
				const auto desired = target(requested);
				s.fingers.thumb += (desired.thumb - s.fingers.thumb) * weight;
				s.fingers.index += (desired.index - s.fingers.index) * weight;
				s.fingers.remaining += (desired.remaining - s.fingers.remaining) * weight;
			}
			// Multiple model/eye draws of the same input snapshot do not animate
			// twice. An out-of-order/recentered snapshot only restarts the clock.
			s.sequence = input.sequence; s.reference = input.reference_generation;
			s.at = input.sampled_at; s.tracked = true;
			return {!occupied && !authored_pose(plan) && !plan.knife_attachment, requested, s.fingers};
		}
	};
	inline float amount(std::string_view joint, const curls& values) noexcept
	{
		if (joint.starts_with("j_thumb_")) return values.thumb;
		if (joint.starts_with("j_index_")) return values.index;
		return values.remaining;
	}
	// Reuse a witnessed closed left hand and the destination model's relaxed
	// bind pose. The right hand uses the existing anatomical mirror conversion;
	// neither a weapon's grip animation nor guessed joint axes define open hands.
	// Only fingers are written: wrist ownership remains with the pose plan.
	template<class Pose>
	inline bool apply(const rig& r, const pose_library& library, const Pose& closed,
		vr::hand actor, const presentation& value, std::span<bone> solved,
		std::span<const quat> initial_from_rest = {}, float target_weight = 1) noexcept
	{
		if (!value.enabled || !vr::valid_hand(actor) || !library.valid || r.count <= 0 || r.count > 256 ||
			solved.size() < std::size_t(r.count) || !std::isfinite(target_weight) || target_weight < 0 || target_weight > 1 ||
			(!initial_from_rest.empty() && initial_from_rest.size() < closed.fingers.size())) return false;
		for (int i = 0; i < r.count; ++i)
			if (r.parent[i] < -1 || r.parent[i] >= i) return false;
		for (const float v : {value.fingers.thumb, value.fingers.index, value.fingers.remaining})
			if (!std::isfinite(v) || v < 0 || v > 1) return false;
		const auto h = unsigned(actor);
		if (r.arms[h].wrist < 0 || r.arms[h].wrist >= r.count) return false;
		std::array<hands::joint_pose, 64> joints{};
		std::size_t count{};
		for (int i = 0; i < r.count; ++i)
		{
			const int index = library.finger[i], parent = r.parent[i];
			if (index < 0 || !descendant(i, r.arms[h].wrist, r)) continue;
			if (std::size_t(index) >= closed.fingers.size() || parent < 0 || parent >= i || count == joints.size()) return false;
			auto rotation = closed.fingers[index].rotation;
			if (h == 1)
			{
				const int other = library.opposite[i];
				if (other < 0 || other >= r.count || library.finger[other] < 0 ||
					std::size_t(library.finger[other]) >= closed.fingers.size()) return false;
				rotation = hands::pose_mirror::local_rotation(library, i, parent, closed.fingers[library.finger[other]].rotation);
			}
			const auto name = closed.fingers[index].name;
			rotation = blend_quat(library.rest_local[i].rotation, rotation, amount(name, value.fingers));
			if (!initial_from_rest.empty())
				rotation = blend_quat(normalize(multiply(library.rest_local[i].rotation, initial_from_rest[index])), rotation, target_weight);
			joints[count++] = {name, rotation};
		}
		if (!count) return false;
		hands::pose_math::fingers(r, library, closed, {joints.data(), count}, int(h), solved);
		return true;
	}
	class pose_controller
	{
		using clock = controller_input::clock;
		struct hand_state
		{
			std::array<quat, 64> from_rest{};
			const joint_pose* profile{};
			std::uint64_t sequence{}, reference{};
			clock::time_point at{};
			float weight{};
			bool observed{}, empty{};
		};
		controller gestures_{};
		std::array<hand_state, 2> hands_{};
		template<class Pose>
		static bool observe(hand_state& s, const rig& r, const pose_library& library,
			const Pose& profile, unsigned hand, std::span<const bone> solved) noexcept
		{
			if (!library.valid || r.count <= 0 || r.count > 256 || solved.size() < std::size_t(r.count) ||
				profile.fingers.empty() || profile.fingers.size() > s.from_rest.size() ||
				r.arms[hand].wrist < 0 || r.arms[hand].wrist >= r.count) return false;
			for (int i = 0; i < r.count; ++i)
				if (r.parent[i] < -1 || r.parent[i] >= i) return false;
			auto captured = s.from_rest;
			bool found{};
			for (int i = 0; i < r.count; ++i)
			{
				const int index = library.finger[i], parent = r.parent[i];
				if (index < 0 || !descendant(i, r.arms[hand].wrist, r)) continue;
				if (std::size_t(index) >= profile.fingers.size() || parent < 0 || parent >= i) return false;
				const auto local = normalize(multiply(conjugate(normalize(solved[parent].rotation)), normalize(solved[i].rotation)));
				// Store articulation relative to this model's own bind frame. If
				// a receiver change swaps the hands model, retain the gesture while
				// rebuilding on the new model's rest frame and bone lengths.
				captured[index] = normalize(multiply(conjugate(library.rest_local[i].rotation), local));
				found = true;
			}
			if (!found) return false;
			s.from_rest = captured; s.profile = profile.fingers.data(); s.observed = true;
			s.empty = false; s.weight = 0;
			return true;
		}
	public:
		// Call after all entity pose providers, including knife co-grasps. An
		// occupied hand is observed but never overwritten, so releasing a part
		// starts from the hand that was actually visible in the previous frame.
		template<class Pose>
		bool present(const rig& r, const pose_library& library, const Pose& closed,
			const controller_input::frame& input, vr::hand actor, const hand_interaction::pose_plan& plan,
			bool occupied, std::span<bone> solved) noexcept
		{
			if (!vr::valid_hand(actor)) return false;
			const auto h = unsigned(actor);
			auto& s = hands_[h];
			const auto value = gestures_.update(input, actor, plan, occupied);
			if (occupied || authored_pose(plan) || plan.knife_attachment)
			{
				(void)observe(s, r, library, closed, h, solved);
				return false;
			}
			if (!value.enabled) { s.empty = false; return false; }
			if ((!s.observed || s.profile != closed.fingers.data()) && !observe(s, r, library, closed, h, solved)) return false;
			const bool continuity = s.empty && s.reference == input.reference_generation && input.sequence > s.sequence &&
				input.sampled_at >= s.at && input.sampled_at - s.at <= std::chrono::milliseconds(150);
			if (continuity)
			{
				const float dt = std::chrono::duration<float>(input.sampled_at - s.at).count();
				s.weight += (1 - s.weight) * -std::expm1(-.6931471805599453f * dt / controller::half_life_seconds);
			}
			s.sequence = input.sequence; s.reference = input.reference_generation; s.at = input.sampled_at; s.empty = true;
			return apply(r, library, closed, actor, value, solved, s.from_rest, s.weight);
		}
	};
}
