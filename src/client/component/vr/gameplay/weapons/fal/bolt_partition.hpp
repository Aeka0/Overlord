#pragma once
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::fal
{
// Reviewed disconnected bolt surface; remainder retains the source bone.
// Fire and retained-stop travel are authored from the existing manual range,
// not native fire curves (the source fire clips leave this surface static).
inline constexpr std::array<std::array<unsigned,2>,11> bolt_surfaces{{{1113,1508},{5406,7964},{6387,7812},{726,704},{171,288},{94,92},{2680,4364},{9677,11440},{830,1050},{1407,2460},{3394,5520}}};
inline constexpr std::array<scene_models::surface_face_range,3> bolt_faces{{{8,12,41},{8,179,208},{8,946,1009}}};
inline constexpr std::array<bolt_travel_sample,2> bolt_linkage{{{0,0},{.144f,.144f}}};
// h2_viewmodel_fn_fal_base SHA256 7dc28d5381843b0dfe3a643bd7b60a222fcc5720d215942bd4c1073bcf990144
inline constexpr partitioned_bolt bolt_base{
    {"h2_viewmodel_fn_fal_base",21,bolt_surfaces,bolt_faces,{4.326911986f,-0.670069972f,3.100836934f},{9.152540823f,0.685458014f,4.451799768f}},
    {"j_bolt",action_rest,bolt_linkage,.135f,.144f}};
}
