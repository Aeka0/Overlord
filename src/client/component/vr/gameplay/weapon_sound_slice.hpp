#pragma once
#include <algorithm>
#include <cstdint>

namespace vr::gameplay::weapons
{
	// Counts source frames, before pitch/resampling. The native producer runs
	// before the mixer consumes this ring. Trimming its tail cannot expose PCM
	// from the following action, even when the game/main thread stalls.
	struct sound_slice_budget
	{
		bool enabled{};
		std::uint32_t remaining{};
		// Native fill is called again with no decoder while retained PCM drains.
		// A true result is a one-shot request to release a LIVE decoder, not a
		// persistent "slice finished" state. Preserve native null/drain handling.
		template<class Decode> bool fill(bool decoder_live, bool streamed, std::uint32_t capacity,
			std::uint32_t& write, std::uint32_t& count, Decode&& decode)
		{
			if(!decoder_live || streamed){enabled=false;return decode();}
			if(!enabled)return decode();
			if(!remaining){enabled=false;return true;}
			const auto before=count;
			const bool ended=decode();
			const bool clipped=trim(before,capacity,write,count);
			if(ended || clipped)enabled=false;
			return ended || clipped;
		}
		bool trim(std::uint32_t before, std::uint32_t capacity,
			std::uint32_t& write, std::uint32_t& count) noexcept
		{
			if (!enabled || !capacity || before > count || count > capacity) return false;
			const auto produced = count - before;
			const auto kept = std::min(produced, remaining);
			const auto discarded = produced - kept;
			write = (write + capacity - discarded) % capacity;
			count -= discarded;
			remaining -= kept;
			return remaining == 0;
		}
	};
}
