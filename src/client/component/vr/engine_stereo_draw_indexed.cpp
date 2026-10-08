#include <std_include.hpp>
#include "region_capture.hpp"

#include "engine_stereo_draw_indexed.hpp"

#include "engine_stereo_execution.hpp"
#include "engine_stereo_output_merger.hpp"

#include "utils/hook.hpp"
#include "utils/hook_validation.hpp"

#include <atomic>
#include <mutex>

namespace vr::engine_stereo_draw_indexed
{
	namespace
	{
		constexpr std::size_t draw_indexed_vtable_slot = 12;
		constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
		constexpr std::uint64_t fnv_prime = 1099511628211ull;
		using draw_fn = void(__stdcall*)(ID3D11DeviceContext*, UINT, UINT, INT);

		// Context modes can use different DrawIndexed entries, including entries
		// which call each other. Never replace/free a trampoline used by a draw.
		constexpr std::size_t maximum_hook_targets = 16;
		struct hook_entry
		{
			utils::hook::detour hook;
			std::uintptr_t target{};
			std::atomic<draw_fn> original{};
		};
		std::array<hook_entry, maximum_hook_targets> draw_hooks;
		std::atomic_bool live_installed{};
		std::atomic_uintptr_t live_target{}, live_context{};
		std::atomic_uint64_t live_generation{}, live_failures{}, target_changes{};
		std::atomic_uint64_t retained_targets{}, nested_draws{};
		std::uintptr_t rejected_target{}, rejected_context{};
		std::uint64_t rejected_generation{};
		std::uint64_t retired_generation{};
		std::mutex hook_mutex;
		std::mutex report_mutex;
		std::atomic_bool hook_installed{};
		std::atomic_uintptr_t hook_target{};
		std::atomic_uintptr_t expected_context{};
		std::atomic_uint64_t expected_generation{};
		std::atomic_uint64_t hook_failures{};
		std::atomic<gate_state> current_state{gate_state::armed};
		std::atomic_uint64_t attempts{};
		std::atomic_uint64_t completions{};
		std::atomic_uint64_t failures{};
		std::atomic_uint64_t draw_calls{};
		std::atomic_uint64_t context_mismatches{};
		std::atomic_uint64_t thread_mismatches{};
		std::atomic_uint64_t invalid_arguments{};
		std::atomic_uint64_t overflows{};
		std::atomic_uint64_t latest_publication_sequence{};
		std::atomic_uintptr_t latest_record{};
		std::atomic_bool device_invalidated_during_transaction{};
		std::atomic<draw_observer> persistent_draw_observer{};
		std::atomic<draw_copy_observer> copy_observer{};
		report published_report{};
		thread_local transaction* active_transaction{};
		thread_local replay_counter* active_replay{};
		thread_local bool inside_draw{};

		[[nodiscard]] bool is_terminal(const gate_state state) noexcept
		{
			return state == gate_state::complete || state == gate_state::failed;
		}

		static_assert(std::is_trivially_copyable_v<transaction>);
		static_assert(std::is_trivially_copyable_v<report>);

		void clear_transaction(transaction& value) noexcept
		{
			// The complete transaction report is deliberately large. Clear it in
			// place so cleanup never materializes a report-sized stack temporary.
			std::memset(std::addressof(value), 0, sizeof(value));
		}

		void clear_report(report& value) noexcept
		{
			std::memset(std::addressof(value), 0, sizeof(value));
		}

		[[nodiscard]] std::uint64_t qpc() noexcept
		{
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return static_cast<std::uint64_t>(value.QuadPart);
		}

		void hash_bytes(std::uint64_t& hash, const void* const data,
			const std::size_t size) noexcept
		{
			const auto* bytes = static_cast<const std::uint8_t*>(data);
			for (std::size_t index{}; index < size; ++index)
			{
				hash ^= bytes[index];
				hash *= fnv_prime;
			}
		}

		void fail(transaction& active) noexcept
		{
			active.failed = true;
		}

		void observe_draw(transaction& active, ID3D11DeviceContext* const context,
			const UINT index_count, const UINT start_index_location,
			const INT base_vertex_location, const std::uintptr_t caller) noexcept
		{
			if (!active.dispatch_active) return;
			++active.draw_calls;
			draw_calls.fetch_add(1, std::memory_order_relaxed);
			const auto thread_id = GetCurrentThreadId();
			if (thread_id != active.evidence.transaction_thread_id)
			{
				++active.thread_mismatches;
				thread_mismatches.fetch_add(1, std::memory_order_relaxed);
				fail(active);
			}
			const auto expected = expected_context.load(std::memory_order_acquire);
			const auto context_matches = expected != 0 &&
				reinterpret_cast<std::uintptr_t>(context) == expected;
			if (!context_matches)
			{
				++active.context_mismatches;
				context_mismatches.fetch_add(1, std::memory_order_relaxed);
				fail(active);
			}
			if (active.evidence.event_count >= active.evidence.events.size())
			{
				++active.overflows;
				overflows.fetch_add(1, std::memory_order_relaxed);
				fail(active);
				return;
			}

			auto& event = active.evidence.events[active.evidence.event_count++];
			event.sequence = active.evidence.event_count;
			event.timestamp_qpc = qpc();
			event.context = reinterpret_cast<std::uintptr_t>(context);
			event.caller = caller;
			event.thread_id = thread_id;
			event.index_count = index_count;
			event.start_index_location = start_index_location;
			event.base_vertex_location = base_vertex_location;
			event.expected_context = context_matches;
			event.arguments_valid = index_count != 0;
			if (!event.arguments_valid)
			{
				++active.invalid_arguments;
				invalid_arguments.fetch_add(1, std::memory_order_relaxed);
				fail(active);
			}
			if (active.evidence.draw_call_hash == 0)
			{
				active.evidence.draw_call_hash = fnv_offset;
			}
			hash_bytes(active.evidence.draw_call_hash, &event.index_count,
				sizeof(event.index_count));
			hash_bytes(active.evidence.draw_call_hash, &event.start_index_location,
				sizeof(event.start_index_location));
			hash_bytes(active.evidence.draw_call_hash, &event.base_vertex_location,
				sizeof(event.base_vertex_location));
		}

		[[nodiscard]] boundary_phase classify_boundary_phase(
			const transaction& active) noexcept
		{
			if (active.dispatch_active) return boundary_phase::dispatch;
			if (active.dispatch_returned) return boundary_phase::after_dispatch;
			return boundary_phase::before_dispatch;
		}

		void observe_boundary(transaction& active,
			ID3D11DeviceContext* const context, const UINT index_count,
			const UINT start_index_location, const INT base_vertex_location,
			const std::uintptr_t caller) noexcept
		{
			++active.evidence.boundary_draw_calls;
			const auto binding = engine_stereo_output_merger::get_current_binding(context);
			const auto phase = classify_boundary_phase(active);
			auto* group = active.evidence.boundary_group_count == 0
				? nullptr : &active.evidence.boundary_groups[
					active.evidence.boundary_group_count - 1];
			if (group == nullptr || group->execution_phase != phase ||
				group->binding_sequence != binding.sequence ||
				group->render_target != binding.render_target_0 ||
				group->depth_stencil != binding.depth_stencil)
			{
				if (active.evidence.boundary_group_count >=
					active.evidence.boundary_groups.size())
				{
					++active.evidence.boundary_group_overflows;
					return;
				}
				group = &active.evidence.boundary_groups[
					active.evidence.boundary_group_count++];
				group->execution_phase = phase;
				group->binding_sequence = binding.sequence;
				group->render_target = binding.render_target_0;
				group->depth_stencil = binding.depth_stencil;
				group->render_target_count = binding.render_target_count;
				group->target_id = binding.target_id;
				group->first_caller = caller;
				group->first_start_index = start_index_location;
				group->first_base_vertex = base_vertex_location;
			}
			++group->draw_calls;
			group->index_count += index_count;
			group->last_caller = caller;
			group->last_start_index = start_index_location;
			group->last_base_vertex = base_vertex_location;
			if (group->draw_call_hash == 0) group->draw_call_hash = fnv_offset;
			hash_bytes(group->draw_call_hash, &index_count, sizeof(index_count));
			hash_bytes(group->draw_call_hash, &start_index_location,
				sizeof(start_index_location));
			hash_bytes(group->draw_call_hash, &base_vertex_location,
				sizeof(base_vertex_location));
		}

		void observe_replay(replay_counter& active,
			ID3D11DeviceContext* const context, const UINT index_count,
			const UINT start_index_location, const INT base_vertex_location,
			const std::uintptr_t caller) noexcept
		{
			++active.draw_calls;
			if (reinterpret_cast<std::uintptr_t>(context) != active.expected_context)
			{
				++active.context_mismatches;
			}
			if (GetCurrentThreadId() != active.thread_id)
			{
				++active.thread_mismatches;
			}
			if (index_count == 0)
			{
				++active.invalid_arguments;
			}
			if (active.event_count < active.events.size())
			{
				auto& event = active.events[active.event_count++];
				event.caller = caller;
				event.index_count = index_count;
				event.start_index_location = start_index_location;
				event.base_vertex_location = base_vertex_location;
			}
			else
			{
				++active.overflows;
			}
			if (active.draw_call_hash == 0) active.draw_call_hash = fnv_offset;
			hash_bytes(active.draw_call_hash, &index_count, sizeof(index_count));
			hash_bytes(active.draw_call_hash, &start_index_location,
				sizeof(start_index_location));
			hash_bytes(active.draw_call_hash, &base_vertex_location,
				sizeof(base_vertex_location));
			if (active.draw_shape_hash == 0) active.draw_shape_hash = fnv_offset;
			hash_bytes(active.draw_shape_hash, &index_count, sizeof(index_count));
		}

		template<std::size_t Index>
		void __stdcall draw_stub(ID3D11DeviceContext* const context,
			const UINT index_count, const UINT start_index_location,
			const INT base_vertex_location)
		{
			const auto original = draw_hooks[Index].original.load(std::memory_order_acquire);
			if (original == nullptr) return;
			if (inside_draw)
			{
				// Preserve nested native draws and target forwarding, but do not
				// count/capture the same draw or its HUD replay a second time.
				nested_draws.fetch_add(1, std::memory_order_relaxed);
				original(context, index_count, start_index_location, base_vertex_location);
				return;
			}
			inside_draw = true;
			const auto exit = gsl::finally([] { inside_draw = false; });
			region_capture::draw_indexed(index_count);
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			engine_stereo_execution::invocation_scope execution_scope{};
			engine_stereo_execution::begin_draw_indexed(execution_scope, context,
				index_count, start_index_location, base_vertex_location, caller);
			if (const auto observer = persistent_draw_observer.load(
				std::memory_order_acquire))
			{
				observer(context, caller, 4);
			}
			original(context, index_count, start_index_location, base_vertex_location);
			engine_stereo_execution::end_draw_indexed(execution_scope);
			if (live_installed.load(std::memory_order_acquire) &&
				reinterpret_cast<std::uintptr_t>(context) == live_context.load(std::memory_order_acquire))
			{
				if (const auto copy = copy_observer.load(std::memory_order_acquire))
					copy(context, index_count, start_index_location, base_vertex_location, original);
			}
			if (active_transaction != nullptr &&
				static_cast<bool>(*active_transaction))
			{
				observe_boundary(*active_transaction, context, index_count,
					start_index_location, base_vertex_location, caller);
				observe_draw(*active_transaction, context, index_count,
					start_index_location, base_vertex_location, caller);
			}
			if (active_replay != nullptr && active_replay->active)
			{
				observe_replay(*active_replay, context, index_count,
					start_index_location, base_vertex_location, caller);
			}
		}

		template<std::size_t... Indices>
		constexpr auto make_draw_stubs(std::index_sequence<Indices...>) noexcept
		{
			return std::array<draw_fn, sizeof...(Indices)>{draw_stub<Indices>...};
		}
		constexpr auto draw_stubs = make_draw_stubs(std::make_index_sequence<maximum_hook_targets>{});
	}

	void set_draw_observer(const draw_observer observer) noexcept
	{
		persistent_draw_observer.store(observer, std::memory_order_release);
	}

	void set_draw_copy_observer(const draw_copy_observer observer) noexcept
	{
		copy_observer.store(observer, std::memory_order_release);
	}

	bool install(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		// Installation belongs to native frame/UI boundaries, never a draw callback.
		if (inside_draw) return false;
		const auto context_address = reinterpret_cast<std::uintptr_t>(context);
		const auto* const vtable = context ? *reinterpret_cast<void***>(context) : nullptr;
		void* const target = vtable ? vtable[draw_indexed_vtable_slot] : nullptr;
		const auto target_address = reinterpret_cast<std::uintptr_t>(target);
		const auto state = current_state.load(std::memory_order_acquire);
		if (live_installed.load(std::memory_order_acquire) &&
			live_target.load(std::memory_order_acquire) == target_address &&
			live_context.load(std::memory_order_acquire) == context_address &&
			live_generation.load(std::memory_order_acquire) == device_generation &&
			(is_terminal(state) || state == gate_state::active ||
				(expected_context.load(std::memory_order_acquire) == context_address &&
					expected_generation.load(std::memory_order_acquire) == device_generation)))
			return true;

		const std::lock_guard lock(hook_mutex);
		// A late callback from a retired device must not displace the current owner.
		if (device_generation != 0 && (device_generation <= retired_generation ||
			device_generation < live_generation.load(std::memory_order_acquire)))
			return false;
		if (rejected_target == target_address && rejected_context == context_address &&
			rejected_generation == device_generation && rejected_generation != 0)
			return false;
		live_installed.store(false, std::memory_order_release);
		if (context && device_generation != 0)
		{
			live_context.store(context_address, std::memory_order_release);
			live_generation.store(device_generation, std::memory_order_release);
		}
		try
		{
			if (!context || device_generation == 0 ||
				!utils::hook_validation::validate_executable_target(target))
				throw std::runtime_error("D3D11 DrawIndexed context/target is invalid");

			auto entry = std::find_if(draw_hooks.begin(), draw_hooks.end(),
				[&](const auto& value) { return value.target == target_address; });
			if (entry == draw_hooks.end())
			{
				entry = std::find_if(draw_hooks.begin(), draw_hooks.end(),
					[](const auto& value) { return value.target == 0; });
				if (entry == draw_hooks.end())
					throw std::runtime_error("D3D11 DrawIndexed retained-target capacity exceeded");
				const auto index = static_cast<std::size_t>(entry - draw_hooks.begin());
				entry->hook.create_disabled(target, draw_stubs[index]);
				entry->original.store(reinterpret_cast<draw_fn>(entry->hook.get_original()),
					std::memory_order_release);
				// Claim the slot before enabling it. An enable failure leaves a stable
				// disabled entry for a later device generation, never a reused trampoline.
				entry->target = target_address;
				retained_targets.fetch_add(1, std::memory_order_relaxed);
			}
			if (!entry->hook.is_enabled()) entry->hook.enable();

			const auto previous = live_target.load(std::memory_order_acquire);
			const auto evidence_state = current_state.load(std::memory_order_acquire);
			// A concurrent context mode switch requires another boundary check.
			// Do not admit evidence or copies for an entry we have not covered.
			if ((*reinterpret_cast<void***>(context))[draw_indexed_vtable_slot] != target)
			{
				if (evidence_state == gate_state::active)
					device_invalidated_during_transaction.store(true, std::memory_order_release);
				else if (!is_terminal(evidence_state))
					hook_installed.store(false, std::memory_order_release);
				return false;
			}
			if (evidence_state == gate_state::active)
			{
				if (hook_target.load(std::memory_order_acquire) != target_address ||
					expected_context.load(std::memory_order_acquire) != context_address ||
					expected_generation.load(std::memory_order_acquire) != device_generation)
					device_invalidated_during_transaction.store(true, std::memory_order_release);
			}
			else if (!is_terminal(evidence_state))
			{
				hook_target.store(target_address, std::memory_order_release);
				expected_context.store(context_address, std::memory_order_release);
				expected_generation.store(device_generation, std::memory_order_release);
				hook_installed.store(true, std::memory_order_release);
			}
			if (previous != 0 && previous != target_address)
				target_changes.fetch_add(1, std::memory_order_relaxed);
			live_target.store(target_address, std::memory_order_release);
			live_installed.store(true, std::memory_order_release);
			rejected_generation = 0;
			return true;
		}
		catch (...)
		{
			rejected_target = target_address;
			rejected_context = context_address;
			rejected_generation = device_generation;
			live_installed.store(false, std::memory_order_release);
			live_failures.fetch_add(1, std::memory_order_relaxed);
			if (!is_terminal(current_state.load(std::memory_order_acquire)))
			{
				hook_failures.fetch_add(1, std::memory_order_relaxed);
				hook_installed.store(false, std::memory_order_release);
				if (current_state.load(std::memory_order_acquire) == gate_state::active)
					device_invalidated_during_transaction.store(true, std::memory_order_release);
			}
			return false;
		}
	}

	hook_status get_hook_status() noexcept
	{
		const std::lock_guard lock(hook_mutex);
		return {live_installed.load(), live_target.load(), live_context.load(),
			live_generation.load(), live_failures.load(), target_changes.load(),
			retained_targets.load(), nested_draws.load()};
	}

	void invalidate_device(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		const std::lock_guard hook_lock(hook_mutex);
		const auto address = reinterpret_cast<std::uintptr_t>(context);
		if (live_generation.load(std::memory_order_acquire) == device_generation &&
			live_context.load(std::memory_order_acquire) == address)
		{
			live_installed.store(false, std::memory_order_release);
			live_context.store(0, std::memory_order_release);
			retired_generation = std::max(retired_generation, device_generation);
			// Keep the generation watermark and all trampolines for late calls.
		}
		const auto state = current_state.load(std::memory_order_acquire);
		if (is_terminal(state)) return;
		if (expected_generation.load(std::memory_order_acquire) != device_generation ||
			expected_context.load(std::memory_order_acquire) != address)
			return;
		if (state == gate_state::active)
		{
			device_invalidated_during_transaction.store(true, std::memory_order_release);
			return;
		}
		expected_context.store(0, std::memory_order_release);
		expected_generation.store(0, std::memory_order_release);
	}

	bool begin(transaction& output,
		const engine_stereo_binding::backend_claim& claim,
		const std::uintptr_t record) noexcept
	{
		const std::lock_guard lock(hook_mutex);
		if (output || active_transaction != nullptr || !claim || record == 0 ||
			claim.record != record || !hook_installed.load(std::memory_order_acquire) ||
			expected_context.load(std::memory_order_acquire) == 0 ||
			expected_generation.load(std::memory_order_acquire) == 0)
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
		device_invalidated_during_transaction.store(false, std::memory_order_release);
		output.active = true;
		output.evidence.publication_sequence = claim.publication_sequence;
		output.evidence.record = record;
		output.evidence.expected_context = expected_context.load(std::memory_order_acquire);
		output.evidence.device_generation = expected_generation.load(std::memory_order_acquire);
		output.evidence.transaction_thread_id = GetCurrentThreadId();
		latest_publication_sequence.store(claim.publication_sequence,
			std::memory_order_release);
		latest_record.store(record, std::memory_order_release);
		active_transaction = &output;
		return true;
	}

	void enter_dispatch(transaction& active, const void* const commands) noexcept
	{
		if (!active) return;
		if (active.dispatch_active || active.dispatch_returned) fail(active);
		active.evidence.before = engine_command_stream::capture(commands);
		if (!active.evidence.before.valid) fail(active);
		active.dispatch_active = true;
		active.dispatch_entered = true;
	}

	void leave_dispatch(transaction& active, const void* const commands) noexcept
	{
		if (!active) return;
		if (!active.dispatch_active) fail(active);
		active.evidence.after = engine_command_stream::capture(commands);
		if (!engine_command_stream::identical(active.evidence.before,
			active.evidence.after))
		{
			fail(active);
		}
		active.dispatch_active = false;
		active.dispatch_returned = true;
	}

	void end(transaction& active, const bool command_dispatch_returned) noexcept
	{
		if (!active) return;
		const std::lock_guard lock(hook_mutex);
		if (active.dispatch_active || !active.dispatch_entered ||
			!active.dispatch_returned || !command_dispatch_returned ||
			active.draw_calls == 0 || active.evidence.event_count == 0 ||
			active.context_mismatches != 0 || active.thread_mismatches != 0 ||
			active.invalid_arguments != 0 || active.overflows != 0 ||
			device_invalidated_during_transaction.load(std::memory_order_acquire) ||
			expected_context.load(std::memory_order_acquire) !=
				active.evidence.expected_context ||
			expected_generation.load(std::memory_order_acquire) !=
				active.evidence.device_generation)
		{
			fail(active);
		}

		{
			const std::lock_guard report_lock(report_mutex);
			published_report = active.evidence;
		}
		if (active.failed)
		{
			failures.fetch_add(1, std::memory_order_relaxed);
			current_state.store(gate_state::failed, std::memory_order_release);
		}
		else
		{
			completions.fetch_add(1, std::memory_order_relaxed);
			current_state.store(gate_state::complete, std::memory_order_release);
		}
		if (active_transaction == &active) active_transaction = nullptr;
		clear_transaction(active);
	}

	bool begin_replay(replay_counter& output,
		ID3D11DeviceContext* const context) noexcept
	{
		if (output.active || active_replay != nullptr || active_transaction != nullptr ||
			!hook_installed.load(std::memory_order_acquire) || context == nullptr ||
			reinterpret_cast<std::uintptr_t>(context) !=
				expected_context.load(std::memory_order_acquire))
		{
			return false;
		}
		output = {};
		output.active = true;
		output.expected_context = reinterpret_cast<std::uintptr_t>(context);
		output.thread_id = GetCurrentThreadId();
		active_replay = &output;
		return true;
	}

	bool end_replay(replay_counter& active) noexcept
	{
		if (!active.active || active_replay != &active) return false;
		active_replay = nullptr;
		active.active = false;
		return active.draw_calls != 0 && active.context_mismatches == 0 &&
			active.thread_mismatches == 0 && active.invalid_arguments == 0 &&
			active.overflows == 0;
	}

	replay_comparison compare_replay(const replay_counter& natural,
		const replay_counter& replay) noexcept
	{
		replay_comparison result{};
		result.trace_complete = !natural.active && !replay.active &&
			natural.draw_calls != 0 && natural.draw_calls == replay.draw_calls &&
			natural.event_count == natural.draw_calls &&
			replay.event_count == replay.draw_calls && natural.overflows == 0 &&
			replay.overflows == 0;
		if (!result.trace_complete) return result;

		result.shape_matches = true;
		result.exact_matches = true;
		result.dynamic_index_append_matches = true;
		const auto first_natural = natural.events[0].start_index_location;
		const auto first_replay = replay.events[0].start_index_location;
		if (first_replay < first_natural)
		{
			result.dynamic_index_append_matches = false;
		}
		else
		{
			result.start_index_delta = first_replay - first_natural;
		}

		for (std::uint32_t index{}; index < natural.event_count; ++index)
		{
			const auto& left = natural.events[index];
			const auto& right = replay.events[index];
			result.natural_index_count += left.index_count;
			if (left.caller != right.caller || left.index_count != right.index_count ||
				left.base_vertex_location != right.base_vertex_location)
			{
				result.shape_matches = false;
				result.dynamic_index_append_matches = false;
			}
			if (left.start_index_location != right.start_index_location)
			{
				result.exact_matches = false;
			}
			if (static_cast<std::uint64_t>(left.start_index_location) +
				result.start_index_delta != right.start_index_location)
			{
				result.dynamic_index_append_matches = false;
			}
			if (index + 1 < natural.event_count)
			{
				const auto& next_left = natural.events[index + 1];
				const auto& next_right = replay.events[index + 1];
				if (static_cast<std::uint64_t>(left.start_index_location) +
					left.index_count != next_left.start_index_location ||
					static_cast<std::uint64_t>(right.start_index_location) +
					right.index_count != next_right.start_index_location)
				{
					result.dynamic_index_append_matches = false;
				}
			}
		}

		result.exact_matches = result.shape_matches && result.exact_matches;
		result.dynamic_index_append_matches = result.shape_matches &&
			result.dynamic_index_append_matches && result.start_index_delta != 0 &&
			result.start_index_delta == result.natural_index_count;
		result.semantic_matches = result.exact_matches ||
			result.dynamic_index_append_matches;
		return result;
	}

	status get_status() noexcept
	{
		return {
			current_state.load(std::memory_order_acquire),
			hook_installed.load(std::memory_order_acquire),
			hook_target.load(std::memory_order_acquire),
			expected_context.load(std::memory_order_acquire),
			expected_generation.load(std::memory_order_acquire),
			hook_failures.load(std::memory_order_acquire),
			attempts.load(std::memory_order_acquire),
			completions.load(std::memory_order_acquire),
			failures.load(std::memory_order_acquire),
			draw_calls.load(std::memory_order_acquire),
			context_mismatches.load(std::memory_order_acquire),
			thread_mismatches.load(std::memory_order_acquire),
			invalid_arguments.load(std::memory_order_acquire),
			overflows.load(std::memory_order_acquire),
			latest_publication_sequence.load(std::memory_order_acquire),
			latest_record.load(std::memory_order_acquire),
		};
	}

	bool read_report(report& output) noexcept
	{
		const auto state = current_state.load(std::memory_order_acquire);
		if (state != gate_state::complete && state != gate_state::failed) return false;
		const std::lock_guard lock(report_mutex);
		output = published_report;
		return output.event_count != 0;
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

	const char* to_string(const boundary_phase value) noexcept
	{
		switch (value)
		{
		case boundary_phase::before_dispatch: return "before_dispatch";
		case boundary_phase::dispatch: return "dispatch";
		case boundary_phase::after_dispatch: return "after_dispatch";
		default: return "unknown";
		}
	}

	bool reset() noexcept
	{
		const std::lock_guard hook_lock(hook_mutex);
		if (current_state.load(std::memory_order_acquire) == gate_state::active ||
			active_transaction != nullptr || active_replay != nullptr)
		{
			return false;
		}
		attempts.store(0, std::memory_order_relaxed);
		completions.store(0, std::memory_order_relaxed);
		failures.store(0, std::memory_order_relaxed);
		draw_calls.store(0, std::memory_order_relaxed);
		context_mismatches.store(0, std::memory_order_relaxed);
		thread_mismatches.store(0, std::memory_order_relaxed);
		invalid_arguments.store(0, std::memory_order_relaxed);
		overflows.store(0, std::memory_order_relaxed);
		latest_publication_sequence.store(0, std::memory_order_relaxed);
		latest_record.store(0, std::memory_order_relaxed);
		device_invalidated_during_transaction.store(false, std::memory_order_relaxed);
		{
			const std::lock_guard report_lock(report_mutex);
			clear_report(published_report);
		}
		current_state.store(gate_state::armed, std::memory_order_release);
		return true;
	}
}
