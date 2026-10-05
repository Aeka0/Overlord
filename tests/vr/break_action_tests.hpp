#pragma once
#include "component/vr/gameplay/weapons/m79/profile.hpp"
#include "component/vr/gameplay/weapons/ranger/profile.hpp"
#include "component/vr/gameplay/break_action_profiles.hpp"
#include "component/vr/gameplay/break_action_presenter.hpp"
#include "component/vr/gameplay/weapon_clip_projection.hpp"
#include "component/scheduler_context.hpp"

namespace break_action_tests
{
	namespace b=vr::gameplay::weapons::break_action;
	namespace w=vr::gameplay::weapons;
	using namespace std::chrono_literals;
	struct fixture
	{
		const w::break_action_profile& profile;b::controller control;b::state state;
		w::hold owner{77,1,vr::hand::right,vr::hand::none,w::hold_source::engine_default,1};
		vr::controller_input::frame input;b::geometry geometry;
		b::clock::time_point now{1s};w::ammunition::projection native;bool writable{true},manipulation{true};
		int commits{},shots{},ejections{},cases{};
		explicit fixture(const w::break_action_profile& p,int loaded=-1):profile(p)
		{
			state=b::import_native(p.ammunition,77,1,{loaded<0 ? int(p.ammunition.capacity) : loaded,10});native=b::native_ammo(state);
			input.sequence=input.reference_generation=1;input.focused=true;
			for(int h=0;h<2;++h){input.grip[h].valid=input.aim[h].valid=true;input.aim[h].tracking.orientation={{{1,0,0},{0,1,0},{0,0,1}}};
				input.trigger[h]=input.secondary[h]={true,false,0,1};}
			geometry.valid=true;geometry.weapon=77;geometry.instance_generation=geometry.reference_generation=1;
			geometry.shell_in_chamber.fill({0,0,-1});geometry.alignment.fill(1);step();
		}
		bool commit(const b::transaction& tx)
		{if(!writable || tx.before!=native)return false;native=tx.after;++commits;shots+=tx.rounds_spent;if(tx.ejected){++ejections;cases+=std::popcount(tx.ejected);}return true;}
		void step(bool advance=true)
		{if(advance){now+=10ms;++input.sequence;}input.sampled_at=now;geometry.input_sequence=input.sequence;
			control.update(profile.interaction,profile.ammunition,state,input,owner,geometry,true,now,[&](const auto& tx){return commit(tx);},{manipulation});}
		void button(bool down)
		{auto& v=input.secondary[int(owner.holding_hand())];if(down && !v.down)++v.presses;v.down=down;step();}
		void pinch(bool down)
		{auto& v=input.trigger[1-int(owner.holding_hand())];if(down && !v.down)++v.presses;v.down=down;step();}
		void open()
		{button(true);for(int n=0;n<30;++n)step();button(false);}
		void load(unsigned chamber)
		{geometry.barrel_distance=10;geometry.waist_distance=0;geometry.shell_in_chamber.fill({0,0,-1});pinch(false);pinch(true);
			geometry.shell_in_chamber[chamber]={0,0,0};step();pinch(false);geometry.shell_in_chamber.fill({0,0,-1});}
		void manual_close()
		{geometry.waist_distance=10;geometry.barrel_distance=0;geometry.barrel_angle=profile.interaction.open_angle;pinch(false);pinch(true);
			geometry.barrel_angle=0;step();pinch(false);}
	};
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		check(w::break_action_definitions.size()==2,"only Ranger/M79 register break-action feeds");
		{
			fixture m79(w::m79::feed,1);
			namespace storage=w::native_ammunition::storage;namespace projection=w::native_ammunition::projection;
			std::array<std::byte,storage::extent> native_ps{};w::clip_ledger clips;const w::weapon_identity id{77,1};
			check(clips.add(id,1,77,0) && storage::commit(native_ps,77,77,0,0,1,10),"projected M79 starts with one round in native PS and ledger");
			check(!scheduler::is_executing(scheduler::pipeline::server),"native PMove fixture is outside scheduler callback write authority");
			const auto before=projection::read_native_boundary(clips,native_ps,id,77,77);
			check(storage::commit(native_ps,77,77,1,10,0,10),"native engine consumes the cartridge before ledger synchronization");
			const auto unchanged=native_ps;const auto after=projection::read_native_boundary(clips,native_ps,id,77,77);
			check(before && after && before->loaded==1 && after->loaded==0 && clips.find(id)->loaded==1 && native_ps==unchanged,
				"native boundary sees 1-to-0 despite stale ledger 1 and never writes either source");
			const w::weapon_identity other{77,2};check(clips.add(other,1,77,0),"same-model nonprojected fixture added");
			check(!projection::read_native_boundary(clips,native_ps,other,77,77) && !projection::read_native_boundary(clips,native_ps,id,78,77),
				"native debit cannot be attributed to a nonprojected instance or foreign clip key");
			if(after)m79.native=*after;
			const auto tx=b::plan(m79.profile.ammunition,m79.state,{b::operation::accepted_shot,77,1,m79.state.revision,vr::hand::right,vr::hand::right});
			check(tx && before && tx.before==*before && tx.after==m79.native,"actual native M79 debit exactly matches its physical transaction");
			if(tx && before && after && tx.before==*before && tx.after==*after)m79.state=tx.next;
			m79.open();check(m79.state.phase==b::action::open && m79.state.spent==0 && m79.cases==1,"fired M79 opens and ejects without dropping/reacquiring the weapon");
			m79.load(0);m79.manual_close();check(b::ready(m79.profile.ammunition,m79.state),"fired-opened M79 reloads and closes normally");
			b::presentation gate;gate.active=true;gate.owner=m79.owner;gate.ammo=m79.state;gate.fire_armed=true;gate.reference=1;gate.sampled_at=m79.now;
			check(gate.allows_trigger(m79.owner,1,m79.now),"closed M79 can request native fire");
			gate.ammo.live=0;check(gate.allows_trigger(m79.owner,1,m79.now),"closed empty M79 retains native dry-click input");
			gate.ammo.phase=b::action::open;check(!gate.allows_trigger(m79.owner,1,m79.now),"open M79 cannot start the delayed projectile route");
			gate.ammo.phase=b::action::closed;gate.fire_armed=false;check(!gate.allows_trigger(m79.owner,1,m79.now),"closing cannot replay a held native launcher trigger");
			gate.fire_armed=true;check(!gate.allows_trigger(m79.owner,2,m79.now) && !gate.allows_trigger(m79.owner,1,m79.now+151ms),"stale/recentered feed snapshots cannot authorize launcher input");
		}
		{
			const auto& ranger=w::ranger::base;const auto span=sub(ranger.wrists[0].position,ranger.wrists[1].position);
			const auto straight=w::aimed_rotation(ranger.aiming,{0,0,0,1},{0,0,0},{20,0,0},span);
			const auto lifted=w::aimed_rotation(ranger.aiming,{0,0,0,1},{0,0,0},{20,0,8},span);
			check(ranger.aiming==w::aim_rule::two_hand && dot(rotate(straight,{1,0,0}),rotate(lifted,{1,0,0}))<.99f,"Ranger supporting hand controls weapon direction");
			check(w::ranger::feed.shell_fingers.data()==w::hand_poses::shell_grasp::shell_fingers.data(),"Ranger single-cartridge interaction reuses the established single-shell hand pose");
		}
		for(const auto* p:w::break_action_definitions)
		{
			check(length(sub(w::barrel_pose(*p,0).position,p->barrel_closed.position))<.001f &&
				length(sub(w::barrel_pose(*p,1).position,p->barrel_open.position))<.001f,"fixed hinge reproduces both native barrel endpoints");
			const auto rest_radius=length(sub(p->barrel_closed.position,p->hinge_pivot));
			for(float fraction:{.1f,.3f,.5f,.8f})check(std::abs(length(sub(w::barrel_pose(*p,fraction).position,p->hinge_pivot))-rest_radius)<.001f,
				"barrel follows a circular hinge rather than a translating chord");
			check(w::native_break_action_profile(p->native_name,int(p->ammunition.capacity),p)==p &&
				!w::native_break_action_profile(p->native_name,3,p) && !w::native_break_action_profile("unreviewed",1),"break-action native admission uses exact name/capacity/recipe");
			const auto copy=*p;check(!w::native_break_action_profile(p->native_name,int(p->ammunition.capacity),&copy),"unregistered break-action recipe cannot admit");
			for(auto rear:{vr::hand::left,vr::hand::right})for(unsigned initial=0;initial<=p->ammunition.capacity;++initial)
			{
				fixture f(*p,int(initial));f.owner.rear=rear;f.step();const auto total=b::total_rounds(f.state);
				f.button(true);check(f.state.phase==b::action::opening && f.ejections==0 && !b::ready(p->ammunition,f.state),"B/Y opens and blocks fire before delayed automatic ejection");
				for(int n=0;n<30;++n)f.step();
				check(f.state.phase==b::action::open && f.state.spent==0 && std::popcount(f.state.live)==int(initial) && f.cases==int(p->ammunition.capacity-initial),
					"fully opened breech ejects only spent cases and retains each unfired barrel");
				const int events=f.ejections;f.step(false);for(int n=0;n<30;++n)f.step();f.button(false);f.button(true);f.button(false);
				check(f.ejections==events && f.state.phase==b::action::open,"held/open B and duplicate scenes neither close nor re-eject");
				for(unsigned n=initial;n<p->ammunition.capacity;++n)f.load(n);
				check(f.state.live==b::mask(p->ammunition) && b::total_rounds(f.state)==total,"individual-shell reload fills only empty chambers and conserves reserve");
				f.input.trigger[int(rear)].down=true;f.manual_close();
				check(f.state.phase==b::action::closed && f.state.hinge==0 && !f.control.fire_armed(),"auxiliary barrel close latches and cannot replay held fire");
				f.input.trigger[int(rear)].down=false;f.step();check(f.control.fire_armed(),"neutral Trigger rearms after closure");
				const auto shot=b::plan(p->ammunition,f.state,{b::operation::accepted_shot,77,1,f.state.revision,rear,rear});
				check(shot && f.commit(shot),"latched break-action shot settles once");f.state=shot.next;
				check(f.state.spent==1 && std::popcount(f.state.live)==int(p->ammunition.capacity)-1,"shot retains one spent case in its exact chamber");
				f.open();check(f.state.spent==0 && f.state.live==(b::mask(p->ammunition)&~1u),"partial Ranger opening does not discard the other live cartridge");
				f.load(0);check(b::total_rounds(f.state)+f.shots==total,"repeated partial reload cannot fabricate ammunition");
			}
			for(int cause=0;cause<4;++cause)
			{
				fixture f(*p,0);f.open();const auto total=b::total_rounds(f.state);f.geometry.waist_distance=0;f.pinch(true);
				check(f.state.held_rounds==1,"one Trigger press takes exactly one shell");
				if(cause==0)f.input.focused=false;if(cause==1)f.input.grip[0].valid=false;
				if(cause==2){++f.input.reference_generation;++f.geometry.reference_generation;}if(cause==3)f.manipulation=false;
				f.step();check(!f.state.held_rounds && b::total_rounds(f.state)==total,"focus/tracking/recenter/occupancy cancellation refunds one held cartridge");
				f.input.focused=true;f.input.grip[0].valid=true;f.manipulation=true;f.step();check(!f.state.held_rounds,"recovery never replays held shell acquisition");
			}
			fixture reject(*p,0);reject.writable=false;reject.button(true);check(reject.state.phase==b::action::closed,"rejected open keeps hinge and cases unchanged");
			reject.writable=true;reject.step();check(reject.state.phase==b::action::closed,"failed open consumes its edge");
			reject.button(false);reject.open();reject.geometry.waist_distance=0;reject.pinch(true);reject.geometry.shell_in_chamber[0]={0,0,0};reject.writable=false;reject.step();
			check(reject.state.held_rounds==1 && !reject.state.live,"rejected chamber insertion retains escrow");
			reject.writable=true;reject.step();check(!reject.state.live,"failed insertion requires fresh withdrawal/contact");
			reject.geometry.shell_in_chamber[0]={0,0,-1};reject.step();reject.geometry.shell_in_chamber[0]={0,0,0};reject.step();check(reject.state.live==1,"fresh contact can retry a rejected insertion");
			fixture inertial(*p,0);inertial.open();float pitch=0;
			for(int n=0;n<8;++n)
			{
				pitch+=.09f;const quat q{std::sin(pitch/2),0,0,std::cos(pitch/2)};
				const std::array<vec,3> axis{rotate(q,{1,0,0}),rotate(q,{0,1,0}),rotate(q,{0,0,1})};
				for(int row=0;row<3;++row)for(int col=0;col<3;++col)inertial.input.aim[1].tracking.orientation[row][col]=axis[col][row];
				inertial.step();
			}
			check(inertial.state.phase==b::action::closing,"tracked pitch impulse starts automatic barrel closure through the real controller");
			for(int n=0;n<22;++n)inertial.step();
			check(inertial.state.phase==b::action::closed && inertial.state.hinge==0,"inertial closure reaches a mechanically latched state");
			for(bool mirror:{false,true})
			{
				const quat basis=normalize({.3f,-.2f,.5f,.7f});const auto grip=mirror ? vr::gameplay::hands::pose_mirror::part(p->barrel_grip,basis) : p->barrel_grip;
				const auto closed_contact=compose(grip.wrist,{grip.contact_in_wrist,{0,0,0,1}});
				const auto open_contact=compose(compose(p->barrel_open,inverse(p->barrel_closed)),closed_contact);
				check(length(sub(open_contact.position,closed_contact.position))>1.f,"authored barrel hand contact actually follows the hinge in either hand");
			}
		}
		for(const auto* p:{&w::m79::feed,&w::ranger::feed})for(int cadence:{200,250})
		{
			fixture f(*p,0);f.input.continuity_generation=1;f.step();f.open();
			f.now+=std::chrono::milliseconds(cadence-10);f.step(); // Neutral pitch baseline.
			const float angle=9.f*float(cadence)/1000;const quat q{std::sin(angle/2),0,0,std::cos(angle/2)};
			const std::array<vec,3> axis{rotate(q,{1,0,0}),rotate(q,{0,1,0}),rotate(q,{0,0,1})};
			for(int row=0;row<3;++row)for(int col=0;col<3;++col)f.input.aim[1].tracking.orientation[row][col]=axis[col][row];
			f.now+=std::chrono::milliseconds(cadence-10);f.step();
			check(f.state.phase==b::action::closing,"Ranger and M79 pitch close retains actual fast motion at continuous breach cadence");
		}
		// The reusable bounded angular window is projected on pitch for a break
		// action, retaining the revolver's default roll axis unchanged.
		for(float step:{.008f,.025f,.04f})for(float rate:{2.f,5.f,9.f,-9.f})
		{
			vr::gameplay::weapons::cylinder::twist_gate gate;const auto p=w::m79::feed.interaction.flick;auto time=b::clock::time_point{}+1s;
			gate.sample(p,{0,0,0,1},time,true,{1,0,0});bool fired=false;float angle=0;
			for(int n=0;n<int(.20f/step);++n){angle+=rate*step;time+=std::chrono::duration_cast<b::clock::duration>(std::chrono::duration<float>(step));
				fired=gate.sample(p,{std::sin(angle/2),0,0,std::cos(angle/2)},time,true,{1,0,0}) || fired;}
			check(fired==(rate==9.f),"pitch flick accepts deliberate closing motion but rejects ordinary/slow/reversed handling across rates");
		}
		const auto rules=w::ranger::feed.ammunition;auto s=b::import_native(rules,88,1,{2,100});std::uint32_t random=12345;int spent=0;const auto total=b::total_rounds(s);
		for(int n=0;n<12000;++n)
		{
			random=random*1664525u+1013904223u;const auto op=b::operation((random>>16)%9);const bool off=op==b::operation::draw || op==b::operation::load || op==b::operation::discard || op==b::operation::cleanup;
			const auto tx=b::plan(rules,s,{op,88,1,s.revision,vr::hand::right,off ? vr::hand::left : vr::hand::right,(random>>8)&1,float(random&1)});
			if(tx){s=tx.next;spent+=tx.rounds_spent;}check(b::valid(rules,s) && b::total_rounds(s)+spent==total && !(s.live&s.spent),"random break-action operation sequences preserve masks and ammunition");
		}
	}
}
