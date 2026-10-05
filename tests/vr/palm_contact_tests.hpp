#pragma once
#include "component/vr/gameplay/hand_rig_builder.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "marine_sniper_hand_data.hpp"
#include "component/vr/gameplay/empty_hand_pose.hpp"
#include "component/vr/gameplay/knife_profile.hpp"
#include "component/vr/gameplay/receiver_bolt_release.hpp"
#include <string>

namespace palm_contact_tests
{
	namespace h=vr::gameplay::hands;namespace p=vr::gameplay::weapons::physical_reload;
	struct hand_shape
	{
		std::array<h::vec,h::hand_contact_count> joints{};
		std::optional<p::contact_box> palm;
	};
	inline hand_shape shape(int side,bool fist,float units=39.37007874f)
	{
		const h::model_definition model{"viewhands_marine_sniper",0,67};
		const auto r=h::resolve_rig({&model,1},marine_sniper_data::bones,h::rig_kind::hands_only).layout;
		const auto& profile=vr::gameplay::hands::native_hand_schema::definition;
		const auto library=h::bind_poses(r,marine_sniper_data::bones,profile);
		std::array<h::bone,67> pose{};for(size_t i=0;i<pose.size();++i)pose[i]=marine_sniper_data::bones[i].bind;
		const auto gesture=fist?h::empty_hand::gesture::fist:h::empty_hand::gesture::relaxed;
		hand_shape out;
		if(!h::empty_hand::apply(r,library,profile,vr::hand(side),{true,gesture,h::empty_hand::target(gesture)},pose))return out;
		if(!h::contact_points(h::bind_contacts(r,marine_sniper_data::bones,side),pose,{},out.joints))return out;
		out.palm=p::palm_volume(out.joints,units);return out;
	}
	template<class Check> void run(Check check)
	{
		using namespace h;using namespace std::chrono_literals;
		constexpr float units=39.37007874f;const p::receiver_bolt_release policy{};
		int legacy_misses{};
		for(int side=0;side<2;++side)for(bool fist:{false,true})
		{
			const auto hand=shape(side,fist);check(hand.palm.has_value(),"captured native glove produces a bounded palm for either open hand or fist");if(!hand.palm)continue;
			const auto& palm=*hand.palm;
			// Heel/thenar, heel/ulnar edge, palm middle, knuckle roots and both sides.
			for(const vec fraction:{vec{-.9f,.7f,0},vec{-.9f,-.7f,0},vec{0,0,0},vec{.9f,0,0},vec{0,.9f,0},vec{0,-.9f,0},vec{0,0,.9f},vec{0,0,-.9f}})
			for(const quat turn:{quat{0,0,0,1},normalize({.3f,.5f,.2f,.8f})})
			{
				const vec at=p::box_point(palm.pose,{fraction[0]*palm.half[0],fraction[1]*palm.half[1],fraction[2]*palm.half[2]});
				p::receiver_paddle_slap detector;p::handle_slap legacy;bool accepted=false,old=false;p::slap_observation observed;
				for(int frame=0;frame<=24;++frame)
				{
					const float approach=.36f-frame*.015f;
					const anchor wrist{sub(vec{0,approach*units,0},rotate(turn,at)),turn};
					p::handle_catch_input input;input.valid=true;input.rotation=turn;input.hand_world=scale(wrist.position,1/units);
					for(size_t i=0;i<hand.joints.size();++i)input.slap_points[i]=scale(p::box_point(wrist,hand.joints[i]),1/units);
					input.palm=p::palm_relative_to_target(palm,wrist,{},units);
					const auto time=p::handle_slap::clock::time_point{1s}+frame*10ms;
					accepted|=detector.update(policy.impact,policy.max_speed,input,time,.25f,&observed);
					old|=legacy.update(policy.impact,policy.max_speed,input,time,.25f);
				}
				const auto context=std::string("palm surface side=")+std::to_string(side)+" fist="+std::to_string(fist)+" fraction="+std::to_string(fraction[0])+","+std::to_string(fraction[1])+","+std::to_string(fraction[2])+" reason="+std::to_string(int(observed.reason))+" armed="+std::to_string(observed.impact.armed_before);
				check(accepted,context.c_str());
				if(accepted && !old)++legacy_misses;
			}
			// The primitive must be independent of the model's arbitrary local axes.
			auto rotated=hand.joints;const auto q=normalize(quat{.4f,.2f,-.5f,.7f});
			for(auto& point:rotated)point=rotate(q,point);
			const auto other=p::palm_volume(rotated,units);
			check(other && length(sub(other->pose.position,rotate(q,palm.pose.position)))<.0001f && length(sub(other->half,palm.half))<.0001f,
				"palm dimensions and position follow anatomical axes on either hand");
			auto bad=hand.joints;bad[3][0]=NAN;check(!p::palm_volume(bad,units),"non-finite hand roots cannot create a palm volume");
			bad={};check(!p::palm_volume(bad,units),"collapsed anatomy fails closed");
			check(!p::palm_volume(hand.joints,0) && !p::palm_volume(hand.joints,NAN),"invalid world scale cannot create contacts");
		}
		check(legacy_misses>0,"native palm trajectories reproduce gaps in the original 21-point receiver detector");
		// Analytic worst-case coverage: every interior point is within the target
		// radius of a grid sample, including the centre of the largest grid cell.
		float gap2{};for(size_t i=0;i<3;++i){const float half_step=p::maximum_palm_size_m[i]/(2*(p::palm_grid[i]-1));gap2+=half_step*half_step;}
		check(std::sqrt(gap2)<p::palm_grid_cover_m && p::palm_grid_cover_m<policy.impact.radius,
			"solid palm coverage has no unsampled holes at the unchanged 25 mm target radius");
	}
}
