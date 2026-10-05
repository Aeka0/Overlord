#pragma once
#include "component/vr/gameplay/weapons/m79/profile.hpp"
#include "component/vr/gameplay/weapons/ranger/profile.hpp"
#include "component/vr/gameplay/break_action_profiles.hpp"
#include "component/vr/gameplay/break_action_presenter.hpp"
namespace break_action_profile_tests
{
template<class Check> void run(Check&& check)
{
using namespace vr::gameplay;using namespace weapons;
{
// Source receiver hierarchy; M79 rounds are siblings, Ranger rounds are barrel children.
vr::gameplay::hands::rig r;r.count=11;r.gun=0;r.parent.fill(-1);
std::array<vr::gameplay::hands::bone_definition,11> bones{};
bones[0].name="j_gun";r.parent[0]=-1;bones[0].parent=-1;r.weapon_bones[0]=true;
bones[1].name="j_back_lock";r.parent[1]=0;bones[1].parent=0;r.weapon_bones[1]=true;
bones[2].name="j_back_switch";r.parent[2]=0;bones[2].parent=0;r.weapon_bones[2]=true;
bones[3].name="j_reload";r.parent[3]=0;bones[3].parent=0;r.weapon_bones[3]=true;
bones[4].name="j_trigger1";r.parent[4]=0;bones[4].parent=0;r.weapon_bones[4]=true;
bones[5].name="j_trigger2";r.parent[5]=0;bones[5].parent=0;r.weapon_bones[5]=true;
bones[6].name="tag_brass";r.parent[6]=0;bones[6].parent=0;r.weapon_bones[6]=true;
bones[7].name="j_le_bullet";r.parent[7]=3;bones[7].parent=3;r.weapon_bones[7]=true;
bones[8].name="j_pump";r.parent[8]=3;bones[8].parent=3;r.weapon_bones[8]=true;
bones[9].name="j_ri_bullet";r.parent[9]=3;bones[9].parent=3;r.weapon_bones[9]=true;
bones[10].name="tag_flash";r.parent[10]=3;bones[10].parent=3;r.weapon_bones[10]=true;
const auto& p=ranger::feed;const auto bound=break_action::bind_parts(r,bones,p);
check(bound.valid,"native break-action receiver hierarchy binds");
auto bad=r;bad.parent[bound.barrel]=bound.lock;check(!break_action::bind_parts(bad,bones,p).valid,"barrel with unexpected parent rejected");
bad=r;bad.parent[bound.rounds[0]]=bound.lock;check(!break_action::bind_parts(bad,bones,p).valid,"round with unexpected parent rejected");
auto missing=bones;missing[bound.rounds[0]].name="missing_round";check(!break_action::bind_parts(r,missing,p).valid,"missing chamber mesh prevents physical admission");
auto p_bad=p;p_bad.ammunition.capacity=3;check(!break_action::bind_parts(r,bones,p_bad).valid,"oversized chamber bank rejected before indexing");
bad=r;bad.count=257;check(!break_action::bind_parts(bad,bones,p).valid,"oversized native rig fails closed");
const std::array<vr::gameplay::hands::model_definition,1> models{{{"h2_viewmodel_sawed_off_double_barrel_base",0,r.count}}};
check(ranger::select(models,models[0],r,bones).value==&ranger::base,"exact reviewed break-action assembly selected");
auto wrong=models;--wrong[0].count;
check(!ranger::select(wrong,wrong[0],r,bones).value,"wrong receiver count is not silently admitted");
}
{
// Source receiver hierarchy; M79 rounds are siblings, Ranger rounds are barrel children.
vr::gameplay::hands::rig r;r.count=17;r.gun=0;r.parent.fill(-1);
std::array<vr::gameplay::hands::bone_definition,17> bones{};
bones[0].name="j_gun";r.parent[0]=-1;bones[0].parent=-1;r.weapon_bones[0]=true;
bones[1].name="j_brass_round";r.parent[1]=0;bones[1].parent=0;r.weapon_bones[1]=true;
bones[2].name="j_breech_lock";r.parent[2]=0;bones[2].parent=0;r.weapon_bones[2]=true;
bones[3].name="j_front_end_reload";r.parent[3]=0;bones[3].parent=0;r.weapon_bones[3]=true;
bones[4].name="j_grenade_round";r.parent[4]=0;bones[4].parent=0;r.weapon_bones[4]=true;
bones[5].name="j_strap_01";r.parent[5]=0;bones[5].parent=0;r.weapon_bones[5]=true;
bones[6].name="j_strap_02";r.parent[6]=0;bones[6].parent=0;r.weapon_bones[6]=true;
bones[7].name="j_strap_03";r.parent[7]=0;bones[7].parent=0;r.weapon_bones[7]=true;
bones[8].name="j_strap_04";r.parent[8]=0;bones[8].parent=0;r.weapon_bones[8]=true;
bones[9].name="j_strap_05";r.parent[9]=0;bones[9].parent=0;r.weapon_bones[9]=true;
bones[10].name="j_strap_end";r.parent[10]=0;bones[10].parent=0;r.weapon_bones[10]=true;
bones[11].name="j_strap_start";r.parent[11]=0;bones[11].parent=0;r.weapon_bones[11]=true;
bones[12].name="tag_brass";r.parent[12]=0;bones[12].parent=0;r.weapon_bones[12]=true;
bones[13].name="j_ladder_sight";r.parent[13]=3;bones[13].parent=3;r.weapon_bones[13]=true;
bones[14].name="tag_flash";r.parent[14]=3;bones[14].parent=3;r.weapon_bones[14]=true;
bones[15].name="tag_flash_silenced";r.parent[15]=3;bones[15].parent=3;r.weapon_bones[15]=true;
bones[16].name="j_sight";r.parent[16]=13;bones[16].parent=13;r.weapon_bones[16]=true;
const auto& p=m79::feed;const auto bound=break_action::bind_parts(r,bones,p);
check(bound.valid,"native break-action receiver hierarchy binds");
auto bad=r;bad.parent[bound.barrel]=bound.lock;check(!break_action::bind_parts(bad,bones,p).valid,"barrel with unexpected parent rejected");
bad=r;bad.parent[bound.rounds[0]]=bound.lock;check(!break_action::bind_parts(bad,bones,p).valid,"round with unexpected parent rejected");
auto missing=bones;missing[bound.rounds[0]].name="missing_round";check(!break_action::bind_parts(r,missing,p).valid,"missing chamber mesh prevents physical admission");
auto p_bad=p;p_bad.ammunition.capacity=3;check(!break_action::bind_parts(r,bones,p_bad).valid,"oversized chamber bank rejected before indexing");
bad=r;bad.count=257;check(!break_action::bind_parts(bad,bones,p).valid,"oversized native rig fails closed");
const std::array<vr::gameplay::hands::model_definition,1> models{{{"h2_viewmodel_m79_base",0,r.count}}};
check(m79::select(models,models[0],r,bones).value==&m79::base,"exact reviewed break-action assembly selected");
auto wrong=models;--wrong[0].count;
check(!m79::select(wrong,wrong[0],r,bones).value,"wrong receiver count is not silently admitted");
}
}
}
