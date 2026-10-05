#pragma once
#include "reload_well_debug.hpp"
#include "../spatial_lines.hpp"

namespace vr::gameplay::weapons::physical_reload::well_debug
{
	// Use the very same profile and coordinate transform as sweep_well. Outer
	// volume shows the active withdrawal or retention margin, never extra capture.
	inline spatial_lines::batch geometry_for(const sample& s, hands::vec placement) noexcept
	{
		spatial_lines::batch lines;
		if (!s.definition || !valid(s.definition->interaction) || !spatial_panel::finite(placement) ||
			!spatial_panel::finite(s.well_model.position) || !std::isfinite(s.units) || s.units<=0 || s.units>10000) return lines;
		const auto& p=s.definition->interaction;
		const auto world = [&](hands::vec point) {
			return hands::add(placement,hands::add(s.well_model.position,
				hands::rotate(s.well_model.rotation,hands::scale(point,s.units))));
		};
		const auto line = [&](hands::vec a, hands::vec b, spatial_panel::vec4 color) { (void)lines.add(world(a),world(b),color); };
		const auto cylinder = [&](float radius,float below,float depth,spatial_panel::vec4 color) {
			constexpr unsigned sides=24;
			for (unsigned n=0;n<sides;++n)
			{
				const float a=n*6.28318530718f/sides,b=(n+1)*6.28318530718f/sides;
				const float ax=radius*std::cos(a),ay=radius*std::sin(a),bx=radius*std::cos(b),by=radius*std::sin(b);
				line({ax,ay,-below},{bx,by,-below},color); line({ax,ay,depth},{bx,by,depth},color);
				if (n%6==0) line({ax,ay,-below},{ax,ay,depth},color);
			}
		};
		cylinder(p.well_radius,p.well_capture_below,p.well_contact_depth,{0,1,1,.85f});
		const float margin=s.requires_withdrawal?p.well_withdraw_margin:p.well_release_margin;
		cylinder(p.well_radius+margin,p.well_capture_below+margin,
			p.well_contact_depth+margin,{1,1,0,.35f});
		// Actual mouth plane and rail direction, independent of occupied status.
		line({-p.well_radius,0,0},{p.well_radius,0,0},{0,1,1,.7f});
		line({0,-p.well_radius,0},{0,p.well_radius,0},{0,1,1,.7f});
		const auto cross = [&](hands::vec point,float radius,spatial_panel::vec4 color) {
			for (unsigned axis=0;axis<3;++axis) { auto a=point,b=point; a[axis]-=radius; b[axis]+=radius; line(a,b,color); }
		};
		if (s.held)
		{
			const bool inside=sweep_well(s.raw_tip,s.raw_tip,p.well_radius,p.well_contact_depth,p.well_capture_below);
			const bool aligned=s.alignment>=p.insertion_cosine;
			// Green means current GEOMETRY only, never promises a successful commit.
			cross(s.raw_tip,.009f,!aligned ? spatial_panel::vec4{1,0,1,1} :
				inside ? spatial_panel::vec4{0,1,0,1} : spatial_panel::vec4{1,1,1,1});
			cross(s.visual_tip,.004f,{0,.4f,1,1});
			if (s.examined)
			{
				const spatial_panel::vec4 color=s.requires_withdrawal ? spatial_panel::vec4{1,0,0,1} :
					s.well_contact ? spatial_panel::vec4{0,1,0,.65f} : spatial_panel::vec4{1,.4f,0,.85f};
				// Diamond = last consumed simulation point. Never draw its decision
				// color on today's raw tip: these can belong to different inputs.
				const auto t=s.examined_tip;
				const hands::vec a{t[0]-.013f,t[1],t[2]},b{t[0],t[1],t[2]+.013f},
					c{t[0]+.013f,t[1],t[2]},d{t[0],t[1],t[2]-.013f};
				line(a,b,color); line(b,c,color); line(c,d,color); line(d,a,color);
			}
		}
		// Red short bar inside the mouth = old magazine still occupies the well.
		if (s.occupied) line({-p.well_radius,0,.005f},{p.well_radius,0,.005f},{1,0,0,1});
		return lines;
	}
}
