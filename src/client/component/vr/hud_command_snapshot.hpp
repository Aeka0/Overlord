// Adapted from MWR-VR c9c3f18d823ee6e053f8b512df758681db5ca1bf.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vr::native_hud_capture
{
	// Borrowed metadata for ONE immutable native dispatch. Never cache GPU
	// resources or native command addresses across dispatches.
	struct weapon_command
	{
		std::uint16_t size{};
		std::uint8_t op{}, flags{};
		bool blur{}, border{}, widget{}, visible{}, bounds_valid{};
		std::array<float, 4> bounds{};
	};
	class command_snapshot
	{
		std::vector<weapon_command> entries_;
	public:
		void reset() noexcept { entries_.clear(); }
		template<class Decode>
		const weapon_command* get(std::size_t index, Decode&& decode)
		{
			if (index < entries_.size()) return &entries_[index];
			if (index != entries_.size() || index >= 2048) return nullptr;
			weapon_command entry;
			if (!decode(entry)) return nullptr;
			entries_.push_back(entry);
			return &entries_.back();
		}
	};
}
