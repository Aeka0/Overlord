#pragma once
#include "component/vr/controller_calibration.hpp"
#include <limits>

template<class Check> void controller_orientation_tests(Check check)
{
	using namespace vr::controller_calibration;
	using namespace std::chrono_literals;
	vr::controller_input::frame raw;raw.sequence=12;raw.reference_generation=3;raw.focused=true;
	raw.sampled_at=vr::controller_input::clock::time_point{}+1s;
	const matrix identity{{{1,0,0},{0,1,0},{0,0,1}}};
	for (unsigned h=0;h<2;++h)
	{
		raw.aim[h].valid=raw.grip[h].valid=true;
		raw.aim[h].tracking.orientation=raw.grip[h].tracking.orientation=identity;
		raw.aim[h].tracking.position_meters={1,2,3};raw.grip[h].tracking.position_meters={4,5,6};
		raw.trigger[h].active=raw.trigger[h].down=true;raw.trigger[h].presses=7;
	}
	const auto near=[](float a,float b){return std::abs(a-b)<.00001f;};
	const auto same=[&](matrix a,matrix b){for (unsigned r=0;r<3;++r) for(unsigned c=0;c<3;++c) if(!near(a[r][c],b[r][c]))return false;return true;};
	calibration zero;const auto unchanged=zero.apply(raw,{});
	check(unchanged.aim[0].tracking.orientation==identity && unchanged.aim[1].tracking.orientation==identity,"zero angles exactly preserve runtime orientation");
	calibration pitch;const auto down=pitch.apply(raw,{-4,0,0});
	for(unsigned h=0;h<2;++h)
	{
		const auto& m=down.aim[h].tracking.orientation;
		check(near(-m[1][2],-.069756474f) && near(-m[2][2],-.99756405f) && near(m[0][0],1),"negative pitch tilts both hands down without yaw or roll");
		check(down.grip[h].tracking.orientation==raw.grip[h].tracking.orientation && down.grip[h].tracking.position_meters==raw.grip[h].tracking.position_meters &&
			down.aim[h].tracking.position_meters==raw.aim[h].tracking.position_meters && down.runtime_aim[h].tracking.orientation==identity &&
			down.runtime_grip[h].valid && down.runtime_grip[h].tracking.orientation==raw.grip[h].tracking.orientation &&
			down.runtime_grip[h].tracking.position_meters==raw.grip[h].tracking.position_meters,
			"angle calibration preserves raw grip, position pivots and runtime aim witness");
	}
	check(down.reference_generation==3 && down.sequence==12 && down.sampled_at==raw.sampled_at && down.trigger[0].presses==7 && down.trigger[0].down,
		"calibration does not fabricate input edges or recenter generations");
	calibration yaw,roll;
	check(near(-yaw.apply(raw,{0,90,0}).aim[0].tracking.orientation[0][2],-1),"positive yaw turns forward toward tracking left");
	check(near(roll.apply(raw,{0,0,90}).aim[0].tracking.orientation[1][0],-1),"positive roll sends the controller right axis down");
	const matrix global{{{0,-1,0},{0,0,-1},{1,0,0}}};auto rotated=raw;
	for(auto& h:rotated.aim)h.tracking.orientation=global;
	calibration local;check(same(local.apply(rotated,{-4,0,0}).aim[0].tracking.orientation,multiply(global,down.aim[0].tracking.orientation)),
		"correction follows controller-local axes even when the controller is rolled or vertical");
	for(const angles value:{angles{-180,180,-180},angles{23,-71,115}})
	{
		calibration c;const auto m=c.apply(raw,value).aim[0].tracking.orientation;
		bool orthogonal=true;
		for(unsigned r=0;r<3;++r)for(unsigned s=0;s<3;++s)
		{float d{};for(unsigned k=0;k<3;++k)d+=m[r][k]*m[s][k];orthogonal=orthogonal && near(d,r==s?1.f:0.f);}
		check(orthogonal,"combined and extreme angles preserve a finite orthonormal pose");
	}
	raw.sampled_at+=10ms;const auto changing=pitch.apply(raw,{-8,0,0});
	check(changing.focused && changing.orientation_settling && changing.aim[0].valid && changing.aim[1].valid && changing.grip[0].valid &&
		near(-changing.aim[0].tracking.orientation[1][2],-.1391731f),
		"live setting changes mark melee discontinuity while rendering corrected poses and preserving holding data");
	raw.sampled_at+=199ms;check(pitch.apply(raw,{-8,0,0}).orientation_settling,"pose jump stays marked during calibration settling");
	raw.sampled_at+=1ms;check(!pitch.apply(raw,{-8,0,0}).orientation_settling,"stable calibration resumes after the bounded history reset");
	for (float invalid:{181.f,-181.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
	{
		calibration c;const auto bad=c.apply(raw,{invalid,0,0});check(!bad.aim[0].valid && !bad.aim[1].valid,"invalid angle values never publish an actionable pose");
	}
	calibration missing;raw.aim[1].valid=false;check(!missing.apply(raw,{-4,0,0}).aim[1].valid,"calibration cannot invent tracking for a missing hand");
	calibration position_change;raw.aim[1].valid=true;
	const auto base=position_change.apply(raw,{-20,0,0});raw.sampled_at+=1ms;
	const angles position{.03f,.08f,-.02f};const auto adjusted=position_change.apply(raw,{-20,0,0},position);
	check(adjusted.position_offsets_meters==position && adjusted.orientation_settling && adjusted.aim[0].valid &&
		adjusted.aim[0].tracking.orientation==base.aim[0].tracking.orientation,
		"position calibration marks physical history discontinuity without changing aim angles");
	check(!position_change.apply(raw,{}, {0,0,.501f}).aim[0].valid,"malformed position calibration cannot publish an actionable hand pose");
}
