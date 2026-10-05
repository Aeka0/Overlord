#pragma once
#include "optic_catalog.hpp"
#include "ads_alignment.hpp"

namespace vr::gameplay::weapons::ads_comfort
{
	enum class sight { unchanged, holographic, acog, scope, thermal };
	struct definition
	{
		sight type{};
		std::string_view root{};
		const optics::definition* lens{};
	};
	inline definition classify(std::string_view model) noexcept
	{
		// Assembly admission already validates exact native aliases. Woodland
		// shares geometry, but this does not broaden magnifier/material admission.
		if (model.ends_with("_woodland")) model.remove_suffix(9);
		if (const auto* lens=optics::find(model))
			return {lens->thermal ? sight::thermal : lens->magnification==4.f ? sight::acog : sight::scope,lens->root,lens};
		if (optics::variant_name(model,"attach_h2_eotech_2_vm")) return {sight::holographic,"tag_eotech"};
		if (optics::variant_name(model,"attach_h2_thermal_scope_2_vm")) return {sight::thermal,"tag_thermal_scope"};
		if (model=="attach_h2_sa80_scope_vm") return {sight::acog,"tag_sa80_scope"};
		return {}; // Iron sights, red dots (including MARS), and unknown optics.
	}
	inline float distance(sight type) noexcept
	{
		switch(type)
		{
		case sight::holographic: return .05f;
		case sight::acog: return .10f;
		case sight::scope: return .15f;
		case sight::thermal: return ads_alignment::maximum_translation(true);
		default: return 0;
		}
	}
	struct binding
	{
		definition optic{};
		int root{-1};
		// Optics without lens metadata use native bounds in root-local units.
		std::array<hands::vec,8> corners{};
	};
	inline bool finite(hands::vec value) noexcept
	{
		for(float x:value) if(!std::isfinite(x) || std::abs(x)>1e7f) return false;
		return true;
	}
	inline bool valid_rotation(hands::quat value) noexcept
	{
		float norm{};for(float x:value) {if(!std::isfinite(x)) return false;norm+=x*x;}
		return norm>.5f && norm<1.5f;
	}
	inline constexpr float full_assistance_distance_meters=.30f;
	inline constexpr float no_assistance_distance_meters=ads_alignment::sight_reach_meters;
	inline float distance_weight(float meters) noexcept
	{
		if(!std::isfinite(meters) || meters<0) return 0;
		return ads_alignment::smooth((no_assistance_distance_meters-meters)/
			(no_assistance_distance_meters-full_assistance_distance_meters));
	}
	inline float distance_weight(sight type,float meters) noexcept
	{
		if(type!=sight::thermal)return distance_weight(meters);
		if(!std::isfinite(meters) || meters<0)return 0;
		// The small thermal aperture needs near-eye ADS. Full assistance across
		// normal two-hand aiming reach; an extended gun still eases back to rest.
		return ads_alignment::smooth((.70f-meters)/.20f);
	}
	struct eye_geometry
	{
		float clearance_meters{},distance_meters{},depth_meters{};
		bool valid{};
	};
	inline eye_geometry measure_eye(const binding& optic,std::span<const hands::bone> pose,
		hands::vec head,hands::vec forward,float units) noexcept
	{
		using namespace hands;
		if(optic.root<0 || size_t(optic.root)>=pose.size() || !finite(head) || !finite(forward) ||
			std::abs(dot(forward,forward)-1.f)>.002f || !std::isfinite(units) || units<=0 || units>10000) return {};
		const auto& root=pose[optic.root];
		if(!finite(root.position) || !valid_rotation(root.rotation)) return {};
		const auto rotation=normalize(root.rotation);
		const auto from_head=sub(root.position,head);
		vec rear{};float depth{};
		if(optic.optic.lens)
		{
			rear=add(from_head,rotate(rotation,scale(optic.optic.lens->center_meters,units)));
			depth=dot(rear,forward);
		}
		else
		{
			for(size_t i=0;i<optic.corners.size();++i)
			{
				const auto corner=optic.corners[i];if(!finite(corner)) return {};
				const auto point=add(from_head,rotate(rotation,corner));
				const float along=dot(point,forward);
				if(i==0 || along<depth) depth=along;
				rear=add(rear,scale(point,1.f/float(optic.corners.size())));
			}
			// Without reviewed lens metadata, use the native bounds' center
			// projected onto their rear plane, reusing the safety geometry.
			rear=add(rear,scale(forward,depth-dot(rear,forward)));
		}
		const float meters=length(rear)/units;
		depth/=units;
		if(!std::isfinite(meters) || !std::isfinite(depth)) return {};
		// Thermal ADS targets 5 cm behind the measured rear lens; the other
		// optics retain 8 cm. Never push an already close optic away.
		const float relief=optic.optic.type==sight::thermal ? .05f : .08f;
		return {std::clamp(depth-relief,0.f,distance(optic.optic.type)),meters,depth,true};
	}
	class transition
	{
	public:
		inline static constexpr float max_speed_meters_per_second=.25f;
		void reset() noexcept {meters_=velocity_=0;seen_=false;}
		float update(const controller_input::frame& input,const hold& owner,std::uint64_t assembly,
			sight type,bool allowed,float alignment,float maximum,controller_input::clock::time_point now) noexcept
		{
			if(!allowed || !ads_alignment::tracked_hold(input,owner) || !input.focused || input.orientation_settling || !input.sequence ||
				now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150) ||
				!std::isfinite(alignment) || !std::isfinite(maximum) || maximum<0 || distance(type)==0)
			{reset();return 0;}
			if(!seen_ || owner.id()!=owner_.id() || owner.rear!=owner_.rear || owner.rear_revision!=owner_.rear_revision ||
				owner.attachment!=owner_.attachment ||
				assembly!=assembly_ || type!=type_ || input.reference_generation!=reference_ ||
				input.continuity_generation!=continuity_ || input.sequence<sequence_ || input.sampled_at<at_ ||
				(!input.continuity_generation && input.sampled_at-at_>std::chrono::milliseconds(150))) reset();
			const float dt=seen_ ? std::clamp(std::chrono::duration<float>(input.sampled_at-at_).count(),0.f,.15f) : 0.f;
			owner_=owner;assembly_=assembly;type_=type;reference_=input.reference_generation;
			continuity_=input.continuity_generation;sequence_=input.sequence;at_=input.sampled_at;seen_=true;
			const float goal=ads_alignment::supported(input,owner) ?
				std::clamp(alignment,0.f,1.f)*std::min(distance(type),maximum) : 0.f;
			// Retain velocity as well as position when the alignment/ADS corridor
			// changes. The former first-order filter could start a 15 cm approach
			// at 1.25 m/s. A critically damped spring starts at rest, with an
			// explicit speed ceiling for large changes and slow render frames.
			if(dt>0)
			{
				const float frequency=type==sight::thermal ? 24.f : 10.f;
				const float speed=type==sight::thermal ? .9f : max_speed_meters_per_second;
				const float error=meters_-goal,tangent=velocity_+frequency*error,decay=std::exp(-frequency*dt);
				const float next=goal+(error+tangent*dt)*decay;
				velocity_=std::clamp((velocity_-frequency*tangent*dt)*decay,
					-speed,speed);
				meters_+=std::clamp(next-meters_,-speed*dt,speed*dt);
				// Finish the imperceptible tail (under 0.05 mm at under 1 mm/s).
				if(error*(meters_-goal)<0 || (std::abs(goal-meters_)<.00005f && std::abs(velocity_)<.001f))
				{meters_=goal;velocity_=0;}
			}
			// Approach the head safely even while blending out or on duplicate reads.
			const float safe=std::clamp(meters_,0.f,maximum);
			if(safe!=meters_)velocity_=0;
			meters_=safe;
			return meters_;
		}
	private:
		hold owner_{};
		std::uint64_t assembly_{},reference_{},continuity_{},sequence_{};
		controller_input::clock::time_point at_{};
		sight type_{};
		float meters_{},velocity_{};
		bool seen_{};
	};
	// One rigid translation for the complete gun, muzzle and optic. Reuse the
	// arm solver for attached hands; free hands and raw tracking remain intact.
	inline bool apply(const hands::rig& rig,std::span<hands::bone> pose,int rear,int support,
		const std::array<hands::vec,3>& body_axis,hands::vec translation) noexcept
	{
		using namespace hands;
		if(rig.count<=0 || rig.count>256 || pose.size()<size_t(rig.count) || rear<0 || rear>1 ||
			support<-1 || support>1 || support==rear || !finite(translation)) return false;
		if(translation==vec{}) return true;
		std::array<anchor,2> targets;
		std::array<vec,2> shoulders;
		for(int h=0;h<2;++h)
		{
			const auto a=rig.arms[h];
			if(a.wrist<0 || a.wrist>=rig.count || a.shoulder<0 || a.shoulder>=rig.count) return false;
			targets[h]={pose[a.wrist].position,pose[a.wrist].rotation};
			if(h==rear || h==support) targets[h].position=add(targets[h].position,translation);
			shoulders[h]=pose[a.shoulder].position;
		}
		std::array<bone,256> arms{};std::array<bool,2> limited{};
		if(!solve_arms(rig,pose,targets,shoulders,body_axis,arms,limited)) return false;
		for(int h=0;h<2;++h) if(h==rear || h==support)
		{
			const auto wrist=rig.arms[h].wrist;
			const auto correction=sub(targets[h].position,arms[wrist].position);
			for(int i=0;i<rig.count;++i)
				if(!rig.weapon_bones[i] && descendant(i,wrist,rig)) arms[i].position=add(arms[i].position,correction);
		}
		for(int i=0;i<rig.count;++i)
			if(rig.weapon_bones[i] || i==rig.weapon_tag) pose[i].position=add(pose[i].position,translation);
			else if(descendant(i,rig.arms[rear].shoulder,rig) ||
				(support>=0 && descendant(i,rig.arms[support].shoulder,rig))) pose[i]=arms[i];
		return true;
	}
}
