#pragma once
#include "../../magazine_fill.hpp"
namespace vr::gameplay::weapons::aug
{
// SEModel SHA256 2a88b8e7af7b04370ecddf182af06835a5944d6af226b04d149e82735de86bde
inline constexpr std::array<std::array<unsigned,2>,7> fill_0_surfaces{{{6143,7046},{3273,4576},{3068,4962},{2383,3420},{12115,17618},{1678,2864},{497,464}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_0{{{3,0,2411}}};
inline constexpr std::array<scene_models::surface_face_range,2> fill_0_1{{{3,0,2735},{3,3384,3395}}};
inline constexpr std::array<scene_models::surface_face_range,2> fill_0_2{{{3,0,3059},{3,3384,3407}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_3{{{3,0,3419}}};
inline constexpr std::array<magazine_fill_recipe,1> magazine_fills{{{"h2_viewmodel_steyr_base_arctic",18,fill_0_surfaces,{{fill_0_0,fill_0_1,fill_0_2,fill_0_3}},{{{-7.57189097f,-0.76100596f,-5.14841080f},{-7.57189097f,-0.76100596f,-5.14841080f},{-7.57189097f,-0.76100596f,-5.14841080f},{-7.57189097f,-0.76100596f,-5.14841080f}}},{{{-3.13914892f,0.80469096f,3.07997381f},{-3.13914892f,0.80469096f,3.17333289f},{-3.13914892f,0.80469096f,3.17333289f},{-3.13914892f,0.80469096f,3.17333289f}}}}}};
}
