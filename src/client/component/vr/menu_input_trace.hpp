#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace vr::native_menu
{
	enum class input_stage {unavailable,gesture,toggle,accept,waiting_surface,duplicate,physical,delivered};
	struct input_sample
	{
		std::uint64_t tick{},session{},revision{},sequence{},continuity{},physical{};
		std::uint64_t trigger_generation[2]{},trigger_presses[2]{};
		std::uint32_t input_age{},pointer_age{},flags{},count{},hand{};
		int overlay_error{};
		input_stage stage{};
		std::uint64_t mouse_polls{},mouse_moves{},mouse_suppressed{},key_events{};
	};
	class input_trace
	{
	public:
		static constexpr std::uint64_t duration=30000,interval=100;
		struct window
		{
			std::array<input_sample,512> rows{};std::size_t count{};
			std::uint64_t start{},last{};bool dirty{};
			void add(input_sample row) noexcept
			{
				if(!start||row.tick<start||row.tick-start>duration||count==rows.size()||(count&&row.tick-last<interval))return;
				rows[count++]=row;last=row.tick;dirty=true;
			}
		};
		window startup{},recovery{};
	private:
		std::uint64_t unavailable_since_{};
	public:
		void record(input_sample row,bool enabled,bool fresh) noexcept
		{
			if(!enabled)return;
			if(!startup.start)startup.start=row.tick;
			if(!fresh){if(!unavailable_since_)unavailable_since_=row.tick;}
			else if(unavailable_since_)
			{
				if(row.tick>=unavailable_since_&&row.tick-unavailable_since_>=500&&
					(!recovery.start||row.tick-recovery.start>duration))
				{recovery={};recovery.start=row.tick;}
				unavailable_since_=0;
			}
			startup.add(row);recovery.add(row);
		}
	};
}
