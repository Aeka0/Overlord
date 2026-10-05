#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/quick_reload.hpp"
#include "component/vr/gameplay/cylinder_feed.hpp"
#include "component/vr/gameplay/break_action_feed.hpp"
#include "component/vr/gameplay/weapons/de50/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/weapon_holsters.hpp"
#include "component/vr/gameplay/sound_variant_selection.hpp"
#include "component/vr/gameplay/weapon_feedback.hpp"
#include <iostream>
#include <limits>

namespace w=vr::gameplay::weapons;
namespace m=w::mechanics;
namespace c=w::cylinder;
namespace b=w::break_action;
namespace q=w::quick_reload;
using namespace std::chrono_literals;

int main()
{
	int failures{};
	const auto check=[&](bool value,const char* message){if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}};
	for(const auto id:{"m9","usp","usp_silencer","magnum44","de50","m1911","g18","tmp","miniuzi","pp2000","ranger"})
		check(q::supported(id),"explicit eligible feed");
	for(const auto id:{"","m93r","mp5","p90","m4","m79","striker","de50_fake","ranger_akimbo"})
		check(!q::supported(id),"unlisted feeds cannot inherit short-weapon eligibility");
	check(w::de50::gold.reload==w::de50::base.reload && q::supported(w::de50::gold.reload->id),"gold Desert Eagle uses admitted feed");
	check(w::de50::gold.reload->matches_native("deserteagle_gold",7),"gold native identity remains admitted");
	const std::array<std::string_view,2> sounds{"h2_weapons/foley/wpn_handgun_ads_down_01.flac","h2_weapons\\foley\\wpn_handgun_ads_down_02.wav"};
	check(w::sound_variant_mask(q::start_sound_files,sounds)==3,"stock random alias resolves the exact two requested recordings");
	check(w::sound_variant_mask(q::start_sound_files,{sounds.data(),1})==1 &&
		w::sound_variant_mask(q::start_sound_files,{sounds.data()+1,1})==2,"separate aliases retain distinct random choices");
	const std::array<std::string_view,2> unrelated{sounds[0],"wpn_handgun_ads_up_02.flac"};
	check(w::sound_variant_mask(q::start_sound_files,unrelated)==0 && w::sound_variant_mask(q::start_sound_files,{})==0,
		"aliases containing other recordings or no files cannot play as quick reload feedback");
	// Captured from the running native SOUND pool, 2026-09-27. This companion
	// caused the original matcher to reject both otherwise correct recordings.
	const std::array<std::string_view,2> native_files{"foley/weapmvmt/wpn_handgun_ads_down_01","foley/weapmvmt/wpn_handgun_ads_down_02"};
	check(std::string_view(q::start_sound_alias)=="wpn_handgun_ads_down_plr" &&
		w::sound_variant_mask(q::start_sound_files,native_files)==3 && q::start_sound_companions("h2_wpn_foley_ads_plr",""),
		"real handgun ADS-down alias retains its stock foley companion and admits both extensionless files");
	check(q::start_sound_companions("","") && !q::start_sound_companions("unrelated","") &&
		!q::start_sound_companions("h2_wpn_foley_ads_plr","unrelated"),"only the verified companion may accompany the requested recordings");

	for(const auto* p:w::reload_profiles)if(q::supported(p->id))
	for(const auto rear:{vr::hand::left,vr::hand::right})
	for(const int reserve:{0,1,p->ammunition.magazine_capacity-1,p->ammunition.magazine_capacity,1000})
	for(const bool chamber:{false,true})
	for(const auto action:{w::action_state::closed,w::action_state::locked_open,w::action_state::cocked_open,w::action_state::held_open})
	{
		const auto& r=p->ammunition;
		m::state s{49,1,1,false,chamber,0,reserve,0,vr::hand::none,action};
		if(!m::valid(r,s))continue;
		const m::request request{m::operation::quick_load,s.weapon,s.instance_generation,s.revision,rear,rear};
		const auto tx=m::plan(r,s,request);
		check(bool(tx)==(reserve>0),"eligible magazine uses available reserves only");
		if(tx)
		{
			const int rounds=std::min(reserve,r.magazine_capacity-int(!r.plus_one && chamber));
			check(tx.next.magazine_inserted && tx.next.magazine_rounds==rounds && tx.next.reserve_rounds==reserve-rounds,"full and partial magazine fill");
			check(tx.next.action==s.action && tx.next.chamber_loaded==s.chamber_loaded,"insertion never chambers, cocks or releases the action");
			check(m::total_rounds(s)==m::total_rounds(tx.next) && tx.before==m::native_ammo(s) && tx.after==m::native_ammo(tx.next),"magazine projection and conservation");
			check(!m::plan(r,tx.next,request),"replayed transaction rejected");
			auto occupied=tx.next;occupied.magazine_rounds=0;occupied.revision++;
			check(!m::plan(r,occupied,{m::operation::quick_load,occupied.weapon,occupied.instance_generation,occupied.revision,rear,rear}),"inserted empty magazine is never replaced");
		}
		if(action!=w::action_state::held_open)
		{
			s.magazine_hand=vr::hand(1-int(rear));s.held_rounds=2;
			const auto spare=m::plan(r,s,request);
			if(spare)check(spare.next.magazine_hand==s.magazine_hand && spare.next.held_rounds==2,"independently held spare is not consumed or lost");
		}
	}
	{
		m::state s{49,1,1,false,false,0,std::numeric_limits<int>::max()};
		auto tx=m::plan(w::m9::reload_rules,s,{m::operation::quick_load,49,1,1,vr::hand::right,vr::hand::right});
		check(bool(tx) && m::total_rounds(tx.next)==std::numeric_limits<int>::max(),"maximum reserve remains bounded");
		s.revision=UINT64_MAX;
		check(!m::plan(w::m9::reload_rules,s,{m::operation::quick_load,49,1,s.revision,vr::hand::right,vr::hand::right}),"revision cannot wrap");
	}
	for(const auto phase:{c::action::closed,c::action::opening,c::action::open,c::action::closing})
	for(int live=0;live<=6;++live)for(int spent=0;spent<=6-live;++spent)
	for(const int reserve:{0,1,5,6,50})
	{
		c::state s{22,1,1,live,spent,reserve,2,vr::hand::left,phase};
		const auto tx=c::plan({6},s,{c::operation::quick_load,22,1,1,vr::hand::right,vr::hand::right});
		check(bool(tx)==(phase==c::action::open && !live && !spent && reserve>0),"all six chambers must be genuinely empty and fully open");
		if(tx)check(tx.next.live==std::min(6,reserve) && tx.next.reserve==reserve-tx.next.live && tx.next.phase==phase &&
			tx.next.spent==0 && tx.next.held_rounds==2 && tx.next.loader_hand==s.loader_hand && c::total_rounds(s)==c::total_rounds(tx.next),"cylinder fills from reserve and stays open with held loader preserved");
	}
	for(const auto phase:{b::action::closed,b::action::opening,b::action::open,b::action::closing})
	for(unsigned live=0;live<4;++live)for(unsigned spent=0;spent<4;++spent)
	for(const int reserve:{0,1,2,20})
	{
		if(live&spent)continue;
		b::state s{23,1,1,live,spent,reserve,1,vr::hand::left,phase,phase==b::action::closed?0.f:1.f};
		const auto tx=b::plan({2},s,{b::operation::quick_load,23,1,1,vr::hand::right,vr::hand::right});
		check(bool(tx)==(phase==b::action::open && !live && !spent && reserve>0),"Ranger rejects partial, closed and moving chambers");
		if(tx)check(std::popcount(tx.next.live)==std::min(2,reserve) && tx.next.hinge==1 && tx.next.phase==phase &&
			tx.next.held_rounds==1 && tx.next.loader_hand==s.loader_hand && b::total_rounds(s)==b::total_rounds(tx.next),"Ranger fills both or one from reserve and stays open");
	}

	w::hold owner{49,1,vr::hand::right,vr::hand::none,w::hold_source::interaction,1,vr::hand::right,9};
	vr::controller_input::frame input;input.sequence=input.reference_generation=input.continuity_generation=1;input.focused=true;
	input.sampled_at=vr::controller_input::clock::time_point{1s};
	input.grip[1].valid=input.aim[1].valid=true;
	q::dwell timer;
	const auto step=[&](int ms,int slot=0,bool eligible=true){++input.sequence;input.sampled_at+=std::chrono::milliseconds(ms);return timer.update(owner,3,input,slot,eligible,input.sampled_at);};
	check(!step(0),"entry begins dwell");
	check(timer.began() && timer.active(owner,input,input.sampled_at),"begin event and visible loading state start at entry, without an opposite hand");
	check(!timer.update(owner,3,input,0,true,input.sampled_at) && !timer.began(),"duplicate sample never replays start sound");
	for(int n=0;n<9;++n)check(!step(100) && !timer.began() && timer.active(owner,input,input.sampled_at),"continuous dwell keeps HUD status without repeating audio");
	check(!step(99) && step(1),"reload at exactly one second");
	check(!timer.update(owner,3,input,0,true,input.sampled_at),"duplicate input cannot refill");
	check(!step(1,-1) && !step(1),"leaving waist cancels dwell");
	check(timer.began(),"re-entering after cancellation starts one new sound");
	check(!step(999,1),"switching slots resets dwell");
	check(!step(1,0,false) && !step(1),"ineligible or disabled input resets dwell");
	check(!step(999),"no accumulated time from earlier visit");
	input.focused=false;check(!step(1),"focus loss cancels dwell");input.focused=true;check(!step(1),"focus return starts new dwell");
	check(!step(999),"focus gap cannot finish timer");++input.continuity_generation;check(!step(1),"producer interruption resets dwell");
	check(!step(999),"new continuity waits");++input.reference_generation;check(!step(1),"recenter resets dwell");
	check(!step(999),"new reference waits");++owner.rear_revision;check(!step(1),"regrip resets dwell");
	check(!step(999),"new grip waits");owner.rear=vr::hand::left;check(!step(1),"hand swap resets dwell");
	check(!step(999),"new hand waits");++owner.instance_generation;check(!step(1),"identical weapon replacement resets dwell");
	check(!timer.update(owner,3,input,0,true,input.sampled_at+151ms),"stale tracking cancels dwell");
	check(!step(1),"fresh sample restarts");
	for(int n=0;n<4;++n)check(!step(200),"continuous producer supports a slow simulation");
	check(step(200),"slow simulation can complete a one-second dwell");
	timer.reset();check(!timer.active(owner,input,input.sampled_at) && !timer.began(),"completion, interruption and transfer reset the published loading status");
	input.continuity_generation=0;check(!step(1) && !step(1000),"unannotated tracking gaps never complete dwell");
	{
		w::feedback::event sound;sound.owner=owner;sound.reference=input.reference_generation;sound.at=input.sampled_at;sound.quick_reload_start=true;
		input.grip[0].valid=input.aim[0].valid=true;input.grip[1].valid=input.aim[1].valid=false;
		check(w::feedback::fresh(sound,owner,input,true,input.sampled_at),"quick reload start feedback needs only the holding hand");
		auto replacement=owner;++replacement.instance_generation;
		check(!w::feedback::fresh(sound,replacement,input,true,input.sampled_at) &&
			!w::feedback::fresh(sound,owner,input,true,input.sampled_at+101ms),"old instance or stale sound events never play");
	}
	vr::head_pose_bridge::spatial_frame body;body.units_per_meter=40;
	body.head_position={0,0,70};body.head_yaw_axis={{{1,0,0},{0,1,0},{0,0,1}}};
	w::carry::holster_layout layout;layout.waist_width=.3f;layout.waist_down=.7f;
	const auto slots=w::carry::locate_holsters(body,layout);
	check(slots.valid && w::carry::hit(slots,slots.centers[0],3)==w::carry::location::left_waist &&
		w::carry::hit(slots,slots.centers[1],3)==w::carry::location::right_waist,"both configured waist holsters accept the holding hand");
	check(w::carry::hit(slots,slots.centers[2],3)==w::carry::location::absent &&
		w::carry::hit(slots,{NAN,0,0},3)==w::carry::location::absent,"back holster and invalid hand positions never trigger");

	// Each hand has an independent timer; shared reserves are re-observed by the
	// existing native adapter before each transaction, so only the remainder fills.
	m::state left{49,1,1,false,false,0,20},right{49,2,1,false,false,0,20};
	const auto a=m::plan(w::m9::reload_rules,left,{m::operation::quick_load,49,1,1,vr::hand::left,vr::hand::left});
	right.reserve_rounds=a.next.reserve_rounds;
	const auto z=m::plan(w::m9::reload_rules,right,{m::operation::quick_load,49,2,1,vr::hand::right,vr::hand::right});
	check(bool(a) && bool(z) && a.next.magazine_rounds+z.next.magazine_rounds==20 && z.next.reserve_rounds==0,"two same-weapon instances share reserves without duplication");

	std::cout<<"quick reload failures="<<failures<<'\n';return failures?1:0;
}
