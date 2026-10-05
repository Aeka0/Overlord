#pragma once

namespace vr
{
	enum class hand : int { none = -1, left = 0, right = 1 };
	constexpr bool valid_hand(hand value) noexcept
	{
		return value == hand::left || value == hand::right;
	}
}
