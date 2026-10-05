#pragma once
#include "../../magazine_fill.hpp"
namespace vr::gameplay::weapons::tmp
{
// SEModel SHA256 19f45105a81c07afec24ed3508a30ac1a957d7a40a43991d8e0990d45a761785
inline constexpr std::array<std::array<unsigned,2>,6> fill_0_surfaces{{{1424,1612},{1503,1676},{8609,10000},{1909,2340},{5595,6972},{388,416}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_0{{{1,0,973}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_1{{{1,0,1207}}};
inline constexpr std::array<scene_models::surface_face_range,2> fill_0_2{{{1,0,1207},{1,1442,1675}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_3{{{1,0,1675}}};
inline constexpr std::array<magazine_fill_recipe,1> magazine_fills{{{"h2_viewmodel_mp9_base",14,fill_0_surfaces,{{fill_0_0,fill_0_1,fill_0_2,fill_0_3}},{{{-0.86707900f,-0.52932102f,-6.25446387f},{-0.86707900f,-0.52932102f,-6.25446387f},{-0.86707900f,-0.52932102f,-6.25446387f},{-0.86707900f,-0.52932102f,-6.25446387f}}},{{{0.82165397f,0.52247798f,2.88362597f},{0.82165397f,0.52247798f,2.88362597f},{0.82165397f,0.52247798f,2.88362597f},{0.82165397f,0.52247798f,2.88362597f}}}}}};
}
