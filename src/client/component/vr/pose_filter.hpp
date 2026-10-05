#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace vr::pose_filter
{
	using vec = std::array<float,3>;
	using quat = std::array<float,4>; // x, y, z, w; column-vector rotation.
	using matrix = std::array<vec,3>;
	using clock = std::chrono::steady_clock;
	inline constexpr matrix identity{{{1,0,0},{0,1,0},{0,0,1}}};
	struct pose {vec position{}; matrix orientation{identity};};
	inline vec add(vec a,vec b) noexcept {for(unsigned i=0;i<3;++i)a[i]+=b[i];return a;}
	inline vec sub(vec a,vec b) noexcept {for(unsigned i=0;i<3;++i)a[i]-=b[i];return a;}
	inline vec scale(vec a,float s) noexcept {for(auto& x:a)x*=s;return a;}
	inline float length(vec a) noexcept {return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
	inline matrix transpose(const matrix& m) noexcept
	{matrix out{};for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)out[i][j]=m[j][i];return out;}
	inline vec rotate(const matrix& m,vec v) noexcept
	{vec out{};for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)out[i]+=m[i][j]*v[j];return out;}
	inline matrix multiply(const matrix& a,const matrix& b) noexcept
	{matrix out{};for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)out[i][j]+=a[i][k]*b[k][j];return out;}
	inline bool valid(const pose& p) noexcept
	{
		for(float x:p.position)if(!std::isfinite(x) || std::abs(x)>100000)return false;
		for(auto row:p.orientation)for(float x:row)if(!std::isfinite(x))return false;
		const auto unit=multiply(p.orientation,transpose(p.orientation));
		for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)if(std::abs(unit[i][j]-(i==j?1.f:0.f))>.05f)return false;
		const auto& m=p.orientation;
		return m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+
			m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0])>.95f;
	}
	inline quat normalize(quat q) noexcept
	{float n{};for(float x:q)n+=x*x;if(!std::isfinite(n)||n<1e-12f)return {0,0,0,1};for(auto& x:q)x/=std::sqrt(n);return q;}
	inline quat quaternion(const matrix& m) noexcept
	{
		quat q{};const float trace=m[0][0]+m[1][1]+m[2][2];
		if(trace>0){const float s=2*std::sqrt(trace+1);q={(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s*.25f};}
		else
		{
			unsigned i=0;if(m[1][1]>m[i][i])i=1;if(m[2][2]>m[i][i])i=2;
			const unsigned j=(i+1)%3,k=(i+2)%3;const float s=2*std::sqrt((std::max)(0.f,1+m[i][i]-m[j][j]-m[k][k]));
			if(s<1e-6f)return {0,0,0,1};q[i]=s*.25f;q[j]=(m[i][j]+m[j][i])/s;q[k]=(m[i][k]+m[k][i])/s;q[3]=(m[k][j]-m[j][k])/s;
		}
		return normalize(q);
	}
	inline matrix rotation(quat q) noexcept
	{
		q=normalize(q);const auto x=q[0],y=q[1],z=q[2],w=q[3];
		return {{{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
			{2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},
			{2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}}};
	}
	inline float dot(quat a,quat b) noexcept {float d{};for(unsigned i=0;i<4;++i)d+=a[i]*b[i];return d;}
	inline float angle(quat a,quat b) noexcept {return 2*std::acos(std::clamp(std::abs(dot(a,b)),0.f,1.f));}
	inline quat slerp(quat a,quat b,float t) noexcept
	{
		float d=dot(a,b);if(d<0){for(auto& x:b)x=-x;d=-d;}
		float x=1-t,y=t;
		if(d<.9995f){const float theta=std::acos(std::clamp(d,0.f,1.f));x=std::sin((1-t)*theta)/std::sin(theta);y=std::sin(t*theta)/std::sin(theta);}
		for(unsigned i=0;i<4;++i)a[i]=a[i]*x+b[i]*y;return normalize(a);
	}
	inline pose compose(const pose& a,const pose& b) noexcept
	{return {add(a.position,rotate(a.orientation,b.position)),multiply(a.orientation,b.orientation)};}
	inline pose inverse(const pose& p) noexcept
	{const auto r=transpose(p.orientation);return {rotate(r,scale(p.position,-1)),r};}
	inline pose correction(const pose& raw,const pose& filtered) noexcept {return compose(filtered,inverse(raw));}
	struct parameters {float half_life,position_limit,angle_limit,speed_reference,angular_reference;};
	inline constexpr float radians=.017453292519943295f;
	inline constexpr parameters head{.015f,.002f,.25f*radians,.05f,30*radians};
	inline constexpr parameters hand{.045f,.015f,2*radians,.25f,120*radians};
	inline constexpr parameters desktop{.120f,0,30*radians,1,180*radians};
	class filter
	{
		pose output_{},previous_{};clock::time_point at_{};std::uint64_t sequence_{},epoch_{};
		float strength_{};bool initialized_{};
	public:
		void reset() noexcept {*this={};}
		pose update(const pose& raw,std::uint64_t sequence,std::uint64_t epoch,clock::time_point at,
			float strength,const parameters& p) noexcept
		{
			if(!std::isfinite(strength) || strength<=0){reset();return raw;}
			if(!valid(raw)){reset();return raw;}
			strength=std::clamp(strength,0.f,100.f);
			const auto seed=[&]{output_=previous_=raw;at_=at;sequence_=sequence;epoch_=epoch;strength_=strength;initialized_=true;return raw;};
			if(!initialized_ || epoch!=epoch_ || strength!=strength_ || sequence<sequence_ || at<at_ || at-at_>std::chrono::milliseconds(150))return seed();
			if(sequence==sequence_)return output_;
			if(at==at_)return seed();
			const float dt=std::chrono::duration<float>(at-at_).count();
			const auto raw_q=quaternion(raw.orientation),last_q=quaternion(previous_.orientation);
			const float v=length(sub(raw.position,previous_.position))/dt/p.speed_reference;
			const float w=angle(last_q,raw_q)/dt/p.angular_reference;
			const float half=p.half_life*(strength/100)/(1+v*v+w*w);
			const float alpha=half>1e-6f ? -std::expm1(-.69314718056f*dt/half) : 1.f;
			output_.position=add(output_.position,scale(sub(raw.position,output_.position),alpha));
			auto q=slerp(quaternion(output_.orientation),raw_q,alpha);
			const auto lag=sub(output_.position,raw.position);const float distance=length(lag);
			if(distance>p.position_limit)output_.position=add(raw.position,scale(lag,p.position_limit/distance));
			const float turn=angle(q,raw_q);if(turn>p.angle_limit)q=slerp(raw_q,q,p.angle_limit/turn);
			output_.orientation=rotation(q);previous_=raw;at_=at;sequence_=sequence;return output_;
		}
	};
}
