#pragma once
#include "../../magazine_fill.hpp"
namespace vr::gameplay::weapons::pp2000
{
// SEModel SHA256 769aefc737b8671dc1a281672b59849b666613440ad2738670c5bad179c385cb
inline constexpr std::array<std::array<unsigned,2>,7> fill_0_surfaces{{{5554,7596},{2659,3742},{5286,6068},{727,846},{624,600},{46,42},{2442,3148}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_0{{{4,0,599}}};
inline constexpr std::array<scene_models::surface_face_range,5> fill_0_1{{{1,1005,1052},{1,2109,2636},{1,2781,2852},{1,2901,2924},{4,0,599}}};
inline constexpr std::array<scene_models::surface_face_range,5> fill_0_2{{{1,957,1052},{1,1581,2636},{1,2709,2852},{1,2877,2924},{4,0,599}}};
inline constexpr std::array<scene_models::surface_face_range,2> fill_0_3{{{1,909,2924},{4,0,599}}};
inline constexpr std::array<magazine_fill_recipe,1> magazine_fills{{{"h2_viewmodel_p2000_base",18,fill_0_surfaces,{{fill_0_0,fill_0_1,fill_0_2,fill_0_3}},{{{-0.73395400f,-0.61089495f,-3.93037383f},{-0.73395400f,-0.61089495f,-3.93037383f},{-0.73395400f,-0.61089495f,-3.93037383f},{-0.73395400f,-0.61089495f,-3.93037383f}}},{{{1.63308804f,0.61089401f,1.98482585f},{1.63308804f,0.61089401f,1.98482585f},{1.63308804f,0.61089401f,1.98482585f},{1.63308804f,0.61089401f,1.98482585f}}}}}};
}
