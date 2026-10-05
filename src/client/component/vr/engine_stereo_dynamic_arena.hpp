#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_dynamic_arena
{
	inline constexpr std::size_t mesh_count = 8;
	inline constexpr std::uintptr_t backend_data_pointer_offset = 0x2BE8ull;
	inline constexpr std::uintptr_t code_trans_offset = 0x5409C0ull;
	inline constexpr std::uintptr_t mesh_stride = 0x58ull;
	inline constexpr std::uintptr_t glass_offset = 0x540B78ull;
	inline constexpr std::uintptr_t mark_offset = 0x540BD0ull;
	inline constexpr std::uintptr_t spark_offset = 0x540C28ull;
	inline constexpr std::uintptr_t index_count_offset = 0x04ull;
	inline constexpr std::uintptr_t index_buffer_pointer_offset = 0x08ull;
	inline constexpr std::uintptr_t index_base_pointer_offset = 0x18ull;
	inline constexpr std::uintptr_t vertex_payload_bytes_offset = 0x24ull;
	inline constexpr std::uintptr_t vertex_buffer_pointer_offset = 0x28ull;

	struct view_copy_scope
	{
		bool camera_view{};
		bool owned_geometry{};
	};
	// Shadow/light views keep their own camera matrices, but consume dynamic
	// geometry from the same frontend arena. Camera identity cannot decide
	// whether their index origins need the paired left/right restoration.
	[[nodiscard]] constexpr view_copy_scope classify_view_copy(std::uintptr_t eye_record,
		std::uintptr_t source_view,std::uintptr_t owner_data,std::uintptr_t backend_data,
		bool geometry_boundary) noexcept
	{
		return {eye_record!=0 && source_view==eye_record,
			geometry_boundary && owner_data!=0 && backend_data==owner_data};
	}

	static_assert(code_trans_offset + 5 * mesh_stride == glass_offset);
	static_assert(glass_offset + mesh_stride == mark_offset);
	static_assert(mark_offset + mesh_stride == spark_offset);

	enum class failure : std::uint8_t
	{
		none,
		invalid_argument,
		backend_unreadable,
		data_identity,
		descriptor_unreadable,
		descriptor_unwritable,
		write,
		verification,
		boundary_capacity,
		boundary_order,
		boundary_count,
	};

	struct index_base_snapshot
	{
		std::uintptr_t data_identity{};
		std::array<std::uintptr_t, mesh_count> index_buffers{};
		std::array<std::uint32_t, mesh_count> index_payload_bytes{};
		std::array<std::uintptr_t, mesh_count> index_bases{};
		std::array<std::uintptr_t, mesh_count> vertex_buffers{};
		std::array<std::uint32_t, mesh_count> vertex_payload_bytes{};
		bool valid{};
	};

	[[nodiscard]] constexpr std::uintptr_t mesh_offset(
		const std::size_t index) noexcept
	{
		if (index < 5) return code_trans_offset + index * mesh_stride;
		if (index == 5) return glass_offset;
		if (index == 6) return mark_offset;
		return spark_offset;
	}

	// Capture the eight backend dynamic-mesh index origins from the exact H2
	// backend data block owned by a view-copy boundary.
	[[nodiscard]] bool capture_frontend_data(std::uintptr_t frontend_data,
		index_base_snapshot& output, failure& error) noexcept;
	[[nodiscard]] bool capture_backend(void* backend_state,
		index_base_snapshot& output, failure& error) noexcept;
	// Require both the backend data identity and every scoped index origin to
	// remain bit-exact.
	[[nodiscard]] bool validate_backend(void* backend_state,
		const index_base_snapshot& expected, failure& error) noexcept;
	// Save H2's current natural state, install the left-record-compatible index
	// origins, and verify all writes before the right owner is allowed to run.
	[[nodiscard]] bool replace(const index_base_snapshot& desired,
		index_base_snapshot& displaced, failure& error) noexcept;
	// As above, but first prove that this exact view-copy backend owns the data
	// block named by desired. This prevents an ordinal match from mutating a
	// foreign arena when H2 changes backend state between subviews.
	[[nodiscard]] bool replace_backend(void* backend_state,
		const index_base_snapshot& desired, index_base_snapshot& displaced,
		failure& error) noexcept;
	// Restore the exact natural post-left state after the right owner returns.
	[[nodiscard]] bool restore(const index_base_snapshot& saved,
		failure& error) noexcept;

	[[nodiscard]] const char* to_string(failure value) noexcept;
#ifdef H2VR_DYNAMIC_ARENA_TESTING
	[[nodiscard]] std::uint64_t validation_queries_for_tests() noexcept;
	[[nodiscard]] std::uint64_t resident_validations_for_tests() noexcept;
#endif
}
