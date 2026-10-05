#include <std_include.hpp>

#include "engine_stereo_replay.hpp"

#include "engine_stereo_backend_target.hpp"
#include "engine_stereo_backend_view.hpp"
#include "engine_stereo_output_merger.hpp"

#include <atomic>
#include <mutex>

namespace vr::engine_stereo_replay
{
	namespace
	{
		constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
		constexpr std::uint64_t fnv_prime = 1099511628211ull;
		constexpr std::uint32_t maximum_readback_polls = 240;
		constexpr float clear_color[4]{0.03125f, 0.0625f, 0.125f, 1.0f};

		struct device_resources
		{
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> right_target;
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> right_view;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> left_staging;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> clear_staging;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> right_staging;
			Microsoft::WRL::ComPtr<ID3D11Query> completion_query;
			D3D11_TEXTURE2D_DESC target_description{};
			std::uint64_t generation{};
		};

		std::mutex state_mutex;
		device_resources resources;
		report evidence;
		std::atomic<gate_state> current_state{gate_state::waiting};
		std::atomic<failure> current_failure{failure::none};
		std::atomic_uint64_t preparations{};
		std::atomic_uint64_t attempts{};
		std::atomic_uint64_t completions{};
		std::atomic_uint64_t failure_count{};
		std::atomic_uint64_t replay_dispatches{};
		std::atomic_uint64_t replay_draw_calls{};
		std::atomic_uint64_t readback_polls{};
		std::atomic_uint64_t latest_publication_sequence{};
		std::atomic_uintptr_t latest_record{};
		std::atomic_uint64_t started_present{};
		std::atomic_uint64_t completed_present{};
		thread_local transaction* active_transaction{};

		void fail_global(const failure error) noexcept
		{
			current_failure.store(error, std::memory_order_release);
			current_state.store(gate_state::failed, std::memory_order_release);
			failure_count.fetch_add(1, std::memory_order_relaxed);
			std::lock_guard lock(state_mutex);
			evidence.error = error;
		}

		void fail_transaction(transaction& active, const failure error) noexcept
		{
			active.failed = true;
			if (active.error == failure::none) active.error = error;
		}

		[[nodiscard]] bool prerequisites_complete() noexcept
		{
			const auto view = engine_stereo_backend_view::get_status();
			const auto target = engine_stereo_backend_target::get_status();
			const auto frame = engine_stereo_backend_target::get_frame_status();
			const auto output = engine_stereo_output_merger::get_status();
			const auto draw = engine_stereo_draw_indexed::get_status();
			return view.state == engine_stereo_backend_view::gate_state::complete &&
				target.state == engine_stereo_backend_target::gate_state::complete &&
				frame.state == engine_stereo_backend_target::gate_state::complete &&
				output.state == engine_stereo_output_merger::gate_state::complete &&
				draw.state == engine_stereo_draw_indexed::gate_state::complete;
		}

		[[nodiscard]] bool supported_description(
			const D3D11_TEXTURE2D_DESC& description) noexcept
		{
			return description.Width != 0 && description.Height != 0 &&
				description.MipLevels == 1 && description.ArraySize == 1 &&
				description.Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
				description.SampleDesc.Count == 1;
		}

		[[nodiscard]] bool dispatch_scene_batch_observed() noexcept
		{
			engine_stereo_draw_indexed::report draw_report{};
			if (!engine_stereo_draw_indexed::read_report(draw_report)) return false;
			for (std::uint32_t index{}; index < draw_report.boundary_group_count; ++index)
			{
				const auto& group = draw_report.boundary_groups[index];
				if (group.execution_phase ==
					engine_stereo_draw_indexed::boundary_phase::dispatch &&
					group.render_target_count != 0 && group.render_target != 0 &&
					group.depth_stencil != 0 && group.draw_calls != 0)
				{
					return true;
				}
			}
			return false;
		}

		void release_resources() noexcept
		{
			resources = {};
		}

		void prepare(const d3d11::present_event& event) noexcept
		{
			if (!event.graphics || !prerequisites_complete())
			{
				return;
			}
			std::lock_guard lock(state_mutex);
			if (current_state.load(std::memory_order_acquire) != gate_state::waiting)
			{
				return;
			}
			preparations.fetch_add(1, std::memory_order_relaxed);
			evidence = {};
			evidence.started_present = event.frame_index;
			evidence.device_generation = event.graphics.generation;
			evidence.context = reinterpret_cast<std::uintptr_t>(
				event.graphics.context.Get());
			started_present.store(event.frame_index, std::memory_order_release);

			if (!dispatch_scene_batch_observed())
			{
				evidence.error = failure::scene_batch_selection;
				current_failure.store(evidence.error, std::memory_order_release);
				current_state.store(gate_state::failed, std::memory_order_release);
				failure_count.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			resources.device = event.graphics.device;
			resources.context = event.graphics.context;
			resources.generation = event.graphics.generation;
			evidence.swap_chain_buffer_result = S_OK;
			current_state.store(gate_state::ready, std::memory_order_release);
		}

		[[nodiscard]] bool create_resources(
			const D3D11_TEXTURE2D_DESC& natural_description) noexcept
		{
			if (!resources.device)
			{
				return false;
			}
			resources.target_description = natural_description;
			resources.target_description.Usage = D3D11_USAGE_DEFAULT;
			resources.target_description.BindFlags = D3D11_BIND_RENDER_TARGET;
			resources.target_description.CPUAccessFlags = 0;
			resources.target_description.MiscFlags = 0;
			evidence.width = natural_description.Width;
			evidence.height = natural_description.Height;
			evidence.format = static_cast<std::uint32_t>(natural_description.Format);

			evidence.target_result = resources.device->CreateTexture2D(
				&resources.target_description, nullptr, &resources.right_target);
			if (SUCCEEDED(evidence.target_result))
			{
				evidence.target_view_result = resources.device->CreateRenderTargetView(
					resources.right_target.Get(), nullptr, &resources.right_view);
			}
			D3D11_TEXTURE2D_DESC staging = resources.target_description;
			staging.Usage = D3D11_USAGE_STAGING;
			staging.BindFlags = 0;
			staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			evidence.left_staging_result = resources.device->CreateTexture2D(
				&staging, nullptr, &resources.left_staging);
			evidence.clear_staging_result = resources.device->CreateTexture2D(
				&staging, nullptr, &resources.clear_staging);
			evidence.right_staging_result = resources.device->CreateTexture2D(
				&staging, nullptr, &resources.right_staging);
			D3D11_QUERY_DESC query_description{D3D11_QUERY_EVENT, 0};
			evidence.query_result = resources.device->CreateQuery(&query_description,
				&resources.completion_query);
			return SUCCEEDED(evidence.target_result) &&
				SUCCEEDED(evidence.target_view_result) &&
				SUCCEEDED(evidence.left_staging_result) &&
				SUCCEEDED(evidence.clear_staging_result) &&
				SUCCEEDED(evidence.right_staging_result) &&
				SUCCEEDED(evidence.query_result);
		}

		[[nodiscard]] bool apply_view(transaction& active,
			const std::array<std::uint8_t, engine_stereo_view::h2_view_slot_size>& view)
			noexcept
		{
			if (active.backend_state == 0 || active.copy_original == nullptr) return false;
			auto* const state = reinterpret_cast<std::uint8_t*>(active.backend_state);
			std::uintptr_t source{};
			std::memcpy(&source, state + engine_stereo_backend_view::source_pointer_offset,
				sizeof(source));
			if (source != active.record) return false;
			const auto replacement = reinterpret_cast<std::uintptr_t>(view.data());
			std::memcpy(state + engine_stereo_backend_view::source_pointer_offset,
				&replacement, sizeof(replacement));
			active.copy_original(reinterpret_cast<void*>(active.backend_state));
			std::memcpy(state + engine_stereo_backend_view::source_pointer_offset,
				&source, sizeof(source));
			return std::memcmp(state + engine_stereo_backend_view::copied_view_offset,
				view.data(), view.size()) == 0;
		}

		[[nodiscard]] bool hash_staging(ID3D11DeviceContext* const context,
			ID3D11Texture2D* const texture, const D3D11_TEXTURE2D_DESC& description,
			HRESULT& map_result, std::uint64_t& hash,
			std::uint64_t& nonzero_bytes) noexcept
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			map_result = context->Map(texture, 0, D3D11_MAP_READ, 0, &mapped);
			if (FAILED(map_result)) return false;
			hash = fnv_offset;
			nonzero_bytes = 0;
			const auto row_bytes = static_cast<std::size_t>(description.Width) * 4;
			for (UINT row{}; row < description.Height; ++row)
			{
				const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData) +
					static_cast<std::size_t>(row) * mapped.RowPitch;
				for (std::size_t column{}; column < row_bytes; ++column)
				{
					hash ^= bytes[column];
					hash *= fnv_prime;
					nonzero_bytes += bytes[column] != 0 ? 1 : 0;
				}
			}
			context->Unmap(texture, 0);
			return true;
		}

		void poll_readback(const d3d11::present_event& event) noexcept
		{
			std::lock_guard lock(state_mutex);
			if (current_state.load(std::memory_order_acquire) !=
				gate_state::readback_pending)
			{
				return;
			}
			if (resources.generation != event.graphics.generation ||
				resources.context.Get() != event.graphics.context.Get())
			{
				evidence.error = failure::device;
				current_failure.store(evidence.error, std::memory_order_release);
				current_state.store(gate_state::failed, std::memory_order_release);
				failure_count.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			++evidence.readback_polls;
			readback_polls.fetch_add(1, std::memory_order_relaxed);
			evidence.query_poll_result = resources.context->GetData(
				resources.completion_query.Get(), nullptr, 0,
				D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (evidence.query_poll_result == S_FALSE)
			{
				if (evidence.readback_polls >= maximum_readback_polls)
				{
					evidence.error = failure::timeout;
					current_failure.store(evidence.error, std::memory_order_release);
					current_state.store(gate_state::failed, std::memory_order_release);
					failure_count.fetch_add(1, std::memory_order_relaxed);
				}
				return;
			}
			if (FAILED(evidence.query_poll_result) ||
				!hash_staging(resources.context.Get(), resources.left_staging.Get(),
					resources.target_description, evidence.left_map_result,
					evidence.left_hash, evidence.left_nonzero_bytes) ||
				!hash_staging(resources.context.Get(), resources.clear_staging.Get(),
					resources.target_description, evidence.clear_map_result,
					evidence.clear_hash, evidence.clear_nonzero_bytes) ||
				!hash_staging(resources.context.Get(), resources.right_staging.Get(),
					resources.target_description, evidence.right_map_result,
					evidence.right_hash, evidence.right_nonzero_bytes))
			{
				evidence.error = failure::readback;
				current_failure.store(evidence.error, std::memory_order_release);
				current_state.store(gate_state::failed, std::memory_order_release);
				failure_count.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			evidence.right_changed_from_clear = evidence.right_hash != evidence.clear_hash;
			evidence.eyes_distinct = evidence.left_hash != evidence.right_hash;
			evidence.completed_present = event.frame_index;
			completed_present.store(event.frame_index, std::memory_order_release);
			if (evidence.left_nonzero_bytes == 0 || evidence.right_nonzero_bytes == 0 ||
				!evidence.right_changed_from_clear || !evidence.eyes_distinct ||
				!evidence.output_restored || !evidence.view_restored ||
				!evidence.command_immutable || evidence.replay_draw_calls == 0)
			{
				evidence.error = failure::content;
				current_failure.store(evidence.error, std::memory_order_release);
				current_state.store(gate_state::failed, std::memory_order_release);
				failure_count.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			evidence.error = failure::none;
			completions.fetch_add(1, std::memory_order_relaxed);
			current_state.store(gate_state::complete, std::memory_order_release);
			release_resources();
		}
	}

	void on_present_pre(const d3d11::present_event& event) noexcept
	{
		const auto state = current_state.load(std::memory_order_acquire);
		if (state == gate_state::waiting) prepare(event);
		else if (state == gate_state::readback_pending) poll_readback(event);
	}

	void on_present_post(const d3d11::present_event& event, const HRESULT result) noexcept
	{
		const auto state = current_state.load(std::memory_order_acquire);
		if ((state == gate_state::readback_pending || state == gate_state::complete) &&
			FAILED(result))
		{
			std::lock_guard lock(state_mutex);
			evidence.completed_present = event.frame_index;
			evidence.error = failure::present;
			current_failure.store(evidence.error, std::memory_order_release);
			current_state.store(gate_state::failed, std::memory_order_release);
			failure_count.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void invalidate_device(ID3D11DeviceContext* const context,
		const std::uint64_t generation) noexcept
	{
		std::lock_guard lock(state_mutex);
		if (resources.generation != generation || resources.context.Get() != context) return;
		release_resources();
		evidence.error = failure::device;
		current_failure.store(evidence.error, std::memory_order_release);
		current_state.store(gate_state::failed, std::memory_order_release);
		failure_count.fetch_add(1, std::memory_order_relaxed);
	}

	bool begin(transaction& output,
		const engine_stereo_binding::backend_claim& claim,
		const std::uintptr_t record) noexcept
	{
		if (output || active_transaction != nullptr || !claim || record == 0 ||
			claim.record != record || !engine_stereo_view::validate_finalized(claim.views))
		{
			return false;
		}
		auto expected = gate_state::ready;
		if (!current_state.compare_exchange_strong(expected, gate_state::active,
			std::memory_order_acq_rel, std::memory_order_acquire))
		{
			return false;
		}
		attempts.fetch_add(1, std::memory_order_relaxed);
		output.active = true;
		output.publication_sequence = claim.publication_sequence;
		output.record = record;
		output.owner_thread_id = GetCurrentThreadId();
		output.left = claim.views.eyes[0].bytes;
		output.right = claim.views.eyes[1].bytes;
		{
			std::lock_guard lock(state_mutex);
			evidence.publication_sequence = claim.publication_sequence;
			evidence.record = record;
			evidence.owner_thread_id = output.owner_thread_id;
		}
		latest_publication_sequence.store(claim.publication_sequence,
			std::memory_order_release);
		latest_record.store(record, std::memory_order_release);
		active_transaction = &output;
		return true;
	}

	void invoke_natural_copy(transaction& active, void* const backend_state,
		const h2_copy_fn original) noexcept
	{
		if (!active)
		{
			if (original != nullptr) original(backend_state);
			return;
		}
		++active.natural_copy_calls;
		if (backend_state == nullptr || original == nullptr ||
			GetCurrentThreadId() != active.owner_thread_id)
		{
			fail_transaction(active, failure::view_copy);
			if (original != nullptr) original(backend_state);
			return;
		}
		const auto address = reinterpret_cast<std::uintptr_t>(backend_state);
		if (active.backend_state != 0 && active.backend_state != address)
		{
			fail_transaction(active, failure::view_copy);
			original(backend_state);
			return;
		}
		active.backend_state = address;
		active.copy_original = original;
		if (!apply_view(active, active.left))
		{
			fail_transaction(active, failure::view_copy);
			return;
		}
		++active.natural_left_substitutions;
	}

	void enter_natural_dispatch(transaction& active) noexcept
	{
		if (!active) return;
		if (active.natural_draw.active || active.natural_draw_complete ||
			!resources.context ||
			!engine_stereo_draw_indexed::begin_replay(active.natural_draw,
				resources.context.Get()))
		{
			fail_transaction(active, failure::draw_replay);
		}
	}

	void leave_natural_dispatch(transaction& active) noexcept
	{
		if (!active) return;
		if (!active.natural_draw.active ||
			!engine_stereo_draw_indexed::end_replay(active.natural_draw))
		{
			fail_transaction(active, failure::draw_replay);
			return;
		}
		active.natural_draw_complete = true;
	}

	void execute(transaction& active, void* const commands, const int* const filter,
		const bool flagged_mode, const h2_dispatch_fn original) noexcept
	{
		if (!active) return;
		if (active.failed || original == nullptr || commands == nullptr ||
			active.backend_state == 0 || active.copy_original == nullptr ||
			active.natural_left_substitutions == 0 ||
			!active.natural_draw_complete || active.natural_draw.draw_calls == 0 ||
			GetCurrentThreadId() != active.owner_thread_id)
		{
			fail_transaction(active, failure::transaction);
			return;
		}

		std::lock_guard lock(state_mutex);
		if (!resources.device || !resources.context ||
			resources.generation != evidence.device_generation ||
			reinterpret_cast<std::uintptr_t>(resources.context.Get()) != evidence.context)
		{
			fail_transaction(active, failure::device);
			return;
		}
		evidence.publication_sequence = active.publication_sequence;
		evidence.record = active.record;
		evidence.owner_thread_id = active.owner_thread_id;
		evidence.natural_copy_calls = active.natural_copy_calls;
		evidence.natural_left_substitutions = active.natural_left_substitutions;
		evidence.command_before = engine_command_stream::capture(commands);
		if (!evidence.command_before.valid)
		{
			fail_transaction(active, failure::command_stream);
			return;
		}

		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> natural_view;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> natural_depth;
		resources.context->OMGetRenderTargets(1, &natural_view, &natural_depth);
		if (!natural_view || natural_depth)
		{
			fail_transaction(active, failure::output_binding);
			return;
		}
		Microsoft::WRL::ComPtr<ID3D11Resource> natural_resource;
		natural_view->GetResource(&natural_resource);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> natural_texture;
		if (!natural_resource || FAILED(natural_resource.As(&natural_texture)) ||
			!natural_texture)
		{
			fail_transaction(active, failure::output_binding);
			return;
		}
		D3D11_TEXTURE2D_DESC natural_description{};
		natural_texture->GetDesc(&natural_description);
		evidence.width = natural_description.Width;
		evidence.height = natural_description.Height;
		evidence.format = static_cast<std::uint32_t>(natural_description.Format);
		if (!supported_description(natural_description))
		{
			fail_transaction(active, failure::target_description);
			return;
		}
		if (!create_resources(natural_description))
		{
			release_resources();
			fail_transaction(active, failure::resource_creation);
			return;
		}

		bool output_modified{};
		bool right_view_applied{};
		const auto restore_state = [&]
		{
			if (output_modified)
			{
				auto* const restored_view = natural_view.Get();
				resources.context->OMSetRenderTargets(1, &restored_view, nullptr);
				output_modified = false;
				Microsoft::WRL::ComPtr<ID3D11RenderTargetView> verified_view;
				resources.context->OMGetRenderTargets(1, &verified_view, nullptr);
				evidence.output_restored = verified_view.Get() == natural_view.Get();
			}
			if (right_view_applied)
			{
				evidence.view_restored = apply_view(active, active.left);
				right_view_applied = false;
			}
			evidence.replay_view_substitutions = active.replay_view_substitutions;
		};
		const auto restore = gsl::finally(restore_state);

		resources.context->OMSetRenderTargets(0, nullptr, nullptr);
		output_modified = true;
		resources.context->CopyResource(resources.left_staging.Get(),
			natural_texture.Get());
		if (!apply_view(active, active.right))
		{
			fail_transaction(active, failure::view_copy);
			return;
		}
		right_view_applied = true;
		++active.replay_view_substitutions;

		auto* replay_view = resources.right_view.Get();
		resources.context->OMSetRenderTargets(1, &replay_view, nullptr);
		resources.context->ClearRenderTargetView(replay_view, clear_color);
		resources.context->OMSetRenderTargets(0, nullptr, nullptr);
		resources.context->CopyResource(resources.clear_staging.Get(),
			resources.right_target.Get());
		resources.context->OMSetRenderTargets(1, &replay_view, nullptr);

		engine_stereo_draw_indexed::replay_counter counter{};
		engine_stereo_output_merger::replay_guard output_guard{};
		if (!engine_stereo_draw_indexed::begin_replay(counter,
			resources.context.Get()) ||
			!engine_stereo_output_merger::begin_replay_guard(output_guard,
				resources.context.Get()))
		{
			if (counter.active) (void)engine_stereo_draw_indexed::end_replay(counter);
			fail_transaction(active, failure::draw_replay);
			return;
		}
		original(commands, filter, flagged_mode);
		const auto output_guard_valid =
			engine_stereo_output_merger::end_replay_guard(output_guard);
		const auto replay_valid = engine_stereo_draw_indexed::end_replay(counter);
		++active.replay_dispatches;
		replay_dispatches.fetch_add(1, std::memory_order_relaxed);
		replay_draw_calls.fetch_add(counter.draw_calls, std::memory_order_relaxed);
		evidence.replay_dispatches = active.replay_dispatches;
		evidence.natural_draw_calls = active.natural_draw.draw_calls;
		evidence.replay_draw_calls = counter.draw_calls;
		evidence.natural_draw_hash = active.natural_draw.draw_call_hash;
		evidence.replay_draw_hash = counter.draw_call_hash;
		evidence.natural_draw_trace = active.natural_draw;
		evidence.replay_draw_trace = counter;
		evidence.draw_comparison = engine_stereo_draw_indexed::compare_replay(
			evidence.natural_draw_trace, evidence.replay_draw_trace);
		evidence.replay_output_bind_calls = output_guard.output_bind_calls;
		evidence.replay_draw_matches = evidence.draw_comparison.semantic_matches;
		evidence.command_after = engine_command_stream::capture(commands);
		evidence.command_immutable = engine_command_stream::identical(
			evidence.command_before, evidence.command_after);
		restore_state();
		if (!evidence.output_restored)
		{
			fail_transaction(active, failure::output_binding);
			return;
		}
		if (!evidence.view_restored)
		{
			fail_transaction(active, failure::view_copy);
			return;
		}
		if (!evidence.command_immutable)
		{
			fail_transaction(active, failure::command_stream);
			return;
		}
		if (!replay_valid || !evidence.replay_draw_matches || !output_guard_valid)
		{
			fail_transaction(active, !output_guard_valid
				? failure::output_binding : failure::draw_replay);
			return;
		}

		resources.context->CopyResource(resources.right_staging.Get(),
			resources.right_target.Get());
		resources.context->End(resources.completion_query.Get());
		current_state.store(gate_state::readback_pending, std::memory_order_release);
	}

	void end(transaction& active, const bool natural_dispatch_returned) noexcept
	{
		if (!active) return;
		if (!natural_dispatch_returned || active.natural_copy_calls == 0 ||
			active.natural_left_substitutions == 0 || active.replay_dispatches != 1 ||
			!active.natural_draw_complete || active.natural_draw.active ||
			current_state.load(std::memory_order_acquire) !=
				gate_state::readback_pending)
		{
			fail_transaction(active, failure::transaction);
		}
		if (active.failed)
		{
			fail_global(active.error == failure::none
				? failure::transaction : active.error);
		}
		if (active_transaction == &active) active_transaction = nullptr;
		active = {};
	}

	status get_status() noexcept
	{
		return {
			current_state.load(std::memory_order_acquire),
			current_failure.load(std::memory_order_acquire),
			preparations.load(std::memory_order_acquire),
			attempts.load(std::memory_order_acquire),
			completions.load(std::memory_order_acquire),
			failure_count.load(std::memory_order_acquire),
			replay_dispatches.load(std::memory_order_acquire),
			replay_draw_calls.load(std::memory_order_acquire),
			readback_polls.load(std::memory_order_acquire),
			latest_publication_sequence.load(std::memory_order_acquire),
			latest_record.load(std::memory_order_acquire),
			started_present.load(std::memory_order_acquire),
			completed_present.load(std::memory_order_acquire),
		};
	}

	bool read_report(report& output) noexcept
	{
		const auto state = current_state.load(std::memory_order_acquire);
		if (state != gate_state::complete && state != gate_state::failed) return false;
		std::lock_guard lock(state_mutex);
		output = evidence;
		return evidence.publication_sequence != 0 || evidence.error != failure::none;
	}

	const char* to_string(const gate_state state) noexcept
	{
		switch (state)
		{
		case gate_state::waiting: return "waiting";
		case gate_state::ready: return "ready";
		case gate_state::active: return "active";
		case gate_state::readback_pending: return "readback_pending";
		case gate_state::complete: return "complete";
		case gate_state::failed: return "failed";
		default: return "unknown";
		}
	}

	const char* to_string(const failure error) noexcept
	{
		switch (error)
		{
		case failure::none: return "none";
		case failure::prerequisite: return "prerequisite";
		case failure::scene_batch_selection: return "scene_batch_selection";
		case failure::device: return "device";
		case failure::target_description: return "target_description";
		case failure::resource_creation: return "resource_creation";
		case failure::transaction: return "transaction";
		case failure::view_copy: return "view_copy";
		case failure::output_binding: return "output_binding";
		case failure::command_stream: return "command_stream";
		case failure::draw_replay: return "draw_replay";
		case failure::readback: return "readback";
		case failure::content: return "content";
		case failure::present: return "present";
		case failure::timeout: return "timeout";
		default: return "unknown";
		}
	}

	bool reset() noexcept
	{
		const auto state = current_state.load(std::memory_order_acquire);
		if (state == gate_state::active || state == gate_state::readback_pending ||
			active_transaction != nullptr)
		{
			return false;
		}
		std::lock_guard lock(state_mutex);
		release_resources();
		evidence = {};
		preparations.store(0, std::memory_order_relaxed);
		attempts.store(0, std::memory_order_relaxed);
		completions.store(0, std::memory_order_relaxed);
		failure_count.store(0, std::memory_order_relaxed);
		replay_dispatches.store(0, std::memory_order_relaxed);
		replay_draw_calls.store(0, std::memory_order_relaxed);
		readback_polls.store(0, std::memory_order_relaxed);
		latest_publication_sequence.store(0, std::memory_order_relaxed);
		latest_record.store(0, std::memory_order_relaxed);
		started_present.store(0, std::memory_order_relaxed);
		completed_present.store(0, std::memory_order_relaxed);
		current_failure.store(failure::none, std::memory_order_release);
		current_state.store(gate_state::waiting, std::memory_order_release);
		return true;
	}
}
