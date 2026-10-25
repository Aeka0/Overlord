#pragma once
#include "tracer_style.hpp"
#include <cstddef>
#include <cstring>
#include <span>

namespace vr::gameplay::weapons::tracers
{
	inline constexpr std::size_t entity_limit = 3998;
	inline constexpr bool bullet_event(int event) noexcept { return event >= 0x44 && event <= 0x4d; }
	// G_TempEntity's state is copied to centity +0x128. Only serialized fields
	// are compared: no padding, server pointers, interpolated pose or wall time.
	struct event_key
	{
		std::uint8_t type{}, signature{};
		std::uint16_t entity{}, shooter{}, target{}, flags{};
		std::uint32_t weapon{}, alternate{}, parameter{}, generation{};
		std::array<std::uint32_t, 9> geometry{}; // trBase, original world position, start
		bool operator==(const event_key&) const = default;
	};
	inline event_key key(std::span<const std::byte> state) noexcept
	{
		event_key out;
		if (state.size() < 0x94) return out;
		const auto read = [&]<class T>(std::size_t offset, T& value) { std::memcpy(&value, state.data()+offset, sizeof(value)); };
		read(0, out.type); read(8, out.parameter); read(0x68, out.flags);
		// G_FreeEntity preserves +0x78 across memset and increments it on reuse
		// (0x140517AA5 / 0x140517AF4). This travels in the entity state too.
		read(0x78, out.generation);
		read(0x80, out.weapon); read(0x84, out.alternate); read(0x88, out.signature);
		read(0x8c, out.entity); read(0x8e, out.shooter); read(0x90, out.target);
		std::memcpy(out.geometry.data(), state.data()+0x1c, 24);
		std::memcpy(out.geometry.data()+6, state.data()+0x58, 12);
		return out;
	}
	inline bool valid(const event_key& value) noexcept
	{ return bullet_event(int(value.type)-0x10) && value.entity < entity_limit && value.shooter < entity_limit && value.weapon; }
	struct appearance { style selected{style::native}; unsigned shot{}; };
	struct event_record
	{
		event_key identity{};
		appearance visual{};
		std::uint64_t allocation{};
		bool published{}, consumed{};
	};
	struct delivery { bool matched{}, draw{}; appearance visual{}; };
	template<std::size_t Capacity = entity_limit> class event_bridge
	{
		std::array<event_record, Capacity> entries_{};
	public:
		void clear() noexcept { for (auto& entry : entries_) entry = {}; }
		std::uint64_t allocate(unsigned entity) noexcept
		{
			if (entity >= Capacity) return 0;
			auto& entry = entries_[entity]; const auto serial = entry.allocation + 1;
			entry = {}; entry.allocation = serial ? serial : 1;
			return entry.allocation;
		}
		bool publish(const event_key& identity, std::uint64_t allocation, appearance visual) noexcept
		{
			if (!valid(identity) || identity.entity >= Capacity || !allocation || visual.selected == style::native) return false;
			auto& entry = entries_[identity.entity];
			if (entry.allocation != allocation || entry.published) return false;
			entry.identity = identity; entry.visual = visual; entry.published = true;
			return true;
		}
		delivery consume(const event_key& identity) noexcept
		{
			if (!valid(identity) || identity.entity >= Capacity) return {};
			auto& entry = entries_[identity.entity];
			if (!entry.published || entry.identity != identity) return {};
			const bool draw = !entry.consumed; entry.consumed = true;
			// Replayed events remain owned but cannot produce a second stock tracer.
			return {true, draw, entry.visual};
		}
	};
}
