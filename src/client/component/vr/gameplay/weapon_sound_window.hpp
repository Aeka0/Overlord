#pragma once
#include "weapon_sound_reference.hpp"
#include "weapon_sound_cues.hpp"
#include <cstdint>

namespace vr::gameplay::weapons
{
	struct resolved_sound_window {bool valid{};unsigned begin_ms{},duration_ms{};}; // duration=0: natural EOF
	inline resolved_sound_window resolve_sound_window(const weapon_sound_cue& cue,sound_part part,const sound_window* window=nullptr)noexcept
	{
		if(window)
		{
			if(part!=sound_part::whole || window->recording!=cue.file || window->begin_ms>=window->end_ms ||
				std::uint64_t(window->end_ms)*cue.rate>std::uint64_t(cue.frames)*1000)return {};
			return {true,window->begin_ms,window->end_ms-window->begin_ms};
		}
		if(part==sound_part::first)return {true,0,cue.split_ms};
		if(part==sound_part::second)return {true,cue.split_ms,0};
		return {};
	}
}
