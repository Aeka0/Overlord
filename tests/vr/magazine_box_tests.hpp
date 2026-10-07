#pragma once
#include "component/vr/gameplay/magazine_manipulation.hpp"
#include <limits>

namespace magazine_box_tests
{
	namespace p=vr::gameplay::weapons::physical_reload;
	namespace h=vr::gameplay::hands;
	// Tiny volumes preserve the older mechanical fixtures' point trajectories;
	// the geometric tests below exercise broad faces and tilted end plates.
	inline p::box_motion point_fixture(h::vec position)
	{
		p::box_motion m;m.frame.position=position;
		for(auto& b:m.regions)b.half={.00001f,.00001f,.00001f};return m;
	}
	template<class Check> void run(Check check)
	{
		using namespace std::chrono_literals;
		const auto tuning=p::rocking_magazine();const auto epoch=p::magazine_latch_contact::clock::time_point{1s};
		p::box_motion magazine;
		magazine.regions[0]={{{0,0,.12f},{0,0,0,1}},{.05f,.018f,.01f}};
		magazine.regions[1]={{{.04f,0,-.12f},{0,-.5f,0,.8660254038f}},{.05f,.018f,.01f}};
		// Strike every broad face interior, including opposite corners. No fixed
		// list of centre/vertex probes can represent this full contact coverage.
		for(size_t end=0;end<2;++end)for(float x:{-.9f,0.f,.9f})for(float y:{-.9f,0.f,.9f})
		{
			const auto& box=magazine.regions[end];const auto local=p::box_point(box.pose,{x*box.half[0],y*box.half[1],0});
			auto a=magazine;a.frame.position=h::sub({-.2f,0,0},local);auto b=a;b.frame.position[0]+=.4f;
			p::magazine_latch_contact detector;(void)detector.update(tuning,a,epoch,.5f);
			check(detector.update(tuning,b,epoch+20ms,.5f),"entire top and tilted bottom face sweep through the latch between samples");
			check(!detector.update(tuning,b,epoch+30ms,.5f),"one continuous box approach cannot eject twice");
		}
		{
			auto a=magazine;a.frame.position={-.2f,0,0};auto b=a;b.frame.position={.2f,0,0};
			p::magazine_latch_contact detector;(void)detector.update(tuning,a,epoch,.5f);
			check(!detector.update(tuning,b,epoch+20ms,.5f),"space between the two end regions is not a whole-magazine strike volume");
		}
		{
			p::box_motion whole;whole.region_count=1;whole.regions[0]={{{0,0,0},{0,0,0,1}},{.05f,.018f,.13f}};
			whole.regions[1].half[0]=std::numeric_limits<float>::quiet_NaN();
			check(p::valid(whole),"inactive compound-box storage does not invalidate a one-box body");
			for(float z:{-.12f,0.f,.12f})
			{
				auto a=whole;a.frame.position={-.2f,0,z};auto b=a;b.frame.position[0]=.2f;
				p::magazine_latch_contact detector;(void)detector.update(tuning,a,epoch,.5f);
				check(detector.update(tuning,b,epoch+20ms,.5f),"whole-body strikes cover the top, middle and bottom without an endcap gap");
			}
			h::vec local{};check(!p::box_sweep(whole,whole).contact(1,tuning.latch_radius,local),"inactive contact regions cannot be queried");
			for(size_t count:{size_t{0},size_t{3}}){auto bad=whole;bad.region_count=count;check(!p::valid(bad),"invalid active region counts fail closed");}
			auto changed=whole;changed.region_count=2;changed.regions[1]=changed.regions[0];
			check(!p::same_regions(whole,changed) && !p::box_sweep(whole,changed).contact(0,tuning.latch_radius,local),"changing compound topology cannot retain or sweep old approach geometry");
		}
		{
			// Pure rotation: the top cap passes through the target while both
			// sampled end poses miss. Its material-point velocity is toward +X.
			auto a=magazine;a.frame.position={0,0,-.12f};a.frame.rotation={0,-.5735764364f,0,.8191520443f};
			auto b=a;b.frame.rotation={0,.5735764364f,0,.8191520443f};
			check(p::closest_box(a,0).distance>tuning.latch_rearm_radius && p::closest_box(b,0).distance>tuning.latch_radius,"rotational sweep begins separated and ends outside contact");
			p::magazine_latch_contact detector;(void)detector.update(tuning,a,epoch,.6f);
			p::magazine_strike strike;
			check(detector.update(tuning,b,epoch+30ms,.6f,&strike) && strike.velocity[0]>0,
				"rotating end plate retains swept contact and material-point velocity despite a stationary body origin");
		}
		{
			// A point in the enclosing axis-aligned bounds is not necessarily in
			// the tilted base plate itself. The empty wedge must stay empty.
			auto m=magazine;m.frame.position={-.04f-.045f,0,.12f-.035f};
			const p::box_sweep stationary(m,m);h::vec local{};
			check(!stationary.contact(1,.001f,local),"tilted bottom does not accept the empty wedge of a world-aligned bounding box");
		}
		for(int mode=0;mode<5;++mode)
		{
			auto a=point_fixture({-.09f,0,0}),b=point_fixture({.01f,0,0});
			if(mode==0)a.frame.position={0,0,0}; // born in contact
			if(mode==1){a.frame.position={.09f,0,0};b.frame.position={-.01f,0,0};}
			if(mode==2)b.frame.position={.5f,0,0}; // teleport
			if(mode==3)b.frame.rotation={};
			if(mode==4)b.regions[1].half[0]=std::numeric_limits<float>::quiet_NaN();
			p::magazine_latch_contact detector;(void)detector.update(tuning,a,epoch,.35f);
			check(!detector.update(tuning,b,epoch+20ms,.35f),"overlap, reverse motion, tracking jumps and invalid box data fail closed");
		}
		{
			p::magazine_latch_contact detector;auto m=point_fixture({-.09f,0,0});(void)detector.update(tuning,m,epoch,.35f);
			for(int i=1;i<=100;++i){m.frame.position[0]=-.09f+i*.0006f;check(!detector.update(tuning,m,epoch+i*10ms,.35f),"slow overlap never becomes an intentional impact");}
			m.frame.position[0]=.01f;check(!detector.update(tuning,m,epoch+1010ms,.35f),"acceleration while already overlapping cannot manufacture a new strike");
		}
	}
}
