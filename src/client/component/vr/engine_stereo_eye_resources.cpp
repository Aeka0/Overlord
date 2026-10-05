#include <std_include.hpp>

#include "engine_stereo_eye_resources.hpp"
#include "auxiliary_scene.hpp"
#include "engine_stereo_pointer_cache.hpp"

#include "engine_stereo_output_merger.hpp"
#include "engine_stereo_resource_ops.hpp"

#include "utils/hook.hpp"
#include "utils/hook_validation.hpp"

#include <atomic>
#include <mutex>
#include <type_traits>

namespace vr::engine_stereo_eye_resources
{
	namespace
	{
		constexpr std::array<std::size_t, shader_stage_count> shader_vtable_slots{
			8, 25, 31, 59, 63, 67,
		};
		constexpr std::size_t unordered_access_vtable_slot = 68;
		constexpr std::size_t maximum_target_views = 64;
		constexpr std::size_t maximum_foreign_views_per_pair = 1024;

		using shader_resource_fn = void(__stdcall*)(ID3D11DeviceContext*, UINT,
			UINT, ID3D11ShaderResourceView* const*);
		using unordered_access_fn = void(__stdcall*)(ID3D11DeviceContext*, UINT,
			UINT, ID3D11UnorderedAccessView* const*, const UINT*);

		struct isolated_target
		{
			std::uint32_t target_id{};
			role target_role{role::color};
			Microsoft::WRL::ComPtr<ID3D11View> owner_view;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> original;
			std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, auxiliary_scene::view_count> eyes;
			// Owned by the same resource generation as the auxiliary texture. A
			// moving optical near plane can reset history on consecutive pairs.
			std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,
				D3D11_REQ_MIP_LEVELS> auxiliary_clear_views;
			D3D11_TEXTURE2D_DESC description{};
		};

		template <typename View>
		struct target_view_mapping
		{
			Microsoft::WRL::ComPtr<View> original;
			std::array<Microsoft::WRL::ComPtr<View>, auxiliary_scene::view_count> eyes;
			std::uint32_t target_index{};
		};

		template <typename View>
		struct target_view_cache
		{
			std::array<target_view_mapping<View>, maximum_target_views> entries{};
			std::size_t count{};
			stereo_pointer_index<512> known;
			stereo_pointer_cache<maximum_foreign_views_per_pair> foreign;
		};

		struct persistent_state
		{
			bool ready{};
			std::uintptr_t context{};
			std::uint64_t generation{};
			std::uint32_t owner_thread{};
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			std::array<isolated_target, isolated_target_count> targets{};
			target_view_cache<ID3D11RenderTargetView> render_targets;
			target_view_cache<ID3D11DepthStencilView> depth_stencils;
			target_view_cache<ID3D11ShaderResourceView> shader_resources;
			target_view_cache<ID3D11UnorderedAccessView> unordered_access;
		};

		struct pair_state
		{
			bool active{};
			bool failed{};
			bool exact_read_enabled{};
			std::uint64_t pair_id{};
			std::uintptr_t context{};
			std::uint64_t generation{};
			std::uint32_t owner_thread{};
			std::uint32_t current_eye{auxiliary_scene::no_view};
			bool auxiliary_complete{};
			std::uint8_t completed_eye_mask{};
			failure error{failure::none};
		};

		struct thread_pair_token
		{
			std::uint64_t pair_id{};
			std::uintptr_t context{};
			std::uint64_t generation{};
		};

		std::array<utils::hook::detour, shader_stage_count> shader_hooks;
		utils::hook::detour unordered_access_hook;
		std::mutex hook_mutex;
		std::mutex state_mutex;
		std::atomic_bool hooks_installed{};
		std::atomic_uintptr_t expected_context{};
		std::atomic_uint64_t expected_generation{};
		std::array<std::atomic_uintptr_t, shader_stage_count> shader_hook_targets{};
		std::atomic_uintptr_t unordered_access_hook_target{};
		std::atomic_uint64_t hook_failures{};
		std::atomic_uint64_t pair_attempts{};
		std::atomic_uint64_t pair_completions{};
		std::atomic_uint64_t pair_failures{};
		std::atomic_uint64_t resource_rebuilds{};
		std::atomic_uint64_t seed_copies{};
		std::atomic_uint64_t view_creations{};
		std::atomic_uint64_t view_capacity_failures{};
		std::atomic_uint64_t boundary_rebind_attempts{};
		std::atomic_uint64_t boundary_rebind_completions{};
		std::atomic_uint64_t boundary_rebind_failures{};
		std::atomic_uint64_t boundary_rebind_calls{};
		std::atomic_uint64_t boundary_original_views_remaining{};
		std::atomic_uint64_t restore_rebind_attempts{};
		std::atomic_uint64_t restore_rebind_completions{};
		std::atomic_uint64_t restore_rebind_failures{};
		std::atomic_uint64_t restore_rebind_calls{};
		std::atomic_uint64_t restore_isolated_views_remaining{};
		std::atomic_uint64_t resource_replacements{};
		std::atomic_uint64_t render_target_replacements{};
		std::atomic_uint64_t depth_stencil_replacements{};
		std::atomic_uint64_t unordered_access_replacements{};
		std::array<std::atomic_uint64_t, shader_stage_count>
			shader_resource_replacements{};
		std::atomic_bool resources_ready{};
		std::atomic_bool pair_active{};
		std::atomic_bool pair_failed{};
		std::atomic_bool cleanup_quarantined{};
		std::atomic_bool invalidation_pending{};
		std::atomic_uint64_t active_pair{};
		std::atomic_uint32_t active_owner_thread{};
		std::atomic_uint32_t active_eye{auxiliary_scene::no_view};
		std::atomic_uintptr_t quarantine_context{};
		std::atomic_uint64_t quarantine_generation{};
		std::atomic_uint64_t quarantine_pair{};
		std::atomic_uint32_t quarantine_owner_thread{};
		std::atomic_uint64_t quarantine_events{};
		std::atomic_uint64_t quarantine_rejections{};
		std::atomic_uint64_t cleanup_retries{};
		std::atomic_uint64_t cleanup_recoveries{};
		std::atomic_uint64_t deferred_invalidations{};
		std::atomic<failure> last_failure{failure::none};
		std::atomic_bool exact_read_enabled{};
		std::atomic_uint64_t exact_read_pair{};
		std::atomic_uint64_t exact_read_observations{};
		std::atomic_uint64_t exact_read_expected{};
		std::atomic_uint64_t exact_read_unexpected{};
		std::array<std::array<std::atomic_uint64_t, exact_read_binding_count>, 2>
			exact_read_bindings{};
		std::atomic_uint64_t exact_read_last_pair{};
		std::atomic_uint32_t exact_read_last_eye{2};
		std::atomic_uintptr_t exact_read_last_caller{};
		std::atomic_uint32_t exact_read_last_output_target{};
		std::atomic_uintptr_t exact_read_last_view{};
		std::atomic_uintptr_t exact_read_last_resource{};
		std::atomic_uint32_t exact_read_last_binding{};
		std::atomic_uint32_t exact_read_last_view_dimension{};
		std::atomic_uint32_t exact_read_last_format{};
		std::atomic_uint32_t exact_read_last_most_detailed_mip{};
		std::atomic_uint32_t exact_read_last_mip_levels{};
		std::atomic_uint32_t exact_read_last_first_array_slice{};
		std::atomic_uint32_t exact_read_last_array_size{};
		persistent_state resources;
		pair_state pair;
		thread_local thread_pair_token current_pair{};

		[[nodiscard]] bool valid_resource_description(const role target_role,
			const D3D11_TEXTURE2D_DESC& value) noexcept
		{
			if (value.Width == 0 || value.Height == 0 || value.MipLevels == 0 ||
				value.ArraySize != 1 || value.Format == DXGI_FORMAT_UNKNOWN ||
				value.SampleDesc.Count != 1 || value.SampleDesc.Quality != 0 ||
				value.Usage != D3D11_USAGE_DEFAULT || value.CPUAccessFlags != 0 ||
				value.MiscFlags != 0 ||
				(value.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0)
			{
				return false;
			}
			const auto required = target_role == role::color ?
				D3D11_BIND_RENDER_TARGET : D3D11_BIND_DEPTH_STENCIL;
			return (value.BindFlags & required) != 0;
		}

		void publish_failure(const failure value) noexcept
		{
			last_failure.store(value, std::memory_order_release);
			if (pair.active)
			{
				pair.failed = true;
				if (pair.error == failure::none) pair.error = value;
				pair_failed.store(true, std::memory_order_release);
			}
		}

		[[nodiscard]] bool owns_pair_on_current_thread() noexcept
		{
			return pair.active && GetCurrentThreadId() == pair.owner_thread &&
				current_pair.pair_id == pair.pair_id &&
				current_pair.context == pair.context &&
				current_pair.generation == pair.generation;
		}

		// Hook callbacks can arrive on arbitrary threads. They must prove the
		// published owner identity using atomics and that thread's value-only TLS
		// token before reading any non-atomic pair field. The admitted owner cannot
		// race retire_pair because both execute synchronously on that same thread.
		[[nodiscard]] bool owner_hook_token_matches() noexcept
		{
			if (!pair_active.load(std::memory_order_acquire) ||
				active_owner_thread.load(std::memory_order_acquire) !=
					GetCurrentThreadId())
			{
				return false;
			}
			const auto pair_id = active_pair.load(std::memory_order_acquire);
			const auto context = expected_context.load(std::memory_order_acquire);
			const auto generation = expected_generation.load(std::memory_order_acquire);
			return pair_id != 0 && context != 0 && generation != 0 &&
				current_pair.pair_id == pair_id && current_pair.context == context &&
				current_pair.generation == generation;
		}

		void enter_cleanup_quarantine(const failure reason) noexcept
		{
			const auto was_quarantined = cleanup_quarantined.exchange(true,
				std::memory_order_acq_rel);
			if (!was_quarantined)
				quarantine_events.fetch_add(1, std::memory_order_relaxed);
			quarantine_context.store(pair.context, std::memory_order_release);
			quarantine_generation.store(pair.generation, std::memory_order_release);
			quarantine_pair.store(pair.pair_id, std::memory_order_release);
			quarantine_owner_thread.store(pair.owner_thread, std::memory_order_release);
			last_failure.store(reason, std::memory_order_release);
			pair_failed.store(true, std::memory_order_release);
			if (owns_pair_on_current_thread())
			{
				pair.failed = true;
				if (pair.error == failure::none) pair.error = reason;
			}
		}

		void clear_cleanup_quarantine() noexcept
		{
			cleanup_quarantined.store(false, std::memory_order_release);
			invalidation_pending.store(false, std::memory_order_release);
			quarantine_context.store(0, std::memory_order_release);
			quarantine_generation.store(0, std::memory_order_release);
			quarantine_pair.store(0, std::memory_order_release);
			quarantine_owner_thread.store(0, std::memory_order_release);
		}

		void defer_generation_release(const std::uintptr_t context,
			const std::uint64_t generation, const std::uint64_t pair_id,
			const std::uint32_t owner_thread, const failure reason) noexcept
		{
			const auto was_quarantined = cleanup_quarantined.exchange(true,
				std::memory_order_acq_rel);
			if (!was_quarantined)
				quarantine_events.fetch_add(1, std::memory_order_relaxed);
			quarantine_context.store(context, std::memory_order_release);
			quarantine_generation.store(generation, std::memory_order_release);
			quarantine_pair.store(pair_id, std::memory_order_release);
			quarantine_owner_thread.store(owner_thread, std::memory_order_release);
			last_failure.store(reason, std::memory_order_release);
			pair_failed.store(pair_id != 0, std::memory_order_release);
			invalidation_pending.store(true, std::memory_order_release);
			deferred_invalidations.fetch_add(1, std::memory_order_relaxed);
		}

		void apply_deferred_failure_on_owner() noexcept
		{
			if (!cleanup_quarantined.load(std::memory_order_acquire) ||
				!owns_pair_on_current_thread())
			{
				return;
			}
			pair.failed = true;
			if (pair.error == failure::none)
				pair.error = last_failure.load(std::memory_order_acquire);
			pair_failed.store(true, std::memory_order_release);
		}

		[[nodiscard]] bool active_isolated_eye(ID3D11DeviceContext* const context) noexcept
		{
			if (cleanup_quarantined.load(std::memory_order_acquire) ||
				invalidation_pending.load(std::memory_order_acquire) ||
				!owner_hook_token_matches() || !owns_pair_on_current_thread() ||
				pair.current_eye >= auxiliary_scene::view_count) return false;
			if (reinterpret_cast<std::uintptr_t>(context) != pair.context ||
				pair.context != expected_context.load(std::memory_order_acquire) ||
				pair.generation != expected_generation.load(
					std::memory_order_acquire))
			{
				publish_failure(failure::context);
				return false;
			}
			return true;
		}

		template <typename View>
		void clear_cache(target_view_cache<View>& cache) noexcept
		{
			for (std::size_t index{}; index < cache.count; ++index)
				cache.entries[index] = {};
			cache.count = 0;
			cache.known.clear();
			cache.foreign.clear();
		}

		void clear_all_caches() noexcept
		{
			clear_cache(resources.render_targets);
			clear_cache(resources.depth_stencils);
			clear_cache(resources.shader_resources);
			clear_cache(resources.unordered_access);
		}

		void clear_foreign_caches() noexcept
		{
			resources.render_targets.foreign.clear();
			resources.depth_stencils.foreign.clear();
			resources.shader_resources.foreign.clear();
			resources.unordered_access.foreign.clear();
		}

		template <typename View>
		[[nodiscard]] bool foreign_cached(const target_view_cache<View>& cache,
			View* const view) noexcept
		{
			return cache.foreign.contains(reinterpret_cast<std::uintptr_t>(view));
		}

		template <typename View>
		void cache_foreign(target_view_cache<View>& cache, View* const view) noexcept
		{
			cache.foreign.insert(reinterpret_cast<std::uintptr_t>(view));
		}

		template <typename View>
		[[nodiscard]] HRESULT create_matching_view(ID3D11Device* const device,
			ID3D11Texture2D* const texture, View* const original,
			View** const output) noexcept
		{
			if constexpr (std::is_same_v<View, ID3D11RenderTargetView>)
			{
				D3D11_RENDER_TARGET_VIEW_DESC description{};
				original->GetDesc(&description);
				return device->CreateRenderTargetView(texture, &description, output);
			}
			else if constexpr (std::is_same_v<View, ID3D11DepthStencilView>)
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC description{};
				original->GetDesc(&description);
				return device->CreateDepthStencilView(texture, &description, output);
			}
			else if constexpr (std::is_same_v<View, ID3D11ShaderResourceView>)
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC description{};
				original->GetDesc(&description);
				return device->CreateShaderResourceView(texture, &description, output);
			}
			else
			{
				D3D11_UNORDERED_ACCESS_VIEW_DESC description{};
				original->GetDesc(&description);
				return device->CreateUnorderedAccessView(texture, &description, output);
			}
		}

		template <typename View>
		[[nodiscard]] View* create_target_view_mapping(
			target_view_cache<View>& cache, View* const original,
			const std::uint32_t target_index) noexcept
		{
			if (cache.count >= cache.entries.size())
			{
				view_capacity_failures.fetch_add(1, std::memory_order_relaxed);
				publish_failure(failure::view_capacity);
				return original;
			}
			std::array<Microsoft::WRL::ComPtr<View>, auxiliary_scene::view_count> replacements;
			for (std::size_t eye{}; eye < replacements.size(); ++eye)
			{
				if (!resources.targets[target_index].eyes[eye]) continue;
				const auto result = create_matching_view(resources.device.Get(),
					resources.targets[target_index].eyes[eye].Get(), original,
					replacements[eye].GetAddressOf());
				if (FAILED(result) || !replacements[eye])
				{
					publish_failure(failure::view_creation);
					return original;
				}
			}
			auto& entry = cache.entries[cache.count++];
			entry.original = original;
			entry.eyes = std::move(replacements);
			entry.target_index = target_index;
			const auto mapping_index = cache.count - 1;
			cache.known.insert(reinterpret_cast<std::uintptr_t>(entry.original.Get()), mapping_index);
			for (const auto& eye : entry.eyes) if (eye)
			{
				cache.known.insert(reinterpret_cast<std::uintptr_t>(eye.Get()), mapping_index);
				view_creations.fetch_add(1, std::memory_order_relaxed);
			}
			return entry.eyes[pair.current_eye < auxiliary_scene::view_count ? pair.current_eye : 0].Get();
		}

		template <typename View>
		[[nodiscard]] View* rewrite_admitted_view(target_view_cache<View>& cache,
			View* const original) noexcept
		{
			const auto identity = reinterpret_cast<std::uintptr_t>(original);
			if (const auto* index = cache.known.find(identity); index && *index < cache.count)
				return cache.entries[*index].eyes[pair.current_eye].Get();
			// Non-target SRVs dominate scene draws. Even a negative-cache hit used
			// to scan every original/left/right mapping first.
			if (foreign_cached(cache, original)) return original;
			for (std::size_t index{}; index < cache.count; ++index)
			{
				const auto& entry = cache.entries[index];
				if (entry.original.Get() == original || entry.eyes[0].Get() == original ||
					entry.eyes[1].Get() == original || entry.eyes[2].Get() == original)
				{
					cache.known.insert(identity, index);
					return entry.eyes[pair.current_eye].Get();
				}
			}

			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			original->GetResource(resource.GetAddressOf());
			if (!resource)
			{
				publish_failure(failure::resource_contract);
				return original;
			}
			for (std::uint32_t index{}; index < resources.targets.size(); ++index)
			{
				if (resource.Get() == resources.targets[index].original.Get())
					return create_target_view_mapping(cache, original, index);
			}
			cache_foreign(cache, original);
			return original;
		}

		template <typename View>
		[[nodiscard]] View* rewrite_view(target_view_cache<View>& cache,
			ID3D11DeviceContext* const context, View* const original) noexcept
		{
			if (original == nullptr || !active_isolated_eye(context)) return original;
			return rewrite_admitted_view(cache, original);
		}

		template <typename View>
		[[nodiscard]] View* restore_view(const target_view_cache<View>& cache,
			View* const current) noexcept
		{
			if (current == nullptr) return nullptr;
			if (const auto* index = cache.known.find(reinterpret_cast<std::uintptr_t>(current));
				index && *index < cache.count) return cache.entries[*index].original.Get();
			if (foreign_cached(cache, current)) return current;
			for (std::size_t index{}; index < cache.count; ++index)
			{
				if (cache.entries[index].eyes[0].Get() == current ||
					cache.entries[index].eyes[1].Get() == current || cache.entries[index].eyes[2].Get() == current)
					return cache.entries[index].original.Get();
			}
			return current;
		}

		template <typename View>
		void restore_bound_view(const target_view_cache<View>& cache,
			View*& current) noexcept
		{
			auto* const restored = restore_view(cache, current);
			if (restored == current) return;
			if (restored != nullptr) restored->AddRef();
			if (current != nullptr) current->Release();
			current = restored;
		}

		[[nodiscard]] ID3D11RenderTargetView* rewrite_render_target(
			ID3D11DeviceContext* const context,
			ID3D11RenderTargetView* const original) noexcept
		{
			auto* const output = rewrite_view(resources.render_targets, context, original);
			if (output != original)
				render_target_replacements.fetch_add(1, std::memory_order_relaxed);
			return output;
		}

		[[nodiscard]] ID3D11DepthStencilView* rewrite_depth_stencil(
			ID3D11DeviceContext* const context,
			ID3D11DepthStencilView* const original) noexcept
		{
			auto* const output = rewrite_view(resources.depth_stencils, context, original);
			if (output != original)
				depth_stencil_replacements.fetch_add(1, std::memory_order_relaxed);
			return output;
		}

		[[nodiscard]] ID3D11UnorderedAccessView* rewrite_unordered_access(
			ID3D11DeviceContext* const context,
			ID3D11UnorderedAccessView* const original) noexcept
		{
			auto* const output = rewrite_view(resources.unordered_access, context, original);
			if (output != original)
				unordered_access_replacements.fetch_add(1, std::memory_order_relaxed);
			return output;
		}

		[[nodiscard]] ID3D11Resource* rewrite_resource(
			ID3D11DeviceContext* const context, ID3D11Resource* const original,
			engine_stereo_resource_ops::access) noexcept
		{
			if (original == nullptr || !active_isolated_eye(context)) return original;
			for (const auto& target : resources.targets)
			{
				if (target.original.Get() == original || target.eyes[0].Get() == original ||
					target.eyes[1].Get() == original || target.eyes[2].Get() == original)
				{
					resource_replacements.fetch_add(1, std::memory_order_relaxed);
					return target.eyes[pair.current_eye].Get();
				}
			}
			return original;
		}

		[[nodiscard]] ID3D11ShaderResourceView* rewrite_resource_operation_srv(
			ID3D11DeviceContext* const context,
			ID3D11ShaderResourceView* const original,
			engine_stereo_resource_ops::access) noexcept
		{
			return rewrite_view(resources.shader_resources, context, original);
		}

		[[nodiscard]] ID3D11UnorderedAccessView* rewrite_resource_operation_uav(
			ID3D11DeviceContext* const context,
			ID3D11UnorderedAccessView* const original,
			engine_stereo_resource_ops::access) noexcept
		{
			return rewrite_unordered_access(context, original);
		}

		template <typename View, std::size_t Size>
		void release_views(std::array<View*, Size>& views) noexcept
		{
			for (auto*& view : views)
			{
				if (view != nullptr) view->Release();
				view = nullptr;
			}
		}

		template <typename View, std::size_t Size>
		[[nodiscard]] UINT bound_prefix(const std::array<View*, Size>& views) noexcept
		{
			for (auto index = views.size(); index != 0; --index)
				if (views[index - 1] != nullptr) return static_cast<UINT>(index);
			return 0;
		}

		void get_shader_resources(const std::size_t stage,
			ID3D11DeviceContext* const context, const UINT count,
			ID3D11ShaderResourceView** const output) noexcept
		{
			switch (stage)
			{
			case 0: context->PSGetShaderResources(0, count, output); break;
			case 1: context->VSGetShaderResources(0, count, output); break;
			case 2: context->GSGetShaderResources(0, count, output); break;
			case 3: context->HSGetShaderResources(0, count, output); break;
			case 4: context->DSGetShaderResources(0, count, output); break;
			case 5: context->CSGetShaderResources(0, count, output); break;
			default: break;
			}
		}

		void set_shader_resources(const std::size_t stage,
			ID3D11DeviceContext* const context, const UINT count,
			ID3D11ShaderResourceView* const* const views) noexcept
		{
			switch (stage)
			{
			case 0: context->PSSetShaderResources(0, count, views); break;
			case 1: context->VSSetShaderResources(0, count, views); break;
			case 2: context->GSSetShaderResources(0, count, views); break;
			case 3: context->HSSetShaderResources(0, count, views); break;
			case 4: context->DSSetShaderResources(0, count, views); break;
			case 5: context->CSSetShaderResources(0, count, views); break;
			default: break;
			}
		}

		template <typename View>
		auto& cache_for_view() noexcept
		{
			if constexpr (std::is_same_v<View, ID3D11ShaderResourceView>) return resources.shader_resources;
			else if constexpr (std::is_same_v<View, ID3D11RenderTargetView>) return resources.render_targets;
			else if constexpr (std::is_same_v<View, ID3D11DepthStencilView>) return resources.depth_stencils;
			else return resources.unordered_access;
		}

		template <typename View>
		[[nodiscard]] bool uses_target_resource(View* const view,
			const bool isolated) noexcept
		{
			if (view == nullptr) return false;
			// The mapping owns these immutable COM views. Reuse their exact resource
			// identity during validation/restoration too; unknown views still query.
			const auto& cache = cache_for_view<View>();
			if (const auto* index = cache.known.find(reinterpret_cast<std::uintptr_t>(view));
				index && *index < cache.count)
				return isolated != (cache.entries[*index].original.Get() == view);
			// A view not rewritten by routing can still alias a private eye texture.
			// The negative routing cache is not proof that cleanup is complete.
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			view->GetResource(resource.GetAddressOf());
			if (!resource)
			{
				publish_failure(failure::resource_contract);
				return true;
			}
			for (const auto& target : resources.targets)
			{
				if (isolated ? (resource.Get() == target.eyes[0].Get() ||
					resource.Get() == target.eyes[1].Get() || resource.Get() == target.eyes[2].Get()) :
					resource.Get() == target.original.Get())
					return true;
			}
			return false;
		}

		[[nodiscard]] std::uint64_t count_bound_target_views(
			ID3D11DeviceContext* const context, const bool isolated) noexcept
		{
			std::uint64_t remaining{};
			std::array<ID3D11ShaderResourceView*,
				D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> shader_views{};
			for (std::size_t stage{}; stage < shader_stage_count; ++stage)
			{
				get_shader_resources(stage, context,
					static_cast<UINT>(shader_views.size()), shader_views.data());
				for (auto* const view : shader_views)
					remaining += uses_target_resource(view, isolated) ? 1 : 0;
				release_views(shader_views);
			}

			std::array<ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets{};
			ID3D11DepthStencilView* depth{};
			context->OMGetRenderTargets(static_cast<UINT>(render_targets.size()),
				render_targets.data(), &depth);
			for (auto* const view : render_targets)
				remaining += uses_target_resource(view, isolated) ? 1 : 0;
			remaining += uses_target_resource(depth, isolated) ? 1 : 0;
			release_views(render_targets);
			if (depth != nullptr) depth->Release();

			std::array<ID3D11UnorderedAccessView*,
				D3D11_PS_CS_UAV_REGISTER_COUNT> unordered_views{};
			context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0,
				static_cast<UINT>(unordered_views.size()), unordered_views.data());
			for (auto* const view : unordered_views)
				remaining += uses_target_resource(view, isolated) ? 1 : 0;
			release_views(unordered_views);
			context->CSGetUnorderedAccessViews(0,
				static_cast<UINT>(unordered_views.size()), unordered_views.data());
			for (auto* const view : unordered_views)
				remaining += uses_target_resource(view, isolated) ? 1 : 0;
			release_views(unordered_views);
			return remaining;
		}

		[[nodiscard]] bool rebind_inherited_state(
			ID3D11DeviceContext* const context) noexcept
		{
			boundary_rebind_attempts.fetch_add(1, std::memory_order_relaxed);
			if (!active_isolated_eye(context))
			{
				boundary_rebind_failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}

			std::array<ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets{};
			ID3D11DepthStencilView* depth{};
			context->OMGetRenderTargets(static_cast<UINT>(render_targets.size()),
				render_targets.data(), &depth);
			const auto render_target_count = bound_prefix(render_targets);
			std::array<ID3D11UnorderedAccessView*,
				D3D11_PS_CS_UAV_REGISTER_COUNT> om_unordered{};
			context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0,
				static_cast<UINT>(om_unordered.size()), om_unordered.data());
			const auto om_unordered_count = bound_prefix(om_unordered);
			if (om_unordered_count != 0)
			{
				UINT unordered_start{};
				while (unordered_start < om_unordered_count &&
					om_unordered[unordered_start] == nullptr) ++unordered_start;
				if (unordered_start < render_target_count)
				{
					publish_failure(failure::binding_contract);
				}
				else
				{
					std::array<UINT, D3D11_PS_CS_UAV_REGISTER_COUNT> initial_counts{};
					initial_counts.fill(D3D11_KEEP_UNORDERED_ACCESS_VIEWS);
					context->OMSetRenderTargetsAndUnorderedAccessViews(
						render_target_count,
						render_target_count == 0 ? nullptr : render_targets.data(), depth,
						unordered_start, om_unordered_count - unordered_start,
						om_unordered.data() + unordered_start,
						initial_counts.data() + unordered_start);
					boundary_rebind_calls.fetch_add(1, std::memory_order_relaxed);
				}
			}
			else if (render_target_count != 0 || depth != nullptr)
			{
				context->OMSetRenderTargets(render_target_count,
					render_target_count == 0 ? nullptr : render_targets.data(), depth);
				boundary_rebind_calls.fetch_add(1, std::memory_order_relaxed);
			}
			release_views(render_targets);
			if (depth != nullptr) depth->Release();
			release_views(om_unordered);

			std::array<ID3D11UnorderedAccessView*,
				D3D11_PS_CS_UAV_REGISTER_COUNT> cs_unordered{};
			context->CSGetUnorderedAccessViews(0,
				static_cast<UINT>(cs_unordered.size()), cs_unordered.data());
			const auto cs_unordered_count = bound_prefix(cs_unordered);
			if (cs_unordered_count != 0)
			{
				std::array<UINT, D3D11_PS_CS_UAV_REGISTER_COUNT> initial_counts{};
				initial_counts.fill(D3D11_KEEP_UNORDERED_ACCESS_VIEWS);
				context->CSSetUnorderedAccessViews(0, cs_unordered_count,
					cs_unordered.data(), initial_counts.data());
				boundary_rebind_calls.fetch_add(1, std::memory_order_relaxed);
			}
			release_views(cs_unordered);

			std::array<ID3D11ShaderResourceView*,
				D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> shader_views{};
			for (std::size_t stage{}; stage < shader_stage_count; ++stage)
			{
				get_shader_resources(stage, context,
					static_cast<UINT>(shader_views.size()), shader_views.data());
				const auto count = bound_prefix(shader_views);
				if (count != 0)
				{
					set_shader_resources(stage, context, count, shader_views.data());
					boundary_rebind_calls.fetch_add(1, std::memory_order_relaxed);
				}
				release_views(shader_views);
			}

			if (pair.failed)
			{
				boundary_rebind_failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			const auto remaining = count_bound_target_views(context, false);
			boundary_original_views_remaining.fetch_add(remaining,
				std::memory_order_relaxed);
			if (remaining != 0 || pair.failed)
			{
				publish_failure(failure::binding_contract);
				boundary_rebind_failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			boundary_rebind_completions.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		[[nodiscard]] bool restore_inherited_state(
			ID3D11DeviceContext* const context) noexcept
		{
			restore_rebind_attempts.fetch_add(1, std::memory_order_relaxed);
			if (context == nullptr || !owns_pair_on_current_thread() ||
				reinterpret_cast<std::uintptr_t>(context) != pair.context)
			{
				publish_failure(failure::lifecycle);
				restore_rebind_failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}

			std::array<ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets{};
			ID3D11DepthStencilView* depth{};
			context->OMGetRenderTargets(static_cast<UINT>(render_targets.size()),
				render_targets.data(), &depth);
			const auto render_target_count = bound_prefix(render_targets);
			for (auto*& view : render_targets)
				restore_bound_view(resources.render_targets, view);
			restore_bound_view(resources.depth_stencils, depth);

			std::array<ID3D11UnorderedAccessView*,
				D3D11_PS_CS_UAV_REGISTER_COUNT> om_unordered{};
			context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0,
				static_cast<UINT>(om_unordered.size()), om_unordered.data());
			const auto om_unordered_count = bound_prefix(om_unordered);
			for (auto*& view : om_unordered)
				restore_bound_view(resources.unordered_access, view);

			// Disable redirection before the inverse setters run. Every view handed to
			// those setters has already been mapped eye->original above.
			pair.current_eye = auxiliary_scene::no_view;
			active_eye.store(auxiliary_scene::no_view, std::memory_order_release);
			if (om_unordered_count != 0)
			{
				UINT unordered_start{};
				while (unordered_start < om_unordered_count &&
					om_unordered[unordered_start] == nullptr) ++unordered_start;
				if (unordered_start < render_target_count)
				{
					publish_failure(failure::binding_contract);
				}
				else
				{
					std::array<UINT, D3D11_PS_CS_UAV_REGISTER_COUNT> initial_counts{};
					initial_counts.fill(D3D11_KEEP_UNORDERED_ACCESS_VIEWS);
					context->OMSetRenderTargetsAndUnorderedAccessViews(
						render_target_count,
						render_target_count == 0 ? nullptr : render_targets.data(), depth,
						unordered_start, om_unordered_count - unordered_start,
						om_unordered.data() + unordered_start,
						initial_counts.data() + unordered_start);
					restore_rebind_calls.fetch_add(1, std::memory_order_relaxed);
				}
			}
			else if (render_target_count != 0 || depth != nullptr)
			{
				context->OMSetRenderTargets(render_target_count,
					render_target_count == 0 ? nullptr : render_targets.data(), depth);
				restore_rebind_calls.fetch_add(1, std::memory_order_relaxed);
			}
			release_views(render_targets);
			if (depth != nullptr) depth->Release();
			release_views(om_unordered);

			std::array<ID3D11UnorderedAccessView*,
				D3D11_PS_CS_UAV_REGISTER_COUNT> cs_unordered{};
			context->CSGetUnorderedAccessViews(0,
				static_cast<UINT>(cs_unordered.size()), cs_unordered.data());
			const auto cs_unordered_count = bound_prefix(cs_unordered);
			for (auto*& view : cs_unordered)
				restore_bound_view(resources.unordered_access, view);
			if (cs_unordered_count != 0)
			{
				std::array<UINT, D3D11_PS_CS_UAV_REGISTER_COUNT> initial_counts{};
				initial_counts.fill(D3D11_KEEP_UNORDERED_ACCESS_VIEWS);
				context->CSSetUnorderedAccessViews(0, cs_unordered_count,
					cs_unordered.data(), initial_counts.data());
				restore_rebind_calls.fetch_add(1, std::memory_order_relaxed);
			}
			release_views(cs_unordered);

			std::array<ID3D11ShaderResourceView*,
				D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> shader_views{};
			for (std::size_t stage{}; stage < shader_stage_count; ++stage)
			{
				get_shader_resources(stage, context,
					static_cast<UINT>(shader_views.size()), shader_views.data());
				const auto count = bound_prefix(shader_views);
				for (auto*& view : shader_views)
					restore_bound_view(resources.shader_resources, view);
				if (count != 0)
				{
					set_shader_resources(stage, context, count, shader_views.data());
					restore_rebind_calls.fetch_add(1, std::memory_order_relaxed);
				}
				release_views(shader_views);
			}

			const auto remaining = count_bound_target_views(context, true);
			restore_isolated_views_remaining.fetch_add(remaining,
				std::memory_order_relaxed);
			if (remaining != 0)
			{
				publish_failure(failure::binding_contract);
				restore_rebind_failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			restore_rebind_completions.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		void shader_resource_stub(const std::size_t stage,
			ID3D11DeviceContext* const context, const UINT start_slot,
			const UINT view_count, ID3D11ShaderResourceView* const* const views)
		{
			const auto original = reinterpret_cast<shader_resource_fn>(
				shader_hooks[stage].get_original());
			if (original == nullptr) return;
			if (!active_isolated_eye(context) || views == nullptr || view_count == 0)
			{
				original(context, start_slot, view_count, views);
				return;
			}
			if (start_slot > D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT ||
				view_count > D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - start_slot)
			{
				publish_failure(failure::binding_contract);
				original(context, start_slot, view_count, views);
				return;
			}
			// Every consumed slot is assigned below. Do not clear all 128 slots for
			// each native material bind, or repeat the same owner admission per slot.
			std::array<ID3D11ShaderResourceView*,
				D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> rewritten;
			for (UINT index{}; index < view_count; ++index)
			{
				rewritten[index] = views[index] == nullptr ? nullptr :
					rewrite_admitted_view(resources.shader_resources, views[index]);
				if (rewritten[index] != views[index])
					shader_resource_replacements[stage].fetch_add(1,
						std::memory_order_relaxed);
			}
			original(context, start_slot, view_count, rewritten.data());
		}

		void __stdcall ps_shader_resource_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11ShaderResourceView* const* views)
		{
			shader_resource_stub(0, context, start, count, views);
		}

		void __stdcall vs_shader_resource_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11ShaderResourceView* const* views)
		{
			shader_resource_stub(1, context, start, count, views);
		}

		void __stdcall gs_shader_resource_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11ShaderResourceView* const* views)
		{
			shader_resource_stub(2, context, start, count, views);
		}

		void __stdcall hs_shader_resource_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11ShaderResourceView* const* views)
		{
			shader_resource_stub(3, context, start, count, views);
		}

		void __stdcall ds_shader_resource_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11ShaderResourceView* const* views)
		{
			shader_resource_stub(4, context, start, count, views);
		}

		void __stdcall cs_shader_resource_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11ShaderResourceView* const* views)
		{
			shader_resource_stub(5, context, start, count, views);
		}

		void __stdcall cs_unordered_access_stub(ID3D11DeviceContext* const context,
			const UINT start_slot, const UINT view_count,
			ID3D11UnorderedAccessView* const* const views,
			const UINT* const initial_counts)
		{
			const auto original = reinterpret_cast<unordered_access_fn>(
				unordered_access_hook.get_original());
			if (original == nullptr) return;
			if (!active_isolated_eye(context) || views == nullptr || view_count == 0)
			{
				original(context, start_slot, view_count, views, initial_counts);
				return;
			}
			if (start_slot > D3D11_PS_CS_UAV_REGISTER_COUNT ||
				view_count > D3D11_PS_CS_UAV_REGISTER_COUNT - start_slot)
			{
				publish_failure(failure::binding_contract);
				original(context, start_slot, view_count, views, initial_counts);
				return;
			}
			std::array<ID3D11UnorderedAccessView*,
				D3D11_PS_CS_UAV_REGISTER_COUNT> rewritten{};
			for (UINT index{}; index < view_count; ++index)
				rewritten[index] = views[index] == nullptr ? nullptr :
					rewrite_unordered_access(context, views[index]);
			original(context, start_slot, view_count, rewritten.data(), initial_counts);
		}

		[[nodiscard]] std::array<void*, shader_stage_count> shader_stubs() noexcept
		{
			return {reinterpret_cast<void*>(ps_shader_resource_stub),
				reinterpret_cast<void*>(vs_shader_resource_stub),
				reinterpret_cast<void*>(gs_shader_resource_stub),
				reinterpret_cast<void*>(hs_shader_resource_stub),
				reinterpret_cast<void*>(ds_shader_resource_stub),
				reinterpret_cast<void*>(cs_shader_resource_stub)};
		}

		[[nodiscard]] bool extract_texture(ID3D11View* const view,
			Microsoft::WRL::ComPtr<ID3D11Texture2D>& output) noexcept
		{
			if (view == nullptr) return false;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			view->GetResource(resource.GetAddressOf());
			return resource && SUCCEEDED(resource.As(&output)) && output;
		}

		[[nodiscard]] bool source_role_matches(const source_binding& binding) noexcept
		{
			if (binding.owner_view == nullptr) return false;
			if (binding.target_role == role::color)
			{
				Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
				return SUCCEEDED(binding.owner_view->QueryInterface(
					IID_PPV_ARGS(view.GetAddressOf()))) && view;
			}
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> view;
			return SUCCEEDED(binding.owner_view->QueryInterface(
				IID_PPV_ARGS(view.GetAddressOf()))) && view;
		}

		[[nodiscard]] bool resources_match(
			const std::array<source_binding, isolated_target_count>& bindings,
			const std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,
				isolated_target_count>& originals,
			const std::array<D3D11_TEXTURE2D_DESC, isolated_target_count>& descriptions,
			ID3D11Device* const device, ID3D11DeviceContext* const context,
			const std::uint64_t generation) noexcept
		{
			if (!resources.ready || resources.device.Get() != device ||
				resources.context != reinterpret_cast<std::uintptr_t>(context) ||
				resources.generation != generation) return false;
			for (std::size_t index{}; index < bindings.size(); ++index)
			{
				const auto& existing = resources.targets[index];
				// H2 may publish another mip RTV for the same texture. A view change
				// must not reseed persistent image history while matrices stay committed.
				if (existing.target_id != bindings[index].target_id ||
					existing.target_role != bindings[index].target_role ||
					existing.original.Get() != originals[index].Get() ||
					std::memcmp(&existing.description, &descriptions[index],
						sizeof(D3D11_TEXTURE2D_DESC)) != 0)
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool append_owner_view_mapping(const std::uint32_t index) noexcept
		{
			auto& target = resources.targets[index];
			if (target.target_role == role::color)
			{
				Microsoft::WRL::ComPtr<ID3D11RenderTargetView> original;
				if (FAILED(target.owner_view.As(&original)) || !original) return false;
				return create_target_view_mapping(resources.render_targets,
					original.Get(), index) != original.Get();
			}
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> original;
			if (FAILED(target.owner_view.As(&original)) || !original) return false;
			return create_target_view_mapping(resources.depth_stencils,
				original.Get(), index) != original.Get();
		}

		[[nodiscard]] bool rebuild_resources(ID3D11Device* const device,
			ID3D11DeviceContext* const context, const std::uint64_t generation,
			const std::array<source_binding, isolated_target_count>& bindings,
			const std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,
				isolated_target_count>& originals,
			const std::array<D3D11_TEXTURE2D_DESC, isolated_target_count>& descriptions) noexcept
		{
			persistent_state replacement{};
			replacement.context = reinterpret_cast<std::uintptr_t>(context);
			replacement.generation = generation;
			replacement.owner_thread = GetCurrentThreadId();
			replacement.device = device;
			for (std::size_t index{}; index < bindings.size(); ++index)
			{
				auto& target = replacement.targets[index];
				target.target_id = bindings[index].target_id;
				target.target_role = bindings[index].target_role;
				target.owner_view = bindings[index].owner_view;
				target.original = originals[index];
				target.description = descriptions[index];
				for (unsigned index_eye=0;index_eye<2;++index_eye)
				{
					auto& eye=target.eyes[index_eye];
					const auto result = device->CreateTexture2D(&descriptions[index], nullptr,
						eye.GetAddressOf());
					if (FAILED(result) || !eye)
					{
						last_failure.store(failure::resource_creation,
							std::memory_order_release);
						return false;
					}
				}
			}

			for (std::size_t index{}; index < replacement.targets.size(); ++index)
			{
				for (unsigned eye=0;eye<2;++eye)
				{
					context->CopyResource(replacement.targets[index].eyes[eye].Get(), replacement.targets[index].original.Get());
					seed_copies.fetch_add(1, std::memory_order_relaxed);
				}
			}
			resources = std::move(replacement);
			clear_all_caches();
			for (std::uint32_t index{}; index < resources.targets.size(); ++index)
			{
				if (!append_owner_view_mapping(index))
				{
					resources = {};
					resources_ready.store(false, std::memory_order_release);
					last_failure.store(failure::view_creation,
						std::memory_order_release);
					return false;
				}
			}
			resources.ready = true;
			resources_ready.store(true, std::memory_order_release);
			resource_rebuilds.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		void retire_pair(const bool successful) noexcept
		{
			if (successful) pair_completions.fetch_add(1, std::memory_order_relaxed);
			else pair_failures.fetch_add(1, std::memory_order_relaxed);
			if (current_pair.pair_id == pair.pair_id &&
				current_pair.context == pair.context &&
				current_pair.generation == pair.generation)
			{
				current_pair = {};
			}
			pair = {};
			pair_failed.store(false, std::memory_order_release);
			active_pair.store(0, std::memory_order_release);
			active_owner_thread.store(0, std::memory_order_release);
			active_eye.store(auxiliary_scene::no_view, std::memory_order_release);
			exact_read_enabled.store(false, std::memory_order_release);
			exact_read_pair.store(0, std::memory_order_release);
			// Publish inactivity only after the owner has stopped touching pair state.
			// A changed-generation installer acquires this exact boundary before it is
			// allowed to release any cached COM object returned by a rewrite hook.
			pair_active.store(false, std::memory_order_release);
		}

		// pair_active=false is published only after the owner has returned from every
		// hook and dropped every borrowed replacement view. Any thread may reclaim the
		// generation after acquiring that exact quiescence boundary.
		[[nodiscard]] bool release_generation_state() noexcept
		{
			if (pair_active.load(std::memory_order_acquire)) return false;
			resources = {};
			resources_ready.store(false, std::memory_order_release);
			expected_context.store(0, std::memory_order_release);
			expected_generation.store(0, std::memory_order_release);
			clear_cleanup_quarantine();
			return true;
		}
	}

	bool install(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		if (context == nullptr || device_generation == 0)
		{
			hook_failures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (hooks_installed.load(std::memory_order_acquire) &&
			expected_context.load(std::memory_order_acquire) ==
				reinterpret_cast<std::uintptr_t>(context) &&
			expected_generation.load(std::memory_order_acquire) == device_generation)
		{
			return true;
		}
		const auto output_merger = engine_stereo_output_merger::get_status();
		const auto resource_ops = engine_stereo_resource_ops::get_status();
		if (!output_merger.hook_installed || !output_merger.extended_hooks_installed ||
			output_merger.expected_context != reinterpret_cast<std::uintptr_t>(context) ||
			output_merger.device_generation != device_generation ||
			!resource_ops.hooks_installed ||
			resource_ops.expected_context != reinterpret_cast<std::uintptr_t>(context) ||
			resource_ops.device_generation != device_generation)
		{
			hook_failures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		try
		{
			auto* const vtable = *reinterpret_cast<void***>(context);
			if (vtable == nullptr) throw std::runtime_error("D3D11 context vtable is null");
			std::array<void*, shader_stage_count> targets{};
			for (std::size_t stage{}; stage < targets.size(); ++stage)
			{
				targets[stage] = vtable[shader_vtable_slots[stage]];
				if (!utils::hook_validation::validate_executable_target(targets[stage]))
					throw std::runtime_error("shader resource setter is not executable");
				for (std::size_t previous{}; previous < stage; ++previous)
					if (targets[stage] == targets[previous])
						throw std::runtime_error("shader resource setters alias");
			}
			auto* const unordered_target = vtable[unordered_access_vtable_slot];
			if (!utils::hook_validation::validate_executable_target(unordered_target))
				throw std::runtime_error("unordered access setter is not executable");
			for (const auto target : targets)
				if (target == unordered_target)
					throw std::runtime_error("resource binding setters alias");

			const std::lock_guard lock(hook_mutex);
			const auto stubs = shader_stubs();
			for (std::size_t stage{}; stage < targets.size(); ++stage)
			{
				const auto existing = shader_hook_targets[stage].load(
					std::memory_order_acquire);
				if (existing != 0 && existing != reinterpret_cast<std::uintptr_t>(
					targets[stage]))
				{
					throw std::runtime_error("shader resource setter changed");
				}
				if (!shader_hooks[stage].is_enabled())
				{
					if (shader_hooks[stage].get_original() == nullptr)
						shader_hooks[stage].create(targets[stage], stubs[stage]);
					else shader_hooks[stage].enable();
				}
				shader_hook_targets[stage].store(reinterpret_cast<std::uintptr_t>(
					targets[stage]), std::memory_order_release);
			}
			const auto existing_uav = unordered_access_hook_target.load(
				std::memory_order_acquire);
			if (existing_uav != 0 && existing_uav != reinterpret_cast<std::uintptr_t>(
				unordered_target))
			{
				throw std::runtime_error("unordered access setter changed");
			}
			if (!unordered_access_hook.is_enabled())
			{
				if (unordered_access_hook.get_original() == nullptr)
					unordered_access_hook.create(unordered_target,
						reinterpret_cast<void*>(cs_unordered_access_stub));
				else unordered_access_hook.enable();
			}
			for (const auto& hook : shader_hooks)
				if (!hook.is_enabled()) throw std::runtime_error("shader hook enable failed");
			if (!unordered_access_hook.is_enabled())
				throw std::runtime_error("unordered access hook enable failed");

			unordered_access_hook_target.store(reinterpret_cast<std::uintptr_t>(
				unordered_target), std::memory_order_release);
			{
				const std::lock_guard state_lock(state_mutex);
				const auto prior_context = expected_context.load(
					std::memory_order_acquire);
				const auto prior_generation = expected_generation.load(
					std::memory_order_acquire);
				if ((prior_context != 0 || prior_generation != 0) &&
					(prior_context != reinterpret_cast<std::uintptr_t>(context) ||
						prior_generation != device_generation))
				{
					// The old owner publishes pair_active=false only after every H2 hook
					// and borrowed right-view use has returned. Until that acquire observes
					// the quiescent boundary, keep the entire old COM graph alive and reject
					// this generation's VR transaction.
					if (pair_active.load(std::memory_order_acquire))
					{
						defer_generation_release(prior_context, prior_generation,
							active_pair.load(std::memory_order_acquire),
							active_owner_thread.load(std::memory_order_acquire),
							failure::context);
						return false;
					}
					if (!release_generation_state()) return false;
				}
				expected_context.store(reinterpret_cast<std::uintptr_t>(context),
					std::memory_order_release);
				expected_generation.store(device_generation, std::memory_order_release);
			}
			engine_stereo_output_merger::set_view_rewriters(rewrite_render_target,
				rewrite_depth_stencil, rewrite_unordered_access);
			engine_stereo_resource_ops::set_rewriters(rewrite_resource,
				rewrite_resource_operation_srv, rewrite_resource_operation_uav);
			hooks_installed.store(true, std::memory_order_release);
			return true;
		}
		catch (...)
		{
			hook_failures.fetch_add(1, std::memory_order_relaxed);
			last_failure.store(failure::hooks, std::memory_order_release);
			return false;
		}
	}

	void invalidate_device(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context) ||
			expected_generation.load(std::memory_order_acquire) != device_generation)
		{
			return;
		}
		if (pair_active.load(std::memory_order_acquire))
		{
			defer_generation_release(reinterpret_cast<std::uintptr_t>(context),
				device_generation, active_pair.load(std::memory_order_acquire),
				active_owner_thread.load(std::memory_order_acquire), failure::context);
			return;
		}
		(void)release_generation_state();
	}

	bool begin_pair(const std::uint64_t pair_id, ID3D11Device* const device,
		ID3D11DeviceContext* const context, const std::uint64_t device_generation,
		const std::array<source_binding, isolated_target_count>& bindings) noexcept
	{
		const std::lock_guard lock(state_mutex);
		pair_attempts.fetch_add(1, std::memory_order_relaxed);
		if (cleanup_quarantined.load(std::memory_order_acquire))
		{
			quarantine_rejections.fetch_add(1, std::memory_order_relaxed);
			pair_failures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (!hooks_installed.load(std::memory_order_acquire) || pair.active ||
			pair_id == 0 || device == nullptr || context == nullptr ||
			expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context) ||
			expected_generation.load(std::memory_order_acquire) != device_generation)
		{
			last_failure.store(failure::lifecycle, std::memory_order_release);
			pair_failures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, isolated_target_count>
			originals{};
		std::array<D3D11_TEXTURE2D_DESC, isolated_target_count> descriptions{};
		for (std::size_t index{}; index < bindings.size(); ++index)
		{
			if (bindings[index].target_id != isolated_target_id ||
				bindings[index].target_role != role::color ||
				!source_role_matches(bindings[index]) ||
				!extract_texture(bindings[index].owner_view, originals[index]))
			{
				last_failure.store(failure::binding_contract, std::memory_order_release);
				pair_failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			originals[index]->GetDesc(&descriptions[index]);
			if (!valid_resource_description(bindings[index].target_role,
				descriptions[index]))
			{
				last_failure.store(failure::resource_contract, std::memory_order_release);
				pair_failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			Microsoft::WRL::ComPtr<ID3D11Device> source_device;
			originals[index]->GetDevice(source_device.GetAddressOf());
			if (source_device.Get() != device)
			{
				last_failure.store(failure::resource_contract, std::memory_order_release);
				pair_failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			for (std::size_t previous{}; previous < index; ++previous)
			{
				if (bindings[previous].target_id == bindings[index].target_id ||
					originals[previous].Get() == originals[index].Get())
				{
					last_failure.store(failure::binding_contract,
						std::memory_order_release);
					pair_failures.fetch_add(1, std::memory_order_relaxed);
					return false;
				}
			}
		}

		if (!resources_match(bindings, originals, descriptions, device, context,
			device_generation) && !rebuild_resources(device, context, device_generation,
			bindings, originals, descriptions))
		{
			pair_failures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		clear_foreign_caches();
		pair = {};
		pair.active = true;
		pair.pair_id = pair_id;
		pair.context = reinterpret_cast<std::uintptr_t>(context);
		pair.generation = device_generation;
		pair.owner_thread = GetCurrentThreadId();
		current_pair = {pair_id, pair.context, device_generation};
		pair_failed.store(false, std::memory_order_release);
		active_pair.store(pair_id, std::memory_order_release);
		active_owner_thread.store(pair.owner_thread, std::memory_order_release);
		active_eye.store(auxiliary_scene::no_view, std::memory_order_release);
		last_failure.store(failure::none, std::memory_order_release);
		pair_active.store(true, std::memory_order_release);
		return true;
	}

	namespace
	{
		template<class View>
		bool extend_auxiliary_views(target_view_cache<View>& cache) noexcept
		{
			for(std::size_t i=0;i<cache.count;++i)
			{
				auto& entry=cache.entries[i];
				if(entry.eyes[auxiliary_scene::view_index]) continue;
				if(FAILED(create_matching_view(resources.device.Get(),
					resources.targets[entry.target_index].eyes[auxiliary_scene::view_index].Get(),
					entry.original.Get(),entry.eyes[auxiliary_scene::view_index].GetAddressOf())))
					return false;
				cache.known.insert(reinterpret_cast<std::uintptr_t>(entry.eyes[auxiliary_scene::view_index].Get()),i);
				view_creations.fetch_add(1,std::memory_order_relaxed);
			}
			return true;
		}
		bool ensure_auxiliary_resources() noexcept
		{
			// Grow only the auxiliary mapping. Existing HMD images, views and
			// histories keep their identities when the first scope is raised.
			for(auto& target:resources.targets)
				if(!target.eyes[auxiliary_scene::view_index] &&
					FAILED(resources.device->CreateTexture2D(&target.description,nullptr,
						target.eyes[auxiliary_scene::view_index].GetAddressOf()))) return false;
			return extend_auxiliary_views(resources.render_targets) && extend_auxiliary_views(resources.depth_stencils) &&
				extend_auxiliary_views(resources.shader_resources) && extend_auxiliary_views(resources.unordered_access);
		}
	}

	bool begin_auxiliary(const std::uint64_t pair_id, const bool reset_history) noexcept
	{
		if (!owns_pair_on_current_thread() || pair.pair_id!=pair_id ||
			pair.current_eye!=auxiliary_scene::no_view || pair.completed_eye_mask!=1 ||
			pair.auxiliary_complete || pair.failed) return false;
		if(!ensure_auxiliary_resources()) {publish_failure(failure::resource_creation);return false;}
		auto* context=reinterpret_cast<ID3D11DeviceContext*>(pair.context);
		if (reset_history)
		{
			// Clear every mip; SSR can read a different level from the writer.
			for (auto& target : resources.targets)
			{
				if (target.description.MipLevels > target.auxiliary_clear_views.size())
				{publish_failure(failure::resource_contract);return false;}
				for (unsigned mip = 0; mip < target.description.MipLevels; ++mip)
				{
					auto& view = target.auxiliary_clear_views[mip];
					if (!view)
					{
						D3D11_RENDER_TARGET_VIEW_DESC desc{};
						desc.Format = target.description.Format;
						desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
						desc.Texture2D.MipSlice = mip;
						if (FAILED(resources.device->CreateRenderTargetView(
							target.eyes[auxiliary_scene::view_index].Get(), &desc, &view)))
						{publish_failure(failure::view_creation);return false;}
						view_creations.fetch_add(1, std::memory_order_relaxed);
					}
					const float black[4]{};
					context->ClearRenderTargetView(view.Get(), black);
				}
			}
		}
		pair.current_eye=auxiliary_scene::view_index;
		active_eye.store(auxiliary_scene::view_index,std::memory_order_release);
		return rebind_inherited_state(context) && !pair.failed;
	}

	bool end_auxiliary(const std::uint64_t pair_id) noexcept
	{
		if(!owns_pair_on_current_thread() || pair.pair_id!=pair_id || pair.current_eye!=auxiliary_scene::view_index)
			return false;
		apply_deferred_failure_on_owner();
		const bool restored=restore_inherited_state(reinterpret_cast<ID3D11DeviceContext*>(pair.context));
		pair.current_eye=auxiliary_scene::no_view;pair.auxiliary_complete=true;
		active_eye.store(auxiliary_scene::no_view,std::memory_order_release);
		if(!restored) {enter_cleanup_quarantine(failure::binding_contract);return false;}
		return !pair.failed;
	}

	bool begin_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		if (!owns_pair_on_current_thread() || pair.pair_id != pair_id || eye >= 2 ||
			pair.current_eye < auxiliary_scene::view_count || GetCurrentThreadId() != pair.owner_thread ||
			(pair.completed_eye_mask & (1u << eye)) != 0 ||
			eye != (pair.completed_eye_mask == 0 ? 0u : 1u))
		{
			publish_failure(failure::lifecycle);
			return false;
		}
		apply_deferred_failure_on_owner();
		if (pair.failed) return false;
		pair.current_eye = eye;
		active_eye.store(eye, std::memory_order_release);
		if (!rebind_inherited_state(
			reinterpret_cast<ID3D11DeviceContext*>(pair.context)))
		{
			return false;
		}
		return !pair.failed;
	}

	bool end_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		if (!owns_pair_on_current_thread() || pair.pair_id != pair_id || eye >= 2 ||
			pair.current_eye != eye || GetCurrentThreadId() != pair.owner_thread)
		{
			publish_failure(failure::lifecycle);
			return false;
		}
		apply_deferred_failure_on_owner();
		const auto restored = restore_inherited_state(
			reinterpret_cast<ID3D11DeviceContext*>(pair.context));
		pair.completed_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		pair.current_eye = auxiliary_scene::no_view;
		active_eye.store(auxiliary_scene::no_view, std::memory_order_release);
		if (!restored)
		{
			enter_cleanup_quarantine(failure::binding_contract);
			return false;
		}
		return !pair.failed;
	}

	bool end_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (!owns_pair_on_current_thread() || pair.pair_id != pair_id ||
			pair.current_eye < auxiliary_scene::view_count || pair.completed_eye_mask != 0x3 ||
			GetCurrentThreadId() != pair.owner_thread)
		{
			if (pair.active) enter_cleanup_quarantine(failure::lifecycle);
			else last_failure.store(failure::lifecycle, std::memory_order_release);
			return false;
		}
		const auto remaining = count_bound_target_views(
			reinterpret_cast<ID3D11DeviceContext*>(pair.context), true);
		restore_isolated_views_remaining.fetch_add(remaining,
			std::memory_order_relaxed);
		if (remaining != 0)
		{
			enter_cleanup_quarantine(failure::binding_contract);
			return false;
		}
		const auto successful = !pair.failed;
		retire_pair(successful);
		return successful;
	}

	void cancel_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (!pair.active || pair.pair_id != pair_id) return;
		if (!owns_pair_on_current_thread())
		{
			enter_cleanup_quarantine(failure::thread);
			return;
		}
		const auto retrying_cleanup = cleanup_quarantined.load(
			std::memory_order_acquire);
		if (retrying_cleanup)
			cleanup_retries.fetch_add(1, std::memory_order_relaxed);
		if (pair.current_eye < auxiliary_scene::view_count || pair.completed_eye_mask != 0)
		{
			if (!restore_inherited_state(
				reinterpret_cast<ID3D11DeviceContext*>(pair.context)))
			{
				enter_cleanup_quarantine(failure::binding_contract);
				return;
			}
		}
		if (retrying_cleanup)
			cleanup_recoveries.fetch_add(1, std::memory_order_relaxed);
		retire_pair(false);
	}

	bool set_exact_read_proof(const std::uint64_t pair_id,
		const bool enabled) noexcept
	{
		if (!owns_pair_on_current_thread() || pair.pair_id != pair_id ||
			GetCurrentThreadId() != pair.owner_thread)
		{
			return false;
		}
		pair.exact_read_enabled = enabled;
		exact_read_pair.store(enabled ? pair_id : 0, std::memory_order_release);
		exact_read_enabled.store(enabled, std::memory_order_release);
		return true;
	}

	void note_draw_indexed(ID3D11DeviceContext* const context,
		const std::uintptr_t caller, const std::uint32_t output_target_id) noexcept
	{
		if (caller != exact_read_draw_indexed_caller ||
			output_target_id != exact_read_output_target ||
			!owner_hook_token_matches() || !owns_pair_on_current_thread() ||
			!pair.exact_read_enabled ||
			pair.current_eye >= 2 ||
			reinterpret_cast<std::uintptr_t>(context) != pair.context)
		{
			return;
		}

		const auto eye = pair.current_eye;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		context->PSGetShaderResources(exact_read_ps_slot, 1, view.GetAddressOf());
		Microsoft::WRL::ComPtr<ID3D11Resource> resource;
		D3D11_SHADER_RESOURCE_VIEW_DESC description{};
		if (view)
		{
			view->GetResource(resource.GetAddressOf());
			view->GetDesc(&description);
		}

		auto binding = exact_read_binding::null_view;
		if (view)
		{
			binding = exact_read_binding::foreign;
			if (resource.Get() == resources.targets[0].original.Get())
				binding = exact_read_binding::original;
			else if (resource.Get() == resources.targets[0].eyes[0].Get())
				binding = exact_read_binding::left;
			else if (resource.Get() == resources.targets[0].eyes[1].Get())
				binding = exact_read_binding::right;
		}

		std::uint32_t most_detailed_mip{};
		std::uint32_t mip_levels{};
		std::uint32_t first_array_slice{};
		std::uint32_t array_size{1};
		switch (description.ViewDimension)
		{
		case D3D11_SRV_DIMENSION_TEXTURE2D:
			most_detailed_mip = description.Texture2D.MostDetailedMip;
			mip_levels = description.Texture2D.MipLevels;
			break;
		case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
			most_detailed_mip = description.Texture2DArray.MostDetailedMip;
			mip_levels = description.Texture2DArray.MipLevels;
			first_array_slice = description.Texture2DArray.FirstArraySlice;
			array_size = description.Texture2DArray.ArraySize;
			break;
		case D3D11_SRV_DIMENSION_TEXTURECUBE:
			most_detailed_mip = description.TextureCube.MostDetailedMip;
			mip_levels = description.TextureCube.MipLevels;
			array_size = 6;
			break;
		case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
			most_detailed_mip = description.TextureCubeArray.MostDetailedMip;
			mip_levels = description.TextureCubeArray.MipLevels;
			first_array_slice = description.TextureCubeArray.First2DArrayFace;
			array_size = description.TextureCubeArray.NumCubes * 6;
			break;
		default:
			array_size = 0;
			break;
		}

		exact_read_observations.fetch_add(1, std::memory_order_relaxed);
		exact_read_bindings[eye][static_cast<std::size_t>(binding)].fetch_add(1,
			std::memory_order_relaxed);
		if ((eye == 0 && binding == exact_read_binding::left) ||
			(eye == 1 && binding == exact_read_binding::right))
		{
			exact_read_expected.fetch_add(1, std::memory_order_relaxed);
		}
		else if (binding == exact_read_binding::original ||
			(eye == 0 && binding == exact_read_binding::right) ||
			(eye == 1 && binding == exact_read_binding::left))
		{
			exact_read_unexpected.fetch_add(1, std::memory_order_relaxed);
		}
		if (binding == exact_read_binding::original || binding == exact_read_binding::left ||
			binding == exact_read_binding::right)
		{
			exact_read_last_pair.store(pair.pair_id,
				std::memory_order_relaxed);
			exact_read_last_eye.store(eye, std::memory_order_relaxed);
			exact_read_last_caller.store(caller, std::memory_order_relaxed);
			exact_read_last_output_target.store(output_target_id,
				std::memory_order_relaxed);
			exact_read_last_view.store(reinterpret_cast<std::uintptr_t>(view.Get()),
				std::memory_order_relaxed);
			exact_read_last_resource.store(
				reinterpret_cast<std::uintptr_t>(resource.Get()),
				std::memory_order_relaxed);
			exact_read_last_binding.store(static_cast<std::uint32_t>(binding),
				std::memory_order_relaxed);
			exact_read_last_view_dimension.store(
				static_cast<std::uint32_t>(description.ViewDimension),
				std::memory_order_relaxed);
			exact_read_last_format.store(static_cast<std::uint32_t>(description.Format),
				std::memory_order_relaxed);
			exact_read_last_most_detailed_mip.store(most_detailed_mip,
				std::memory_order_relaxed);
			exact_read_last_mip_levels.store(mip_levels, std::memory_order_relaxed);
			exact_read_last_first_array_slice.store(first_array_slice,
				std::memory_order_relaxed);
			exact_read_last_array_size.store(array_size, std::memory_order_release);
		}
	}

	status get_status() noexcept
	{
		status output{};
		output.hooks_installed = hooks_installed.load(std::memory_order_acquire);
		output.resources_ready = resources_ready.load(std::memory_order_acquire);
		output.pair_active = pair_active.load(std::memory_order_acquire);
		output.pair_failed = pair_failed.load(std::memory_order_acquire);
		output.cleanup_quarantined = cleanup_quarantined.load(
			std::memory_order_acquire);
		output.invalidation_pending = invalidation_pending.load(
			std::memory_order_acquire);
		output.expected_context = expected_context.load(std::memory_order_acquire);
		output.device_generation = expected_generation.load(std::memory_order_acquire);
		output.active_pair = active_pair.load(std::memory_order_acquire);
		output.owner_thread = active_owner_thread.load(std::memory_order_acquire);
		output.active_eye = active_eye.load(std::memory_order_acquire);
		output.quarantine_context = quarantine_context.load(
			std::memory_order_acquire);
		output.quarantine_generation = quarantine_generation.load(
			std::memory_order_acquire);
		output.quarantine_pair = quarantine_pair.load(std::memory_order_acquire);
		output.quarantine_owner_thread = quarantine_owner_thread.load(
			std::memory_order_acquire);
		output.last_failure = last_failure.load(std::memory_order_acquire);
		output.hook_failures = hook_failures.load(std::memory_order_acquire);
		output.pair_attempts = pair_attempts.load(std::memory_order_acquire);
		output.pair_completions = pair_completions.load(std::memory_order_acquire);
		output.pair_failures = pair_failures.load(std::memory_order_acquire);
		output.quarantine_events = quarantine_events.load(std::memory_order_acquire);
		output.quarantine_rejections = quarantine_rejections.load(
			std::memory_order_acquire);
		output.cleanup_retries = cleanup_retries.load(std::memory_order_acquire);
		output.cleanup_recoveries = cleanup_recoveries.load(
			std::memory_order_acquire);
		output.deferred_invalidations = deferred_invalidations.load(
			std::memory_order_acquire);
		output.resource_rebuilds = resource_rebuilds.load(std::memory_order_acquire);
		output.seed_copies = seed_copies.load(std::memory_order_acquire);
		output.view_creations = view_creations.load(std::memory_order_acquire);
		output.view_capacity_failures = view_capacity_failures.load(
			std::memory_order_acquire);
		output.boundary_rebind_attempts = boundary_rebind_attempts.load(
			std::memory_order_acquire);
		output.boundary_rebind_completions = boundary_rebind_completions.load(
			std::memory_order_acquire);
		output.boundary_rebind_failures = boundary_rebind_failures.load(
			std::memory_order_acquire);
		output.boundary_rebind_calls = boundary_rebind_calls.load(
			std::memory_order_acquire);
		output.boundary_original_views_remaining =
			boundary_original_views_remaining.load(std::memory_order_acquire);
		output.restore_rebind_attempts = restore_rebind_attempts.load(
			std::memory_order_acquire);
		output.restore_rebind_completions = restore_rebind_completions.load(
			std::memory_order_acquire);
		output.restore_rebind_failures = restore_rebind_failures.load(
			std::memory_order_acquire);
		output.restore_rebind_calls = restore_rebind_calls.load(
			std::memory_order_acquire);
		output.restore_isolated_views_remaining = restore_isolated_views_remaining.load(
			std::memory_order_acquire);
		output.resource_replacements = resource_replacements.load(
			std::memory_order_acquire);
		output.render_target_replacements = render_target_replacements.load(
			std::memory_order_acquire);
		output.depth_stencil_replacements = depth_stencil_replacements.load(
			std::memory_order_acquire);
		output.unordered_access_replacements = unordered_access_replacements.load(
			std::memory_order_acquire);
		for (std::size_t stage{}; stage < shader_stage_count; ++stage)
		{
			output.shader_resource_replacements[stage] =
				shader_resource_replacements[stage].load(std::memory_order_acquire);
			output.shader_hook_targets[stage] = shader_hook_targets[stage].load(
				std::memory_order_acquire);
		}
		output.unordered_access_hook_target = unordered_access_hook_target.load(
			std::memory_order_acquire);
		output.exact_read_enabled = exact_read_enabled.load(std::memory_order_acquire);
		output.exact_read_pair = exact_read_pair.load(std::memory_order_acquire);
		output.exact_read_observations = exact_read_observations.load(
			std::memory_order_acquire);
		output.exact_read_expected = exact_read_expected.load(std::memory_order_acquire);
		output.exact_read_unexpected = exact_read_unexpected.load(
			std::memory_order_acquire);
		for (std::size_t eye{}; eye < output.exact_read_bindings.size(); ++eye)
		{
			for (std::size_t binding{};
				binding < output.exact_read_bindings[eye].size(); ++binding)
			{
				output.exact_read_bindings[eye][binding] =
					exact_read_bindings[eye][binding].load(std::memory_order_acquire);
			}
		}
		output.exact_read_last_pair = exact_read_last_pair.load(
			std::memory_order_acquire);
		output.exact_read_last_eye = exact_read_last_eye.load(std::memory_order_acquire);
		output.exact_read_last_caller = exact_read_last_caller.load(
			std::memory_order_acquire);
		output.exact_read_last_output_target = exact_read_last_output_target.load(
			std::memory_order_acquire);
		output.exact_read_last_view = exact_read_last_view.load(
			std::memory_order_acquire);
		output.exact_read_last_resource = exact_read_last_resource.load(
			std::memory_order_acquire);
		output.exact_read_last_binding = static_cast<exact_read_binding>(
			exact_read_last_binding.load(std::memory_order_acquire));
		output.exact_read_last_view_dimension = exact_read_last_view_dimension.load(
			std::memory_order_acquire);
		output.exact_read_last_format = exact_read_last_format.load(
			std::memory_order_acquire);
		output.exact_read_last_most_detailed_mip =
			exact_read_last_most_detailed_mip.load(std::memory_order_acquire);
		output.exact_read_last_mip_levels = exact_read_last_mip_levels.load(
			std::memory_order_acquire);
		output.exact_read_last_first_array_slice =
			exact_read_last_first_array_slice.load(std::memory_order_acquire);
		output.exact_read_last_array_size = exact_read_last_array_size.load(
			std::memory_order_acquire);

		const std::lock_guard lock(state_mutex);
		if (resources.ready)
		{
			for (std::size_t index{}; index < resources.targets.size(); ++index)
			{
				const auto& source = resources.targets[index];
				auto& target = output.targets[index];
				target.target_id = source.target_id;
				target.target_role = source.target_role;
				target.owner_view = reinterpret_cast<std::uintptr_t>(
					source.owner_view.Get());
				target.original_resource = reinterpret_cast<std::uintptr_t>(
					source.original.Get());
				target.left_resource = reinterpret_cast<std::uintptr_t>(source.eyes[0].Get());
				target.right_resource = reinterpret_cast<std::uintptr_t>(source.eyes[1].Get());
				target.width = source.description.Width;
				target.height = source.description.Height;
				target.mip_levels = source.description.MipLevels;
				target.format = static_cast<std::uint32_t>(source.description.Format);
				target.bind_flags = source.description.BindFlags;
			}
		}
		return output;
	}

	const char* to_string(const role value) noexcept
	{
		switch (value)
		{
		case role::color: return "color";
		case role::depth: return "depth";
		default: return "unknown";
		}
	}

	const char* to_string(const failure value) noexcept
	{
		switch (value)
		{
		case failure::none: return "none";
		case failure::hooks: return "hooks";
		case failure::lifecycle: return "lifecycle";
		case failure::context: return "context";
		case failure::thread: return "thread";
		case failure::binding_contract: return "binding_contract";
		case failure::resource_contract: return "resource_contract";
		case failure::resource_creation: return "resource_creation";
		case failure::view_creation: return "view_creation";
		case failure::view_capacity: return "view_capacity";
		default: return "unknown";
		}
	}

	const char* to_string(const exact_read_binding value) noexcept
	{
		switch (value)
		{
		case exact_read_binding::null_view: return "null";
		case exact_read_binding::original: return "natural91";
		case exact_read_binding::left: return "left91";
		case exact_read_binding::right: return "right91";
		case exact_read_binding::foreign: return "foreign";
		default: return "unknown";
		}
	}
}
