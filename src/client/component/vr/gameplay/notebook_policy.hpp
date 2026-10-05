#pragma once
#include "special_equipment_policy.hpp"
#include "hinge_travel.hpp"
#include "physical_reload_geometry.hpp"

namespace vr::gameplay::equipment::special::notebook
{
	using namespace hands::pose_math;
	inline constexpr float radians=.01745329252f;
	// Native h2_viewmodel_uav_control_unit bind coordinates, in engine inches.
	inline constexpr vec hinge{4.33396387f,2.f,1.99011505f};
	inline constexpr vec auxiliary_hinge{4.14704418f,-7.28323603f,2.09837294f};
	inline constexpr float bind_angle=106.606f;
	inline constexpr float max_angle=bind_angle,stop_tolerance=.05f;
	inline constexpr float stowed_clearance_m=.03f;
	inline anchor stowed_root(const head_pose_bridge::spatial_frame& body)noexcept
	{
		auto result=abdomen(body);const auto frame=head_pose_bridge::body_slots_frame(body);
		result.position=add(result.position,scale(frame.head_yaw_axis[0],frame.units_per_meter*stowed_clearance_m));
		// Case back is -Z, screen lip is -X. Put the back against the torso
		// with the lip above the hinge, using the shared body heading.
		result.rotation=from_axis({scale(frame.head_yaw_axis[2],-1),frame.head_yaw_axis[1],frame.head_yaw_axis[0]});
		return result;
	}
	// Native camera acknowledgement follows the authored blackout. Hold the
	// physical lease through a conservative scene-settle window, starting at
	// that acknowledgement, never at activation. Remote input does not wait.
	inline constexpr int camera_settle_msec=1000;
	inline bool camera_settled(int now,int acknowledged_at)noexcept
	{return acknowledged_at>=0 && now>=acknowledged_at && std::int64_t(now)-acknowledged_at>=camera_settle_msec;}
	inline bool needs_empty_return(unsigned device,unsigned held_weapon,unsigned selected_weapon,unsigned actual_weapon)noexcept
	{
		// The native type-1 action-slot handler rejects an already active
		// exclusive device. Only clear OUR completed device when the physical
		// inventory is empty; a script's different final selection wins.
		return device && device<512 && !held_weapon && selected_weapon==device && actual_weapon==device;
	}
	inline anchor lid(float angle,bool auxiliary=false)noexcept
	{
		const auto pivot=auxiliary?auxiliary_hinge:hinge;
		const float a=(angle-bind_angle)*radians*.5f;const quat q{0,std::sin(a),0,std::cos(a)};
		return {sub(pivot,rotate(q,pivot)),q};
	}
	inline vec lip(float angle)noexcept
	{
		const float a=angle*radians;return add(hinge,{-8.f*std::cos(a),0,8.f*std::sin(a)});
	}
	inline float screen_edge_distance(vec local,float angle)noexcept
	{
		// Native main-screen frame spans Y=-4.96..9.12; the hinge is at Y=2.
		// Include both side rails and the hinge edge, not just the opening lip.
		const auto top=lip(angle);
		const std::array<vec,4> edge{{add(hinge,{0,-7,0}),add(top,{0,-7,0}),add(top,{0,7.2f,0}),add(hinge,{0,7.2f,0})}};
		float distance=INFINITY;
		for(unsigned i=0;i<edge.size();++i)distance=std::min(distance,weapons::physical_reload::segment_distance(local,edge[i],edge[(i+1)%edge.size()]));
		return distance;
	}
	struct hinge_drag
	{
		bool active{};float start_angle{},phase{},travel{};anchor wrist{};vec probe_offset{};
		bool begin(float angle,anchor local)noexcept
		{
			auto p=sub(local.position,hinge);probe_offset={};
			if(!std::isfinite(angle) || !std::isfinite(length(p)))return false;
			for(float x:local.rotation)if(!std::isfinite(x))return false;
			// A hinge-edge grasp has almost no translational lever. Track a
			// virtual point on the same screen so wrist rotation can open it too.
			if(std::hypot(p[0],p[2])<2)
			{probe_offset=compose(inverse(local),{lip(angle),{0,0,0,1}}).position;p=sub(lip(angle),hinge);}
			active=true;start_angle=angle;travel=angle/max_angle;phase=std::atan2(p[2],-p[0]);wrist=local;return true;
		}
		float sample(anchor local)noexcept{return sample(add(local.position,rotate(local.rotation,probe_offset)));}
		float sample(vec local)noexcept
		{
			const auto p=sub(local,hinge);if(active && std::isfinite(length(p)) && std::hypot(p[0],p[2])>=2)
				weapons::advance_hinge_travel(travel,phase,std::atan2(p[2],-p[0]),max_angle*radians);
			return std::clamp(travel,0.f,1.f)*max_angle;
		}
		anchor constrained(float angle)const noexcept
		{
			const float a=(angle-start_angle)*radians*.5f;const quat q{0,std::sin(a),0,std::cos(a)};
			return {add(hinge,rotate(q,sub(wrist.position,hinge))),normalize(multiply(q,wrist.rotation))};
		}
	};
	enum class phase {stowed,holding,pending,remote};
	struct session
	{
		phase stage{};vr::hand holder{vr::hand::none},opener{vr::hand::none};
		vr::hand controller{vr::hand::none}; // Activation-hand provenance; remote fire accepts either hand.
		float angle{};hinge_drag drag{};bool saw_native{},docked{},opener_trigger{};
		std::uint64_t revision{},control_revision{};
		bool awaiting_native()const noexcept{return stage==phase::pending && !saw_native;}
		bool native_control()const noexcept{return requested() && (saw_native || stage==phase::remote);}
		bool physical()const noexcept{return stage==phase::holding || (awaiting_native() && !docked);}
		bool held()const noexcept{return stage==phase::holding || (requested() && !docked);}
		bool requested()const noexcept{return stage==phase::pending || stage==phase::remote;}
		void close()noexcept{const auto next=revision+1;*this={};revision=next;}
		bool take(vr::hand h)noexcept{if(stage!=phase::stowed || !vr::valid_hand(h))return false;holder=h;stage=phase::holding;++revision;return true;}
		bool take_lid(vr::hand h,bool trigger,anchor local)noexcept
		{
			if(!physical() || !vr::valid_hand(h) || h==holder || vr::valid_hand(opener) || !drag.begin(angle,local))return false;
			opener=h;opener_trigger=trigger;return true;
		}
		bool open(float value)noexcept
		{
			if(!physical() || !drag.active || !std::isfinite(value))return false;
			angle=std::clamp(value,0.f,max_angle);return true;
		}
		bool release_lid(bool deliberate)noexcept
		{
			if(!physical() || !drag.active || !vr::valid_hand(opener))return false;
			const bool commit=stage==phase::holding && deliberate && angle>=max_angle-stop_tolerance;
			if(commit){angle=max_angle;controller=opener;stage=phase::pending;control_revision=++revision;}
			opener=vr::hand::none;opener_trigger=false;drag={};return commit;
		}
		void dock()noexcept
		{if(!docked){docked=true;holder=opener=vr::hand::none;angle=0;drag={};opener_trigger=false;++revision;}}
		// A pending request owns native selection, not the user's physical grip.
		// Manual stow releases both hands without cancelling an in-flight native
		// action or losing the later camera acknowledgement.
		void release()noexcept{if(awaiting_native())dock();else if(!requested())close();}
		void observe(bool native,bool camera,bool settled,bool timeout)noexcept
		{
			if(!requested())return;
			if(native)
			{
				saw_native=true;if(camera)stage=phase::remote;
				if(camera && settled)dock();
			}
			else if(saw_native || timeout)close();
		}
	};
}
