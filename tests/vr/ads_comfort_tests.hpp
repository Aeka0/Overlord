#pragma once
#include "component/vr/gameplay/ads_comfort.hpp"
#include <limits>

namespace ads_comfort_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr;
		using namespace gameplay::hands;
		using namespace gameplay::weapons;
		using namespace ads_comfort;
		using namespace std::chrono;
		const auto close=[](vec a,vec b){return length(sub(a,b))<.002f;};
		check(classify("attach_h2_eotech_2_vm_woodland").type==sight::holographic &&
			classify("attach_h2_acog_2_vm_digital").type==sight::acog &&
			classify("attach_h2_steyr_scope_vm").type==sight::acog &&
			classify("attach_h2_sa80_scope_vm").type==sight::acog &&
			classify("attach_h2_thermal_scope_2_vm_arctic").type==sight::thermal &&
			classify("attach_h2_dragunov_scope_vm_woodland").type==sight::scope,
			"ADS comfort classifies actual optic assemblies independently of magnifier support");
		check(distance(sight::holographic)==.05f && distance(sight::acog)==.10f &&
			distance(sight::scope)==.15f && distance(sight::thermal)==.45f,
			"holographic, ACOG and sniper tuning changes independently of thermal approach");
		for(const auto name:{"", "h2_viewmodel_m4", "attach_h2_red_dot_sight_vm", "attach_h2_red_dot_sight_vm_woodland",
			"attach_h2_tavor_scope_vm", "attach_h2_eotech_2_vm_unknown", "attach_h2_thermal_scope_2_vm_extra"})
			check(classify(name).type==sight::unchanged,"iron, red-dot, MARS and unknown optics retain their original pose");
		for(const auto& lens:optics::catalog)
			check(distance(classify(lens.model).type)>0,"every existing magnified optic has a comfort tier");
		check(distance_weight(.15f)==1 && distance_weight(.30f)==1 &&
			std::abs(distance_weight(.45f)-.5f)<.00001f && distance_weight(.60f)==0 && distance_weight(2.f)==0,
			"eye distance preserves close assistance, halves it at 45 cm and removes it at 60 cm");
		float previous_distance_weight=1;
		for(float meters:{.30f,.35f,.40f,.45f,.50f,.55f,.60f,.90f})
		{
			const float weight=distance_weight(meters);
			check(weight>=0 && weight<=previous_distance_weight,"extending the sight monotonically reduces the assistance target");
			previous_distance_weight=weight;
		}
		for(float meters:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
			check(distance_weight(meters)==0,"invalid eye distance cannot request approach");

		controller_input::frame input;
		input.sequence=input.reference_generation=input.continuity_generation=1;
		input.focused=true;input.grip[0].valid=input.grip[1].valid=true;
		input.sampled_at=controller_input::clock::now();
		hold owner{7,1,hand::right,hand::left,hold_source::interaction,1};owner.instance_generation=1;
		transition movement;
		const auto read=[&](float alignment=1.f,bool allowed=true,float cap=.20f)
			{return movement.update(input,owner,1,sight::scope,allowed,alignment,cap,input.sampled_at);};
		const auto advance=[&](int ms){input.sampled_at+=milliseconds(ms);++input.sequence;};
		check(read()==0,"first ADS comfort sample cannot jump the gun");
		advance(40);const float partial=read();
		check(partial>0 && partial<.075f && read()==partial,"ADS blends over fresh samples and duplicate reads cannot advance it");
		for(int i=0;i<3;++i){advance(40);(void)read();}
		check(read()>partial && read()<=transition::max_speed_meters_per_second*.16f,"large approach remains speed-bounded after 160 ms");
		for(int i=0;i<5;++i){advance(40);(void)read();}
		check(read()>.04f && read()<.09f,"grip entry spreads the large offset over time instead of front-loading the approach");
		check(read(1.f,true,.02f)<=.02f,"moving the head closer clamps even an unchanged input sample");
		for(int i=0;i<4;++i){advance(40);(void)read();}
		advance(40);check(read(0.f)>0 && read(0.f)<.15f,"looking away blends back instead of snapping");
		for(int i=0;i<30;++i){advance(40);(void)read(0.f);}
		check(read(0.f)==0,"looking away restores the original pose");
		for(int i=0;i<30;++i){advance(40);(void)read(.5f);}
		check(std::abs(read(.5f)-.075f)<.00001f,"partial angular alignment settles at a partial distance, not the ADS maximum");
		for(const auto rear:{hand::left,hand::right})
		{
			movement.reset();const auto original=owner;
			owner.rear=rear;owner.support=hand::none;
			check(read()==0,"single-handed rest cannot initialize a comfort offset");advance(40);
			check(read()==0,"single-handed alignment stays at original distance");
			owner.support=rear==hand::left ? hand::right : hand::left;++owner.revision;
			check(read()==0,"support acquisition cannot jump position on its first sample");advance(40);
			check(read()>0 && read()<.075f,"support acquisition eases into the existing angular target");
			for(int i=0;i<30;++i){advance(40);(void)read();}
			const auto full=read();
			owner.support=hand::none;++owner.revision;
			check(read()==full,"support release preserves position while ADS authority ends immediately");
			advance(40);const auto released=read();
			check(released>full*.6f && released<full,"support release returns gradually instead of clearing the offset");
			owner.support=rear==hand::left ? hand::right : hand::left;++owner.revision;
			check(read()==released,"rapid regrip preserves the current return position");advance(40);
			check(std::abs(read()-released)<=.010001f && read()>0 && read()<full,
				"rapid regrip preserves momentum within the speed limit instead of instantly reversing velocity");
			for(int i=0;i<5;++i){advance(40);(void)read();}
			check(read()>released && read()<=full,"rapid regrip smoothly reverses and approaches the goal without overshoot");
			const auto before_revision=read();++owner.revision;
			check(read()==before_revision,"release and regrip between samples cannot reset visual position");
			owner.support=hand::none;++owner.revision;float previous=read();
			for(int i=0;i<45;++i)
			{
				advance(40);const float returned=read();
				check(returned<=previous && returned>=0,"released support returns monotonically while the rear hand retains the gun");
				previous=returned;
			}
			check(read()==0,"support release settles at the original single-handed pose");
			owner=original;movement.reset();
		}
		for(int change=0;change<14;++change)
		{
			for(int i=0;i<5;++i){advance(40);(void)read();}
			const auto old=input;const auto old_owner=owner;
			switch(change)
			{
			case 0: ++owner.instance_generation;break;
			case 1: ++owner.rear_revision;break;
			case 2: ++input.reference_generation;break;
			case 3: ++input.continuity_generation;break;
			case 4: input.focused=false;break;
			case 5: input.orientation_settling=true;break;
			case 6: input.grip[int(owner.rear)].valid=false;break;
			case 7: advance(151);++input.continuity_generation;break;
			case 8: --input.sequence;break;
			case 9: owner.rear=hand::left;owner.support=hand::none;break;
			case 10: owner.support=static_cast<hand>(7);break;
			case 11: owner.support=owner.rear;break;
			case 12: input.grip[int(owner.support)].valid=false;break;
			case 13: owner.attachment=control_attachment::moving;break;
			}
			check(read()==0,"ownership, reference, tracking and sample discontinuities cannot retain a comfort offset");
			input=old;owner=old_owner;
		}
		check(read(1.f,false)==0,"mechanical interaction or gameplay suppression cancels comfort");
		check(read(1.f,true,std::numeric_limits<float>::quiet_NaN())==0,"nonfinite clearance fails closed");
		check(read(std::numeric_limits<float>::quiet_NaN())==0,"nonfinite angular alignment fails closed");
		check(movement.update(input,owner,1,sight::scope,true,1.f,.2f,input.sampled_at+milliseconds(151))==0,
			"stale input cannot retain ADS comfort");
		for(const auto rear:{hand::left,hand::right})
		{
			const auto old=owner;owner.rear=rear;owner.support=hand::none;
			for(int i=0;i<5;++i){advance(40);check(read()==0,"single-handed aiming never acquires an approach offset");}
			owner=old;
		}
		float previous_rate_result{};
		for(int steps:{1,2,4,8,16})
		{
			transition filtered;auto sample=input;
			(void)filtered.update(sample,owner,1,sight::scope,true,.1f,.2f,sample.sampled_at);
			float result{};
			for(int i=0;i<steps;++i)
			{
				sample.sampled_at+=microseconds(100000/steps);++sample.sequence;
				result=filtered.update(sample,owner,1,sight::scope,true,.1f,.2f,sample.sampled_at);
			}
			check(steps==1 || std::abs(result-previous_rate_result)<.00001f,"angular filtering is independent of frame rate, including valid 100 ms frames");
			previous_rate_result=result;
		}
		{
			transition filtered;auto sample=input;float previous{};
			(void)filtered.update(sample,owner,1,sight::scope,true,0,.2f,sample.sampled_at);
			for(int i=0;i<150;++i)
			{
				sample.sampled_at+=milliseconds(10);++sample.sequence;
				const float target=i<80?1.f:0.f;
				const float current=filtered.update(sample,owner,1,sight::scope,true,target,.2f,sample.sampled_at);
				check(std::abs(current-previous)<=.002501f,"rapid corridor entry and reversal cannot exceed 2.5 mm per 10 ms");
				if(i==0)check(current<.001f,"approach begins from rest instead of jumping to its maximum speed");
				previous=current;
			}
			sample.sampled_at+=milliseconds(200);++sample.sequence;
			const float slow=filtered.update(sample,owner,1,sight::scope,true,1,.2f,sample.sampled_at);
			check(slow>0 && std::abs(slow-previous)<=.037501f,"continuous tracking with a slow consumer does not reset the visual offset");
		}

		{
			transition thermal,ordinary;auto sample=input;
			(void)thermal.update(sample,owner,1,sight::thermal,true,1,.45f,sample.sampled_at);
			(void)ordinary.update(sample,owner,1,sight::scope,true,1,.15f,sample.sampled_at);
			float fast{},slow{},last{};
			for(int i=0;i<60;++i)
			{
				sample.sampled_at+=milliseconds(10);++sample.sequence;
				fast=thermal.update(sample,owner,1,sight::thermal,true,1,.45f,sample.sampled_at);
				slow=ordinary.update(sample,owner,1,sight::scope,true,1,.15f,sample.sampled_at);
				check(fast>=last && fast-last<=.009001f,"thermal fast acquisition remains monotonic and speed bounded");last=fast;
			}
			check(fast>.44f && slow<.15f,"thermal completes its much larger approach while ordinary scope retains its slower settling");
			check(thermal.update(sample,owner,1,sight::thermal,true,1,.45f,sample.sampled_at)==fast,
				"duplicate samples cannot accelerate thermal movement");
			// Returning the full 45 cm at 0.9 m/s needs at least 500 ms,
			// followed by the existing critically damped spring's settling tail.
			// Keep the per-frame speed assertion; allow 750 ms to reach <1 mm.
			for(int i=0;i<75;++i)
			{
				sample.sampled_at+=milliseconds(10);++sample.sequence;
				fast=thermal.update(sample,owner,1,sight::thermal,true,0,.45f,sample.sampled_at);
				check(std::abs(fast-last)<=.009001f,"thermal fast return retains its speed bound");last=fast;
			}
			check(fast<.001f,"thermal returns promptly after aim leaves the corridor");
			sample.focused=false;check(thermal.update(sample,owner,1,sight::thermal,true,1,.45f,sample.sampled_at)==0,
				"thermal response still clears immediately on focus loss");
		}
		const std::array<vec,3> axis{{{1,0,0},{0,1,0},{0,0,1}}};
		float previous_weight=-1;
		for(float degrees:{50.f,45.f,40.f,35.f,30.f,25.f,20.f,10.f,5.f,0.f})
		{
			const float radians=degrees/57.2957795131f;
			const auto sample=ads_alignment::measure({}, {std::cos(radians),std::sin(radians),0}, {.6f,0,-.06f},axis,1.f);
			const float weight=ads_alignment::approach(sample);
			check(weight>=previous_weight && weight>=0 && weight<=1,"angular approach increases continuously toward ADS center");
			if(degrees>=45)check(weight<.00001f,"outside forty-five degrees leaves the weapon at tracked distance");
			if(degrees<=5)check(weight>.99999f,"within five degrees reaches the maximum approach");
			if(degrees==25)check(std::abs(weight-.5f)<.00001f,"the angular midpoint applies half of the available approach");
			if(degrees==35)check(weight>0 && !ads_alignment::inside(sample,false),"approach begins before native ADS can enter");
			previous_weight=weight;
		}
		for(const auto muzzle:{vec{.6f,0,-.5f},vec{.6f,.3f,-.06f},vec{-.5f,0,-.06f},vec{2,0,-.06f}})
			check(ads_alignment::approach(ads_alignment::measure({},{1,0,0},muzzle,axis,1.f))==0,
				"hip, lateral, behind-head and remote parallel poses cannot pull the gun toward the face");
		for(float lateral:{.09f,.10f,.11f,.12f,.13f})
		{
			const float weight=ads_alignment::approach(ads_alignment::measure({},{1,0,0},{.6f,lateral,-.06f},axis,1.f));
			check(weight<=previous_weight && weight>=0,"position corridor fades continuously rather than switching distance");
			previous_weight=weight;
		}
		for(float units:{10.f,40.f,100.f}) for(bool rotated:{false,true})
		{
			binding optic{classify("attach_h2_cheytac_scope_vm"),0};
			const quat rotation=rotated ? from_to({1,0,0},{0,0,1}) : quat{0,0,0,1};
			const auto forward=rotate(rotation,{1,0,0});const vec head{100,200,300};
			const std::array<vec,3> turned_axis{forward,rotate(rotation,{0,1,0}),rotate(rotation,{0,0,1})};
			check(ads_alignment::approach(ads_alignment::measure(head,forward,add(head,rotate(rotation,{.6f*units,0,-.06f*units})),turned_axis,units))>.99999f,
				"alignment weight is invariant under world scale, rotation and translation");
			std::array<bone,1> pose{};pose[0].rotation=rotation;
			pose[0].position=sub(add(head,scale(forward,.5f*units)),rotate(rotation,scale(optic.optic.lens->center_meters,units)));
			const auto half_meter=measure_eye(optic,pose,head,forward,units);
			check(half_meter.valid && std::abs(half_meter.distance_meters-.5f)<.0001f &&
				std::abs(half_meter.depth_meters-.5f)<.0001f &&
				std::abs(half_meter.clearance_meters-.15f)<.0001f,
				"scope approach is invariant under world scale, rotation and translation");
			const auto long_muzzle=add(head,rotate(rotation,{1.98f*units,0,-.094f*units}));
			const auto raised=ads_alignment::measure(head,forward,long_muzzle,turned_axis,units);
			const auto sight_to_muzzle=raised.depth-half_meter.depth_meters;
			check(std::abs(sight_to_muzzle-1.48f)<.0001f && ads_alignment::inside(raised,false,false,sight_to_muzzle) &&
				ads_alignment::approach(raised,sight_to_muzzle)>.9999f && ads_alignment::approach(raised)==0,
				"assembly-based far reach fixes long-rifle intent and approach at every world scale and rotation");
			auto beyond=raised;beyond.depth+=.31f;
			check(!ads_alignment::inside(beyond,true,false,sight_to_muzzle) && ads_alignment::approach(beyond,sight_to_muzzle)==0,
				"long-rifle intent and approach both end beyond rear-sight reach");
			for(float bad:{-1.f,11.f,INFINITY,std::numeric_limits<float>::quiet_NaN()})
				check(ads_alignment::approach(raised,bad)==0,"invalid assembly extent cannot request comfort motion");
			pose[0].position=sub(pose[0].position,scale(forward,.4f*units));
			check(std::abs(measure_eye(optic,pose,head,forward,units).clearance_meters-.02f)<.0001f,"rear lens preserves eight centimeters of clearance");
			pose[0].position=sub(pose[0].position,scale(forward,.05f*units));
			check(measure_eye(optic,pose,head,forward,units).clearance_meters==0,"already-close lenses are not translated or pushed away");
			pose[0].position=sub(add(head,rotate(rotation,{.36f*units,.48f*units,0})),rotate(rotation,scale(optic.optic.lens->center_meters,units)));
			const auto diagonal=measure_eye(optic,pose,head,forward,units);
			check(diagonal.valid && std::abs(diagonal.distance_meters-.60f)<.0001f && distance_weight(diagonal.distance_meters)<.00001f,
				"distance attenuation measures the actual rear lens distance rather than only axial muzzle depth");
			optic.optic=classify("attach_h2_eotech_2_vm");
			for(unsigned c=0;c<8;++c) optic.corners[c]={(c&1 ? .10f : -.15f)*units,
				(c&2 ? .04f : -.04f)*units,(c&4 ? .025f : -.025f)*units};
			pose[0].position=add(head,scale(forward,.25f*units));
			check(std::abs(measure_eye(optic,pose,head,forward,units).clearance_meters-.02f)<.0001f,
				"holographic safety uses the rear native geometry rather than the forward mounting tag");
			pose[0].position=add(head,scale(forward,.60f*units));
			const auto thermal=measure_eye(optic,pose,head,forward,units);
			check(thermal.valid && std::abs(thermal.distance_meters-.45f)<.0001f &&
				std::abs(distance_weight(thermal.distance_meters)-.5f)<.0001f,
				"optics without lens metadata use the rear bounds center for distance attenuation");
			optic.optic=classify("attach_h2_thermal_scope_2_vm");
			check(optic.optic.type==sight::thermal && optic.optic.lens && optic.optic.lens->thermal,
				"thermal keeps its comfort tier when native lens metadata becomes available");
			for(float meters:{.10f,.30f,.50f})
			{
				pose[0].position=sub(add(head,scale(forward,meters*units)),rotate(rotation,scale(optic.optic.lens->center_meters,units)));
				const auto measured=measure_eye(optic,pose,head,forward,units);
				check(measured.valid && std::abs(measured.clearance_meters-(meters-.05f))<.0001f &&
					distance_weight(sight::thermal,measured.distance_meters)>.9999f,
					"thermal ADS brings the measured rear lens to five centimeters across normal aiming reach");
			}
			pose[0].position=sub(add(head,scale(forward,.04f*units)),rotate(rotation,scale(optic.optic.lens->center_meters,units)));
			check(measure_eye(optic,pose,head,forward,units).clearance_meters==0,"already-close thermal lens cannot be pushed through the head");
			check(std::abs(distance_weight(sight::thermal,.60f)-.5f)<.0001f && distance_weight(sight::thermal,.70f)==0 &&
				distance_weight(sight::thermal,INFINITY)==0 && distance_weight(sight::scope,.45f)==distance_weight(.45f),
				"thermal fades an extended gun without changing ordinary optic attenuation");
			pose[0].position[0]=std::numeric_limits<float>::quiet_NaN();
			check(!measure_eye(optic,pose,head,forward,units).valid,"invalid optic geometry cannot retain an eye-distance sample");
		}
		{
			// Combine the real lens measurement with the existing transition.
			// Distance fading must change the target, not the hard safety limit.
			binding optic{classify("attach_h2_cheytac_scope_vm"),0};
			std::array<bone,1> raw{};raw[0].rotation={0,0,0,1};
			movement.reset();
			const auto at_distance=[&](float meters)
			{
				raw[0].position=sub(vec{meters*40,0,0},scale(optic.optic.lens->center_meters,40));
				const auto eye=measure_eye(optic,raw,{},axis[0],40);
				return read(eye.valid ? distance_weight(eye.distance_meters) : 0.f,true,eye.clearance_meters);
			};
			(void)at_distance(.30f);
			for(int i=0;i<35;++i){advance(40);(void)at_distance(.30f);}
			const float close_offset=at_distance(.30f);
			check(std::abs(close_offset-.15f)<.00001f,"close alignment retains the accepted sniper assistance strength");
			check(at_distance(.60f)==close_offset,"extending beyond the fade range cannot snap position on a duplicate sample");
			advance(40);const float retreat=at_distance(.60f);
			check(retreat>close_offset*.6f && retreat<close_offset,"far-distance reduction eases through the retained spring state");
			for(int i=0;i<35;++i){advance(40);(void)at_distance(.60f);}
			check(at_distance(.60f)==0,"distant sight settles at the original tracked position");
			for(int i=0;i<35;++i){advance(40);(void)at_distance(.45f);}
			check(std::abs(at_distance(.45f)-.075f)<.00001f,"bringing the sight halfway back smoothly restores half the assistance");
			check(at_distance(.09f)<=.01001f,"near-eye safety still overrides retained distance smoothing immediately");
		}

		rig r{};r.count=12;r.gun=9;r.muzzle=10;r.weapon_tag=0;
		r.arms={arm{1,2,3},arm{5,6,7}};
		const int parents[]{-1,0,1,2,3,0,5,6,7,0,9,9};
		std::copy(std::begin(parents),std::end(parents),r.parent.begin());
		r.weapon_bones[9]=r.weapon_bones[10]=r.weapon_bones[11]=true;
		for(int rear=0;rear<2;++rear) for(bool supported:{false,true})
		{
			std::array<bone,12> pose{};for(auto& b:pose)b.rotation={0,0,0,1};
			for(int h=0;h<2;++h)
			{
				const auto a=r.arms[h];const float side=h==0?1.f:-1.f;
				pose[a.shoulder].position={-5,side*5,5};pose[a.elbow].position={-2,side*7,0};
				pose[a.wrist].position={h==rear?0.f:8.f,0,0};
				pose[a.wrist+1].position=add(pose[a.wrist].position,{1,0,0});
			}
			pose[r.muzzle].position={20,0,0};pose[11].position={5,0,2};
			const auto before=pose;const vec offset{-8,0,0};
			check(apply(r,pose,rear,supported?1-rear:-1,axis,offset),"ADS comfort reuses valid arm IK for either firing hand");
			for(int i:{0,9,10,11}) check(close(pose[i].position,add(before[i].position,offset)) && pose[i].rotation==before[i].rotation,
				"weapon tag, receiver, muzzle and lens translate together without changing aim");
			for(int h=0;h<2;++h)
			{
				const auto a=r.arms[h];
				check(close(pose[a.shoulder].position,before[a.shoulder].position),"comfort does not translate shoulders or camera");
				if(h==rear || supported)
				{
					check(close(pose[a.wrist].position,add(before[a.wrist].position,offset)) &&
						close(sub(pose[a.wrist+1].position,pose[a.wrist].position),{1,0,0}),"attached wrists and fingers keep their exact gun contacts");
				}
				else for(int i=a.shoulder;i<=a.wrist+1;++i) check(pose[i].position==before[i].position && pose[i].rotation==before[i].rotation,
					"free hand or other firing hand is untouched by ADS comfort");
			}
			pose=before;
			check(!apply(r,pose,rear,supported?1-rear:-1,axis,{std::numeric_limits<float>::quiet_NaN(),0,0}),"bad translation is rejected transactionally");
			for(int i=0;i<r.count;++i) check(pose[i].position==before[i].position && pose[i].rotation==before[i].rotation,
				"rejected comfort pose preserves the whole source skeleton");
		}
	}
}
