#pragma once
#include "../controller_input.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "weapon_holding.hpp"
#include "weapon_pose_library.hpp"

namespace vr::gameplay::hands::trigger_discipline
{
	inline bool eligible(const weapons::profile* profile, const weapons::hold& owner,
		unsigned mechanical_hands = 0) noexcept
	{
		return profile && !profile->defense &&
			(profile->reload || profile->tube || profile->cylinder || profile->break_open || profile->launcher || profile->id=="laserdesignator") && owner.can_fire() &&
			!(mechanical_hands & (1u << unsigned(owner.rear)));
	}

	class controller
	{
		controller_input::clock::time_point at_{};
		std::uint64_t sequence_{}, reference_{}, continuity_{};
		weapons::weapon_identity weapon_{};
		vr::hand hand_{vr::hand::none};
		std::uint64_t assembly_{}, rear_revision_{};
		float weight_{};
		bool tracked_{};
	public:
		inline static constexpr float half_life_seconds = .035f;
		void reset() noexcept { *this = {}; }
		float update(const controller_input::frame& input, const weapons::hold& owner,
			std::uint64_t assembly, bool enabled) noexcept
		{
			if (!enabled || !owner.can_fire() || !input.sequence || !input.focused ||
				!input.grip[unsigned(owner.rear)].valid || !input.trigger_touch[unsigned(owner.rear)].active)
			{ reset(); return 0; }
			const auto h = unsigned(owner.rear);
			const bool same = tracked_ && weapon_ == owner.id() && hand_ == owner.rear &&
				assembly_ == assembly && rear_revision_ == owner.rear_revision;
			if (!same) weight_ = 0;
			const bool continuous = same && input.reference_generation == reference_ &&
				input.continuity_generation == continuity_ && input.sequence > sequence_ &&
				input.sampled_at >= at_ && input.sampled_at - at_ <= std::chrono::milliseconds(150);
			if (continuous)
			{
				const float dt = std::chrono::duration<float>(input.sampled_at - at_).count();
				// A pressed trigger wins if a driver briefly reports conflicting contact.
				const float target = input.trigger_touch[h].down || (input.trigger[h].active && input.trigger[h].down) ? 0.f : 1.f;
				const float half_life = target == 0 ? .012f : half_life_seconds;
				weight_ += (target - weight_) * -std::expm1(-.6931471805599453f * dt / half_life);
			}
			at_ = input.sampled_at; sequence_ = input.sequence; reference_ = input.reference_generation;
			continuity_ = input.continuity_generation; weapon_ = owner.id(); hand_ = owner.rear;
			assembly_ = assembly; rear_revision_ = owner.rear_revision; tracked_ = true;
			return weight_;
		}
	};

	inline vec safe_direction(unsigned hand, const pose_library& library) noexcept
	{return unit(library.safe_index->hands[hand].direction);}

	inline bool apply(const rig& r, const pose_library& library, const weapons::profile& profile,
		vr::hand actor, float weight, std::span<bone> solved) noexcept
	{
		if (!vr::valid_hand(actor) || !library.valid || r.count <= 0 || r.count > 256 ||
			solved.size() < std::size_t(r.count) || r.gun < 0 || r.gun >= r.count ||
			!std::isfinite(weight) || weight <= 0 || weight > 1 || !library.safe_index) return false;
		const auto h = unsigned(actor);
		const auto& tuning = library.safe_index->hands[h];
		if (!std::isfinite(tuning.curl) || tuning.curl < 0 || tuning.curl > 1) return false;
		for (float v : tuning.direction) if (!std::isfinite(v)) return false;
		if (length(tuning.direction) < 1e-5f) return false;
		const int wrist = r.arms[h].wrist;
		if (wrist < 0 || wrist >= r.count) return false;
		for (int i = 0; i < r.count; ++i)
			if (r.parent[i] < -1 || r.parent[i] >= i) return false;
		const std::array<std::string_view, 3> names = h == 0
			? std::array<std::string_view, 3>{"j_index_le_0", "j_index_le_1", "j_index_le_2"}
			: std::array<std::string_view, 3>{"j_index_ri_0", "j_index_ri_1", "j_index_ri_2"};
		std::array<int, 3> nodes{-1, -1, -1};
		for (int i = 0; i < r.count; ++i)
		{
			const int index = library.finger[i];
			if (index < 0) continue;
			if (std::size_t(index) >= profile.fingers.size()) return false;
			for (unsigned j = 0; j < nodes.size(); ++j)
				if (profile.fingers[index].name == names[j])
				{
					if (nodes[j] >= 0 || r.weapon_bones[i] || !descendant(i, wrist, r)) return false;
					nodes[j] = i;
				}
		}
		if (nodes[0] < 0 || nodes[1] < 0 || nodes[2] < 0 || r.parent[nodes[0]] != wrist ||
			r.parent[nodes[1]] != nodes[0] || r.parent[nodes[2]] != nodes[1]) return false;
		const auto valid_rotation = [](quat q) {
			float n{}; for (float x : q) { if (!std::isfinite(x)) return false; n += x*x; }
			return n > .5f && n < 1.5f;
		};
		if (!valid_rotation(solved[r.gun].rotation) || !valid_rotation(solved[wrist].rotation)) return false;
		for (int node : nodes)
		{
			if (!valid_rotation(solved[node].rotation) || !valid_rotation(library.rest_local[node].rotation)) return false;
			for (float x : library.rest_local[node].position) if (!std::isfinite(x)) return false;
		}
		// Open distal joints only towards their incoming segment, never past it.
		// Preserving the native bend plane avoids reverse flexion and twist from
		// independently aiming every joint at the same world-space direction.
		const auto direction = rotate(normalize(solved[r.gun].rotation), safe_direction(h, library));
		std::array<hands::joint_pose, 3> joints{};
		for (unsigned j = 0; j < nodes.size(); ++j)
		{
			const int node = nodes[j];
			const auto axis = j < 2 ? library.rest_local[nodes[j+1]].position : library.rest_local[nodes[1]].position;
			if (length(axis) < 1e-5f || !std::isfinite(length(axis))) return false;
			const auto current = normalize(solved[node].rotation);
			const auto original_local = normalize(multiply(conjugate(normalize(solved[r.parent[node]].rotation)), current));
			quat target;
			if (j == 0)
				target = normalize(multiply(conjugate(normalize(solved[wrist].rotation)),
					multiply(from_to(rotate(current, axis), direction), current)));
			else
			{
				const auto incoming = unit(library.rest_local[node].position);
				const auto outgoing = unit(rotate(original_local, axis));
				const auto retained = blend_quat({0,0,0,1}, from_to(incoming, outgoing), tuning.curl);
				target = normalize(multiply(from_to(outgoing, rotate(retained, incoming)), original_local));
			}
			joints[j] = {names[j], blend_quat(original_local, target, weight)};
		}
		hands::pose_math::fingers(r, library, profile, joints, int(h), solved);
		return true;
	}
}
