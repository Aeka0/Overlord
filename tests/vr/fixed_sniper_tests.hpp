#pragma once
#include "component/vr/gameplay/fixed_sniper_policy.hpp"
#include <limits>

template<class Check> void fixed_sniper_tests(Check check)
{
	using namespace vr::controller_input;
	using namespace vr::gameplay::fixed_sniper;
	check(accepts(0x3002,750,"m82_bipod_stand_thermal") && !accepts(0,750,"m82_bipod_stand_thermal") &&
		!accepts(0x3002,4000,"m82_bipod_stand_thermal") && !accepts(0x3002,750,"barrett") &&
		!accepts(0x3002,750,"minigun_laatpv_player"),"fixed scope admits only the occupied native story weapon");
	check(overlay_material("h2_hud_overlay_sniper_thermal_reticle") && !overlay_material("postfx_color2") &&
		!overlay_material("h1_hud_overlay_sniperescape_scope_extra"),"scope HUD excludes unrelated and prefix-only materials");
	controller control;frame f;auto now=clock::now();
	f.focused=f.move_active=f.turn_active=true;f.sequence=f.reference_generation=f.continuity_generation=1;
	for(unsigned h=0;h<2;++h){f.grip[h].valid=f.aim[h].valid=true;f.trigger[h].active=true;f.trigger[h].generation=1;}
	f.primary[1].active=true;f.primary[1].generation=1;
	for(auto& button:f.secondary){button.active=true;button.generation=1;}
	const auto sample=[&](std::uint64_t epoch=1,bool allowed=true){now+=std::chrono::milliseconds(20);f.sampled_at=now;++f.sequence;return control.consume(f,epoch,allowed,.2f,60,now);};
	f.move={1,1};f.turn={1,1};f.trigger[1].down=true;++f.trigger[1].presses;
	auto out=sample();check(out.zoom==0 && out.pitch==0 && out.yaw==0 && !out.fire,"held entry input cannot zoom, aim or fire");
	f.move=f.turn={};f.trigger[1].down=false;sample();
	f.move={1,1};f.turn={1,1};f.trigger[1].down=true;++f.trigger[1].presses;out=sample();
	check(out.zoom==.25f && out.pitch<0 && out.yaw<0 && out.fire,"right forward supplies quarter-strength zoom while left up/right aims and a deliberate trigger fires");
	out=sample();check(std::abs(out.pitch+.12f)<.001f && std::abs(out.yaw+.12f)<.001f,"both aiming axes integrate at ten percent of the ordinary turn speed");
	f.turn={-1,0};f.move={};out=sample();check(out.zoom==0 && out.yaw==0,"right lateral deflection cannot strafe or turn");
	f.turn={0,-1};f.move={-1,-1};out=sample();check(out.zoom==-.25f && out.pitch>0 && out.yaw>0,"reverse zoom and both opposite aim axes retain signs");
	f.turn_active=false;out=sample();check(out.zoom==0 && out.pitch>0,"missing right stick cannot block left aim");
	f.turn_active=true;f.turn={};sample();f.turn={0,1};f.move[0]=std::numeric_limits<float>::quiet_NaN();
	out=sample();check(out.zoom==.25f && out.pitch==0 && out.yaw==0,"invalid left axis does not poison right zoom or angles");
	f.move={};sample();f.move={1,0};out=sample(2);check(!out.fire && out.zoom==0 && out.yaw==0,"a new turret epoch rearms held actions");
	f.focused=false;sample(2);f.focused=true;out=sample(2);check(!out.fire && out.yaw==0,"focus resume requires neutral inputs");
	f.move=f.turn={};f.trigger[1].down=false;sample(2);f.primary[1].down=true;++f.primary[1].presses;
	check(!sample(2).exit,"A must no longer leave the fixed sniper");
	for(unsigned h=0;h<2;++h)
	{
		f.secondary[h].down=true;++f.secondary[h].presses;
		check(sample(2).exit && !sample(2).exit,"B or Y exits on its own neutral-armed edge");
		f.secondary[h].down=false;sample(2);
	}
	f.grip[0].valid=false;f.secondary[0].down=true;++f.secondary[0].presses;
	check(!sample(2).exit,"untracked Y cannot exit");f.grip[0].valid=true;
	check(!sample(2).exit,"tracking recovery cannot replay held Y");
	f.secondary[0].down=false;sample(2);
	f.trigger[0].down=true;++f.trigger[0].presses;check(sample(2).fire,"either tracked trigger can operate the fixed weapon");
	f.grip[0].valid=false;check(!sample(2).fire,"tracking loss cancels that hand's trigger");
	++f.reference_generation;out=sample(2);check(!out.fire && !out.exit,"recenter never replays held firing or exit");
}
