#include <std_include.hpp>

#include "engine_stereo_resource_ops.hpp"

#include "utils/hook.hpp"
#include "utils/hook_validation.hpp"

#include <atomic>
#include <mutex>

namespace vr::engine_stereo_resource_ops
{
	namespace
	{
		constexpr std::array<std::size_t, api_count> vtable_slots{
			46, 47, 48, 49, 51, 52, 54, 57,
		};

		using copy_subresource_region_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT,
			const D3D11_BOX*);
		using copy_resource_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11Resource*, ID3D11Resource*);
		using update_subresource_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);
		using copy_structure_count_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11Buffer*, UINT, ID3D11UnorderedAccessView*);
		using clear_uav_uint_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11UnorderedAccessView*, const UINT[4]);
		using clear_uav_float_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11UnorderedAccessView*, const FLOAT[4]);
		using generate_mips_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11ShaderResourceView*);
		using resolve_subresource_fn = void(__stdcall*)(ID3D11DeviceContext*,
			ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT);

		utils::hook::detour copy_subresource_region_hook;
		utils::hook::detour copy_resource_hook;
		utils::hook::detour update_subresource_hook;
		utils::hook::detour copy_structure_count_hook;
		utils::hook::detour clear_uav_uint_hook;
		utils::hook::detour clear_uav_float_hook;
		utils::hook::detour generate_mips_hook;
		utils::hook::detour resolve_subresource_hook;
		std::mutex hook_mutex;
		std::atomic_bool hooks_installed{};
		std::atomic_uintptr_t expected_context{};
		std::atomic_uint64_t expected_generation{};
		std::atomic_uint64_t hook_failures{};
		std::atomic_uint64_t observer_callbacks{};
		std::atomic_uint64_t foreign_context_calls{};
		std::array<std::atomic_uintptr_t, api_count> hook_targets{};
		std::array<std::atomic_uint64_t, api_count> calls{};
		std::array<std::atomic<observer_fn>, observer_channel_count> observers{};
		std::atomic<resource_rewriter_fn> resource_rewriter{};
		std::atomic<shader_resource_rewriter_fn> shader_resource_rewriter{};
		std::atomic<unordered_access_rewriter_fn> unordered_access_rewriter{};

		constexpr std::size_t index(const api value) noexcept
		{
			return static_cast<std::size_t>(value);
		}

		void publish(const event& value) noexcept
		{
			const auto expected = expected_context.load(std::memory_order_acquire);
			if (expected == 0) return;
			if (reinterpret_cast<std::uintptr_t>(value.context) != expected)
			{
				foreign_context_calls.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			calls[index(value.operation)].fetch_add(1, std::memory_order_relaxed);
			for (auto& registered : observers)
			{
				if (const auto callback = registered.load(std::memory_order_acquire))
				{
					observer_callbacks.fetch_add(1, std::memory_order_relaxed);
					callback(value);
				}
			}
		}

		void __stdcall copy_subresource_region_stub(ID3D11DeviceContext* const context,
			ID3D11Resource* const destination, const UINT destination_subresource,
			const UINT destination_x, const UINT destination_y, const UINT destination_z,
			ID3D11Resource* const source, const UINT source_subresource,
			const D3D11_BOX* const source_box)
		{
			const auto original = reinterpret_cast<copy_subresource_region_fn>(
				copy_subresource_region_hook.get_original());
			if (original == nullptr) return;
			auto* effective_destination = destination;
			auto* effective_source = source;
			if (const auto rewrite = resource_rewriter.load(std::memory_order_acquire))
			{
				if (destination != nullptr)
					effective_destination = rewrite(context, destination, access::write);
				if (source != nullptr)
					effective_source = rewrite(context, source, access::read);
			}
			original(context, effective_destination, destination_subresource, destination_x,
				destination_y, destination_z, effective_source, source_subresource, source_box);
			publish({api::copy_subresource_region, context, effective_destination,
				effective_source, nullptr,
				reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
				destination_subresource, source_subresource});
		}

		void __stdcall copy_resource_stub(ID3D11DeviceContext* const context,
			ID3D11Resource* const destination, ID3D11Resource* const source)
		{
			const auto original = reinterpret_cast<copy_resource_fn>(
				copy_resource_hook.get_original());
			if (original == nullptr) return;
			auto* effective_destination = destination;
			auto* effective_source = source;
			if (const auto rewrite = resource_rewriter.load(std::memory_order_acquire))
			{
				if (destination != nullptr)
					effective_destination = rewrite(context, destination, access::write);
				if (source != nullptr)
					effective_source = rewrite(context, source, access::read);
			}
			original(context, effective_destination, effective_source);
			publish({api::copy_resource, context, effective_destination,
				effective_source, nullptr,
				reinterpret_cast<std::uintptr_t>(_ReturnAddress())});
		}

		void __stdcall update_subresource_stub(ID3D11DeviceContext* const context,
			ID3D11Resource* const destination, const UINT destination_subresource,
			const D3D11_BOX* const destination_box, const void* const source_data,
			const UINT source_row_pitch, const UINT source_depth_pitch)
		{
			const auto original = reinterpret_cast<update_subresource_fn>(
				update_subresource_hook.get_original());
			if (original == nullptr) return;
			auto* effective_destination = destination;
			if (const auto rewrite = resource_rewriter.load(std::memory_order_acquire);
				rewrite != nullptr && destination != nullptr)
			{
				effective_destination = rewrite(context, destination, access::write);
			}
			original(context, effective_destination, destination_subresource, destination_box,
				source_data, source_row_pitch, source_depth_pitch);
			publish({api::update_subresource, context, effective_destination,
				nullptr, nullptr,
				reinterpret_cast<std::uintptr_t>(_ReturnAddress()), destination_subresource,
				0, source_data, destination_box, source_row_pitch,
				source_depth_pitch});
		}

		void __stdcall copy_structure_count_stub(ID3D11DeviceContext* const context,
			ID3D11Buffer* const destination, const UINT destination_offset,
			ID3D11UnorderedAccessView* const source)
		{
			const auto original = reinterpret_cast<copy_structure_count_fn>(
				copy_structure_count_hook.get_original());
			if (original == nullptr) return;
			auto* effective_source = source;
			if (const auto rewrite = unordered_access_rewriter.load(
				std::memory_order_acquire); rewrite != nullptr && source != nullptr)
			{
				effective_source = rewrite(context, source, access::read);
			}
			original(context, destination, destination_offset, effective_source);
			publish({api::copy_structure_count, context, destination, nullptr,
				effective_source,
				reinterpret_cast<std::uintptr_t>(_ReturnAddress()), destination_offset});
		}

		void __stdcall clear_uav_uint_stub(ID3D11DeviceContext* const context,
			ID3D11UnorderedAccessView* const view, const UINT values[4])
		{
			const auto original = reinterpret_cast<clear_uav_uint_fn>(
				clear_uav_uint_hook.get_original());
			if (original == nullptr) return;
			auto* effective_view = view;
			if (const auto rewrite = unordered_access_rewriter.load(
				std::memory_order_acquire); rewrite != nullptr && view != nullptr)
			{
				effective_view = rewrite(context, view, access::write);
			}
			original(context, effective_view, values);
			publish({api::clear_uav_uint, context, nullptr, nullptr, effective_view,
				reinterpret_cast<std::uintptr_t>(_ReturnAddress())});
		}

		void __stdcall clear_uav_float_stub(ID3D11DeviceContext* const context,
			ID3D11UnorderedAccessView* const view, const FLOAT values[4])
		{
			const auto original = reinterpret_cast<clear_uav_float_fn>(
				clear_uav_float_hook.get_original());
			if (original == nullptr) return;
			auto* effective_view = view;
			if (const auto rewrite = unordered_access_rewriter.load(
				std::memory_order_acquire); rewrite != nullptr && view != nullptr)
			{
				effective_view = rewrite(context, view, access::write);
			}
			original(context, effective_view, values);
			publish({api::clear_uav_float, context, nullptr, nullptr, effective_view,
				reinterpret_cast<std::uintptr_t>(_ReturnAddress())});
		}

		void __stdcall generate_mips_stub(ID3D11DeviceContext* const context,
			ID3D11ShaderResourceView* const view)
		{
			const auto original = reinterpret_cast<generate_mips_fn>(
				generate_mips_hook.get_original());
			if (original == nullptr) return;
			auto* effective_view = view;
			if (const auto rewrite = shader_resource_rewriter.load(
				std::memory_order_acquire); rewrite != nullptr && view != nullptr)
			{
				effective_view = rewrite(context, view, access::write);
			}
			original(context, effective_view);
			publish({api::generate_mips, context, nullptr, nullptr, effective_view,
				reinterpret_cast<std::uintptr_t>(_ReturnAddress())});
		}

		void __stdcall resolve_subresource_stub(ID3D11DeviceContext* const context,
			ID3D11Resource* const destination, const UINT destination_subresource,
			ID3D11Resource* const source, const UINT source_subresource,
			const DXGI_FORMAT format)
		{
			const auto original = reinterpret_cast<resolve_subresource_fn>(
				resolve_subresource_hook.get_original());
			if (original == nullptr) return;
			auto* effective_destination = destination;
			auto* effective_source = source;
			if (const auto rewrite = resource_rewriter.load(std::memory_order_acquire))
			{
				if (destination != nullptr)
					effective_destination = rewrite(context, destination, access::write);
				if (source != nullptr)
					effective_source = rewrite(context, source, access::read);
			}
			original(context, effective_destination, destination_subresource, effective_source,
				source_subresource, format);
			publish({api::resolve_subresource, context, effective_destination,
				effective_source, nullptr,
				reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
				destination_subresource, source_subresource});
		}

		std::array<utils::hook::detour*, api_count> hooks() noexcept
		{
			return {&copy_subresource_region_hook, &copy_resource_hook,
				&update_subresource_hook, &copy_structure_count_hook,
				&clear_uav_uint_hook, &clear_uav_float_hook,
				&generate_mips_hook, &resolve_subresource_hook};
		}

		std::array<void*, api_count> stubs() noexcept
		{
			return {reinterpret_cast<void*>(copy_subresource_region_stub),
				reinterpret_cast<void*>(copy_resource_stub),
				reinterpret_cast<void*>(update_subresource_stub),
				reinterpret_cast<void*>(copy_structure_count_stub),
				reinterpret_cast<void*>(clear_uav_uint_stub),
				reinterpret_cast<void*>(clear_uav_float_stub),
				reinterpret_cast<void*>(generate_mips_stub),
				reinterpret_cast<void*>(resolve_subresource_stub)};
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
		try
		{
			auto* const vtable = *reinterpret_cast<void***>(context);
			if (vtable == nullptr) throw std::runtime_error("D3D11 context vtable is null");
			std::array<void*, api_count> targets{};
			for (std::size_t operation{}; operation < targets.size(); ++operation)
			{
				targets[operation] = vtable[vtable_slots[operation]];
				if (!utils::hook_validation::validate_executable_target(targets[operation]))
				{
					throw std::runtime_error("D3D11 resource operation target is not executable");
				}
				for (std::size_t previous{}; previous < operation; ++previous)
				{
					if (targets[operation] == targets[previous])
						throw std::runtime_error("D3D11 resource operation targets alias");
				}
			}

			const std::lock_guard lock(hook_mutex);
			const auto hook_set = hooks();
			const auto stub_set = stubs();
			for (std::size_t operation{}; operation < targets.size(); ++operation)
			{
				const auto existing = hook_targets[operation].load(std::memory_order_acquire);
				if (existing != 0 && existing != reinterpret_cast<std::uintptr_t>(
					targets[operation]))
				{
					throw std::runtime_error("D3D11 resource operation target changed");
				}
				if (!hook_set[operation]->is_enabled())
				{
					if (hook_set[operation]->get_original() == nullptr)
						hook_set[operation]->create(targets[operation], stub_set[operation]);
					else hook_set[operation]->enable();
				}
				hook_targets[operation].store(reinterpret_cast<std::uintptr_t>(
					targets[operation]), std::memory_order_release);
			}
			for (const auto* const hook : hook_set)
				if (!hook->is_enabled()) throw std::runtime_error("resource hook enable failed");
			expected_context.store(reinterpret_cast<std::uintptr_t>(context),
				std::memory_order_release);
			expected_generation.store(device_generation, std::memory_order_release);
			hooks_installed.store(true, std::memory_order_release);
			return true;
		}
		catch (...)
		{
			hook_failures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
	}

	void invalidate_device(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		const std::lock_guard lock(hook_mutex);
		if (expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context) ||
			expected_generation.load(std::memory_order_acquire) != device_generation)
		{
			return;
		}
		expected_context.store(0, std::memory_order_release);
		expected_generation.store(0, std::memory_order_release);
	}

	void set_observer(const observer_channel channel, const observer_fn value) noexcept
	{
		const auto slot = static_cast<std::size_t>(channel);
		if (slot >= observers.size()) return;
		observers[slot].store(value, std::memory_order_release);
	}

	void set_rewriters(const resource_rewriter_fn resource,
		const shader_resource_rewriter_fn shader_resource,
		const unordered_access_rewriter_fn unordered_access) noexcept
	{
		resource_rewriter.store(resource, std::memory_order_release);
		shader_resource_rewriter.store(shader_resource, std::memory_order_release);
		unordered_access_rewriter.store(unordered_access, std::memory_order_release);
	}

	bool copy_buffer_region_unobserved(ID3D11DeviceContext* const context,
		ID3D11Buffer* const destination, const std::uint32_t destination_offset,
		ID3D11Buffer* const source, const std::uint32_t byte_width) noexcept
	{
		return copy_buffer_region_unobserved(context, destination,
			destination_offset, source, 0, byte_width);
	}

	bool copy_buffer_region_unobserved(ID3D11DeviceContext* const context,
		ID3D11Buffer* const destination, const std::uint32_t destination_offset,
		ID3D11Buffer* const source, const std::uint32_t source_offset,
		const std::uint32_t byte_width) noexcept
	{
		if (context == nullptr || destination == nullptr || source == nullptr ||
			byte_width == 0 || expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context))
		{
			return false;
		}
		const auto original = reinterpret_cast<copy_subresource_region_fn>(
			copy_subresource_region_hook.get_original());
		if (original == nullptr || !copy_subresource_region_hook.is_enabled())
			return false;

		D3D11_BUFFER_DESC destination_description{};
		D3D11_BUFFER_DESC source_description{};
		destination->GetDesc(&destination_description);
		source->GetDesc(&source_description);
		if (destination_offset > destination_description.ByteWidth ||
			byte_width > destination_description.ByteWidth - destination_offset ||
			source_offset > source_description.ByteWidth ||
			byte_width > source_description.ByteWidth - source_offset)
		{
			return false;
		}
		const D3D11_BOX source_box{source_offset, 0, 0,
			source_offset + byte_width, 1, 1};
		original(context, destination, 0, destination_offset, 0, 0, source, 0,
			&source_box);
		return true;
	}

	status get_status() noexcept
	{
		status output{};
		output.hooks_installed = hooks_installed.load(std::memory_order_acquire);
		output.expected_context = expected_context.load(std::memory_order_acquire);
		output.device_generation = expected_generation.load(std::memory_order_acquire);
		output.hook_failures = hook_failures.load(std::memory_order_acquire);
		output.observer_callbacks = observer_callbacks.load(std::memory_order_acquire);
		output.foreign_context_calls = foreign_context_calls.load(std::memory_order_acquire);
		for (std::size_t operation{}; operation < api_count; ++operation)
		{
			output.hook_targets[operation] = hook_targets[operation].load(
				std::memory_order_acquire);
			output.calls[operation] = calls[operation].load(std::memory_order_acquire);
		}
		return output;
	}

	const char* to_string(const api value) noexcept
	{
		switch (value)
		{
		case api::copy_subresource_region: return "CopySubresourceRegion";
		case api::copy_resource: return "CopyResource";
		case api::update_subresource: return "UpdateSubresource";
		case api::copy_structure_count: return "CopyStructureCount";
		case api::clear_uav_uint: return "ClearUnorderedAccessViewUint";
		case api::clear_uav_float: return "ClearUnorderedAccessViewFloat";
		case api::generate_mips: return "GenerateMips";
		case api::resolve_subresource: return "ResolveSubresource";
		default: return "unknown";
		}
	}
}
