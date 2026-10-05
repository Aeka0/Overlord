#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::native_render_contract
{
	// These offsets belong to the fingerprint-gated H2 renderer build.  Every
	// consumer must retain the executable and owner-thread admission contract.
	inline constexpr std::uintptr_t record_command_stream_offset = 0x570;
	inline constexpr std::uintptr_t record_frontend_offset = 0x1F68;
	inline constexpr std::uintptr_t record_type_offset = 0x1F70;
	inline constexpr std::uintptr_t record_target_0_offset = 0x2C90;
	inline constexpr std::uintptr_t record_target_1_offset = 0x2C94;
	inline constexpr std::uintptr_t record_target_2_offset = 0x2C98;
	inline constexpr std::uintptr_t record_target_selector_offset = 0x2C9C;
	inline constexpr std::uint32_t expected_world_record_type = 4;
	inline constexpr std::uint32_t frontend_record_capacity = 4;
	inline constexpr std::uintptr_t target_registry_base = 0x150FD1700;
	inline constexpr std::uintptr_t target_registry_stride = 0x40;
	inline constexpr std::uint32_t target_registry_capacity = 304;
	inline constexpr std::size_t target_registry_size =
		static_cast<std::size_t>(target_registry_capacity) * target_registry_stride;
	using target_registry_entry = std::array<std::uint8_t, target_registry_stride>;
	inline constexpr std::uint64_t h2_query_recent_generation_count = 8;

}
