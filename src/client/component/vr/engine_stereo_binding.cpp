#include <std_include.hpp>

#include "engine_stereo_binding.hpp"

#include <array>
#include <atomic>
#include <limits>

namespace vr::engine_stereo_binding
{
	namespace
	{
		enum class entry_state : std::uint32_t
		{
			free,
			publishing,
			published,
			claimed,
		};

		struct alignas(64) publication_entry
		{
			std::atomic<entry_state> state{entry_state::free};
			std::uint64_t sequence{};
			std::uintptr_t frontend{};
			std::uintptr_t record{};
			std::uint32_t record_index{};
			std::uint32_t record_type{};
			std::uint64_t frontend_epoch{};
			std::uint64_t frontend_transaction_id{};
			engine_stereo_view::slot_pair views{};
			// Keep ownership words on distinct cache lines as camera metadata grows.
		};
		static_assert(alignof(publication_entry)==64 && sizeof(publication_entry)%64==0);

		static_assert(std::atomic<entry_state>::is_always_lock_free);
		static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

		std::array<publication_entry, publication_capacity> entries{};
		std::atomic_uint64_t next_sequence{1};
		std::atomic_uint64_t publications{};
		std::atomic_uint64_t claims{};
		std::atomic_uint64_t releases{};
		std::atomic_uint64_t publication_drops{};
		std::atomic_uint64_t mapping_misses{};
		std::atomic_uint64_t invalid_publications{};
		std::atomic_uint64_t release_mismatches{};
		std::atomic_uint64_t active_claims{};
		std::atomic_uint64_t maximum_active_claims{};
		std::atomic_uint64_t latest_publication_sequence{};
		std::atomic_uint64_t owner_camera_matches{};
		std::atomic_uint64_t owner_camera_differences{};
		std::atomic_uint64_t owner_camera_invalid{};
		// First-difference storage is immutable once state reaches 2. No blocking
		// lock on the owner thread and no reader sees the partially written sample.
		std::atomic_uint32_t owner_camera_difference_state{};
		owner_camera_difference first_owner_camera_difference{};
		std::atomic_uint64_t current_scene_matches{};
		std::atomic_uint64_t current_scene_misses{};
		std::atomic_uint64_t superseded_publications{};
		std::atomic_uint64_t current_scene_camera_mismatches{};
		std::atomic_uint64_t current_scene_camera_changes{};

		void update_maximum(std::atomic_uint64_t& destination,
			const std::uint64_t value) noexcept
		{
			auto current = destination.load(std::memory_order_relaxed);
			while (current < value && !destination.compare_exchange_weak(current, value,
				std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		bool valid_publication(const frontend_publication& value) noexcept
		{
			if (value.frontend == 0 || value.record == 0 ||
				value.frontend_epoch == 0 || value.frontend_transaction_id == 0 ||
				!engine_stereo_view::validate_finalized(value.views))
			{
				return false;
			}
			const auto& left = value.views.eyes[0];
			const auto& right = value.views.eyes[1];
			return left.pair_id != 0 && left.pair_id == right.pair_id &&
				left.publication == right.publication && left.output_eye == 0 &&
				right.output_eye == 1 && left.view_eye != right.view_eye;
		}
	}

	bool publish(const frontend_publication& publication) noexcept
	{
		if (!valid_publication(publication))
		{
			invalid_publications.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		for (std::size_t index{}; index < entries.size(); ++index)
		{
			auto expected = entry_state::free;
			auto& entry = entries[index];
			if (!entry.state.compare_exchange_strong(expected, entry_state::publishing,
				std::memory_order_acquire, std::memory_order_relaxed))
			{
				continue;
			}

			const auto sequence = next_sequence.fetch_add(1,
				std::memory_order_relaxed);
			entry.sequence = sequence;
			entry.frontend = publication.frontend;
			entry.record = publication.record;
			entry.record_index = publication.record_index;
			entry.record_type = publication.record_type;
			entry.frontend_epoch = publication.frontend_epoch;
			entry.frontend_transaction_id = publication.frontend_transaction_id;
			entry.views = publication.views;
			entry.state.store(entry_state::published, std::memory_order_release);
			publications.fetch_add(1, std::memory_order_relaxed);
			latest_publication_sequence.store(sequence, std::memory_order_release);
			return true;
		}

		publication_drops.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	bool has_publications() noexcept
	{
		return publications.load(std::memory_order_acquire) != 0;
	}

	backend_claim acquire(const std::uintptr_t record,
		const std::uintptr_t frontend) noexcept
	{
		if (record == 0 || frontend == 0)
		{
			mapping_misses.fetch_add(1, std::memory_order_relaxed);
			return {};
		}

		for (;;)
		{
			std::size_t selected = entries.size();
			auto selected_sequence = (std::numeric_limits<std::uint64_t>::max)();
			for (std::size_t index{}; index < entries.size(); ++index)
			{
				auto& entry = entries[index];
				if (entry.state.load(std::memory_order_acquire) !=
					entry_state::published || entry.record != record ||
					entry.frontend != frontend || entry.sequence >= selected_sequence)
				{
					continue;
				}
				selected = index;
				selected_sequence = entry.sequence;
			}

			if (selected == entries.size())
			{
				mapping_misses.fetch_add(1, std::memory_order_relaxed);
				return {};
			}

			auto& entry = entries[selected];
			auto expected = entry_state::published;
			if (!entry.state.compare_exchange_strong(expected, entry_state::claimed,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				continue;
			}

			backend_claim result;
			result.publication_sequence = entry.sequence;
			result.frontend = entry.frontend;
			result.record = entry.record;
			result.record_index = entry.record_index;
			result.record_type = entry.record_type;
			result.frontend_epoch = entry.frontend_epoch;
			result.frontend_transaction_id = entry.frontend_transaction_id;
			result.views = entry.views;
			result.storage_index = static_cast<std::uint32_t>(selected);
			claims.fetch_add(1, std::memory_order_relaxed);
			const auto active = active_claims.fetch_add(1,
				std::memory_order_acq_rel) + 1;
			update_maximum(maximum_active_claims, active);
			return result;
		}
	}

	backend_claim acquire_current(const void* const natural_record,
		const std::uintptr_t frontend) noexcept
	{
		if (natural_record == nullptr || frontend == 0)
		{
			current_scene_misses.fetch_add(1, std::memory_order_relaxed);
			return {};
		}
		// Bound this acquisition to publications visible before the camera read.
		// A concurrently published next generation is left for its own H2 owner.
		const auto ceiling = latest_publication_sequence.load(std::memory_order_acquire);
		const auto record = reinterpret_cast<std::uintptr_t>(natural_record);
		const auto* const camera = static_cast<const std::uint8_t*>(natural_record) +
			engine_stereo_view::h2_view_origin_offset;
		std::array<float, 12> source{};
		std::memcpy(source.data(), camera, sizeof(source));
		backend_claim result{};
		for (std::size_t index{}; index < entries.size(); ++index)
		{
			auto& entry = entries[index];
			if (entry.state.load(std::memory_order_acquire) != entry_state::published ||
				entry.sequence > ceiling || entry.record != record || entry.frontend != frontend)
			{
				continue;
			}
			auto expected = entry_state::published;
			if (!entry.state.compare_exchange_strong(expected, entry_state::claimed,
				std::memory_order_acq_rel, std::memory_order_acquire)) continue;

			backend_claim candidate{};
			candidate.publication_sequence = entry.sequence;
			candidate.frontend = entry.frontend;
			candidate.record = entry.record;
			candidate.record_index = entry.record_index;
			candidate.record_type = entry.record_type;
			candidate.frontend_epoch = entry.frontend_epoch;
			candidate.frontend_transaction_id = entry.frontend_transaction_id;
			candidate.views = entry.views;
			candidate.storage_index = static_cast<std::uint32_t>(index);
			claims.fetch_add(1, std::memory_order_relaxed);
			update_maximum(maximum_active_claims,
				active_claims.fetch_add(1, std::memory_order_acq_rel) + 1);
			if (!result || candidate.publication_sequence > result.publication_sequence)
				std::swap(result, candidate);
			if (candidate)
			{
				release(candidate);
				superseded_publications.fetch_add(1, std::memory_order_relaxed);
			}
		}
		if (result)
		{
			const auto same_source = std::memcmp(source.data(),
				result.views.natural_camera.data(), sizeof(source)) == 0;
			const auto unchanged = std::memcmp(source.data(), camera, sizeof(source)) == 0;
			if (same_source && unchanged)
			{
				current_scene_matches.fetch_add(1, std::memory_order_relaxed);
				return result;
			}
			if (!same_source)
				current_scene_camera_mismatches.fetch_add(1, std::memory_order_relaxed);
			if (!unchanged)
				current_scene_camera_changes.fetch_add(1, std::memory_order_relaxed);
			release(result);
		}
		current_scene_misses.fetch_add(1, std::memory_order_relaxed);
		return {};
	}

	void release(backend_claim& claim) noexcept
	{
		if (!claim || claim.storage_index >= entries.size())
		{
			if (claim.publication_sequence != 0)
			{
				release_mismatches.fetch_add(1, std::memory_order_relaxed);
			}
			claim = {};
			return;
		}

		auto& entry = entries[claim.storage_index];
		if (entry.sequence != claim.publication_sequence)
		{
			release_mismatches.fetch_add(1, std::memory_order_relaxed);
			claim = {};
			return;
		}

		auto expected = entry_state::claimed;
		if (!entry.state.compare_exchange_strong(expected, entry_state::free,
			std::memory_order_release, std::memory_order_relaxed))
		{
			release_mismatches.fetch_add(1, std::memory_order_relaxed);
			claim = {};
			return;
		}

		releases.fetch_add(1, std::memory_order_relaxed);
		active_claims.fetch_sub(1, std::memory_order_acq_rel);
		claim = {};
	}

	void observe_owner_camera(const backend_claim& claim,
		const void* const natural_record) noexcept
	{
		if (!claim || natural_record == nullptr ||
			claim.record != reinterpret_cast<std::uintptr_t>(natural_record))
		{
			owner_camera_invalid.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		std::array<float, 12> consumed{};
		std::memcpy(consumed.data(), static_cast<const std::uint8_t*>(natural_record) +
			engine_stereo_view::h2_view_origin_offset, sizeof(consumed));
		if (std::memcmp(consumed.data(), claim.views.natural_camera.data(),
			sizeof(consumed)) == 0)
		{
			owner_camera_matches.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		owner_camera_differences.fetch_add(1, std::memory_order_relaxed);
		std::uint32_t expected{};
		if (owner_camera_difference_state.compare_exchange_strong(expected, 1,
			std::memory_order_acq_rel, std::memory_order_relaxed))
		{
			first_owner_camera_difference = {
				claim.publication_sequence, claim.views.eyes[0].pair_id,
				claim.frontend_epoch, claim.frontend_transaction_id,
				claim.frontend, claim.record, claim.views.natural_camera, consumed,
			};
			owner_camera_difference_state.store(2, std::memory_order_release);
		}
	}

	status get_status() noexcept
	{
		status result{
			publications.load(std::memory_order_acquire),
			claims.load(std::memory_order_acquire),
			releases.load(std::memory_order_acquire),
			publication_drops.load(std::memory_order_acquire),
			mapping_misses.load(std::memory_order_acquire),
			invalid_publications.load(std::memory_order_acquire),
			release_mismatches.load(std::memory_order_acquire),
			active_claims.load(std::memory_order_acquire),
			maximum_active_claims.load(std::memory_order_acquire),
			latest_publication_sequence.load(std::memory_order_acquire),
			owner_camera_matches.load(std::memory_order_relaxed),
			owner_camera_differences.load(std::memory_order_relaxed),
			owner_camera_invalid.load(std::memory_order_relaxed),
		};
		if (owner_camera_difference_state.load(std::memory_order_acquire) == 2)
		{
			result.first_owner_camera_difference = first_owner_camera_difference;
		}
		result.current_scene_matches = current_scene_matches.load(std::memory_order_relaxed);
		result.current_scene_misses = current_scene_misses.load(std::memory_order_relaxed);
		result.superseded_publications = superseded_publications.load(std::memory_order_relaxed);
		result.current_scene_camera_mismatches = current_scene_camera_mismatches.load(std::memory_order_relaxed);
		result.current_scene_camera_changes = current_scene_camera_changes.load(std::memory_order_relaxed);
		return result;
	}

	bool reset() noexcept
	{
		for (const auto& entry : entries)
		{
			const auto state = entry.state.load(std::memory_order_acquire);
			if (state == entry_state::publishing || state == entry_state::claimed)
			{
				return false;
			}
		}
		for (auto& entry : entries)
		{
			entry.state.store(entry_state::free, std::memory_order_relaxed);
			entry.sequence = 0;
			entry.frontend = 0;
			entry.record = 0;
			entry.record_index = 0;
			entry.record_type = 0;
			entry.frontend_epoch = 0;
			entry.frontend_transaction_id = 0;
			entry.views = {};
		}
		next_sequence.store(1, std::memory_order_relaxed);
		publications.store(0, std::memory_order_relaxed);
		claims.store(0, std::memory_order_relaxed);
		releases.store(0, std::memory_order_relaxed);
		publication_drops.store(0, std::memory_order_relaxed);
		mapping_misses.store(0, std::memory_order_relaxed);
		invalid_publications.store(0, std::memory_order_relaxed);
		release_mismatches.store(0, std::memory_order_relaxed);
		active_claims.store(0, std::memory_order_relaxed);
		maximum_active_claims.store(0, std::memory_order_relaxed);
		latest_publication_sequence.store(0, std::memory_order_relaxed);
		owner_camera_matches.store(0, std::memory_order_relaxed);
		owner_camera_differences.store(0, std::memory_order_relaxed);
		owner_camera_invalid.store(0, std::memory_order_relaxed);
		current_scene_matches.store(0, std::memory_order_relaxed);
		current_scene_misses.store(0, std::memory_order_relaxed);
		superseded_publications.store(0, std::memory_order_relaxed);
		current_scene_camera_mismatches.store(0, std::memory_order_relaxed);
		current_scene_camera_changes.store(0, std::memory_order_relaxed);
		// Like the ring reset itself, this is allowed only while callers are
		// quiescent (control-plane/tests), never alongside a status reader.
		first_owner_camera_difference = {};
		owner_camera_difference_state.store(0, std::memory_order_relaxed);
		return true;
	}
}
