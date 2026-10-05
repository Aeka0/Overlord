#pragma once
#include "hk_slap_debug.hpp"
#include "../spatial_line_label.hpp"

namespace vr::gameplay::weapons::physical_reload::hk_slap_debug
{
	inline std::array<spatial_lines::batch,3> geometry_for(const sample& s,hands::vec placement) noexcept
	{
		using namespace hands;
		std::array<spatial_lines::batch,3> out{};
		if (!s.definition || !s.definition->interaction.manual_catch || !s.valid || !valid(*s.definition->interaction.manual_catch) ||
			!spatial_panel::finite(placement) || !spatial_panel::finite(s.contact_model.position) ||
			!unit_quaternion(s.contact_model.rotation) || !std::isfinite(s.units) || s.units<=0 || s.units>10000) return out;
		const auto& p=*s.definition->interaction.manual_catch;
		const auto world=[&](vec point) { return add(placement,add(s.contact_model.position,rotate(s.contact_model.rotation,scale(point,s.units)))); };
		const auto line=[&](vec a,vec b,spatial_panel::vec4 color) { (void)out[0].add(world(a),world(b),color); };
		const auto marker=[&](vec at,float r,spatial_panel::vec4 color) {
			for (int axis=0;axis<3;++axis) { auto a=at,b=at; a[axis]-=r; b[axis]+=r; line(a,b,color); }
		};
		const auto circles=[&](float radius,int planes,spatial_panel::vec4 color) {
			for (int plane=0;plane<planes;++plane) for (int i=0;i<16;++i)
			{
				vec a{},b{}; const int x=plane%3,y=(plane+1)%3;
				a[x]=radius*std::cos(i*6.2831853f/16); a[y]=radius*std::sin(i*6.2831853f/16);
				b[x]=radius*std::cos((i+1)*6.2831853f/16); b[y]=radius*std::sin((i+1)*6.2831853f/16); line(a,b,color);
			}
		};
		circles(p.slap.radius,3,{0,1,1,.8f}); circles(p.slap.rearm_radius,2,{1,1,0,.4f});
		const auto hand=[&](const auto& points,spatial_panel::vec4 color,bool tips) {
			for (int f=0;f<5;++f)
			{
				line(points[20],points[3*f],color); line(points[3*f],points[3*f+1],color);
				line(points[3*f+1],points[3*f+2],color); line(points[3*f+2],points[15+f],color);
				if (tips) marker(points[15+f],.003f,color);
			}
			if (tips) marker(points[20],.004f,color);
		};
		hand(s.raw,{1,1,1,.9f},true); if (s.visual_valid) hand(s.visual,{.1f,.4f,1,.7f},false);
		// Allowed approach cone above the tab, derived from the SAME angle limit.
		const auto direction=unit(p.slap.direction);
		const auto perpendicular=[](vec axis) { return unit(cross(axis,std::abs(axis[0])<.9f ? vec{1,0,0} : vec{0,1,0})); };
		const auto u=perpendicular(direction),v=cross(direction,u);
		const float cone_length=.10f,axial=cone_length*p.slap.direction_cosine;
		const float radius=cone_length*std::sqrt(1-p.slap.direction_cosine*p.slap.direction_cosine);
		for (int i=0;i<8;++i)
		{
			const auto ring=[&](int n) { return add(scale(direction,-axial),add(scale(u,radius*std::cos(n*6.2831853f/8)),scale(v,radius*std::sin(n*6.2831853f/8)))); };
			line(ring(i),ring(i+1),{.3f,1,.3f,.35f}); if (i%2==0) line({},ring(i),{.3f,1,.3f,.35f});
		}
		const auto& last=s.trace.last_contact;
		const auto& shown=last.sequence ? last : s.trace.latest;
		if (shown.examined && shown.impact.sampled && shown.impact.point>=0)
		{
			const auto& i=shown.impact;
			const spatial_panel::vec4 color=shown.reason==slap_reason::accepted ? spatial_panel::vec4{0,1,0,1} : spatial_panel::vec4{1,.3f,0,1};
			line(i.previous,i.current,color); marker(i.current,.007f,color);
			const auto delta=sub(i.current,i.previous),tip=scale(unit(delta),.12f);
			const spatial_panel::vec4 arrow=i.direction_ok ? spatial_panel::vec4{0,1,0,.8f} : spatial_panel::vec4{1,0,1,.8f};
			line({},tip,arrow); const auto wing=scale(perpendicular(unit(delta)),.008f);
			line(tip,add(scale(tip,.8f),wing),arrow); line(tip,sub(scale(tip,.8f),wing),arrow);
		}
		// Labels sit above the receiver in its Y/Z plane. Each has its own fixed
		// line budget so geometry or a long verdict cannot silently clip the text.
		for (int row=0;row<2;++row)
		{
			const auto& report=row ? s.trace.last_contact : s.trace.latest;
			std::array<char,32> text{}; const std::string_view prefix=row ? "LAST " : "NOW ";
			const std::string_view reason=report.sequence ? name(report.reason) : "NO SAMPLE";
			std::copy(prefix.begin(),prefix.end(),text.begin()); std::copy(reason.begin(),reason.end(),text.begin()+prefix.size());
			const spatial_panel::vec4 color=report.reason==slap_reason::accepted ? spatial_panel::vec4{0,1,0,1} :
				row ? spatial_panel::vec4{1,.65f,.2f,1} : spatial_panel::vec4{1,1,1,1};
			(void)spatial_lines::label(out[1+row],world({0,.17f,.235f-row*.035f}),rotate(s.contact_model.rotation,{0,-1,0}),
				rotate(s.contact_model.rotation,{0,0,1}),.018f*s.units,{text.data(),prefix.size()+reason.size()},color);
		}
		return out;
	}
}
