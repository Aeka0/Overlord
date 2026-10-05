#pragma once
#include "weapon_holding.hpp"
#include "hand_pose_solver.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::vehicles
{
	struct rendered_gun
	{
		weapons::hold owner{};hands::anchor pose{};std::uint64_t reference{},sequence{},serial{};
		controller_input::clock::time_point at{};
	};
	struct effect_request
	{
		weapons::hold owner{};std::uint64_t reference{},sequence{},after_render{};
		controller_input::clock::time_point at{};bool brass{};
	};
	enum class effect_readiness { reject, wait, ready };
	inline effect_readiness effect_state(const effect_request& effect,const weapons::hold& owner,std::uint64_t reference,
		const rendered_gun& pose,controller_input::clock::time_point now) noexcept
	{
		if(!owner.can_fire() || effect.owner.id()!=owner.id() || effect.owner.rear_revision!=owner.rear_revision ||
			effect.reference!=reference || now<effect.at || now-effect.at>std::chrono::milliseconds(150))return effect_readiness::reject;
		return pose.owner.id()==effect.owner.id() && pose.owner.rear_revision==effect.owner.rear_revision && pose.reference==effect.reference &&
			pose.sequence>=effect.sequence && pose.serial>effect.after_render && now>=pose.at && now-pose.at<=std::chrono::milliseconds(150)?
			effect_readiness::ready:effect_readiness::wait;
	}
}
