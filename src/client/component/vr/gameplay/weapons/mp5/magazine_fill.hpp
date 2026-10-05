#pragma once
#include "../../magazine_fill.hpp"
namespace vr::gameplay::weapons::mp5
{
// SEModel SHA256 9d42162e1c5836e64f2beb14001cf8c199c8bdac7c66b64bbe359fe2df9ade4d
inline constexpr std::array<std::array<unsigned,2>,16> fill_0_surfaces{{{8637,9340},{1684,2686},{657,884},{657,972},{367,416},{416,504},{162,280},{1853,2290},{733,882},{3295,5418},{153,194},{6496,9316},{629,894},{168,224},{142,130},{382,504}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_0{{{1,0,2685}}};
inline constexpr std::array<scene_models::surface_face_range,4> fill_0_1{{{1,0,2685},{3,150,299},{3,450,463},{3,492,651}}};
inline constexpr std::array<scene_models::surface_face_range,3> fill_0_2{{{1,0,2685},{3,150,477},{3,492,811}}};
inline constexpr std::array<scene_models::surface_face_range,2> fill_0_3{{{1,0,2685},{3,0,971}}};
// SEModel SHA256 eace707ed68c4c20e992cba1c577c7d7c86442f65f0ba03958f6de19ca9b3750
inline constexpr std::array<std::array<unsigned,2>,16> fill_1_surfaces{{{8637,9340},{1684,2686},{657,884},{657,972},{367,416},{416,504},{162,280},{1853,2290},{733,882},{3295,5418},{153,194},{6496,9316},{629,894},{168,224},{142,130},{382,504}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_1_0{{{1,0,2685}}};
inline constexpr std::array<scene_models::surface_face_range,4> fill_1_1{{{1,0,2685},{3,150,299},{3,450,463},{3,492,651}}};
inline constexpr std::array<scene_models::surface_face_range,3> fill_1_2{{{1,0,2685},{3,150,477},{3,492,811}}};
inline constexpr std::array<scene_models::surface_face_range,2> fill_1_3{{{1,0,2685},{3,0,971}}};
inline constexpr std::array<magazine_fill_recipe,2> magazine_fills{{{"h2_viewmodel_mp5k_base",21,fill_0_surfaces,{{fill_0_0,fill_0_1,fill_0_2,fill_0_3}},{{{5.60265376f,-0.41381998f,-3.49607618f},{5.60265376f,-0.41381998f,-3.49607618f},{5.60265376f,-0.41381998f,-3.49607618f},{5.60265376f,-0.41381998f,-3.49607618f}}},{{{9.87403299f,0.41381998f,3.87341169f},{9.87403299f,0.41381998f,3.98939688f},{9.87403299f,0.41381998f,3.98939688f},{9.87403299f,0.41381998f,3.98939688f}}}},
{"h2_viewmodel_mp5k_base_arctic",21,fill_1_surfaces,{{fill_1_0,fill_1_1,fill_1_2,fill_1_3}},{{{5.60265376f,-0.41381998f,-3.49607618f},{5.60265376f,-0.41381998f,-3.49607618f},{5.60265376f,-0.41381998f,-3.49607618f},{5.60265376f,-0.41381998f,-3.49607618f}}},{{{9.87403299f,0.41381998f,3.87341169f},{9.87403299f,0.41381998f,3.98939688f},{9.87403299f,0.41381998f,3.98939688f},{9.87403299f,0.41381998f,3.98939688f}}}}}};
}
