#pragma once
#include "poses.hpp"
#include "reload_poses.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::m1014
{
	inline constexpr std::array<std::string_view,4> names{"m1014","m1014_arctic","m1014_reflex","m1014_eotech"};
	inline sound_reference sound(tube::effect e)noexcept
	{
		switch(e){
		case tube::effect::draw:return {"weap_m4benelli_start_plr"};
		case tube::effect::load_tube:return {"weap_m4benelli_loop_plr"};
		// The same recording serves both manual directions and port loading.
		case tube::effect::load_port:case tube::effect::rack_open:case tube::effect::rack_close:return {"wpn_h2_m1014_close_chamber_plr"};
		default:return {};
		}
	}
	inline constexpr tube::tuning interaction{
		{.18f,part_grip_capture::radius_m,.11033303f,.10433303f,.10733303f,
		.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,2},.035f,.035f,.35f,{.015f,.04f}};
	inline const tube_profile feed=[] {
		tube_profile p{"m1014","h2_viewmodel_benelli_m4_base",names,{7},interaction,
			bolt_rest,lifter_rest,lifter_loaded,shell_in_wrist,shell_center,port_center,tube_center,
			port_forward,tube_forward,rack_low,rack_high,rack_grips,shell_fingers,sound};
		p.bolt_return_seconds=.10f;return p;
	}();
	inline const tube_profile arctic_feed=[] {auto p=feed;p.receiver="h2_viewmodel_benelli_m4_base_arctic";return p;}();
	inline bool suppress_equip(std::string_view name)noexcept{return base_equip_action(name,"h2_wpn_sho_m1014_");}
	inline const std::array<profile,2> assemblies=[] {
		std::array<profile,2> out{};size_t i{};
		for(auto* f:{&feed,&arctic_feed})
		{out[i]={"m1014",f->receiver,"support",aim_rule::two_hand,wrists,1,.10f,.22f,.10f,idle_fingers,equip_rest,suppress_equip,nullptr,{part_visibility::rigid_groups}};out[i++].tube=f;}
		return out;
	}();
	inline int receiver_skin(std::string_view name)noexcept
	{for(size_t i=0;i<assemblies.size();++i)if(assemblies[i].receiver==name)return int(i);return -1;}
	inline profile_match select(std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones)noexcept
	{
		const int skin=receiver_skin(receiver.name);if(skin<0 || receiver.count!=12)return {};
		const auto bound=bind_attachment_set(rifle_attachments::common,models,receiver,rig,bones);
		if(!bound.valid)return {nullptr,"M1014 attachment topology rejected"};
		return {&assemblies[skin],"M1014 individual-shell tube feed",{},bound.muzzle};
	}
}
