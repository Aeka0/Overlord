#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace vr::engine_view_probe
{
	// H2 h2_sp64_bnet_ship.exe 2026-08-21 renderer layout observed at the
	// fingerprint-gated R_RenderScene implementation. These values describe CPU
	// memory only; this probe never creates, queries, or submits a GPU resource.
	inline constexpr std::uintptr_t frontend_slot_base_offset = 0x540400;
	inline constexpr std::uintptr_t frontend_slot_count_offset = 0x540ED4;
	inline constexpr std::uintptr_t frontend_current_record_index_offset = 0x540F90;
	inline constexpr std::uintptr_t frontend_record_count_offset = 0x540F94;
	inline constexpr std::uintptr_t frontend_slot_stride = 0x170;
	inline constexpr std::uintptr_t frontend_record_stride = 0x8090;
	// Runtime disassembly shows a 0x20240-byte arena and a backend loop bounded by
	// frontend_record_count_offset: 0x20240 / 0x8090 == 4 records.
	inline constexpr std::uint32_t frontend_record_capacity = 4;
	inline constexpr std::uintptr_t scratch_draw_type_offset = 0x524;

	inline constexpr std::size_t trace_capacity = 16 * 1024;
	inline constexpr std::uint32_t invalid_slot_index =
		(std::numeric_limits<std::uint32_t>::max)();

	enum class event_kind : std::uint8_t
	{
		begin = 1,
		callstack,
		callstack_tail,
		view_call,
		frontend,
		slot,
		record_reservation,
		generator,
		view_state,
		camera_state,
		call_return,
		end,
		flip,
		slot_initializer,
		descriptor_contract,
		ownership_boundary,
	};

	enum class ownership_boundary_kind : std::uint8_t
	{
		frame_state_transition = 1,
		command_cleanup,
		frontend_handoff,
	};

	enum class observation_stage : std::uint8_t
	{
		unknown,
		enter,
		before_call,
		after_call,
		leave,
		snapshot,
	};

	enum class record_flag : std::uint8_t
	{
		none = 0,
		orphan = 1u << 0,
		duplicate = 1u << 1,
		nested = 1u << 2,
		thread_mismatch = 1u << 3,
		inside_r_end_frame = 1u << 4,
		invalid_slot = 1u << 5,
		count_mismatch = 1u << 6,
		selector_changed = 1u << 7,
	};

	using record_flags = std::uint8_t;

	[[nodiscard]] constexpr record_flags flag(const record_flag value) noexcept
	{
		return static_cast<record_flags>(value);
	}

	[[nodiscard]] constexpr record_flags operator|(const record_flag left,
		const record_flag right) noexcept
	{
		return static_cast<record_flags>(flag(left) | flag(right));
	}

	[[nodiscard]] constexpr record_flags with_flag(const record_flags values,
		const record_flag value) noexcept
	{
		return static_cast<record_flags>(values | flag(value));
	}

	[[nodiscard]] constexpr bool has_flag(const record_flags values,
		const record_flag value) noexcept
	{
		return (values & flag(value)) != 0;
	}

	[[nodiscard]] constexpr std::uint64_t pack_u32_pair(const std::uint32_t low,
		const std::uint32_t high) noexcept
	{
		return static_cast<std::uint64_t>(low) |
			(static_cast<std::uint64_t>(high) << 32);
	}

	[[nodiscard]] constexpr std::uint32_t unpack_low_u32(const std::uint64_t value) noexcept
	{
		return static_cast<std::uint32_t>(value);
	}

	[[nodiscard]] constexpr std::uint32_t unpack_high_u32(const std::uint64_t value) noexcept
	{
		return static_cast<std::uint32_t>(value >> 32);
	}

	[[nodiscard]] constexpr std::uint64_t pack_u16_quad(const std::uint16_t value0,
		const std::uint16_t value1, const std::uint16_t value2,
		const std::uint16_t value3) noexcept
	{
		return static_cast<std::uint64_t>(value0) |
			(static_cast<std::uint64_t>(value1) << 16) |
			(static_cast<std::uint64_t>(value2) << 32) |
			(static_cast<std::uint64_t>(value3) << 48);
	}

	[[nodiscard]] constexpr std::uint16_t unpack_u16(const std::uint64_t value,
		const std::uint32_t index) noexcept
	{
		return static_cast<std::uint16_t>(value >> (index * 16));
	}

	[[nodiscard]] constexpr std::uint64_t pack_i32_pair(const std::int32_t low,
		const std::int32_t high) noexcept
	{
		return pack_u32_pair(static_cast<std::uint32_t>(low),
			static_cast<std::uint32_t>(high));
	}

	[[nodiscard]] constexpr std::int32_t unpack_low_i32(const std::uint64_t value) noexcept
	{
		return static_cast<std::int32_t>(unpack_low_u32(value));
	}

	[[nodiscard]] constexpr std::int32_t unpack_high_i32(const std::uint64_t value) noexcept
	{
		return static_cast<std::int32_t>(unpack_high_u32(value));
	}

	[[nodiscard]] constexpr std::uint32_t float_bits(const float value) noexcept
	{
		return std::bit_cast<std::uint32_t>(value);
	}

	[[nodiscard]] constexpr float float_from_bits(const std::uint32_t value) noexcept
	{
		return std::bit_cast<float>(value);
	}

	struct slot_index_result
	{
		std::uint32_t index{invalid_slot_index};
		bool aligned{};
		bool within_count{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return index != invalid_slot_index && aligned && within_count;
		}
	};

	[[nodiscard]] constexpr std::uintptr_t slot_base_address(
		const std::uintptr_t frontend) noexcept
	{
		if (frontend == 0 || frontend >
			(std::numeric_limits<std::uintptr_t>::max)() - frontend_slot_base_offset)
		{
			return 0;
		}
		return frontend + frontend_slot_base_offset;
	}

	[[nodiscard]] constexpr slot_index_result derive_slot_index(
		const std::uintptr_t frontend, const std::uintptr_t slot,
		const std::uint32_t slot_count) noexcept
	{
		const auto base = slot_base_address(frontend);
		if (base == 0 || slot < base || frontend_slot_stride == 0)
		{
			return {};
		}

		const auto delta = slot - base;
		if ((delta % frontend_slot_stride) != 0)
		{
			return {invalid_slot_index, false, false};
		}

		const auto wide_index = delta / frontend_slot_stride;
		if (wide_index > (std::numeric_limits<std::uint32_t>::max)())
		{
			return {invalid_slot_index, true, false};
		}

		const auto index = static_cast<std::uint32_t>(wide_index);
		return {index, true, index < slot_count};
	}

	struct transaction_token
	{
		std::uint64_t transaction_id{};
		std::uint64_t frontend_frame_id{};
		std::uint32_t owner_thread_id{};
		std::uint16_t depth{};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return transaction_id != 0;
		}
	};

	struct outer_observation
	{
		std::uintptr_t scene_descriptor{};
		std::uint64_t scene_descriptor_prefix_hash{};
		std::int32_t local_client{};
		std::uint32_t scene_record_index{};
		float lod_scale{};
		std::int32_t draw_type{};
		std::uint64_t parent_transaction_id{};
	};

	// Raw return addresses captured at the exact H2 R_RenderScene callsite. The
	// probe records addresses only; symbolization and formatting remain on the
	// control plane so the renderer hook never performs module or symbol lookup.
	struct callstack_observation
	{
		std::array<std::uintptr_t, 16> frames{};
	};

	// One explicitly ordered observation of H2's original scene-builder call.
	// The call index is diagnostic ordering only; it is never an eye assignment.
	struct view_call_observation
	{
		std::uint32_t call_index{};
		std::uintptr_t frontend{};
		std::uint64_t selector{};
		std::uint32_t slot_count{};
		std::uintptr_t scene_descriptor{};
		std::uint64_t scene_descriptor_prefix_hash{};
		std::uint32_t global_record_count{};
		std::uint32_t current_record_index{};
		std::uint32_t record_count{};
	};

	struct frontend_observation
	{
		std::uintptr_t frontend{};
		std::uint64_t selector{};
		std::uint32_t slot_count{};
		std::uint32_t current_record_index{};
		std::uint32_t record_count{};
		std::uint32_t global_record_count{};
		std::uintptr_t slot_base{};
		std::uint64_t auxiliary{};
	};

	struct slot_observation
	{
		std::uintptr_t frontend{};
		std::uintptr_t slot{};
		std::uint32_t count_before{};
		std::uint32_t count_after{};
		std::uint32_t slot_index{invalid_slot_index};
		std::uint64_t selector{};
		std::uint32_t current_record_index{};
		std::uint32_t record_count{};
		std::uint32_t global_record_count{};
	};

	// Exact before/after evidence for H2's view-slot initializer at 0x14077E340.
	// All hashes cover CPU memory only. changed byte offsets are relative to the
	// 0x170-byte slot and remain invalid_slot_index when the slot did not change.
	struct slot_initializer_observation
	{
		std::uintptr_t scene_descriptor{};
		std::uintptr_t slot{};
		std::uint64_t scene_descriptor_prefix_hash{};
		std::uint64_t slot_before_hash{};
		std::uint64_t slot_after_hash{};
		std::uint64_t shared_before_hash{};
		std::uint64_t shared_after_hash{};
		std::uint32_t changed_bytes{};
		std::uint32_t first_changed{invalid_slot_index};
		std::uint32_t last_changed{invalid_slot_index};
		std::uint32_t slot_index{invalid_slot_index};
	};

	// Bounded observation of the complete descriptor range that H2's recovered
	// frontend path can address.  This proves only in-call byte stability; it does
	// not assert ownership of pointers stored inside the descriptor.
	struct descriptor_contract_observation
	{
		std::uintptr_t scene_descriptor{};
		std::uint64_t before_hash{};
		std::uint64_t after_hash{};
		std::uint32_t observed_size{};
		bool readable_before{};
		bool readable_after{};
	};

	struct record_reservation_observation
	{
		std::uintptr_t frontend_before{};
		std::uintptr_t frontend_after{};
		std::uint32_t global_count_before{};
		std::uint32_t global_count_after{};
		std::uint32_t output_index{invalid_slot_index};
		std::uint32_t current_index_before{};
		std::uint32_t current_index_after{};
		std::uint32_t record_count_before{};
		std::uint32_t record_count_after{};
		std::uint32_t slot_count_before{};
		std::uint32_t slot_count_after{};
		std::uint32_t selector_before{};
		std::uint32_t selector_after{};
	};

	struct generator_observation
	{
		std::int32_t local_client{};
		std::uint32_t scene_record_index{};
		std::uintptr_t scratch{};
		std::uintptr_t selected{};
		std::uintptr_t slot{};
		std::uintptr_t per_client_output{};
		std::int32_t draw_type{};
		std::uintptr_t frontend{};
		std::uint32_t slot_count{};
		std::uint32_t slot_index{invalid_slot_index};
	};

	enum class view_state_source : std::uint8_t
	{
		unknown,
		outer_scene,
		draw_surface_generator,
	};

	enum class view_state_relation : std::uint8_t
	{
		none = 0,
		slot_compared = 1u << 0,
		slot_unchanged = 1u << 1,
		output_compared = 1u << 2,
		output_matches_slot = 1u << 3,
	};

	using view_state_relations = std::uint8_t;

	[[nodiscard]] constexpr view_state_relations relation(
		const view_state_relation value) noexcept
	{
		return static_cast<view_state_relations>(value);
	}

	[[nodiscard]] constexpr view_state_relations with_relation(
		const view_state_relations values, const view_state_relation value) noexcept
	{
		return static_cast<view_state_relations>(values | relation(value));
	}

	[[nodiscard]] constexpr bool has_relation(const view_state_relations values,
		const view_state_relation value) noexcept
	{
		return (values & relation(value)) != 0;
	}

	// Hashes of the exact CPU-resident state carriers observed around one natural
	// H2 view build. A hash is identity evidence only: this probe never copies or
	// mutates the scene, view slot, shared globals, D3D resources, or command list.
	struct view_state_observation
	{
		view_state_source source{view_state_source::unknown};
		view_state_relations relations{};
		std::uintptr_t subject{};
		std::uint64_t view_slot_hash{};
		std::uint64_t per_client_output_hash{};
		std::uint64_t scene_globals_hash{};
		std::uint64_t camera_primary_hash{};
		std::uint64_t camera_secondary_hash{};
		std::uint64_t owner_globals_hash{};
	};

	enum class camera_state_source : std::uint8_t
	{
		unknown,
		set_viewpos_now,
		camera_helper,
	};

	// CPU-only evidence captured by H2-MOD's existing camera hooks. The helper
	// input/output sizes are taken from the recovered 0x140781090 body; unknown
	// set_viewpos arguments are never dereferenced by the probe.
	struct camera_state_observation
	{
		camera_state_source source{camera_state_source::unknown};
		std::uint32_t scalar_bits{};
		std::uintptr_t caller{};
		std::uintptr_t input{};
		std::uintptr_t output{};
		std::uint64_t input_hash{};
		std::uint64_t output_hash{};
		std::uint64_t refdef_prefix_hash{};
		std::uint64_t owner_globals_hash{};
	};

	enum class return_source : std::uint8_t
	{
		unknown,
		allocator,
		generator,
		outer,
	};

	struct return_observation
	{
		return_source source{ return_source::unknown };
		std::uintptr_t object{};
		std::uint64_t result{};
		std::uint32_t slot_count{};
		std::uint32_t slot_index{invalid_slot_index};
		std::uint64_t selector{};
		std::uint32_t current_record_index{};
		std::uint32_t record_count{};
		std::int32_t draw_type{};
		std::uint64_t auxiliary{};
	};

	struct end_observation
	{
		std::uint32_t count_begin{};
		std::uint32_t count_end{};
		std::uint32_t slot_calls{};
		std::uint32_t generator_calls{};
		std::uint32_t return_calls{};
		std::uintptr_t last_slot{};
		std::uint32_t last_slot_index{invalid_slot_index};
		std::uint64_t selector_begin{};
		std::uint64_t selector_end{};
		std::uint64_t slot_signature{};
	};

	struct flip_observation
	{
		std::uintptr_t frontend_before{};
		std::uintptr_t frontend_after{};
		std::uint32_t selector_before{};
		std::uint32_t selector_after{};
		std::uint32_t count_before{};
		std::uint32_t count_after{};
		std::uint32_t current_record_index_before{};
		std::uint32_t current_record_index_after{};
		std::uint32_t record_count_before{};
		std::uint32_t record_count_after{};
		std::uint32_t global_record_count_before{};
		std::uint32_t global_record_count_after{};
		std::uint64_t reason{};
	};

	struct ownership_boundary_observation
	{
		ownership_boundary_kind boundary{
			ownership_boundary_kind::frame_state_transition};
		std::uintptr_t frontend{};
		std::uintptr_t backend_frontend{};
		std::uintptr_t record_arena{};
		std::uint32_t selector{};
		std::uint32_t slot_count{};
		std::uint32_t current_record_index{};
		std::uint32_t record_count{};
		std::uint32_t global_record_count{};
		std::uint32_t owner_record_index{};
		std::uint64_t owner_view_hash{};
	};

	// A decoded ring record. values[] has an event-specific layout documented by
	// the corresponding record_* function implementation. The hot path never
	// constructs a string or a dynamic container.
	struct record
	{
		std::uint64_t sequence{};
		std::uint64_t timestamp_qpc{};
		std::uint64_t transaction_id{};
		std::uint64_t frontend_frame_id{};
		std::uint32_t thread_id{};
		std::uint16_t depth{};
		event_kind kind{event_kind::begin};
		observation_stage stage{observation_stage::unknown};
		record_flags flags{};
		std::array<std::uint64_t, 8> values{};
	};

	struct status
	{
		bool enabled{};
		std::uint64_t newest_sequence{};
		std::uint64_t transaction_count{};
		std::uint64_t overwrite_count{};
		std::uint64_t dropped_records{};
		std::uint64_t orphan_records{};
		std::uint64_t duplicate_records{};
		std::uint64_t invalid_slot_records{};
		std::uint64_t count_mismatch_records{};
		std::uint64_t thread_mismatch_records{};
		std::uint64_t view_state_snapshots{};
		std::uint64_t stable_slot_comparisons{};
		std::uint64_t changed_slot_comparisons{};
		std::uint64_t matching_output_copies{};
		std::uint64_t mismatching_output_copies{};
		std::uint64_t camera_state_snapshots{};
		std::uint64_t scoped_camera_state_snapshots{};
		std::uint64_t unscoped_camera_state_snapshots{};
		std::uint64_t set_viewpos_calls{};
		std::uint64_t camera_helper_calls{};
		std::uint64_t slot_initializer_calls{};
		std::uint64_t slot_initializer_slot_changes{};
		std::uint64_t slot_initializer_shared_changes{};
		std::uint64_t slot_initializer_invalid_slots{};
		std::uint64_t descriptor_contract_samples{};
		std::uint64_t descriptor_contract_stable{};
		std::uint64_t descriptor_contract_changes{};
		std::uint64_t descriptor_contract_unreadable{};
	};

	void set_enabled(bool enabled) noexcept;
	[[nodiscard]] bool is_enabled() noexcept;
	// Reset is a control-plane operation. Call it while hooks are disabled; it is
	// intentionally not part of the renderer hot path.
	void reset() noexcept;

	[[nodiscard]] transaction_token begin(std::uint64_t frontend_frame_id,
		std::uint16_t depth, const outer_observation& observation,
		record_flags flags = 0) noexcept;
	void record_callstack(const transaction_token& token,
		const callstack_observation& observation,
		record_flags flags = 0) noexcept;
	void record_view_call(const transaction_token& token,
		const view_call_observation& observation, observation_stage stage,
		record_flags flags = 0) noexcept;
	void record_frontend(const transaction_token& token,
		const frontend_observation& observation,
		observation_stage stage = observation_stage::snapshot,
		record_flags flags = 0) noexcept;
	void record_slot(const transaction_token& token,
		const slot_observation& observation,
		observation_stage stage = observation_stage::after_call,
		record_flags flags = 0) noexcept;
	void record_slot_initializer(const transaction_token& token,
		const slot_initializer_observation& observation,
		observation_stage stage = observation_stage::after_call,
		record_flags flags = 0) noexcept;
	void record_descriptor_contract(const transaction_token& token,
		const descriptor_contract_observation& observation,
		observation_stage stage = observation_stage::after_call,
		record_flags flags = 0) noexcept;
	void record_reservation(const transaction_token& token,
		const record_reservation_observation& observation,
		observation_stage stage = observation_stage::after_call,
		record_flags flags = 0) noexcept;
	void record_generator(const transaction_token& token,
		const generator_observation& observation,
		observation_stage stage = observation_stage::before_call,
		record_flags flags = 0) noexcept;
	void record_view_state(const transaction_token& token,
		const view_state_observation& observation,
		observation_stage stage = observation_stage::snapshot,
		record_flags flags = 0) noexcept;
	void record_camera_state(const transaction_token& token,
		const camera_state_observation& observation,
		observation_stage stage = observation_stage::snapshot,
		record_flags flags = 0) noexcept;
	void record_unscoped_camera_state(std::uint64_t frontend_frame_id,
		const camera_state_observation& observation,
		observation_stage stage = observation_stage::snapshot,
		record_flags flags = 0) noexcept;
	void record_return(const transaction_token& token,
		const return_observation& observation,
		observation_stage stage = observation_stage::after_call,
		record_flags flags = 0) noexcept;
	void end(const transaction_token& token, const end_observation& observation,
		record_flags flags = 0) noexcept;
	void record_flip(const transaction_token& token,
		const flip_observation& observation,
		record_flags flags = 0) noexcept;
	void record_frame_flip(std::uint64_t frontend_frame_id, std::uint16_t depth,
		const flip_observation& observation, record_flags flags = 0) noexcept;
	void record_ownership_boundary(std::uint64_t frontend_frame_id,
		std::uint16_t depth, const ownership_boundary_observation& observation,
		observation_stage stage, record_flags flags = 0) noexcept;

	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] std::size_t read_recent(record* output, std::size_t capacity) noexcept;
	// Formatting is deliberately a control-plane API and may allocate. Never call
	// it from an H2 renderer hook.
	[[nodiscard]] std::string format_recent(std::size_t maximum_records = trace_capacity);
	[[nodiscard]] const char* to_string(event_kind value) noexcept;
	[[nodiscard]] const char* to_string(observation_stage value) noexcept;
	[[nodiscard]] const char* to_string(return_source value) noexcept;
	[[nodiscard]] const char* to_string(view_state_source value) noexcept;
	[[nodiscard]] const char* to_string(camera_state_source value) noexcept;
	[[nodiscard]] const char* to_string(ownership_boundary_kind value) noexcept;
}
