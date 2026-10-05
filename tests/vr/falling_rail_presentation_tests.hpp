#pragma once
#include "component/vr/gameplay/falling_rail_presentation.hpp"

namespace falling_rail_presentation_tests
{
	template<class Check> void run(Check check)
	{
		namespace motion=vr::gameplay::motion;using namespace vr::gameplay::hands;using namespace std::chrono_literals;
		const auto born=motion::clock::time_point{1s};
		motion::flight initial;initial.start.position={0,0,40};initial.rail={0,0,-4};
		initial.rail_seconds=.16f;initial.units=40;initial.born=born;initial.advance(born);
		// Reproduce the measured 20 Hz source publication while the carrier
		// moves 160 native units/s. Rendering observes the actual per-record pose.
		for(int hz:{45,90,144})
		{
			motion::rail_presentation display;auto published=initial;double previous_tick=-1;
			for(int frame=0;frame<hz;++frame)
			{
				const double seconds=double(frame)/hz,tick=std::floor(seconds/.05)*.05;
				const auto at=born+std::chrono::duration_cast<motion::clock::duration>(std::chrono::duration<double>(seconds));
				anchor parent{{float(160*seconds),0,40},{0,0,0,1}};
				if(tick!=previous_tick)
				{
					anchor coarse{{float(160*tick),0,40},{0,0,0,1}};
					published.advance(born+std::chrono::duration_cast<motion::clock::duration>(std::chrono::duration<double>(tick)),&coarse);
					previous_tick=tick;
				}
				const auto before=published;
				const auto rendered=display.pose(published,at,&parent);
				const float gravity=std::max(0.f,float(seconds)-.16f);
				const vec expected{float(160*std::min(seconds,.16)),0,
					seconds<.16?40.f-25.f*float(seconds):36.f-25.f*gravity-.5f*9.81f*40.f*gravity*gravity};
				check(length(sub(rendered.position,expected))<.002f,
					"moving-carrier rail follows the exact render pose and freezes at its deadline despite late 20 Hz exit publication");
				const auto duplicate=display.pose(published,at,&parent);
				check(duplicate.position==rendered.position && duplicate.rotation==rendered.rotation,
					"repeated native consumers do not advance the visual rail or gravity twice");
				check(published.start.position==before.start.position && published.exit.position==before.exit.position &&
					published.velocity==before.velocity && published.detached==before.detached,
					"visual rail continuity cannot change the simulation's rail, departure or collision trajectory");
			}
		}
		motion::rail_presentation display;
		anchor a{{12,0,40},{0,0,0,1}},b{{28,0,40},{0,0,0,1}};
		(void)display.pose(initial,born+75ms,&a);(void)display.pose(initial,born+175ms,&b);
		const auto before=display.pose(initial,born+300ms,nullptr);
		(void)display.pose(initial,born+75ms,&a);
		const auto after=display.pose(initial,born+300ms,nullptr);
		check(before.position==after.position && before.rotation==after.rotation,
			"an older queued rail job cannot rewind an already retained departure");
		auto committed=initial;committed.advance(born+200ms,&b);display.reset();
		anchor moved{{1000,0,40},{0,0,0,1}};
		check(display.pose(committed,born+300ms,&moved).position==committed.pose(born+300ms).position,
			"first rendering after committed departure cannot reattach the falling object to a moved source gun");
		auto fresh=initial;fresh.born=born+1s;fresh.start=a;fresh.advance(fresh.born);
		check(display.pose(fresh,fresh.born+20ms,&a).position==fresh.pose(fresh.born+20ms).position,
			"a new release resets visual departure ownership rather than inheriting a previous flight");
	}
}
