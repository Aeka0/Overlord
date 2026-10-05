#pragma once
#include "engine_stereo_view.hpp"
#include <array>
#include <type_traits>

namespace vr
{
	// Bounded sidecar for optional presentation, not a source of scene identity.
	// Caller serializes short copies. A miss never substitutes the newest entry.
	template<class T, std::size_t Capacity = 64> class scene_snapshot_cache
	{
		static_assert(Capacity > 0 && std::is_trivially_copyable_v<T>);
		struct entry
		{
			std::uint64_t pair{}, publication{};
			std::array<float, 12> camera{};
			T value{};
		};
		std::array<entry, Capacity> entries_{};
	public:
		bool put(const engine_stereo_view::slot_pair& views, const T& value) noexcept
		{
			const auto& left = views.eyes[0]; const auto& right = views.eyes[1];
			if (!left.pair_id || left.pair_id != right.pair_id || left.publication != right.publication) return false;
			entries_[left.pair_id % Capacity] = {left.pair_id, left.publication, views.natural_camera, value};
			return true;
		}
		bool get(const engine_stereo_view::slot_pair& views, T& value) const noexcept
		{
			const auto& left = views.eyes[0]; const auto& right = views.eyes[1];
			const auto& entry = entries_[left.pair_id % Capacity];
			if (!left.pair_id || left.pair_id != right.pair_id || left.publication != right.publication ||
				entry.pair != left.pair_id || entry.publication != left.publication || entry.camera != views.natural_camera) return false;
			value = entry.value; return true;
		}
	};
}
