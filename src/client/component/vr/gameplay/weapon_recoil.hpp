#pragma once
#include "weapon_holding.hpp"
#include "weapon_recoil_tuning.hpp"
#include "controller_stance.hpp"
#include "hands/pose_solver.hpp"
#include "../controller_input.hpp"
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace vr::gameplay::weapons::recoil
{
	using clock = controller_input::clock;
	enum class penalty_mode { all, long_weapons, off };
	inline constexpr float one_hand_multiplier = 4.f;
	inline constexpr float base_strength = 3.f;
	inline constexpr float max_climb_degrees = 55.f;
	inline constexpr float recovery_seconds = .22f;
	// Native BG_WeaponFireRecoil adds these pitch values to angular velocity.
	// Map a 50 ms kick impulse to the VR pose, then use the time-based recovery.
	inline constexpr float native_impulse_seconds = .05f;
	inline float posture_strength(controller_input::posture actual) noexcept
	{
		switch (actual)
		{
		case controller_input::posture::crouch: return 2.f;
		case controller_input::posture::prone: return 1.f;
		default: return base_strength; // Unknown posture must not grant a reduction.
		}
	}

	// H2 runtime values, verified from registered definitions: rifle=0,
	// sniper=1, MG=2, SMG=3, shotgun=4, pistol=5, grenade launcher=6, RPG=7.
	// The shared game::weapClass_t labels come from another engine version.
	// This policy is called only after an admitted firearm/projectile shot.
	inline bool long_weapon(int cls,std::string_view name={}) noexcept
	{
		const auto* tuning=tuning_for(name,cls);
		return ((cls >= 0 && cls <= 4) || cls == 6 || cls == 7) && !(tuning && tuning->short_weapon);
	}
	inline float multiplier(penalty_mode mode, bool supported, int cls,std::string_view name={},
		controller_input::posture actual=controller_input::posture::stand) noexcept
	{return actual!=controller_input::posture::prone && !supported && (mode == penalty_mode::all ||
		(mode == penalty_mode::long_weapons && long_weapon(cls,name))) ? one_hand_multiplier : 1.f;}
	inline float native_pitch(float endpoint_a, float endpoint_b,
		controller_input::posture actual=controller_input::posture::stand) noexcept
	{
		if (!std::isfinite(endpoint_a) || !std::isfinite(endpoint_b) ||
			std::abs(endpoint_a) > 10000 || std::abs(endpoint_b) > 10000) return 0;
		// Authored ranges may descend (M4: -10/-15) or cross zero (AK: 5/-15).
		// Use the expected absolute speed of that uniform range so signed native
		// gun motion becomes upward VR climb without cancelling mixed signs.
		const float low = std::min(endpoint_a, endpoint_b), high = std::max(endpoint_a, endpoint_b);
		const float speed = low < 0 && high > 0 ? (low*low + high*high)/(2*(high-low)) :
			(std::abs(low) + std::abs(high))*.5f;
		return std::min(speed * native_impulse_seconds, 8.f) * posture_strength(actual);
	}
	struct entry
	{
		weapon_identity id{};
		std::uint64_t rear_revision{}, reference{};
		clock::time_point at{};
		float degrees{};
	};
	class state
	{
	public:
		void shot(const hold& owner, std::uint64_t reference, clock::time_point at, float degrees) noexcept
		{
			if (!owner.can_fire() || !reference || !std::isfinite(degrees) || degrees <= 0) return;
			const std::lock_guard lock(mutex_);
			auto* target = &entries_[0];
			for (auto& value : entries_)
			{
				if (value.id == owner.id() && value.rear_revision == owner.rear_revision && value.reference == reference)
				{target = &value; break;}
				if (value.at < target->at) target = &value;
			}
			const float previous = target->id == owner.id() && target->rear_revision == owner.rear_revision &&
				target->reference == reference ? decayed(*target, at) : 0.f;
			*target = {owner.id(), owner.rear_revision, reference, at,
				std::min(max_climb_degrees, previous + degrees)};
		}
		float current(const hold& owner, std::uint64_t reference, clock::time_point at) const noexcept
		{
			if (!owner.can_fire() || !reference) return 0;
			const std::lock_guard lock(mutex_);
			for (const auto& value : entries_)
				if (value.id == owner.id() && value.rear_revision == owner.rear_revision && value.reference == reference)
					return decayed(value, at);
			return 0;
		}
		void clear() noexcept { const std::lock_guard lock(mutex_); entries_ = {}; }
	private:
		static float decayed(const entry& value, clock::time_point at) noexcept
		{
			if (at < value.at) return 0;
			const float elapsed = std::chrono::duration<float>(at - value.at).count();
			return elapsed >= 2.f ? 0.f : value.degrees * std::exp(-elapsed / recovery_seconds);
		}
		mutable std::mutex mutex_;
		std::array<entry, 16> entries_{}; // At most 15 physical inventory instances.
	};

	inline void apply_to_pose(const hands::rig& rig, std::span<hands::bone> solved,
		int rear, int support, const std::array<hands::vec,3>& body_axis, float degrees) noexcept
	{
		if (rig.count <= 0 || rig.count > 256 || rear < 0 || rear > 1 ||
			support < -1 || support > 1 || support == rear || rig.gun < 0 || rig.gun >= rig.count ||
			rig.arms[rear].wrist < 0 || rig.arms[rear].wrist >= rig.count ||
			solved.size() < std::size_t(rig.count) || !std::isfinite(degrees) || degrees <= 0) return;
		const auto radians = std::min(degrees, max_climb_degrees) * (3.14159265358979323846f / 180.f);
		const auto axis = hands::rotate(hands::normalize(solved[rig.gun].rotation), {0,-1,0});
		const float sine = std::sin(radians * .5f);
		const hands::quat turn{axis[0]*sine,axis[1]*sine,axis[2]*sine,std::cos(radians*.5f)};
		const auto pivot = solved[rig.arms[rear].wrist].position;
		std::array<hands::bone,256> arms{};
		if (support >= 0)
		{
			std::array<hands::anchor,2> targets;
			std::array<hands::vec,2> shoulders;
			for (int h=0;h<2;++h)
			{
				const auto a=rig.arms[h];
				if (a.wrist<0 || a.wrist>=rig.count || a.shoulder<0 || a.shoulder>=rig.count) return;
				targets[h]={solved[a.wrist].position,solved[a.wrist].rotation};
				shoulders[h]=solved[a.shoulder].position;
			}
			const auto wrist=rig.arms[support].wrist;
			const auto desired=hands::transformed(solved[wrist],pivot,pivot,turn);
			targets[support]={desired.position,desired.rotation};
			std::array<bool,2> limited{};
			if (!hands::solve_arms(rig,solved,targets,shoulders,body_axis,arms,limited)) return;
			// Keep the support contact exact even at maximum arm reach, matching
			// the existing physical-part constraint's forearm stretch policy.
			const auto correction=hands::sub(desired.position,arms[wrist].position);
			for (int i=0;i<rig.count;++i)
				if (!rig.weapon_bones[i] && hands::descendant(i,wrist,rig))
					arms[i].position=hands::add(arms[i].position,correction);
		}
		// Pitch the firing wrist and its authored fingers with the receiver.
		// Its tracked position stays the pivot. The attached support hand follows
		// the same rigid motion, with its shoulder/elbow solved by shared arm IK.
		// Raw controller targets and the free/other firing hand are not changed.
		for (int i = 0; i < rig.count; ++i)
			if (rig.weapon_bones[i] || hands::descendant(i,rig.arms[rear].wrist,rig))
				solved[i] = hands::transformed(solved[i], pivot, pivot, turn);
			else if (support>=0 && hands::descendant(i,rig.arms[support].shoulder,rig)) solved[i]=arms[i];
	}
	void confirmed_shot(const hold& owner, std::uint64_t reference, clock::time_point at,
		controller_input::posture actual) noexcept;
	float current_climb(const hold& owner, std::uint64_t reference, clock::time_point at) noexcept;
	void clear() noexcept;
}
