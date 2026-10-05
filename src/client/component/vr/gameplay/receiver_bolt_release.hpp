#pragma once
#include "handle_catch.hpp"
#include <string_view>

namespace vr::gameplay::weapons::physical_reload
{
	struct receiver_bolt_release
	{
		vec centre{}; // Authored gun-local native units; never mirrored with the hand.
		impact_profile impact{.025f,.07f,.5f,.025f,{0,-1,0},.5f,true};
		float max_speed{8.f};
		std::string_view visual_bone{}; // Optional audited paddle group; never the receiver root.
	};
	inline bool valid(const receiver_bolt_release& p) noexcept
	{
		for(float x:p.centre)if(!std::isfinite(x) || std::abs(x)>1000)return false;
		handle_catch shared;shared.slap=p.impact;shared.max_slap_speed=p.max_speed;
		return valid(shared) && p.impact.radius>=palm_grid_cover_m;
	}
	class receiver_paddle_slap
	{
	public:
		using clock=std::chrono::steady_clock;
		void reset() noexcept { sweep_.reset(); palm_present_=false; }
		bool update(const impact_profile& impact,float max_speed,const handle_catch_input& in,clock::time_point at,float max_step,slap_observation* observed=nullptr) noexcept
		{
			if(!valid(in) || (in.palm && impact.radius<palm_grid_cover_m)){reset();return false;}
			// One detector owns all fingers and the complete palm. Independent
			// palm/finger histories could retry a failed write without separation.
			if(palm_present_!=in.palm.has_value())sweep_.reset();
			palm_present_=in.palm.has_value();
			std::array<vec,hands::hand_contact_count+palm_contact_count> points;
			std::copy(in.slap_points.begin(),in.slap_points.end(),points.begin());
			if(in.palm)sample_palm(*in.palm,[&](size_t i,vec point){points[hands::hand_contact_count+i]=point;});
			else std::fill(points.begin()+hands::hand_contact_count,points.end(),in.slap_points.back());
			return sweep_.update(impact,max_speed,points,in.hand_world,at,max_step,observed);
		}
	private:
		hand_slap_sweep<hands::hand_contact_count+palm_contact_count> sweep_;
		bool palm_present_{};
	};
}
