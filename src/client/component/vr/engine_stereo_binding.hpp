#pragma once

#include "engine_stereo_view.hpp"

#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_binding
{
	inline constexpr std::size_t publication_capacity = 64;

	struct frontend_publication
	{
		std::uintptr_t frontend{};
		std::uintptr_t record{};
		std::uint32_t record_index{};
		std::uint32_t record_type{};
		std::uint64_t frontend_epoch{};
		std::uint64_t frontend_transaction_id{};
		engine_stereo_view::slot_pair views{};
	};

	struct backend_claim
	{
		std::uint64_t publication_sequence{};
		std::uintptr_t frontend{};
		std::uintptr_t record{};
		std::uint32_t record_index{};
		std::uint32_t record_type{};
		std::uint64_t frontend_epoch{};
		std::uint64_t frontend_transaction_id{};
		engine_stereo_view::slot_pair views{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return publication_sequence != 0;
		}

	private:
		friend backend_claim acquire(std::uintptr_t, std::uintptr_t) noexcept;
		friend backend_claim acquire_current(const void*, std::uintptr_t) noexcept;
		friend void release(backend_claim&) noexcept;
		std::uint32_t storage_index{};
	};

	struct owner_camera_difference
	{
		std::uint64_t publication_sequence{};
		std::uint64_t pair_id{};
		std::uint64_t frontend_epoch{};
		std::uint64_t frontend_transaction_id{};
		std::uintptr_t frontend{};
		std::uintptr_t record{};
		std::array<float, 12> published{};
		std::array<float, 12> consumed{};
	};

	struct status
	{
		std::uint64_t publications{};
		std::uint64_t claims{};
		std::uint64_t releases{};
		std::uint64_t publication_drops{};
		std::uint64_t mapping_misses{};
		std::uint64_t invalid_publications{};
		std::uint64_t release_mismatches{};
		std::uint64_t active_claims{};
		std::uint64_t maximum_active_claims{};
		std::uint64_t latest_publication_sequence{};
		std::uint64_t owner_camera_matches{};
		std::uint64_t owner_camera_differences{};
		std::uint64_t owner_camera_invalid{};
		owner_camera_difference first_owner_camera_difference{};
		std::uint64_t current_scene_matches{};
		std::uint64_t current_scene_misses{};
		std::uint64_t superseded_publications{};
		std::uint64_t current_scene_camera_mismatches{};
		std::uint64_t current_scene_camera_changes{};
	};

	// Publish immutable eye VIEW inputs before the natural H2 scene generator can
	// dispatch consumers. The backend must join CPU scene completion separately
	// before cloning the asynchronously produced record. This module never
	// mutates H2 memory or calls D3D/OpenVR.
	[[nodiscard]] bool publish(const frontend_publication& publication) noexcept;
	[[nodiscard]] bool has_publications() noexcept;

	// Claim the oldest publication matching the exact record/frontend identity.
	// The returned eye slots are private backend-local copies.
	[[nodiscard]] backend_claim acquire(std::uintptr_t record,
		std::uintptr_t frontend) noexcept;
	// Sole scene-owner consumer after bootstrap. H2 reuses record addresses;
	// they are not FIFO frame identities. Drain publications already visible at
	// entry for this address, retain the newest one, and require its complete
	// source camera to match the current record before either eye can use it.
	// No older-camera substitution, record write, wait or GPU operation.
	[[nodiscard]] backend_claim acquire_current(const void* natural_record,
		std::uintptr_t frontend) noexcept;
	void release(backend_claim& claim) noexcept;

	// Temporary read-only witness at an admitted owner transaction, before either
	// eye changes H2's record. Caller owns a readable natural view slot. A mismatch
	// records evidence only: no rejection, substitution, GPU work or per-frame log.
	void observe_owner_camera(const backend_claim& claim,
		const void* natural_record) noexcept;

	[[nodiscard]] status get_status() noexcept;
	// Test/control-plane only. Fails while a producer or backend claim is active.
	[[nodiscard]] bool reset() noexcept;
}
