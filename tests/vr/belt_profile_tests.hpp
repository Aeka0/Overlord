#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/m240/profile.hpp"
#include "component/vr/gameplay/weapons/mg4/profile.hpp"
#include "component/vr/gameplay/weapons/rpd/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/belt_presentation.hpp"
#include "component/vr/gameplay/heartbeat_rig.hpp"
#include "belt_weapon_data.hpp"
#include "heartbeat_data.hpp"
#include "rpd_weapon_data.hpp"
#include <vector>

namespace belt_profile_tests
{
	template<class Check>void run(Check check)
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;
		for(const auto* p:{&m240::base,&m240::arctic,&mg4::base,&mg4::arctic,&rpd::base,&rpd::digital})for(int optic:{-1,1,3,5,7,9,11})
		{
			const bool m240_receiver=p==&m240::base || p==&m240::arctic;
			const bool rail=p==&rpd::base || p==&rpd::digital;
			const auto source=m240_receiver ? std::span<const bone_definition>(belt_weapon_data::m240) : rail ? std::span<const bone_definition>(rpd_weapon_data::rpd) : std::span<const bone_definition>(belt_weapon_data::mg4);
			rig r{};r.parent.fill(-1);r.gun=68;r.count=68+int(source.size());r.arms[0].wrist=0;r.arms[1].wrist=1;
			std::array<bone_definition,256> bones{};
			for(size_t i=0;i<p->fingers.size();++i){bones[2+i].name=p->fingers[i].name;r.parent[2+i]=bones[2+i].name.find("_le")!=std::string_view::npos?0:1;}
			for(size_t i=0;i<source.size();++i){bones[68+i]=source[i];r.parent[68+i]=i?68+source[i].parent:13;r.weapon_bones[68+i]=true;}
			for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];if(i<68)bones[i].bind.rotation={0,0,0,1};}
			std::vector<model_definition> models{{"viewhands_us_army",0,68},{p->receiver,68,int(source.size())}};
			if(p==&m240::arctic)
			{
				// The live M240 uses the same standard sensor bind as the ACR fixture.
				const int begin=r.count;models.push_back({"attach_h2_heartbeat_vm",begin,9});
				for(int i=0;i<9;++i){bones[begin+i]=heartbeat_test_data::sensor[i];
					r.parent[begin+i]=bones[begin+i].parent=i ? begin+bones[begin+i].parent : 78;r.weapon_bones[begin+i]=true;}
				r.count+=9;
				const auto sensor=heartbeat::bind(models,r,{bones.data(),size_t(r.count)},
					{heartbeat_test_data::housing_bounds[0],heartbeat_test_data::housing_bounds[1]});
				check(sensor.valid && length(sub(sensor.local[0].position,bones[78].bind.position))<1e-5f,
					"M240 heartbeat binds at its own receiver mount with the shared ACR mechanism");
			}
			int optic_root=-1;
			if(optic>=0)
			{
				const auto& a=rifle_attachments::common[optic];int parent=-1;
				for(int i=68;i<r.count;++i)if(bones[i].name==a.contract.receiver_parent)parent=i;
				optic_root=r.count;models.push_back({a.contract.model,r.count,a.bones});
				for(int i=0;i<a.bones;++i){r.parent[r.count+i]=i?r.count:parent;r.weapon_bones[r.count+i]=true;bones[r.count+i].name=i?"optic_child":a.contract.root;bones[r.count+i].bind.rotation={0,0,0,1};}
				r.count+=a.bones;
			}
			if(rail)for(int n:{0,1})
			{
				const auto& a=rpd::attachments[n==0 && p==&rpd::digital ? 2 : n];int parent=-1;
				for(int i=68;i<r.count;++i)if(bones[i].name==a.contract.receiver_parent)parent=i;
				models.push_back({a.contract.model,r.count,a.bones});
				for(int i=0;i<a.bones;++i){r.parent[r.count+i]=i?r.count:parent;r.weapon_bones[r.count+i]=true;bones[r.count+i].name=i?"bipod_child":a.contract.root;bones[r.count+i].bind.rotation={0,0,0,1};}
				r.count+=a.bones;
			}
			check(select_profile(models,r,{bones.data(),size_t(r.count)}).value==p,"belt receiver with native optics and receiver-specific attachments binds");
			if(p==&m240::arctic)
			{
				check(p->reload==&m240::arctic_physical && p->reload->rigid_magazine_source==p->receiver,
					"arctic M240 retains its own detached box source through the registered scene recipe");
				check(native_reload_profile("m240_heartbeat_reflex_arctic",100,p->reload)==p->reload &&
					!native_reload_profile("m240_heartbeat_reflex_arctic",99,p->reload),"captured heartbeat M240 retains native capacity validation");
				const auto saved=models[1].name;models[1].name="h2_viewmodel_m240_base_unreviewed";
				check(!select_profile(models,r,{bones.data(),size_t(r.count)}).value,"M240 registration does not admit unknown receivers");
				models[1].name=saved;
			}
			if(p==&mg4::arctic)
			{
				check(p->reload==&mg4::arctic_physical && p->reload->rigid_magazine_source==p->receiver,
					"arctic MG4 retains its own detached box source through the registered scene recipe");
				check(native_reload_profile("mg4_arctic",100,p->reload)==p->reload && !native_reload_profile("mg4_arctic",99,p->reload),
					"captured arctic MG4 retains native capacity validation");
				const auto saved=models[1].name;models[1].name="h2_viewmodel_mg4_base_unreviewed";
				check(!select_profile(models,r,{bones.data(),size_t(r.count)}).value,"MG4 skin registration does not admit unknown receivers");
				models[1].name=saved;
			}
			if(p==&rpd::digital)
			{
				check(p->reload->rigid_magazine_source==p->receiver,"digital RPD detached drum uses the digital receiver source");
				for(auto name:{"rpd_digital","rpd_digital_acog","rpd_digital_reflex"})
					check(native_reload_profile(name,100,p->reload)==p->reload && !native_reload_profile(name,99,p->reload),
						"captured digital RPD variants retain scene recipe and native capacity validation");
				const auto saved=models[1].name;models[1].name="h2_viewmodel_rpd_base_unreviewed";
				check(!select_profile(models,r,{bones.data(),size_t(r.count)}).value,"RPD skin registration does not admit unknown receiver names");
				models[1].name=saved;
			}
			const auto parts=physical_reload::bind_parts(r,{bones.data(),size_t(r.count)},*p->reload);
			check(parts.valid && parts.cover>=0 && (rail ? parts.bolt>=0 && parts.bridge>=0 && r.parent[parts.slide]==parts.bolt : parts.bolt==-1) && parts.belt_link_count==(m240_receiver?21:rail?20:14),"audited handle, bolt relationship, cover, bridge and complete belt bind");
			check(bind_weapon_poses(r,{bones.data(),size_t(r.count)},*p).valid,"native idle hands and all mechanical rest poses bind");
			const auto& reload=*p->reload;
			check(reload.slide_grips.size()==2 && reload.interaction.slide_pose_count==2,
				"belt-fed handles expose only the two accepted hook styles to presentation and interaction");
			// Measured contacts retained from the original native reload samples.
			const vec contact=m240_receiver ? vec{5.75043678f,-2.87027407f,3.28163886f} :
				rail ? vec{4.79233742f,-1.31585705f,2.36871719f} : vec{5.00650311f,-2.96732783f,3.82197404f};
			for(int hand=0;hand<2;++hand)
			{
				std::array<part_grip_pose,2> styles{};
				const auto basis=normalize(quat{.2f,.3f,.1f,.9f});
				for(size_t i=0;i<styles.size();++i)styles[i]=hand ? vr::gameplay::hands::pose_mirror::part(reload.slide_grips[i],basis):reload.slide_grips[i];
				for(size_t i=0;i<styles.size();++i)
				{
					const auto& style=styles[i];
					check(style.name==(i?"pinky_side":"index_side") && style.allowed_hands==3 && style.fingers.size()==18,
						"each hand retains complete index/pinky hook chains without the legacy action pose");
					for(const auto& joint:style.fingers)
						check(std::count_if(style.fingers.begin(),style.fingers.end(),[&](const auto& other){return other.name==joint.name;})==1 &&
							std::any_of(p->fingers.begin(),p->fingers.end(),[&](const auto& finger){return finger.name==joint.name;}),
							"every hook joint has a unique name matching the current glove schema, including the ring chain");
				}
				for(size_t i=0;i<styles.size();++i)for(float amount:{0.f,.5f,1.f})
				{
					const auto& style=styles[i];
					check(!style.symmetry_center && length(sub(add(style.wrist.position,rotate(style.wrist.rotation,style.contact_in_wrist)),contact))<.0001f,
						"both hands keep all hook contacts on the real single-sided handle");
					const auto offset=scale(reload.interaction.slide_axis,amount*reload.interaction.slide_stroke*39.37007874f);
					auto wrist=style.wrist;wrist.position=add(wrist.position,offset);
					const auto candidate=choose_part_grip(styles,wrist,offset,reload.slide_grab_low,reload.slide_grab_high,39.37007874f,reload.slide_capture);
					check(candidate.pose==i && candidate.distance_meters<.002f,"index/pinky handle poses select independently throughout travel for both hands");
					if(reload.slide_capture)for(float y:{0.f,1.f,4.f})
					{
						auto across=wrist;across.position[1]=y;
						check(choose_part_grip(styles,across,offset,reload.slide_grab_low,reload.slide_grab_high,39.37007874f,reload.slide_capture).pose==no_part_grip,
							"M240 handle never acquires from receiver centre/left despite finger offsets and capture slack");
					}
				}
			}
			if(optic_root>=0)check(descendant(optic_root,rail?parts.bridge:parts.cover,r),"optic follows the actual bridge or cover hierarchy");
			const auto mesh=p->reload->magazine_mesh();check(mesh && mesh.subsets==1 && mesh.selected_count(0)==(rail?3:1),"detached rigid box includes declared cosmetic children and excludes articulated belt");
			std::array<bone,256> solved{};for(int i=0;i<r.count;++i)solved[i]=bones[i].bind;
			const auto& b=*p->reload->interaction.belt;
			if(m240_receiver)
			{
				check(std::abs(b.cover_angle-1.570796327f)<1e-6f,"M240 cover reaches a full ninety degrees for VR");
				for(vec delta:{vec{.26f,0,0},vec{-.26f,0,0},vec{0,0,.26f},vec{0,.17f,0},vec{0,-.17f,0}})
					check(belt_feed::cover_distance(b,delta)<b.cover_radius,"M240 cover gains large front/back/up reach and small side reach");
				check(belt_feed::cover_distance(b,{0,0,-.16f})>b.cover_radius && belt_feed::cover_distance(b,{0,.19f,0})>b.cover_radius,
					"cover extension does not grow downward or over-expand sideways");
			}
			for(int hand=0;hand<2;++hand)for(float amount:{0.f,.5f,1.f})
			{
				const quat mirror{0,0,0,1};const auto grip=hand?vr::gameplay::hands::pose_mirror::part(b.cover_grip,mirror):b.cover_grip;
				const auto pose=belt_feed::cover_pose(b,amount);
				const auto wrist=compose_reload(compose_reload(pose,inverse_reload(b.cover_rest)),grip.wrist);
				mechanics::state s{};s.belt.cover=amount;
				const auto v=belt_feed::present(b,parts,r,s,{},{{},{0,0,0,1}},wrist,{},p->reload->magazine_rest,hand,mirror,39.37007874f,true,false,solved,true);
				check(v.query.cover_distance<.0001f && std::abs(v.query.cover_angle/b.cover_angle-amount)<.001f,"both hands reach moving cover and recover its angle without offsets");
				auto turned=wrist;turned.rotation=multiply(wrist.rotation,{1,0,0,0});
				const auto relaxed=belt_feed::present(b,parts,r,s,{},{{},{0,0,0,1}},turned,{},p->reload->magazine_rest,hand,mirror,39.37007874f,true,false,solved,true);
				check(relaxed.query.cover_distance<.0001f,"controller roll cannot reject a wrist already at the moving cover grip");
			}
			for(bool seated:{false,true})for(vec offset:{vec{0,0,-1.5f},vec{0,1,0},vec{0,0,0}})
			{
				for(int i=0;i<r.count;++i)solved[i]=bones[i].bind;
				const anchor gun{{7,11,13},normalize(quat{.1f,.2f,.3f,.9f})};
				auto box=compose_reload(gun,p->reload->magazine_rest);box.position=add(box.position,offset);
				vr::gameplay::hands::pose_math::move_part(r,parts.magazine,box,solved);
				mechanics::state s{};s.magazine_inserted=true;s.belt.laid=seated;
				(void)belt_feed::present(b,parts,r,s,{},gun,{}, {},p->reload->magazine_rest,0,{0,0,0,1},39.37007874f,true,false,solved);
				const auto delta=compose_reload(box,inverse_reload(p->reload->magazine_rest));
				for(size_t i=0;i<b.links.size();++i)
					check(length(sub(solved[parts.belt_links[i]].position,compose_reload(delta,seated?b.links[i].seated:b.links[i].loose).position))<.0001f,
						"every belt link follows the actual box through final seating/pull translation, including installed ownership");
			}
			if(rail)
			{
				if(optic<0)for(int actor=0;actor<2;++actor)
				{
					const auto library=bind_weapon_poses(r,{bones.data(),size_t(r.count)},*p);
					for(int i=0;i<r.count;++i)solved[i]=bones[i].bind;
					vr::gameplay::hands::pose_mirror::fingers(r,library,*p,b.cover_grip.fingers,actor,solved,actor!=0);
					unsigned applied=0;
					for(const auto& joint:b.cover_grip.fingers)if(joint.name.starts_with("j_ring"))
					{
						for(int i=0;i<r.count;++i)if(library.finger[i]>=0 && descendant(i,r.arms[actor].wrist,r))
						{
							const auto source=actor?library.opposite[i]:i;
							if(source>=0 && bones[source].name==joint.name)
							{
								const auto expected=actor?vr::gameplay::hands::pose_mirror::local_rotation(library,i,r.parent[i],joint.rotation):joint.rotation;
								float alignment=0;for(int c=0;c<4;++c)alignment+=solved[i].rotation[c]*expected[c];
								check(std::abs(alignment)>.9999f,"RPD cover pose applies every ring joint through the actual direct/mirrored hand path");++applied;
							}
						}
					}
					check(applied==4,"RPD cover has one palm and three canonical ring joints for either hand");
				}
				const auto& bridge=*b.bridge;
				check(bridge.release && b.details.size()==1 && b.details[0].bone=="j_rail_release","only the bridge release is animated; drum latch and belt pad stay at rest");
				for(float cover:{0.f,.5f,1.f})
				{
					for(int i=0;i<r.count;++i)solved[i]=bones[i].bind;
					mechanics::state s{};s.belt={cover,false,1};
					(void)belt_feed::present(b,parts,r,s,{},{{},{0,0,0,1}},{},{},reload.magazine_rest,0,{0,0,0,1},39.37007874f,true,false,solved);
					for(int i=68;i<r.count;++i)if(bones[i].name=="j_lock" || bones[i].name=="j_ammodoor")
						check(solved[i].position==bones[i].bind.position && solved[i].rotation==bones[i].bind.rotation,"cover angle never changes drum latch or belt pad");
				}
				for(int hand=0;hand<2;++hand)for(float amount:{0.f,.5f,1.f})
				{
					for(int i=0;i<r.count;++i)solved[i]=bones[i].bind;
					const auto style=hand?vr::gameplay::hands::pose_mirror::part(bridge.grip,{0,0,0,1}):bridge.grip;
					const auto pose=belt_feed::hinge_pose(bridge.rest,bridge.axis,bridge.angle,amount);
					const auto delta=compose_reload(pose,inverse_reload(bridge.rest));
					const auto wrist=compose_reload(delta,style.wrist);
					mechanics::state s{};s.magazine_inserted=true;s.belt.bridge=amount;
					const auto v=belt_feed::present(b,parts,r,s,{},{{},{0,0,0,1}},wrist,{},p->reload->magazine_rest,hand,{0,0,0,1},39.37007874f,true,false,solved);
					check(v.query.bridge_distance<.0001f && std::abs(v.query.bridge_angle/bridge.angle-amount)<.001f,"both hands acquire real moving bridge and recover angular travel");
					check(length(sub(solved[parts.bridge].position,pose.position))<.0001f,"bridge root follows authored pivot");
					for(int i=68;i<r.count;++i)if(bones[i].name=="j_lock" || bones[i].name=="j_ammodoor")
						check(solved[i].position==bones[i].bind.position && solved[i].rotation==bones[i].bind.rotation,"bridge movement never drives stationary drum latch or belt pad");
					if(optic_root>=0)
					{
						const anchor before{bones[optic_root].bind.position,bones[optic_root].bind.rotation};
						const anchor rest{bones[parts.bridge].bind.position,bones[parts.bridge].bind.rotation};
						check(length(sub(solved[optic_root].position,compose_reload(compose_reload(pose,inverse_reload(rest)),before).position))<.0001f,"optic follows bridge through complete swing");
					}
				}
				for(int hand=0;hand<2;++hand)for(float drop:{0.f,.5f,1.f})
				{
					const auto press=hand?vr::gameplay::hands::pose_mirror::part(*bridge.release,{0,0,0,1}):*bridge.release;
					mechanics::state s{};s.belt.bridge=1;
					const auto v=belt_feed::present(b,parts,r,s,{belt_feed::lease::bridge_release},{{},{0,0,0,1}},press.wrist,{},reload.magazine_rest,hand,{0,0,0,1},39.37007874f,true,true,solved,false,drop);
					check(v.held && v.wrist.position==press.wrist.position && v.fingers.data()==press.fingers.data() && v.query.release_distance<.0001f,"release hand stays on the receiver lever throughout automatic bridge fall");
					check(v.query.bridge_settled==(drop==1),"contact admission uses the actual visible bridge endpoint");
				}
				const auto saved=r.parent[parts.slide];r.parent[parts.slide]=r.gun;
				check(!physical_reload::bind_parts(r,{bones.data(),size_t(r.count)},reload).valid,"RPD rejects sibling handle topology rather than double-driving bolt");r.parent[parts.slide]=saved;
				const auto parent=r.parent[parts.bridge];r.parent[parts.bridge]=parts.cover;
				check(!physical_reload::bind_parts(r,{bones.data(),size_t(r.count)},reload).valid,"RPD rejects bridge attached to cover");r.parent[parts.bridge]=parent;
			}
			const auto last=parts.belt_links[parts.belt_link_count-1];r.parent[last]=r.gun;
			check(!physical_reload::bind_parts(r,{bones.data(),size_t(r.count)},*p->reload).valid,"broken chain hierarchy fails admission");
		}
	}
}
