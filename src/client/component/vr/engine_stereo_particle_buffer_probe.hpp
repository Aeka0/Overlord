#pragma once

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_particle_buffer_probe
{
	enum class state : std::uint8_t
	{
		idle,
		recording,
		pending,
		complete,
		failed,
	};

	enum class failure : std::uint8_t
	{
		none,
		lifecycle,
		context,
		thread,
		missing_draw,
		draw_contract,
		resource_contract,
		resource_creation,
		copy,
		query,
		map,
		index_range,
		vertex_range,
	};

	inline constexpr std::uintptr_t particle_cloud_draw_caller = 0x1407BA988ull;
	inline constexpr std::uint8_t invalid_family = 0xFF;
	inline constexpr std::size_t maximum_family_samples = 4;
	inline constexpr std::uint32_t maximum_buffer_bytes = 32u * 1024u * 1024u;
	inline constexpr std::size_t maximum_difference_offsets = 32;
	inline constexpr std::size_t maximum_vertex_samples = 4;
	inline constexpr std::size_t maximum_vertex_sample_bytes = 64;
	using cpu_reference_reader = bool (*)(std::uint64_t pair_id,
		std::uint64_t device_generation, std::uintptr_t resource,
		std::uint32_t offset, std::uint32_t bytes, std::uint8_t* output,
		bool& cpu_eyes_equal) noexcept;

	struct draw
	{
		std::uint64_t output_ordinal{};
		std::uintptr_t caller{};
		std::uintptr_t vertex_shader{}, pixel_shader{};
		std::uintptr_t index_buffer{}, vertex_buffer{};
		std::uint32_t output_target_id{0xFFFFFFFFu};
		std::uint32_t index_count{}, start_index{};
		std::int32_t base_vertex{};
		std::uint32_t index_format{}, index_offset{};
		std::uint32_t vertex_stride{}, vertex_offset{};
		// Numeric value of gpu_census::dynamic_fx_family. Keeping this POD boundary
		// independent avoids a circular include between the census and readback.
		std::uint8_t family{invalid_family};
	};

	struct buffer_comparison
	{
		bool available{}, exact_range{}, identical{};
		std::array<std::uintptr_t, 2> identities{};
		std::array<std::uint64_t, 2> hashes{};
		std::uint32_t source_bytes{}, compared_offset{}, compared_bytes{},
			differing_bytes{}, first_difference{}, last_difference{};
		std::array<std::uint32_t, maximum_difference_offsets> difference_offsets{};
		std::array<std::uint8_t, maximum_difference_offsets> output0_values{},
			output1_values{};
		std::size_t difference_offset_count{};
	};

	struct vertex_sample
	{
		std::uint32_t index{}, byte_offset{}, captured_bytes{};
		bool stride_complete{}, cpu_reference_available{}, cpu_eyes_equal{};
		std::array<bool, 2> gpu_matches_cpu_reference{};
		std::array<std::array<std::uint8_t, maximum_vertex_sample_bytes>, 2> gpu{};
		std::array<std::uint8_t, maximum_vertex_sample_bytes> cpu_pre_restore{};
	};

	struct family_sample
	{
		std::uint8_t family{invalid_family};
		std::uint8_t captured_eye_mask{};
		std::array<std::uint64_t, 2> candidate_hits{};
		std::array<draw, 2> draws{};
		std::uint32_t minimum_index{}, maximum_index{};
		bool index_values_equal{}, vertex_range_resolved{};
		buffer_comparison index{}, vertex{};
		std::array<vertex_sample, maximum_vertex_samples> vertices{};
		std::size_t vertex_sample_count{};
		std::array<HRESULT, 4> staging_create_results{
			E_PENDING, E_PENDING, E_PENDING, E_PENDING};
		std::array<HRESULT, 4> map_results{
			E_PENDING, E_PENDING, E_PENDING, E_PENDING};
	};

	struct report
	{
		state current{state::idle};
		failure error{failure::none};
		std::uint64_t pair_id{}, device_generation{};
		std::uintptr_t context{};
		std::uint32_t owner_thread{}, active_eye{2};
		std::uint8_t completed_eye_mask{}, observed_family_mask{},
			complete_family_mask{}, incomplete_family_mask{};
		std::size_t sample_count{};
		std::array<family_sample, maximum_family_samples> samples{};
		HRESULT query_create_result{E_PENDING};
		HRESULT query_result{E_PENDING};
		std::uint64_t retirement_polls{}, not_ready_polls{};
	};

	[[nodiscard]] bool begin_pair(std::uint64_t pair_id,
		ID3D11DeviceContext* context, std::uint64_t device_generation,
		std::uint32_t owner_thread, cpu_reference_reader read_cpu = nullptr) noexcept;
	[[nodiscard]] bool begin_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	void capture_draw(ID3D11DeviceContext* context, std::uint32_t eye,
		const draw& value) noexcept;
	[[nodiscard]] bool end_eye(std::uint64_t pair_id, std::uint32_t eye) noexcept;
	[[nodiscard]] bool end_pair(std::uint64_t pair_id) noexcept;
	void poll(ID3D11DeviceContext* context, std::uint64_t device_generation,
		std::uint32_t owner_thread) noexcept;
	void cancel(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	[[nodiscard]] report get_report() noexcept;
	[[nodiscard]] const char* to_string(state value) noexcept;
	[[nodiscard]] const char* to_string(failure value) noexcept;
}
