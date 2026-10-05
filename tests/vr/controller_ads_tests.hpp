#pragma once
#include "component/vr/gameplay/controller_ads.hpp"
#include "component/vr/gameplay/ads_comfort.hpp"
#include "m200_live_assembly.hpp"
#include <limits>

template<class Check>
void controller_ads_tests(Check check)
{
	using namespace vr;
	using namespace gameplay::weapons;
	using namespace std::chrono;
	const auto start=controller_input::clock::now();
	controller_input::frame input;
	hold owner;
	muzzle_frame muzzle;
	ads_policy policy;
	auto now=start;
	const auto prepare=[&]
	{
		input={};input.sequence=input.reference_generation=input.continuity_generation=1;
		input.focused=true;input.grip[0].valid=input.grip[1].valid=true;
		owner={7,1,hand::right,hand::left,hold_source::interaction,1};owner.instance_generation=1;
		muzzle={};muzzle.valid=true;muzzle.owner=owner;muzzle.input_sequence=muzzle.reference_generation=1;
		muzzle.units_per_meter=40;muzzle.position={24,0,-2.4f};
		muzzle.axis={{{1,0,0},{0,1,0},{0,0,1}}};muzzle.head_forward={1,0,0};
		policy.reset();now=start;
		input.sampled_at=muzzle.sampled_at=muzzle.camera_at=now;
	};
	const auto read=[&](bool allowed=true) {return policy.consume(input,owner,muzzle,allowed,now);};
	const auto advance=[&](int ms)
	{
		now+=milliseconds(ms);++input.sequence;
		input.sampled_at=muzzle.sampled_at=muzzle.camera_at=now;muzzle.input_sequence=input.sequence;
		muzzle.owner=owner;
	};
	const auto activate=[&]
	{
		check(!read(),"ADS enters only after dwell");advance(50);
		check(!read(),"ADS short pose crossing does not enter");advance(50);
		check(read(),"ADS sustained alignment enters");
	};
	prepare();activate();
	muzzle.position[1]=4.4f;advance(20);
	check(read(),"ADS wider exit corridor retains small aiming motion");
	muzzle.position[2]=-16;advance(20);
	check(read(),"ADS brief misalignment has exit grace");advance(90);
	check(read(),"ADS exit grace lasts until observed deadline");advance(10);
	check(!read(),"ADS lowered weapon exits");
	muzzle.position[2]=-2.4f;advance(20);
	check(!read(),"ADS exit corridor alone cannot reenter");
	muzzle.position[1]=0;advance(20);activate();

	prepare();check(!read(),"ADS duplicate-sample setup");
	now+=milliseconds(110);input.sampled_at=now;++input.sequence;
	check(!read(),"ADS fresh input cannot age an unchanged muzzle into activation");
	now+=milliseconds(50);check(!read(),"ADS stale geometry fails closed");

	for (auto change : {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21})
	{
		prepare();activate();advance(10);
		switch(change)
		{
		case 0: input.focused=false;break;
		case 1: input.grip[1].valid=false;break;
		case 2: owner.rear=hand::none;break;
		case 3: owner.rear=static_cast<hand>(7);break;
		case 4: muzzle.sampled_at=now-milliseconds(151);break;
		case 5: muzzle.camera_at=now+milliseconds(1);break;
		case 6: muzzle.head_position[0]=std::numeric_limits<float>::quiet_NaN();break;
		case 7: muzzle.head_forward[0]=std::numeric_limits<float>::infinity();break;
		case 8: muzzle.units_per_meter=0;break;
		case 9: muzzle.units_per_meter=std::numeric_limits<float>::quiet_NaN();break;
		case 10: muzzle.axis[0]={0,0,0};break;
		case 11: input.orientation_settling=true;break;
		case 12: muzzle.firing_capable=false;break;
		case 13: input.sampled_at=now-milliseconds(151);break;
		case 14: ++input.reference_generation;break;
		case 15: ++muzzle.owner.revision;break;
		case 16: muzzle.input_sequence=input.sequence+1;break;
		case 17: muzzle.head_forward={};break;
		case 18: owner.support=hand::none;break;
		case 19: owner.support=owner.rear;break;
		case 20: owner.support=static_cast<hand>(7);break;
		case 21: input.grip[0].valid=false;break;
		}
		check(!read(),"ADS invalid ownership/input/geometry cancels immediately");
	}
	prepare();activate();check(!read(false),"ADS gameplay/setting/native selection gate cancels immediately");
	advance(10);activate();
	++input.continuity_generation;advance(10);
	check(!read(),"ADS producer discontinuity cancels old dwell");
	advance(50);check(!read(),"ADS recovery waits for new dwell");advance(50);check(read(),"ADS recovery reenters naturally");
	owner.rear=hand::left;owner.support=hand::right;++owner.rear_revision;++owner.revision;advance(10);
	check(!read(),"ADS rear-hand transfer cancels before reentry");advance(100);
	check(read(),"ADS left hand can enter naturally");
	++owner.revision;advance(10);
	check(!read(),"ADS release and regrip between reads cannot inherit the previous latch");
	advance(100);check(read(),"ADS supported regrip enters only after a fresh dwell");
	++owner.instance_generation;advance(10);
	check(!read(),"ADS same-definition replacement cannot inherit latch");
	for(const auto rear:{hand::left,hand::right})
	{
		prepare();owner.rear=rear;owner.support=hand::none;advance(10);
		check(!read(),"single-handed alignment never starts ADS");advance(100);
		check(!read(),"either single firing hand remains outside ADS despite perfect angle and distance");
		owner.support=rear==hand::left ? hand::right : hand::left;++owner.revision;advance(10);activate();
		owner.support=hand::none;++owner.revision;advance(1);
		check(!read(),"support release exits ADS immediately without the spatial exit grace");
		owner.support=rear==hand::left ? hand::right : hand::left;++owner.revision;advance(1);activate();
	}

	for (float scale : {10.f,40.f,100.f})
	{
		prepare();muzzle.units_per_meter=scale;
		// A rolled upward-facing gun/head pair translated in the world.
		muzzle.axis={{{0,0,1},{1,0,0},{0,1,0}}};muzzle.head_forward={0,0,1};
		muzzle.head_position={100,200,300};muzzle.position={100,200-.06f*scale,300+.6f*scale};
		activate();
	}
	for (const auto position : {gameplay::hands::vec{24,0,-20},gameplay::hands::vec{-24,0,-2.4f},gameplay::hands::vec{24,12,-2.4f}})
	{
		prepare();muzzle.position=position;
		check(!read(),"ADS waist/behind/lateral pose rejected");advance(100);
		check(!read(),"ADS rejected pose cannot dwell into activation");
	}
	{
		// Captured desert M200: receiver silencer socket + silencer03 muzzle,
		// and the actual scope socket/rear lens. Reproduce the 1.68 m aiming
		// depth that failed the old 1.5 m corridor despite a near-eye scope.
		using namespace gameplay::hands;
		prepare();muzzle.units_per_meter=1.f/.0254f;
		const auto& bones=m200_live_assembly::bones;
		muzzle.position=add(bones[82].bind.position,bones[86].bind.position);
		muzzle.head_position=add(muzzle.position,scale(vec{-1.68f,.04f,.094f},muzzle.units_per_meter));
		const ads_comfort::binding sight{ads_comfort::classify("attach_h2_cheytac_scope_vm_desert"),0};
		const std::array<bone,1> pose{bones[77].bind};
		const auto eye=ads_comfort::measure_eye(sight,pose,muzzle.head_position,muzzle.axis[0],muzzle.units_per_meter);
		const auto raised=ads_alignment::measure(muzzle.head_position,muzzle.head_forward,muzzle.position,muzzle.axis,muzzle.units_per_meter);
		check(eye.valid && eye.distance_meters<.30f && !ads_alignment::inside(raised,false),
			"captured M200 near-eye aim reproduces the muzzle-only ADS rejection");
		muzzle.ads_sight_to_muzzle_meters=raised.depth-eye.depth_meters;
		check(muzzle.ads_sight_to_muzzle_meters>1.47f && muzzle.ads_sight_to_muzzle_meters<1.49f &&
			ads_alignment::approach(raised,muzzle.ads_sight_to_muzzle_meters)>.999f,
			"captured M200 assembly grants the same near-eye alignment to ADS and approach");
		activate();
		muzzle.ads_translation=scale(muzzle.axis[0],-.15f*muzzle.units_per_meter);
		muzzle.position=add(muzzle.position,muzzle.ads_translation);advance(100);
		check(read(),"long-rifle comfort translation cannot change its assembly length or cancel ADS");
		owner.support=hand::none;++owner.revision;advance(1);
		check(!read(),"long-rifle geometry never bypasses immediate support release");
		owner.support=hand::left;++owner.revision;advance(1);activate();
		const auto entry_depth=muzzle.ads_sight_to_muzzle_meters+ads_alignment::sight_reach_meters;
		muzzle.position[0]=muzzle.head_position[0]+(entry_depth+.1f)*muzzle.units_per_meter+muzzle.ads_translation[0];
		advance(100);check(read(),"long-rifle far exit retains its twenty-centimeter hysteresis");
		muzzle.position[0]+=.11f*muzzle.units_per_meter;advance(1);
		check(read(),"extended long rifle starts its ordinary exit dwell");advance(100);
		check(!read(),"long rifle still exits when its rear sight moves beyond reach");
		muzzle.position[0]-=.11f*muzzle.units_per_meter;advance(100);check(!read(),"long-rifle exit corridor cannot start a new entry");
	}
	for(float invalid:{-1.f,11.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
	{
		prepare();activate();muzzle.ads_sight_to_muzzle_meters=invalid;
		check(!read(),"invalid sight geometry cancels ADS immediately");
	}
	prepare();muzzle.head_forward={0,1,0};check(!read(),"ADS looking sideways rejected");advance(100);
	check(!read(),"ADS gun near face without facing alignment remains inactive");
	prepare();activate();input.sequence=0;check(!read(),"ADS missing input cancels");
	const auto facing=[&](float degrees)
	{
		const float radians=degrees/57.2957795131f;
		muzzle.head_forward={std::cos(radians),std::sin(radians),0};
	};
	prepare();facing(29.f);activate();
	facing(39.f);advance(20);check(read(),"ADS expanded exit angle retains a supported thirty-nine-degree pose");
	facing(41.f);advance(20);check(read(),"ADS beyond forty degrees starts the normal exit grace");advance(100);
	check(!read(),"ADS beyond forty degrees exits after the spatial grace");
	prepare();facing(31.f);check(!read(),"ADS outside thirty degrees cannot start entry");advance(100);
	check(!read(),"ADS expanded entry still rejects thirty-one degrees after dwell");
	prepare();muzzle.ads_translation={-8,0,0};muzzle.position={0,0,-2.4f};activate();
	advance(100);check(read(),"ADS comfort at the near corridor edge cannot feed back and release its own request");
	muzzle.ads_translation[0]=std::numeric_limits<float>::quiet_NaN();
	check(!read(),"ADS rejects nonfinite comfort correction");
	prepare();muzzle.optic.thermal=true;muzzle.ads_translation={-18,0,0};muzzle.position={6,0,-2.4f};facing(34.f);
	check(!read(),"thermal entry still requires a fresh intentional pose");advance(30);
	check(!read(),"thermal short crossing does not enter");advance(10);
	check(read(),"inactive thermal lens capability admits its full 45 cm approach and 34-degree intent after 40 ms");
	advance(100);check(read(),"near-eye thermal translation cannot repeatedly cancel its own ADS request");
	facing(44.f);advance(10);check(read(),"thermal retains 44-degree intent inside its distinct exit angle");
	facing(46.f);advance(10);check(read(),"thermal departure begins a short grace");advance(40);
	check(!read(),"thermal returns from ADS after 40 ms beyond 45 degrees");
	facing(0);muzzle.ads_translation={-18.4f,0,0};advance(10);check(!read(),"thermal excessive translation remains invalid");
	muzzle.ads_translation={-18,0,0};muzzle.optic.thermal=false;advance(100);
	check(!read(),"thermal translation allowance does not leak to an ordinary weapon");
}
