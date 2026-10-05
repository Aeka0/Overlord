#pragma once
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>

namespace vr::gameplay::weapons::native_ammunition::reload_layout
{
	// H2 WeaponDef. The adjacent flags have different meanings: partial reload
	// permission does not identify a segmented (individual-round) feed.
	inline constexpr size_t capacity_offset=0x6f0, add_offset=0xb34;
	inline constexpr size_t no_partial_offset=0xe95, segmented_offset=0xe96;
	inline constexpr size_t extent=segmented_offset+1;
	struct shape { int capacity{},add{};bool no_partial{},segmented{}; };
	inline std::optional<shape> decode(std::span<const std::byte> bytes) noexcept
	{
		if(bytes.size()<extent)return std::nullopt;
		shape out;
		std::memcpy(&out.capacity,bytes.data()+capacity_offset,sizeof(out.capacity));
		std::memcpy(&out.add,bytes.data()+add_offset,sizeof(out.add));
		out.no_partial=bytes[no_partial_offset]!=std::byte{};
		out.segmented=bytes[segmented_offset]!=std::byte{};
		return out;
	}
}
