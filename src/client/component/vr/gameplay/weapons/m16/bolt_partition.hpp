#pragma once
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::m16
{
// Reviewed disconnected bolt surface; remainder retains the source bone.
// Fire and retained-stop travel are authored from the existing manual range,
// not native fire curves (the source fire clips leave this surface static).
inline constexpr std::array<std::array<unsigned,2>,10> bolt_surfaces{{{1157,1122},{5034,6346},{494,696},{540,896},{12398,13670},{2193,3290},{3364,4760},{8569,11578},{4556,6384},{722,910}}};
inline constexpr std::array<scene_models::surface_face_range,1> bolt_faces{{{8,4727,4860}}};
inline constexpr std::array<bolt_travel_sample,2> bolt_linkage{{{0,0},{handle_stroke_m,handle_stroke_m}}};
// h2_viewmodel_m16_base SHA256 58be751a7b9f437110a5ac2176cd92fe12702cb195af7b8a5d463de700a5cfeb
inline constexpr partitioned_bolt bolt_base{
    {"h2_viewmodel_m16_base",28,bolt_surfaces,bolt_faces,{2.891052051f,-0.505918968f,3.564830840f},{6.283493868f,-0.080578997f,4.376690031f}},
    {"j_reload",handle_rest,bolt_linkage,.078f,handle_stroke_m}};
}
