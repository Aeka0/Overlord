#pragma once

#include <d3d11.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace d3d11::texture_diagnostics
{
	inline constexpr std::uint32_t stream_type = 0x4F565458; // OVTX
	inline constexpr std::size_t capacity = 256, active_capacity = 32;
	enum class phase : std::uint64_t
	{
		texture_begin = 1, texture_end, execute_begin, execute_end, resources_reset, device_observed,
		post_aa_begin, post_aa_end, scope_abandoned,
	};
	struct event
	{
		std::uint64_t sequence{}, call{}, tick{}, thread{}, operation{}, device{}, generation{}, caller{};
		std::uint64_t texture{}, context{}, pair{}, eye{}, result{}, descriptor_valid{}, initial_data{};
		std::uint64_t width{}, height{}, mips{}, array_size{}, format{}, samples{}, quality{}, usage{}, bind{}, cpu{}, misc{};
		std::uint64_t source{}, command{}, detail{};
	};
	static_assert(sizeof(event) == 29 * sizeof(std::uint64_t));
	struct snapshot
	{
		std::uint64_t version{1}, event_bytes{sizeof(event)}, capacity{texture_diagnostics::capacity}, recent_count{}, active_count{};
		std::uint64_t newest{}, dropped{}, texture_calls{}, texture_failures{}, active_overflow{};
		std::uint64_t hooks{}, hook_failures{}, identity_overflow{}, first_failure_present{}, sampled_tick{}, skipped{};
		std::array<event, active_capacity> active{};
		std::array<event, texture_diagnostics::capacity> recent{};
		event first_failure{};
	};
	static_assert(sizeof(snapshot) == 128 + (active_capacity + capacity + 1) * sizeof(event));

	// Observe the SDK's CreateTexture2D boundary. Descriptors/results are copied;
	// resource data and COM ownership are untouched. Failure leaves rendering on
	// its original path and is reported as unavailable diagnostic coverage.
	[[nodiscard]] bool install(ID3D11Device* device, std::uint64_t generation) noexcept;
	class cpu_scope final
	{
	public:
		cpu_scope(phase begin, phase end, std::uint64_t generation, std::uint64_t pair, std::uint64_t eye,
			ID3D11DeviceContext* context, const void* source, const void* destination, const void* command = nullptr,
			std::uint64_t detail = 0) noexcept;
		~cpu_scope() noexcept;
		cpu_scope(const cpu_scope&) = delete;
		cpu_scope& operator=(const cpu_scope&) = delete;
		void complete(std::uint64_t result = 0) noexcept;
	private:
		event value_{};
		std::size_t active_slot_{active_capacity};
		phase end_{};
	};
	void record_phase(phase operation, std::uint64_t generation, std::uint64_t pair,
		std::uint64_t eye, ID3D11DeviceContext* context, const void* source = nullptr,
		const void* destination = nullptr, const void* command = nullptr) noexcept;
	void capture(snapshot& output) noexcept;
	[[nodiscard]] std::string format_recent(std::size_t limit = capacity);
}
