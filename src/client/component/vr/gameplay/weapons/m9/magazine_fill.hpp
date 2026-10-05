#pragma once
#include "../../magazine_fill.hpp"
namespace vr::gameplay::weapons::m9
{
// SEModel SHA256 3114bd3163e1cc837f0f43f4440dab2c392fdd1df128fa4c76d5775245304316
inline constexpr std::array<std::array<unsigned,2>,5> fill_0_surfaces{{{3647,4226},{402,666},{895,1182},{48,42},{3029,3350}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_0{{{2,0,563}}};
inline constexpr std::array<scene_models::surface_face_range,4> fill_0_1{{{2,0,635},{2,770,937},{2,1026,1035},{2,1122,1181}}};
inline constexpr std::array<scene_models::surface_face_range,5> fill_0_2{{{2,0,707},{2,770,985},{2,1026,1045},{2,1056,1091},{2,1122,1181}}};
inline constexpr std::array<scene_models::surface_face_range,1> fill_0_3{{{2,0,1181}}};
inline constexpr std::array<magazine_fill_recipe,1> magazine_fills{{{"wpn_h1_pst_m9_vm",9,fill_0_surfaces,{{fill_0_0,fill_0_1,fill_0_2,fill_0_3}},{{{-1.14563000f,-0.57346600f,-2.52516195f},{-1.14563000f,-0.57346600f,-2.52516195f},{-1.14563000f,-0.57346600f,-2.52516195f},{-1.14563000f,-0.57346600f,-2.52516195f}}},{{{1.53801000f,0.57347398f,2.72077895f},{1.55153697f,0.57347398f,2.85416494f},{1.55153697f,0.57347398f,2.85416494f},{1.55153697f,0.57347398f,2.85416494f}}}}}};
}
