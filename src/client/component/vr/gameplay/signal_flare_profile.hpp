#pragma once
#include "component/vr/gameplay/hands/pose_mirror.hpp"
#include "knife_profile.hpp"
namespace vr::gameplay::equipment::special::flare::authored
{
// Read-only H2 h2_dcwhitehouse_flare_idle DObj witness, 2026-09-29.
// Relative to j_flare in native inches; cap bind +Z = 4.970859.
inline constexpr hands::anchor right_attachment{{-3.003184786f,-0.881702169f,-1.281665293f},{0.611384740f,0.268528778f,-0.467779096f,0.579036883f}};
inline constexpr std::array<hands::joint_pose,15> fingers{{
{"j_index_ri_0",{0.595022059f,-0.379071759f,0.443984894f,0.552386426f}},
{"j_mid_ri_0",{0.546403493f,-0.449811513f,0.431012114f,0.559769044f}},
{"j_thumb_ri_0",{-0.008179005f,-0.364789348f,0.327343440f,0.871612361f}},
{"j_index_ri_1",{0.022827579f,-0.014588202f,0.465158056f,0.884813014f}},
{"j_mid_ri_1",{-0.017273375f,-0.012726671f,0.497326234f,0.867298264f}},
{"j_pinky_ri_0",{0.025605649f,0.108890518f,0.439591033f,0.891205324f}},
{"j_ring_ri_0",{-0.023652912f,0.008850922f,0.584796347f,0.810786922f}},
{"j_thumb_ri_1",{0.087587221f,0.102480624f,-0.404610761f,0.904497835f}},
{"j_index_ri_2",{0.004273134f,0.036133633f,0.389754735f,0.920199624f}},
{"j_mid_ri_2",{-0.029083756f,0.004333660f,0.577809717f,0.815641641f}},
{"j_pinky_ri_1",{0.037659010f,0.018921838f,0.536969000f,0.842548548f}},
{"j_ring_ri_1",{0.010376294f,0.009308126f,0.507765591f,0.861382491f}},
{"j_thumb_ri_2",{-0.008222811f,-0.071683181f,-0.324988623f,0.942961453f}},
{"j_pinky_ri_2",{-0.021026881f,-0.002686144f,0.412368359f,0.910770548f}},
{"j_ring_ri_2",{-0.001342839f,-0.002838752f,0.483937530f,0.875096912f}},
}};
inline hands::anchor attachment(unsigned hand,hands::quat mirror) noexcept
{
    auto result=right_attachment;
    if(hand==0){result=hands::pose_mirror::object_in_wrist({},result,mirror);
        result.rotation=hands::normalize(hands::multiply(result.rotation,{0,0,1,0}));}
    return result;
}
// Native h2_dcwhitehouse_flare_fire, frame 20: left hand grasp before cap
// separation (sound at frame 24). Static pose only, never animation playback.
inline constexpr hands::anchor left_cap_attachment{{3.005613883f,1.296834018f,1.196515168f},{-0.142163542f,0.559739217f,-0.557811260f,-0.596094065f}};
inline constexpr std::array<hands::joint_pose,15> cap_fingers{{
{"j_index_le_0",{0.532893097f,-0.275883509f,0.449058172f,0.662012080f}},
{"j_mid_le_0",{0.478450256f,-0.336581320f,0.468501100f,0.662046136f}},
{"j_thumb_le_0",{-0.114192446f,-0.313047680f,0.385070302f,0.860628897f}},
{"j_index_le_1",{0.000097277f,-0.083101258f,0.475901952f,0.875563535f}},
{"j_mid_le_1",{0.006591989f,0.020142189f,0.585130602f,0.810662086f}},
{"j_pinky_le_0",{-0.073916303f,0.071438565f,0.605613174f,0.789091626f}},
{"j_ring_le_0",{-0.077980852f,0.168335106f,0.571431048f,0.799405302f}},
{"j_thumb_le_1",{0.094271670f,0.060024183f,-0.445453861f,0.888302205f}},
{"j_index_le_2",{0.007507514f,0.035553875f,0.470684478f,0.881552994f}},
{"j_mid_le_2",{0.009698651f,-0.001375349f,0.274526561f,0.961529621f}},
{"j_pinky_le_1",{0.023682611f,0.014893188f,0.529410124f,0.847904622f}},
{"j_ring_le_1",{0.022650113f,0.099244687f,0.541695323f,0.834388184f}},
{"j_thumb_le_2",{-0.002197321f,-0.071998499f,-0.408243905f,0.910026539f}},
{"j_pinky_le_2",{-0.032349375f,0.009393907f,0.403848373f,0.914205537f}},
{"j_ring_le_2",{0.058448501f,0.067110010f,0.370585201f,0.924525082f}},
}};
inline hands::anchor cap_attachment(unsigned hand,hands::quat mirror) noexcept
{
    return hand==0?left_cap_attachment:hands::pose_mirror::object_in_wrist({},left_cap_attachment,mirror);
}
inline hands::anchor choose_cap_attachment(unsigned hand,hands::quat mirror,hands::quat wrist,hands::quat cap) noexcept
{
    using namespace hands;using namespace hands::pose_math;
    const auto base=cap_attachment(hand,mirror);auto selected=base;float best=-1;
    // The round cap admits four equivalent approaches around its axis. Pick
    // the closest anatomical wrist ONCE, then retain it through detachment.
    constexpr std::array<quat,4> turns{{{0,0,0,1},{0,0,.70710678f,.70710678f},{0,0,1,0},{0,0,-.70710678f,.70710678f}}};
    for(const auto turn:turns)
    {
        const auto candidate=compose(base,{{},turn});const auto desired=normalize(multiply(cap,conjugate(candidate.rotation)));
        float score{};for(unsigned i=0;i<4;++i)score+=wrist[i]*desired[i];score=std::abs(score);
        if(score>best){best=score;selected=candidate;}
    }
    return selected;
}
}
