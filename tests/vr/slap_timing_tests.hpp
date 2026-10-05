#pragma once
#include "component/vr/gameplay/handle_catch.hpp"

namespace slap_timing_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::hands;
		namespace p=vr::gameplay::weapons::physical_reload;
		const p::handle_catch tuning;
		const auto time=[](double seconds) -> std::chrono::steady_clock::time_point
		{
			return std::chrono::steady_clock::time_point(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds)));
		};
		// Recorded MP5K contact #7, 2026-09-10: all motion gates passed,
		// but a 68.4718 ms sample crossed the old fixed 25 cm step cap.
		// Only this point and the wrist speed were exported, not a full pose replay.
		{
			p::handle_slap current; p::swept_impact<hand_contact_count> fixed;
			p::handle_catch_input input; input.valid=true; input.slap_points.fill({1,0,1});
			p::slap_observation observed; p::impact_observation old;
			input.slap_points[7]={-.185587f,.0389072f,.251883f};
			current.update(tuning,input,time(1),.25f); fixed.update(tuning.slap,input.slap_points,time(1),.25f);
			input.slap_points[7]={-.099154f,-.0045773f,.200531f};
			current.update(tuning,input,time(1.033),.25f); fixed.update(tuning.slap,input.slap_points,time(1.033),.25f);
			input.slap_points[7]={.0255932f,.00929985f,-.0584546f};
			input.hand_world={2.07305f*.0684718f,0,0};
			const bool accepted=current.update(tuning,input,time(1.1014718),.25f,&observed);
			const bool previously=fixed.update(tuning.slap,input.slap_points,time(1.1014718),.25f,&old);
			check(!previously && !old.continuous && old.radius_ok && old.speed_ok && old.direction_ok && old.travel_ok,
				"recorded normal slap reproduces the old fixed-distance false rejection");
			check(accepted && observed.impact.point==7 && observed.impact.continuous && observed.world_speed_ok &&
				std::abs(observed.impact.step_limit-.5477744f)<.0001f,"elapsed-time step allowance admits the recorded segment within the unchanged speed bound");
		}
		for (int hz:{15,20,30,60,90})
		{
			p::handle_slap gesture; p::handle_catch_input input; input.valid=true; bool accepted=false;
			for (int frame=0;frame<30 && !accepted;++frame)
			{
				const float z=.31f-5.f*frame/hz;
				input.slap_points.fill({0,0,z}); input.hand_world={0,0,z};
				accepted=gesture.update(tuning,input,time(1.+double(frame)/hz),.25f);
			}
			check(accepted,"the same five-metre-per-second slap works across 15-90 Hz consumed samples");
		}
		{
			p::handle_slap current,old; auto anchored=tuning; anchored.slap.follow_approach_peak=false;
			p::handle_catch_input input; input.valid=true;
			bool accepted=false,previously=false; int frame=0; p::slap_observation observation,previous;
			for (float z:{-.20f,-.15f,-.10f,-.05f,0.f,.05f,.10f,.15f,.10f,.05f})
			{
				input.slap_points.fill({0,0,z}); input.hand_world={0,0,z};
				const auto at=time(1.+.02*frame++);
				accepted=current.update(tuning,input,at,.25f,&observation) || accepted;
				previously=old.update(anchored,input,at,.25f,&previous) || previously;
			}
			check(accepted && !previously && previous.reason==p::slap_reason::travel && observation.impact.travel_ok,
				"lifting from below then slapping uses the high point instead of subtracting the entire lift");
			check(std::abs(observation.impact.start[2]-.15f)<1e-5f && std::abs(observation.impact.travel-.10f)<1e-5f,
				"diagnostic reports the actual wind-up peak and downward displacement");
		}
		// Rising hand must not become an upward hit, and small oscillations must
		// never accumulate enough travel by summing each downward half-cycle.
		{
			p::handle_slap gesture; p::handle_catch_input input; input.valid=true;
			input.slap_points.fill({.11f,0,.07f}); input.hand_world={.11f,0,.07f};
			gesture.update(tuning,input,time(1),.25f);
			input.slap_points.fill({0,0,.07f}); input.hand_world={0,0,.07f}; gesture.update(tuning,input,time(1.03),.25f);
			bool accepted=false;
			for (int frame=0;frame<100;++frame)
			{
				const float z=frame%2 ? .07f : .05f; input.slap_points.fill({0,0,z}); input.hand_world={0,0,z};
				accepted=gesture.update(tuning,input,time(1.04+.01*frame),.25f) || accepted;
			}
			check(!accepted,"two-centimetre oscillations do not sum into an artificial slap");
		}
		for (int failure=0;failure<3;++failure)
		{
			p::handle_slap gesture; p::handle_catch_input input; input.valid=true; p::slap_observation observation;
			input.slap_points.fill({0,0,.4f}); input.hand_world={0,0,.4f}; gesture.update(tuning,input,time(1),.25f);
			input.slap_points.fill({0,0,-.2f}); input.hand_world=failure==2 ? vec{0,0,.4f} : vec{0,0,-.2f};
			const double dt=failure==0 ? .05 : failure==1 ? .16 : .10;
			check(!gesture.update(tuning,input,time(1+dt),.25f,&observation),"overspeed teleport, long tracking gap and stationary wrist still reject");
			check(observation.reason==(failure==0 ? p::slap_reason::jump : failure==1 ? p::slap_reason::sample_gap : p::slap_reason::world_slow),
				"adaptive continuity still reports the true tracking or wrist-speed rejection");
		}
	}
}
