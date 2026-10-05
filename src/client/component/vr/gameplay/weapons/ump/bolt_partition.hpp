#pragma once
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::ump
{
// Reviewed disconnected bolt surface; remainder retains the source bone.
// Fire and retained-stop travel are authored from the existing manual range,
// not native fire curves (the source fire clips leave this surface static).
inline constexpr std::array<std::array<unsigned,2>,6> bolt_surfaces{{{18366,25984},{2210,2988},{2108,2288},{418,384},{594,970},{4714,6718}}};
inline constexpr std::array<scene_models::surface_face_range,6> bolt_faces{{{2,0,13},{2,16,119},{2,384,389},{2,1440,1485},{2,1678,1723},{2,2084,2095}}};
inline constexpr std::array<bolt_travel_sample,2> bolt_linkage{{{0,0},{.085f,.085f}}};
// h2_viewmodel_ump45_base SHA256 f65f1857e9e152f906e2de28306b4d96ab9b3e64f8c5aa482845fe6ba4dc3cd4
inline constexpr partitioned_bolt bolt_base{
    {"h2_viewmodel_ump45_base",14,bolt_surfaces,bolt_faces,{4.583424846f,-0.614606020f,3.401101856f},{7.367243729f,-0.333424980f,4.625552095f}},
    {"j_gun",hands::anchor{{},{0,0,0,1}},bolt_linkage,.079f,.085f}};
// h2_viewmodel_ump45_base_arctic SHA256 9e27bf537c6c8baab8be34716640264bfbe2b89fa053be831b8ffe10fac9d956
inline constexpr partitioned_bolt bolt_arctic{
    {"h2_viewmodel_ump45_base_arctic",14,bolt_surfaces,bolt_faces,{4.583424846f,-0.614606020f,3.401101856f},{7.367243729f,-0.333424980f,4.625552095f}},
    {"j_gun",hands::anchor{{},{0,0,0,1}},bolt_linkage,.079f,.085f}};
// h2_viewmodel_ump45_base_digital SHA256 1c499ecc86ed48be598abef3e803eeea54ece16f1b4fde048bba7e0aab5491f5
inline constexpr partitioned_bolt bolt_digital{
    {"h2_viewmodel_ump45_base_digital",14,bolt_surfaces,bolt_faces,{4.583424846f,-0.614606020f,3.401101856f},{7.367243729f,-0.333424980f,4.625552095f}},
    {"j_gun",hands::anchor{{},{0,0,0,1}},bolt_linkage,.079f,.085f}};
}
