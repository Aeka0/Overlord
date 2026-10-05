#pragma once
#include "component/vr/gameplay/part_hand_transition.hpp"
#include "component/vr/gameplay/support_carry_rotation.hpp"

namespace part_hand_transition_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::hands;
		namespace w=vr::gameplay::weapons;
		using namespace std::chrono_literals;
		using clock=vr::controller_input::clock;
		const auto close=[](vec a,vec b){return length(sub(a,b))<.0001f;};
		const auto start=clock::time_point{}+1s;
		w::hold owner{9,1,vr::hand::right,vr::hand::none,w::hold_source::interaction,1};
		owner.instance_generation=3;
		for(float units:{1.f,39.37007874f,100.f})
		{
			part_hand_transition motion;anchor tracked{{}, {0,0,0,1}};
			const auto target=scale(vec{.08f,-.03f,.02f},units);
			const auto update=[&](part_hand_attachment kind,vec desired,auto at){return motion.update(kind,tracked,desired,tracked.position,owner,4,1,at,units,true);};
			check(close(update(part_hand_attachment::free,{},start),{}),"free wrist stays at calibrated controller");
			check(close(update(part_hand_attachment::seated_magazine,target,start+10ms),{}),"seated magazine acquisition has no wrist jump");
			const auto half=update(part_hand_attachment::seated_magazine,target,start+55ms);
			check(close(half,scale(target,.5f)),"magazine acquisition reaches midpoint smoothly");
			check(close(update(part_hand_attachment::seated_magazine,target,start+55ms),half),"second eye cannot advance the transition");
			check(close(update(part_hand_attachment::seated_magazine,target,start+100ms),target),"acquisition ends exactly without permanent tracking lag");
			check(close(update(part_hand_attachment::magazine,{},start+110ms),target),"extraction starts at the seated wrist");
			check(close(update(part_hand_attachment::magazine,{},start+200ms),{}),"free magazine returns to the controller wrist");
			check(close(update(part_hand_attachment::seated_magazine,target,start+210ms),{}),"reinsertion uses the same wrist transition");
			const auto before=update(part_hand_attachment::seated_magazine,target,start+240ms);
			check(close(update(part_hand_attachment::free,{},start+240ms),before),"rapid release continues from the visible intermediate position");
			tracked.position=scale(vec{2,3,4},units);tracked.rotation={0,0,.70710678f,.70710678f};
			const auto moving=update(part_hand_attachment::free,tracked.position,start+285ms);
			check(close(moving,add(tracked.position,rotate(tracked.rotation,scale(before,.5f)))),"transition follows controller translation and rotation without trailing in world space");
			check(close(motion.update(part_hand_attachment::free,tracked,tracked.position,tracked.position,owner,4,2,start+286ms,units,true),tracked.position),"recenter discards previous-space offset");
		}
		{
			part_hand_transition motion;const anchor tracked{};const vec first{.08f,0,0},second{0,.08f,0};
			(void)motion.update(part_hand_attachment::action,tracked,first,{},owner,4,1,start,1,true,1);
			(void)motion.update(part_hand_attachment::action,tracked,first,{},owner,4,1,start+90ms,1,true,1);
			check(close(motion.update(part_hand_attachment::action,tracked,second,{},owner,4,1,start+100ms,1,true,2),first),
				"regrabbing the same part between render samples starts from its displayed wrist");
		}
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			w::hold before=owner;before.rear=rear;before.support=vr::hand(1-int(rear));
			auto after=before;after.rear=vr::hand::none;++after.rear_revision;
			w::carry::support_carry_rotation carried;
			const quat controller{.258819f,0,0,.965926f},gun{0,0,.3826834f,.9238795f};
			check(carried.capture(before,after,1,controller,gun),"either support hand captures relative gun rotation on rear release");
			const auto basis=carried.basis(after,1);
			check(basis && close(rotate(multiply(controller,*basis),{1,0,0}),rotate(gun,{1,0,0})),"releasing main hand preserves the gun angle");
			const quat turn{0,.3826834f,0,.9238795f};
			check(basis && close(rotate(multiply(multiply(turn,controller),*basis),{1,0,0}),rotate(multiply(turn,gun),{1,0,0})),"turning support wrist rotates gun by the same relative motion");
			check(!carried.basis(before,1) && !carried.basis(after,2),"main regrasp and reference change cannot reuse sole-support rotation");
			++after.rear_revision;check(!carried.basis(after,1),"new grip lifetime rejects old support offset");
		}
	}
}
