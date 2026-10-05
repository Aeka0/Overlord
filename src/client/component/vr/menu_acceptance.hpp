#pragma once
#include "controller_input.hpp"

namespace vr::menu_surface
{
	inline bool accepts_without_pointer(bool entry_screen,bool video,unsigned menus) noexcept
	{
		// Frontend menus can play a background movie. They must keep normal
		// ray navigation; only a movie without a menu stack owns direct Enter.
		return entry_screen||(video&&!menus);
	}
	// Native entry/briefing confirmation has no clickable target. Preserve a
	// held trigger for the native skip timer; neutral-arm each hand on recovery.
	class acceptance
	{
		std::uint64_t context_{},reference_{},continuity_{};
		std::array<std::uint64_t,2> generation_{};
		std::array<bool,2> armed_{};
	public:
		void reset() noexcept {*this={};}
		bool held(const controller_input::frame& f,std::uint64_t context,bool allowed,
			controller_input::clock::time_point now) noexcept
		{
			if(!allowed||!context||!f.sequence||!f.focused||now<f.sampled_at||
				now-f.sampled_at>std::chrono::milliseconds(150)){reset();return false;}
			if(context_!=context||reference_!=f.reference_generation||continuity_!=f.continuity_generation)
			{reset();context_=context;reference_=f.reference_generation;continuity_=f.continuity_generation;}
			bool down{};
			for(unsigned h=0;h<2;++h)
			{
				const auto& b=f.trigger[h];
				if(!b.active||generation_[h]!=b.generation){armed_[h]=false;generation_[h]=b.generation;}
				if(!b.active)continue;
				if(!b.down)armed_[h]=true;
				down|=armed_[h]&&b.down;
			}
			return down;
		}
	};
}
