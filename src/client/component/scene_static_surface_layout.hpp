#pragma once
#include "scene_surface_layout.hpp"
#include <array>
#include <cstring>

namespace scene_static_surfaces
{
	// Separate from the skinned/DObj surface arena and its packed 16-bit IDs.
	// Static-model lists publish raw begin pointers and byte counts to each view.
	inline constexpr std::uint32_t native_offset=0xd55700,native_capacity=0xd2000;
	inline constexpr std::size_t capacity=4*native_capacity;
	inline constexpr std::uintptr_t budget_address=0x140990B78;
	inline constexpr unsigned transparent_region=3,transparent_bytes=4096;
	// Per camera region: output bytes per surface type, temporary sorting count.
	inline constexpr std::array<std::array<std::uint16_t,2>,14> native_budgets{{
		{16384,512},{1024,128},{1024,128},{1024,128},{1024,256},{0,0},{0,0},
		{0,0},{16384,512},{0,0},{0,0},{0,0},{0,0},{0,0}}};
	enum class operand_kind {arena_member,stack_base};
	struct operand
	{
		std::uintptr_t address;
		std::array<std::uint8_t,8> bytes;
		std::uint8_t size=7,field=3;
		operand_kind kind=operand_kind::arena_member;
	};
	// Eleven base references, two base+0x2000 references, and two inverse
	// references in the shadow initializers. The latter recover stack sorting
	// pointers from the arena displacement; relocating only positive operands
	// shifts those pointers AND the output ends by the arena relocation delta.
	inline constexpr std::array<operand,15> operands{{
		{0x1407100BA,{0x48,0x8d,0x8a,0x00,0x57,0xd5,0x00}},
		{0x140710D82,{0x49,0x8d,0x88,0x00,0x57,0xd5,0x00}},
		{0x140711515,{0x48,0x8d,0x8a,0x00,0x57,0xd5,0x00}},
		{0x1407115E3,{0x48,0x8d,0x8a,0x00,0x57,0xd5,0x00}},
		{0x1407116AF,{0x4c,0x8d,0x8a,0x00,0x57,0xd5,0x00}},
		{0x1407116B9,{0x4c,0x8d,0x9a,0x00,0x77,0xd5,0x00}},
		{0x1407116CD,{0x4c,0x8d,0x94,0x05,0x80,0x01,0x2b,0xff},8,4,operand_kind::stack_base},
		{0x140711D43,{0x48,0x8d,0x8a,0x00,0x57,0xd5,0x00}},
		{0x140712287,{0x48,0x8d,0x8a,0x00,0x57,0xd5,0x00}},
		{0x140712351,{0x48,0x8d,0x8a,0x00,0x57,0xd5,0x00}},
		{0x140712B32,{0x48,0x8d,0x8a,0x00,0x57,0xd5,0x00}},
		{0x140712C02,{0x48,0x8d,0x8a,0x00,0x57,0xd5,0x00}},
		{0x140712CCC,{0x4c,0x8d,0x8a,0x00,0x57,0xd5,0x00}},
		{0x140712CD6,{0x4c,0x8d,0x9a,0x00,0x77,0xd5,0x00}},
		{0x140712CEA,{0x4c,0x8d,0x94,0x05,0x30,0x01,0x2b,0xff},8,4,operand_kind::stack_base},
	}};
	inline std::optional<std::uint32_t> relocate(const operand& item,std::uintptr_t displacement)noexcept
	{
		if(item.size>item.bytes.size() || item.field+4>item.size ||
			!displacement || displacement>INT32_MAX || displacement%4)return {};
		std::int32_t original{};std::memcpy(&original,item.bytes.data()+item.field,4);
		if(item.kind==operand_kind::stack_base)
		{
			// Native LEAs cancel the arena base to recover RBP+0x5880/0x5830.
			const auto stack_offset=std::int64_t{original}+native_offset;
			if(original>=0 || stack_offset<0)return {};
			const auto value=stack_offset-static_cast<std::int64_t>(displacement);
			if(value<INT32_MIN || value>INT32_MAX)return {};
			return static_cast<std::uint32_t>(value);
		}
		if(original<static_cast<std::int32_t>(native_offset) || original>=native_offset+native_capacity)return {};
		const auto extra=original-native_offset;
		if(displacement>INT32_MAX-extra)return {};
		return static_cast<std::uint32_t>(displacement+extra);
	}
}
