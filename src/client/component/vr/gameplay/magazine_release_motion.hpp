#pragma once
#include "physical_reload_profile.hpp"
#include "falling_trajectory.hpp"
#include <optional>

namespace vr::gameplay::weapons
{
	// Deterministic response to the accepted material-point strike.
	// Hardware opts in separately from contact admission and ammunition mechanics.
	inline motion::release_impulse magazine_release_impulse(const reload_profile& p,
		const physical_reload::magazine_strike& strike,hands::anchor attached,float units)noexcept
	{
		using namespace hands;
		const auto* contact=p.magazine_contacts;const auto* policy=p.interaction.manual_magazine;
		if(!contact || !policy || !policy->spare_strike || !std::isfinite(units) || units<=0 || units>10000 ||
			!physical_reload::valid_box_pose({{},attached.rotation}) || contact->strike_regions.size()!=1 ||
			!physical_reload::valid(contact->strike_regions[0]))return {};
		const auto* second=strike.second_latch?contact->second_latch:nullptr;
		if(strike.second_latch?(!second || !second->transfers_impulse):!policy->latch_impulse)return {};
		const float speed=length(strike.velocity);
		if(!std::isfinite(speed) || speed<=0)return {};
		// Retain the full vector, with 80% transfer and a 3 m/s total-speed ceiling.
		const auto velocity=scale(strike.velocity,std::min(.8f,3.f/speed));
		const auto pivot=contact->strike_regions[0].pose.position;
		const auto centre=add(p.magazine_rest.position,rotate(p.magazine_rest.rotation,pivot));
		const auto lever=scale(sub(second?second->position:contact->latch,centre),1/units);
		// A bounded moment about the existing body enclosure, not a random spin.
		auto angular=scale(cross(lever,velocity),.6f/std::max(dot(lever,lever),.0025f));
		const float spin=length(angular);if(!std::isfinite(spin))return {};
		if(spin>6.f)angular=scale(angular,6.f/spin);
		const auto gun_rotation=normalize(multiply(attached.rotation,conjugate(p.magazine_rest.rotation)));
		return {scale(rotate(gun_rotation,velocity),units),rotate(gun_rotation,angular),pivot};
	}
	// Both recoverable items and cosmetic overflow use this release boundary.
	// Presence means an accepted strike, including a button with zero impulse.
	inline motion::flight magazine_release_flight(const reload_profile& p,hands::anchor world,float units,
		motion::clock::time_point at,const std::optional<motion::release_impulse>& strike)noexcept
	{
		motion::flight out;out.start=world;out.units=units;out.born=at;
		if(strike)
		{
			out.impulse=*strike;out.exit=world;out.detached=true;
		}
		else
		{
			out.rail=magazine_exit_translation(p,units);out.rail_seconds=p.presentation.magazine_exit_seconds;
			out.advance(at);
		}
		return out;
	}
}
