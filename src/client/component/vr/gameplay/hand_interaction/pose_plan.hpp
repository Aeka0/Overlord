#pragma once
#include "core.hpp"

namespace vr::gameplay::hand_interaction
{
	struct pose_plan
	{
		target driver{};recipe grasp{};capability abilities{};bool knife_attachment{},melee{true};
	};
	inline pose_plan compose_pose(hand actor,std::span<const session> sessions)noexcept
	{
		pose_plan result;unsigned best=100;
		const auto knife=std::find_if(sessions.begin(),sessions.end(),[&](const session& s)
		{return s && s.actor==actor && !s.settling && s.held.destination.provider==domain::knife &&
			s.held.purpose==role::knife && has(s.held.abilities,capability::melee);});
		for(const auto& s:sessions)if(s && s.actor==actor)
		{
			result.knife_attachment=result.knife_attachment || s.held.purpose==role::knife;
			const bool knife_magazine=knife!=sessions.end() && s.held.pose==recipe::knife_magazine && compatible(knife->held,s.held);
			if(s.held.destination.provider==domain::grenade || (s.held.destination.provider==domain::special && !has(s.held.abilities,capability::melee)) || s.held.destination.provider==domain::vehicle || s.settling || s.held.purpose==role::part ||
				(s.held.purpose==role::supply && !knife_magazine))result.melee=false;
			const unsigned rank=s.held.purpose==role::part || s.held.purpose==role::supply?0:s.held.purpose==role::firing || s.held.purpose==role::foregrip?1:s.held.destination.provider==domain::carry?2:s.held.purpose==role::knife?3:4;
			if(rank<best){best=rank;result.driver=s.held.destination;result.grasp=s.held.pose;result.abilities=s.held.abilities;}
		}
		return result;
	}
}
