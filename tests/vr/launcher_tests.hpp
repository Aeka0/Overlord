#pragma once
#include "component/vr/gameplay/hand_rig_builder.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/launcher_feed.hpp"
#include "component/vr/gameplay/launcher_targeting.hpp"
#include "component/vr/gameplay/launcher_presenter.hpp"
#include "component/vr/gameplay/weapons/rpg/profile.hpp"
#include "component/vr/gameplay/weapons/at4/profile.hpp"
#include "component/vr/gameplay/weapons/stinger/profile.hpp"
#include "component/vr/gameplay/weapons/javelin/profile.hpp"
#include "component/vr/gameplay/native_shot_history.hpp"
#include "component/vr/gameplay/ads_alignment.hpp"
#include "component/vr/gameplay/controller_ads.hpp"
#include "component/vr/gameplay/weapon_carry_pose.hpp"
#include "component/vr/gameplay/weapon_carry_grip.hpp"
#include "component/vr/gameplay/weapon_sound_window.hpp"
#include "javelin_rig_data.hpp"
#include "component/vr/gameplay/weapon_carry_profiles.hpp"
#include "component/vr/gameplay/javelin_display_control.hpp"
#include "component/vr/gameplay/javelin_lock_gate.hpp"
#include "component/vr/spatial_panel.hpp"

namespace launcher_tests
{
	template<class Check>void run(Check check)
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		{
			// Native 44133/BCBD statement layout, including its separate pickup gate.
			constexpr std::array<std::uint8_t,72> code{0x2c,0x00,0x32,0x6f,0x1b,0x2f,0x00,0x21,0x23,0x03,0x00,0x6f,0x21,0x25,0xb0,0x14,0x00,
				0x41,0xa9,0x4b,0x83,0x2b,0x00,0x00,0x80,0x3f,0x61,0xb0,0x02,0x00,0x79,0x19,0x77,0x0b,0x00,0x00,0x00,
				0x41,0xa9,0x4b,0x83,0x79,0x90,0xb0,0x02,0x00,0x79,0x19,0x41,0xa9,0x1c,0x83,0x17,0x01,0x53,0x12,0xb2,0x00,0x00,0x6f,0x1c,0x25,0x01,0x40,0x02,0x00,0x79,0x19,0xa0,0x01,0x19,0x34};
			check(javelin_screen::full_ads_call(code,0x834b)==std::optional<std::size_t>{21},
				"only the full-ADS lock query is selected; partial-ADS pickup query stays native");
			auto changed=code;changed[25]=0x3e;
			check(!javelin_screen::full_ads_call(changed,0x834b) && !javelin_screen::full_ads_call(code,0x834c),
				"changed threshold or different builtin cannot inherit the native lock adapter");
			changed=code;changed[42]=0x61;
			check(!javelin_screen::full_ads_call(changed,0x834b),"changed partial-ADS branch rejects the script contract");
			check(!javelin_screen::lock_gate_ready(true,1,9,400) && !javelin_screen::lock_gate_ready(true,0,0,0) &&
				!javelin_screen::lock_gate_ready(true,1,0,200),"inserted ammo cannot start lock before native reload/cooldown completes");
			check(javelin_screen::lock_gate_ready(true,1,0,0) && !javelin_screen::lock_gate_ready(false,1,0,0),
				"native-ready near-eye Javelin may reacquire immediately; moving away ends admission");
		}
		{
			const auto& authored=reviewed_muzzles[0].muzzle;muzzle_frame muzzle;muzzle.position=authored.position;muzzle.units_per_meter=39.37007874f;
			for(unsigned i=0;i<3;++i){vec axis{};axis[i]=1;muzzle.axis[i]=rotate(normalize(authored.rotation),axis);}
			const vec eyepiece{-.052678456f*muzzle.units_per_meter,.1841611f*muzzle.units_per_meter,.0056947f*muzzle.units_per_meter};
			muzzle.head_position=add(eyepiece,{-.30f*muzzle.units_per_meter,0,0});muzzle.head_forward={1,0,0};
			check(!javelin_screen::near_eyepiece(muzzle,false),"aligned but distant Javelin cannot enter ADS");
			muzzle.head_position=add(eyepiece,{-.10f*muzzle.units_per_meter,0,0});
			const auto original=muzzle;
			check(javelin_screen::near_eyepiece(muzzle,false) && muzzle.position==original.position && muzzle.axis==original.axis,
				"actual eyepiece proximity admits ADS without moving or snapping the weapon");
			javelin_screen::proximity_controls proximity;hold owner{13,1,hand::right};owner.instance_generation=1;
			vr::controller_input::frame f;f.sequence=f.reference_generation=1;f.focused=true;f.grip[1].valid=true;f.trigger[1]={true,true};
			check(proximity.consume(f,owner,true,muzzle,f.sampled_at).open && !proximity.consume(f,owner,true,muzzle,f.sampled_at).fire_ready,
				"near-eye entry with a held trigger requires release before fire");
			f.trigger[1].down=false;f.secondary[1]={true,true};++f.sequence;
			auto state=proximity.consume(f,owner,true,muzzle,f.sampled_at);check(state.open && state.fire_ready,"button experiment does not close proximity ADS");
			muzzle.head_position=add(eyepiece,{-.17f*muzzle.units_per_meter,0,0});
			check(proximity.consume(f,owner,true,muzzle,f.sampled_at).open && !javelin_screen::near_eyepiece(muzzle,false),"separate entry and exit distances prevent chatter");
			muzzle.head_position=add(eyepiece,{-.25f*muzzle.units_per_meter,0,0});
			check(!proximity.consume(f,owner,true,muzzle,f.sampled_at).open,"moving away exits Javelin ADS");
			check(std::abs(javelin_screen::optical_half_y(25)-.166271f)<.000001f &&
				std::abs(javelin_screen::canvas_aspect-16.f/9)<.000001f,"native optical FOV and design aspect do not depend on capture texture size");
		}
		{
			muzzle_frame aim;aim.position={-1700,2400,130};const auto q=normalize(quat{.2f,-.1f,.3f,.8f});
			for(unsigned i=0;i<3;++i){vec unit_axis{};unit_axis[i]=1;aim.axis[i]=rotate(q,unit_axis);}
			const vec head{-1710,2407,132};const std::array<vec,3> head_axis{{{0,1,0},{-1,0,0},{0,0,1}}};
			vr::spatial_panel::matrix vp{};
			for(unsigned i=0;i<3;++i){vp[i*4]=-aim.axis[1][i]/.31275076f;vp[i*4+1]=aim.axis[2][i]/.166271f;vp[i*4+3]=aim.axis[0][i];}vp[14]=.1f;
			for(const vec offset:{vec{1000,0,0},vec{1000,50,-20},vec{4000,-80,60}})
			{
				auto target=aim.position;for(unsigned i=0;i<3;++i)target=add(target,scale(aim.axis[i],offset[i]));
				const auto lock=launcher::reticle_delta(sub(target,head),head,aim,head_axis);
				vr::spatial_panel::projected_quad pixel;const vr::spatial_panel::quad points{target,target,target,target};
				check(vr::spatial_panel::project(points,aim.position,vp,.001f,pixel),"shared launcher camera projects a native target");
				check(std::abs(pixel[0][0]/pixel[0][3]+dot(lock,head_axis[1])/dot(lock,head_axis[0])/.31275076f)<.00001f &&
					std::abs(pixel[0][1]/pixel[0][3]-dot(lock,head_axis[2])/dot(lock,head_axis[0])/.166271f)<.00001f,
					"screen target and native lock use the same origin and axes despite head/launcher disagreement");
			}
		}
		for(const std::string_view name:{"at4","at4_player","stinger","stinger_anti_air","javelin","javelin_dcburn"})
		{
			carry::inventory owned;const carry::identity id{13,1};const std::array<carry::owned_instance,1> stock{{{id,carry::profile_for(name)}}};
			check(owned.reconcile_instances(stock) && owned.equip(id,hand::left),"asymmetric launcher may be picked up in the left hand");
			check(owned.find(id)->owner.rear==hand::none && owned.find(id)->owner.support==hand::left && !owned.find(id)->owner.can_fire(),
				"left pickup is support-only and cannot fire");
			check(!owned.control(id,hand::left) && owned.control(id,hand::right),"only right hand may acquire launcher control");
			const auto release=owned.release(id,2,carry::location::absent,true,[](const auto&){return false;});
			check(release.action==carry::outcome::carry_only && owned.find(id)->owner.support==hand::left && !owned.find(id)->owner.can_fire(),
				"releasing the right control never promotes the left support");
			check(owned.control(id,hand::right) && owned.invariant(),"right hand can reacquire the carried launcher");
			owned.release(id,3,carry::location::back,true,[](const auto&){return false;});
			check(owned.draw(carry::location::back,hand::left).action==carry::outcome::drawn && !owned.find(id)->owner.can_fire(),
				"left holster draw retains support-only authority");
		}
		for(hand actor:{hand::left,hand::right})
		{
			carry::inventory owned;const carry::identity id{14,1};const std::array<carry::owned_instance,1> stock{{{id,carry::profile_for("rpg_player")}}};
			owned.reconcile_instances(stock);check(owned.equip(id,actor) && owned.find(id)->owner.rear==actor && owned.find(id)->owner.can_fire(),
				"RPG keeps equal left/right control authority");
		}
		{
			javelin_screen::controls controls;hold owner{13,1,hand::right,hand::left,hold_source::interaction,1};owner.instance_generation=1;
			vr::controller_input::frame f;f.sequence=f.reference_generation=f.continuity_generation=1;f.focused=true;f.grip[1].valid=true;
			f.trigger[1].active=f.secondary[0].active=f.secondary[1].active=true;
			const auto step=[&]{++f.sequence;f.sampled_at+=std::chrono::milliseconds(11);return controls.consume(f,owner,true,f.sampled_at);};
			controls.consume(f,owner,true,f.sampled_at);f.trigger[1].down=true;++f.trigger[1].presses;
			auto s=step();check(s.open && !s.fire_ready,"trigger entry opens display without firing");
			check(!step().fire_ready && !controls.consume(f,owner,true,f.sampled_at).fire_ready,"held entry and repeated sampling cannot arm firing");
			f.trigger[1].down=false;check(step().fire_ready,"releasing entry trigger arms a later shot");
			f.trigger[1].down=true;++f.trigger[1].presses;s=step();check(s.open && s.fire_ready,"subsequent trigger fires without exiting display");
			f.secondary[0].down=true;++f.secondary[0].presses;s=step();check(!s.open && !s.fire_ready,"Y exits even with the firing trigger held");
			f.secondary[0].down=false;f.trigger[1].down=false;step();f.secondary[1].down=true;++f.secondary[1].presses;
			check(step().open,"B enters display");f.secondary[1].down=false;step();f.secondary[1].down=true;++f.secondary[1].presses;
			check(!step().open,"B exits display");
			check(!controls.consume(f,owner,false,f.sampled_at).open,"pause cancels display input ownership");
			f.trigger[1].down=true;++f.trigger[1].presses;check(!step().open,"held trigger on resume cannot enter or fire");
			f.trigger[1].down=false;step();f.trigger[1].down=true;++f.trigger[1].presses;check(step().open,"fresh trigger after resume enters");
			f.trigger[1].down=false;step();f.trigger[1].active=false;check(!step().fire_ready,"missing trigger sensor removes firing readiness");
			f.trigger[1].active=true;f.trigger[1].down=true;++f.trigger[1].generation;++f.trigger[1].presses;
			check(!step().fire_ready,"reconnected held trigger must release before firing");
			owner.rear=hand::none;check(!step().open,"left support-only hold cannot operate the CLU");
		}
		{
			const auto& models=javelin_rig_data::models;const auto& bones=javelin_rig_data::bones;
			const auto resolved=resolve_rig(models,bones);
			check(!resolved.rejection && resolved.contract=="single-root-reviewed-muzzle" && resolved.muzzle_forward_dot<.9961947f,
				"real Javelin bind reaches the profile through its measured off-axis muzzle contract");
			if(!resolved.rejection)
			{
				const auto match=javelin::select(models,models[1],resolved.layout,bones);
				const auto library=bind_weapon_poses(resolved.layout,bones,javelin::base);
				check(match.value==&javelin::base && library.valid && library.safe_index &&
					launcher::bind_parts(resolved.layout,bones,javelin::feed).valid,
					"witnessed hand model and receiver pass the whole rig, profile, fingers and launcher binding chain");
				for(auto actor:{hand::left,hand::right})
				{
					hold owner{13,1,actor,hand(1-int(actor)),hold_source::interaction,1};
					carry::pose_profile adapted(javelin::base,owner,resolved.layout,library);
					std::array<bone,74> posed{};for(unsigned i=0;i<posed.size();++i)posed[i]=bones[i].bind;
					check(apply_poses(resolved.layout,library,adapted.value,adapted.value.wrists,{1,1},true,posed),
						"both Javelin grip roles accept authored poses after real rig admission");
				}
			}
			auto unknown=models;unknown[1].name="unreviewed_launcher";
			check(resolve_rig(unknown,bones).rejection,"generic firearm axis tolerance is not relaxed for other receivers");
			const auto muzzle=resolved.muzzle_bone;
			if(muzzle<0)return; // The admission assertion above already reports a broken fixture.
			auto wrong=bones;wrong[muzzle].bind.rotation[1]*=-1;
			check(resolve_rig(models,wrong).rejection,"opposite pitch with the same forward dot cannot impersonate the Javelin contract");
			wrong=bones;wrong[muzzle].bind.rotation={0,0,0,1};
			check(resolve_rig(models,wrong).rejection,"known Javelin cannot silently use an altered straight muzzle");
			wrong=bones;wrong[muzzle].bind.position[1]+=1;
			check(resolve_rig(models,wrong).rejection,"displaced Javelin muzzle rejected");
			wrong=bones;wrong[muzzle].parent=69;
			check(resolve_rig(models,wrong).rejection,"Javelin muzzle cannot move under another receiver part");
			wrong=bones;const auto rotation=normalize(quat{.2f,.3f,-.1f,.8f});
			for(unsigned i=68;i<wrong.size();++i)
			{wrong[i].bind.position=add(rotate(rotation,wrong[i].bind.position),{7,-3,9});wrong[i].bind.rotation=multiply(rotation,wrong[i].bind.rotation);}
			check(!resolve_rig(models,wrong).rejection,"reviewed muzzle contract is receiver-local, independent of bind root orientation");
		}
		for(hand rear:{hand::left,hand::right})
		{
			const auto other=hand(1-int(rear));carry::inventory inventory;const carry::identity id{29,7};
			const std::array<carry::owned_instance,1> stock{{{id,{}}}};inventory.reconcile_instances(stock);inventory.equip(id,rear);
			const auto before=inventory.find(id)->owner;
			launcher::scene_frame binding;binding.owner=before;binding.definition=&rpg::feed;binding.assembly=1;binding.binding.valid=true;
			check(inventory.support(id,other),"RPG support sound starts only after carry accepts the grip");
			const auto held=inventory.find(id)->owner;
			check(held.revision!=binding.owner.revision && launcher::owns_feed(held,binding),
				"support revision/render gap cannot relinquish the bound physical launcher feed to native auto reload");
			const auto grab=launcher::support_changed(before,held);
			check(grab.change==launcher::support_change::grab && grab.actor==other &&
				launcher::support_changed(held,held).change==launcher::support_change::none,"one confirmed acquisition emits one support transition, not one per held frame");
			const auto released=inventory.release(id,1u<<unsigned(other),carry::location::absent,false,[](const auto&){return false;});
			const auto after=inventory.find(id)->owner;
			check(released.action==carry::outcome::support_released && launcher::owns_feed(after,binding) &&
				launcher::support_changed(held,after).change==launcher::support_change::release,"support release emits once and keeps native reload blocked across the stale scene revision");
			auto replaced=after;++replaced.instance_generation;
			check(!launcher::owns_feed(replaced,binding) && launcher::support_changed(held,replaced).change==launcher::support_change::none,"a different physical instance inherits neither feed admission nor grip sound");
			auto dropped=after;dropped.rear=hand::none;dropped.support=hand::none;
			check(!launcher::owns_feed(dropped,binding) && launcher::support_changed(held,dropped).change==launcher::support_change::none,"loss of the weapon is not a support-release sound");
			binding.binding.valid=false;check(!launcher::owns_feed(after,binding),"unvalidated launcher bindings cannot claim native feed authority");
		}
		{
			const auto grab=launcher::support_sound(rpg::feed,launcher::support_change::grab),release=launcher::support_sound(rpg::feed,launcher::support_change::release);
			check(grab.name && release.name && std::string_view(grab.name)=="weap_rpg_lift_plr" && std::string_view(release.name)==grab.name && grab.window && release.window,
				"RPG grip and release use distinct bounded ranges of lift, never twist");
			check(!grab.notetrack_weapon && !release.notetrack_weapon,
				"RPG windows preserve the launcher's own notetrack source after shared-source integration");
			const auto* cue=find_weapon_sound_cue("h1_foley/wpfoly_rpg_reload_lift_v1",48000,40969);
			check(cue!=nullptr,"RPG support windows recognize the captured stock recording format");
			if(cue)
			{
				const auto a=resolve_sound_window(*cue,grab.part,grab.window),b=resolve_sound_window(*cue,release.part,release.window);
				check(a.valid && b.valid && a.duration_ms<=100 && b.duration_ms<=100 && a.begin_ms+a.duration_ms<b.begin_ms,
					"RPG support windows are short, non-overlapping, and terminate before natural EOF");
				auto invalid=*grab.window;invalid.recording="different_recording";check(!resolve_sound_window(*cue,sound_part::whole,&invalid).valid,"a sound window cannot cut an unrelated alias-selected recording");
				invalid=*grab.window;invalid.end_ms=1000;check(!resolve_sound_window(*cue,sound_part::whole,&invalid).valid,"range beyond recording EOF is rejected");
				invalid=*grab.window;invalid.begin_ms=invalid.end_ms;check(!resolve_sound_window(*cue,sound_part::whole,&invalid).valid,"empty explicit sound range is rejected");
				check(!resolve_sound_window(*cue,sound_part::first,grab.window).valid,"explicit window cannot silently combine with another split policy");
			}
			check(!at4::feed.support_grab_sound.name && !stinger::feed.support_release_sound.name && rpg::feed.load_sound.window==nullptr,
				"support cues are RPG-only and do not trim the successful rocket insertion sound");
		}
		for(auto actor:{hand::left,hand::right})
		{
			launcher::rocket_hold hold;ammunition::projection ammo{0,3};bool writable=false;
			const auto commit=[&](ammunition::projection next){if(!writable)return false;ammo=next;return true;};
			check(!launcher::draw(rpg::feed,hold,actor,ammo,commit) && hold.actor==hand::none && ammo.reserve==3,"failed rocket draw retains native budget and free hand");
			writable=true;check(launcher::draw(rpg::feed,hold,actor,ammo,commit) && ammo.reserve==2,"RPG reserves exactly one rocket for either hand");
			check(!launcher::seated(hold,{0,0,0},1),"overlapping new rocket is not a completed load");
			check(!launcher::seated(hold,{.12f,0,0},-1) && !hold.approached,"backwards rocket cannot arm insertion");
			check(!launcher::seated(hold,{.12f,.2f,0},1) && !hold.approached,"approach must pass through the real radial envelope");
			check(!launcher::seated(hold,{.12f,0,0},1) && hold.approached && launcher::seated(hold,{.02f,0,0},.8f),"front approach followed by seating accepts broad natural alignment");
			writable=false;check(!launcher::settle(hold,ammo,true,commit) && hold.actor==actor && ammo.loaded==0,"rejected seating keeps rocket escrow");
			writable=true;check(launcher::settle(hold,ammo,true,commit) && ammo.loaded==1 && ammo.reserve==2 && hold.actor==hand::none,"seating transfers escrow without a second debit");
			check(!launcher::draw(rpg::feed,hold,actor,ammo,commit),"loaded RPG rejects a second rocket");
			ammo.loaded=0;check(launcher::draw(rpg::feed,hold,actor,ammo,commit),"empty RPG may load again after a shot");
			writable=false;check(!launcher::settle(hold,ammo,false,commit) && hold.actor==actor,"failed cleanup retains refundable rocket");
			writable=true;check(launcher::settle(hold,ammo,false,commit) && ammo.reserve==2 && launcher::settle(hold,ammo,false,commit) && ammo.reserve==2,"interruption refunds exactly once");
			for(const auto* p:{&at4::feed,&stinger::feed,&javelin::feed})check(!launcher::draw(*p,hold,actor,ammo,commit),"disposable and native-reload launchers have no manual rocket draw");
		}
		muzzle_frame muzzle;muzzle.position={5,7,9};muzzle.axis={vec{1,0,0},vec{0,1,0},vec{0,0,1}};
		const vec eye{0,0,10},target{105,17,29};
		for(const auto rotation:{quat{0,0,0,1},normalize(quat{.2f,.3f,.7f,.1f})})
		{
			std::array<vec,3> axis{rotate(rotation,{1,0,0}),rotate(rotation,{0,1,0}),rotate(rotation,{0,0,1})};
			const auto delta=launcher::reticle_delta(sub(target,eye),eye,muzzle,axis);
			check(std::abs(dot(delta,axis[0])-100)<.0001f && std::abs(dot(delta,axis[1])-10)<.0001f && std::abs(dot(delta,axis[2])-20)<.0001f,
				"reticle projection follows the launcher while the native view rotates independently");
		}
		check(stinger::feed.guided && javelin::feed.guided && !at4::feed.guided && !rpg::feed.guided,
			"only native guided launchers adapt lock projection; campaign AT4 and RPG remain unguided");
		for(int h=0;h<2;++h)
		{
			const auto control=carry::control_grip(rpg::base,h,{1,0,0,0});
			check(control.position[0]>rpg::base.wrists[0].position[0]+4.f,"either RPG controlling hand uses the forward grip, ahead of the rear support");
		}
		constexpr float units=39.37007874f;
		const auto old_overlap=launcher::tail_contact(rpg::feed,rpg::feed.rocket_rest,units);
		launcher::rocket_hold probe{hand::left,true};
		check(!launcher::seated(probe,old_overlap,1),"matching the rocket root cannot substitute for tail contact at the tube mouth");
		for(const auto q:{quat{0,0,0,1},quat{0,.5f,0,.8660254f}})
		{
			const auto middle=scale(add(rpg::feed.tail_start,rpg::feed.tail_end),.5f);
			anchor rocket{sub(rpg::feed.load_mouth,rotate(q,middle)),q};
			const auto point=launcher::tail_contact(rpg::feed,rocket,units);
			check(launcher::seated(probe,point,rotate(q,{1,0,0})[0]),"real tail segment at the tube mouth loads straight or at 60 degrees");
			check(!launcher::seated(probe,point,-1),"tail overlap still cannot load a backwards rocket");
		}
		check(!launcher::seated(probe,launcher::tail_contact(rpg::feed,{},0),1),"invalid scale cannot create a tail overlap");
		check(carry::support_contact(at4::base,{{},{0,0,0,1}},at4::wrists[0],at4::wrists[1].position,
			add(at4::wrists[0].position,{0,.20f*units,0}),units),"AT4 support is reachable 20 cm away from its authored centre");
		check(!carry::support_contact(at4::base,{{},{0,0,0,1}},at4::wrists[0],at4::wrists[1].position,
			add(at4::wrists[0].position,{0,.40f*units,0}),units),"AT4 expanded support remains bounded");
		check(at4::suppress_equip("h2_wpn_lau_at4_empty_putaway") && native_equip_clip("h2_wpn_lau_at4_empty_putaway") &&
			stinger::suppress_equip("h2_wpn_lau_stinger_putaway_empty"),"disposable empty-putaway clips cannot move the VR control hands");
		check(rpg::feed.matches("rpg_player") && !rpg::feed.matches("rpg_unknown") && !at4::feed.matches("javelin") &&
			javelin::feed.matches("javelin_dcburn") && javelin::feed.matches("javelin_estate_jeep") && !javelin::feed.matches("javelin_unknown"),"launcher aliases remain explicit across mission variants");
		check(!javelin::feed.blocks_native_reload() && rpg::feed.blocks_native_reload() && at4::feed.blocks_native_reload() &&
			stinger::feed.blocks_native_reload(),"only Javelin delegates reload timing and clip insertion to native PM");
		check(javelin::suppress_equip("h2_wpn_lau_javelin_reload") && !javelin::suppress_equip("h2_wpn_lau_javelin_near_fire") &&
			!native_equip_clip("h2_wpn_lau_javelin_reload"),"Javelin reload overrides solved parts without redirecting the native animation/notetrack timeline");
		{
			muzzle_frame physical;physical.position={0,0,0};physical.axis={vec{1,0,0},vec{0,1,0},vec{0,0,1}};physical.units_per_meter=units;
			const vec head{-.8f*units,javelin::feed.sight_lateral_meters*units,.05f*units};
			const auto sight=launcher::sighting_frame(javelin::feed,physical);
			check(!ads_alignment::inside(ads_alignment::measure(head,{1,0,0},physical.position,physical.axis,units),false) &&
				ads_alignment::inside(ads_alignment::measure(head,{1,0,0},sight.position,sight.axis,units),false),"Javelin raises naturally at its left-offset CLU rather than requiring eye alignment with the tube");
			check(physical.position==vec{} && physical.axis==sight.axis && launcher::sighting_frame(rpg::feed,physical).position==physical.position,
				"CLU ADS intent leaves the ballistic muzzle and other launcher alignment unchanged");
		}
		{
			// Captured invasion Stinger pose: both grips tracked, but tube-relative
			// lateral error was 15.30 cm, so the old 9 cm ADS entry gate never opened.
			hold owner{15,46,hand::right,hand::left,hold_source::interaction,45};owner.instance_generation=19;
			muzzle_frame physical;physical.valid=true;physical.owner=owner;physical.reference_generation=3;
			physical.units_per_meter=39.370098114f;
			physical.position={394.19668579f,-1387.42346191f,2379.30712891f};
			physical.axis={vec{-.794451714f,-.499750912f,.345101953f},
				vec{.536946476f,-.843489646f,.014613964f},vec{.283786595f,.196911365f,.938451409f}};
			physical.head_position={424.28778076f,-1375.56994629f,2370.00781250f};
			physical.head_forward={-.803867579f,-.400100350f,.440132409f};
			vr::controller_input::frame input;input.focused=true;input.reference_generation=3;input.continuity_generation=1;
			input.grip[0].valid=input.grip[1].valid=true;
			ads_policy tube_policy,sight_policy;bool admitted=false;
			for(const int ms:{0,50,100})
			{
				const auto now=vr::controller_input::clock::time_point{std::chrono::milliseconds{1000+ms}};
				++input.sequence;physical.input_sequence=input.sequence;
				input.sampled_at=physical.sampled_at=physical.camera_at=now;
				check(!tube_policy.consume(input,owner,physical,true,now),"captured Stinger tube basis reproduces missing ADS");
				admitted=sight_policy.consume(input,owner,launcher::sighting_frame(stinger::feed,physical),true,now);
				check(admitted==(ms==100),"Stinger circular sight admits the captured pose after the existing ADS dwell");
			}
			const auto sight=launcher::sighting_frame(stinger::feed,physical);
			check(physical.position==vec{394.19668579f,-1387.42346191f,2379.30712891f} && physical.axis==sight.axis &&
				std::abs(dot(sub(sight.position,physical.position),physical.axis[1])/physical.units_per_meter-.12609456f)<.00001f,
				"circular-sight admission preserves ballistic muzzle and axes and excludes the outer pane");
			input.grip[0].valid=false;
			check(!sight_policy.consume(input,owner,sight,true,input.sampled_at),"Stinger sight offset cannot bypass lost support tracking");
		}
		{
			native_shot_history history;check(history.allow(13,1,100,1,true,true),"native Javelin shot admitted once");history.spent(13,1,100);
			check(!history.allow(13,1,100,1,true,true) && history.allow(13,1,100,1,false,false),"native reload never permits duplicate server shots and retains prediction replay");
		}
		for(const auto* p:{&rpg::base,&at4::base,&stinger::base,&javelin::base})
		{
			rig r;r.parent.fill(-1);r.gun=68;r.parent[68]=13;r.arms[0].wrist=0;r.arms[1].wrist=1;
			std::array<bone_definition,256> bones{};
			for(size_t n=0;n<p->fingers.size();++n){bones[2+n].name=p->fingers[n].name;r.parent[2+n]=0;}
			bones[68].name="j_gun";r.weapon_bones[68]=true;int n=69;
			for(const auto& part:p->equip_rest){bones[n].name=part.name;r.parent[n]=68;r.weapon_bones[n++]=true;}
			if(p==&at4::base)r.parent[77]=72;
			if(p==&javelin::base)r.parent[73]=70;
			r.count=n;for(int b=0;b<n;++b){bones[b].parent=r.parent[b];bones[b].bind.rotation={0,0,0,1};}
			std::array<model_definition,2> models{{{"viewhands_us_army",0,68},{p->receiver,68,n-68}}};
			const auto select=p==&rpg::base?rpg::select:p==&at4::base?at4::select:p==&stinger::base?stinger::select:javelin::select;
			check(select(models,models[1],r,{bones.data(),size_t(n)}).value==p,"native launcher receiver and hand contract is selectable");
			check(bind_weapon_poses(r,{bones.data(),size_t(n)},*p).valid && launcher::bind_parts(r,{bones.data(),size_t(n)},*p->launcher).valid,"launcher hands and ammunition socket bind together");
			if(p==&javelin::base)for(auto actor:{hand::left,hand::right})
			{
				hold owner{13,1,actor,hand(1-int(actor)),hold_source::interaction,1};pose_library library{};
				library.mirror_basis.fill({1,0,0,0});carry::pose_profile posed(*p,owner,r,library);
				check(posed.controls[int(actor)].position==p->wrists[int(actor)].position && posed.supports[1-int(actor)].position==p->wrists[1-int(actor)].position &&
					posed.value.authored_rear==int(actor),"either Javelin control hand and opposite support stay on their actual asymmetric CLU grips");
			}
			models[1].count--;check(!select(models,models[1],r,{bones.data(),size_t(n)}).value,"malformed launcher topology is rejected");
		}
	}
}
