#pragma once
#include "physical_reload_geometry.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	struct well_motion {vec previous{};bool contact{},withdraw{};};
	struct well_decision {bool insert{};const char* reason{};};
	// Shared by ordinary held magazines and independently recovered containers.
	template<class Profile> inline well_decision advance_well(const Profile& p,well_motion& s,
		vec tip,float alignment,bool occupied,bool delayed=false)noexcept
	{
		for(float x:tip)if(!std::isfinite(x)){s.contact=false;s.withdraw=true;return {false,"invalid magazine tip"};}
		if(!std::isfinite(alignment)){s.contact=false;s.withdraw=true;return {false,"invalid magazine angle"};}
		const bool aligned=alignment>=p.insertion_cosine;
		const bool continuous=hands::length(hands::sub(tip,s.previous))<=p.max_contact_step;
		const bool nearby=sweep_well(tip,tip,p.well_radius+p.well_release_margin,
			p.well_contact_depth+p.well_release_margin,p.well_capture_below+p.well_release_margin);
		const bool within=sweep_well(tip,tip,p.well_radius+p.well_withdraw_margin,
			p.well_contact_depth+p.well_withdraw_margin,p.well_capture_below+p.well_withdraw_margin);
		if(!continuous)
		{
			s.contact=false;s.withdraw=within;s.previous=tip;
			return {false,within?"magazine teleport; withdraw before contact":"magazine outside jump; contact history rebased"};
		}
		if(s.withdraw)
		{
			s.contact=false;if(!within)s.withdraw=false;s.previous=tip;
			return {false,"magazine requires exit and fresh contact"};
		}
		if(delayed){s.contact=false;s.previous=tip;return {false,"post-strike insertion delay"};}
		if(!nearby)s.contact=false;
		if(aligned && nearby && sweep_well(s.previous,tip,p.well_radius,p.well_contact_depth,p.well_capture_below))s.contact=true;
		s.previous=tip;
		if(!occupied && aligned && s.contact)return {true,"magazine insertion contact"};
		return {false,s.contact?(occupied?"magazine staged at occupied well":"staged magazine requires alignment"):
			occupied?"well occupied":!aligned?"magazine angle rejected":"magazine outside mouth volume"};
	}
}
