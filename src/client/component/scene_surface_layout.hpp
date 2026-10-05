#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace scene_surface_storage
{
	// NECESSARY FORMAT CHANGE, not spare cache memory:
	// Two coordinated airport captures hit 313592 / 301376 bytes against the
	// native 262144-byte arena, with 780 / 855 owned model packing failures.
	// The uint16 surfId is a scaled BYTE OFFSET, not a surface ordinal. Raising
	// only the byte limit wraps IDs and renders unrelated memory as geometry.
	// Capacity, index units, hidden-record stride, brush-prefix padding and ALL
	// native encoders/decoders in the checked contract must change together.
	// See docs/scene-surface-storage.md before profiling, refactoring or rollback.
	inline constexpr std::size_t index_unit=8;
	inline constexpr std::size_t capacity=(std::size_t{1}<<16)*index_unit;
	inline constexpr std::size_t hidden_bytes=8;
	inline constexpr std::size_t skinned_bytes=56;
	inline constexpr std::size_t brush_prefix_bytes=32;
	inline constexpr std::size_t brush_surface_bytes=24;
	inline constexpr std::uint32_t native_arena_offset=0x62b700;
	inline constexpr std::uintptr_t native_first_frontend=0x14ef1eb80;
	inline constexpr std::size_t native_frontend_stride=0x1039300;

	// Instruction operands can contain arena+field, not just arena. The initial
	// literal-only relocation left +0x08/+0x28 readers on the old arena and
	// crashed in brush/model rendering. Preserve the verified field displacement.
	constexpr std::optional<std::uint32_t> relocate_operand(unsigned kind,
		std::uint32_t original,std::uintptr_t displacement) noexcept
	{
		if(!displacement || displacement>INT32_MAX || displacement%index_unit)return {};
		if(kind==1)
		{
			if(original<native_arena_offset || original>=native_arena_offset+0x40000)return {};
			const auto value=displacement+(original-native_arena_offset);
			if(value>INT32_MAX)return {};
			return static_cast<std::uint32_t>(value);
		}
		if(kind==2 && original==native_arena_offset/4)
			return static_cast<std::uint32_t>(displacement/index_unit);
		if(kind==3 && original==((std::uint32_t{0}-native_arena_offset)&0x3ffff))
			return (std::uint32_t{0}-static_cast<std::uint32_t>(displacement))&static_cast<std::uint32_t>(capacity-1);
		return {};
	}

	constexpr std::optional<std::uint16_t> encode(std::size_t offset) noexcept
	{
		if(offset>=capacity || offset%index_unit) return {};
		return static_cast<std::uint16_t>(offset/index_unit);
	}
	constexpr std::size_t decode(std::uint16_t id) noexcept {return std::size_t{id}*index_unit;}
	constexpr std::optional<std::size_t> record_bytes(std::int32_t tag) noexcept
	{
		if(tag==-3) return hidden_bytes;
		if(tag>=0) return skinned_bytes;
		const auto bones=-3-std::int64_t{tag};
		if(bones<1 || bones>255) return {};
		return static_cast<std::size_t>(bones+2)*32;
	}
}
