#pragma once
#include <cmath>
#include <cstdint>
#include <array>
#include <algorithm>

namespace vr::game_view
{
	// The animated camera still owns translation. Keep the tracking frame's
	// world heading independent of native linked-view clamps and rig switches.
	class scripted_view
	{
		std::uint64_t epoch_{}, reference_{};
		std::uint64_t vehicle_epoch_{};
		bool heading_align_entry_{};
		float vehicle_heading_{};
		std::uint64_t authored_epoch_{},authored_source_{};
		float authored_heading_{};int authored_time_{};
		using axis_type=std::array<std::array<float,3>,3>;
		axis_type authored_axis_{};
		bool have_authored_axis_{};
		float base_{}, last_base_{}, last_world_{};
		bool have_last_{}, restored_{}, recorded_{};
		int last_time_{}, release_time_{};
	public:
		float follow_authored(std::uint64_t epoch,std::uint64_t reference,int time,float native_base,float head_yaw,
			std::uint64_t source,const axis_type& axis) noexcept
		{
			bool valid=source!=0;
			for(const auto& row:axis)
			{
				float norm{};for(float x:row){valid&=std::isfinite(x);norm+=x*x;}
				valid&=std::abs(norm-1.f)<.01f;
			}
			const bool continuous=valid && have_authored_axis_ && epoch && epoch_==epoch &&
				authored_epoch_==epoch && authored_source_==source && time>=last_time_ && authored_time_==last_time_;
			float heading{};
			if(continuous)
			{
				// A scripted camera bone can point through the vertical pole. Its
				// forward XY heading then reverses, even for pure pitch. Compare the
				// SAME axis in both poses, choosing the strongest horizontal pair.
				// Pure pitch uses left; pure roll uses forward. Switching axes adds
				// only that pair's delta, never the axes' different absolute headings.
				float best=-1,delta{};
				for(unsigned i=0;i<3;++i)
				{
					const auto& a=authored_axis_[i];const auto& b=axis[i];
					const float score=std::min(a[0]*a[0]+a[1]*a[1],b[0]*b[0]+b[1]*b[1]);
					if(score>best){best=score;delta=std::atan2(a[0]*b[1]-a[1]*b[0],a[0]*b[0]+a[1]*b[1])*57.29577951308232f;}
				}
				heading=std::remainder(authored_heading_+delta,360.f);
			}
			const float result=follow_authored(epoch,reference,time,native_base,head_yaw,valid?source:0,heading);
			authored_axis_=axis;have_authored_axis_=valid;return result;
		}
		float follow_authored(std::uint64_t epoch,std::uint64_t reference,int time,float native_base,float head_yaw,
			std::uint64_t source,float authored_heading) noexcept
		{
			have_authored_axis_=false;
			const bool valid=source && std::isfinite(authored_heading);
			const bool continuous=valid && epoch && epoch_==epoch && authored_epoch_==epoch && authored_source_==source &&
				time>=last_time_ && authored_time_==last_time_;
			// compose owns entry, recenter and command handback. The authored
			// delta is independent of both native clamp output and physical yaw.
			float base=compose(epoch,reference,time,native_base,head_yaw);
			if(continuous)base=std::remainder(base+std::remainder(authored_heading-authored_heading_,360.f),360.f);
			authored_epoch_=epoch;authored_source_=valid?source:0;authored_heading_=valid?authored_heading:0;authored_time_=time;
			follow_native(epoch,reference,time,base+head_yaw,head_yaw);return base;
		}
		float follow_heading(std::uint64_t epoch,std::uint64_t reference,int time,float heading,float head_yaw,bool align_entry) noexcept
		{
			float base=heading-(align_entry?head_yaw:0.f);
			if(vehicle_epoch_==epoch && heading_align_entry_==align_entry && epoch && time>=last_time_)
			{
				base=reference_==reference?last_base_:last_world_-head_yaw;
				base+=std::remainder(heading-vehicle_heading_,360.f);
			}
			vehicle_epoch_=epoch;heading_align_entry_=align_entry;vehicle_heading_=heading;base=std::remainder(base,360.f);
			// Add native vehicle yaw while preserving independent head rotation
			// and entry/recenter continuity; this never writes driving commands.
			follow_native(epoch,reference,time,base+head_yaw,head_yaw);return base;
		}
		float follow_vehicle(std::uint64_t epoch,std::uint64_t reference,int time,float heading,float head_yaw) noexcept
		{return follow_heading(epoch,reference,time,heading,head_yaw,true);}
		float follow_native_yaw(std::uint64_t epoch,std::uint64_t reference,int time,float heading,float head_yaw) noexcept
		{return follow_heading(epoch,reference,time,heading,head_yaw,false);}
		// The rig owns entry admission/deduplication. This only resets the
		// heading bridge and the previous shot's authored-delta history.
		void align_entry(std::uint64_t epoch,std::uint64_t reference,int time,float native_heading,float head_yaw) noexcept
		{
			follow_native(epoch,reference,time,native_heading,head_yaw);
			authored_epoch_=authored_source_=vehicle_epoch_=0;have_authored_axis_=false;
		}
		void follow_native(std::uint64_t epoch,std::uint64_t reference,int time,float heading,float head_yaw) noexcept
		{
			epoch_=epoch;reference_=reference;last_time_=time;last_world_=heading;
			base_=last_base_=std::remainder(heading-head_yaw,360.f);have_last_=true;restored_=recorded_=false;
		}
		bool restore_command(std::uint64_t epoch, std::uint64_t reference,
			float& yaw, float delta_yaw, float head_yaw,float native_head_yaw) noexcept
		{
			if (epoch || !epoch_ || restored_) return false;
			if (reference_!=reference) {base_=std::remainder(last_world_-head_yaw,360.f);reference_=reference;}
			// Camera bookkeeping uses branch-independent physical heading. Native
			// input still needs the continuous Euler yaw paired with its pitch.
			yaw=std::remainder(base_+native_head_yaw-delta_yaw,360.f);
			restored_=true;return true;
		}
		bool restore_command(std::uint64_t epoch,std::uint64_t reference,float& yaw,float delta_yaw,float head_yaw) noexcept
		{return restore_command(epoch,reference,yaw,delta_yaw,head_yaw,head_yaw);}
		void record(int time) noexcept
		{if(restored_ && !recorded_){release_time_=time;recorded_=true;}}
		float compose(std::uint64_t epoch,std::uint64_t reference,int time,float native_base,float head_yaw) noexcept
		{
			if (have_last_ && time<last_time_) *this={};
			if (epoch)
			{
				if (epoch_!=epoch) {base_=have_last_ ? last_base_ : native_base;restored_=recorded_=false;}
				if (epoch_==epoch && reference_!=reference) base_=std::remainder(last_world_-head_yaw,360.f);
				epoch_=epoch;
			}
			else if (recorded_ && time>=release_time_) {epoch_=0;restored_=recorded_=false;}
			else if (epoch_ && reference_!=reference) base_=std::remainder(last_world_-head_yaw,360.f);
			const float result=epoch_ ? base_ : native_base;
			reference_=reference;last_time_=time;last_base_=result;last_world_=result+head_yaw;have_last_=true;
			return result;
		}
		bool owns_camera() const noexcept {return epoch_!=0;}
	};
}
