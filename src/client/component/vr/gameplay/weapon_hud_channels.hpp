#pragma once

namespace vr::gameplay::weapon_hud
{
	enum class feed : unsigned { primary, underbarrel };
	inline constexpr unsigned hand_count=2, feed_count=2, source_count=hand_count*feed_count;
	// Fixed tree/capture order: primary left/right, then secondary left/right.
	constexpr unsigned source_index(unsigned hand,feed channel) noexcept
	{return hand<hand_count && unsigned(channel)<feed_count ? unsigned(channel)*hand_count+hand : source_count;}
	constexpr unsigned source_hand(unsigned index) noexcept {return index%hand_count;}
	constexpr feed source_feed(unsigned index) noexcept {return static_cast<feed>(index/hand_count);}
}
