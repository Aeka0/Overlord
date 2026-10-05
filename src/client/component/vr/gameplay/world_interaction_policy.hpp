#pragma once
#include "hand_pose_solver.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace vr::gameplay::interaction
{
	using hands::vec;
	struct target_key
	{
		int entity{-1};
		std::uint64_t generation{};
		bool operator==(const target_key&) const = default;
		explicit operator bool() const noexcept { return entity>0 && entity<4000; }
	};
	struct ray
	{
		vec origin{}, forward{}, head{};
		float units{}, distance_meters{2.2f}, half_angle_degrees{12.f};
		bool operator==(const ray&) const = default;
	};
	enum class prompt_kind { use, resupply };
	struct target
	{
		target_key key{};
		vec position{};
		std::uint32_t weapon{};
		float cosine{-1}, distance{};
		bool contact{};
		prompt_kind prompt{};
		std::uint8_t hint{}; // Native sethintstring identity, sampled after admission.
		explicit operator bool() const noexcept { return bool(key); }
	};
	// Arbitration, execution and hover share the selection made for this exact
	// input/ray. Never carry it to another frame or use it as a live-entity proof.
	class query_batch
	{
		std::uint64_t sequence_{},reference_{};unsigned mask_{};
		std::array<ray,2> rays_{};std::array<target,2> targets_{};
	public:
		void begin(std::uint64_t sequence=0,std::uint64_t reference=0)noexcept
		{sequence_=sequence;reference_=reference;mask_=0;}
		void record(unsigned hand,const ray& aim,const target& value)noexcept
		{if(hand<2 && sequence_ && reference_){rays_[hand]=aim;targets_[hand]=value;mask_|=1u<<hand;}}
		std::optional<target> find(unsigned hand,std::uint64_t sequence,std::uint64_t reference,const ray& aim)const noexcept
		{
			if(hand>=2 || !(mask_&(1u<<hand)) || sequence!=sequence_ || reference!=reference_ || aim!=rays_[hand])return {};
			return targets_[hand];
		}
	};
	inline bool valid(const ray& r) noexcept
	{
		const auto finite=[](const vec& p) {for (float x:p) if (!std::isfinite(x) || std::abs(x)>1e7f) return false;return true;};
		return finite(r.origin) && finite(r.head) && finite(r.forward) &&
			std::isfinite(r.units) && r.units>1 && r.units<1000 &&
			std::isfinite(r.distance_meters) && r.distance_meters>=.5f && r.distance_meters<=3.f &&
			std::isfinite(r.half_angle_degrees) && r.half_angle_degrees>=1 && r.half_angle_degrees<=25 &&
			std::abs(hands::dot(r.forward,r.forward)-1)<.002f &&
			hands::length(hands::sub(r.origin,r.head))<=1.25f*r.units;
	}
	// Direct contact precedes remote angular intent. Distance breaks near-equal angles; entity ID
	// breaks an exact tie so candidate enumeration order cannot flicker the HUD.
	inline bool better(const target& a,const target& b) noexcept
	{
		if (!a) return false;
		if (!b) return true;
		if (a.contact!=b.contact) return a.contact;
		if (std::abs(a.cosine-b.cosine)>0.0001f) return a.cosine>b.cosine;
		if (std::abs(a.distance-b.distance)>.001f) return a.distance<b.distance;
		return a.key.entity<b.key.entity;
	}
	inline bool prefer(const target& proposal,const target& selected,target_key held) noexcept
	{
		return proposal && (!selected || (proposal.key==held && selected.key!=held) ||
			(selected.key!=held && better(proposal,selected)));
	}
	inline target score(const ray& r,target_key key,const vec& position,std::uint32_t weapon=0) noexcept
	{
		if (!valid(r) || !key) return {};
		for (float x:position) if (!std::isfinite(x) || std::abs(x)>1e7f) return {};
		const auto delta=hands::sub(position,r.origin);
		const float distance=hands::length(delta), limit=r.distance_meters*r.units;
		if (distance<.01f*r.units || distance>limit || hands::length(hands::sub(position,r.head))>limit) return {};
		const float cosine=hands::dot(delta,r.forward)/distance;
		if (!std::isfinite(cosine) || cosine<std::cos(r.half_angle_degrees*.0174532925199433f)) return {};
		return {key,position,weapon,std::clamp(cosine,-1.f,1.f),distance};
	}
	// Closest points between a finite hand ray and a native world-space AABB.
	// The squared distance is quadratic between slab crossings; evaluating its
	// minimum in each interval avoids iterative searches and centre-point aiming.
	inline vec ray_box_point(const ray& r,const vec& center,const vec& half) noexcept
	{
		const float limit=r.distance_meters*r.units;
		std::array<float,8> cuts{};unsigned count=2;cuts[1]=limit;
		for (int i=0;i<3;++i) if (std::abs(r.forward[i])>1e-6f)
			for (const float edge:{center[i]-half[i],center[i]+half[i]})
			{
				const float t=(edge-r.origin[i])/r.forward[i];
				if (t>0 && t<limit) cuts[count++]=t;
			}
		std::sort(cuts.begin(),cuts.begin()+count);
		vec best{};float best_distance=std::numeric_limits<float>::max();
		for (unsigned n=1;n<count;++n)
		{
			const float mid=(cuts[n-1]+cuts[n])*.5f;float a{},b{};
			for (int i=0;i<3;++i)
			{
				const float p=r.origin[i]+r.forward[i]*mid,lo=center[i]-half[i],hi=center[i]+half[i];
				if (p>=lo && p<=hi) continue;
				a+=r.forward[i]*r.forward[i];b+=r.forward[i]*(r.origin[i]-(p<lo ? lo : hi));
			}
			const float t=a>0 ? std::clamp(-b/a,cuts[n-1],cuts[n]) : cuts[n-1];
			vec point{};float distance{};
			for (int i=0;i<3;++i)
			{
				const float p=r.origin[i]+r.forward[i]*t;
				point[i]=std::clamp(p,center[i]-half[i],center[i]+half[i]);
				distance+=(p-point[i])*(p-point[i]);
			}
			if (distance<best_distance) {best_distance=distance;best=point;}
		}
		return best;
	}
	// Shared model selection for pickups and native script models. A zero weapon
	// token remains an object-backed native use, never a carry transaction.
	inline target score_model(const ray& r,target_key key,const vec& center,const vec& half,std::uint32_t weapon=0) noexcept
	{
		if (!valid(r) || !key) return {};
		vec nearest{},head_nearest{};
		for (int i=0;i<3;++i)
		{
			if (!std::isfinite(center[i]) || std::abs(center[i])>1e7f ||
				!std::isfinite(half[i]) || half[i]<0 || half[i]>2*r.units) return {};
			nearest[i]=std::clamp(r.origin[i],center[i]-half[i],center[i]+half[i]);
			head_nearest[i]=std::clamp(r.head[i],center[i]-half[i],center[i]+half[i]);
		}
		if (hands::length(hands::sub(head_nearest,r.head))>r.distance_meters*r.units) return {};
		const auto delta=hands::sub(nearest,r.origin);const float distance=hands::length(delta);
		if (distance<=.12f*r.units)
		{
			// At contact the ray may begin inside the gun or point past its centre.
			// Intent is direct touch; do not require an unstable normalized zero ray.
			const float cosine=distance>.001f*r.units ? hands::dot(delta,r.forward)/distance : 1.f;
			return {key,nearest,weapon,std::clamp(cosine,-1.f,1.f),distance,true};
		}
		return score(r,key,ray_box_point(r,center,half),weapon);
	}
	inline target score_weapon(const ray& r,target_key key,const vec& center,const vec& half,std::uint32_t weapon) noexcept
	{return weapon ? score_model(r,key,center,half,weapon) : target{};}
	inline bool use_trigger_class(std::string_view name) noexcept
	{return name=="trigger_use" || name.starts_with("trigger_use_");}
	inline target score_use_trigger(const ray& r,target_key key,const vec& center,const vec& half) noexcept
	{
		if(!valid(r) || !key)return {};
		bool large{};
		for(float v:half){if(!std::isfinite(v) || v<0)return {};large|=v>2*r.units;}
		// Ordinary use triggers have authored volumes, not visible model bounds.
		// Keep legacy point selection for unusually large scene trigger volumes.
		return large ? score(r,key,center) : score_model(r,key,center,half);
	}
	inline target score_breach(const ray& r,target_key key,const vec& center,const vec& native_half) noexcept
	{
		if(!valid(r) || !key)return {};
		vec half{};
		for(unsigned i=0;i<3;++i)
		{
			if(!std::isfinite(native_half[i]) || native_half[i]<0)return {};
			half[i]=std::clamp(native_half[i],(i==2 ? .4f : .3f)*r.units,r.units);
		}
		return score_model(r,key,center,half);
	}
	// A grip owns one target until release. Hover changes never retarget a hold.
	class use_lease
	{
	public:
		bool begin(int hand,target value,std::uint64_t reference) noexcept
		{
			if (hand<0 || hand>1 || active_) return false;
			hand_=hand;target_=value;reference_=reference;active_=true;return true;
		}
		bool retain(unsigned available,unsigned held,std::uint64_t reference,bool target_live) noexcept
		{
			if (active_ && (reference!=reference_ || !(available&(1u<<hand_)) || !(held&(1u<<hand_)) || !target_live)) cancel();
			return active_;
		}
		void cancel() noexcept {active_=false;hand_=-1;target_={};}
		bool active() const noexcept {return active_;}
		// A notify-only Grip can hold +activate for scripts without owning an
		// object. It must not consume the empty hand needed for belt ammunition.
		bool owns_hand() const noexcept {return active_ && bool(target_);}
		int hand() const noexcept {return hand_;}
		const target& value() const noexcept {return target_;}
	private:
		target target_{};
		std::uint64_t reference_{};
		int hand_{-1};
		bool active_{};
	};
	struct activation_edges {bool press{}, release_before{}, release_after{}, down{};};
	class notification_gate
	{
	public:
		activation_edges consume(std::uint64_t serial,bool held,bool enabled) noexcept
		{
			activation_edges out;
			if (!enabled || serial<seen_)
			{out.release_before=down_;down_=false;seen_=serial;return out;}
			if (serial!=seen_)
			{
				out.release_before=down_;out.press=true;down_=true;seen_=serial;
			}
			if (down_ && !held) {out.release_after=true;down_=false;}
			out.down=down_;return out;
		}
	private:
		std::uint64_t seen_{};
		bool down_{};
	};
}
