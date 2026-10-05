#pragma once
#include "continuous_view_angles.hpp"
#include "pose_filter.hpp"

namespace vr::game_view
{
	// Remote vehicles consume relative look like native mouse input. Preserve
	// their starting aim, clamp authority and authored camera rotation.
	class remote_view
	{
		struct sample {int time{};std::uint64_t epoch{},reference{};pose_filter::matrix head{};};
		std::array<sample,64> history_{};
		std::size_t next_{},count_{};
		std::uint64_t epoch_{},reference_{};
		std::int64_t at_{};
		continuous_angles previous_{};
		continuous_angles consumed_{};
		std::array<float,2> remaining_{};
		int command_time_{};
		float roll_origin_{};
		bool seeded_{},pending_{},wire_{};
		static pose_filter::matrix axis(continuous_angles angles)noexcept
		{
			const float p=-angles.pitch*pose_filter::radians,y=angles.yaw*pose_filter::radians,r=angles.roll*pose_filter::radians;
			const float cp=std::cos(p),sp=std::sin(p),cy=std::cos(y),sy=std::sin(y),cr=std::cos(r),sr=std::sin(r);
			return {{{cp*cy,cp*sy,-sp},{-sy*cr+sp*cy*sr,cy*cr+sp*sy*sr,cp*sr},
				{sy*sr+sp*cy*cr,-cy*sr+sp*sy*cr,cp*cr}}};
		}
	public:
		bool active()const noexcept{return epoch_!=0;}
		void suspend()noexcept{seeded_=pending_=false;remaining_={};}
		bool apply(float& pitch,float& yaw,continuous_angles head,std::uint64_t epoch,std::uint64_t reference,std::int64_t now)noexcept
		{
			if(!epoch || !reference || !std::isfinite(pitch) || !std::isfinite(yaw) ||
				!std::isfinite(head.pitch) || !std::isfinite(head.yaw) || !std::isfinite(head.roll))return false;
			if(epoch_!=epoch || wire_){*this={};epoch_=epoch;}
			if(!seeded_ || reference_!=reference || now<at_ || now-at_>150)
			{roll_origin_=head.roll;count_=next_=0;}
			else
			{
				pitch=std::remainder(pitch-std::remainder(head.pitch-previous_.pitch,360.f),360.f);
				yaw=std::remainder(yaw+std::remainder(head.yaw-previous_.yaw,360.f),360.f);
			}
			consumed_=previous_=head;reference_=reference;at_=now;seeded_=pending_=true;return true;
		}
		bool apply_control(std::array<std::int8_t,2>& command,continuous_angles head,std::uint64_t epoch,std::uint64_t reference,
			std::int64_t now,int command_time,std::array<float,2> rates,std::array<float,2> stick)noexcept
		{
			if(!epoch || !reference || !std::isfinite(head.pitch) || !std::isfinite(head.yaw) || !std::isfinite(head.roll))return false;
			for(unsigned i=0;i<2;++i)if(!std::isfinite(rates[i]) || rates[i]<=0 || rates[i]>720 ||
				!std::isfinite(stick[i]) || std::abs(stick[i])>1.001f)return false;
			if(epoch_!=epoch || !wire_){*this={};epoch_=epoch;wire_=true;}
			const auto elapsed=std::int64_t(command_time)-command_time_;
			const bool seed=!seeded_ || reference_!=reference || now<at_ || now-at_>150 || elapsed<0 || elapsed>150;
			if(seed){consumed_=head;remaining_={};roll_origin_=head.roll;count_=next_=0;}
			else
			{
				remaining_[0]=std::remainder(remaining_[0]+std::remainder(head.pitch-previous_.pitch,360.f),360.f);
				remaining_[1]=std::remainder(remaining_[1]+std::remainder(head.yaw-previous_.yaw,360.f),360.f);
			}
			const auto pack=[](float value){return static_cast<std::int8_t>(std::floor(std::clamp(value,-1.f,1.f)*127.f+.5f));};
			for(unsigned i=0;i<2;++i)
			{
				// Native CL_RemoteControlMove negates both non-inverted gamepad
				// look axes. Logical stick up/right are not positive wire values.
				const auto base=pack(float(command[i])/127.f-stick[i]);
				command[i]=base;
				if(seed || !elapsed)continue;
				const float step=rates[i]*float(elapsed)*.001f,sign=i?-1.f:1.f;
				command[i]=pack(float(base)/127.f+sign*remaining_[i]/step);
				const float delivered=sign*float(int(command[i])-int(base))/127.f*step;
				remaining_[i]-=delivered;
				float& angle=i?consumed_.yaw:consumed_.pitch;angle=std::remainder(angle+delivered,360.f);
			}
			previous_=head;reference_=reference;at_=now;command_time_=command_time;seeded_=pending_=true;return true;
		}
		void record(int time)noexcept
		{
			if(!pending_)return;
			pending_=false;
			if(count_ && time<history_[(next_+history_.size()-1)%history_.size()].time)
			{count_=next_=0;seeded_=false;}
			auto consumed=consumed_;consumed.roll=roll_origin_;
			history_[next_]={time,epoch_,reference_,axis(consumed)};
			next_=(next_+1)%history_.size();count_=std::min(count_+1,history_.size());
		}
		pose_filter::matrix correction(std::uint64_t epoch,std::uint64_t reference,int time,const pose_filter::matrix& head)const noexcept
		{
			if(!pose_filter::valid({{},head}))return pose_filter::identity;
			for(std::size_t n=0;n<count_;++n)
			{
				const auto& value=history_[(next_+history_.size()-1-n)%history_.size()];
				if(value.time!=time)continue;
				if(value.epoch!=epoch || value.reference!=reference)return pose_filter::identity;
				// Only the unconsumed tracking delta is layered over native aim.
				// Native pitch/yaw already contain the recorded HMD command.
				return pose_filter::multiply(head,pose_filter::transpose(value.head));
			}
			return pose_filter::identity;
		}
	};
}
