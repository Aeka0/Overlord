#pragma once
#include "weapon_holding.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"
#include "body_reach_volume.hpp"
#include "../head_pose_bridge.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::equipment
{
	using vr::hand;
	using namespace hands;
	// Body-relative metres. Keep the whole chest layout together so future
	// native lethal/tactical inventory does not compete with the knife anchor.
	enum class slot : unsigned { knife, tactical, lethal, count };
	enum class knife_grip : unsigned { forward, reverse };
	struct chest_layout
	{
		float forward{.045f},down{.35f},spacing{.18f},grab_radius{.10f};
	};
	struct chest_slots
	{
		bool valid{};
		std::array<anchor,3> anchors{};
		std::array<body_reach_volume,3> grabs{};
		float radius{};
	};
	inline vec body_local(const head_pose_bridge::spatial_frame& body,vec point) noexcept
	{
		const auto delta=scale(sub(point,body.head_position),1/body.units_per_meter);
		return {dot(delta,body.head_yaw_axis[0]),dot(delta,body.head_yaw_axis[1]),dot(delta,body.head_yaw_axis[2])};
	}
	inline vec body_world(const head_pose_bridge::spatial_frame& body,vec point) noexcept
	{return add(body.head_position,scale(add(scale(body.head_yaw_axis[0],point[0]),add(scale(body.head_yaw_axis[1],point[1]),scale(body.head_yaw_axis[2],point[2]))),body.units_per_meter));}
	inline chest_slots locate_chest(const head_pose_bridge::spatial_frame& frame,const chest_layout& layout={}) noexcept
	{
		const auto body=head_pose_bridge::body_slots_frame(frame);
		chest_slots out;
		if (!std::isfinite(body.units_per_meter) || body.units_per_meter<=0 || body.units_per_meter>10000) return out;
		for (float x:{layout.forward,layout.down,layout.spacing,layout.grab_radius}) if (!std::isfinite(x) || x<=0 || x>.8f) return out;
		for (auto axis:body.head_yaw_axis) if (!std::isfinite(dot(axis,axis)) || std::abs(dot(axis,axis)-1)>.001f) return out;
		if (std::abs(dot(body.head_yaw_axis[0],body.head_yaw_axis[1]))>.001f || length(sub(cross(body.head_yaw_axis[0],body.head_yaw_axis[1]),body.head_yaw_axis[2]))>.001f) return out;
		for (float x:body.head_position) if (!std::isfinite(x)) return out;
		const auto center=add(body.head_position,scale(sub(scale(body.head_yaw_axis[0],layout.forward),scale(body.head_yaw_axis[2],layout.down)),body.units_per_meter));
		const auto rotation=from_axis({scale(body.head_yaw_axis[2],-1),body.head_yaw_axis[1],body.head_yaw_axis[0]});
		for (unsigned i=0;i<3;++i)
		{
			out.anchors[i]={add(center,scale(body.head_yaw_axis[1],(1.f-float(i))*layout.spacing*body.units_per_meter)),rotation};
			// Tall body-aligned boxes share a symmetric width and a gap between
			// slots. The knife alone has more depth/height and a raised handle area.
			const bool knife=i==unsigned(slot::knife);
			const vec half=scale(vec{knife?.12f:.10f,std::min(.08f,layout.spacing*.45f),knife?.20f:.16f},body.units_per_meter);
			const auto origin=add(out.anchors[i].position,scale(body.head_yaw_axis[2],knife?.04f*body.units_per_meter:0.f));
			out.grabs[i]={origin,scale(half,-1),half,body.head_yaw_axis,0};
		}
		out.radius=layout.grab_radius*body.units_per_meter;out.valid=true;return out;
	}
	inline float chest_grab_distance(const chest_slots& chest,slot where,vec palm) noexcept
	{return chest.valid && unsigned(where)<chest.grabs.size()?chest.grabs[unsigned(where)].box_distance(palm):INFINITY;}
	struct knife_state
	{
		hand holder{hand::none};
		knife_grip grip{};
		std::uint64_t revision{};
		void return_to_chest() noexcept {if (holder!=hand::none){holder=hand::none;++revision;}}
		bool take(hand actor,knife_grip selected) noexcept
		{
			if (!vr::valid_hand(actor) || holder!=hand::none) return false;
			holder=actor;grip=selected;++revision;return true;
		}
		bool release(unsigned edges) noexcept
		{
			if (!vr::valid_hand(holder) || !(edges&(1u<<unsigned(holder)))) return false;
			return_to_chest();return true;
		}
	};
	// A real squeeze may arrive just before the palm reaches the handle. Keep
	// that intent briefly, but never acquire from an old held button, an occupied
	// hand, a release, or a tracking/context recovery without a new press.
	class grab_intent
	{
		using clock=controller_input::clock;
		std::array<clock::time_point,2> until_{};
		std::uint64_t reference_{};
	public:
		void reset() noexcept {until_={};reference_=0;}
		unsigned consume(const controller_input::frame& input,unsigned available,unsigned pressed,unsigned released,
			const std::array<controller_input::digital_action,2>& buttons) noexcept
		{
			if (reference_!=input.reference_generation) {reset();reference_=input.reference_generation;}
			unsigned result{};
			for (unsigned h=0;h<2;++h)
			{
				const unsigned bit=1u<<h;
				if (!input.focused || !input.reference_generation || !(available&bit) || (released&bit) ||
					!buttons[h].active || !buttons[h].down) {until_[h]={};continue;}
				if (pressed&bit) until_[h]=input.sampled_at+std::chrono::milliseconds(200);
				if (until_[h]!=clock::time_point{} && input.sampled_at<=until_[h] &&
					until_[h]-input.sampled_at<=std::chrono::milliseconds(200)) result|=bit;
				else until_[h]={};
			}
			return result;
		}
		unsigned consume(const controller_input::frame& input,unsigned available,unsigned pressed,unsigned released) noexcept
		{return consume(input,available,pressed,released,input.squeeze);}
	};
}
