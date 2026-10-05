#pragma once
#include "reload_poses.hpp"

namespace vr::gameplay::weapons::mp5
{
// Reviewed disconnected bolt surface; remainder retains the source bone.
// Fire and retained-stop travel are authored from the existing manual range,
// not native fire curves (the source fire clips leave this surface static).
inline constexpr std::array<std::array<unsigned,2>,16> bolt_surfaces{{{8637,9340},{1684,2686},{657,884},{657,972},{367,416},{416,504},{162,280},{1853,2290},{733,882},{3295,5418},{153,194},{6496,9316},{629,894},{168,224},{142,130},{382,504}}};
inline constexpr std::array<scene_models::surface_face_range,2> bolt_faces{{{11,1772,1819},{11,2603,2629}}};
inline constexpr std::array<bolt_travel_sample,2> bolt_linkage{{{0,0},{.065f,.065f}}};
// h2_viewmodel_mp5k_base SHA256 9d42162e1c5836e64f2beb14001cf8c199c8bdac7c66b64bbe359fe2df9ade4d
inline constexpr partitioned_bolt bolt_base{
    {"h2_viewmodel_mp5k_base",21,bolt_surfaces,bolt_faces,{4.634031536f,-0.843228975f,5.032033995f},{6.676277401f,-0.426220002f,5.946207797f}},
    {"j_gun",hands::anchor{{},{0,0,0,1}},bolt_linkage,.060f,.065f}};
// h2_viewmodel_mp5k_base_arctic SHA256 eace707ed68c4c20e992cba1c577c7d7c86442f65f0ba03958f6de19ca9b3750
inline constexpr partitioned_bolt bolt_arctic{
    {"h2_viewmodel_mp5k_base_arctic",21,bolt_surfaces,bolt_faces,{4.634031536f,-0.843228975f,5.032033995f},{6.676277401f,-0.426220002f,5.946207797f}},
    {"j_gun",hands::anchor{{},{0,0,0,1}},bolt_linkage,.060f,.065f}};
}
