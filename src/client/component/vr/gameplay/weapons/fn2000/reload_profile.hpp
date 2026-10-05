#pragma once
#include "../../magazine_grasp_profile.hpp"
#include "reload_poses.hpp"
#include "folding_handle.hpp"

namespace vr::gameplay::weapons::fn2000
{
inline bool native_family(std::string_view name) noexcept { return native_weapon_family(name,"fn2000"); }
inline constexpr mechanics::rules reload_rules{30,mechanics::magazine_release::physical_pull,false,false,true};
// The recessed release ahead of the magazine is pressed upward by a spare.
inline constexpr physical_reload::magazine_manipulation manual_magazine{.05f,.05f,.10f,{0,0,-1},.025f,.06f,.15f,.012f,{0,0,1},true};
// Exported j_reload does not reciprocate in fire; it returns independently.
// Expand capture by 3 cm in every direction without shifting the handle or
// either authored hand pose. Candidate arbitration uses this same radius.
inline constexpr physical_reload::profile reload_interaction{.18f,part_grip_capture::radius_m+.03f,action_stroke_m,0,action_stroke_m*.95f,.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,2,physical_reload::action_motion::charging_handle,&manual_magazine};
inline const char* sound_key(mechanics::effect kind) noexcept
{
using enum mechanics::effect;
switch(kind) {
case magazine_out: case magazine_take: return "weap_fn2000_clipout_plr";
case magazine_in: return "weap_fn2000_clipin_plr";
case action_close: return "weap_fn2000_chamber_plr";
default: return nullptr;
}
}
inline const reload_profile physical=with_split_sounds([] {
reload_profile p{
		.id="fn2000",
		.native_name="fn2000",
		.ammunition=reload_rules,
		.interaction=reload_interaction,
		.magazine_bone="tag_clip",
		.slide_bone="j_reload",
		.bullets_bone="j_bullets",
		.magazine_rest=magazine_rest,
		.slide_rest=action_rest,
		.rigid_in_magazine={},
		.magazine_in_wrist=magazine_in_wrist,
		.magazine_top=magazine_top,
		.well=magazine_well,
		.slide_grab_low=action_grab_low,
		.slide_grab_high=action_grab_high,
		.magazine_fingers=magazine_fingers,
		.slide_grips=folding_grips,
		.sound_key=sound_key,
		.native_family=native_family,
		.magazine_contacts=&contacts,
		.rigid_magazine_source="h2_viewmodel_fn2000_base"
		};
// Actual handle is on +Y. Extend only outward and downward by 1 cm;
// leave its centre/mesh/hand anchors and the other four faces untouched.
p.slide_grab_high[1]+=1.f/2.54f;p.slide_grab_low[2]-=1.f/2.54f;
p.handle_fold=&handle_fold;return with_controller_magazine(p);
}(),"weap_fn2000_chamber_plr",false,true);
inline const std::array<const reload_profile*,1> skins{&physical};
static_assert(folding_grips.size()==reload_interaction.slide_pose_count);
}
