#include <std_include.hpp>

#include "engine_stereo_backend_view.hpp"

#include <atomic>
#include <cstring>

namespace vr::engine_stereo_backend_view
{
	namespace
	{
		std::atomic<gate_state> current_state{gate_state::armed};
		std::atomic_uint64_t attempts{};
		std::atomic_uint64_t completions{};
		std::atomic_uint64_t failures{};
		std::atomic_uint64_t copy_calls{};
		std::atomic_uint64_t substitutions{};
		std::atomic_uint64_t foreign_copy_bypasses{};
		std::atomic_uint64_t destination_mismatches{};
		std::atomic_uint64_t record_mutations{};
		std::atomic_uint64_t dispatch_misses{};
		std::atomic_uint64_t latest_publication_sequence{};
		std::atomic_uintptr_t latest_record{};

		void fail(transaction& active) noexcept
		{
			active.failed = true;
		}
	}

	bool begin(transaction& output,
		const engine_stereo_binding::backend_claim& claim,
		const std::uintptr_t record) noexcept
	{
		if (output || !claim || record == 0 || claim.record != record ||
			!engine_stereo_view::validate_finalized(claim.views))
		{
			return false;
		}

		auto expected = gate_state::armed;
		if (!current_state.compare_exchange_strong(expected, gate_state::active,
			std::memory_order_acq_rel, std::memory_order_acquire))
		{
			return false;
		}

		attempts.fetch_add(1, std::memory_order_relaxed);
		output.active = true;
		output.publication_sequence = claim.publication_sequence;
		output.record = record;
		output.selected_eye = 0;
		std::memcpy(output.natural.data(), reinterpret_cast<const void*>(record),
			output.natural.size());
		output.selected = claim.views.eyes[0].bytes;
		latest_publication_sequence.store(claim.publication_sequence,
			std::memory_order_release);
		latest_record.store(record, std::memory_order_release);

		if (std::memcmp(output.natural.data(), output.selected.data(),
			output.natural.size()) == 0)
		{
			fail(output);
		}
		return true;
	}

	void invoke_copy(transaction& active, void* const backend_state,
		const h2_copy_fn original) noexcept
	{
		if (!active)
		{
			if (original != nullptr) original(backend_state);
			return;
		}

		++active.copy_calls;
		copy_calls.fetch_add(1, std::memory_order_relaxed);
		if (backend_state == nullptr || original == nullptr)
		{
			fail(active);
			if (original != nullptr) original(backend_state);
			return;
		}

		auto* const state = static_cast<std::uint8_t*>(backend_state);
		std::uintptr_t source{};
		std::memcpy(&source, state + source_pointer_offset, sizeof(source));
		if (source != active.record)
		{
			foreign_copy_bypasses.fetch_add(1, std::memory_order_relaxed);
			original(backend_state);
			return;
		}

		const auto replacement = reinterpret_cast<std::uintptr_t>(
			active.selected.data());
		std::memcpy(state + source_pointer_offset, &replacement,
			sizeof(replacement));
		original(backend_state);
		std::memcpy(state + source_pointer_offset, &source, sizeof(source));
		++active.substitutions;
		substitutions.fetch_add(1, std::memory_order_relaxed);

		std::uintptr_t restored{};
		std::memcpy(&restored, state + source_pointer_offset, sizeof(restored));
		if (restored != source || std::memcmp(state + copied_view_offset,
			active.selected.data(), active.selected.size()) != 0)
		{
			destination_mismatches.fetch_add(1, std::memory_order_relaxed);
			fail(active);
		}
	}

	void end(transaction& active, const void* const record,
		const bool command_dispatch_returned) noexcept
	{
		if (!active) return;
		const auto record_stable = record != nullptr &&
			reinterpret_cast<std::uintptr_t>(record) == active.record &&
			std::memcmp(record, active.natural.data(), active.natural.size()) == 0;
		if (!record_stable)
		{
			record_mutations.fetch_add(1, std::memory_order_relaxed);
			fail(active);
		}
		if (!command_dispatch_returned)
		{
			dispatch_misses.fetch_add(1, std::memory_order_relaxed);
			fail(active);
		}
		const auto complete = !active.failed && active.substitutions != 0;
		if (complete)
		{
			completions.fetch_add(1, std::memory_order_relaxed);
			current_state.store(gate_state::complete, std::memory_order_release);
		}
		else
		{
			failures.fetch_add(1, std::memory_order_relaxed);
			current_state.store(gate_state::failed, std::memory_order_release);
		}
		active = {};
	}

	status get_status() noexcept
	{
		return {
			current_state.load(std::memory_order_acquire),
			attempts.load(std::memory_order_acquire),
			completions.load(std::memory_order_acquire),
			failures.load(std::memory_order_acquire),
			copy_calls.load(std::memory_order_acquire),
			substitutions.load(std::memory_order_acquire),
			foreign_copy_bypasses.load(std::memory_order_acquire),
			destination_mismatches.load(std::memory_order_acquire),
			record_mutations.load(std::memory_order_acquire),
			dispatch_misses.load(std::memory_order_acquire),
			latest_publication_sequence.load(std::memory_order_acquire),
			latest_record.load(std::memory_order_acquire),
		};
	}

	const char* to_string(const gate_state state) noexcept
	{
		switch (state)
		{
		case gate_state::armed: return "armed";
		case gate_state::active: return "active";
		case gate_state::complete: return "complete";
		case gate_state::failed: return "failed";
		default: return "unknown";
		}
	}

	bool reset() noexcept
	{
		if (current_state.load(std::memory_order_acquire) == gate_state::active)
		{
			return false;
		}
		attempts.store(0, std::memory_order_relaxed);
		completions.store(0, std::memory_order_relaxed);
		failures.store(0, std::memory_order_relaxed);
		copy_calls.store(0, std::memory_order_relaxed);
		substitutions.store(0, std::memory_order_relaxed);
		foreign_copy_bypasses.store(0, std::memory_order_relaxed);
		destination_mismatches.store(0, std::memory_order_relaxed);
		record_mutations.store(0, std::memory_order_relaxed);
		dispatch_misses.store(0, std::memory_order_relaxed);
		latest_publication_sequence.store(0, std::memory_order_relaxed);
		latest_record.store(0, std::memory_order_relaxed);
		current_state.store(gate_state::armed, std::memory_order_release);
		return true;
	}
}
