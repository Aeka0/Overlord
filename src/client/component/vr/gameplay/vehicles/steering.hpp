#pragma once
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "../../controller_input.hpp"
#include "../../pose_filter.hpp"

namespace vr::gameplay::vehicles
{
	// Metres in the transported tracking reference. Native grip spacing supplies
	// the lever radius; physical tracking-up supplies the driver's steering plane.
	struct steering_geometry
	{
		hands::vec pivot{},axis{0,0,1};
		std::array<hands::vec,2> contacts{};
	};
	inline steering_geometry handle_control_geometry(hands::anchor handle,
		const std::array<hands::anchor,2>& wrists,float units,hands::vec up={0,0,1}) noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		steering_geometry result;
		if(!std::isfinite(units) || units<=0 || !std::isfinite(length(up)) || length(up)<.1f)return {};
		for(unsigned h=0;h<2;++h)result.contacts[h]=scale(compose(handle,wrists[h]).position,1/units);
		result.pivot=scale(add(result.contacts[0],result.contacts[1]),.5f);result.axis=unit(up);
		// The authored stem is below/forward of the palms. Using it as a physical
		// single-hand pivot couples inward/vertical arm motion into steering.
		auto left=sub(vec{0,1,0},scale(result.axis,dot(vec{0,1,0},result.axis)));
		if(length(left)<.1f)left=cross(result.axis,{1,0,0});
		const float radius=length(sub(result.contacts[0],result.contacts[1]))*.5f;
		left=scale(unit(left),radius);
		result.contacts={add(result.pivot,left),sub(result.pivot,left)};
		return result;
	}
	class handle_steering
	{
		using vec=hands::vec;using clock=controller_input::clock;
		unsigned held_{};
		steering_geometry geometry_{};
		vec pivot_{},base_vector_{};
		float base_angle_{},raw_angle_{},angle_{},value_{};
		bool rebase_{},sample_valid_{},engaged_{},paired_center_{};
		clock::time_point at_{};
		pose_filter::filter filter_;
		std::uint64_t filter_sequence_{};
		static constexpr float radians=pose_filter::radians;
		static bool finite(vec p) noexcept
		{for(float v:p)if(!std::isfinite(v))return false;return true;}
		vec project(vec p)const noexcept
		{return hands::sub(p,hands::scale(geometry_.axis,hands::dot(p,geometry_.axis)));}
		vec turn(vec p,float degrees)const noexcept
		{
			const float half=-degrees*radians*.5f,s=std::sin(half);
			return hands::rotate({geometry_.axis[0]*s,geometry_.axis[1]*s,geometry_.axis[2]*s,std::cos(half)},p);
		}
		vec direction(const std::array<vec,2>& positions)const noexcept
		{return project(held_==3?hands::sub(positions[0],positions[1]):hands::sub(positions[held_==1?0:1],pivot_));}
		bool usable_direction(vec v)const noexcept
		{
			// A collapsed angle is undefined, but it does not release Grip.
			return finite(v) && hands::length(v)>=(held_==3?.06f:.025f);
		}
		bool rebase(const std::array<vec,2>& positions) noexcept
		{
			if(held_!=3)
			{
				const unsigned h=held_==1?0:1;
				const auto lever=turn(hands::sub(geometry_.contacts[h],geometry_.pivot),raw_angle_);
				pivot_=hands::sub(positions[h],lever);
			}
			else
			{
				const auto middle=hands::scale(hands::add(positions[0],positions[1]),.5f);
				pivot_=middle;
				// The first bilateral grasp defines the user's neutral bar heading.
				// Reusing fixed tracking +Y assumed a symmetric, perfectly aligned seat.
				if(!paired_center_)
				{
					const auto span=direction(positions);
					if(!usable_direction(span)){rebase_=true;return false;}
					const auto lever=hands::scale(span,.5f);
					geometry_.contacts={hands::add(geometry_.pivot,lever),hands::sub(geometry_.pivot,lever)};
					raw_angle_=angle_=value_=0;engaged_=false;filter_.reset();paired_center_=true;
				}
				base_vector_=project(hands::sub(geometry_.contacts[0],geometry_.contacts[1]));
				base_angle_=0;rebase_=!usable_direction(direction(positions));return !rebase_;
			}
			base_vector_=direction(positions);base_angle_=raw_angle_;
			rebase_=!usable_direction(base_vector_);return !rebase_;
		}
	public:
		static constexpr float deadzone_degrees=4.f,engage_degrees=4.5f,full_degrees=30.f;
		static constexpr float response_per_second=8.f;
		void reset()noexcept{*this={};}
		unsigned held()const noexcept{return held_;}
		float value()const noexcept{return value_;}
		float angle()const noexcept{return angle_;}
		float raw_angle()const noexcept{return raw_angle_;}
		vec axis()const noexcept{return geometry_.axis;}
		vec pivot()const noexcept{return pivot_;}
		bool sample_valid()const noexcept{return sample_valid_;}
		vec neutral()const noexcept{return hands::unit(project(hands::sub(geometry_.contacts[0],geometry_.contacts[1])));}
		bool center(const std::array<vec,2>& positions) noexcept
		{
			if(!held_)return false;
			for(unsigned h=0;h<2;++h)if((held_&(1u<<h)) && !finite(positions[h]))return false;
			raw_angle_=angle_=value_=0;engaged_=paired_center_=false;filter_.reset();rebase_=true;
			sample_valid_=rebase(positions);return sample_valid_;
		}
		static float response(float degrees) noexcept
		{
			if(!std::isfinite(degrees))return 0;
			const auto x=std::clamp((std::abs(degrees)-deadzone_degrees)/(full_degrees-deadzone_degrees),0.f,1.f);
			return std::copysign(x*std::sqrt(x),degrees);
		}
		void release(unsigned mask)noexcept
		{
			const unsigned before=held_;held_&=~mask;
			if(!held_){raw_angle_=angle_=value_=0;engaged_=sample_valid_=paired_center_=false;filter_.reset();}
			if(held_!=before)rebase_=true;
		}
		bool grab(unsigned h,const std::array<vec,2>& positions,const steering_geometry& geometry)noexcept
		{
			if(h>1 || !finite(positions[h]) || (held_&(1u<<h)))return false;
			if(!held_)
			{
				const auto n=hands::length(geometry.axis);
				if(!finite(geometry.pivot) || !std::isfinite(n) || n<.99f || n>1.01f)return false;
				for(auto contact:geometry.contacts)if(!finite(contact) || hands::length(hands::sub(contact,geometry.pivot))<.025f)return false;
				geometry_=geometry;geometry_.axis=hands::scale(geometry.axis,1/n);
			}
			held_|=1u<<h;rebase_=true;sample_valid_=rebase(positions);return true;
		}
		float update(const std::array<vec,2>& positions,unsigned valid,clock::time_point now)noexcept
		{
			if(at_!=clock::time_point{} && (now<at_ || now-at_>std::chrono::milliseconds(150))){reset();return 0;}
			const float dt=at_==clock::time_point{}?0:std::clamp(std::chrono::duration<float>(now-at_).count(),0.f,.05f);at_=now;
			for(unsigned h=0;h<2;++h)if((held_&(1u<<h)) && (!(valid&(1u<<h)) || !finite(positions[h])))release(1u<<h);
			if(!held_)return 0;
			if(rebase_)rebase(positions);
			const auto current=direction(positions);sample_valid_=!rebase_ && usable_direction(current);
			if(!sample_valid_)
			{
				// Retain ownership; suppress an undefined angle and rebase on recovery.
				raw_angle_=angle_=value_=0;engaged_=false;filter_.reset();rebase_=true;return 0;
			}
			const auto a=hands::unit(base_vector_),b=hands::unit(current);
			const float delta=-std::atan2(hands::dot(geometry_.axis,hands::cross(a,b)),hands::dot(a,b))/radians;
			raw_angle_=std::clamp(base_angle_+delta,-90.f,90.f);
			if(held_==3)
			{
				// Preserve the latest real hand spacing for a 2->1 transition.
				const auto lever=hands::scale(hands::unit(base_vector_),hands::length(current)*.5f);
				geometry_.contacts={hands::add(geometry_.pivot,lever),hands::sub(geometry_.pivot,lever)};
				pivot_=hands::scale(hands::add(positions[0],positions[1]),.5f);
			}
			// Reuse speed-adaptive pose filtering for the common mechanical angle.
			// Runtime inputs are raw physical wrists, so hand visual filters do not stack here.
			const float half=raw_angle_*radians*.5f;
			const auto filtered=filter_.update({{},pose_filter::rotation({0,0,std::sin(half),std::cos(half)})},
				++filter_sequence_,1,now,100,pose_filter::hand);
			const auto q=pose_filter::quaternion(filtered.orientation);
			angle_=2*std::atan2(q[2],q[3])/radians;
			if(std::abs(angle_)<=deadzone_degrees)engaged_=false;
			else if(std::abs(angle_)>=engage_degrees)engaged_=true;
			const float target=engaged_?response(angle_):0;
			value_+=std::clamp(target-value_,-response_per_second*dt,response_per_second*dt);
			return value_;
		}
	};
	struct driver_input {float steering{},throttle{};};
	inline driver_input physical_driver_input(unsigned held,float steering,const controller_input::frame& input) noexcept
	{
		driver_input result;
		if(!input.focused || input.orientation_settling)return result;
		for(unsigned h=0;h<2;++h)if((held&(1u<<h)) && input.grip[h].valid && input.aim[h].valid && input.squeeze[h].active && input.squeeze[h].down)
		{
			result.steering=std::isfinite(steering)?std::clamp(steering,-1.f,1.f):0;
			if(input.trigger[h].active && input.trigger[h].down)result.throttle=1;
		}
		return result;
	}
	inline float merge_driver_throttle(float stick,float trigger) noexcept
	{return stick<0?stick:std::max(stick,trigger);}
}
