#pragma once
#include "component/vr/screen_scope_layout.hpp"

template<class Check> void screen_scope_tests(Check check)
{
	using namespace vr;
	using axis=std::array<spatial_panel::vec3,3>;
	const axis identity{{{1,0,0},{0,1,0},{0,0,1}}};
	const auto yaw=[](float degrees){const float r=degrees*.017453292519943295f,c=std::cos(r),s=std::sin(r);return axis{{{c,s,0},{-s,c,0},{0,0,1}}};};
	const engine_stereo_bridge::eye_projection projection{-1,1,-1,1};
	const auto center=[](const spatial_panel::projected_quad& q){spatial_panel::vec4 c{};for(auto& p:q)for(unsigned i=0;i<4;++i)c[i]+=p[i]*.25f;return c;};
	screen_scope::anchored_plane plane;
	spatial_panel::projected_quad left{},right{},turned{},moved{};
	check(plane.update(1,1,{},identity,.1f) && plane.project(projection,.032f,1920,1080,left) &&
		plane.project(projection,-.032f,1920,1080,right),"one mounted plane projects through both physical eye origins");
	const auto l=center(left),r=center(right);
	check(std::abs((left[1][0]-left[0][0])/(left[0][1]-left[2][1])-1920.f/1080)<.00001f,
		"physical scope plane retains the authored HUD aspect ratio");
	check(std::abs(l[0]/l[3]-.016f)<.00001f && std::abs(r[0]/r[3]+.016f)<.00001f,
		"two-meter screen has finite binocular disparity rather than independent centered eye rectangles");
	check(plane.update(1,1,{},yaw(20),.1f) && plane.project(projection,0,1920,1080,turned),"head yaw changes only the viewer of the anchored plane");
	const auto t=center(turned);
	check(t[0]/t[3]>.36f && std::abs(turned[0][3]-turned[1][3])>.7f,
		"turned head sees shifted and foreshortened geometry with perspective-correct depth");
	check(plane.update(1,1,{.3f,0,0},identity,.1f) && plane.project(projection,0,1920,1080,moved) &&
		std::abs(center(moved)[3]-1.97f)<.00001f,"physical translation moves the viewer at existing Scaling gain");
	check(plane.update(1,1,{30,0,0},identity,.1f) && plane.project(projection,0,1920,1080,moved) &&
		std::abs(center(moved)[3]-1.95f)<.00001f,"large physical travel retains the five-centimeter comfort bound");
	check(plane.update(1,2,{30,0,0},yaw(70),.1f) && plane.project(projection,0,1920,1080,moved) &&
		std::abs(center(moved)[0])<.00001f && std::abs(center(moved)[3]-2)<.00001f,
		"recenter reanchors only the viewing plane in front of the current head");
	check(!plane.update(0,2,{},identity,.1f) && !plane.project(projection,0,1920,1080,moved),"dismount discards the viewing plane");
	check(plane.update(2,2,{},identity,.1f) && plane.update(2,2,{},yaw(180),.1f) && plane.project(projection,0,1920,1080,moved) &&
		std::all_of(moved.begin(),moved.end(),[](auto p){return p[3]<0;}),"looking away lets homogeneous clipping hide the whole plane");
	check(plane.update(2,2,{},yaw(65),.1f) && plane.project(projection,0,1920,1080,moved) && moved[0][3]*moved[1][3]<0,
		"a plane crossing the eye plane remains projectable instead of popping out as a whole");
	check(!plane.project(projection,0,0,1080,moved),"invalid canvas dimensions are rejected");
}
