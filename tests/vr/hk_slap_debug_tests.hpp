#pragma once
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/hk_slap_geometry.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include <fstream>

namespace hk_slap_debug_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		namespace p=physical_reload;
		namespace d=p::hk_slap_debug;
		using vr::spatial_panel::matrix;
		using vr::spatial_panel::vec4;
		const auto now=p::clock::now();
		d::sample s;
		s.definition=&mp5::physical; s.units=40; s.contact_model={{25,0,0},{0,0,0,1}}; s.valid=s.visual_valid=true;
		s.object=10; s.matrices=20; s.epoch=30; s.instance=1; s.reference=2; s.input_sequence=3; s.at=now;
		s.owner={49,1,hand::right,hand::none,hold_source::engine_default,1};
		for (int f=0;f<5;++f)
		{
			for (int j=0;j<3;++j) s.raw[f*3+j]={.02f,(f-2)*.016f,.12f-j*.025f};
			s.raw[15+f]={.02f,(f-2)*.016f,.045f};
		}
		s.raw[20]={.02f,0,.16f}; s.visual=s.raw;
		for (auto& point:s.visual) point[1]+=.01f;
		s.trace.latest.sequence=2; s.trace.latest.at=now; s.trace.latest.examined=true; s.trace.latest.reason=p::slap_reason::separate;
		s.trace.last_contact=s.trace.latest; s.trace.last_contact.reason=p::slap_reason::direction;
		s.trace.last_contact.impact.point=17; s.trace.last_contact.impact.previous={-.07f,0,.07f}; s.trace.last_contact.impact.current={.04f,0,.045f};
		s.trace.last_contact.impact.direction_cosine=.22f;
		s.trace.last_contact.impact.sampled=true;
		const auto world=d::geometry_for(s,{});
		check(world[0].count>140 && world[0].count<vr::spatial_lines::capacity && world[1].count>0 && world[2].count>0,
			"HK diagnostic fits whole glove, visual glove, spheres, cone, trajectory and both verdict labels");
		check(std::abs(world[0].lines[0].a[0]-(25+s.definition->interaction.manual_catch->slap.radius*40))<1e-5f,
			"slap overlay radius comes from the actual controller profile");
		for (int reason=0;reason<=int(p::slap_reason::accepted);++reason)
		{
			auto copy=s; copy.trace.latest.reason=copy.trace.last_contact.reason=p::slap_reason(reason);
			const auto lines=d::geometry_for(copy,{});
			check(lines[1].count>0 && lines[2].count>0,"every full diagnostic reason fits without truncated lettering");
		}
		const matrix camera{0,0,0,1,-1,0,0,0,0,1,0,0,0,0,.01f,0};
		for (size_t n=0;n<world.size();++n)
		{
			const auto left=vr::spatial_lines::project(world[n],{0,1.28f,0},camera,.12f);
			const auto right=vr::spatial_lines::project(world[n],{0,-1.28f,0},camera,.12f);
			check(left.count>0 && right.count==left.count && left.lines[0].a[0]!=right.lines[0].a[0],"HK geometry and text use real per-eye disparity");
			const auto translated=d::geometry_for(s,{100,-200,30});
			const auto moved=vr::spatial_lines::project(translated[n],{100,-198.72f,30},camera,.12f);
			check(left.count==moved.count && std::abs(left.lines[0].a[0]-moved.lines[0].a[0])<1e-5f,"current placement and eye locomotion cancel for all diagnostic batches");
		}
		p::debug::sample_history<d::sample> history; d::sample selected;
		history.push(s);
		check(history.select(s.object,s.matrices,s.epoch,s.owner,s.reference,now,selected),"diagnostics select an exact current skeleton epoch");
		for (int wrong=0;wrong<7;++wrong)
		{
			auto owner=s.owner; if (wrong==3) ++owner.weapon; if (wrong==4) ++owner.rear_revision;
			check(!history.select(s.object+(wrong==0),s.matrices+(wrong==1),s.epoch+(wrong==2),owner,s.reference+(wrong==5),
				now+std::chrono::milliseconds(wrong==6 ? 151 : 0),selected),"foreign epoch, owner, reference or stale sample cannot paint the wrong weapon");
		}
		for (int i=0;i<140;++i) { auto copy=s; copy.epoch+=i+1; history.push(copy); }
		check(!history.select(s.object,s.matrices,s.epoch,s.owner,s.reference,now,selected),"bounded diagnostic history evicts old native epochs");
		auto invalid=s; invalid.valid=false; check(!d::geometry_for(invalid,{})[0].count,"invalid hand contact geometry is not fabricated");
		invalid=s; invalid.units=0; check(!d::geometry_for(invalid,{})[0].count,"invalid model scale suppresses diagnostic drawing");
		invalid=s; invalid.definition=&m9::physical; check(!d::geometry_for(invalid,{})[0].count,"non-catch weapons do not inherit an HK target");
		vr::spatial_lines::batch label;
		check(!vr::spatial_lines::label(label,{},{1,0,0},{0,1,0},1,std::string(10000,'A'),{1,1,1,1}) && !label.count,
			"oversized diagnostic labels fail before writing partial text");
		// Optional developer artifact uses the production projected batches.
		// The caller chooses a local output path; no machine path enters source.
		if (const auto path=std::getenv("VR_HK_SLAP_PREVIEW"))
		{
			std::ofstream out(path); out<<"["; bool first=true;
			for (const auto& batch:world)
			{
				const auto projected=vr::spatial_lines::project(batch,{},camera,.12f);
				for (size_t i=0;i<projected.count;++i)
				{
					const auto& line=projected.lines[i]; if (!first) out<<','; first=false;
					out<<'['<<line.a[0]/line.a[3]<<','<<line.a[1]/line.a[3]<<','<<line.b[0]/line.b[3]<<','<<line.b[1]/line.b[3];
					for (float c:line.color) out<<','<<c; out<<']';
				}
			}
			out<<']'; check(bool(out),"optional HK preview artifact wrote successfully");
		}
	}
}
