#pragma once
#include "../../magazine_fill.hpp"
namespace vr::gameplay::weapons::fal
{
// SEModel SHA256 7dc28d5381843b0dfe3a643bd7b60a222fcc5720d215942bd4c1073bcf990144
inline constexpr std::array<std::array<unsigned,2>,11> fill_0_surfaces{{{1113,1508},{5406,7964},{6387,7812},{726,704},{171,288},{94,92},{2680,4364},{9677,11440},{830,1050},{1407,2460},{3394,5520}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_0{{{1,2974,5947}}};
inline constexpr std::array<scene_models::surface_face_range,4> fill_0_1{{{1,2974,5947},{1,6988,7019},{1,7340,7627},{1,7932,7947}}};
inline constexpr std::array<scene_models::surface_face_range,4> fill_0_2{{{1,2974,5947},{1,6988,7051},{1,7340,7915},{1,7932,7963}}};
inline constexpr std::array<scene_models::surface_face_range,2> fill_0_3{{{1,2974,5947},{1,6956,7963}}};
inline constexpr std::array<magazine_fill_recipe,1> magazine_fills{{{"h2_viewmodel_fn_fal_base",21,fill_0_surfaces,{{fill_0_0,fill_0_1,fill_0_2,fill_0_3}},{{{5.47028564f,-0.79360600f,-6.50701373f},{5.47028564f,-0.79360600f,-6.50701373f},{5.47028564f,-0.79360600f,-6.50701373f},{5.47028564f,-0.79360600f,-6.50701373f}}},{{{10.02312232f,0.82540700f,3.09727492f},{10.02312232f,0.82540700f,3.09727492f},{10.02312232f,0.82540700f,3.09727492f},{10.02312232f,0.82540700f,3.09727492f}}}}}};
}
