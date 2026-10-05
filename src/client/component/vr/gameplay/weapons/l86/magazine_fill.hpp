#pragma once
#include "../../magazine_fill.hpp"
namespace vr::gameplay::weapons::l86
{
// SEModel SHA256 980ea68a1ec2101c5762b447dff15274ed16ff37241f2b8df95d2fc894117d3c
inline constexpr std::array<std::array<unsigned,2>,11> fill_0_surfaces{{{9344,10998},{565,798},{8967,11792},{4580,4840},{1482,2482},{6474,9778},{741,864},{5355,6870},{3930,4064},{1882,3340},{1674,1936}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_0{{{5,0,9777}}};
inline constexpr std::array<scene_models::surface_face_range,4> fill_0_1{{{5,0,9777},{6,0,71},{6,216,407},{6,792,815}}};
inline constexpr std::array<scene_models::surface_face_range,4> fill_0_2{{{5,0,9777},{6,0,143},{6,216,599},{6,792,839}}};
inline constexpr std::array<scene_models::surface_face_range,2> fill_0_3{{{5,0,9777},{6,0,863}}};
inline constexpr std::array<magazine_fill_recipe,1> magazine_fills{{{"h2_viewmodel_sa80_lmg_base",16,fill_0_surfaces,{{fill_0_0,fill_0_1,fill_0_2,fill_0_3}},{{{-7.14681580f,-3.60646999f,-7.49295903f},{-7.14681580f,-3.60646999f,-7.49295903f},{-7.14681580f,-3.60646999f,-7.49295903f},{-7.14681580f,-3.60646999f,-7.49295903f}}},{{{-3.58376090f,3.60670202f,2.17569400f},{-3.58376090f,3.60670202f,2.25039505f},{-3.58376090f,3.60670202f,2.25039505f},{-3.58376090f,3.60670202f,2.25039505f}}}}}};
}
