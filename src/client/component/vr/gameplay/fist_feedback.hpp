#pragma once
#include "../controller_input.hpp"
#include "../hand.hpp"
#include <cmath>

namespace vr::gameplay::melee
{
	struct fist_contact
	{
		vr::hand actor{vr::hand::none};
		std::uint64_t revision{}, reference{}, continuity{}, sequence{};
		controller_input::digital_action grip{};
		controller_input::clock::time_point at{};
		std::array<float,3> position{};
	};
	inline bool fresh(const fist_contact& hit,const controller_input::frame& input,
		bool gameplay,bool empty,std::uint64_t revision,controller_input::clock::time_point now) noexcept
	{
		if(!gameplay || !empty || !vr::valid_hand(hit.actor) || !hit.revision || hit.revision!=revision ||
			!input.focused || !input.sequence || input.sequence<hit.sequence || input.orientation_settling ||
			hit.reference!=input.reference_generation || hit.continuity!=input.continuity_generation ||
			now<hit.at || now-hit.at>std::chrono::milliseconds(100) || now<input.sampled_at ||
			now-input.sampled_at>std::chrono::milliseconds(100))return false;
		const auto h=unsigned(hit.actor);const auto& grip=input.squeeze[h];
		if(!input.grip[h].valid || !input.aim[h].valid || !grip.active || !grip.down ||
			grip.generation!=hit.grip.generation || grip.presses!=hit.grip.presses || grip.releases!=hit.grip.releases)return false;
		for(float x:hit.position)if(!std::isfinite(x) || std::abs(x)>1e7f)return false;
		return true;
	}
}
