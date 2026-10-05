#pragma once
#include "physical_reload_geometry.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	// A complete solid box, with an independently authored orientation inside
	// a rigid object. Whole bodies and optional compound shapes share this query.
	struct contact_box { hands::anchor pose; vec half; };
	inline constexpr size_t max_contact_boxes=2;
	struct box_motion
	{
		hands::anchor frame;
		std::array<contact_box,max_contact_boxes> regions;
		size_t region_count{max_contact_boxes};
	};
	inline bool valid_box_pose(const hands::anchor& pose) noexcept
	{
		float square{};
		for(float v:pose.position)if(!std::isfinite(v) || std::abs(v)>10000)return false;
		for(float v:pose.rotation){if(!std::isfinite(v))return false;square+=v*v;}
		return std::abs(square-1)<.001f;
	}
	inline bool valid(const contact_box& box) noexcept
	{
		for(float v:box.half)if(!std::isfinite(v) || v<=0 || v>10000)return false;
		return valid_box_pose(box.pose);
	}
	inline bool valid(const box_motion& motion) noexcept
	{
		if(!valid_box_pose(motion.frame) || !motion.region_count || motion.region_count>motion.regions.size())return false;
		for(size_t i=0;i<motion.region_count;++i)if(!valid(motion.regions[i]))return false;
		return true;
	}
	inline vec box_point(hands::anchor frame,vec local) noexcept
	{return hands::add(frame.position,hands::rotate(frame.rotation,local));}
	inline vec box_inverse_point(hands::anchor frame,vec point) noexcept
	{return hands::rotate(hands::conjugate(frame.rotation),hands::sub(point,frame.position));}
	struct box_proximity { float distance; vec local; };
	inline box_proximity closest_box(const box_motion& motion,size_t region) noexcept
	{
		if(region>=motion.region_count || region>=motion.regions.size())return {INFINITY,{}};
		const auto& box=motion.regions[region];
		const auto object_point=box_inverse_point(motion.frame,{});
		auto point=box_inverse_point(box.pose,object_point);
		for(size_t i=0;i<3;++i)point[i]=std::clamp(point[i],-box.half[i],box.half[i]);
		const auto local=box_point(box.pose,point);
		return {hands::length(box_point(motion.frame,local)),local};
	}
	inline bool same_regions(const box_motion& a,const box_motion& b) noexcept
	{
		if(a.region_count!=b.region_count || !a.region_count || a.region_count>a.regions.size())return false;
		for(size_t i=0;i<a.region_count;++i)
			if(a.regions[i].pose.position!=b.regions[i].pose.position || a.regions[i].pose.rotation!=b.regions[i].pose.rotation || a.regions[i].half!=b.regions[i].half)return false;
		return true;
	}
	struct box_sweep
	{
		box_motion from,to;
		float half_angle{},sine{},bound{};
		box_sweep(const box_motion& a,const box_motion& b):from(a),to(b)
		{
			float cosine{};for(size_t i=0;i<4;++i)cosine+=from.frame.rotation[i]*to.frame.rotation[i];
			if(cosine<0){for(auto& v:to.frame.rotation)v=-v;cosine=-cosine;}
			half_angle=std::acos(std::clamp(cosine,0.f,1.f));sine=std::sin(half_angle);
			float radius{};for(size_t i=0;i<std::min(to.region_count,to.regions.size());++i)
			{
				const auto& box=to.regions[i];radius=std::max(radius,hands::length(box.pose.position)+hands::length(box.half));
			}
			bound=hands::length(hands::sub(to.frame.position,from.frame.position))+2*half_angle*radius;
		}
		box_motion at(float fraction) const noexcept
		{
			auto out=to;
			out.frame.position=hands::add(from.frame.position,hands::scale(hands::sub(to.frame.position,from.frame.position),fraction));
			const float a=sine>1e-5f ? std::sin((1-fraction)*half_angle)/sine : 1-fraction;
			const float b=sine>1e-5f ? std::sin(fraction*half_angle)/sine : fraction;
			for(size_t i=0;i<4;++i)out.frame.rotation[i]=a*from.frame.rotation[i]+b*to.frame.rotation[i];
			out.frame.rotation=hands::normalize(out.frame.rotation);return out;
		}
		bool contact(size_t region,float radius,vec& local) const noexcept
		{
			if(!valid(from) || !valid(to) || !same_regions(from,to) || region>=to.region_count ||
				!std::isfinite(radius) || radius<=0)return false;
			// Conservative advancement of the solid box against the latch sphere.
			// Translation plus angular corner travel bounds every material point;
			// no step can skip a face crossing. Work and tolerance stay bounded.
			constexpr float tolerance=.00005f; // 0.05 mm numerical contact tolerance.
			float fraction{};
			for(int step=0;step<128;++step)
			{
				const auto proximity=closest_box(at(fraction),region);
				if(proximity.distance<=radius+tolerance){local=proximity.local;return true;}
				if(bound<1e-8f || fraction>=1)return false;
				const float advance=(proximity.distance-radius)/bound;
				if(fraction+advance>1)return false;
				fraction=std::min(1.f,fraction+advance);
			}
			return false;
		}
	};
}
