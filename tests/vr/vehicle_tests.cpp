#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/vehicles/policy.hpp"
#include "component/vr/gameplay/weapons/g18/profile.hpp"
#include "component/vr/gameplay/weapons/miniuzi/profile.hpp"
#include "component/vr/gameplay/campaign/sequences/vehicle.hpp"
#include "component/vr/gameplay/weapons/miniuzi/reload_profile.hpp"
#include "component/vr/gameplay/weapons/g18/reload_profile.hpp"
#include "component/vr/gameplay/vehicles/script_contract.hpp"
#include "component/vr/gameplay/native_viewmodel_policy.hpp"
#include "component/vr/gameplay/vehicles/model_pose.hpp"
#include "component/vr/gameplay/vehicles/effect.hpp"
#include "component/vr/gameplay/vehicles/fire_intent.hpp"
#include "component/vr/gameplay/vehicles/steering.hpp"
#include "component/vr/gameplay/snowmobile_handle_pose.hpp"
#include "vehicle_steering_tests.hpp"
#include "component/vr/gameplay/weapon_feedback.hpp"
#include "component/vr/scripted_view.hpp"
#include "component/vr/scripted_position.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc,char** argv)
{
	using namespace vr::gameplay;using namespace hands;namespace v=vehicles;namespace w=weapons;
	using namespace std::chrono_literals;int failures{};
	const auto check=[&](bool ok,const char* label){if(!ok){std::cerr<<"FAIL: "<<label<<'\n';++failures;}};
	vehicle_steering_tests::run(check);
	{
		const auto now=vr::controller_input::clock::now();v::fire_press tap{12,now};
		check(tap.pending(11,now+50ms),"released tap survives one native shooting poll");
		check(!tap.pending(12,now+50ms),"one accepted bullet consumes the pending press");
		check(!tap.pending(11,now+151ms) && !tap.pending(11,now-1ms),"expired or backward-clock tap never replays");
		tap={};check(!tap.pending(11,now),"stow or magazine release cancels a pending shot");
		std::vector<std::uint8_t> code{0x2c,0,0x32};
		const auto wait=[&](std::uint32_t tag,bool cached) {
			code.insert(code.end(),{0x53,46,0,0,0,0x53});for(unsigned n=0;n<4;++n)code.push_back(std::uint8_t(tag>>(n*8)));
			if(cached)code.insert(code.end(),{0x75,20});else code.push_back(0x6f);
			code.insert(code.end(),{0xa5,1,0xa4,0x4d,0x34});
		};
		wait(47222,false);wait(47226,true);wait(47228,true);
		const auto sites=v::inspect_animation_waits(code,47222,47228,false);
		check(sites.valid && sites.count==2 && sites.sites[0].begin==3 && sites.sites[0].end==18,"Zodiac skips complete animation wait statements after parameter binding");
		check(sites.sites[1].begin==36 && sites.sites[1].end==52,"reload wait is not treated as a putaway wait");
		for(size_t i=0;i<sites.count;++i){const auto site=sites.sites[i];check(code[site.end-1]==0x4d && code[site.end]==0x34,"continuation has no unconsumed wait arguments");}
		auto truncated=code;truncated.resize(sites.sites[1].end-1);check(!v::inspect_animation_waits(truncated,47222,47228,false).valid,"partial wait cleanup cannot bind");
		code[sites.sites[0].end-2]=0;check(!v::inspect_animation_waits(code,47222,47228,false).valid,"changed wait opcode fails closed");
		code={0x2c,0,0x32};wait(47222,false);wait(47222,true);wait(47228,true);wait(47228,true);wait(47228,true);
		check(v::inspect_animation_waits(code,47222,47228,true).valid,"snowmobile preserves attach detach work between all five waits");
		wait(47228,true);check(!v::inspect_animation_waits(code,47222,47228,true).valid,"extra animation matches reject an ambiguous script");
	}
	{
		const auto now=vr::controller_input::clock::now();w::hold holder{47,3,vr::hand::right,vr::hand::none,w::hold_source::interaction,3};holder.instance_generation=2;
		v::effect_request effect{holder,1,20,4,now,false};v::rendered_gun pose{holder,{},1,20,4,now};
		check(v::effect_state(effect,holder,1,pose,now)==v::effect_readiness::wait,"muzzle FX waits for a hand solve after the shot");
		pose.serial=5;check(v::effect_state(effect,holder,1,pose,now)==v::effect_readiness::ready,"next matching solved hand pose supplies flash position");
		check(v::effect_state(effect,holder,2,pose,now)==v::effect_readiness::reject,"recenter discards queued FX");
		auto changed=holder;++changed.rear_revision;check(v::effect_state(effect,changed,1,pose,now)==v::effect_readiness::reject,"hand transfer cannot inherit pending flash");
		changed=holder;changed.rear=vr::hand::none;check(v::effect_state(effect,changed,1,pose,now)==v::effect_readiness::reject,"stowed gun cannot emit queued flash");
		check(v::effect_state(effect,holder,1,pose,now+151ms)==v::effect_readiness::reject,"late FX is discarded rather than replayed behind the moving vehicle");
		vr::controller_input::frame tracking;tracking.focused=true;tracking.sequence=20;tracking.reference_generation=1;tracking.sampled_at=now;tracking.grip[1].valid=tracking.aim[1].valid=true;
		w::feedback::event shot;shot.owner=holder;shot.reference=1;shot.at=now;shot.kind=w::mechanics::effect::shot;shot.vehicle=true;
		check(w::feedback::fresh(shot,holder,tracking,true,now),"driver shot feedback does not require an unused opposite controller");
		check(!w::feedback::fresh(shot,changed,tracking,true,now),"driver shot feedback rejects ended ownership");
		const auto kick=w::feedback::pattern(w::mechanics::effect::shot),dry=w::feedback::pattern(w::mechanics::effect::dry_fire);
		check(kick.rear_amplitude>dry.rear_amplitude && dry.rear_amplitude>0,"vehicle uses existing distinct shot and dry-fire haptic patterns");
		vr::controller_input::digital_press_gate edge;vr::controller_input::digital_action button;button.active=true;button.generation=1;
		check(!edge.consume(button),"dry-fire feedback arms from neutral");button.down=true;++button.presses;
		check(edge.consume(button) && !edge.consume(button),"empty held trigger produces one dry-fire event");button.down=false;(void)edge.consume(button);button.down=true;++button.presses;
		check(edge.consume(button),"second deliberate empty pull provides feedback again");
	}
	{
		vr::game_view::scripted_view camera;
		check(std::abs(camera.follow_vehicle(1,1,10,100,20)-80)<.001f,"driving camera preserves entry heading");
		check(std::abs(camera.follow_vehicle(1,1,11,100,55)-80)<.001f,"physical head yaw does not turn vehicle reference");
		check(std::abs(camera.follow_vehicle(1,1,12,130,55)-110)<.001f,"steering adds vehicle heading delta to camera");
		check(std::abs(camera.follow_vehicle(1,2,13,140,0)-175)<.001f,"recenter preserves view while still applying vehicle turn");
		float native_yaw{};check(camera.restore_command(0,2,native_yaw,5,0) && std::abs(native_yaw-170)<.001f,"dismount restores current transported heading");
		camera={};const auto before=camera.follow_vehicle(2,1,100,179,0),after=camera.follow_vehicle(2,1,101,-179,0);
		check(std::abs(std::remainder(after-before,360.f)-2)<.001f,"vehicle heading crosses 180 degrees without a full turn");
		check(std::abs(camera.follow_vehicle(2,1,1,40,10)-30)<.001f,"checkpoint time rollback rebases vehicle reference");
	}
	{
		// Actual captured Zodiac A8AF prefix: 3 parameter slots (last optional),
		// then the original native spawnstruct and its local-variable assignment.
		std::array<std::uint8_t,12> code{0x2c,0,0x2c,1,0x2c,2,0x32,0x1a,0xa5,1,0x17,3};
		check(v::target_body_offset(code)==7,"target replacement executes only after all native parameters are bound");
		for(size_t n=0;n<code.size();++n)check(!v::target_body_offset({code.data(),n}),"incomplete argument prologue cannot install target replacement");
		for(size_t n=0;n<code.size();++n){auto bad=code;bad[n]^=0xff;check(!v::target_body_offset(bad),"changed native parameter/return contract is rejected");}
		const auto continuation=v::builtin_result_code(0x345);
		check(continuation==std::array<char,4>{char(0x1a),char(0x45),char(3),char(0x19)},"replacement continuation returns builtin value without clearing caller parameters");
		// Native VM contract: 4D clears to type 7, not incoming type 8. Simulate
		// the observed frame boundary to prevent reintroducing an entry clear.
		for(unsigned supplied=0;supplied<=3;++supplied)
		{
			std::vector<unsigned> stack{7,1,8};for(unsigned i=0;i<supplied;++i)stack.push_back(1);
			auto unsafe=stack;while(unsafe.back()!=7)unsafe.pop_back();
			check(unsafe.size()==1,"old entry clear crosses PRECODEPOS and consumes the caller object");
			const auto caller=stack[1];unsigned bound{};
			for(unsigned i=0;i<v::target_body_offset(code);)
			{
				if(code[i]==0x2c){if(stack.back()!=8){stack.pop_back();++bound;}i+=2;}
				else {check(code[i]==0x32 && stack.back()==8,"native argument binder stops at PRECODEPOS");stack.back()=7;++i;}
			}
			check(bound==supplied && stack.size()==3 && stack[1]==caller && stack.back()==7,"optional target arguments bind without releasing caller references");
		}
	}
	w::hold retained{34,5,vr::hand::right,vr::hand::none,w::hold_source::interaction,5};retained.instance_generation=11;
	const auto driver_owner=model_pose_owner(retained,true,true);
	check(!driver_owner.weapon && !driver_owner.can_fire() && retained.weapon==34 && retained.can_fire(),
		"driver hands-only rig ignores suspended firearm without changing stored carry owner");
	check(model_pose_owner(retained,true,false).id()==retained.id() && model_pose_owner(retained,false,true).id()==retained.id(),
		"on-foot empty-hand handoff and actual weapon rigs retain owner validation");
	{
		using namespace vr::gameplay::hands::pose_math;
		std::array<bone_definition,4> bones{};std::array<anchor,4> rest{};
		const std::array<const char*,4> names{"j_gun","stock","sight","tag_clip"};
		const std::array<int,4> parents{-1,0,1,0};
		const anchor root{{10,-20,30},{0,0,.70710678f,.70710678f}};
		const std::array<anchor,4> bind{{root,compose(root,{{-5,0,3},{0,0,0,1}}),
			compose(root,{{-4,0,4},{0,0,0,1}}),compose(root,{{1,0,-2},{0,0,0,1}})}};
		for(unsigned i=0;i<bones.size();++i){bones[i].name=names[i];bones[i].parent=parents[i];bones[i].bind.position=bind[i].position;bones[i].bind.rotation=bind[i].rotation;}
		check(v::model_rest_pose(bones,{},rest),"bind-space receiver with translated rotated root accepted");
		const anchor gun{{200,300,-100},{.38268343f,0,0,.92387953f}};
		for(unsigned i=0;i<bones.size();++i)
		{
			const auto delta=v::rigid_delta(rest[i],bind[i]);const auto point=compose(compose(gun,delta),bind[i]);
			check(length(sub(point.position,compose(gun,rest[i]).position))<.001f,"rigid source vertices receive one bind correction, without separating gun groups");
			check(length(sub(delta.position,inverse(root).position))<.001f,"unmodified groups share a single receiver transform");
		}
		const std::array<vr::gameplay::hands::part_pose,1> folded{{{"stock",{{-5,0,3},{0,0,1,0}}}}};
		check(v::model_rest_pose(bones,folded,rest),"authored folded stock accepted as a parent-local pose");
		check(length(sub(rest[2].position,vec{-6,0,4}))<.001f,"sight child follows folded parent without losing its bind-relative offset");
		const auto magazine=rest[3];const auto local=compose(inverse(magazine),v::rigid_delta(rest[3],bind[3]));
		check(length(sub(compose(compose(compose(gun,magazine),local),bind[3]).position,compose(gun,magazine).position))<.001f,
			"detachable magazine and receiver use compatible bind-space placement");
		bones[2].parent=2;check(!v::model_rest_pose(bones,folded,rest),"cyclic part hierarchy rejected");
	}
	check(v::classify("af_chase",true,true,true,"zodiac_player")==v::kind::zodiac,"boat requires live driver relation");
	check(v::classify("cliffhanger",true,true,true,"snowmobile_player")==v::kind::snowmobile,"snowmobile driver admitted");
	check(v::classify("af_chase",false,true,true,"zodiac_player")==v::kind::none &&
		v::classify("af_chase",true,false,true,"zodiac_player")==v::kind::none &&
		v::classify("af_chase",true,true,false,"zodiac_player")==v::kind::none &&
		v::classify("af_chase",true,true,true,"player_rig")==v::kind::none,"death, dismount, other vehicle and ending rigs do not inherit driving");
	vr::head_pose_bridge::spatial_frame body;body.units_per_meter=40;body.head_position={0,0,70};body.head_yaw_axis={{{1,0,0},{0,1,0},{0,0,1}}};
	const auto chest=v::chest(body);check(std::abs(chest.position[1])<.001f,"vehicle gun centered on chest");
	const auto left=rotate(chest.rotation,{0,1,0}),barrel=rotate(chest.rotation,{1,0,0});
	check(dot(left,body.head_yaw_axis[0])<-.999f && barrel[1]>.9f && barrel[2]<-.42f,"gun left side faces player; muzzle left and down like Exodus");
	const auto waist=w::carry::locate_holsters(body);
	check(v::reload_zone(body,waist,chest.position)==2 && v::reload_zone(body,waist,waist.centers[0])==0 && v::reload_zone(body,waist,waist.centers[1])==1,"chest and both configured waist zones reload");
	check(v::reload_zone(body,waist,waist.centers[2])==-1 && v::reload_zone(body,waist,{NAN,0,0})==-1,"back and invalid coordinates cannot reload");
	for(float units:{20.f,40.f,80.f})
	{
		body.units_per_meter=units;body.head_position={100,-50,90};body.head_yaw_axis={{{0,1,0},{-1,0,0},{0,0,1}}};
		const auto pose=v::chest(body);check(dot(rotate(pose.rotation,{0,1,0}),body.head_yaw_axis[0])<-.999f && v::chest_distance(body,pose.position)<.001f,"Exodus chest orientation remains body-relative at all world scales");
	}
	check(std::isfinite(v::aim_score(v::kind::zodiac,{1000,200,150},1300)) && !std::isfinite(v::aim_score(v::kind::zodiac,{1000,500,0},1300)) &&
		!std::isfinite(v::aim_score(v::kind::zodiac,{1000,0,400},1300)),"native zodiac horizontal and vertical assistance bounds follow gun");
	check(std::isfinite(v::aim_score(v::kind::snowmobile,{700,100,0},750)) && !std::isfinite(v::aim_score(v::kind::snowmobile,{800,0,0},750)) &&
		!std::isfinite(v::aim_score(v::kind::snowmobile,{-500,0,0},750)),"native snowmobile cone and distance do not become global aim assist");
	{
		const auto at=v::chest(body).position;
		const auto expanded=add(at,scale(vr::head_pose_bridge::body_slots_frame(body).head_yaw_axis[0],.22f*body.units_per_meter));
		check(v::weapon_grab_distance(body,expanded)<=1 && v::chest_distance(body,expanded)>1,"generous grab reach does not expand automatic reload dwell");
		const auto slots=w::carry::locate_holsters(body);
		check(v::magazine_supply_distance(body,slots,expanded)<=1 && v::magazine_supply_distance(body,slots,slots.centers[0])<=1,"manual magazine supply accepts chest and waist");
		check(v::magazine_supply_distance(body,slots,add(at,vec{1000,1000,1000}))>1,"remote points cannot spawn a driver magazine");
	}
	{
		for(const auto* reload:{&w::miniuzi::physical,&w::g18::physical})
		{
			const auto& rules=reload->interaction;w::physical_reload::well_motion motion{{0,0,0},false,true};
			check(!w::physical_reload::advance_well(rules,motion,{0,0,0},1,false).insert,"magazine drawn inside well requires withdrawal");
			const float outside=-rules.well_capture_below-rules.well_withdraw_margin-.02f;
			(void)w::physical_reload::advance_well(rules,motion,{0,0,outside},1,false);
			check(!motion.withdraw,"chest-drawn magazine outside well arms insertion");
			check(!w::physical_reload::advance_well(rules,motion,{0,0,0},1,true).insert,"spare cannot replace an inserted magazine");
			check(w::physical_reload::advance_well(rules,motion,{0,0,0},1,false).insert,"staged spare enters once B/Y frees the feed");
			motion={{0,0,outside},false,false};
			check(!w::physical_reload::advance_well(rules,motion,{0,0,0},-1,false).insert,"reversed magazine cannot insert");
			motion={{1,0,0},false,false};
			check(!w::physical_reload::advance_well(rules,motion,{0,0,0},1,false).insert && motion.withdraw,"tracking jump cannot auto-insert");
		}
	}
	{
		const auto falling=sequences::vehicle::waterfall(true,true,true,false);
		check(falling.stage==sequences::phase::execution && falling.vehicle==v::kind::none && falling.suspend_weapons && !falling.hide_body_arms &&
			falling.camera==vr::game_view::camera_profiles::aligned,"waterfall retires driver hands while preserving narrative arms and free look");
		check(sequences::vehicle::waterfall(true,true,false,true).suspend_weapons,"post-dismount blend target retains cinematic ownership");
		check(sequences::vehicle::waterfall(false,true,true,false).stage==sequences::phase::none &&
			sequences::vehicle::waterfall(true,false,true,false).stage==sequences::phase::none &&
			sequences::vehicle::waterfall(true,true,false,false).stage==sequences::phase::none,"stale waterfall flag cannot capture unrelated rigs or on-foot state");
	}
	vr::controller_input::frame input;input.focused=true;input.sequence=input.reference_generation=input.continuity_generation=1;input.sampled_at=vr::controller_input::clock::now();
	input.grip[1].valid=input.aim[1].valid=true;w::hold owner{3,1,vr::hand::right,vr::hand::none,w::hold_source::interaction,1};owner.instance_generation=9;
	w::quick_reload::dwell timer;
	check(!timer.update(owner,1,input,2,true,input.sampled_at) && !timer.began(),"ordinary quick reload still rejects chest");
	check(!timer.update(owner,1,input,2,true,input.sampled_at,true) && timer.began(),"driver chest starts the same dwell without a setting");
	input.sampled_at+=999ms;++input.sequence;check(!timer.update(owner,1,input,2,true,input.sampled_at,true),"partial dwell cannot load");
	input.sampled_at+=1ms;++input.sequence;check(timer.update(owner,1,input,2,true,input.sampled_at,true),"one second fills an absent driver magazine");
	check(!timer.update(owner,1,input,2,false,input.sampled_at,true) && !timer.active(owner,input,input.sampled_at),"inserted magazine cancels even a completed dwell");
	++input.sequence;timer.update(owner,1,input,2,true,input.sampled_at,true);input.sampled_at+=1s;++input.sequence;++input.reference_generation;
	check(!timer.update(owner,1,input,2,true,input.sampled_at,true),"recenter cannot finish old reload");
	input.sampled_at+=1s;++input.sequence;check(!timer.update(owner,1,input,0,true,input.sampled_at,true),"switching chest to waist restarts dwell");
	input.focused=false;check(!timer.update(owner,1,input,0,true,input.sampled_at,true),"focus loss cancels reload");
	vr::game_view::scripted_position translation;translation.offset(1,1,{0,0,0},vr::game_view::camera_profiles::aligned);
	check(length(translation.offset(1,1,{10,-20,15},vr::game_view::camera_profiles::aligned))<=.05001f,"driving reuses bounded five-centimeter HMD translation");

	// A real, minimal H2 statement sequence. The deliberately separated copies
	// test ambiguity rejection and native operand width, not a second decoder.
	std::vector<std::uint8_t> code{0x88,20,0x51,0x16,0xb0,0xa0,1,0x7b,0x88,20,0x52,0x16,0xb0,0x5b,
		0x88,20,0x51,0x16,0xb0,0x79,0x38,0xb0,18,0,
		0x2b,0,0,0x80,0x3f,0xa0,32,0x88,20,0x52,0x16,0xb0,0x5b,0x2b,0,0,0x80,0x3f,0x34,0x34,0x34,0x34};
	const auto sites=v::inspect_script(code,0xb016);check(sites.valid && sites.consume_end==14 && sites.reload==24 && sites.refill==29 && sites.idle==42,"native reload and deferred decrement have exact instruction boundaries");
	check(!v::inspect_script(code,0xbfd3).valid,"another ammo field is never hooked");
	for(size_t length=0;length<38;++length)check(!v::inspect_script({code.data(),length},0xb016).valid,"truncated bytecode rejected");
	auto duplicate=code;duplicate.insert(duplicate.end(),code.begin(),code.end());check(!v::inspect_script(duplicate,0xb016).valid,"multiple candidate statements fail closed");
	code[22]=255;check(!v::inspect_script(code,0xb016).valid,"branch outside function rejected");
	// Optional read-only native witnesses: argv file, start, length, field.
	for(int i=1;i+3<argc;i+=4)
	{
		std::ifstream f(argv[i],std::ios::binary);std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};
		const auto start=std::stoul(argv[i+1],nullptr,0),length=std::stoul(argv[i+2],nullptr,0);const auto field=static_cast<std::uint16_t>(std::stoul(argv[i+3],nullptr,0));
		check(start<=bytes.size() && length<=bytes.size()-start && v::inspect_script({bytes.data()+start,length},field).valid,"captured native vehicle function contract");
	}
	std::cout<<"vehicle failures="<<failures<<'\n';return failures?1:0;
}
