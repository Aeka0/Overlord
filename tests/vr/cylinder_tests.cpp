#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/cylinder_gesture.hpp"
#include "component/vr/gameplay/weapons/magnum44/profile.hpp"
#include "component/vr/gameplay/cylinder_presenter.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/weapon_sound_dispatch.hpp"
#include <iostream>
#include <limits>
#include <random>

namespace c = vr::gameplay::weapons::cylinder;
namespace w = vr::gameplay::weapons;
using namespace std::chrono_literals;

struct fixture
{
	c::rules rules{6}; c::tuning tuning{};
	c::state state = c::import_native(rules,22,1,{6,18});
	c::controller gesture{};
	vr::controller_input::frame input{};
	w::hold owner{22,1,vr::hand::right,vr::hand::none,w::hold_source::engine_default,1};
	c::geometry geometry{true,22,1,1,1,.1f,1,1,{0,0,-.03f}};
	c::clock::time_point now{1s};
	w::ammunition::projection native{6,18};
	int commits{}, lost{}, clears{}; bool writable{true};
	bool manipulation{true};
	fixture()
	{
		input.sequence = input.reference_generation = 1; input.focused = true;
		for (int h=0;h<2;++h)
		{
			input.aim[h].valid = input.grip[h].valid = true;
			input.aim[h].tracking.orientation = {{{1,0,0},{0,1,0},{0,0,1}}};
			input.trigger[h] = input.secondary[h] = {true,false,0,1};
		}
		step();
	}
	bool write(const c::transaction& tx)
	{
		if (!writable || tx.before != native) return false;
		native = tx.after; ++commits; lost += tx.rounds_spent;
		if (tx.feedback == c::effect::clear) ++clears;
		return true;
	}
	void step(int milliseconds=10, bool fresh=true)
	{
		now += std::chrono::milliseconds(milliseconds);
		if (fresh) { ++input.sequence; input.sampled_at = now; geometry.input_sequence = input.sequence; }
		gesture.update(tuning,rules,state,input,owner,geometry,true,now,[&](auto& tx){ return write(tx); },{manipulation});
	}
	void key(vr::controller_input::digital_action& action, bool down)
	{ if (down && !action.down) ++action.presses; action.down=down; step(); }
	void open() { key(input.secondary[1],true); key(input.secondary[1],false); }
	void wait_open() { for (int i=0;i<25;++i) step(); }
	void take() { key(input.trigger[0],true); }
	void drop() { key(input.trigger[0],false); }
	bool op(c::operation operation, vr::hand actor = vr::hand::right)
	{
		const auto tx=c::plan(rules,state,{operation,state.weapon,state.instance_generation,state.revision,owner.rear,actor});
		if (!tx || !write(tx)) return false;
		state=tx.next; return true;
	}
};

int main()
{
	int failed{};
	const auto check=[&](bool value,const char* name) { if (!value) { ++failed; std::cerr << "FAIL: " << name << '\n'; } };
	check(c::valid(c::tuning{}),"default gesture tuning valid");
	for(auto event:{c::effect::open,c::effect::fill,c::effect::close,c::effect::clear})
	{
		const auto sound=w::magnum44::sound(event);int emissions{};
		const std::array<float,3> origin{1,2,3};
		const auto emit=[&](auto token,std::string_view name,int capacity,w::sound_reference reference,const auto& point){
			++emissions;return token==129 && name=="coltanaconda_shepherd" && capacity==6 && reference.name==sound.name && reference.kind==sound.kind && point==origin;};
		check(w::dispatch_profile_sound(w::magnum44::feed,129,129,"coltanaconda_shepherd",6,true,sound,origin,emit) && emissions==1,
			"Shepherd cylinder feedback forwards the actual variant identity and semantic sound to native playback");
		for(auto name:{"coltanaconda_akimbo","coltanaconda_unknown"})
			check(!w::dispatch_profile_sound(w::magnum44::feed,129,129,name,6,true,sound,origin,emit),"unverified cylinder variants never emit sound");
		check(!w::dispatch_profile_sound(w::magnum44::feed,129,130,"coltanaconda_shepherd",6,true,sound,origin,emit) &&
			!w::dispatch_profile_sound(w::magnum44::feed,129,129,"coltanaconda_shepherd",7,true,sound,origin,emit) &&
			!w::dispatch_profile_sound(w::magnum44::feed,129,129,"coltanaconda_shepherd",6,false,sound,origin,emit) && emissions==1,
			"stale token, invalid observation and changed capacity remain rejected before playback");
	}
	for (auto rear:{vr::hand::left,vr::hand::right})
	{
		fixture f;f.owner.rear=rear;f.manipulation=false;f.step();
		const auto off=1-int(rear);f.input.grip[off].valid=f.input.aim[off].valid=false;f.input.trigger[off].active=false;
		f.geometry.opening_up=-1;f.key(f.input.secondary[int(rear)],true);f.wait_open();
		check(f.state.phase==c::action::open && f.clears==1,"cylinder own controls and gravity remain active without opposite hand");
		f.input.grip[off].valid=f.input.aim[off].valid=true;f.input.trigger[off]={true,true,1,2};f.step();
		f.manipulation=true;f.step();
		check(f.state.loader_hand==vr::hand::none,"occupied hand does not queue a loader draw for later");
		f.key(f.input.trigger[off],false);f.key(f.input.trigger[off],true);
		check(f.state.loader_hand==vr::hand(off),"freed hand can draw with a fresh pinch");
	}
	{
		const auto& p=w::magnum44::base;
		check(!p.reload && p.cylinder==&w::magnum44::feed && !w::native_reload_profile("coltanaconda",6),"cylinder never admitted as detachable/plus-one feed");
		check(p.cylinder->matches_native("coltanaconda",6) && !p.cylinder->matches_native("colt_anaconda",6) &&
			!p.cylinder->matches_native("coltanaconda_akimbo",6) && !p.cylinder->matches_native("coltanaconda",7),"exact native identifier and capacity, not export aliases");
		check(p.cylinder->matches_native("coltanaconda_shepherd",6) &&
			!p.cylinder->matches_native("coltanaconda_shepherd",7) && !p.cylinder->matches_native("coltanaconda_unknown",6),
			"verified story revolver admitted without broadening unknown variant or capacity contracts");
		vr::gameplay::hands::rig r{}; r.count=59; r.gun=34; r.arms[0].wrist=2; r.arms[1].wrist=3;
		r.parent.fill(-1); std::array<vr::gameplay::hands::bone_definition,59> bones{};
		for (int n=0;n<30;++n)
		{
			bones[4+n].name=p.fingers[n].name;
			r.parent[4+n]=bones[4+n].name.find("_le_")!=std::string_view::npos ? 2 : 3;
		}
		bones[34].name="j_gun"; r.parent[34]=1; r.weapon_bones[34]=true;
		for (int n=0;n<24;++n)
		{
			bones[35+n].name=p.equip_rest[n].name;
			bones[35+n].bind.position=p.equip_rest[n].local.position;
			bones[35+n].bind.rotation=p.equip_rest[n].local.rotation;
			r.parent[35+n]=34; r.weapon_bones[35+n]=true;
		}
		// Captured receiver-local hierarchy, offset after the synthetic hand rig.
		r.parent[51]=36; // cylinder -> swing
		for (int n=0;n<6;++n) { r.parent[45+n]=35; r.parent[53+n]=45+n; }
		r.parent[52]=44; // knife_fx -> knife
		for (int n=0;n<r.count;++n) { bones[n].parent=r.parent[n]; if (n<35) bones[n].bind.rotation={0,0,0,1}; }
		check(vr::gameplay::hands::bind_weapon_poses(r,bones,p).valid,"complete Magnum fingers and equip poses bind");
		const auto bound=c::bind_parts(r,bones);
		check(bound.valid && bound.cases[0]==45 && bound.loader==38,"cylinder rig binds named live hierarchy");
		std::array<vr::gameplay::hands::bone,59> solved{};
		for (size_t n=0;n<bones.size();++n) solved[n]=bones[n].bind;
		const auto close=[](auto a,auto b) { return vr::gameplay::hands::length(vr::gameplay::hands::sub(a,b))<.001f; };
		for (const float opening:{0.f,1.f,0.f})
		{
			using namespace vr::gameplay::hands::pose_math;
			solved[r.gun].position={10,-20,30}; solved[r.gun].rotation={0,0,.7071068f,.7071068f};
			c::pose_parts(r,bound,*p.cylinder,opening,solved);
			const auto expected=compose(as_anchor(solved[bound.barrel]),p.cylinder->ammo_in_cylinder);
			check(close(solved[bound.ammo].position,expected.position),"cylinder rounds follow barrel through closed/open/closed independent poses");
			const auto swing=compose(as_anchor(solved[r.gun]),p.cylinder->swing_closed);
			check(close(solved[bound.swing].position,swing.position),"cylinder assembly follows translated and rotated receiver");
		}
		const auto hidden=c::hidden_parts(bound,{});
		const auto omitted=[&](int bone) { return bool(hidden[bone/32]&(0x80000000u>>(bone%32))); };
		check(omitted(bound.loader) && omitted(bound.cases[0]) && omitted(bound.tips[5]) && !omitted(bound.barrel),
			"inactive mechanics hide drifting animation loader and unverified rounds but keep assembled cylinder");
		std::array<vr::gameplay::hands::model_definition,2> models{{{"viewhands_us_army",0,34},{p.receiver,34,25}}};
		check(w::select_profile(models,r,bones).value==&p,"whole Magnum assembly selects its own feed profile");
		r.parent[45]=34; check(!c::bind_parts(r,bones).valid,"wrong case parent rejects feed admission");
		check(p.suppress_equip("h2_wpn_pst_colt_anaconda_pullout") && !p.suppress_equip("h2_wpn_pst_colt_anaconda_reload"),"equip suppression never reclassifies reload");
		check(p.cylinder->sound_key(c::effect::clear).kind==w::sound_reference_kind::alias &&
			std::string_view(p.cylinder->sound_key(c::effect::clear).name)=="shell_eject_pistol",
			"gravity clear has a verified explicit native casing alias, not a fictional WeaponDef key");
		check(p.cylinder->sound_key(c::effect::fill).kind==w::sound_reference_kind::notetrack &&
			std::string_view(p.cylinder->sound_key(c::effect::fill).name)=="weap_coltanaconda_clipin_plr",
			"accepted fill sound mapping unchanged");
		check(!p.cylinder->sound_key(c::effect::none).name && !p.cylinder->sound_key(c::effect::shot).name,
			"native shots and silent lifecycle events are not replayed");
	}
	{
		fixture f; f.geometry.opening_up=-1; f.open();
		check(f.state.phase==c::action::opening && f.state.live==6 && f.clears==0,"B downward retains rounds during opening");
		for (int i=0;i<15;++i) f.step();
		check(f.state.live==6 && f.clears==0,"no early gravity discharge");
		f.wait_open();
		check(f.state.phase==c::action::open && f.state.live==0 && f.native.reserve==24 && f.clears==1,"already-down clears exactly once at full open");
		f.wait_open(); check(f.clears==1,"empty gravity condition idempotent");
	}
	{
		fixture f; f.geometry.opening_up=-1; f.open(); f.geometry.opening_up=1; f.wait_open();
		check(f.state.live==6 && f.clears==0,"turning up before full opening cancels no queued event");
		f.key(f.input.secondary[1],true); check(f.state.phase==c::action::open && f.state.live==6,"B again is no-op");
		f.take(); check(f.state.held_rounds==6 && f.state.live==6,"filled cylinder rejects loader without destroying it");
		f.geometry.opening_up=-1; f.step();
		check(c::empty(f.state) && f.state.held_rounds==6,"downward clears but never fills");
		f.geometry.opening_up=0; f.step(); check(c::empty(f.state),"neutral band rejects fill");
		f.geometry.opening_up=1; f.step();
		check(f.state.live==6 && f.state.held_rounds==0 && f.state.loader_hand==vr::hand::left,"same contact fills immediately upward, retains empty loader");
		const int committed=f.commits; f.step(0,false); f.wait_open();
		check(f.commits==committed,"same contact and duplicate input never fill twice");
		f.drop(); check(f.state.loader_hand==vr::hand::none && f.native.reserve==18,"dropping empty loader no ammo effect");
	}
	{
		fixture f; f.state.live=0; f.state.spent=6; f.state.reserve=3; f.native={0,3};
		f.open(); f.wait_open(); f.take();
		check(f.state.held_rounds==3 && f.native.reserve==0,"partial supply takes three rounds");
		check(f.state.spent==6 && !f.state.live,"cases alone reject loading");
		f.geometry.opening_up=-1; f.step(); f.geometry.opening_up=1; f.step();
		check(f.state.live==3 && f.state.spent==0,"partial fill transfers three immediately");
		check(f.op(c::operation::close) && f.op(c::operation::closed),"closure independent of held empty loader");
		for (int n=0;n<3;++n) check(f.op(c::operation::accepted_shot),"next three shots fire regardless of hole index");
		check(!f.op(c::operation::accepted_shot) && f.state.spent==3,"three live become three cases, fourth shot rejected");
	}
	{
		fixture f; f.open(); f.wait_open(); f.geometry.opening_up=-1; f.step(); f.geometry.opening_up=1;
		f.take(); // already at face: draw and transfer within one consumed input
		check(f.state.live==6 && f.state.held_rounds==0,"draw/contact has no arbitrary additional delay");
		f.key(f.input.trigger[1],true);
		check(!f.gesture.fire_armed(),"held trigger while open blocked");
		f.op(c::operation::close); f.op(c::operation::closed); f.step();
		check(!f.gesture.fire_armed(),"held trigger across closure never queues shot");
		f.key(f.input.trigger[1],false); check(f.gesture.fire_armed(),"neutral closed sample rearms trigger");
	}
	{
		fixture f; f.take(); f.input.focused=false; f.step();
		check(f.state.held_rounds==0 && f.native.reserve==18,"tracking/focus cleanup refunds escrow");
		const int committed=f.commits; f.step(); check(f.commits==committed,"cleanup exactly once");
		f.input.focused=true; f.step(); check(f.state.loader_hand==vr::hand::none,"held input on reconnection cannot redraw");
		f.drop(); f.take(); check(f.state.held_rounds==6,"fresh press after reconnect draws");
		f.writable=false; f.drop(); check(f.state.held_rounds==6,"failed native compare preserves escrow");
		f.writable=true; f.step(); check(f.state.held_rounds==0,"safe refund retries without losing ammo");
	}
	{
		fixture f; f.writable=false; f.open(); f.writable=true; f.wait_open();
		check(f.state.phase==c::action::closed,"failed B edge is not replayed later");
		f.input.aim[1].tracking.orientation[0][0]=std::numeric_limits<float>::quiet_NaN(); f.step();
		check(!f.gesture.fire_armed(),"invalid tracking matrix fails closed");
	}
	{
		c::twist_gate gate; c::tuning p; c::clock::time_point t{1s};
		const auto roll=[](float theta) { return vr::gameplay::hands::quat{0,0,-std::sin(theta/2),std::cos(theta/2)}; };
		gate.sample(p,roll(0),t,true); t+=10ms; gate.sample(p,roll(0),t,true);
		bool closed=false;
		for (int i=1;i<=8;++i) { t+=10ms; closed |= gate.sample(p,roll(.14f*i),t,true); }
		check(closed,"deliberate bounded rightward roll closes");
		gate.reset(); t+=10ms; gate.sample(p,roll(0),t,true); t+=10ms; gate.sample(p,roll(0),t,true);
		closed=false;
		for (int i=1;i<=8;++i) { t+=10ms; closed |= gate.sample(p,roll(-.12f*i),t,true); }
		check(!closed,"opposite roll not mirrored or accepted");
		gate.reset(); t+=10ms; gate.sample(p,roll(0),t,true); t+=10ms; gate.sample(p,roll(0),t,true);
		t+=10ms; check(!gate.sample(p,roll(1.5f),t,true),"tracking spike rejected");
	}
	{
		const auto roll=[](float theta){return vr::gameplay::hands::quat{0,0,-std::sin(theta/2),std::cos(theta/2)};};
		c::twist_gate continuous,legacy,changed;const c::tuning p;const c::clock::time_point at{1s};
		continuous.sample(p,roll(0),at,true,{0,0,-1},1);
		legacy.sample(p,roll(0),at,true);
		changed.sample(p,roll(0),at,true,{0,0,-1},1);
		check(continuous.sample(p,roll(2.8f),at+200ms,true,{0,0,-1},1),"fresh continuous slowmo roll retains cylinder close gesture");
		check(!legacy.sample(p,roll(2.8f),at+200ms,true),"unannotated motion still rejects long gaps");
		check(!changed.sample(p,roll(2.8f),at+200ms,true,{0,0,-1},2),"producer generation change cannot fund cylinder close");
		continuous.reset();continuous.sample(p,roll(0),at,true,{0,0,-1},1);
		check(!continuous.sample(p,roll(2.8f),at+1s,true,{0,0,-1},1),"long consumer gaps establish a bounded motion baseline");
	}
	for (auto rear : {vr::hand::left,vr::hand::right})
	{
		using namespace vr::gameplay::hands;
		const auto orientation=[](float roll,float swing=0.f) {
			return normalize(multiply(quat{0,0,-std::sin(roll/2),std::cos(roll/2)},
				quat{std::sin(swing/2),0,0,std::cos(swing/2)}));
		};
		for (int step:{8,11,22,44})
		{
			c::twist_gate gate; c::tuning p; c::clock::time_point t{1s};
			const auto base=normalize(quat{.2f,.1f,.3f,.8f});
			gate.sample(p,base,t,true);
			// Quiet off-axis jitter used to disarm BEFORE the neutral check.
			for (int n=0;n<20;++n) { t+=std::chrono::milliseconds(step); gate.sample(p,multiply(base,orientation(0,(n%2)*.001f)),t,true); }
			bool closed=false;
			for (int ms=step;ms<=220;ms+=step)
			{
				const float phase=std::min(ms/100.f,1.f);
				const float angle=1.15f*(.5f-.5f*std::cos(phase*3.14159265f));
				t+=std::chrono::milliseconds(step);
				closed |= gate.sample(p,multiply(base,orientation(angle,.3f*phase)),t,true);
			}
			check(closed,"mixed-axis accelerate/decelerate flick accepted across 23-125Hz and both hands");
			gate.reset(); t+=1s; gate.sample(p,base,t,true); closed=false;
			for (int ms=step;ms<=500;ms+=step) { t+=std::chrono::milliseconds(step); closed|=gate.sample(p,multiply(base,orientation(ms*.0008f)),t,true); }
			check(!closed,"slow rotation never closes by accumulating old travel");
			for (float speed:{3.f,6.f,9.f,11.f})
			{
				gate.reset(); t+=1s; gate.sample(p,base,t,true); closed=false;
				for (int ms=step;ms<=220;ms+=step)
				{ t+=std::chrono::milliseconds(step); closed|=gate.sample(p,multiply(base,orientation(ms*.001f*speed)),t,true); }
				check(!closed,"sub-threshold handling tilt rejected even with enough angle and pure roll");
			}
			gate.reset(); t+=1s; gate.sample(p,base,t,true); closed=false;
			for (int ms=step;ms<=220;ms+=step) { t+=std::chrono::milliseconds(step); closed|=gate.sample(p,multiply(base,orientation(0,ms*.004f)),t,true); }
			check(!closed,"pure pitch does not close, regardless of pose in room");
		}
		c::twist_gate gate; c::tuning p; c::clock::time_point t{1s};
		gate.sample(p,orientation(0),t,true); t+=44ms;
		check(gate.sample(p,orientation(1.5f),t,true),"real fast flick above old 20rad/s ceiling survives server sampling");
		gate.reset(); t+=1s; gate.sample(p,orientation(0),t,true); t+=10ms;
		check(!gate.sample(p,orientation(1.5f),t,true),"implausible tracking jump still rejected");
		gate.reset(); t+=1s; gate.sample(p,orientation(0),t,true); t+=8ms;
		check(!gate.sample(p,orientation(.12f),t,true),"brief fast twitch lacks meaningful fast travel");
		bool delayed_close=false;
		for (int n=1;n<=12;++n) { t+=10ms; delayed_close|=gate.sample(p,orientation(.12f+n*.04f),t,true); }
		check(!delayed_close,"fast twitch followed by slow tilt cannot spend an old peak");
		gate.reset(); t+=1s; gate.sample(p,orientation(0),t,false); t+=44ms;
		check(!gate.sample(p,orientation(1.5f),t,true),"motion before fully open never closes at eligibility edge");
		t+=1s; check(!gate.sample(p,orientation(2.f),t,true),"stale sample gap establishes new baseline");
		fixture f; f.owner.rear=rear; f.state.phase=c::action::open; f.step();
		for (int n=1;n<=6;++n)
		{
			const auto q=orientation(n*.30f);
			const std::array<vec,3> basis{rotate(q,{1,0,0}),rotate(q,{0,1,0}),rotate(q,{0,0,1})};
			for (int row=0;row<3;++row) for (int col=0;col<3;++col)
				f.input.aim[static_cast<int>(rear)].tracking.orientation[row][col]=basis[col][row];
			f.step(22);
		}
		check(f.state.phase==c::action::closing,"full raw tracking matrix -> gesture -> native commit path closes for either rear hand");
	}
	for (auto rear : {vr::hand::left,vr::hand::right})
	{
		std::mt19937 random(44);
		c::state s=c::import_native({6},22,7,{6,24}); int spent=0;
		for (int i=0;i<50000;++i)
		{
			const auto op=static_cast<c::operation>(random()%11);
			const auto actor=static_cast<vr::hand>(random()%2);
			const auto tx=c::plan({6},s,{op,22,7,s.revision,rear,actor});
			if (tx) { s=tx.next; spent+=tx.rounds_spent; }
			check(c::valid({6},s) && c::total_rounds(s)+spent==30,"random operation conservation/bounds");
		}
	}
	{
		const auto s=c::import_native({6},22,1,{4,18});
		check(s.live==4 && s.spent==2,"initial native import documents conservative spent cases");
		check(!c::plan({6},s,{c::operation::open,22,2,s.revision,vr::hand::right,vr::hand::right}),"stale instance rejected");
		check(!c::valid({6},c::import_native({6},22,1,{7,18})),"no chamber plus one admission");
		check(w::ammunition::dispose(3,w::ammunition::disposition_reason::deliberate_discard,true).lost==3 &&
			w::ammunition::dispose(3,w::ammunition::disposition_reason::forced_cleanup,true).returned==3,"future penalty seam cannot punish cleanup");
	}
	std::cout << "Cylinder tests: " << (failed ? "FAIL" : "PASS") << '\n';
	return failed ? 1 : 0;
}
