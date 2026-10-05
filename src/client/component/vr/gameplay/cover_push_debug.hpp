#pragma once
#include "reload_debug.hpp"
#include "../spatial_line_label.hpp"

namespace vr::gameplay::weapons::physical_reload::cover_debug
{
	struct sample:debug::sample_identity
	{
		hands::anchor hinge{};
		belt_feed::cover_push_contact raw{},examined{};
		hands::vec visual{};
		float cover{},shown_cover{};
		std::uint64_t simulation_sequence{};
		const char* reason{"no simulation sample"};const char* decision{"unavailable"};
		bool trigger{},squeeze{},squeeze_active{},tracking{},available{},visual_valid{};
	};
	bool enabled()noexcept;
	void publish(const sample&)noexcept;
	inline const char* input_gate(const sample& s)noexcept
	{
		if(!s.tracking)return "NO TRACKING";
		if(!s.raw.valid)return "NO PALM SAMPLE";
		if(!s.available)return "HAND OCCUPIED";
		if(s.trigger)return "RELEASE TRIGGER";
		if(!s.squeeze_active)return "GRIP INPUT INACTIVE";
		if(s.squeeze)return "RELEASE GRIP";
		return "OPEN PALM";
	}
	inline std::array<spatial_lines::batch,3> geometry_for(const sample& s,hands::vec placement)noexcept
	{
		using namespace hands;std::array<spatial_lines::batch,3> out{};
		if(!s.definition || !s.definition->interaction.belt || !s.definition->interaction.belt->push ||
			!std::isfinite(s.units) || s.units<=0 || s.units>10000 || !spatial_panel::finite(placement) ||
			!spatial_panel::finite(s.hinge.position) || !unit_quaternion(s.hinge.rotation) || !std::isfinite(s.cover))return out;
		const auto& p=*s.definition->interaction.belt;const auto& patch=*p.push;
		const auto q=belt_feed::hinge_pose({},p.cover_axis,p.cover_angle,s.cover).rotation;
		const auto geometry=belt_feed::query_push(p,s.cover,s.raw);
		const auto extension=belt_feed::push_extension(geometry.along);
		const auto world=[&](vec v){return add(placement,add(s.hinge.position,rotate(s.hinge.rotation,scale(v,s.units))));};
		const auto line=[&](vec a,vec b,spatial_panel::vec4 color){(void)out[0].add(world(a),world(b),color);};
		const auto skin=[&](vec a,vec b,spatial_panel::vec4 color){line(rotate(q,a),rotate(q,b),color);};
		const auto rectangle=[&](float gap,float tolerance,spatial_panel::vec4 color){
			const float z=patch.surface+gap;
			if(tolerance>0)
			{
				// Rectangle + hand-axis segment, rounded by the unchanged palm
				// radius. A fixed eight-point hull matches capsule admission.
				std::array<vec,8> points{};size_t n=0;
				for(float x:{-patch.outer,-patch.inner})for(float y:{patch.side_low,patch.side_high})for(float sign:{-1.f,1.f})
					points[n++]=add(vec{x,y,z},scale(extension,sign));
				std::sort(points.begin(),points.end(),[](vec a,vec b){return a[0]<b[0] || (a[0]==b[0] && a[1]<b[1]);});
				const auto count=std::unique(points.begin(),points.end())-points.begin();
				std::array<vec,16> hull{};size_t used=0;
				const auto turn=[](vec a,vec b,vec c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);};
				for(std::ptrdiff_t i=0;i<count;++i){while(used>=2 && turn(hull[used-2],hull[used-1],points[i])<=0)--used;hull[used++]=points[i];}
				const auto lower=used;
				for(auto i=count-2;i>=0;--i){while(used>lower && turn(hull[used-2],hull[used-1],points[i])<=0)--used;hull[used++]=points[i];}
				if(used>1)--used;
				for(size_t k=0;k<used;++k)
				{
					const auto incoming=sub(hull[k],hull[(k+used-1)%used]),outgoing=sub(hull[(k+1)%used],hull[k]);
					const auto begin=std::atan2(-incoming[0],incoming[1]);auto end=std::atan2(-outgoing[0],outgoing[1]);if(end<begin)end+=6.283185307f;
					const auto arc=[&](vec at,float angle){return add(at,vec{tolerance*std::cos(angle),tolerance*std::sin(angle),0});};
					for(int i=0;i<4;++i)skin(arc(hull[k],begin+(end-begin)*i/4),arc(hull[k],begin+(end-begin)*(i+1)/4),color);
					skin(arc(hull[k],end),arc(hull[(k+1)%used],end),color);
				}
				return;
			}
			const std::array<vec,4> corners{{{-patch.outer-tolerance,patch.side_low-tolerance,z},{-patch.inner+tolerance,patch.side_low-tolerance,z},
				{-patch.inner+tolerance,patch.side_high+tolerance,z},{-patch.outer-tolerance,patch.side_high+tolerance,z}}};
			for(int i=0;i<4;++i)skin(corners[i],corners[(i+1)%4],color);
		};
		rectangle(0,0,{0,1,1,1});rectangle(belt_feed::push_contact_gap,belt_feed::push_palm_radius,{0,1,0,.8f});
		rectangle(belt_feed::push_rearm_gap,belt_feed::push_palm_radius,{1,1,0,.9f});
		for(float x:{-patch.inner,-patch.outer})for(float y:{patch.side_low,patch.side_high})
			skin({x,y,patch.surface},{x,y,patch.surface+belt_feed::push_rearm_gap},{.5f,.5f,.5f,.8f});
		const vec centre{-(patch.inner+patch.outer)*.5f,(patch.side_low+patch.side_high)*.5f,patch.surface};
		skin(add(centre,{0,0,.10f}),centre,{0,1,0,1});
		skin(centre,add(centre,{.012f,0,.02f}),{0,1,0,1});skin(centre,add(centre,{-.012f,0,.02f}),{0,1,0,1});
		const auto cross=[&](vec v,float radius,spatial_panel::vec4 color){for(int i=0;i<3;++i){auto a=v,b=v;a[i]-=radius;b[i]+=radius;line(a,b,color);}};
		if(s.raw.valid && finite_part_vec(s.raw.point) && finite_part_vec(s.raw.palm))
		{
			cross(s.raw.point,.006f,{1,1,1,1});
			// Two semicircles plus longitudinal edges show the actual capsule.
			const auto heading=length(extension)>1e-6f?std::atan2(extension[1],extension[0]):0.f;
			const auto ring=[&](vec centre,float a){return add(s.raw.point,rotate(q,add(centre,vec{belt_feed::push_palm_radius*std::cos(a),belt_feed::push_palm_radius*std::sin(a),0})));};
			for(int end=0;end<2;++end)
			{
				const auto centre=scale(extension,end?-1.f:1.f);const auto begin=heading-1.570796327f+end*3.141592654f;
				for(int i=0;i<8;++i)line(ring(centre,begin+i*3.141592654f/8),ring(centre,begin+(i+1)*3.141592654f/8),{1,1,1,.85f});
				line(ring(centre,begin+3.141592654f),ring(scale(centre,-1),begin+3.141592654f),{1,1,1,.85f});
			}
			line(s.raw.point,add(s.raw.point,scale(s.raw.palm,.065f)),geometry.facing?spatial_panel::vec4{0,1,0,1}:spatial_panel::vec4{1,0,1,1});
		}
		if(s.visual_valid && finite_part_vec(s.visual))cross(s.visual,.004f,{0,.4f,1,1});
		if(s.examined.valid && finite_part_vec(s.examined.point))cross(s.examined.point,.008f,{1,.6f,0,.8f});
		std::array<char,32> text{};const std::string_view reason=s.reason?s.reason:"NO SIMULATION SAMPLE";
		const auto count=std::min(reason.size(),text.size());
		for(size_t i=0;i<count;++i)text[i]=reason[i]>='a' && reason[i]<='z'?reason[i]-'a'+'A':reason[i];
		// Gun-local text stays readable while the lid rotates independently.
		const auto right=rotate(s.hinge.rotation,{0,-1,0}),up=rotate(s.hinge.rotation,{0,0,1});
		(void)spatial_lines::label(out[1],world({-.15f,.17f,.34f}),right,up,.014f*s.units,{text.data(),count},{1,1,1,1});
		(void)spatial_lines::label(out[2],world({-.15f,.17f,.31f}),right,up,.014f*s.units,input_gate(s),{1,.7f,.2f,1});
		return out;
	}
}
