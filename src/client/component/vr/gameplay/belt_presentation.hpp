#pragma once
#include "belt_gesture.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"
#include "physical_reload_rig.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"

namespace vr::gameplay::weapons::belt_feed
{
	struct visual {contact query{};bool held{};hands::anchor wrist{};std::span<const joint_pose> fingers;};
	struct hinge_contact {hands::anchor wrist;hands::vec wrist_delta,contact_delta;float angle;};
	inline hinge_contact query_hinge(hands::anchor rest,hands::vec axis,float angle,float amount,
		const part_grip_pose& hand,hands::anchor raw,float units)
	{
		using namespace hands;
		const auto delta=compose_reload(hinge_pose(rest,axis,angle,amount),inverse_reload(rest));
		const auto wrist=compose_reload(delta,hand.wrist);
		const auto rotation=part_grip_capture::relaxed_rotation(raw.rotation,wrist.rotation);
		const auto point=compose_reload({raw.position,rotation},{hand.contact_in_wrist,{0,0,0,1}}).position;
		const auto initial=compose_reload(hand.wrist,{hand.contact_in_wrist,{0,0,0,1}}).position;
		const auto pivot=inverse_reload(rest);
		auto from=compose_reload(pivot,{initial,{0,0,0,1}}).position,to=compose_reload(pivot,{point,{0,0,0,1}}).position;
		from=sub(from,scale(axis,dot(from,axis)));to=sub(to,scale(axis,dot(to,axis)));
		return {wrist,scale(sub(raw.position,wrist.position),1/units),
			scale(sub(point,compose_reload(delta,{initial,{0,0,0,1}}).position),1/units),std::atan2(dot(axis,cross(from,to)),dot(from,to))};
	}
	inline visual present(const profile& p,const physical_reload::part_rig& parts,const hands::rig& r,
		const mechanics::state& ammo,grasp grip,hands::anchor gun,hands::anchor raw,hands::anchor held_box,
		hands::anchor box_rest,int off,hands::quat mirror,float units,bool active,bool live,std::span<hands::bone> solved,bool query_only=false,float shown_bridge=-1.f,float shown_cover_amount=-1.f)
	{
		using namespace hands;using namespace hands::pose_math;visual out;
		const auto cover_hand=off ? hands::pose_mirror::part(p.cover_grip,mirror) : p.cover_grip;
		const auto chain_hand=off ? hands::pose_mirror::part(p.chain_grip,mirror) : p.chain_grip;
		const auto local=compose_reload(inverse_reload(gun),raw);
		const auto cover=query_hinge(p.cover_rest,p.cover_axis,p.cover_angle,active ? ammo.belt.cover:0,cover_hand,local,units);
		auto& c=out.query;c.hand=scale(local.position,1/units);
		c.cover_angle=cover.angle;c.cover_distance=std::min(cover_distance(p,cover.wrist_delta),cover_distance(p,cover.contact_delta));
		part_grip_pose bridge_hand;
		if(p.bridge)
		{
			bridge_hand=off ? hands::pose_mirror::part(p.bridge->grip,mirror):p.bridge->grip;
			const auto rail=query_hinge(p.bridge->rest,p.bridge->axis,p.bridge->angle,active ? (shown_bridge>=0?shown_bridge:ammo.belt.bridge):0,bridge_hand,local,units);
			c.bridge_angle=rail.angle;c.bridge_distance=std::min(length(rail.wrist_delta),length(rail.contact_delta));
			c.bridge_settled=shown_bridge<0 || std::abs(shown_bridge-ammo.belt.bridge)<.001f;
			if(p.bridge->release)
			{
				const auto press=off ? hands::pose_mirror::part(*p.bridge->release,mirror):*p.bridge->release;
				c.release_distance=length(sub(local.position,press.wrist.position))/units;
			}
		}
		const auto chain_rest_point=compose_reload(chain_hand.wrist,{chain_hand.contact_in_wrist,{0,0,0,1}}).position;
		const auto chain_target_delta=compose_reload(p.links.front().seated,inverse_reload(p.links.front().loose));
		const auto target_point=compose_reload(chain_target_delta,{chain_rest_point,{0,0,0,1}}).position;
		const auto chain_point=compose_reload(local,{chain_hand.contact_in_wrist,{0,0,0,1}}).position;
		c.chain_point=scale(chain_point,1/units);c.chain_distance=std::min(length(sub(local.position,chain_hand.wrist.position)),length(sub(chain_point,chain_rest_point)))/units;
		c.feed_distance=length(sub(chain_point,target_point))/units;
		c.alignment=dot(rotate(local.rotation,{1,0,0}),rotate(compose_reload(chain_target_delta,chain_hand.wrist).rotation,{1,0,0}));
		if(!active || query_only)return out;
		const auto amount=live && grip.part==lease::cover ? cover_target(p,grip,c) : shown_cover_amount>=0?shown_cover_amount:ammo.belt.cover;
		const auto shown_cover=cover_pose(p,amount);
		move_part(r,parts.cover,compose_reload(gun,shown_cover),solved);
		auto bridge_amount=shown_bridge>=0?shown_bridge:ammo.belt.bridge;
		if(p.bridge)
		{
			if(live && grip.part==lease::bridge)
				bridge_amount=std::clamp(grip.initial+std::remainder(c.bridge_angle-grip.angle,6.283185307f)/p.bridge->angle,ammo.belt.cover>0 ? bridge_clearance:0.f,1.f);
			const auto shown=hinge_pose(p.bridge->rest,p.bridge->axis,p.bridge->angle,bridge_amount);
			move_part(r,parts.bridge,compose_reload(gun,shown),solved);
			if(live && grip.part==lease::bridge)
			{out.held=true;out.wrist=compose_reload(gun,compose_reload(compose_reload(shown,inverse_reload(p.bridge->rest)),bridge_hand.wrist));out.fingers=bridge_hand.fingers;}
			else if(live && grip.part==lease::bridge_release && p.bridge->release)
			{
				const auto press=off ? hands::pose_mirror::part(*p.bridge->release,mirror):*p.bridge->release;
				out.held=true;out.wrist=compose_reload(gun,press.wrist);out.fingers=press.fingers;
			}
		}
		for(size_t i=0;i<p.details.size();++i)
		{
			const auto& d=p.details[i];const auto t=d.bridge?bridge_amount:amount;
			const anchor pose{add(d.rest.position,scale(sub(d.open.position,d.rest.position),t)),blend_quat(d.rest.rotation,d.open.rotation,t)};
			move_part(r,parts.belt_details[i],compose_reload(gun,pose),solved);
		}
		const bool holding_box=!ammo.magazine_inserted && ammo.magazine_hand!=hand::none;
		// Installed ownership can precede the final seating translation. Follow
		// this frame's posed box during seating/pull, not its final receiver anchor.
		const anchor posed_box{solved[parts.magazine].position,normalize(solved[parts.magazine].rotation)};
		const auto chain_base=compose_reload(holding_box ? held_box:posed_box,inverse_reload(box_rest));
		const auto delta=sub(chain_point,chain_rest_point);
		// Fixed box end, hand-controlled leading end. Authored poses provide
		// curvature; bounded per-link deformation avoids a rigid-body chain solver.
		const bool pulling=live && grip.part==lease::chain && length(delta)<units*.45f;
		const auto route=sub(target_point,chain_rest_point);
		const float progress=pulling && dot(route,route)>1e-6f ? std::clamp(dot(delta,route)/dot(route,route),0.f,1.f) : 0.f;
		const auto residual=sub(delta,scale(route,progress));
		for(size_t i=0;i<p.links.size();++i)
		{
			auto pose=!holding_box && ammo.belt.laid ? p.links[i].seated : p.links[i].loose;
			if(pulling)
			{
				const auto weight=1.f-float(i)/float(p.links.size()-1);
				pose.position=add(add(pose.position,scale(sub(p.links[i].seated.position,pose.position),progress)),scale(residual,weight*weight));
				pose.rotation=blend_quat(pose.rotation,p.links[i].seated.rotation,progress);
			}
			move_part(r,parts.belt_links[i],compose_reload(chain_base,pose),solved);
		}
		if(live && grip.part==lease::cover)
		{out.held=true;out.wrist=compose_reload(gun,compose_reload(compose_reload(shown_cover,inverse_reload(p.cover_rest)),cover_hand.wrist));out.fingers=cover_hand.fingers;}
		else if(pulling){out.held=true;out.wrist=raw;out.fingers=chain_hand.fingers;}
		return out;
	}
}
