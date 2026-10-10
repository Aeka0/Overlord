#include <std_include.hpp>

#include "engine_stereo_output_merger.hpp"

#include "utils/hook.hpp"
#include "utils/hook_validation.hpp"

#include <atomic>
#include <mutex>

namespace vr::engine_stereo_output_merger
{
	namespace
	{
		constexpr std::size_t output_merger_vtable_slot = 33;
		constexpr std::size_t output_merger_unordered_access_vtable_slot = 34;
		constexpr std::size_t clear_state_vtable_slot = 110;
		constexpr std::size_t clear_render_target_vtable_slot = 50;
		constexpr std::size_t clear_depth_stencil_vtable_slot = 53;
		using output_merger_fn = void(__stdcall*)(ID3D11DeviceContext*, UINT,
			ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
		using output_merger_unordered_access_fn = void(__stdcall*)(
			ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
			ID3D11DepthStencilView*, UINT, UINT,
			ID3D11UnorderedAccessView* const*, const UINT*);
		using clear_state_fn = void(__stdcall*)(ID3D11DeviceContext*);
		using clear_render_target_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11RenderTargetView*, const FLOAT[4]);
		using clear_depth_stencil_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11DepthStencilView*, UINT, FLOAT, UINT8);

		utils::hook::detour output_merger_hook;
		utils::hook::detour output_merger_unordered_access_hook;
		utils::hook::detour clear_state_hook;
		utils::hook::detour clear_render_target_hook;
		utils::hook::detour clear_depth_stencil_hook;
		std::mutex hook_mutex;
		std::mutex report_mutex;
		std::atomic_bool hook_installed{};
		std::atomic_bool extended_hooks_installed{};
		std::atomic_uintptr_t hook_target{};
		std::atomic_uintptr_t unordered_access_hook_target{};
		std::atomic_uintptr_t clear_state_hook_target{};
		std::atomic_uintptr_t clear_render_target_hook_target{};
		std::atomic_uintptr_t clear_depth_stencil_hook_target{};
		std::atomic_uintptr_t expected_context{};
		std::atomic_uint64_t expected_generation{};
		std::atomic_uint64_t hook_failures{};
		std::atomic<gate_state> current_state{gate_state::armed};
		std::atomic_uint64_t attempts{};
		std::atomic_uint64_t completions{};
		std::atomic_uint64_t failures{};
		std::atomic_uint64_t bind_calls{};
		std::atomic_uint64_t nonnull_bind_calls{};
		std::atomic_uint64_t context_mismatches{};
		std::atomic_uint64_t thread_mismatches{};
		std::atomic_uint64_t query_failures{};
		std::atomic_uint64_t overflows{};
		std::atomic_uint64_t metadata_queries{};
		std::atomic_uint64_t unordered_access_calls{};
		std::atomic_uint64_t clear_state_calls{};
		std::atomic_uint64_t clear_render_target_calls{};
		std::atomic_uint64_t clear_depth_stencil_calls{};
		std::array<std::atomic<clear_render_target_observer_fn>,
			clear_observer_channel_count> clear_render_target_observers{};
		std::array<std::atomic<clear_depth_stencil_observer_fn>,
			clear_observer_channel_count> clear_depth_stencil_observers{};
		std::atomic<clear_state_observer_fn> clear_state_observer{};
		std::atomic<render_target_rewriter_fn> render_target_rewriter{};
		std::atomic<depth_stencil_rewriter_fn> depth_stencil_rewriter{};
		std::atomic<unordered_access_rewriter_fn> unordered_access_rewriter{};
		std::atomic_uint64_t binding_invalidations{};
		std::atomic_uint64_t terminal_unordered_access_calls{};
		std::atomic_uint64_t terminal_clear_state_calls{};
		std::atomic_uint64_t terminal_binding_invalidations{};
		std::atomic_bool device_invalidated_during_transaction{};
		std::atomic_uint64_t latest_publication_sequence{};
		std::atomic_uintptr_t latest_record{};
		report published_report{};
		thread_local transaction* active_transaction{};
		thread_local replay_guard* active_replay_guard{};
		thread_local bool inside_output_merger{};
		thread_local binding_snapshot current_binding{};
		thread_local std::uint64_t current_binding_sequence{};
		thread_local std::uint32_t current_target_id{0xFFFFFFFFu};

		[[nodiscard]] bool is_terminal(const gate_state state) noexcept
		{
			return state == gate_state::complete || state == gate_state::failed;
		}

		[[nodiscard]] std::uint64_t query_performance_counter() noexcept
		{
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return static_cast<std::uint64_t>(value.QuadPart);
		}

		void fail(transaction& active) noexcept
		{
			active.failed = true;
		}

		[[nodiscard]] phase classify_phase(const transaction& active) noexcept
		{
			if (active.dispatch_active) return phase::dispatch;
			if (active.dispatch_returned) return phase::after_dispatch;
			return phase::before_dispatch;
		}

		void record_query_failure(transaction& active) noexcept
		{
			++active.query_failures;
			query_failures.fetch_add(1, std::memory_order_relaxed);
			fail(active);
		}

		void record_metadata_query(transaction& active) noexcept
		{
			++active.metadata_queries;
			metadata_queries.fetch_add(1, std::memory_order_relaxed);
		}

		void capture_resource(transaction& active, ID3D11View* const view,
			view_observation& output) noexcept
		{
			if (view == nullptr) return;
			output.view = reinterpret_cast<std::uintptr_t>(view);
			ID3D11Resource* resource{};
			view->GetResource(&resource);
			record_metadata_query(active);
			if (resource == nullptr)
			{
				record_query_failure(active);
				return;
			}

			output.resource = reinterpret_cast<std::uintptr_t>(resource);
			resource->GetType(&output.resource_dimension);
			record_metadata_query(active);
			if (output.resource_dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
			{
				D3D11_TEXTURE2D_DESC description{};
				static_cast<ID3D11Texture2D*>(resource)->GetDesc(&description);
				record_metadata_query(active);
				output.width = description.Width;
				output.height = description.Height;
				output.mip_levels = description.MipLevels;
				output.array_size = description.ArraySize;
				output.resource_format = description.Format;
				output.sample_count = description.SampleDesc.Count;
				output.sample_quality = description.SampleDesc.Quality;
				output.usage = description.Usage;
				output.bind_flags = description.BindFlags;
				output.cpu_access_flags = description.CPUAccessFlags;
				output.misc_flags = description.MiscFlags;
				output.valid = description.Width != 0 && description.Height != 0;
			}
			else
			{
				record_query_failure(active);
			}
			resource->Release();
		}

		void capture_render_target(transaction& active,
			ID3D11RenderTargetView* const view, view_observation& output) noexcept
		{
			if (view == nullptr) return;
			D3D11_RENDER_TARGET_VIEW_DESC description{};
			view->GetDesc(&description);
			record_metadata_query(active);
			output.view_format = description.Format;
			output.view_dimension = static_cast<std::uint32_t>(description.ViewDimension);
			capture_resource(active, view, output);
		}

		void capture_depth_stencil(transaction& active,
			ID3D11DepthStencilView* const view, view_observation& output) noexcept
		{
			if (view == nullptr) return;
			D3D11_DEPTH_STENCIL_VIEW_DESC description{};
			view->GetDesc(&description);
			record_metadata_query(active);
			output.view_format = description.Format;
			output.view_dimension = static_cast<std::uint32_t>(description.ViewDimension);
			capture_resource(active, view, output);
		}

		void observe_output_merger(transaction& active,
			ID3D11DeviceContext* const context, const UINT render_target_count,
			ID3D11RenderTargetView* const* const render_targets,
			ID3D11DepthStencilView* const depth_stencil,
			const std::uintptr_t caller) noexcept
		{
			++active.bind_calls;
			bind_calls.fetch_add(1, std::memory_order_relaxed);
			if (render_target_count != 0 || depth_stencil != nullptr)
			{
				++active.nonnull_bind_calls;
				nonnull_bind_calls.fetch_add(1, std::memory_order_relaxed);
			}

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
			event.timestamp_qpc = query_performance_counter();
			event.publication_sequence = active.evidence.publication_sequence;
			event.record = active.evidence.record;
			event.context = reinterpret_cast<std::uintptr_t>(context);
			event.caller = caller;
			event.thread_id = thread_id;
			event.render_target_count = render_target_count;
			event.latest_target_id = active.latest_target_id;
			event.view_copy_ordinal = active.view_copy_ordinal;
			event.view_substitution_ordinal = active.view_substitution_ordinal;
			event.selected_eye = active.selected_eye;
			event.execution_phase = classify_phase(active);
			event.expected_context = context_matches;

			if (render_target_count > event.render_targets.size() ||
				(render_target_count != 0 && render_targets == nullptr))
			{
				++active.overflows;
				overflows.fetch_add(1, std::memory_order_relaxed);
				fail(active);
			}
			const auto captured_count = (std::min)(static_cast<std::size_t>(render_target_count),
				event.render_targets.size());
			for (std::size_t index{}; index < captured_count && render_targets != nullptr; ++index)
			{
				capture_render_target(active, render_targets[index],
					event.render_targets[index]);
			}
			capture_depth_stencil(active, depth_stencil, event.depth_stencil);
		}

		void publish_current_binding(ID3D11DeviceContext* const context,
			const UINT render_target_count,
			ID3D11RenderTargetView* const* const render_targets,
			ID3D11DepthStencilView* const depth_stencil) noexcept
		{
			if (reinterpret_cast<std::uintptr_t>(context) !=
				expected_context.load(std::memory_order_acquire))
			{
				return;
			}
			current_binding = {};
			current_binding.valid = render_target_count <=
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT &&
				(render_target_count == 0 || render_targets != nullptr);
			current_binding.sequence = ++current_binding_sequence;
			current_binding.context = reinterpret_cast<std::uintptr_t>(context);
			current_binding.render_target_count = render_target_count;
			current_binding.target_id = current_target_id;
			current_binding.render_target_0 = render_target_count != 0 &&
				render_targets != nullptr
				? reinterpret_cast<std::uintptr_t>(render_targets[0]) : 0;
			current_binding.depth_stencil =
				reinterpret_cast<std::uintptr_t>(depth_stencil);
		}

		void observe_replay_mutation(ID3D11DeviceContext* const context) noexcept
		{
			if (active_replay_guard == nullptr || !active_replay_guard->active) return;
			auto& replay = *active_replay_guard;
			const auto expected_context_matches = replay.expected_context ==
				reinterpret_cast<std::uintptr_t>(context);
			const auto expected_thread_matches = replay.thread_id == GetCurrentThreadId();
			if (!expected_context_matches || !expected_thread_matches)
			{
				++replay.invalid_calls;
			}
			else
			{
				++replay.output_bind_calls;
			}
		}

		void observe_transaction_binding(ID3D11DeviceContext* const context,
			const UINT render_target_count,
			ID3D11RenderTargetView* const* const render_targets,
			ID3D11DepthStencilView* const depth_stencil,
			const std::uintptr_t caller) noexcept
		{
			if (inside_output_merger || active_transaction == nullptr ||
				!static_cast<bool>(*active_transaction))
			{
				return;
			}
			inside_output_merger = true;
			observe_output_merger(*active_transaction, context, render_target_count,
				render_targets, depth_stencil, caller);
			inside_output_merger = false;
		}

		void __stdcall output_merger_stub(ID3D11DeviceContext* const context,
			const UINT render_target_count,
			ID3D11RenderTargetView* const* const render_targets,
			ID3D11DepthStencilView* const depth_stencil)
		{
			const auto original = reinterpret_cast<output_merger_fn>(
				output_merger_hook.get_original());
			if (original == nullptr) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			std::array<ID3D11RenderTargetView*, maximum_render_targets> rewritten_targets{};
			auto* effective_targets = render_targets;
			if (render_targets != nullptr && render_target_count <= rewritten_targets.size())
			{
				const auto rewrite = render_target_rewriter.load(std::memory_order_acquire);
				if (rewrite != nullptr)
				{
					for (UINT index{}; index < render_target_count; ++index)
					{
						rewritten_targets[index] = render_targets[index] == nullptr ? nullptr :
							rewrite(context, render_targets[index]);
					}
					effective_targets = rewritten_targets.data();
				}
			}
			auto* effective_depth = depth_stencil;
			if (depth_stencil != nullptr)
			{
				if (const auto rewrite = depth_stencil_rewriter.load(
					std::memory_order_acquire))
				{
					effective_depth = rewrite(context, depth_stencil);
				}
			}
			original(context, render_target_count, effective_targets, effective_depth);
			publish_current_binding(context, render_target_count, effective_targets,
				effective_depth);
			observe_replay_mutation(context);
			observe_transaction_binding(context, render_target_count, effective_targets,
				effective_depth, caller);
		}

		void __stdcall output_merger_unordered_access_stub(
			ID3D11DeviceContext* const context, const UINT render_target_count,
			ID3D11RenderTargetView* const* const render_targets,
			ID3D11DepthStencilView* const depth_stencil,
			const UINT unordered_access_start_slot,
			const UINT unordered_access_count,
			ID3D11UnorderedAccessView* const* const unordered_access_views,
			const UINT* const initial_counts)
		{
			const auto original = reinterpret_cast<output_merger_unordered_access_fn>(
				output_merger_unordered_access_hook.get_original());
			if (original == nullptr) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			std::array<ID3D11RenderTargetView*, maximum_render_targets> rewritten_targets{};
			auto* effective_targets = render_targets;
			auto* effective_depth = depth_stencil;
			if (render_target_count != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
			{
				if (render_targets != nullptr && render_target_count <= rewritten_targets.size())
				{
					const auto rewrite = render_target_rewriter.load(std::memory_order_acquire);
					if (rewrite != nullptr)
					{
						for (UINT index{}; index < render_target_count; ++index)
						{
							rewritten_targets[index] = render_targets[index] == nullptr ? nullptr :
								rewrite(context, render_targets[index]);
						}
						effective_targets = rewritten_targets.data();
					}
				}
				if (depth_stencil != nullptr)
				{
					if (const auto rewrite = depth_stencil_rewriter.load(
						std::memory_order_acquire))
					{
						effective_depth = rewrite(context, depth_stencil);
					}
				}
			}
			std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT>
				rewritten_uavs{};
			auto* effective_uavs = unordered_access_views;
			if (unordered_access_views != nullptr &&
				unordered_access_count <= rewritten_uavs.size())
			{
				const auto rewrite = unordered_access_rewriter.load(
					std::memory_order_acquire);
				if (rewrite != nullptr)
				{
					for (UINT index{}; index < unordered_access_count; ++index)
					{
						rewritten_uavs[index] = unordered_access_views[index] == nullptr ? nullptr :
							rewrite(context, unordered_access_views[index]);
					}
					effective_uavs = rewritten_uavs.data();
				}
			}
			original(context, render_target_count, effective_targets, effective_depth,
				unordered_access_start_slot, unordered_access_count,
				effective_uavs, initial_counts);
			unordered_access_calls.fetch_add(1, std::memory_order_relaxed);
			// D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL changes only UAV state;
			// preserve the exact RTV/DSV snapshot in that case.
			if (render_target_count != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
			{
				publish_current_binding(context, render_target_count, effective_targets,
					effective_depth);
				observe_transaction_binding(context, render_target_count,
					effective_targets, effective_depth, caller);
			}
			observe_replay_mutation(context);
		}

		void __stdcall clear_state_stub(ID3D11DeviceContext* const context)
		{
			const auto original = reinterpret_cast<clear_state_fn>(
				clear_state_hook.get_original());
			if (original == nullptr) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			original(context);
			clear_state_calls.fetch_add(1, std::memory_order_relaxed);
			invalidate_current_binding(context);
			observe_replay_mutation(context);
			observe_transaction_binding(context, 0, nullptr, nullptr, caller);
			if (const auto callback = clear_state_observer.load(
				std::memory_order_acquire))
			{
				callback(context, caller);
			}
		}

		void __stdcall clear_render_target_stub(ID3D11DeviceContext* const context,
			ID3D11RenderTargetView* const view, const FLOAT color[4])
		{
			const auto original = reinterpret_cast<clear_render_target_fn>(
				clear_render_target_hook.get_original());
			if (original == nullptr) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			auto* effective_view = view;
			if (view != nullptr)
			{
				if (const auto rewrite = render_target_rewriter.load(
					std::memory_order_acquire))
				{
					effective_view = rewrite(context, view);
				}
			}
			original(context, effective_view, color);
			clear_render_target_calls.fetch_add(1, std::memory_order_relaxed);
			for (auto& registered : clear_render_target_observers)
			{
				if (const auto observer = registered.load(std::memory_order_acquire))
				{
					observer(context, effective_view, caller,
						current_binding.target_id, current_binding.render_target_0,
						current_binding.sequence);
				}
			}
		}

		void __stdcall clear_depth_stencil_stub(ID3D11DeviceContext* const context,
			ID3D11DepthStencilView* const view, const UINT flags, const FLOAT depth,
			const UINT8 stencil)
		{
			const auto original = reinterpret_cast<clear_depth_stencil_fn>(
				clear_depth_stencil_hook.get_original());
			if (original == nullptr) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			auto* effective_view = view;
			if (view != nullptr)
			{
				if (const auto rewrite = depth_stencil_rewriter.load(
					std::memory_order_acquire))
				{
					effective_view = rewrite(context, view);
				}
			}
			original(context, effective_view, flags, depth, stencil);
			clear_depth_stencil_calls.fetch_add(1, std::memory_order_relaxed);
			for (auto& registered : clear_depth_stencil_observers)
			{
				if (const auto observer = registered.load(std::memory_order_acquire))
				{
					observer(context, effective_view, flags, caller,
						current_binding.target_id, current_binding.render_target_0,
						current_binding.sequence);
				}
			}
		}
	}

	bool begin_replay_guard(replay_guard& output,
		ID3D11DeviceContext* const context) noexcept
	{
		if (output.active || active_replay_guard != nullptr ||
			active_transaction != nullptr || context == nullptr ||
			!hook_installed.load(std::memory_order_acquire) ||
			reinterpret_cast<std::uintptr_t>(context) !=
				expected_context.load(std::memory_order_acquire))
		{
			return false;
		}
		output = {};
		output.active = true;
		output.expected_context = reinterpret_cast<std::uintptr_t>(context);
		output.thread_id = GetCurrentThreadId();
		active_replay_guard = &output;
		return true;
	}

	bool end_replay_guard(replay_guard& active) noexcept
	{
		if (!active.active || active_replay_guard != &active) return false;
		active_replay_guard = nullptr;
		active.active = false;
		return active.invalid_calls == 0 && active.output_bind_calls == 0;
	}

	binding_snapshot get_current_binding(ID3D11DeviceContext* const context) noexcept
	{
		if (context == nullptr || current_binding.context !=
			reinterpret_cast<std::uintptr_t>(context) ||
			expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context))
		{
			return {};
		}
		return current_binding;
	}

	void set_clear_observers(const clear_observer_channel channel,
		const clear_render_target_observer_fn render_target,
		const clear_depth_stencil_observer_fn depth_stencil) noexcept
	{
		const auto slot = static_cast<std::size_t>(channel);
		if (slot >= clear_render_target_observers.size()) return;
		clear_render_target_observers[slot].store(render_target,
			std::memory_order_release);
		clear_depth_stencil_observers[slot].store(depth_stencil,
			std::memory_order_release);
	}

	void set_clear_state_observer(const clear_state_observer_fn value) noexcept
	{
		clear_state_observer.store(value, std::memory_order_release);
	}

	void set_view_rewriters(const render_target_rewriter_fn render_target,
		const depth_stencil_rewriter_fn depth_stencil,
		const unordered_access_rewriter_fn unordered_access) noexcept
	{
		render_target_rewriter.store(render_target, std::memory_order_release);
		depth_stencil_rewriter.store(depth_stencil, std::memory_order_release);
		unordered_access_rewriter.store(unordered_access, std::memory_order_release);
	}

	void invalidate_current_binding(ID3D11DeviceContext* const context) noexcept
	{
		const auto address = reinterpret_cast<std::uintptr_t>(context);
		if (context == nullptr || expected_context.load(std::memory_order_acquire) !=
				address || current_binding.context != address)
		{
			return;
		}
		current_binding = {};
		current_binding.sequence = ++current_binding_sequence;
		current_binding.context = address;
		current_target_id = 0xFFFFFFFFu;
		binding_invalidations.fetch_add(1, std::memory_order_relaxed);
	}

	bool install(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		if (context == nullptr || device_generation == 0)
		{
			const std::lock_guard lock(hook_mutex);
			if (!is_terminal(current_state.load(std::memory_order_acquire)))
			{
				hook_failures.fetch_add(1, std::memory_order_relaxed);
			}
			return false;
		}
		try
		{
			auto* const vtable = *reinterpret_cast<void***>(context);
			if (vtable == nullptr) throw std::runtime_error("D3D11 context vtable is null");
			void* const target = vtable[output_merger_vtable_slot];
			void* const unordered_access_target =
				vtable[output_merger_unordered_access_vtable_slot];
			void* const clear_target = vtable[clear_state_vtable_slot];
			void* const clear_render_target = vtable[clear_render_target_vtable_slot];
			void* const clear_depth_stencil = vtable[clear_depth_stencil_vtable_slot];
			const std::array<void*, 5> targets{target, unordered_access_target,
				clear_target, clear_render_target, clear_depth_stencil};
			for (const auto candidate : targets)
			{
				if (!utils::hook_validation::validate_executable_target(candidate))
				{
					throw std::runtime_error("D3D11 output binding target is not executable");
				}
			}
			for (std::size_t left{}; left < targets.size(); ++left)
			{
				for (auto right = left + 1; right < targets.size(); ++right)
				{
					if (targets[left] == targets[right])
					{
						throw std::runtime_error("D3D11 output binding targets alias");
					}
				}
			}

			const std::lock_guard lock(hook_mutex);
			const auto existing = hook_target.load(std::memory_order_acquire);
			const auto existing_unordered_access = unordered_access_hook_target.load(
				std::memory_order_acquire);
			const auto existing_clear = clear_state_hook_target.load(
				std::memory_order_acquire);
			const auto existing_clear_render_target =
				clear_render_target_hook_target.load(std::memory_order_acquire);
			const auto existing_clear_depth_stencil =
				clear_depth_stencil_hook_target.load(std::memory_order_acquire);
			if (existing != 0 && existing != reinterpret_cast<std::uintptr_t>(target))
			{
				throw std::runtime_error("D3D11 OMSetRenderTargets target changed across devices");
			}
			if (existing_unordered_access != 0 && existing_unordered_access !=
				reinterpret_cast<std::uintptr_t>(unordered_access_target))
			{
				throw std::runtime_error("D3D11 OM UAV target changed across devices");
			}
			if (existing_clear != 0 && existing_clear !=
				reinterpret_cast<std::uintptr_t>(clear_target))
			{
				throw std::runtime_error("D3D11 ClearState target changed across devices");
			}
			if (existing_clear_render_target != 0 && existing_clear_render_target !=
				reinterpret_cast<std::uintptr_t>(clear_render_target))
			{
				throw std::runtime_error("D3D11 ClearRenderTargetView target changed across devices");
			}
			if (existing_clear_depth_stencil != 0 && existing_clear_depth_stencil !=
				reinterpret_cast<std::uintptr_t>(clear_depth_stencil))
			{
				throw std::runtime_error("D3D11 ClearDepthStencilView target changed across devices");
			}
			const auto state = current_state.load(std::memory_order_acquire);
			if (is_terminal(state))
			{
				// The report and all status identity fields describe one immutable
				// device epoch. A later device callback may validate that the
				// process-wide vtable targets are unchanged, but it must never
				// redirect a terminal observation to the new context/generation.
				return existing == reinterpret_cast<std::uintptr_t>(target) &&
					existing_unordered_access ==
						reinterpret_cast<std::uintptr_t>(unordered_access_target) &&
					existing_clear == reinterpret_cast<std::uintptr_t>(clear_target) &&
					existing_clear_render_target ==
						reinterpret_cast<std::uintptr_t>(clear_render_target) &&
					existing_clear_depth_stencil ==
						reinterpret_cast<std::uintptr_t>(clear_depth_stencil) &&
					hook_installed.load(std::memory_order_acquire) &&
					extended_hooks_installed.load(std::memory_order_acquire);
			}
			if (state == gate_state::active)
			{
				const auto same_device = expected_context.load(std::memory_order_acquire) ==
					reinterpret_cast<std::uintptr_t>(context) &&
					expected_generation.load(std::memory_order_acquire) == device_generation;
				if (!same_device)
				{
					device_invalidated_during_transaction.store(true,
						std::memory_order_release);
				}
				return same_device && hook_installed.load(std::memory_order_acquire) &&
					extended_hooks_installed.load(std::memory_order_acquire);
			}
			const auto enable = [](utils::hook::detour& hook, void* const hook_target_value,
				void* const stub)
			{
				if (hook.is_enabled()) return;
				if (hook.get_original() == nullptr) hook.create(hook_target_value, stub);
				else hook.enable();
			};
			enable(output_merger_hook, target,
				reinterpret_cast<void*>(output_merger_stub));
			hook_target.store(reinterpret_cast<std::uintptr_t>(target),
				std::memory_order_release);
			enable(output_merger_unordered_access_hook, unordered_access_target,
				reinterpret_cast<void*>(output_merger_unordered_access_stub));
			unordered_access_hook_target.store(
				reinterpret_cast<std::uintptr_t>(unordered_access_target),
				std::memory_order_release);
			enable(clear_state_hook, clear_target,
				reinterpret_cast<void*>(clear_state_stub));
			clear_state_hook_target.store(reinterpret_cast<std::uintptr_t>(clear_target),
				std::memory_order_release);
			enable(clear_render_target_hook, clear_render_target,
				reinterpret_cast<void*>(clear_render_target_stub));
			clear_render_target_hook_target.store(
				reinterpret_cast<std::uintptr_t>(clear_render_target),
				std::memory_order_release);
			enable(clear_depth_stencil_hook, clear_depth_stencil,
				reinterpret_cast<void*>(clear_depth_stencil_stub));
			clear_depth_stencil_hook_target.store(
				reinterpret_cast<std::uintptr_t>(clear_depth_stencil),
				std::memory_order_release);
			expected_context.store(reinterpret_cast<std::uintptr_t>(context),
				std::memory_order_release);
			expected_generation.store(device_generation, std::memory_order_release);
			hook_installed.store(true, std::memory_order_release);
			extended_hooks_installed.store(output_merger_hook.is_enabled() &&
				output_merger_unordered_access_hook.is_enabled() &&
				clear_state_hook.is_enabled() && clear_render_target_hook.is_enabled() &&
				clear_depth_stencil_hook.is_enabled(), std::memory_order_release);
			return true;
		}
		catch (...)
		{
			const std::lock_guard lock(hook_mutex);
			if (!is_terminal(current_state.load(std::memory_order_acquire)))
			{
				hook_failures.fetch_add(1, std::memory_order_relaxed);
				hook_installed.store(output_merger_hook.is_enabled(),
					std::memory_order_release);
				extended_hooks_installed.store(output_merger_hook.is_enabled() &&
					output_merger_unordered_access_hook.is_enabled() &&
					clear_state_hook.is_enabled() && clear_render_target_hook.is_enabled() &&
					clear_depth_stencil_hook.is_enabled(), std::memory_order_release);
			}
			return false;
		}
	}

	void invalidate_device(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		const std::lock_guard hook_lock(hook_mutex);
		const auto state = current_state.load(std::memory_order_acquire);
		if (is_terminal(state)) return;
		const auto expected = reinterpret_cast<std::uintptr_t>(context);
		if (expected_generation.load(std::memory_order_acquire) != device_generation ||
			expected_context.load(std::memory_order_acquire) != expected)
		{
			return;
		}
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

	void note_view_copy(transaction& active, const std::uint32_t copy_ordinal,
		const std::uint32_t substitution_ordinal, const std::uint32_t selected_eye) noexcept
	{
		if (!active) return;
		active.view_copy_ordinal = copy_ordinal;
		active.view_substitution_ordinal = substitution_ordinal;
		active.selected_eye = selected_eye;
		if (copy_ordinal == 0 || substitution_ordinal > copy_ordinal || selected_eye >= 2)
		{
			fail(active);
		}
	}

	void note_target(transaction& active, const std::uint32_t target_id) noexcept
	{
		current_target_id = target_id;
		if (active) active.latest_target_id = target_id;
	}

	void enter_dispatch(transaction& active) noexcept
	{
		if (!active) return;
		if (active.dispatch_active || active.dispatch_returned) fail(active);
		active.dispatch_active = true;
		active.dispatch_entered = true;
	}

	void leave_dispatch(transaction& active) noexcept
	{
		if (!active) return;
		if (!active.dispatch_active) fail(active);
		active.dispatch_active = false;
		active.dispatch_returned = true;
	}

	void end(transaction& active, const bool command_dispatch_returned) noexcept
	{
		if (!active) return;
		const std::lock_guard lock(hook_mutex);
		if (active.dispatch_active || !active.dispatch_entered ||
			!active.dispatch_returned || !command_dispatch_returned ||
			active.bind_calls == 0 || active.nonnull_bind_calls == 0 ||
			active.evidence.event_count == 0 || active.context_mismatches != 0 ||
			active.thread_mismatches != 0 || active.query_failures != 0 ||
			active.overflows != 0 ||
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
			terminal_unordered_access_calls.store(
				unordered_access_calls.load(std::memory_order_acquire),
				std::memory_order_release);
			terminal_clear_state_calls.store(
				clear_state_calls.load(std::memory_order_acquire),
				std::memory_order_release);
			terminal_binding_invalidations.store(
				binding_invalidations.load(std::memory_order_acquire),
				std::memory_order_release);
			failures.fetch_add(1, std::memory_order_relaxed);
			current_state.store(gate_state::failed, std::memory_order_release);
		}
		else
		{
			terminal_unordered_access_calls.store(
				unordered_access_calls.load(std::memory_order_acquire),
				std::memory_order_release);
			terminal_clear_state_calls.store(
				clear_state_calls.load(std::memory_order_acquire),
				std::memory_order_release);
			terminal_binding_invalidations.store(
				binding_invalidations.load(std::memory_order_acquire),
				std::memory_order_release);
			completions.fetch_add(1, std::memory_order_relaxed);
			current_state.store(gate_state::complete, std::memory_order_release);
		}
		if (active_transaction == &active) active_transaction = nullptr;
		active = {};
	}

	status get_status() noexcept
	{
		status output{};
		output.state = current_state.load(std::memory_order_acquire);
		output.hook_installed = hook_installed.load(std::memory_order_acquire);
		output.extended_hooks_installed = extended_hooks_installed.load(
			std::memory_order_acquire);
		output.hook_target = hook_target.load(std::memory_order_acquire);
		output.unordered_access_hook_target = unordered_access_hook_target.load(
			std::memory_order_acquire);
		output.clear_state_hook_target = clear_state_hook_target.load(
			std::memory_order_acquire);
		output.clear_render_target_hook_target = clear_render_target_hook_target.load(
			std::memory_order_acquire);
		output.clear_depth_stencil_hook_target = clear_depth_stencil_hook_target.load(
			std::memory_order_acquire);
		output.expected_context = expected_context.load(std::memory_order_acquire);
		output.device_generation = expected_generation.load(std::memory_order_acquire);
		output.hook_failures = hook_failures.load(std::memory_order_acquire);
		output.attempts = attempts.load(std::memory_order_acquire);
		output.completions = completions.load(std::memory_order_acquire);
		output.failures = failures.load(std::memory_order_acquire);
		output.bind_calls = bind_calls.load(std::memory_order_acquire);
		output.nonnull_bind_calls = nonnull_bind_calls.load(std::memory_order_acquire);
		output.context_mismatches = context_mismatches.load(std::memory_order_acquire);
		output.thread_mismatches = thread_mismatches.load(std::memory_order_acquire);
		output.query_failures = query_failures.load(std::memory_order_acquire);
		output.overflows = overflows.load(std::memory_order_acquire);
		output.metadata_queries = metadata_queries.load(std::memory_order_acquire);
		output.clear_render_target_calls = clear_render_target_calls.load(
			std::memory_order_acquire);
		output.clear_depth_stencil_calls = clear_depth_stencil_calls.load(
			std::memory_order_acquire);
		if (is_terminal(output.state))
		{
			output.unordered_access_calls = terminal_unordered_access_calls.load(
				std::memory_order_acquire);
			output.clear_state_calls = terminal_clear_state_calls.load(
				std::memory_order_acquire);
			output.binding_invalidations = terminal_binding_invalidations.load(
				std::memory_order_acquire);
		}
		else
		{
			output.unordered_access_calls = unordered_access_calls.load(
				std::memory_order_acquire);
			output.clear_state_calls = clear_state_calls.load(std::memory_order_acquire);
			output.binding_invalidations = binding_invalidations.load(
				std::memory_order_acquire);
		}
		output.latest_publication_sequence = latest_publication_sequence.load(
			std::memory_order_acquire);
		output.latest_record = latest_record.load(std::memory_order_acquire);
		return output;
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

	const char* to_string(const phase value) noexcept
	{
		switch (value)
		{
		case phase::before_dispatch: return "before_dispatch";
		case phase::dispatch: return "dispatch";
		case phase::after_dispatch: return "after_dispatch";
		default: return "unknown";
		}
	}

	bool reset() noexcept
	{
		const std::lock_guard hook_lock(hook_mutex);
		if (current_state.load(std::memory_order_acquire) == gate_state::active ||
			active_transaction != nullptr || active_replay_guard != nullptr)
		{
			return false;
		}
		attempts.store(0, std::memory_order_relaxed);
		completions.store(0, std::memory_order_relaxed);
		failures.store(0, std::memory_order_relaxed);
		bind_calls.store(0, std::memory_order_relaxed);
		nonnull_bind_calls.store(0, std::memory_order_relaxed);
		context_mismatches.store(0, std::memory_order_relaxed);
		thread_mismatches.store(0, std::memory_order_relaxed);
		query_failures.store(0, std::memory_order_relaxed);
		overflows.store(0, std::memory_order_relaxed);
		metadata_queries.store(0, std::memory_order_relaxed);
		unordered_access_calls.store(0, std::memory_order_relaxed);
		clear_state_calls.store(0, std::memory_order_relaxed);
		clear_render_target_calls.store(0, std::memory_order_relaxed);
		clear_depth_stencil_calls.store(0, std::memory_order_relaxed);
		binding_invalidations.store(0, std::memory_order_relaxed);
		terminal_unordered_access_calls.store(0, std::memory_order_relaxed);
		terminal_clear_state_calls.store(0, std::memory_order_relaxed);
		terminal_binding_invalidations.store(0, std::memory_order_relaxed);
		device_invalidated_during_transaction.store(false, std::memory_order_relaxed);
		latest_publication_sequence.store(0, std::memory_order_relaxed);
		latest_record.store(0, std::memory_order_relaxed);
		current_binding = {};
		current_binding_sequence = 0;
		current_target_id = 0xFFFFFFFFu;
		{
			const std::lock_guard report_lock(report_mutex);
			published_report = {};
		}
		current_state.store(gate_state::armed, std::memory_order_release);
		return true;
	}
}
