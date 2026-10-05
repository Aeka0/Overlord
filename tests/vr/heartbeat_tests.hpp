#pragma once
#include "component/vr/gameplay/heartbeat_rig.hpp"
#include "component/vr/gameplay/heartbeat_native_mode.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "heartbeat_data.hpp"
#include <limits>

namespace heartbeat_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::hands;
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands::pose_math;
		check(heartbeat::admits_native_mode("m240_heartbeat_reflex_arctic",0,true,true),"captured fixed M240 tracker is admitted without an alternate definition");
		check(!heartbeat::admits_native_mode("m240_heartbeat_reflex_arctic",5,true,false) &&
			!heartbeat::admits_native_mode("m240_heartbeat_reflex_arctic",0,false,true),"M240 requires its witnessed single tracker mode");
		check(heartbeat::admits_native_mode("masada_silencer_mt_camo_on_h2",43,true,false) &&
			!heartbeat::admits_native_mode("masada_silencer_mt_camo_on_h2",0,true,true),"ACR retains its native tracker mode pair");
		check(!heartbeat::admits_native_mode("m240_heartbeat_unreviewed",0,true,true) &&
			!heartbeat::admits_native_mode("m4_grenadier",5,true,false),"unreviewed sensors and separate underbarrel modes remain excluded");
		rig layout;layout.gun=0;layout.count=28;layout.parent.fill(-1);
		std::array<bone_definition,28> bones{};
		std::copy(heartbeat_test_data::receiver.begin(),heartbeat_test_data::receiver.end(),bones.begin());
		std::copy(heartbeat_test_data::sensor.begin(),heartbeat_test_data::sensor.end(),bones.begin()+19);
		for(int i=0;i<28;++i){layout.parent[i]=i<19 ? bones[i].parent : i==19 ? 10 : 19+bones[i].parent;layout.weapon_bones[i]=true;}
		std::array<model_definition,2> models{{{"h2_viewmodel_magpul_masada_base_arctic",0,19},{"attach_h2_heartbeat_vm_arctic",19,9}}};
		const heartbeat::housing_bounds bounds{heartbeat_test_data::housing_bounds[0],heartbeat_test_data::housing_bounds[1]};
		const auto bound=heartbeat::bind(models,layout,bones,bounds);
		check(bound.valid,"heartbeat captured arctic hierarchy binds");
		if(!bound.valid)return;
		const auto mount=compose(inverse(as_anchor(bones[0].bind)),as_anchor(bones[10].bind));
		check(length(sub(bound.local[0].position,mount.position))<1e-5f,"heartbeat duplicate root uses receiver mount, not attachment model origin");
		const auto closed=heartbeat::pose(bound,0),opened=heartbeat::pose(bound,1);
		check(length(sub(closed[0].position,opened[0].position))<1e-5f,"heartbeat mount never moves during folding");
		check(length(sub(closed[3].position,opened[3].position))>.5f,"heartbeat housing follows actual two-axis path");
		for(int step=0;step<=1024;++step)
		{
			for(float tilt:{0.f,.5f,1.f})
			{
				const auto p=heartbeat::pose(bound,float(step)/1024,tilt);
				for(const auto& a:p)check(hinged_attachment::finite(a),"heartbeat fold and automatic tilt remain finite and normalized");
				check(std::abs(length(sub(p[5].position,p[6].position))-2.807962f)<.001f,"heartbeat native screen width preserved throughout fold");
				check(std::abs(length(sub(p[5].position,p[7].position))-2.074117f)<.001f,"heartbeat native screen height preserved throughout fold");
			}
		}
		const auto flat_open=heartbeat::pose(bound,1,0);
		check(length(sub(rotate(flat_open[3].rotation,{1,0,0}),rotate(opened[3].rotation,{1,0,0})))>.1f,
			"automatic tilt restores native open housing independently of hand fold");
		for(int i=0;i<=16;++i)
		{
			const auto wire=heartbeat::wire_rotation(float(i)),native=heartbeat::authored::rotations[i][2];float dot{};
			for(int j=0;j<4;++j)dot+=wire[j]*native[j];check(std::abs(dot)>.99999f,"continuous wire curve retains native rotation witnesses");
			if(i && i<16){const auto a=heartbeat::wire_rotation(float(i)-.0001f),b=heartbeat::wire_rotation(float(i)+.0001f);float distance{};
				for(int j=0;j<4;++j)distance+=(a[j]-b[j])*(a[j]-b[j]);check(distance<1e-7f,"wire curve has no jump at authored knots");}
		}
		auto invalid=layout;invalid.parent[19]=0;
		check(!heartbeat::bind(models,invalid,bones,bounds).valid,"heartbeat cannot attach directly to receiver root");
		invalid=layout;invalid.parent[23]=20;
		check(!heartbeat::bind(models,invalid,bones,bounds).valid,"heartbeat malformed wire chain rejected");
		auto duplicate=models;duplicate[0]={"attach_h2_heartbeat_vm",0,19};
		check(!heartbeat::bind(duplicate,layout,bones,bounds).valid,"duplicate sensors rejected");
		for(int h=0;h<2;++h)
		{
			auto wrist=heartbeat::authored::wrist_in_sensor;if(h)wrist=vr::gameplay::hands::pose_mirror::wrist(wrist,{0,0,0,1});
			auto path=heartbeat::manipulation_path(bound,wrist,39.3700787f,0,0);
			const auto turn=[&](anchor start,float amount){auto local=compose(inverse(path.hinge),start);const float half=-hinged_attachment::stroke*amount*.5f;
				local.position=rotate({0,0,std::sin(half),std::cos(half)},local.position);return compose(path.hinge,local);};
			const auto stroke=[&](float t){const float half=hinged_attachment::stroke*(1-t)*.5f;
				return compose(path.hinge,compose({{}, {0,0,std::sin(half),std::cos(half)}},path.wrist));};
			hinged_attachment::gesture g;
			auto raw=hinged_attachment::at(path,0);raw.position=add(raw.position,{.01f,-.015f,.008f});
			const auto offset_grab=raw;
			check(g.acquire(path,raw),"either hand acquires sensor with small grasp offset");
			for(int i=0;i<20;++i)g.move(path,raw,.01f);
			check(!g.open() && g.amount()<.01f,"stationary trigger grasp never auto-opens sensor");
			for(int i=1;i<=128;++i)g.move(path,turn(offset_grab,float(i)/128),.01f);
			check(g.held() && g.open(),"both hand paths reach open without time-driven animation");
			g.release();check(!g.held() && g.open() && g.amount()==1,"release preserves open endpoint and frees hand");
			path.contact=hinged_attachment::at(path,1);
			check(g.acquire(path,path.contact),"fresh trigger grasp can close sensor");
			for(int i=127;i>=0;--i)g.move(path,hinged_attachment::at(path,float(i)/128),.01f);
			g.release();check(!g.open() && g.amount()==0,"reverse hand motion closes sensor");
			path.contact=hinged_attachment::at(path,0);
			check(g.acquire(path,path.contact),"sensor regrabs after release");
			auto jump=path.contact;jump.position[0]+=1;g.move(path,jump,.01f);
			check(!g.held() && !g.open(),"tracking jump cancels instead of completing fold");
			check(!g.acquire(path,jump),"distant hand cannot acquire sensor");
			check(g.acquire(path,path.contact),"fresh trigger after tracking recovery works");
			for(int i=1;i<=90;++i)g.move(path,hinged_attachment::at(path,float(i)/128),.01f);
			g.cancel();check(!g.held() && !g.open(),"focus loss does not commit partial fold");
			check(g.acquire(path,path.contact),"regrab before stale input test");
			g.move(path,path.contact,.3f);check(!g.held() && !g.open(),"stale sample cancels sensor lease");
			for(float cadence:{.2f,.25f})
			{
				check(g.acquire(path,path.contact),"continuous slowmo sensor regrab");
				for(int n=1;n<=16;++n)g.move(path,hinged_attachment::at(path,float(n)/16),cadence,true);
				check(g.held() && g.open(),"continuous 200/250 ms consumers retain a geometrically valid sensor stroke");
				g.reset(false);
			}
			check(g.acquire(path,path.contact),"sensor regrab before bounded continuous stall");
			g.move(path,path.contact,.6f,true);check(!g.held(),"continuous input cannot bridge an unbounded consumer stall");
			auto corrupt=path;corrupt.hinge.position[0]=std::numeric_limits<float>::quiet_NaN();
			check(!g.acquire(corrupt,path.contact),"malformed hinge cannot acquire");
			check(g.acquire(path,path.contact),"acquire for continuous-angle and wrist-tilt checks");
			raw=path.contact;raw.rotation={.5f,.5f,.5f,.5f};g.move(path,raw,.01f);
			check(g.held() && std::abs(g.amount())<1e-6f,"wrist pitch and roll do not drive sensor folding");
			g.move(path,hinged_attachment::at(path,.347123f),.01f);
			check(g.held() && std::abs(g.amount()-.347123f)<1e-5f,"sensor accepts an arbitrary continuous hinge angle");
			g.move(path,hinged_attachment::at(path,.347223f),.01f);
			check(std::abs(g.amount()-.347223f)<1e-5f,"sub-bin hand movement is preserved without quantization");
			auto preview=g;preview.move(path,hinged_attachment::at(path,.50f),.01f);
			check(std::abs(preview.amount()-.50f)<1e-5f && std::abs(g.amount()-.347223f)<1e-5f,"latest render angle does not mutate authoritative scan progress");
			for(int i=4;i<=12;++i)g.move(path,stroke(float(i)/10),.01f);
			g.move(path,stroke(1.1f),.01f);
			check(g.held() && g.open() && g.amount()==1,"open-stop overtravel absorbs a small reverse stroke like the belt cover");
			g.release();check(g.open() && g.amount()==1,"release after overtravel cannot close an open sensor");
			g.reset(false);raw=path.contact;raw.position[0]+=.10f;
			check(g.acquire(path,raw),"sensor contact extends four centimetres toward the muzzle");g.cancel();
			raw=path.contact;raw.position[0]-=.07f;
			check(!g.acquire(path,raw),"forward extension does not expand rearward contact");
			raw=path.contact;raw.position[1]+=.07f;
			check(!g.acquire(path,raw),"forward extension leaves lateral contact unchanged");
			g.reset(true);path=heartbeat::manipulation_path(bound,wrist,39.3700787f,1,1);
			check(g.acquire(path,path.contact),"resting native open tilt remains grabbable");
			g.move(path,path.contact,.01f);
			check(g.held() && g.open() && g.amount()==1,"grabbing a tilted open sensor does not fold it");
			// Follow the housing after acquisition flattens it, without adding an
			// artificial controller offset to compensate for the previous tilt.
			for(int i=127;i>=0;--i)g.move(path,hinged_attachment::at(path,float(i)/128),.01f);
			check(g.held() && g.amount()<.35f,"native-tilted open sensor can complete the closing stroke after regrab");
			g.release();check(!g.open() && g.amount()==0,"releasing a regrabbed native-tilted sensor closes it");
			// Witness from the coordinated 2026-10-01 capture: old wrist-only
			// capsule rejected this real attempt at 8.32 cm (limit 6.5 cm).
			const anchor attempted{{.086296230f,.168340339f,.054368374f},{0,0,0,1}};
			g.reset(true);
			check(g.acquire(path,attempted),"captured full-open regrab is admitted by the native housing volume");
			for(int i=1;i<=90;++i)g.move(path,turn(attempted,-float(i)/90),.01f);
			check(g.held() && g.amount()<.001f,"a grip nearer the hinge follows its own lever arm through a full close");
			g.release();check(!g.open(),"captured regrab can settle closed");
			// A world-space gun transform cancels when the controller is brought
			// back into the gun frame; moving the whole gun is not folding it.
			const anchor world{{300,-200,10},{0,0,.70710678f,.70710678f}};
			const auto local=compose(inverse(world),compose(world,path.contact));
			check(length(sub(local.position,path.contact.position))<.0001f,"gun-relative gesture rejects whole-gun world motion");
		}
	}
}
