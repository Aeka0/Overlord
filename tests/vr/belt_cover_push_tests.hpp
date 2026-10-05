#pragma once
#include "component/vr/gameplay/weapons/m240/profile.hpp"
#include "component/vr/gameplay/weapons/mg4/profile.hpp"
#include "component/vr/gameplay/weapons/rpd/profile.hpp"
#include "component/vr/gameplay/weapons/rpd/reload_profile.hpp"
#include "component/vr/gameplay/weapons/m240/reload_profile.hpp"
#include "component/vr/gameplay/weapons/mg4/reload_profile.hpp"
#include "component/vr/gameplay/cover_push_debug.hpp"

namespace belt_cover_push_tests
{
	namespace w=vr::gameplay::weapons;namespace b=w::belt_feed;namespace h=vr::gameplay::hands;
	inline b::cover_push_contact contact(const b::profile& p,float amount,float gap,float radius=0,float side=0)
	{
		const auto& shape=*p.push;
		const auto q=b::hinge_pose({},p.cover_axis,p.cover_angle,amount).rotation;
		const auto pt=h::rotate(q,{- (radius!=0?radius:(shape.inner+shape.outer)*.5f),side,shape.surface+gap});
		return {true,pt,h::rotate(q,{0,0,-1}),pt};
	}
	template<class Fixture,class Check>void run(Check check)
	{
		using namespace std::chrono_literals;
		for(const auto* definition:{&w::rpd::physical,&w::m240::physical,&w::mg4::physical})
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			const auto& p=*definition->interaction.belt;
			for(float speed:{.39f,.41f})
			{
				b::cover_push gesture;b::state state{1,true,p.bridge?1.f:0.f};auto now=vr::controller_input::clock::time_point{1s};
				const auto step=[&](b::cover_push_contact c,bool open=true){now+=10ms;gesture.update(p,state,c,now,open,[&](float target){return b::move_cover(state,target,p.bridge!=nullptr);});};
				step(contact(p,1,.03f,.16f));auto c=contact(p,1,.011f,.16f);step(c);
				const auto radius=h::length(c.point);
				for(int i=0;i<24;++i){const auto q=b::hinge_pose({},p.cover_axis,-speed*.01f/radius,1).rotation;
					c.point=h::rotate(q,c.point);c.palm=h::rotate(q,c.palm);c.world=c.point;step(c);}
				const auto before=state.cover;for(int i=0;i<60;++i)step(c,false);
				check(before<.8f && (speed>.4f?state.cover==0:state.cover>0 && state.cover<before),
					"lower full-close speed gate admits 0.41 m/s while 0.39 m/s retains only a short run-out after sufficient travel");
			}
			{
				const auto& patch=*p.push;const auto q=b::hinge_pose({},p.cover_axis,p.cover_angle,1).rotation;
				for(float direction:{-1.f,1.f})
				{
					auto c=contact(p,1,.02f,direction>0?patch.outer+.074f:patch.inner-.074f);
					// Build directly: the new centre may lie across the hinge origin.
					c.point=h::rotate(q,{direction>0?-patch.outer-.074f:-patch.inner+.074f,0,patch.surface+.02f});
					c.along=h::rotate(q,{direction,0,0});
					check(b::query_push(p,1,c).inside,"capsule adds 1.5 cm toward either fingers or wrist in the actual projected hand direction");
					c.along=h::rotate(q,{0,1,0});
					check(!b::query_push(p,1,c).inside,"longitudinal extension rotates with the hand and does not enlarge its side width");
				}
				const auto d=b::push_extension({1,1,1});check(h::length(d)<=b::push_long_extension && d[2]==0,"tilted hand axis is projected without inflating capsule length");
			}
			for(float start:{0.f,3.f,-3.f})
			{
				Fixture f(definition);f.owner.rear=rear;auto state=f.state;state.belt.cover=0;state.belt.bridge=p.bridge?1.f:0.f;f.adopt(state);f.step();
				auto& c=f.geometry.belt;c.cover_distance=0;c.cover_angle=start;f.trigger(true);
				bool stayed_open=true;
				for(int i=1;i<=36;++i)
				{
					c.cover_angle=std::remainder(start+i*.2f,6.283185307f);
					if(i*.2f>p.cover_angle+.2f)stayed_open=stayed_open && b::cover_target(p,f.control.belt_grip(),c)==1;
					f.step();if(i*.2f>=p.cover_angle)stayed_open=stayed_open && f.state.belt.cover==1;
				}
				c.cover_distance=.36f;f.step();f.trigger(false);
				check(stayed_open && f.state.belt.cover==1 && f.control.belt_grip().part==b::lease::none,
					"opening past the stop and crossing angular wrap cannot snap the cover closed before or after breakaway");
				c.cover_distance=0;f.trigger(true);const auto initial=c.cover_angle;
				for(int i=1;i<=20;++i){c.cover_angle=std::remainder(initial-p.cover_angle*i/20,6.283185307f);f.step();}
				check(f.state.belt.cover==0,"a fresh grasp after over-opening can deliberately close the lid normally");
			}
			for(int hz:{45,90,144})
			{
				b::cover_push gesture;b::state state{1,true,p.bridge?1.f:0.f};auto now=vr::controller_input::clock::time_point{1s};
				const float dt=1.f/hz;const auto frame=std::chrono::duration_cast<vr::controller_input::clock::duration>(std::chrono::duration<float>(dt));
				const auto step=[&](b::cover_push_contact c,bool open=true){now+=frame;gesture.update(p,state,c,now,open,[&](float target){return b::move_cover(state,target,p.bridge!=nullptr);});};
				step(contact(p,1,.03f));auto c=contact(p,1,.03f-3.5f*dt);step(c);
				const float radius=(p.push->inner+p.push->outer)*.5f;
				for(int i=0;i<4 && state.cover>0;++i){const auto q=b::hinge_pose({},p.cover_axis,-3.5f*dt/radius,1).rotation;
					c.point=h::rotate(q,c.point);c.palm=h::rotate(q,c.palm);c.world=c.point;step(c);}
				const auto pushed=state.cover;
				for(int i=0;i<hz;++i)step(c,false);
				check(pushed<.8f && state.cover==0,"3.5 m/s fast sweep follows the moving lid and coasts at 45/90/144 Hz instead of rejecting the old-plane endpoint");
			}
			{
				const auto& patch=*p.push;
				for(int side=0;side<4;++side)for(float margin:{.059f,.061f})
				{
					const float radius=side==0?patch.inner-margin:side==1?patch.outer+margin:(patch.inner+patch.outer)*.5f;
					const float y=side==2?patch.side_low-margin:side==3?patch.side_high+margin:0.f;
					check(b::query_push(p,1,contact(p,1,.02f,radius,y)).inside==(margin<.06f),"finite 6 cm palm radius overlaps all four sides without unbounded reach");
				}
				check(b::query_push(p,1,contact(p,1,.02f,.03f)).inside && !b::query_push(p,1,contact(p,1,.02f,-.10f)).inside,
					"near-hinge palm contact is reachable without granting unbounded reach behind the rotation axis");
				check(b::query_push(p,1,contact(p,1,.02f,patch.outer+.03f,patch.side_high+.03f)).inside &&
					!b::query_push(p,1,contact(p,1,.02f,patch.outer+.045f,patch.side_high+.045f)).inside,"palm contact uses disc-rectangle overlap, including rounded corner distance");
				auto sideways=contact(p,1,.02f);sideways.palm=h::rotate(b::hinge_pose({},p.cover_axis,p.cover_angle,1).rotation,{1,0,0});
				check(b::query_push(p,1,sideways).facing,"near-sideways palm orientation remains usable for a clear outside-in push");
			}
			{
				namespace cd=w::physical_reload::cover_debug;cd::sample s;s.definition=definition;s.units=39.3700787f;
				s.hinge={{2,3,4},{0,0,.38268343f,.92387953f}};s.cover=1;s.raw=contact(p,1,.03f);
				s.tracking=s.available=s.squeeze_active=true;
				for(const char* reason:{"no simulation sample","cooldown","needs exterior approach","contact lost without momentum","discontinuous or passive push","pushing","coasting"})
				{
					s.reason=reason;const auto lines=cd::geometry_for(s,{10,20,30});
					check(lines[0].count>20 && lines[1].count>0 && lines[2].count>0,"cover overlay keeps target, verdict and input labels in separate bounded batches");
				}
				for(h::vec direction:{h::vec{1,0,0},h::vec{0,1,0},h::unit({1,1,0}),h::vec{0,0,1}})
				{
					s.raw.along=direction;const auto lines=cd::geometry_for(s,{});
					check(lines[0].count>20 && lines[0].count<vr::spatial_lines::capacity && lines[1].count && lines[2].count,
						"rotating capsule and rounded admission hull keep all overlay layers within their fixed budgets");
				}
				const auto lines=cd::geometry_for(s,{}),translated=cd::geometry_for(s,{10,20,30});
				check(std::abs(translated[0].lines[0].a[0]-lines[0].lines[0].a[0]-10)<.0001f,"cover diagnostic follows current scene placement");
				s.units=NAN;check(cd::geometry_for(s,{})[0].count==0,"invalid diagnostic scale cannot publish geometry");
			}
			for(int hz:{45,72,90,120,144})
			{
				b::cover_push gesture;b::state state{1,true,p.bridge?1.f:0.f};
				auto now=vr::controller_input::clock::time_point{1s};const float dt=1.f/hz;
				const auto frame=std::chrono::duration_cast<vr::controller_input::clock::duration>(std::chrono::duration<float>(dt));
				const auto step=[&](b::cover_push_contact c){now+=frame;gesture.update(p,state,c,now,true,[&](float target){return b::move_cover(state,target,p.bridge!=nullptr);});};
				// A normal 8 cm/s approach advances < 2 mm at all these rates.
				for(float gap=.045f;gap>.004f;gap-=.08f*dt)step(contact(p,1,gap));
				auto c=contact(p,1,.004f);
				for(float angle=0;angle<.65f;angle+=.4f*dt)
				{
					const auto q=b::hinge_pose({},p.cover_axis,-.4f*dt,1).rotation;
					c.point=h::rotate(q,c.point);c.palm=h::rotate(q,c.palm);c.world=c.point;step(c);
				}
				check(state.cover<.8f,"ordinary slow approach acquires cover independently of 45/72/90/120/144 Hz sampling");
			}
			const auto setup=[&](Fixture& f){f.owner.rear=rear;auto s=f.state;s.belt.cover=1;s.belt.bridge=p.bridge?1.f:0.f;f.adopt(s);
				f.input.squeeze[1-int(rear)]={true,false,0,1};f.step();};
			const auto approach=[&](Fixture& f){f.geometry.belt.push=contact(p,f.state.belt.cover,.035f);f.step();
				f.geometry.belt.push=contact(p,f.state.belt.cover,.011f);f.now+=20ms;f.step();};
			const auto push=[&](Fixture& f,float angle){auto& c=f.geometry.belt.push;
				const auto q=b::hinge_pose({},p.cover_axis,-angle,1).rotation;c.point=h::rotate(q,c.point);c.palm=h::rotate(q,c.palm);c.world=c.point;f.step();};
			{
				// Exercise the real immutable-frame resampler, not only synthetic
				// hinge contacts. The authored wrist basis must not rotate the
				// physical controller palm normal a second time.
				namespace pr=w::physical_reload;namespace hi=vr::gameplay::hand_interaction;using namespace vr::gameplay::hands::pose_math;
				Fixture f(definition);setup(f);const int actor=1-int(rear);constexpr float units=39.3700787f;
				pr::presentation view;view.active=true;view.definition=definition;view.owner=f.owner;
				pr::scene_frame scene;scene.owner=f.owner;scene.definition=definition;scene.assembly=1;scene.binding.valid=true;
				scene.binding.wrist={0,0,.38268343f,.92387953f};scene.binding.mirror={1,0,0,0};
				scene.contact.belt.push.valid=true;scene.contact_in_wrist.back()={1.1f,.4f,.2f};
				hi::frame frame;frame.valid_hands=3;frame.body.units_per_meter=units;
				frame.objects[0].owner=f.owner;frame.objects[0].assembly=1;
				frame.objects[0].gun={{20,-30,15},{0,0,.25881905f,.96592583f}};
				const auto hinge=compose(frame.objects[0].gun,p.cover_rest);
				bool sampled=true;
				const auto step=[&](float amount,float gap){
					const auto c=contact(p,amount,gap);const auto world=compose(hinge,{h::scale(c.point,units),{0,0,0,1}}).position;
					const auto rotation=h::multiply(hinge.rotation,h::multiply(b::hinge_pose({},p.cover_axis,p.cover_angle,amount).rotation,
						h::quat{actor?-.70710678f:.70710678f,0,0,.70710678f}));
					frame.wrists[actor]={h::sub(world,h::rotate(h::multiply(rotation,scene.binding.wrist),scene.contact_in_wrist.back())),rotation};
					view.ammo=f.state;frame.input=f.input;
						sampled=sampled && pr::sample_contact(scene,view,frame) && scene.contact.belt.push.valid &&
							h::length(h::sub(scene.contact.belt.push.point,c.point))<.00001f && h::length(h::sub(scene.contact.belt.push.palm,c.palm))<.00001f &&
							h::length(h::sub(scene.contact.belt.push.along,h::rotate(h::conjugate(hinge.rotation),h::unit(h::sub(world,frame.wrists[actor].position)))))<.00001f;
					f.geometry=scene.contact;f.step();
				};
				for(float gap=.045f;gap>.004f;gap-=.001f)step(1,gap);
				for(float angle=0;angle<.6f;angle+=.006f)step(1-angle/p.cover_angle,.004f);
				check(sampled && f.state.belt.cover<.8f,"actual controller sampling and authoritative update admit a continuous slow palm push for either hand");
			}
			{
				Fixture f(definition);setup(f);const auto ammo=w::mechanics::total_rounds(f.state);approach(f);
				for(int i=0;i<100 && f.state.belt.cover>0;++i)push(f,.025f);
				check(f.state.belt.cover==0 && w::mechanics::total_rounds(f.state)==ammo,"open palm closes each cover with either hand without ammo changes");
				check(f.control.belt_grip().part==b::lease::none,"palm push keeps the free hand pose without manufacturing a pinch lease");
			}
			for(int invalid=0;invalid<8;++invalid)
			{
				Fixture f(definition);setup(f);
				for(float gap:{.04f,.025f,.01f,-.005f,-.02f})
				{
					auto c=contact(p,1,gap);
					if(invalid==0)c=contact(p,1,gap,-.10f); // beyond the hinge, outside the palm footprint
					if(invalid==1)c=contact(p,1,gap,0,.16f); // beyond the enlarged side margin
					if(invalid==2)c.palm=h::scale(c.palm,-1); // back of hand
					if(invalid==3)c=contact(p,1,-.04f); // initially inside
					if(invalid==4)f.input.squeeze[1-int(rear)].down=true;
					if(invalid==5)f.geometry.knife_held=true;
					if(invalid==6)c.world={}; // moving gun into stationary hand
					if(invalid==7)c.valid=false;
					f.geometry.belt.push=c;f.now+=20ms;f.step();
				}
				check(f.state.belt.cover==1,"beyond-axis/side/backface/inside/closed hand/knife/stationary hand/invalid palm cannot close cover");
			}
			{
				Fixture f(definition);setup(f);approach(f);
				for(int i=0;i<65;++i)push(f,.007f);
				const auto partial=f.state.belt.cover;
				f.geometry.belt.push=contact(p,partial,.05f);f.step();
				for(int i=0;i<50;++i)f.step();
				check(partial<1 && f.state.belt.cover<partial && f.state.belt.cover>0 && partial-f.state.belt.cover<=.20944f/p.cover_angle,
					"slow push has a short dissipating run-out capped at twelve degrees instead of a full automatic close");
			}
			{
				Fixture f(definition);setup(f);approach(f);push(f,.04f);push(f,.04f);
				const auto partial=f.state.belt.cover;f.input.squeeze[1-int(rear)].down=true;
				for(int i=0;i<50;++i)f.step();
				check(partial<1 && f.state.belt.cover>0 && partial-f.state.belt.cover<=.20944f/p.cover_angle,
					"fast but short flick gets at most the small run-out and cannot acquire full-close inertia");
			}
			{
				Fixture f(definition);setup(f);approach(f);for(int i=0;i<12;++i)push(f,.04f);
				for(int i=0;i<10;++i)f.step();const auto partial=f.state.belt.cover;
				f.input.squeeze[1-int(rear)].down=true;for(int i=0;i<50;++i)f.step();
				check(f.state.belt.cover==partial,"stopping the palm consumes old momentum instead of banking it indefinitely");
			}
			for(int loss=0;loss<5;++loss)
			{
				Fixture f(definition);setup(f);approach(f);
				for(int i=0;i<12;++i)push(f,.04f);
				const auto partial=f.state.belt.cover;
				if(loss==0)f.input.squeeze[1-int(rear)].down=true; // ordinary gesture loss
				if(loss==1)f.geometry.belt.push=contact(p,partial,.055f); // separation
				if(loss==2)f.input.focused=false;
				if(loss==3)f.geometry.belt.push.world[0]+=1;
				if(loss==4)f.input.reference_generation++;
				for(int i=0;i<55;++i)f.step();
				check(partial<.8f && (loss<2?f.state.belt.cover==0:f.state.belt.cover==partial),
					"committed fast push coasts on ordinary loss but cancels on focus loss, teleport or recenter");
			}
			{
				Fixture f(definition);setup(f);approach(f);for(int i=0;i<12;++i)push(f,.04f);
				const auto partial=f.state.belt.cover;f.geometry.slide_distance=0;f.trigger(true);
				check(f.control.slide_held() && f.state.belt.cover==partial,"explicit handle grasp overrides qualified push inertia without losing its press");
			}
			{
				Fixture f(definition);setup(f);auto s=f.state;s.belt.cover=0;f.adopt(s);
				f.geometry.belt.cover_distance=0;f.trigger(true);f.geometry.belt.cover_angle=p.cover_angle;f.step();
				for(int i=0;i<40;++i)f.step(); // holding open does not consume the release cooldown
				f.trigger(false);approach(f);for(int i=0;i<10;++i)push(f,.02f);
				check(f.state.belt.cover==1,"manual opening release starts a full 300 ms push cooldown");
				for(int i=0;i<35;++i)f.step();
				check(f.state.belt.cover==1,"waiting out cooldown while overlapped cannot arm a push");
				approach(f);push(f,.04f);push(f,.04f);check(f.state.belt.cover<1,"fresh exterior approach after cooldown can push");
			}
			{
				Fixture f(definition);setup(f);approach(f);f.writable=false;push(f,.08f);const auto partial=f.state.belt.cover;
				f.writable=true;for(int i=0;i<20;++i)push(f,.03f);
				check(f.state.belt.cover==partial,"rejected native compare consumes push and cannot queue a close");
			}
			{
				// The sampling path must be covariant under gun placement and
				// native-unit scale. Neither authored grip relaxation nor IK enters.
				using namespace vr::gameplay::hands::pose_math;
				const h::anchor gun{{12,-33,7},{0,0,.38268343f,.92387953f}};
				const auto raw=contact(p,.73f,.035f);const auto frame=compose(gun,p.cover_rest);
				const auto world=compose(frame,{h::scale(raw.point,39.3700787f),{0,0,0,1}}).position;
				const auto sampled=b::sample_cover_push(p,gun,world,h::rotate(frame.rotation,raw.palm),world,39.3700787f);
				check(sampled.valid && h::length(h::sub(sampled.point,raw.point))<.00001f && h::length(h::sub(sampled.palm,raw.palm))<.00001f,
					"raw palm sample preserves hinge coordinates through rotated gun and unit conversion");
			}
		}
		{
			w::physical_reload::part_return_transition visual;const auto at=vr::controller_input::clock::time_point{1s};
			visual.update(1,1,false,1,at,.045f);const auto first=visual.update(1,1,false,.5f,at+10ms,.045f);
			const auto next=visual.update(1,1,false,.5f,at+20ms,.045f),later=visual.update(1,1,false,.5f,at+30ms,.045f);
			check(first>next && next>later && later>.5f,"visual cover advances on render-only frames between mechanical ticks");
			visual.update(1,1,false,0,at+40ms,.045f);
			check(visual.update(1,1,false,0,at+90ms,.045f)==0 && visual.update(1,2,false,1,at+100ms,.045f)==1,
				"cosmetic cover catches the mechanical latch promptly and resets on reference changes");
		}
		for(int hz:{45,90,144})
		{
			w::physical_reload::part_return_transition visual;auto at=vr::controller_input::clock::time_point{1s};
			const auto dt=std::chrono::duration_cast<vr::controller_input::clock::duration>(std::chrono::duration<float>(1.f/hz));
			float previous=visual.update(1,1,false,1,at,.045f);bool advancing=true;
			for(int i=1;i<=15;++i){at+=dt;const auto shown=visual.update(1,1,false,1-i*.05f,at,.045f);
				if(i>1)advancing=advancing && shown<previous;previous=shown;}
			check(advancing,"continuous mechanical retargeting every render frame must not freeze the cover at its old display value");
		}
	}
}
