#pragma once
#include "desktop_mirror_layout.hpp"
#include "pose_filter.hpp"

namespace vr::desktop_mirror
{
	struct camera_sample
	{
		pose_filter::matrix axes{}; // H2 forward/left/up world-space rows.
		pose_filter::clock::time_point at{};
		std::uint64_t epoch{};
		bool valid{};
	};
	struct stabilized_view
	{
		pose_filter::matrix uv_transform{}; // Homogeneous output UV -> source UV.
		crop envelope{};
		float horizontal_fov{}, correction_fraction{1};
		bool valid{};
	};
	inline pose_filter::matrix uv_transform(const engine_stereo_bridge::eye_projection& p,
		const pose_filter::matrix& rotation,float hx,float hy) noexcept
	{
		using namespace pose_filter;
		const matrix rays{{{2*hx,0,-hx},{0,-2*hy,hy},{0,0,1}}};
		const float w=p.tan_right-p.tan_left,h=p.tan_up-p.tan_down;
		const matrix uv{{{1/w,0,-p.tan_left/w},{0,-1/h,p.tan_up/h},{0,0,1}}};
		return multiply(uv,multiply(rotation,rays));
	}
	inline bool contained(const pose_filter::matrix& transform,const crop& bounds,crop* envelope=nullptr) noexcept
	{
		crop box{1,1,0,0};
		for(unsigned i=0;i<4;++i)
		{
			const auto ray=pose_filter::rotate(transform,{float(i&1),float(i>>1),1});
			if(!std::isfinite(ray[2]) || ray[2]<=1e-5f)return false;
			const float u=ray[0]/ray[2],v=ray[1]/ray[2];
			if(!std::isfinite(u)||!std::isfinite(v)||u<bounds.u0||u>bounds.u1||v<bounds.v0||v>bounds.v1)return false;
			box.u0=(std::min)(box.u0,u);box.v0=(std::min)(box.v0,v);box.u1=(std::max)(box.u1,u);box.v1=(std::max)(box.v1,v);
		}
		if(envelope)*envelope=box;return true;
	}
	inline stabilized_view stabilize(const engine_stereo_bridge::eye_projection& p,const crop& base,
		unsigned width,unsigned height,const pose_filter::matrix& source_axes,const pose_filter::matrix& target_axes,
		const crop& protected_crop={}) noexcept
	{
		using namespace pose_filter;
		if(!base||width<2||height<2||!valid({{},source_axes})||!valid({{},target_axes}))return {};
		// Conjugate H2 (forward,left,up) into D3D view (right,up,forward).
		constexpr matrix basis{{{0,0,1},{-1,0,0},{0,1,0}}};
		const auto rotation=multiply(transpose(basis),multiply(multiply(source_axes,transpose(target_axes)),basis));
		const auto q=quaternion(rotation);const quat unit{0,0,0,1};
		const float hx=(base.u1-base.u0)*(p.tan_right-p.tan_left)*.5f*.85f;
		const float hy=(base.v1-base.v0)*(p.tan_up-p.tan_down)*.5f*.85f;
		crop bounds{.5f/width,.5f/height,1-.5f/width,1-.5f/height};
		if(protected_crop){bounds.u0=(std::max)(bounds.u0,protected_crop.u0);bounds.v0=(std::max)(bounds.v0,protected_crop.v0);
			bounds.u1=(std::min)(bounds.u1,protected_crop.u1);bounds.v1=(std::min)(bounds.v1,protected_crop.v1);}
		const auto transform=[&](float t){return uv_transform(p,pose_filter::rotation(slerp(unit,q,t)),hx,hy);};
		if(!contained(transform(0),bounds))return {};
		float low=0,high=1;
		if(contained(transform(1),bounds))low=1;
		else for(unsigned i=0;i<16;++i){const float mid=(low+high)*.5f;if(contained(transform(mid),bounds))low=mid;else high=mid;}
		stabilized_view result;result.uv_transform=transform(low);result.correction_fraction=low;
		result.horizontal_fov=2*std::atan(hx)/radians;
		result.valid=contained(result.uv_transform,bounds,&result.envelope);return result;
	}
}
