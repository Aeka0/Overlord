#pragma once

#include "engine_stereo_binding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_backend_view
{
	inline constexpr std::size_t source_pointer_offset = 0x3238;
	inline constexpr std::size_t copied_view_offset = 0x2BF0;

	enum class gate_state : std::uint8_t
	{
		armed,
		active,
		complete,
		failed,
	};

	struct transaction
	{
		bool active{};
		bool failed{};
		std::uint64_t publication_sequence{};
		std::uintptr_t record{};
		std::uint32_t selected_eye{};
		std::uint32_t copy_calls{};
		std::uint32_t substitutions{};
		alignas(16) std::array<std::uint8_t,
			engine_stereo_view::h2_view_slot_size> natural{};
		alignas(16) std::array<std::uint8_t,
			engine_stereo_view::h2_view_slot_size> selected{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return active;
		}
	};

	struct status
	{
		gate_state state{gate_state::armed};
		std::uint64_t attempts{};
		std::uint64_t completions{};
		std::uint64_t failures{};
		std::uint64_t copy_calls{};
		std::uint64_t substitutions{};
		std::uint64_t foreign_copy_bypasses{};
		std::uint64_t destination_mismatches{};
		std::uint64_t record_mutations{};
		std::uint64_t dispatch_misses{};
		std::uint64_t latest_publication_sequence{};
		std::uintptr_t latest_record{};
	};

	using h2_copy_fn = void(*)(void* backend_state);

	// Claim the first valid backend-local stereo publication in this process for
	// one bounded left-eye view-copy proof. The transaction never owns H2 memory.
	[[nodiscard]] bool begin(transaction& output,
		const engine_stereo_binding::backend_claim& claim,
		std::uintptr_t record) noexcept;

	// Preserve H2's source pointer, replace it only for the exact 0x170-byte copy,
	// call the original leaf once, then restore the pointer before returning.
	void invoke_copy(transaction& active, void* backend_state,
		h2_copy_fn original) noexcept;

	// Verify the natural record stayed byte-identical through command dispatch.
	void end(transaction& active, const void* record,
		bool command_dispatch_returned) noexcept;

	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] const char* to_string(gate_state state) noexcept;
	// Control-plane/test only. Refuses to reset an executing backend transaction.
	[[nodiscard]] bool reset() noexcept;
}
