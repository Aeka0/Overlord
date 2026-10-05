#pragma once

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_resource_ops
{
	enum class api : std::uint8_t
	{
		copy_subresource_region,
		copy_resource,
		update_subresource,
		copy_structure_count,
		clear_uav_uint,
		clear_uav_float,
		generate_mips,
		resolve_subresource,
		count,
	};

	inline constexpr std::size_t api_count = static_cast<std::size_t>(api::count);

	struct event
	{
		api operation{api::copy_subresource_region};
		ID3D11DeviceContext* context{};
		ID3D11Resource* destination{};
		ID3D11Resource* source{};
		ID3D11View* view{};
		std::uintptr_t caller{};
		std::uint32_t destination_subresource{};
		std::uint32_t source_subresource{};
		// Borrowed only for the synchronous observer callback made before the
		// hooked UpdateSubresource call returns to H2. Other operations leave
		// these fields null/zero.
		const void* source_data{};
		const D3D11_BOX* update_box{};
		std::uint32_t source_row_pitch{};
		std::uint32_t source_depth_pitch{};
	};

	using observer_fn = void(*)(const event&) noexcept;
	enum class observer_channel : std::uint8_t
	{
		constant_buffer_history,
		gpu_census,
		ssr_consumer,
		owner_diagnostic,
		effect_timeline,
		count,
	};
	inline constexpr std::size_t observer_channel_count =
		static_cast<std::size_t>(observer_channel::count);
	enum class access : std::uint8_t
	{
		read,
		write,
	};
	using resource_rewriter_fn = ID3D11Resource*(*)(ID3D11DeviceContext*,
		ID3D11Resource*, access) noexcept;
	using shader_resource_rewriter_fn = ID3D11ShaderResourceView*(*)(
		ID3D11DeviceContext*, ID3D11ShaderResourceView*, access) noexcept;
	using unordered_access_rewriter_fn = ID3D11UnorderedAccessView*(*)(
		ID3D11DeviceContext*, ID3D11UnorderedAccessView*, access) noexcept;

	struct status
	{
		bool hooks_installed{};
		std::uintptr_t expected_context{};
		std::uint64_t device_generation{};
		std::uint64_t hook_failures{};
		std::uint64_t observer_callbacks{};
		std::uint64_t foreign_context_calls{};
		std::array<std::uintptr_t, api_count> hook_targets{};
		std::array<std::uint64_t, api_count> calls{};
	};

	// Process-wide D3D11 resource-operation observers. Every stub forwards the
	// natural call exactly once before publishing borrowed pointer identities.
	[[nodiscard]] bool install(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	void invalidate_device(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	// Central observer channels keep independent diagnostics on the one existing
	// detour set. A channel owns exactly one callback and nullptr detaches it.
	void set_observer(observer_channel channel, observer_fn observer) noexcept;
	void set_rewriters(resource_rewriter_fn resource,
		shader_resource_rewriter_fn shader_resource,
		unordered_access_rewriter_fn unordered_access) noexcept;
	// Issue a diagnostic-only buffer copy through the already validated native
	// trampoline. The operation is deliberately not published to observers and
	// is not passed through production eye-resource rewriting, so a one-shot GPU
	// readback cannot alter the census stream it is measuring.
	[[nodiscard]] bool copy_buffer_region_unobserved(ID3D11DeviceContext* context,
		ID3D11Buffer* destination, std::uint32_t destination_offset,
		ID3D11Buffer* source, std::uint32_t byte_width) noexcept;
	[[nodiscard]] bool copy_buffer_region_unobserved(ID3D11DeviceContext* context,
		ID3D11Buffer* destination, std::uint32_t destination_offset,
		ID3D11Buffer* source, std::uint32_t source_offset,
		std::uint32_t byte_width) noexcept;
	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] const char* to_string(api value) noexcept;
}
