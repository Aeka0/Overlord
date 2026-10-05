#pragma once
#include "../../magazine_fill.hpp"
namespace vr::gameplay::weapons::m93r
{
// SEModel SHA256 5b45c88a2a6826a27aeb4d08dc656cb931fb10023b83baa2fb68a01a7d4507c5
inline constexpr std::array<std::array<unsigned,2>,7> fill_0_surfaces{{{5007,5926},{1890,2038},{48,42},{3029,3350},{402,666},{895,1182},{3647,4226}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_0{{{5,0,563}}};
inline constexpr std::array<scene_models::surface_face_range,4> fill_0_1{{{5,0,635},{5,770,937},{5,1026,1035},{5,1122,1181}}};
inline constexpr std::array<scene_models::surface_face_range,5> fill_0_2{{{5,0,707},{5,770,985},{5,1026,1045},{5,1056,1091},{5,1122,1181}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_3{{{5,0,1181}}};
inline constexpr std::array<magazine_fill_recipe,1> magazine_fills{{{"h2_viewmodel_beretta_393_base",14,fill_0_surfaces,{{fill_0_0,fill_0_1,fill_0_2,fill_0_3}},{{{-1.56472095f,-0.50399501f,-4.36081135f},{-1.56472095f,-0.50399501f,-4.36081135f},{-1.56472095f,-0.50399501f,-4.36081135f},{-1.56472095f,-0.50399501f,-4.36081135f}}},{{{1.11891904f,0.64294601f,0.88513095f},{1.13244601f,0.64294601f,1.01851704f},{1.13244601f,0.64294601f,1.01851704f},{1.13244601f,0.64294601f,1.01851704f}}}}}};
}
