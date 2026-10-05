#pragma once
#include "component/vr/damage_screen.hpp"
#include "narrative_ui_tests.hpp"
#include <limits>

namespace damage_screen_tests
{
	template<class Check> void run(Check& check)
	{
		using namespace vr::damage_screen;
		std::array<vr::engine_stereo_bridge::eye_projection,2> eyes{{{-1.4f,.8f,-1,1.2f},{-.8f,1.4f,-1.1f,1}}};
		std::array<vr::spatial_panel::projected_quad,2> canvas{};
		check(shared_canvas(eyes,canvas), "asymmetric eye projections form one damage canvas");
		const auto uv=[&](unsigned eye,float x,float y) {
			const auto& p=eyes[eye]; const auto& c=canvas[eye];
			const float ndc_x=2*(x-p.tan_left)/(p.tan_right-p.tan_left)-1;
			const float ndc_y=2*(y-p.tan_down)/(p.tan_up-p.tan_down)-1;
			return std::array<float,2>{(ndc_x-c[0][0])/(c[1][0]-c[0][0]),
				(c[0][1]-ndc_y)/(c[0][1]-c[2][1])};
		};
		for (float x : {-.7f,0.f,.7f}) for (float y : {-.9f,0.f,.9f})
		{
			const auto a=uv(0,x,y),b=uv(1,x,y);
			check(std::abs(a[0]-b[0])<1e-6f && std::abs(a[1]-b[1])<1e-6f,
				"same binocular view direction samples identical blood UV");
		}
		check(uv(0,.8f,0)[0]<.8f && uv(1,-.8f,0)[0]>.2f,
			"nasal screen edges do not repeat the blood border");
		check(std::abs(uv(0,-1.4f,0)[0])<1e-6f && std::abs(uv(1,1.4f,0)[0]-1)<1e-6f,
			"outer binocular limits retain original blood edges");
		std::swap(eyes[0],eyes[1]);
		std::array<vr::spatial_panel::projected_quad,2> swapped{};
		check(shared_canvas(eyes,swapped) && swapped[0]==canvas[1] && swapped[1]==canvas[0],
			"swapped eye projections retain shared mapping");
		eyes[0]=eyes[1]={-1,1,-1,1};
		check(shared_canvas(eyes,canvas) && canvas[0]==canvas[1] && canvas[0][0][0]==-1 &&
			canvas[0][1][0]==1, "symmetric FOV remains full coverage");
		eyes[0].tan_up=std::numeric_limits<float>::infinity();
		check(!shared_canvas(eyes,canvas), "invalid FOV rejected");
		auto draw = narrative_ui_tests::ending_fade();
		for (auto name : {"h1_fullscreen_lit_bloodsplat_01", "overlay_low_health",
			"overlay_low_health_alt", "h1_screen_blood"})
			check(command(draw.data(),draw.size(),name), "native damage material accepted");
		check(!command(draw.data(),draw.size(),"hit_direction") &&
			!command(draw.data(),draw.size(),"white"), "damage excludes direction warnings and fades");
		draw[48]=6; draw[49]=draw[50]=0; draw[51]=255;
		const float padding=std::numeric_limits<float>::quiet_NaN();
		std::memcpy(draw.data()+52,&padding,4);
		check(command(draw.data(),draw.size(),"h1_fullscreen_lit_bloodsplat_01"),
			"fading blood retains red intensity and ignores StretchPic padding");
		draw[3]=64;
		check(!command(draw.data(),draw.size(),"overlay_low_health"), "blur pass excluded");
		draw[3]=0;
		check(!command(draw.data(),55,"overlay_low_health") && !command(nullptr,56,"overlay_low_health"),
			"incomplete damage commands rejected");
		std::memcpy(draw.data()+16,&padding,4);
		check(!command(draw.data(),draw.size(),"overlay_low_health"), "nonfinite damage geometry rejected");
		check(current(100,350) && !current(100,351) && !current(100,99) && !current(0,1),
			"damage expires without native publication and rejects clock reversal");
		check(static_cast<unsigned>(vr::eye_composition::layer::damage) <
			static_cast<unsigned>(vr::eye_composition::layer::narrative), "native blackout covers blood");
	}
}
