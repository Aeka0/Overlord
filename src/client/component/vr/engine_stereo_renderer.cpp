#include <std_include.hpp>
#include "component/vr/native_render_contract.hpp"

#include "loader/component_loader.hpp"

#include "component/console.hpp"
#include "diagnostics.hpp"
#include "diagnostics/runtime_code_snapshot.hpp"
#include "engine_backend_probe.hpp"
#include "engine_stereo_binding.hpp"
#include "eye_composition.hpp"
#include "engine_stereo_backend_target.hpp"
#include "engine_stereo_backend_view.hpp"
#include "engine_stereo_bridge.hpp"
#include "engine_stereo_draw_indexed.hpp"
#include "engine_stereo_execution.hpp"
#include "engine_stereo_gpu_timing.hpp"
#include "engine_stereo_output_merger.hpp"
#include "engine_stereo_owner_pass.hpp"
#include "engine_scene_resolution.hpp"
#include "engine_scene_job_capture.hpp"
#include "engine_scene_completion.hpp"
#include "engine_stereo_replay.hpp"
#include "engine_stereo_probe.hpp"
#include "engine_stereo_renderer.hpp"
#include "debug_options.hpp"
#include "engine_stereo_view.hpp"
#include "engine_view_probe.hpp"
#include "game/game.hpp"
#include "native_render_session.hpp"
#include "native_fullscreen_blur.hpp"

#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/cryptography.hpp>
#include <utils/io.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace vr::engine_stereo_renderer
{
	namespace
	{
		static_assert(offsetof(game::Material, techniqueSet) == 0x138);
		static_assert(offsetof(game::MaterialInfo, textureAtlasRowCount) == 0xA);
		static_assert(offsetof(game::MaterialTechniqueHeader, passCount) == 0xA);
		static_assert(offsetof(game::MaterialTechnique, passArray) == 0x10);
		static_assert(sizeof(game::MaterialPass) == 0x48);
		static_assert(sizeof(game::GfxStateBits) == 0x28);
		constexpr std::uintptr_t reserve_scene_record_call_site = 0x1403CA20E;
		constexpr std::uintptr_t reserve_scene_record_function = 0x140779E00;
		constexpr std::uintptr_t scene_owner_record_index = 0x141C2DB34;
		constexpr std::uintptr_t scene_owner_argument = 0x141BB3C1C;
		constexpr std::uintptr_t render_scene_call_site = 0x1403CA23E;
		constexpr std::uintptr_t render_scene_function = 0x14077B820;
		constexpr std::uintptr_t allocate_view_slot_call_site = 0x14077B8EB;
		constexpr std::uintptr_t allocate_view_slot_function = 0x14076D5D0;
		constexpr std::uintptr_t initialize_view_slot_call_site = 0x14077B8F9;
		constexpr std::uintptr_t initialize_view_slot_function = 0x14077E340;
		constexpr std::uintptr_t finalize_view_slot_function = 0x14077F550;
		constexpr std::uintptr_t generate_draw_surfs_call_site = 0x14077BC83;
		constexpr std::uintptr_t generate_draw_surfs_function = 0x140778E60;
		constexpr std::uintptr_t fx_camera_call_site = 0x14045B98E;
		constexpr std::uintptr_t fx_camera_function = 0x140463310;
		constexpr std::uintptr_t frontend_data_pointer = 0x150F91188;
		constexpr std::uintptr_t frontend_global_selector = 0x150F911A0;
		constexpr std::uintptr_t global_scene_record_count = 0x14EEE0BD0;
		constexpr std::uintptr_t backend_post_bind_call_site = 0x1407A8469;
		constexpr std::uintptr_t backend_post_bind_function = 0x1407A7320;
		constexpr std::uintptr_t backend_target_prepare_function = 0x1407A7DA0;
		constexpr std::uintptr_t backend_dynamic_upload_call_site = 0x1407A8032;
		constexpr std::uintptr_t backend_dynamic_upload_function = 0x1407A8A80;
		constexpr std::array<std::uint8_t, 5> backend_dynamic_upload_bytes{
			0xE8, 0x49, 0x0A, 0x00, 0x00};
		constexpr std::uintptr_t backend_frontend_data_pointer = 0x151A7F3B0;
		constexpr std::uintptr_t backend_record_call_site = 0x1407A84A1;
		constexpr std::uintptr_t backend_record_function = 0x1407A6BE0;
		constexpr std::uintptr_t backend_view_bind_call_site = 0x1407A83D7;
		constexpr std::uintptr_t backend_view_bind_function = 0x14079F6F0;
		constexpr std::uintptr_t backend_record_classifier_call_site = 0x1407A83E6;
		constexpr std::uintptr_t backend_record_classifier_function = 0x1407B0740;
		// The historical "classifier" is actually H2's final PostFX owner.
		constexpr std::uintptr_t postfx_destination_call_site = 0x1407B0329;
		constexpr std::uintptr_t postfx_destination_function = 0x1407B09A0;
		constexpr std::array<std::uint8_t, 5> postfx_destination_bytes{
			0xE8, 0x72, 0x06, 0x00, 0x00};
		constexpr std::uintptr_t backend_view_copy_call_site = 0x14078A3FF;
		constexpr std::uintptr_t backend_view_copy_function = 0x14078B5D0;
		constexpr std::uintptr_t backend_view_setup_function = 0x14078A340;
		constexpr std::uintptr_t backend_geometry_view_setup_call_site = 0x14078727E;
		constexpr std::array<std::uint8_t, 5> backend_geometry_view_setup_bytes{
			0xE8, 0xBD, 0x30, 0x00, 0x00};
		constexpr std::array<std::uint8_t, 15> backend_view_setup_prologue{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
			0x57, 0x48, 0x83, 0xEC, 0x30};
		bool backend_view_setup_stack_verified{};
		constexpr std::uintptr_t backend_depth_hack_projection_function = 0x140786D30;
		constexpr std::uintptr_t backend_target_select_function = 0x1407892C0;
		constexpr std::uintptr_t backend_command_dispatch_call_site = 0x1407A6D05;
		constexpr std::uintptr_t backend_command_dispatch_function = 0x14079EAE0;
		constexpr std::uintptr_t h2_query_publish_call_site = 0x1407A1555;
		constexpr std::uintptr_t h2_query_publish_function = 0x14074BB90;
		constexpr std::uintptr_t h2_query_publish_generation = 0x14EEE0D38;
		constexpr std::uintptr_t h2_query_gate_completed_generation = 0x14EE43A04;
		constexpr std::uintptr_t h2_query_gate_published_generation = 0x14EEE0D2C;
		constexpr std::uintptr_t h2_query_result_observation_site = 0x1407507E3;
		constexpr std::uintptr_t h2_query_result_original_global = 0x140C019DC;
		constexpr std::uintptr_t frontend_record_arena_pointer_offset = 0x540FA0;
		constexpr std::uintptr_t owner_view_globals = 0x141EB3970;
		constexpr std::uintptr_t scene_view_globals = 0x14EEE02F4;
		constexpr std::uintptr_t camera_primary_globals = 0x14EEE0890;
		constexpr std::uintptr_t camera_secondary_globals = 0x14EEE08B8;
		constexpr std::uintptr_t per_client_view_output = 0x151427A80;
		constexpr std::size_t owner_view_globals_size = 0x18;
		constexpr std::size_t scene_view_globals_size = 0x18;
		constexpr std::size_t camera_globals_size = 0x28;
		constexpr std::size_t view_slot_size = engine_view_probe::frontend_slot_stride;
		static_assert(engine_stereo_view::h2_scene_record_size ==
			engine_view_probe::frontend_record_stride);
		constexpr std::size_t extended_scene_descriptor_size = 0x501C8;
		constexpr std::uint64_t extended_scene_descriptor_sample_limit = 256;
		constexpr std::size_t scene_view_input_size = 0x60;
		constexpr std::int32_t observed_local_client_capacity = 2;
		constexpr std::size_t maximum_r_end_frame_depth = 8;

		using reserve_scene_record_fn = void(*)(std::uint32_t* output_index);
		using render_scene_fn = void(*)(int local_client_num, void* scene_descriptor,
			std::uint32_t scene_record_index, float lod_scale, int draw_type);
		using allocate_view_slot_fn = void* (*)();
		using initialize_view_slot_fn = void(*)(const void* scene_descriptor, void* slot);
		using finalize_view_slot_fn = void(*)(void* slot);
		using generate_draw_surfs_fn = void(*)(std::uint32_t local_client,
			std::uint32_t frontend_record_index, void* scratch, void* selected,
			void* slot, void* per_client_output);
		using backend_post_bind_fn = void(*)(void* record, std::uint32_t target_id);
		using backend_record_fn = void(*)(void* record);
		using backend_view_bind_fn = bool(*)(void* record);
		using backend_record_classifier_fn = void(*)(void* record, void* frontend,
			bool dimensions_match);
		using backend_view_copy_fn = void(*)(void* backend_state);
		using backend_view_setup_fn = void(*)(void* backend_state,
			const void* parameters, const void* primary_view, const void* rebase_view);
		using backend_target_select_fn = void(*)(void* context, std::uint32_t target_id);
		using backend_command_dispatch_fn = void(*)(void* commands,
			const int* filter, bool flagged_mode);
		using h2_query_publish_fn = void(*)(void* query);

		struct frontend_snapshot
		{
			std::uintptr_t frontend{};
			std::uint32_t slot_count{};
			std::uint32_t global_selector{};
			std::uint32_t current_record_index{};
			std::uint32_t record_count{};
			std::uint32_t global_record_count{};
		};

		struct candidate_observation
		{
			frontend_snapshot allocator_before;
			frontend_snapshot allocator_after;
			frontend_snapshot generator_before;
			frontend_snapshot generator_after;
			std::uintptr_t allocator_slot{};
			std::uintptr_t generator_slot{};
			std::uintptr_t generator_selected{};
			std::uintptr_t generator_scratch{};
			std::uintptr_t generator_output{};
			std::uint32_t allocator_index{engine_view_probe::invalid_slot_index};
			std::uint32_t generator_index{engine_view_probe::invalid_slot_index};
			std::uint32_t generator_record_index{engine_view_probe::invalid_slot_index};
			std::uint32_t generator_local_client{};
			std::int32_t generator_draw_type{};
			bool allocator_seen{};
			bool generator_seen{};
		};

		struct view_transaction_context
		{
			engine_view_probe::transaction_token token;
			view_transaction_context* previous{};
			frontend_snapshot begin_snapshot;
			engine_view_probe::record_flags flags{};
			std::uint32_t slot_calls{};
			std::uint32_t generator_calls{};
			std::uint32_t return_calls{};
			std::uintptr_t last_slot{};
			std::uint32_t last_slot_index{engine_view_probe::invalid_slot_index};
			std::uint64_t slot_signature{};
			std::uint64_t last_generator_slot_hash{};
			bool last_generator_state_valid{};
			std::array<std::uint8_t, view_slot_size> frontend_slot{};
			engine_stereo_view::slot_pair stereo_views{};
			bool stereo_views_ready{};
			bool stereo_published{};
			std::array<candidate_observation, 2> candidates{};
		};

		struct r_end_frame_scope
		{
			std::uint64_t frame_id{};
			frontend_snapshot before;
			bool trace_active{};
		};

		struct pending_record_reservation
		{
			engine_view_probe::record_reservation_observation observation;
			std::uint64_t frontend_epoch{};
			bool valid{};
			bool overwrote_previous{};
		};

		std::atomic_uint64_t frontend_epoch_sequence{};
		std::atomic_uint64_t target_prepare_owner_sequence{};
		std::atomic_uint64_t scene_publication_attempts{}, scene_publication_successes{}, scene_publication_failures{};
		std::atomic_uint64_t culling_union_attempts{};
		std::atomic_uint64_t culling_union_applications{};
		std::atomic_uint64_t culling_union_failures{};
		std::atomic_uint64_t fx_culling_attempts{};
		std::atomic_uint64_t fx_culling_applications{};
		std::atomic_uint64_t fx_culling_failures{};
		std::atomic_bool fx_culling_contract_failed{};
		std::atomic_uint32_t culling_union_tan_left{};
		std::atomic_uint32_t culling_union_tan_right{};
		std::atomic_uint32_t culling_union_tan_down{};
		std::atomic_uint32_t culling_union_tan_up{};
		std::atomic_uint32_t culling_union_near_distance{};
		std::atomic_uint32_t culling_union_horizontal_expansion{};
		thread_local view_transaction_context* active_view_transaction{};
		thread_local std::array<r_end_frame_scope, maximum_r_end_frame_depth> r_end_frame_scopes{};
		thread_local std::uint16_t r_end_frame_depth{};
		thread_local std::uint64_t active_boundary_frame_id{};
		thread_local std::uint16_t frame_state_transition_depth{};
		thread_local std::uint16_t frontend_handoff_depth{};
		thread_local pending_record_reservation pending_reservation{};
		thread_local engine_backend_probe::backend_token active_backend_transaction{};
		thread_local engine_stereo_binding::backend_claim active_stereo_binding{};
		thread_local engine_stereo_backend_view::transaction active_backend_view_copy{};
		thread_local engine_stereo_backend_target::transaction active_backend_target_route{};
		thread_local engine_stereo_output_merger::transaction active_output_merger{};
		thread_local engine_stereo_draw_indexed::transaction active_draw_indexed{};
		thread_local engine_stereo_owner_pass::transaction active_owner_pass{};
		thread_local bool dynamic_upload_owner_scope{};
		utils::hook::detour backend_target_prepare_hook{};
		utils::hook::detour backend_target_select_hook{};
		utils::hook::detour backend_depth_hack_projection_hook{};

		inline constexpr std::size_t evidence_metadata_count = 8;
		inline constexpr auto evidence_directory =
			"minidumps/vr-native-stereo-evidence";

		template <std::size_t Size>
		struct one_shot_artifact
		{
			// 0=empty, 1=producer copying, 2=immutable and readable. File I/O is
			// deliberately tracked separately and occurs only on the diagnostics thread.
			std::atomic_uint32_t state{};
			std::array<std::uint8_t, Size> bytes{};
			std::array<std::uint64_t, evidence_metadata_count> metadata{};
			std::atomic_bool written{};
		};

		one_shot_artifact<native_render_contract::target_registry_size>
			baseline_target_registry_artifact{};
		one_shot_artifact<extended_scene_descriptor_size> descriptor_before_artifact{};
		one_shot_artifact<extended_scene_descriptor_size> descriptor_after_artifact{};
		one_shot_artifact<view_slot_size> slot_before_initializer_artifact{};
		one_shot_artifact<view_slot_size> slot_after_initializer_artifact{};
		std::array<one_shot_artifact<view_slot_size>, 2> stereo_eye_slot_artifacts{};
		std::atomic_uint32_t stereo_eye_slot_capture_state{};
		one_shot_artifact<view_slot_size> backend_view_source_before_artifact{};
		one_shot_artifact<view_slot_size> backend_view_source_after_artifact{};
		std::array<one_shot_artifact<view_slot_size>, 2>
			backend_bound_eye_slot_artifacts{};
		one_shot_artifact<view_slot_size> slot_after_generator_artifact{};
		one_shot_artifact<view_slot_size> output_after_generator_artifact{};
		one_shot_artifact<engine_view_probe::frontend_record_stride>
			record_before_target_prepare_artifact{};
		one_shot_artifact<engine_view_probe::frontend_record_stride>
			record_after_target_prepare_artifact{};
		one_shot_artifact<native_render_contract::target_registry_size>
			registry_before_target_prepare_artifact{};
		one_shot_artifact<native_render_contract::target_registry_size>
			registry_after_target_prepare_artifact{};
		std::atomic_bool baseline_registry_summary_written{};
		std::atomic_bool before_registry_summary_written{};
		std::atomic_bool after_registry_summary_written{};
		std::atomic_bool backend_target_route_manifest_written{};
		std::atomic_bool backend_target_frame_manifest_written{};
		std::atomic_bool backend_output_merger_manifest_written{};
		std::atomic_bool backend_draw_indexed_manifest_written{};
		// Disk files can outlive H2-MOD. Track the exact process-lifetime terminal
		// manifest contents, not merely file existence, so an earlier process can
		// never authorize a safe-close notification.
		std::atomic_uint64_t backend_execution_manifest_signature{};
		std::atomic_bool backend_stereo_replay_manifest_written{};
		// This is the FNV-1a content hash of the exact aggregate manifest bytes
		// atomically written by this process. It is deliberately not a readiness
		// bitmap: every serialized context, generation, counter, and execution
		// identity change produces a new version key and a new atomic replacement.
		std::atomic_uint64_t artifact_manifest_signature{};
		std::atomic_bool artifact_completion_announced{};
		std::atomic_bool backend_execution_completion_announced{};

		struct ownership_boundary_artifact
		{
			std::atomic_uint32_t state{};
			engine_view_probe::ownership_boundary_observation observation{};
			std::uint64_t frontend_frame_id{};
			std::uint64_t capture_tick{};
			std::uint32_t thread_id{};
		};

		inline constexpr std::size_t ownership_boundary_count = 3;
		inline constexpr std::size_t ownership_boundary_stage_count = 2;
		std::array<ownership_boundary_artifact,
			ownership_boundary_count * ownership_boundary_stage_count>
			ownership_boundary_artifacts{};
		std::atomic_uint64_t ownership_boundary_capture_frame{};
		std::atomic_uint64_t ownership_boundary_manifest_signature{
			(std::numeric_limits<std::uint64_t>::max)()};
		std::atomic_bool retired_dual_record_manifest_written{};
		std::atomic_bool retired_owner_stereo_manifest_written{};

		frontend_snapshot read_frontend_snapshot() noexcept
		{
			frontend_snapshot result;
			result.frontend = reinterpret_cast<std::uintptr_t>(
				*reinterpret_cast<void**>(frontend_data_pointer));
			result.global_selector = *reinterpret_cast<const std::uint32_t*>(
				frontend_global_selector);
			result.global_record_count = *reinterpret_cast<const std::uint32_t*>(
				global_scene_record_count);
			if (result.frontend != 0)
			{
				result.slot_count = *reinterpret_cast<const std::uint32_t*>(result.frontend +
					engine_view_probe::frontend_slot_count_offset);
				result.current_record_index = *reinterpret_cast<const std::uint32_t*>(
					result.frontend + engine_view_probe::frontend_current_record_index_offset);
				result.record_count = *reinterpret_cast<const std::uint32_t*>(
					result.frontend + engine_view_probe::frontend_record_count_offset);
			}
			return result;
		}

		bool view_diagnostics_enabled() noexcept
		{
			return debug_options::enabled(debug_options::probe::view);
		}

		std::uint64_t hash_memory_region(const std::uintptr_t address,
			const std::size_t size) noexcept
		{
			if (address == 0 || size == 0) return 0;
			constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
			constexpr std::uint64_t fnv_prime = 1099511628211ull;
			auto hash = fnv_offset;
			const auto* const bytes = reinterpret_cast<const volatile unsigned char*>(address);
			for (std::size_t index{}; index < size; ++index)
			{
				hash ^= bytes[index];
				hash *= fnv_prime;
			}
			return hash;
		}

		std::uint64_t ensure_boundary_frame_id() noexcept
		{
			if (active_boundary_frame_id == 0)
			{
				active_boundary_frame_id = frontend_epoch_sequence.fetch_add(
					1, std::memory_order_relaxed) + 1;
			}
			return active_boundary_frame_id;
		}

		void record_ownership_boundary(
			const engine_view_probe::ownership_boundary_kind boundary,
			const engine_view_probe::observation_stage stage,
			const std::uint16_t depth) noexcept
		{
			if (!view_diagnostics_enabled() || !engine_view_probe::is_enabled()) return;
			const auto snapshot = read_frontend_snapshot();
			std::uintptr_t record_arena{};
			if (snapshot.frontend != 0)
			{
				std::memcpy(&record_arena, reinterpret_cast<const void*>(
					snapshot.frontend + frontend_record_arena_pointer_offset),
					sizeof(record_arena));
			}
			const auto backend_frontend = reinterpret_cast<std::uintptr_t>(
				*reinterpret_cast<void**>(backend_frontend_data_pointer));
			const auto frame_id = ensure_boundary_frame_id();
			const engine_view_probe::ownership_boundary_observation observation{
					boundary,
					snapshot.frontend,
					backend_frontend,
					record_arena,
					snapshot.global_selector,
					snapshot.slot_count,
					snapshot.current_record_index,
					snapshot.record_count,
					snapshot.global_record_count,
					*reinterpret_cast<const std::uint32_t*>(scene_owner_record_index),
					hash_memory_region(owner_view_globals, owner_view_globals_size),
			};
			engine_view_probe::record_ownership_boundary(frame_id, depth, observation,
				stage);

			const auto stage_index = stage == engine_view_probe::observation_stage::enter
				? std::size_t{0}
				: (stage == engine_view_probe::observation_stage::leave
					? std::size_t{1} : ownership_boundary_stage_count);
			const auto boundary_value = static_cast<std::size_t>(boundary);
			if (stage_index >= ownership_boundary_stage_count || boundary_value == 0 ||
				boundary_value > ownership_boundary_count)
			{
				return;
			}

			auto capture_frame = ownership_boundary_capture_frame.load(
				std::memory_order_acquire);
			if (capture_frame == 0 && stage_index == 0 && observation.record_count != 0)
			{
				auto expected = std::uint64_t{};
				(void)ownership_boundary_capture_frame.compare_exchange_strong(expected,
					frame_id, std::memory_order_acq_rel, std::memory_order_acquire);
				capture_frame = ownership_boundary_capture_frame.load(
					std::memory_order_acquire);
			}
			if (capture_frame != frame_id) return;

			auto& artifact = ownership_boundary_artifacts[
				(boundary_value - 1) * ownership_boundary_stage_count + stage_index];
			auto expected_state = std::uint32_t{};
			if (!artifact.state.compare_exchange_strong(expected_state, 1,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return;
			}
			artifact.observation = observation;
			artifact.frontend_frame_id = frame_id;
			artifact.capture_tick = GetTickCount64();
			artifact.thread_id = GetCurrentThreadId();
			artifact.state.store(2, std::memory_order_release);
		}

		bool validate_readable_data_range(const std::uintptr_t begin,
			const std::size_t size) noexcept
		{
			if (begin == 0 || size == 0 || begin >
				(std::numeric_limits<std::uintptr_t>::max)() - size)
			{
				return false;
			}
			const auto end = begin + size;
			auto current = begin;
			while (current < end)
			{
				MEMORY_BASIC_INFORMATION memory{};
				if (VirtualQuery(reinterpret_cast<const void*>(current), &memory,
					sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
					(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
				{
					return false;
				}
				switch (memory.Protect & 0xFF)
				{
				case PAGE_READONLY:
				case PAGE_READWRITE:
				case PAGE_WRITECOPY:
				case PAGE_EXECUTE_READ:
				case PAGE_EXECUTE_READWRITE:
				case PAGE_EXECUTE_WRITECOPY:
					break;
				default:
					return false;
				}
				const auto region_begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
				if (memory.RegionSize == 0 || region_begin >
					(std::numeric_limits<std::uintptr_t>::max)() - memory.RegionSize)
				{
					return false;
				}
				const auto region_end = region_begin + memory.RegionSize;
				if (region_end <= current) return false;
				current = (std::min)(region_end, end);
			}
			return true;
		}

		bool validate_writable_data_range(const std::uintptr_t begin,
			const std::size_t size) noexcept
		{
			if (begin == 0 || size == 0 || begin >
				(std::numeric_limits<std::uintptr_t>::max)() - size)
			{
				return false;
			}
			const auto end = begin + size;
			auto current = begin;
			while (current < end)
			{
				MEMORY_BASIC_INFORMATION memory{};
				if (VirtualQuery(reinterpret_cast<const void*>(current), &memory,
					sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
					(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
				{
					return false;
				}
				switch (memory.Protect & 0xFF)
				{
				case PAGE_READWRITE:
				case PAGE_WRITECOPY:
				case PAGE_EXECUTE_READWRITE:
				case PAGE_EXECUTE_WRITECOPY:
					break;
				default:
					return false;
				}
				const auto region_begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
				if (memory.RegionSize == 0 || region_begin >
					(std::numeric_limits<std::uintptr_t>::max)() - memory.RegionSize)
				{
					return false;
				}
				const auto region_end = region_begin + memory.RegionSize;
				if (region_end <= current) return false;
				current = (std::min)(region_end, end);
			}
			return true;
		}

		template <std::size_t Size>
		bool capture_artifact(one_shot_artifact<Size>& artifact,
			const std::uintptr_t source,
			const std::array<std::uint64_t, evidence_metadata_count>& metadata) noexcept
		{
			// A completed one-shot must not keep issuing VirtualQuery on every frame.
			if (!view_diagnostics_enabled() ||
				artifact.state.load(std::memory_order_acquire) != 0 ||
				!validate_readable_data_range(source, Size)) return false;
			std::uint32_t expected{};
			if (!artifact.state.compare_exchange_strong(expected, 1,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return false;
			}
			artifact.metadata = metadata;
			std::memcpy(artifact.bytes.data(), reinterpret_cast<const void*>(source), Size);
			artifact.state.store(2, std::memory_order_release);
			return true;
		}

		template <std::size_t Size>
		[[nodiscard]] bool artifact_ready(const one_shot_artifact<Size>& artifact) noexcept
		{
			return artifact.state.load(std::memory_order_acquire) == 2;
		}

		template <std::size_t Size>
		bool write_artifact(one_shot_artifact<Size>& artifact,
			const char* const filename) noexcept
		{
			if (!artifact_ready(artifact)) return false;
			if (artifact.written.load(std::memory_order_acquire)) return true;
			const std::string bytes(reinterpret_cast<const char*>(artifact.bytes.data()), Size);
			std::ostringstream path;
			path << evidence_directory << '/' << filename;
			const auto written = utils::io::write_file_atomic(path.str(), bytes);
			if (written) artifact.written.store(true, std::memory_order_release);
			return written;
		}

		template <std::size_t Size>
		void append_artifact_manifest(std::ostringstream& output, const char* const name,
			const char* const filename, const one_shot_artifact<Size>& artifact)
		{
			const auto state = artifact.state.load(std::memory_order_acquire);
			const auto ready = state == 2;
			output << "artifact=" << name
				<< " state=" << (ready ? "ready" : (state == 1 ? "capturing" : "empty"))
				<< " written=" << (artifact.written.load(std::memory_order_acquire)
					? "yes" : "no")
				<< " size=" << Size << " file=" << evidence_directory << '/' << filename;
			if (ready)
			{
				const std::string bytes(reinterpret_cast<const char*>(artifact.bytes.data()), Size);
				output << " sha256=" << utils::cryptography::sha256::compute(bytes, true);
				for (std::size_t index{}; index < artifact.metadata.size(); ++index)
				{
					output << " m" << index << "=0x" << std::hex
						<< artifact.metadata[index] << std::dec;
				}
			}
			output << "\r\n";
		}

		template <std::size_t Size>
		void append_delta_manifest(std::ostringstream& output, const char* const name,
			const one_shot_artifact<Size>& before,
			const one_shot_artifact<Size>& after)
		{
			if (!artifact_ready(before) || !artifact_ready(after)) return;
			std::size_t changed{};
			std::size_t first = Size;
			std::size_t last{};
			for (std::size_t index{}; index < Size; ++index)
			{
				if (before.bytes[index] == after.bytes[index]) continue;
				if (changed++ == 0) first = index;
				last = index;
			}
			output << "delta=" << name << " changed_bytes=" << changed;
			if (changed != 0)
			{
				output << " first=0x" << std::hex << first << " last=0x" << last
					<< std::dec;
			}
			output << "\r\n";
		}

		std::string format_target_registry_artifact(const char* const label,
			const one_shot_artifact<native_render_contract::target_registry_size>& artifact)
		{
			if (!artifact_ready(artifact)) return {};
			constexpr auto qword_0_offset = std::size_t{0x00};
			constexpr auto qword_1_offset = std::size_t{0x08};
			constexpr auto qword_2_offset = std::size_t{0x10};
			constexpr auto qword_3_offset = std::size_t{0x18};
			constexpr auto width_offset = std::size_t{0x30};
			constexpr auto height_offset = std::size_t{0x32};
			constexpr auto related_offset = std::size_t{0x38};
			std::ostringstream entries;
			std::uint32_t pointer_occupied{};
			std::uint32_t dimensioned{};
			for (std::uint32_t id{}; id < native_render_contract::target_registry_capacity; ++id)
			{
				const auto* const entry = artifact.bytes.data() +
					static_cast<std::size_t>(id) * native_render_contract::target_registry_stride;
				std::uintptr_t qword_0{};
				std::uintptr_t qword_1{};
				std::uintptr_t qword_2{};
				std::uintptr_t qword_3{};
				std::uint16_t width{};
				std::uint16_t height{};
				std::uint32_t related{};
				std::memcpy(&qword_0, entry + qword_0_offset, sizeof(qword_0));
				std::memcpy(&qword_1, entry + qword_1_offset, sizeof(qword_1));
				std::memcpy(&qword_2, entry + qword_2_offset, sizeof(qword_2));
				std::memcpy(&qword_3, entry + qword_3_offset, sizeof(qword_3));
				std::memcpy(&width, entry + width_offset, sizeof(width));
				std::memcpy(&height, entry + height_offset, sizeof(height));
				std::memcpy(&related, entry + related_offset, sizeof(related));
				if (qword_0 != 0 || qword_1 != 0 || qword_2 != 0 || qword_3 != 0)
				{
					++pointer_occupied;
				}
				if (width != 0 || height != 0) ++dimensioned;
				entries << "id=" << id << " qword0=0x" << std::hex << qword_0
					<< " qword1=0x" << qword_1 << " qword2=0x" << qword_2
					<< " qword3=0x" << qword_3 << std::dec
					<< " size=" << width << 'x' << height
					<< " related=" << related << "\r\n";
			}
			std::ostringstream output;
			output << "H2 target registry CPU snapshot\r\n"
				<< "label=" << label << "\r\n"
				<< "base=0x" << std::hex << native_render_contract::target_registry_base
				<< std::dec << " stride=" << native_render_contract::target_registry_stride
				<< " capacity=" << native_render_contract::target_registry_capacity
				<< " pointer_occupied=" << pointer_occupied
				<< " dimensioned=" << dimensioned << "\r\n"
				<< "proven_binding_semantics=qword1_dsv; qword0_or_qword2_single_rtv; qword3_not_consumed_by_0x140781CA0\r\n"
				<< "classification=observation_only; zero fields do not prove a slot is free\r\n"
				<< entries.str();
			return output.str();
		}

		bool write_evidence_text_once(std::atomic_bool& written,
			const char* const filename, const std::string& value) noexcept
		{
			if (written.load(std::memory_order_acquire)) return true;
			if (value.empty()) return false;
			const auto success = utils::io::write_file_atomic(
				std::string{evidence_directory} + '/' + filename, value);
			if (success) written.store(true, std::memory_order_release);
			return success;
		}

		bool write_evidence_text_versioned(std::atomic_uint64_t& written_signature,
			const char* const filename, const std::string& value,
			const std::uint64_t signature) noexcept
		{
			if (value.empty() || signature == 0) return false;
			if (written_signature.load(std::memory_order_acquire) == signature) return true;
			const auto success = utils::io::write_file_atomic(
				std::string{evidence_directory} + '/' + filename, value);
			if (success) written_signature.store(signature, std::memory_order_release);
			return success;
		}

		std::string format_backend_target_route(
			const engine_stereo_backend_target::report& report,
			const engine_stereo_backend_target::status& status)
		{
			std::ostringstream output;
			output << "H2 backend target-route evidence\r\n"
				<< "state=" << engine_stereo_backend_target::to_string(status.state) << "\r\n"
				<< "contract=one_shot_cpu_observation_only\r\n"
				<< "registry_writes=0\r\nd3d_calls=0\r\nopenvr_calls=0\r\n"
				<< "publication_sequence=" << report.publication_sequence
				<< " record=0x" << std::hex << report.record << std::dec << "\r\n"
				<< "select_calls=" << status.select_calls
				<< " dispatch_select_calls=" << status.dispatch_select_calls
				<< " applied_transitions=" << status.applied_transitions
				<< " retained_targets=" << status.retained_targets
				<< " unique_targets=" << status.unique_targets
				<< " invalid_observations=" << status.invalid_observations
				<< " application_mismatches=" << status.application_mismatches
				<< " entry_mutations=" << status.entry_mutations
				<< " route_overflows=" << status.route_overflows << "\r\n";
			for (std::size_t index{}; index < report.event_count; ++index)
			{
				const auto& event = report.events[index];
				output << "event=" << index
					<< " phase=" << engine_stereo_backend_target::to_string(event.phase)
					<< " caller=0x" << std::hex << event.caller
					<< " caller_rva=0x" << (event.caller >= 0x140000000
						? event.caller - 0x140000000 : event.caller)
					<< " context=0x" << event.context << std::dec
					<< " backend_state=0x" << event.backend_state << std::dec
					<< " target=" << event.target_id
					<< " current_before=" << event.current_before
					<< " current_after=" << event.current_after
					<< " entry_stable=" << (event.entry_stable ? "yes" : "no")
					<< "\r\n";
			}
			for (std::uint32_t id{}; id < report.targets.size(); ++id)
			{
				const auto& target = report.targets[id];
				if (!target.seen) continue;
				std::array<std::uintptr_t, 4> pointers{};
				std::uint16_t width{};
				std::uint16_t height{};
				std::uint32_t related{};
				std::memcpy(pointers.data(), target.bytes.data(),
					pointers.size() * sizeof(std::uintptr_t));
				std::memcpy(&width, target.bytes.data() + 0x30, sizeof(width));
				std::memcpy(&height, target.bytes.data() + 0x32, sizeof(height));
				std::memcpy(&related, target.bytes.data() + 0x38, sizeof(related));
				output << "target=" << id << " qword0=0x" << std::hex << pointers[0]
					<< " qword1=0x" << pointers[1]
					<< " qword2=0x" << pointers[2]
					<< " qword3=0x" << pointers[3] << std::dec
					<< " size=" << width << 'x' << height
					<< " related=" << related << "\r\n";
			}
			return output.str();
		}

		std::string format_backend_target_frame(
			const engine_stereo_backend_target::frame_report& report,
			const engine_stereo_backend_target::frame_status& status)
		{
			std::ostringstream output;
			output << "H2 backend target frame-route evidence\r\n"
				<< "state=" << engine_stereo_backend_target::to_string(status.state) << "\r\n"
				<< "contract=one_shot_claim_to_following_present_pre_cpu_observation\r\n"
				<< "registry_writes=0\r\nd3d_calls=0\r\nopenvr_calls=0\r\n"
				<< "registry_semantics=qword1_dsv; qword0_or_qword2_single_rtv; qword3_not_consumed_by_0x140781CA0\r\n"
				<< "publication_sequence=" << report.start_publication_sequence
				<< " record=0x" << std::hex << report.start_record << std::dec
				<< " present_post=" << report.start_present_post_frame
				<< " present_post_thread=" << report.start_present_post_thread_id
				<< " present_pre=" << report.end_present_pre_frame
				<< " generation=" << report.device_generation
				<< " boundary_thread=" << report.boundary_thread_id << "\r\n"
				<< "select_calls=" << status.select_calls
				<< " view_copy_events=" << status.view_copy_events
				<< " events=" << report.event_count
				<< " report_view_copy_events=" << report.view_copy_event_count
				<< " unique_targets=" << report.unique_target_count
				<< " invalid_observations=" << status.invalid_observations
				<< " application_mismatches=" << status.application_mismatches
				<< " entry_mutations=" << status.entry_mutations
				<< " route_overflows=" << status.route_overflows
				<< " generation_mismatches=" << status.device_generation_mismatches
				<< " max_concurrent_writers=" << status.maximum_active_writers << "\r\n";
			for (std::size_t index{}; index < report.event_count; ++index)
			{
				const auto& event = report.events[index];
				output << "event=" << index
					<< " sequence=" << event.sequence
					<< " kind=" << engine_stereo_backend_target::to_string(event.kind)
					<< " qpc=" << event.timestamp_qpc
					<< " present_post=" << event.latest_present_post_frame
					<< " thread=" << event.thread_id
					<< " phase=" << engine_stereo_backend_target::to_string(event.phase)
					<< " caller=0x" << std::hex << event.caller
					<< " caller_rva=0x" << (event.caller >= 0x140000000
						? event.caller - 0x140000000 : event.caller)
					<< " record=0x" << event.record << std::dec
					<< " backend_id=" << event.backend_id
					<< " publication=" << event.publication_sequence
					<< " record_type=" << event.record_type
					<< " backend_active=" << (event.backend_active ? "yes" : "no")
					<< " binding_active=" << (event.binding_active ? "yes" : "no");
				if (event.kind == engine_stereo_backend_target::frame_event_kind::view_copy)
				{
					output << " copy_ordinal=" << event.view_copy_ordinal
						<< " substitution_ordinal="
						<< event.view_substitution_ordinal
						<< " selected_eye=" << event.selected_eye
						<< " source_matched="
						<< (event.view_source_matched ? "yes" : "no") << "\r\n";
					continue;
				}
				std::array<std::uintptr_t, 4> qwords{};
				std::uint16_t width{};
				std::uint16_t height{};
				std::uint32_t related{};
				if (event.entry_valid)
				{
					std::memcpy(qwords.data(), event.target_entry.data(),
						qwords.size() * sizeof(std::uintptr_t));
					std::memcpy(&width, event.target_entry.data() + 0x30, sizeof(width));
					std::memcpy(&height, event.target_entry.data() + 0x32, sizeof(height));
					std::memcpy(&related, event.target_entry.data() + 0x38, sizeof(related));
				}
				output << " context=0x" << std::hex << event.context
					<< " backend_state=0x" << event.backend_state
					<< std::dec << " target=" << event.target_id
					<< " current_before=" << event.current_before
					<< " current_after=" << event.current_after
					<< " entry_valid=" << (event.entry_valid ? "yes" : "no")
					<< " entry_stable=" << (event.entry_stable ? "yes" : "no")
					<< " application_matches="
					<< (event.application_matches ? "yes" : "no")
					<< " qword0=0x" << std::hex << qwords[0]
					<< " qword1=0x" << qwords[1]
					<< " qword2=0x" << qwords[2]
					<< " qword3=0x" << qwords[3] << std::dec
					<< " size=" << width << 'x' << height
					<< " related=" << related << "\r\n";
			}
			for (std::uint32_t id{}; id < report.targets.size(); ++id)
			{
				const auto& target = report.targets[id];
				if (!target.seen) continue;
				std::array<std::uintptr_t, 4> qwords{};
				std::uint16_t width{};
				std::uint16_t height{};
				std::uint32_t related{};
				std::memcpy(qwords.data(), target.bytes.data(),
					qwords.size() * sizeof(std::uintptr_t));
				std::memcpy(&width, target.bytes.data() + 0x30, sizeof(width));
				std::memcpy(&height, target.bytes.data() + 0x32, sizeof(height));
				std::memcpy(&related, target.bytes.data() + 0x38, sizeof(related));
				output << "target=" << id
					<< " qword0=0x" << std::hex << qwords[0]
					<< " qword1=0x" << qwords[1]
					<< " qword2=0x" << qwords[2]
					<< " qword3=0x" << qwords[3] << std::dec
					<< " size=" << width << 'x' << height
					<< " related=" << related << "\r\n";
			}
			return output.str();
		}

		void append_output_view(std::ostringstream& output, const char* const label,
			const std::size_t index,
			const engine_stereo_output_merger::view_observation& view)
		{
			std::string prefix{label};
			if (index != (std::numeric_limits<std::size_t>::max)())
			{
				prefix += std::to_string(index);
			}
			output << ' ' << prefix << "_view=0x" << std::hex << view.view
				<< ' ' << prefix << "_resource=0x" << view.resource << std::dec
				<< ' ' << prefix << "_valid=" << (view.valid ? "yes" : "no")
				<< ' ' << prefix << "_view_format="
				<< static_cast<std::uint32_t>(view.view_format)
				<< ' ' << prefix << "_view_dimension=" << view.view_dimension
				<< ' ' << prefix << "_resource_dimension="
				<< static_cast<std::uint32_t>(view.resource_dimension)
				<< ' ' << prefix << "_size=" << view.width << 'x' << view.height
				<< ' ' << prefix << "_resource_format="
				<< static_cast<std::uint32_t>(view.resource_format)
				<< ' ' << prefix << "_mips=" << view.mip_levels
				<< ' ' << prefix << "_array=" << view.array_size
				<< ' ' << prefix << "_samples=" << view.sample_count << ':'
				<< view.sample_quality
				<< ' ' << prefix << "_usage=" << static_cast<std::uint32_t>(view.usage)
				<< ' ' << prefix << "_bind=0x" << std::hex << view.bind_flags
				<< ' ' << prefix << "_cpu=0x" << view.cpu_access_flags
				<< ' ' << prefix << "_misc=0x" << view.misc_flags << std::dec;
		}

		std::string format_backend_output_merger(
			const engine_stereo_output_merger::report& report,
			const engine_stereo_output_merger::status& status)
		{
			std::ostringstream output;
			output << "H2 backend D3D11 output-merger evidence\r\n"
				<< "state=" << engine_stereo_output_merger::to_string(status.state) << "\r\n"
				<< "contract=one_shot_exact_backend_transaction_read_only_om_observation\r\n"
				<< "additional_gpu_commands=0\r\ntarget_replacements=0\r\n"
				<< "command_replays=0\r\nopenvr_calls=0\r\n"
				<< "metadata_queries=" << status.metadata_queries << "\r\n"
				<< "publication_sequence=" << report.publication_sequence
				<< " record=0x" << std::hex << report.record
				<< " expected_context=0x" << report.expected_context << std::dec
				<< " generation=" << report.device_generation
				<< " transaction_thread=" << report.transaction_thread_id << "\r\n"
				<< "bind_calls=" << status.bind_calls
				<< " nonnull_bind_calls=" << status.nonnull_bind_calls
				<< " events=" << report.event_count
				<< " context_mismatches=" << status.context_mismatches
				<< " thread_mismatches=" << status.thread_mismatches
				<< " query_failures=" << status.query_failures
				<< " overflows=" << status.overflows << "\r\n";
			for (std::size_t event_index{}; event_index < report.event_count; ++event_index)
			{
				const auto& event = report.events[event_index];
				output << "event=" << event_index
					<< " sequence=" << event.sequence
					<< " qpc=" << event.timestamp_qpc
					<< " thread=" << event.thread_id
					<< " phase=" << engine_stereo_output_merger::to_string(
						event.execution_phase)
					<< " caller=0x" << std::hex << event.caller
					<< " caller_rva=0x" << (event.caller >= 0x140000000
						? event.caller - 0x140000000 : event.caller)
					<< " context=0x" << event.context << std::dec
					<< " expected_context=" << (event.expected_context ? "yes" : "no")
					<< " target=" << event.latest_target_id
					<< " copy_ordinal=" << event.view_copy_ordinal
					<< " substitution_ordinal=" << event.view_substitution_ordinal
					<< " selected_eye=" << event.selected_eye
					<< " render_target_count=" << event.render_target_count;
				const auto target_count = (std::min)(
					static_cast<std::size_t>(event.render_target_count),
					event.render_targets.size());
				for (std::size_t target_index{}; target_index < target_count; ++target_index)
				{
					append_output_view(output, "rtv", target_index,
						event.render_targets[target_index]);
				}
				append_output_view(output, "dsv",
					(std::numeric_limits<std::size_t>::max)(), event.depth_stencil);
				output << "\r\n";
			}
			return output.str();
		}

		std::string format_backend_draw_indexed(
			const engine_stereo_draw_indexed::report& report,
			const engine_stereo_draw_indexed::status& status)
		{
			std::ostringstream output;
			output << "H2 backend DrawIndexed replayability evidence\r\n"
				<< "state=" << engine_stereo_draw_indexed::to_string(status.state) << "\r\n"
				<< "contract=one_shot_exact_dispatch_read_only_draw_indexed_observation\r\n"
				<< "additional_gpu_commands=0\r\ntarget_replacements=0\r\n"
				<< "command_replays=0\r\nopenvr_calls=0\r\n"
				<< "publication_sequence=" << report.publication_sequence
				<< " record=0x" << std::hex << report.record
				<< " expected_context=0x" << report.expected_context << std::dec
				<< " generation=" << report.device_generation
				<< " transaction_thread=" << report.transaction_thread_id << "\r\n"
				<< "command_before_address=0x" << std::hex << report.before.address
				<< " command_before_hash=0x" << report.before.hash << std::dec
				<< " command_before_bytes=" << report.before.byte_count
				<< " command_before_records=" << report.before.record_count
				<< " command_before_valid=" << (report.before.valid ? "yes" : "no") << "\r\n"
				<< "command_after_address=0x" << std::hex << report.after.address
				<< " command_after_hash=0x" << report.after.hash << std::dec
				<< " command_after_bytes=" << report.after.byte_count
				<< " command_after_records=" << report.after.record_count
				<< " command_after_valid=" << (report.after.valid ? "yes" : "no") << "\r\n"
				<< "command_stream_immutable=" << (report.before.valid && report.after.valid &&
					report.before.address == report.after.address &&
					report.before.byte_count == report.after.byte_count &&
					report.before.record_count == report.after.record_count &&
					report.before.hash == report.after.hash ? "yes" : "no") << "\r\n"
				<< "draw_calls=" << status.draw_calls
				<< " events=" << report.event_count
				<< " draw_call_hash=0x" << std::hex << report.draw_call_hash << std::dec
				<< " context_mismatches=" << status.context_mismatches
				<< " thread_mismatches=" << status.thread_mismatches
				<< " invalid_arguments=" << status.invalid_arguments
				<< " overflows=" << status.overflows << "\r\n";
			for (std::size_t event_index{}; event_index < report.event_count; ++event_index)
			{
				const auto& event = report.events[event_index];
				output << "event=" << event_index
					<< " sequence=" << event.sequence
					<< " qpc=" << event.timestamp_qpc
					<< " thread=" << event.thread_id
					<< " caller=0x" << std::hex << event.caller
					<< " caller_rva=0x" << (event.caller >= 0x140000000
						? event.caller - 0x140000000 : event.caller)
					<< " context=0x" << event.context << std::dec
					<< " expected_context=" << (event.expected_context ? "yes" : "no")
					<< " arguments_valid=" << (event.arguments_valid ? "yes" : "no")
					<< " index_count=" << event.index_count
					<< " start_index=" << event.start_index_location
					<< " base_vertex=" << event.base_vertex_location << "\r\n";
			}
			output << "boundary_draw_calls=" << report.boundary_draw_calls
				<< " boundary_groups=" << report.boundary_group_count
				<< " boundary_group_overflows=" << report.boundary_group_overflows
				<< "\r\n";
			for (std::size_t group_index{};
				group_index < report.boundary_group_count; ++group_index)
			{
				const auto& group = report.boundary_groups[group_index];
				output << "boundary_group=" << group_index
					<< " phase=" << engine_stereo_draw_indexed::to_string(
						group.execution_phase)
					<< " binding_sequence=" << group.binding_sequence
					<< " target=" << group.target_id
					<< " render_target_count=" << group.render_target_count
					<< " rtv0=0x" << std::hex << group.render_target
					<< " dsv=0x" << group.depth_stencil << std::dec
					<< " draws=" << group.draw_calls
					<< " indices=" << group.index_count
					<< " hash=0x" << std::hex << group.draw_call_hash
					<< " first_caller_rva=0x"
					<< (group.first_caller >= 0x140000000
						? group.first_caller - 0x140000000 : group.first_caller)
					<< " last_caller_rva=0x"
					<< (group.last_caller >= 0x140000000
						? group.last_caller - 0x140000000 : group.last_caller)
					<< std::dec << " first_start_index=" << group.first_start_index
					<< " last_start_index=" << group.last_start_index
					<< " first_base_vertex=" << group.first_base_vertex
					<< " last_base_vertex=" << group.last_base_vertex << "\r\n";
			}
			return output.str();
		}

		const char* execution_scope_name(const std::uint8_t flags) noexcept
		{
			if ((flags & engine_stereo_execution::classifier_scope_flag) != 0)
			{
				return "classifier";
			}
			if ((flags & engine_stereo_execution::frame_scope_flag) != 0)
			{
				return "frame";
			}
			return "none";
		}

		std::string format_backend_execution(
			const engine_stereo_execution::report& report,
			const engine_stereo_execution::status& status)
		{
			std::ostringstream output;
			output << "H2 D3D11 execution census\r\n"
				<< "state=" << engine_stereo_execution::to_string(status.state) << "\r\n"
				<< "error=" << engine_stereo_execution::to_string(status.error) << "\r\n"
				<< "contract=exact_type4_classifier_plus_next_present_to_present_frame_read_only\r\n"
				<< "scene_owner_contract=outer_current_record_0x7a7da0_to_0x7a8306_cpu_lifetime\r\n"
				<< "additional_gpu_commands=0\r\ntarget_replacements=0\r\n"
				<< "command_replays=0\r\nopenvr_calls=0\r\n"
				<< "expected_context=0x" << std::hex << report.expected_context << std::dec
				<< " generation=" << report.device_generation
				<< " hooks_installed=" << (status.hooks_installed ? "yes" : "no")
				<< " installed_hook_count=" << status.installed_hook_count
				<< " targets_distinct=" << (status.targets_distinct ? "yes" : "no")
				<< " permanent_install_failure="
				<< (status.installation_permanently_failed ? "yes" : "no")
				<< " draw_indexed_forwarding_external="
				<< (status.draw_indexed_forwarding_external ? "yes" : "no")
				<< " draw_indexed_forwarded_calls="
				<< status.draw_indexed_forwarded_calls
				<< " hook_failures=" << status.hook_failures
				<< " om_target=0x" << std::hex << status.output_merger_target
				<< " om_uav_target=0x" << status.output_merger_unordered_access_target
				<< " clear_state_target=0x" << status.clear_state_target
				<< std::dec << "\r\n"
				<< "classifier_record=0x" << std::hex << report.classifier_record << std::dec
				<< " classifier_type=" << report.classifier_record_type
				<< " classifier_thread=" << report.classifier_thread_id
				<< " classifier_qpc=" << report.classifier_begin_qpc << "->"
				<< report.classifier_end_qpc << "\r\n"
				<< "frame_present=" << report.frame_start_present_post << "->"
				<< report.frame_end_present_pre << "->" << report.frame_end_present_post
				<< " result=0x" << std::hex
				<< static_cast<std::uint32_t>(report.frame_present_result) << std::dec
				<< " frame_threads=" << report.frame_start_thread_id << "->"
				<< report.frame_end_thread_id << "\r\n"
				<< "event_reservations=" << status.event_reservations
				<< " events=" << report.event_count
				<< " classifier_events=" << report.classifier_event_count
				<< " frame_events=" << report.frame_event_count
				<< " expected_context_frame_events="
				<< report.expected_context_frame_events
				<< " backend_scoped_events=" << report.backend_scoped_events
				<< " backend_scoped_frame_events="
				<< report.backend_scoped_frame_events
				<< " backend_unscoped_frame_events="
				<< report.backend_unscoped_frame_events
				<< " backend_thread_mismatches="
				<< report.backend_thread_mismatches
				<< " distinct_backend_records="
				<< report.distinct_backend_records
				<< " scene_owner_scoped_events="
				<< report.scene_owner_scoped_events
				<< " scene_owner_scoped_frame_events="
				<< report.scene_owner_scoped_frame_events
				<< " scene_owner_unscoped_frame_events="
				<< report.scene_owner_unscoped_frame_events
				<< " scene_owner_thread_mismatches="
				<< report.scene_owner_thread_mismatches
				<< " distinct_scene_owner_records="
				<< report.distinct_scene_owner_records
				<< " call_stack_samples=" << report.call_stack_samples
				<< " call_stack_capture_failures="
				<< report.call_stack_capture_failures
				<< " call_stack_key_overflows="
				<< report.call_stack_key_overflows
				<< " admission_collisions=" << report.admission_collisions
				<< " overflows=" << report.overflow_count
				<< " foreign_context_events=" << report.foreign_context_events
				<< " classifier_thread_mismatches="
				<< report.classifier_thread_mismatches
				<< " distinct_contexts=" << report.distinct_contexts
				<< " distinct_threads=" << report.distinct_threads
				<< " execute_command_lists=" << report.opaque_execute_command_lists
				<< " known_conversion_recordings=" << report.known_conversion_recordings
				<< " known_conversion_executions=" << report.known_conversion_executions
				<< " known_conversion_replays=" << report.known_conversion_replays
				<< " deferred_execution_opaque="
				<< (report.deferred_execution_opaque ? "yes" : "no")
				<< " identity_truncated=" << (report.identity_truncated ? "yes" : "no")
				<< " max_concurrent_writers=" << status.maximum_active_writers << "\r\n";
			for (std::size_t index{}; index < engine_stereo_execution::api_count; ++index)
			{
				const auto operation = static_cast<engine_stereo_execution::api>(index);
				output << "api=" << engine_stereo_execution::to_string(operation)
					<< " hook_target=0x" << std::hex << status.hook_targets[index]
					<< std::dec << " total=" << report.per_api[index]
					<< " classifier=" << report.classifier_per_api[index]
					<< " frame=" << report.frame_per_api[index] << "\r\n";
			}
			for (std::uint32_t event_index{}; event_index < report.event_count;
				++event_index)
			{
				const auto& event = report.events[event_index];
				output << "event=" << event_index
					<< " sequence=" << event.sequence
					<< " scope=" << execution_scope_name(event.scope_flags)
					<< " api=" << engine_stereo_execution::to_string(event.operation)
					<< " qpc=" << event.timestamp_qpc
					<< " thread=" << event.thread_id
					<< " caller=0x" << std::hex << event.caller;
				constexpr auto h2_image_base = std::uintptr_t{0x140000000};
				constexpr auto h2_image_end = std::uintptr_t{0x151E6BC00};
				if (event.caller >= h2_image_base && event.caller < h2_image_end)
				{
					output << " caller_rva=0x" << (event.caller - h2_image_base);
				}
				else
				{
					output << " caller_rva=external";
				}
				output << " context=0x" << event.context << std::dec
					<< " expected_context=" << (event.expected_context ? "yes" : "no")
					<< " argument_count=" << static_cast<std::uint32_t>(event.argument_count);
				for (std::size_t argument_index{};
					argument_index < event.argument_count &&
					argument_index < event.arguments.size(); ++argument_index)
				{
					output << " arg" << argument_index << "=0x" << std::hex
						<< event.arguments[argument_index] << std::dec;
				}
				output << " binding_valid="
					<< (event.output_binding.valid ? "yes" : "no")
					<< " binding_sequence=" << event.output_binding.sequence
					<< " target=" << event.output_binding.target_id
					<< " rtv_count=" << event.output_binding.render_target_count
					<< " rtv0=0x" << std::hex
					<< event.output_binding.render_target_0
					<< " dsv=0x" << event.output_binding.depth_stencil
					<< std::dec
					<< " backend_active=" << (event.backend ? "yes" : "no")
					<< " backend_id=" << event.backend.backend_id
					<< " backend_record=0x" << std::hex << event.backend.record
					<< " backend_frontend=0x" << event.backend.frontend
					<< " backend_commands=0x" << event.backend.command_stream
					<< std::dec
					<< " backend_record_index=" << event.backend.record_index
					<< " backend_record_type=" << event.backend.record_type
					<< " backend_target=" << event.backend.target_id
					<< " backend_owner_thread=" << event.backend.owner_thread_id
					<< " backend_thread_match="
					<< (event.backend_thread_match ? "yes" : "no")
					<< " backend_dispatch_entered="
					<< (event.backend.dispatch_entered ? "yes" : "no")
					<< " frontend_epoch=" << event.backend.frontend_epoch
					<< " frontend_transaction="
					<< event.backend.frontend_transaction_id
					<< " scene_owner_active="
					<< (event.scene_owner ? "yes" : "no")
					<< " scene_owner_id=" << event.scene_owner.observation_id
					<< " scene_owner_record=0x" << std::hex
					<< event.scene_owner.record
					<< " scene_owner_frontend=0x" << event.scene_owner.frontend
					<< " scene_owner_caller=0x" << event.scene_owner.caller
					<< std::dec
					<< " scene_owner_record_index=" << event.scene_owner.record_index
					<< " scene_owner_record_type=" << event.scene_owner.record_type
					<< " scene_owner_targets=" << event.scene_owner.target_ids[0]
					<< ',' << event.scene_owner.target_ids[1]
					<< ',' << event.scene_owner.target_ids[2]
					<< " scene_owner_selector=" << event.scene_owner.target_selector
					<< " scene_owner_thread="
					<< event.scene_owner.owner_thread_id
					<< " scene_owner_thread_match="
					<< (event.scene_owner_thread_match ? "yes" : "no")
					<< " scene_owner_record_valid="
					<< (event.scene_owner.record_valid ? "yes" : "no")
					<< " stack_depth="
					<< static_cast<std::uint32_t>(event.call_stack_depth);
				for (std::size_t stack_index{};
					stack_index < event.call_stack_depth &&
					stack_index < event.call_stack.size(); ++stack_index)
				{
					output << " stack" << stack_index << "=0x" << std::hex
						<< event.call_stack[stack_index] << std::dec;
				}
				output << "\r\n";
			}
			return output.str();
		}

		std::string format_backend_stereo_replay(
			const engine_stereo_replay::report& report,
			const engine_stereo_replay::status& status)
		{
			std::ostringstream output;
			output << "H2 same-device stereo replay evidence\r\n"
				<< "state=" << engine_stereo_replay::to_string(status.state) << "\r\n"
				<< "error=" << engine_stereo_replay::to_string(report.error) << "\r\n"
				<< "contract=one_shot_same_device_nonshared_exact_command_replay\r\n"
				<< "fallbacks=0\r\nopenvr_calls=0\r\nshared_resources=0\r\n"
				<< "publication_sequence=" << report.publication_sequence
				<< " record=0x" << std::hex << report.record
				<< " context=0x" << report.context << std::dec
				<< " generation=" << report.device_generation
				<< " owner_thread=" << report.owner_thread_id << "\r\n"
				<< "target=" << report.width << "x" << report.height
				<< " format=" << report.format << "\r\n"
				<< "natural_copy_calls=" << report.natural_copy_calls
				<< " natural_left_substitutions=" << report.natural_left_substitutions
				<< " replay_view_substitutions=" << report.replay_view_substitutions
				<< " replay_dispatches=" << report.replay_dispatches
				<< " natural_draw_calls=" << report.natural_draw_calls
				<< " replay_draw_calls=" << report.replay_draw_calls
				<< " replay_output_bind_calls=" << report.replay_output_bind_calls
				<< " target_replacements=0\r\n"
				<< "natural_draw_hash=0x" << std::hex << report.natural_draw_hash
				<< " replay_draw_hash=0x" << report.replay_draw_hash << std::dec
				<< " replay_draw_matches=" << (report.replay_draw_matches ? "yes" : "no")
				<< " exact_draw_matches="
				<< (report.draw_comparison.exact_matches ? "yes" : "no")
				<< " dynamic_index_append_matches="
				<< (report.draw_comparison.dynamic_index_append_matches ? "yes" : "no")
				<< " start_index_delta=" << report.draw_comparison.start_index_delta
				<< " natural_index_count=" << report.draw_comparison.natural_index_count
				<< " natural_shape_hash=0x" << std::hex
				<< report.natural_draw_trace.draw_shape_hash
				<< " replay_shape_hash=0x" << report.replay_draw_trace.draw_shape_hash
				<< std::dec << " draw_shape_matches="
				<< (report.natural_draw_trace.draw_calls ==
					report.replay_draw_trace.draw_calls &&
					report.natural_draw_trace.draw_shape_hash ==
					report.replay_draw_trace.draw_shape_hash ? "yes" : "no")
				<< " command_before_hash=0x" << std::hex << report.command_before.hash
				<< " command_after_hash=0x" << report.command_after.hash << std::dec
				<< " command_bytes=" << report.command_before.byte_count
				<< " command_records=" << report.command_before.record_count
				<< " command_immutable=" << (report.command_immutable ? "yes" : "no")
				<< "\r\n"
				<< "left_hash=0x" << std::hex << report.left_hash
				<< " clear_hash=0x" << report.clear_hash
				<< " right_hash=0x" << report.right_hash << std::dec
				<< " left_nonzero_bytes=" << report.left_nonzero_bytes
				<< " clear_nonzero_bytes=" << report.clear_nonzero_bytes
				<< " right_nonzero_bytes=" << report.right_nonzero_bytes << "\r\n"
				<< "right_changed_from_clear="
				<< (report.right_changed_from_clear ? "yes" : "no")
				<< " eyes_distinct=" << (report.eyes_distinct ? "yes" : "no")
				<< " output_restored=" << (report.output_restored ? "yes" : "no")
				<< " view_restored=" << (report.view_restored ? "yes" : "no") << "\r\n"
				<< "present=" << report.started_present << "->" << report.completed_present
				<< " readback_polls=" << report.readback_polls << "\r\n"
				<< "hresult swap_chain=0x" << std::hex
				<< static_cast<std::uint32_t>(report.swap_chain_buffer_result)
				<< " target=0x" << static_cast<std::uint32_t>(report.target_result)
				<< " target_view=0x" << static_cast<std::uint32_t>(report.target_view_result)
				<< " left_staging=0x" << static_cast<std::uint32_t>(report.left_staging_result)
				<< " clear_staging=0x" << static_cast<std::uint32_t>(report.clear_staging_result)
				<< " right_staging=0x" << static_cast<std::uint32_t>(report.right_staging_result)
				<< " query=0x" << static_cast<std::uint32_t>(report.query_result)
				<< " query_poll=0x" << static_cast<std::uint32_t>(report.query_poll_result)
				<< " left_map=0x" << static_cast<std::uint32_t>(report.left_map_result)
				<< " clear_map=0x" << static_cast<std::uint32_t>(report.clear_map_result)
				<< " right_map=0x" << static_cast<std::uint32_t>(report.right_map_result)
				<< std::dec << "\r\n";
			output << "natural_trace_events=" << report.natural_draw_trace.event_count
				<< " natural_trace_overflows=" << report.natural_draw_trace.overflows
				<< " replay_trace_events=" << report.replay_draw_trace.event_count
				<< " replay_trace_overflows=" << report.replay_draw_trace.overflows
				<< "\r\n";
			const auto trace_count = (std::max)(report.natural_draw_trace.event_count,
				report.replay_draw_trace.event_count);
			for (std::uint32_t index{}; index < trace_count; ++index)
			{
				output << "draw=" << index;
				if (index < report.natural_draw_trace.event_count)
				{
					const auto& event = report.natural_draw_trace.events[index];
					output << " natural_caller_rva=0x" << std::hex
						<< (event.caller >= 0x140000000
							? event.caller - 0x140000000 : event.caller)
						<< std::dec << " natural_index_count=" << event.index_count
						<< " natural_start_index=" << event.start_index_location
						<< " natural_base_vertex=" << event.base_vertex_location;
				}
				else
				{
					output << " natural=missing";
				}
				if (index < report.replay_draw_trace.event_count)
				{
					const auto& event = report.replay_draw_trace.events[index];
					output << " replay_caller_rva=0x" << std::hex
						<< (event.caller >= 0x140000000
							? event.caller - 0x140000000 : event.caller)
						<< std::dec << " replay_index_count=" << event.index_count
						<< " replay_start_index=" << event.start_index_location
						<< " replay_base_vertex=" << event.base_vertex_location;
				}
				else
				{
					output << " replay=missing";
				}
				output << "\r\n";
			}
			return output.str();
		}

		std::uint64_t hash_scene_descriptor_prefix(const void* const scene_descriptor) noexcept
		{
			if (!view_diagnostics_enabled() || scene_descriptor == nullptr) return 0;
			// game::refdef_t describes only the known 0x50-byte camera prefix. Runtime
			// disassembly proves R_RenderScene also reads fields beyond +0x50000; this
			// hash must never be treated as proof that the full descriptor is immutable.
			static_assert(std::is_trivially_copyable_v<game::refdef_t>);
			return hash_memory_region(reinterpret_cast<std::uintptr_t>(scene_descriptor),
				sizeof(game::refdef_t));
		}

		std::uintptr_t per_client_output_address(const std::int32_t local_client) noexcept
		{
			if (local_client < 0 || local_client >= observed_local_client_capacity) return 0;
			return per_client_view_output + static_cast<std::uintptr_t>(local_client) *
				view_slot_size;
		}

		engine_view_probe::view_state_observation capture_view_state(
			const engine_view_probe::view_state_source source,
			const std::uintptr_t subject, const std::uintptr_t view_slot,
			const std::uintptr_t output) noexcept
		{
			return {
				source,
				0,
				subject,
				hash_memory_region(view_slot, view_slot != 0 ? view_slot_size : 0),
				hash_memory_region(output, output != 0 ? view_slot_size : 0),
				hash_memory_region(scene_view_globals, scene_view_globals_size),
				hash_memory_region(camera_primary_globals, camera_globals_size),
				hash_memory_region(camera_secondary_globals, camera_globals_size),
				hash_memory_region(owner_view_globals, owner_view_globals_size),
			};
		}

		std::uint64_t hash_slot_initializer_shared_state(
			const frontend_snapshot& snapshot) noexcept
		{
			const std::array values{
				static_cast<std::uint64_t>(snapshot.frontend),
				static_cast<std::uint64_t>(snapshot.slot_count),
				static_cast<std::uint64_t>(snapshot.global_selector),
				static_cast<std::uint64_t>(snapshot.current_record_index),
				static_cast<std::uint64_t>(snapshot.record_count),
				static_cast<std::uint64_t>(snapshot.global_record_count),
				hash_memory_region(scene_view_globals, scene_view_globals_size),
				hash_memory_region(camera_primary_globals, camera_globals_size),
				hash_memory_region(camera_secondary_globals, camera_globals_size),
				hash_memory_region(owner_view_globals, owner_view_globals_size),
			};
			return hash_memory_region(reinterpret_cast<std::uintptr_t>(values.data()),
				sizeof(values));
		}

		bool same_frontend_snapshot(const frontend_snapshot& left,
			const frontend_snapshot& right) noexcept
		{
			return left.frontend == right.frontend &&
				left.slot_count == right.slot_count &&
				left.global_selector == right.global_selector &&
				left.current_record_index == right.current_record_index &&
				left.record_count == right.record_count &&
				left.global_record_count == right.global_record_count;
		}

		struct frontend_correlation
		{
			std::uint64_t frame_id{};
			engine_view_probe::record_flags flags{};
		};

		frontend_correlation current_frontend_correlation() noexcept
		{
			if (r_end_frame_depth != 0)
			{
				if (r_end_frame_depth <= r_end_frame_scopes.size())
				{
					const auto& boundary = r_end_frame_scopes[r_end_frame_depth - 1];
					if (boundary.frame_id != 0)
					{
						return {
							boundary.frame_id,
							engine_view_probe::flag(
								engine_view_probe::record_flag::inside_r_end_frame),
						};
					}
				}
				// Even if pathological nesting exceeds the bounded diagnostic stack,
				// preserve the fact that this call occurred inside R_EndFrame.
				return {
					frontend_epoch_sequence.load(std::memory_order_relaxed),
					engine_view_probe::flag(
						engine_view_probe::record_flag::inside_r_end_frame),
				};
			}

			// R_RenderScene completes before R_EndFrame. Associate a normal scene with
			// the next boundary that will consume/reset its frontend record arena, not
			// the preceding marker. A call in R_EndFrame's dynamic extent (including the
			// bounded native-right finalizer) is separately flagged above; this marker is
			// never treated as the scene owner.
			return {
				frontend_epoch_sequence.load(std::memory_order_relaxed) + 1,
				0,
			};
		}

		class view_transaction_scope final
		{
		public:
			view_transaction_scope(const int local_client_num, void* const scene_descriptor,
				const std::uint32_t scene_record_index, const float lod_scale,
				const int draw_type) noexcept
			{
				context_.previous = active_view_transaction;
				// Always shadow a parent transaction while this real H2 call is in scope.
				// If the probe is toggled concurrently, an inner wrapper must become an
				// explicit orphan rather than being attributed to the previous outer view.
				active_view_transaction = nullptr;
				const auto depth = static_cast<std::uint16_t>(context_.previous != nullptr
					? context_.previous->token.depth + 1 : 1);
				context_.begin_snapshot = read_frontend_snapshot();
				const auto correlation = current_frontend_correlation();
				context_.flags = correlation.flags;
				const auto reservation = pending_reservation;
				pending_reservation = {};
				const auto reservation_matches = reservation.valid &&
					reservation.frontend_epoch == correlation.frame_id &&
					reservation.observation.frontend_before ==
						reservation.observation.frontend_after &&
					reservation.observation.frontend_after == context_.begin_snapshot.frontend &&
					reservation.observation.selector_before ==
						reservation.observation.selector_after &&
					reservation.observation.selector_after ==
						context_.begin_snapshot.global_selector &&
					reservation.observation.global_count_before !=
						(std::numeric_limits<std::uint32_t>::max)() &&
					reservation.observation.output_index ==
						reservation.observation.global_count_before &&
					reservation.observation.global_count_after ==
						reservation.observation.global_count_before + 1 &&
					reservation.observation.output_index == scene_record_index &&
					scene_record_index < engine_view_probe::frontend_record_capacity &&
					reservation.observation.current_index_after == scene_record_index &&
					reservation.observation.record_count_after ==
						reservation.observation.global_count_after &&
					reservation.observation.global_count_after ==
						context_.begin_snapshot.global_record_count &&
					reservation.observation.current_index_after ==
						context_.begin_snapshot.current_record_index &&
					reservation.observation.record_count_after ==
						context_.begin_snapshot.record_count &&
					reservation.observation.slot_count_before ==
						reservation.observation.slot_count_after;
				if (!reservation_matches)
				{
					context_.flags = engine_view_probe::with_flag(context_.flags,
						engine_view_probe::record_flag::count_mismatch);
				}
				if (reservation.overwrote_previous)
				{
					context_.flags = engine_view_probe::with_flag(context_.flags,
						engine_view_probe::record_flag::duplicate);
				}
				context_.token = engine_view_probe::begin(correlation.frame_id, depth, {
					reinterpret_cast<std::uintptr_t>(scene_descriptor),
					hash_scene_descriptor_prefix(scene_descriptor),
					local_client_num,
					scene_record_index,
					lod_scale,
					draw_type,
					context_.previous != nullptr ? context_.previous->token.transaction_id : 0,
				}, context_.flags);
				if (!context_.token) return;
				if (reservation.valid)
				{
					engine_view_probe::record_reservation(context_.token,
						reservation.observation, engine_view_probe::observation_stage::after_call,
						context_.flags);
				}
				else
				{
					auto missing = engine_view_probe::record_reservation_observation{};
					missing.frontend_before = context_.begin_snapshot.frontend;
					missing.frontend_after = context_.begin_snapshot.frontend;
					missing.output_index = engine_view_probe::invalid_slot_index;
					missing.global_count_after = context_.begin_snapshot.global_record_count;
					missing.current_index_after = context_.begin_snapshot.current_record_index;
					missing.record_count_after = context_.begin_snapshot.record_count;
					missing.slot_count_after = context_.begin_snapshot.slot_count;
					missing.selector_before = context_.begin_snapshot.global_selector;
					missing.selector_after = context_.begin_snapshot.global_selector;
					engine_view_probe::record_reservation(context_.token, missing,
						engine_view_probe::observation_stage::after_call, context_.flags);
				}
				if (view_diagnostics_enabled())
				{
					engine_view_probe::callstack_observation stack{};
					std::array<void*, 16> frames{};
					const auto captured = CaptureStackBackTrace(0,
						static_cast<DWORD>(frames.size()), frames.data(), nullptr);
					for (std::size_t index{}; index < captured; ++index)
					{
						stack.frames[index] = reinterpret_cast<std::uintptr_t>(frames[index]);
					}
					engine_view_probe::record_callstack(context_.token, stack, context_.flags);
				}
				active_view_transaction = &context_;
				engine_view_probe::record_frontend(context_.token, {
					context_.begin_snapshot.frontend,
					context_.begin_snapshot.global_selector,
					context_.begin_snapshot.slot_count,
					context_.begin_snapshot.current_record_index,
					context_.begin_snapshot.record_count,
					context_.begin_snapshot.global_record_count,
					engine_view_probe::slot_base_address(context_.begin_snapshot.frontend),
					0, // No generator-local auxiliary value exists at the outer boundary.
				}, engine_view_probe::observation_stage::enter, context_.flags);
			}

			view_transaction_scope(const view_transaction_scope&) = delete;
			view_transaction_scope& operator=(const view_transaction_scope&) = delete;

			~view_transaction_scope()
			{
				if (context_.token)
				{
					const auto end_snapshot = read_frontend_snapshot();
					engine_view_probe::end(context_.token, {
						context_.begin_snapshot.slot_count,
						end_snapshot.slot_count,
						context_.slot_calls,
						context_.generator_calls,
						context_.return_calls,
						context_.last_slot,
						context_.last_slot_index,
						context_.begin_snapshot.global_selector,
						end_snapshot.global_selector,
						context_.slot_signature,
					}, context_.flags);
				}
				active_view_transaction = context_.previous;
			}

		private:
			friend void render_scene_stub(int, void*, std::uint32_t, float, int);
			view_transaction_context context_;
		};

		void invoke_original(const int local_client_num, void* const scene_descriptor,
			const std::uint32_t scene_record_index, const float lod_scale, const int draw_type)
		{
			reinterpret_cast<render_scene_fn>(render_scene_function)(
				local_client_num, scene_descriptor, scene_record_index, lod_scale, draw_type);
		}

		void reserve_scene_record_probe_stub(std::uint32_t* const output_index)
		{
			if (!engine_view_probe::is_enabled())
			{
				pending_reservation = {};
				reinterpret_cast<reserve_scene_record_fn>(reserve_scene_record_function)(
					output_index);
				return;
			}

			const auto before = read_frontend_snapshot();
			reinterpret_cast<reserve_scene_record_fn>(reserve_scene_record_function)(
				output_index);
			const auto after = read_frontend_snapshot();
			pending_record_reservation reservation{};
			reservation.valid = true;
			reservation.overwrote_previous = pending_reservation.valid;
			reservation.frontend_epoch = current_frontend_correlation().frame_id;
			reservation.observation = {
				before.frontend,
				after.frontend,
				before.global_record_count,
				after.global_record_count,
				output_index != nullptr ? *output_index : engine_view_probe::invalid_slot_index,
				before.current_record_index,
				after.current_record_index,
				before.record_count,
				after.record_count,
				before.slot_count,
				after.slot_count,
				before.global_selector,
				after.global_selector,
			};
			pending_reservation = reservation;
		}

		void* allocate_view_slot_probe_stub()
		{
			auto* const transaction = active_view_transaction;
			if (transaction == nullptr)
			{
				return reinterpret_cast<allocate_view_slot_fn>(
					allocate_view_slot_function)();
			}
			const auto before = read_frontend_snapshot();
			const auto token = transaction->token;
			engine_view_probe::record_frontend(token, {
				before.frontend,
				before.global_selector,
				before.slot_count,
				before.current_record_index,
				before.record_count,
				before.global_record_count,
				engine_view_probe::slot_base_address(before.frontend),
				0,
			}, engine_view_probe::observation_stage::before_call);

			// Forward the original H2 allocator exactly once. Observation never changes
			// the slot count, selected arena, return value, or renderer ordering.
			auto* const slot = reinterpret_cast<allocate_view_slot_fn>(
				allocate_view_slot_function)();

			const auto after = read_frontend_snapshot();
			const auto derived = engine_view_probe::derive_slot_index(before.frontend,
				reinterpret_cast<std::uintptr_t>(slot), after.slot_count);
			auto flags = transaction->flags;
			const auto call_index = transaction->slot_calls;
			++transaction->slot_calls;
			if (transaction->slot_calls > transaction->candidates.size())
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::duplicate);
			}
			transaction->last_slot = reinterpret_cast<std::uintptr_t>(slot);
			transaction->last_slot_index = derived ? derived.index :
				engine_view_probe::invalid_slot_index;
			if (call_index < transaction->candidates.size())
			{
				auto& candidate = transaction->candidates[call_index];
				candidate.allocator_before = before;
				candidate.allocator_after = after;
				candidate.allocator_slot = reinterpret_cast<std::uintptr_t>(slot);
				candidate.allocator_index = transaction->last_slot_index;
				candidate.allocator_seen = true;
			}
			if (!derived)
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::invalid_slot);
			}
			transaction->slot_signature ^= reinterpret_cast<std::uintptr_t>(slot) +
				(static_cast<std::uint64_t>(transaction->last_slot_index) << 32) +
				0x9E3779B97F4A7C15ull;
			if (before.global_selector != after.global_selector ||
				before.frontend != after.frontend)
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::selector_changed);
			}
			if (before.current_record_index != after.current_record_index ||
				before.record_count != after.record_count ||
				before.global_record_count != after.global_record_count)
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::count_mismatch);
			}
			transaction->flags = flags;
			engine_view_probe::record_slot(token, {
				before.frontend,
				reinterpret_cast<std::uintptr_t>(slot),
				before.slot_count,
				after.slot_count,
				derived ? derived.index : engine_view_probe::invalid_slot_index,
				after.global_selector,
				after.current_record_index,
				after.record_count,
				after.global_record_count,
			}, engine_view_probe::observation_stage::after_call, flags);
			++transaction->return_calls;
			engine_view_probe::record_return(token, {
				engine_view_probe::return_source::allocator,
				reinterpret_cast<std::uintptr_t>(slot),
				reinterpret_cast<std::uintptr_t>(slot),
				after.slot_count,
				derived ? derived.index : engine_view_probe::invalid_slot_index,
				after.global_selector,
				after.current_record_index,
				after.record_count,
				0,
				before.frontend,
			}, engine_view_probe::observation_stage::after_call, flags);
			return slot;
		}

		bool derive_stereo_eye_slots(const void* const natural_slot,
			const engine_view_probe::transaction_token& token,
			engine_stereo_view::slot_pair& output,
			std::array<engine_stereo_bridge::render_config, 2>& configs) noexcept
		{
			output = {};
			configs = {};
			if (natural_slot == nullptr || !token) return false;

			if (!engine_stereo_bridge::get_render_configs(configs) ||
				!engine_stereo_view::derive(natural_slot, configs, output))
			{
				return false;
			}
			std::memcpy(output.native_tan_half.data(),static_cast<const std::byte*>(natural_slot)+0x140,sizeof(output.native_tan_half));
			if(const auto provider=auxiliary_scene::screen_scope_epoch.load())output.screen_scope_epoch=provider();
			if(const auto provider=eye_composition::remote_camera_epoch.load())output.remote_camera_epoch=provider();
			if(const auto provider=auxiliary_scene::weapon_display_epoch.load())output.weapon_display_epoch=provider();
			std::memcpy(&output.native_near_distance,static_cast<const std::byte*>(natural_slot)+0x148,4);
			if(output.screen_scope_epoch)if(const auto provider=auxiliary_scene::screen_scope_aspect.load())output.screen_scope_aspect=provider();

			for (auto& eye : output.eyes)
			{
				reinterpret_cast<finalize_view_slot_fn>(finalize_view_slot_function)(
					eye.bytes.data());
			}
			if (!engine_stereo_view::validate_finalized(output))
			{
				output = {};
				return false;
			}

			if (!view_diagnostics_enabled()) return true;
			auto expected = std::uint32_t{};
			if (!stereo_eye_slot_capture_state.compare_exchange_strong(expected, 1,
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return true;
			}
			bool complete = true;
			for (std::size_t eye_index{}; eye_index < output.eyes.size(); ++eye_index)
			{
				const auto& eye = output.eyes[eye_index];
				const auto& projection = configs[eye_index].eyes[eye.view_eye];
				complete = capture_artifact(stereo_eye_slot_artifacts[eye_index],
					reinterpret_cast<std::uintptr_t>(eye.bytes.data()), {
						GetTickCount64(),
						reinterpret_cast<std::uintptr_t>(natural_slot),
						eye.pair_id,
						eye.publication,
						token.transaction_id,
						(static_cast<std::uint64_t>(engine_view_probe::float_bits(
							projection.tan_left)) << 32) |
							engine_view_probe::float_bits(projection.tan_right),
						(static_cast<std::uint64_t>(engine_view_probe::float_bits(
							projection.tan_down)) << 32) |
							engine_view_probe::float_bits(projection.tan_up),
						(static_cast<std::uint64_t>(eye.output_eye) << 32) |
							eye.view_eye,
					}) && complete;
			}
			stereo_eye_slot_capture_state.store(complete ? 2u : 3u,
				std::memory_order_release);
			// Optional evidence cannot veto already validated stereo views.
			return true;
		}

		void build_fx_camera_stub(const void* refdef, const void* view,
			float zfar, int thermal, int other_flag, void* camera)
		{
			// Sole verified builder call, before 45B920 publishes the camera to
			// its FX consumers/workers. Never patch FxSystem.camera asynchronously.
			utils::hook::invoke<void>(fx_camera_function, refdef, view, zfar,
				thermal, other_flag, camera);
			std::array<engine_stereo_bridge::render_config, 2> configs{};
			if (!engine_stereo_bridge::get_render_configs(configs)) return;
			fx_culling_attempts.fetch_add(1, std::memory_order_relaxed);
			const auto applied = engine_stereo_view::apply_fx_culling_union(camera, configs);
			fx_culling_contract_failed.store(!applied, std::memory_order_release);
			if (applied)
			{
				fx_culling_applications.fetch_add(1, std::memory_order_relaxed);
			}
			else if (fx_culling_failures.fetch_add(1, std::memory_order_relaxed) == 0)
			{
				console::error("[VR] FX camera culling contract failed; native scene admission rejected; run vr_status\n");
			}
		}

		bool apply_production_culling_union(void* const slot,
			const std::array<engine_stereo_bridge::render_config, 2>& configs,
			view_transaction_context& transaction) noexcept
		{
			if (slot == nullptr || transaction.stereo_views.eyes[0].pair_id == 0)
				return false;
			const auto admission = engine_stereo_owner_pass::admit_frontend_culling_pair(
				transaction.stereo_views.eyes[0].pair_id);
			if (admission == engine_stereo_owner_pass::frontend_culling_admission::unarmed)
			{
				// The proof frame and any unarmed frame keep H2's natural view. They
				// are never submitted as production stereo, so widening them would
				// only contaminate the desktop path without fixing HMD visibility.
				return true;
			}
			if (admission != engine_stereo_owner_pass::frontend_culling_admission::accepted)
				return false;
			if (fx_culling_contract_failed.load(std::memory_order_acquire)) return false;

			culling_union_attempts.fetch_add(1, std::memory_order_relaxed);
			engine_stereo_view::culling_union_slot culling{};
			if (!engine_stereo_view::derive_culling_union(slot, configs, culling,transaction.stereo_views.screen_scope_aspect))
			{
				culling_union_failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			reinterpret_cast<finalize_view_slot_fn>(finalize_view_slot_function)(
				culling.bytes.data());
			if (!engine_stereo_view::validate_finalized_culling_union(culling))
			{
				culling_union_failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}

			std::memcpy(slot, culling.bytes.data(), culling.bytes.size());
			std::memcpy(transaction.frontend_slot.data(), culling.bytes.data(),
				transaction.frontend_slot.size());
			culling_union_tan_left.store(engine_view_probe::float_bits(culling.tan_left),
				std::memory_order_relaxed);
			culling_union_tan_right.store(engine_view_probe::float_bits(culling.tan_right),
				std::memory_order_relaxed);
			culling_union_tan_down.store(engine_view_probe::float_bits(culling.tan_down),
				std::memory_order_relaxed);
			culling_union_tan_up.store(engine_view_probe::float_bits(culling.tan_up),
				std::memory_order_relaxed);
			culling_union_near_distance.store(engine_view_probe::float_bits(
				culling.near_distance_units), std::memory_order_relaxed);
			culling_union_horizontal_expansion.store(engine_view_probe::float_bits(
				culling.horizontal_origin_expansion), std::memory_order_relaxed);
			culling_union_applications.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		void initialize_view_slot_probe_stub(const void* const scene_descriptor,
			void* const slot)
		{
			auto* const transaction = active_view_transaction;
			if (transaction == nullptr || !transaction->token)
			{
				reinterpret_cast<initialize_view_slot_fn>(initialize_view_slot_function)(
					scene_descriptor, slot);
				return;
			}

			const auto before = read_frontend_snapshot();
			std::array<std::uint8_t, view_slot_size> before_bytes{};
			if (view_diagnostics_enabled() && slot != nullptr)
			{
				std::memcpy(before_bytes.data(), slot, before_bytes.size());
				(void)capture_artifact(slot_before_initializer_artifact,
					reinterpret_cast<std::uintptr_t>(slot), {
						GetTickCount64(),
						reinterpret_cast<std::uintptr_t>(scene_descriptor),
						reinterpret_cast<std::uintptr_t>(slot),
						transaction->token.transaction_id,
						transaction->token.frontend_frame_id,
						engine_view_probe::invalid_slot_index,
						0,
						0,
					});
			}
			const auto slot_before_hash = view_diagnostics_enabled() && slot != nullptr
				? hash_memory_region(reinterpret_cast<std::uintptr_t>(slot), view_slot_size) : 0;
			const auto shared_before_hash = view_diagnostics_enabled() ? hash_slot_initializer_shared_state(before) : 0;

			// Forward H2's initializer exactly once. This observer does not allocate a
			// second slot, alter the descriptor, or publish any renderer/GPU work.
			reinterpret_cast<initialize_view_slot_fn>(initialize_view_slot_function)(
				scene_descriptor, slot);
			if (transaction->slot_calls != 1 || transaction->stereo_views_ready)
			{
				// Never join initializers from different record-local states.
				transaction->stereo_views = {};
				transaction->stereo_views_ready = false;
			}
			else
			{
				engine_stereo_view::slot_pair stereo_views{};
				std::array<engine_stereo_bridge::render_config, 2> configs{};
				if (derive_stereo_eye_slots(slot, transaction->token, stereo_views,
					configs))
				{
					std::memcpy(transaction->frontend_slot.data(), slot,
						transaction->frontend_slot.size());
					transaction->stereo_views = stereo_views;
					transaction->stereo_views_ready = apply_production_culling_union(
						slot, configs, *transaction);
				}
			}

			const auto after = read_frontend_snapshot();
			const auto slot_after_hash = view_diagnostics_enabled() && slot != nullptr
				? hash_memory_region(reinterpret_cast<std::uintptr_t>(slot), view_slot_size) : 0;
			const auto shared_after_hash = view_diagnostics_enabled() ? hash_slot_initializer_shared_state(after) : 0;
			auto flags = transaction->flags;
			if (before.frontend != after.frontend ||
				before.global_selector != after.global_selector)
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::selector_changed);
			}
			if (!same_frontend_snapshot(before, after))
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::count_mismatch);
			}

			const auto derived = engine_view_probe::derive_slot_index(after.frontend,
				reinterpret_cast<std::uintptr_t>(slot), after.slot_count);
			if (view_diagnostics_enabled() && slot != nullptr)
			{
				(void)capture_artifact(slot_after_initializer_artifact,
					reinterpret_cast<std::uintptr_t>(slot), {
						GetTickCount64(),
						reinterpret_cast<std::uintptr_t>(scene_descriptor),
						reinterpret_cast<std::uintptr_t>(slot),
						transaction->token.transaction_id,
						transaction->token.frontend_frame_id,
						derived ? derived.index : engine_view_probe::invalid_slot_index,
						1,
						0,
					});
			}
			std::uint32_t changed_bytes{};
			std::uint32_t first_changed = engine_view_probe::invalid_slot_index;
			std::uint32_t last_changed = engine_view_probe::invalid_slot_index;
			if (view_diagnostics_enabled() && slot != nullptr)
			{
				const auto* const after_bytes = reinterpret_cast<const std::uint8_t*>(slot);
				for (std::uint32_t index{}; index < view_slot_size; ++index)
				{
					if (before_bytes[index] == after_bytes[index]) continue;
					if (first_changed == engine_view_probe::invalid_slot_index)
					{
						first_changed = index;
					}
					last_changed = index;
					++changed_bytes;
				}
			}
			if (!derived)
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::invalid_slot);
			}
			transaction->flags = flags;
			if (!view_diagnostics_enabled()) return;
			engine_view_probe::record_slot_initializer(transaction->token, {
				reinterpret_cast<std::uintptr_t>(scene_descriptor),
				reinterpret_cast<std::uintptr_t>(slot),
				hash_scene_descriptor_prefix(scene_descriptor),
				slot_before_hash,
				slot_after_hash,
				shared_before_hash,
				shared_after_hash,
				changed_bytes,
				first_changed,
				last_changed,
				derived ? derived.index : engine_view_probe::invalid_slot_index,
			}, engine_view_probe::observation_stage::after_call, flags);
		}

		void publish_generator_views(view_transaction_context& transaction,
			const frontend_snapshot& frontend, std::uint32_t index, const void* slot, const void* selected)
		{
			if (!transaction.stereo_views_ready || !transaction.token) return;
			scene_publication_attempts.fetch_add(1, std::memory_order_relaxed);
			const auto fail = [&]
			{
				if (scene_publication_failures.fetch_add(1, std::memory_order_relaxed) == 0)
					console::error("[VR] pre-generator stereo publication rejected; run vr_status\n");
			};
			if (transaction.stereo_published || transaction.generator_calls != 1 ||
				frontend.frontend == 0 || index >= engine_view_probe::frontend_record_capacity ||
				index != transaction.last_slot_index || !slot || selected != slot ||
				std::memcmp(slot, transaction.frontend_slot.data(), transaction.frontend_slot.size()) != 0)
			{ fail(); return; }
			std::uintptr_t arena{};
			std::memcpy(&arena, reinterpret_cast<const void*>(frontend.frontend + frontend_record_arena_pointer_offset), sizeof(arena));
			if (!arena) { fail(); return; }
			const auto record = arena + index * engine_view_probe::frontend_record_stride;
			region_capture::phase_scope timing(region_capture::phase::scene_publication,
				reinterpret_cast<void*>(record), frontend.frontend);
			// Publish immutable VIEW inputs before H2's generator can enqueue any
			// scene consumer. The record payload is still being built: never copy it
			// here. 0x14077F526 sets its world type to 4; the backend independently
			// validates that type, exact camera and native CPU completion before use.
			eye_composition::register_scene(transaction.stereo_views, record);
			transaction.stereo_published = engine_stereo_binding::publish({
				frontend.frontend, record, index, native_render_contract::expected_world_record_type,
				transaction.token.frontend_frame_id, transaction.token.transaction_id, transaction.stereo_views});
			timing.finish(transaction.stereo_published);
			if (transaction.stereo_published) scene_publication_successes.fetch_add(1, std::memory_order_relaxed);
			else fail();
		}

		void generate_draw_surfs_probe_stub(const std::uint32_t local_client,
			const std::uint32_t frontend_record_index, void* const scratch,
			void* const selected, void* const slot, void* const per_client_output)
		{
			auto* const transaction = active_view_transaction;
			if (transaction == nullptr)
			{
			reinterpret_cast<generate_draw_surfs_fn>(generate_draw_surfs_function)(
				local_client, frontend_record_index, scratch, selected, slot,
				per_client_output);
				return;
			}
			const auto before = read_frontend_snapshot();
			const auto token = transaction->token;
			std::int32_t draw_type{};
			if (scratch != nullptr)
			{
				draw_type = *reinterpret_cast<const std::int32_t*>(
					reinterpret_cast<std::uintptr_t>(scratch) +
					engine_view_probe::scratch_draw_type_offset);
			}
			const auto derived = engine_view_probe::derive_slot_index(before.frontend,
				reinterpret_cast<std::uintptr_t>(slot), before.slot_count);
			auto flags = transaction->flags;
			std::uint32_t call_index{engine_view_probe::invalid_slot_index};
			call_index = transaction->generator_calls;
			++transaction->generator_calls;
			if (transaction->generator_calls > transaction->candidates.size())
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::duplicate);
			}
			if (call_index < transaction->candidates.size())
			{
				auto& candidate = transaction->candidates[call_index];
				candidate.generator_before = before;
				candidate.generator_slot = reinterpret_cast<std::uintptr_t>(slot);
				candidate.generator_selected = reinterpret_cast<std::uintptr_t>(selected);
				candidate.generator_scratch = reinterpret_cast<std::uintptr_t>(scratch);
				candidate.generator_output = reinterpret_cast<std::uintptr_t>(
					per_client_output);
				candidate.generator_index = derived ? derived.index :
					engine_view_probe::invalid_slot_index;
				candidate.generator_record_index = frontend_record_index;
				candidate.generator_local_client = local_client;
				candidate.generator_draw_type = draw_type;
				candidate.generator_seen = true;
				const auto binding_valid = candidate.allocator_seen && derived &&
					candidate.generator_slot == candidate.allocator_slot &&
					candidate.generator_selected == candidate.allocator_slot &&
					candidate.generator_index == candidate.allocator_index &&
					candidate.generator_record_index == candidate.allocator_index &&
					same_frontend_snapshot(candidate.generator_before,
						candidate.allocator_after);
				if (!binding_valid)
				{
					flags = engine_view_probe::with_flag(flags,
						engine_view_probe::record_flag::invalid_slot);
				}
			}
			else
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::invalid_slot);
			}
			transaction->flags = flags;
			engine_view_probe::record_frontend(token, {
				before.frontend,
				before.global_selector,
				before.slot_count,
				before.current_record_index,
				before.record_count,
				before.global_record_count,
				engine_view_probe::slot_base_address(before.frontend),
				frontend_record_index,
			}, engine_view_probe::observation_stage::before_call, flags);
			engine_view_probe::record_generator(token, {
				static_cast<std::int32_t>(local_client),
				frontend_record_index,
				reinterpret_cast<std::uintptr_t>(scratch),
				reinterpret_cast<std::uintptr_t>(selected),
				reinterpret_cast<std::uintptr_t>(slot),
				reinterpret_cast<std::uintptr_t>(per_client_output),
				draw_type,
				before.frontend,
				before.slot_count,
				derived ? derived.index : engine_view_probe::invalid_slot_index,
			}, engine_view_probe::observation_stage::before_call, flags);
			engine_view_probe::view_state_observation view_state_before{};
			if (view_diagnostics_enabled())
			{
				view_state_before = capture_view_state(
					engine_view_probe::view_state_source::draw_surface_generator,
					reinterpret_cast<std::uintptr_t>(scratch),
					reinterpret_cast<std::uintptr_t>(slot),
					reinterpret_cast<std::uintptr_t>(per_client_output));
				engine_view_probe::record_view_state(token, view_state_before,
					engine_view_probe::observation_stage::before_call, flags);
			}
			publish_generator_views(*transaction, before, frontend_record_index, slot, selected);
			reinterpret_cast<generate_draw_surfs_fn>(generate_draw_surfs_function)(
				local_client, frontend_record_index, scratch, selected, slot,
				per_client_output);
			if (view_diagnostics_enabled())
			{
				auto view_state_after = capture_view_state(
					engine_view_probe::view_state_source::draw_surface_generator,
					reinterpret_cast<std::uintptr_t>(scratch),
					reinterpret_cast<std::uintptr_t>(slot),
					reinterpret_cast<std::uintptr_t>(per_client_output));
				view_state_after.relations = engine_view_probe::with_relation(
					view_state_after.relations,
					engine_view_probe::view_state_relation::slot_compared);
				if (view_state_after.view_slot_hash == view_state_before.view_slot_hash)
				{
					view_state_after.relations = engine_view_probe::with_relation(
						view_state_after.relations,
						engine_view_probe::view_state_relation::slot_unchanged);
				}
				transaction->last_generator_slot_hash = view_state_after.view_slot_hash;
				transaction->last_generator_state_valid = true;
				if (slot != nullptr)
				{
					(void)capture_artifact(slot_after_generator_artifact,
						reinterpret_cast<std::uintptr_t>(slot), {
							GetTickCount64(),
							reinterpret_cast<std::uintptr_t>(slot),
							reinterpret_cast<std::uintptr_t>(per_client_output),
							token.transaction_id,
							token.frontend_frame_id,
							frontend_record_index,
							local_client,
							static_cast<std::uint32_t>(draw_type),
						});
				}
				if (per_client_output != nullptr)
				{
					(void)capture_artifact(output_after_generator_artifact,
						reinterpret_cast<std::uintptr_t>(per_client_output), {
							GetTickCount64(),
							reinterpret_cast<std::uintptr_t>(slot),
							reinterpret_cast<std::uintptr_t>(per_client_output),
							token.transaction_id,
							token.frontend_frame_id,
							frontend_record_index,
							local_client,
							static_cast<std::uint32_t>(draw_type),
						});
				}
				engine_view_probe::record_view_state(token, view_state_after,
					engine_view_probe::observation_stage::after_call, flags);
			}

			const auto after = read_frontend_snapshot();
			++transaction->return_calls;
			if (call_index < transaction->candidates.size())
			{
				transaction->candidates[call_index].generator_after = after;
			}
			if (before.global_selector != after.global_selector ||
				before.frontend != after.frontend)
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::selector_changed);
			}
			if (before.current_record_index != after.current_record_index ||
				before.record_count != after.record_count ||
				before.global_record_count != after.global_record_count)
			{
				flags = engine_view_probe::with_flag(flags,
					engine_view_probe::record_flag::count_mismatch);
			}
			transaction->flags = flags;
			engine_view_probe::record_return(token, {
				engine_view_probe::return_source::generator,
				reinterpret_cast<std::uintptr_t>(slot),
				0,
				after.slot_count,
				derived ? derived.index : engine_view_probe::invalid_slot_index,
				after.global_selector,
				after.current_record_index,
				after.record_count,
				draw_type,
				reinterpret_cast<std::uintptr_t>(per_client_output),
			}, engine_view_probe::observation_stage::after_call, flags);
		}

		void render_scene_stub(const int local_client_num, void* const scene_descriptor,
			const std::uint32_t scene_record_index, const float lod_scale,
			const int draw_type)
		{
			if (!engine_view_probe::is_enabled())
			{
				pending_reservation = {};
				if (active_view_transaction != nullptr)
				{
					auto* const previous = active_view_transaction;
					active_view_transaction = nullptr;
					const auto restore_transaction = gsl::finally([previous]
					{
						active_view_transaction = previous;
					});
					invoke_original(local_client_num, scene_descriptor, scene_record_index,
						lod_scale, draw_type);
					return;
				}
				invoke_original(local_client_num, scene_descriptor, scene_record_index,
					lod_scale, draw_type);
				return;
			}

			const auto before = read_frontend_snapshot();
			const auto hash_before = hash_scene_descriptor_prefix(scene_descriptor);
			view_transaction_scope view_transaction(local_client_num, scene_descriptor,
				scene_record_index, lod_scale, draw_type);
			const auto sample_extended_descriptor = view_diagnostics_enabled() &&
				view_transaction.context_.token &&
				view_transaction.context_.token.transaction_id <=
					extended_scene_descriptor_sample_limit;
			const auto descriptor_address = reinterpret_cast<std::uintptr_t>(scene_descriptor);
			const auto descriptor_readable_before = sample_extended_descriptor &&
				validate_readable_data_range(descriptor_address,
					extended_scene_descriptor_size);
			const auto descriptor_hash_before = descriptor_readable_before
				? hash_memory_region(descriptor_address, extended_scene_descriptor_size) : 0;
			if (descriptor_readable_before)
			{
				(void)capture_artifact(descriptor_before_artifact, descriptor_address, {
					GetTickCount64(),
					descriptor_address,
					view_transaction.context_.token.transaction_id,
					view_transaction.context_.token.frontend_frame_id,
					scene_record_index,
					static_cast<std::uint32_t>(local_client_num),
					static_cast<std::uint32_t>(draw_type),
					descriptor_hash_before,
				});
			}
			diagnostics::scene_hook_entered();
			engine_stereo_bridge::record_scene_hook_entry();
			const auto scene_hook_exit = gsl::finally([]()
			{
				diagnostics::scene_hook_exited();
			});
			diagnostics::record_trace(diagnostics::trace_event::scene_hook_enter,
				reinterpret_cast<std::uintptr_t>(scene_descriptor),
				static_cast<std::uint64_t>(draw_type));
			if (view_diagnostics_enabled())
			{
				const auto view_output = per_client_output_address(local_client_num);
				engine_view_probe::record_view_state(view_transaction.context_.token,
					capture_view_state(engine_view_probe::view_state_source::outer_scene,
						reinterpret_cast<std::uintptr_t>(scene_descriptor), 0, view_output),
					engine_view_probe::observation_stage::before_call,
					view_transaction.context_.flags);
			}
			engine_view_probe::record_view_call(view_transaction.context_.token, {
				0,
				before.frontend,
				before.global_selector,
				before.slot_count,
				reinterpret_cast<std::uintptr_t>(scene_descriptor),
				hash_before,
				before.global_record_count,
				before.current_record_index,
				before.record_count,
			}, engine_view_probe::observation_stage::before_call,
				view_transaction.context_.flags);
			diagnostics::record_trace(diagnostics::trace_event::scene_original_call,
				reinterpret_cast<std::uintptr_t>(scene_descriptor), 0);
			// Preserve H2's exact natural scene transaction. The retired dual-record
			// experiment invoked a second scene owner after the frame finalizer had
			// already invalidated part of its transient state; same-process A/B testing
			// proved that mutation caused the fatal drop. Boundary diagnosis below is
			// now observation-only until a complete owner boundary is proven.
			invoke_original(local_client_num, scene_descriptor, scene_record_index,
				lod_scale, draw_type);
			if (sample_extended_descriptor)
			{
				const auto descriptor_readable_after = validate_readable_data_range(
					descriptor_address, extended_scene_descriptor_size);
				const auto descriptor_hash_after = descriptor_readable_after
					? hash_memory_region(descriptor_address, extended_scene_descriptor_size) : 0;
				if (descriptor_readable_after)
				{
					(void)capture_artifact(descriptor_after_artifact, descriptor_address, {
						GetTickCount64(),
						descriptor_address,
						view_transaction.context_.token.transaction_id,
						view_transaction.context_.token.frontend_frame_id,
						scene_record_index,
						static_cast<std::uint32_t>(local_client_num),
						static_cast<std::uint32_t>(draw_type),
						descriptor_hash_after,
					});
				}
				engine_view_probe::record_descriptor_contract(
					view_transaction.context_.token, {
						descriptor_address,
						descriptor_hash_before,
						descriptor_hash_after,
						static_cast<std::uint32_t>(extended_scene_descriptor_size),
						descriptor_readable_before,
						descriptor_readable_after,
					}, engine_view_probe::observation_stage::after_call,
					view_transaction.context_.flags);
			}
			const auto after = read_frontend_snapshot();
			const auto hash_after = hash_scene_descriptor_prefix(scene_descriptor);
			if (view_diagnostics_enabled())
			{
				const auto view_output = per_client_output_address(local_client_num);
				auto outer_view_state = capture_view_state(
					engine_view_probe::view_state_source::outer_scene,
					reinterpret_cast<std::uintptr_t>(scene_descriptor),
					view_transaction.context_.last_slot, view_output);
				if (view_transaction.context_.last_generator_state_valid)
				{
					outer_view_state.relations = engine_view_probe::with_relation(
						outer_view_state.relations,
						engine_view_probe::view_state_relation::slot_compared);
					if (outer_view_state.view_slot_hash ==
						view_transaction.context_.last_generator_slot_hash)
					{
						outer_view_state.relations = engine_view_probe::with_relation(
							outer_view_state.relations,
							engine_view_probe::view_state_relation::slot_unchanged);
					}
				}
				if (view_transaction.context_.last_slot != 0 && view_output != 0)
				{
					outer_view_state.relations = engine_view_probe::with_relation(
						outer_view_state.relations,
						engine_view_probe::view_state_relation::output_compared);
					if (outer_view_state.view_slot_hash ==
						outer_view_state.per_client_output_hash)
					{
						outer_view_state.relations = engine_view_probe::with_relation(
							outer_view_state.relations,
							engine_view_probe::view_state_relation::output_matches_slot);
					}
				}
				engine_view_probe::record_view_state(view_transaction.context_.token,
					outer_view_state, engine_view_probe::observation_stage::after_call,
					view_transaction.context_.flags);
			}
			engine_view_probe::record_view_call(view_transaction.context_.token, {
				0,
				after.frontend,
				after.global_selector,
				after.slot_count,
				reinterpret_cast<std::uintptr_t>(scene_descriptor),
				hash_after,
				after.global_record_count,
				after.current_record_index,
				after.record_count,
			}, engine_view_probe::observation_stage::after_call,
				view_transaction.context_.flags);

			// Bootstrap observation remains after the original call. Production view
			// publication is already complete before entering the generator; never
			// publish it again here after the backend may have consumed the scene.
			if (view_transaction.context_.token && after.frontend != 0 &&
				scene_record_index < engine_view_probe::frontend_record_capacity)
			{
				std::uintptr_t record_arena{};
				std::memcpy(&record_arena, reinterpret_cast<const void*>(after.frontend +
					frontend_record_arena_pointer_offset), sizeof(record_arena));
				if (record_arena != 0)
				{
					const auto record = record_arena +
						static_cast<std::uintptr_t>(scene_record_index) *
						engine_view_probe::frontend_record_stride;
					std::uint32_t record_type{};
					std::memcpy(&record_type, reinterpret_cast<const void*>(record +
						native_render_contract::record_type_offset), sizeof(record_type));
					if (!engine_stereo_execution::bootstrap_complete())
					{
						(void)engine_backend_probe::publish_frontend_record({
							after.frontend,
							record,
							scene_record_index,
							record_type,
							view_transaction.context_.token.frontend_frame_id,
							view_transaction.context_.token.transaction_id,
						});
					}
				}
			}
		}

		void publish_execution_backend_context() noexcept
		{
			if (!active_backend_transaction)
			{
				engine_stereo_execution::clear_backend_record_context();
				return;
			}
			engine_stereo_execution::set_backend_record_context({
				active_backend_transaction.backend_id,
				active_backend_transaction.frontend_epoch,
				active_backend_transaction.frontend_transaction_id,
				active_backend_transaction.record,
				active_backend_transaction.frontend,
				active_backend_transaction.command_stream,
				active_backend_transaction.record_index,
				active_backend_transaction.record_type,
				active_backend_transaction.target_id,
				active_backend_transaction.owner_thread_id,
				active_backend_transaction.dispatch_entered,
			});
		}

		void release_active_stereo_binding(
			const bool command_dispatch_returned = false) noexcept
		{
			engine_stereo_execution::clear_backend_record_context();
			if (active_draw_indexed)
			{
				engine_stereo_draw_indexed::end(active_draw_indexed,
					command_dispatch_returned);
			}
			if (active_output_merger)
			{
				engine_stereo_output_merger::end(active_output_merger,
					command_dispatch_returned);
			}
			if (active_backend_target_route)
			{
				engine_stereo_backend_target::end(active_backend_target_route,
					command_dispatch_returned);
			}
			if (active_backend_view_copy)
			{
				engine_stereo_backend_view::end(active_backend_view_copy,
					reinterpret_cast<const void*>(active_stereo_binding.record),
					command_dispatch_returned);
			}
			if (active_stereo_binding)
			{
				engine_stereo_binding::release(active_stereo_binding);
			}
		}

		bool backend_view_bind_probe_stub(void* const record)
		{
			// A retained token at the next record boundary is an incomplete prior
			// transaction. Close it explicitly; never carry an eye pair across H2
			// records or infer completion from elapsed time.
			if (active_backend_transaction)
			{
				engine_backend_probe::end_backend(active_backend_transaction,
					engine_backend_probe::backend_completion::incomplete, 0,
					engine_backend_probe::fault(
						engine_backend_probe::backend_fault::overlap));
				release_active_stereo_binding();
			}

			if (!engine_backend_probe::is_enabled() ||
				engine_stereo_execution::bootstrap_complete())
			{
				return reinterpret_cast<backend_view_bind_fn>(
					backend_view_bind_function)(record);
			}

			const auto record_address = reinterpret_cast<std::uintptr_t>(record);
			std::uintptr_t frontend{};
			if (record != nullptr)
			{
				std::memcpy(&frontend, reinterpret_cast<const void*>(record_address +
					native_render_contract::record_frontend_offset), sizeof(frontend));
			}
			active_backend_transaction = engine_backend_probe::begin_backend(
				record_address);
			publish_execution_backend_context();
			if (engine_stereo_binding::has_publications())
			{
				active_stereo_binding = engine_stereo_binding::acquire(record_address,
					frontend);
				if (active_stereo_binding)
				{
					(void)engine_stereo_backend_view::begin(active_backend_view_copy,
						active_stereo_binding, record_address);
					(void)engine_stereo_backend_target::begin(active_backend_target_route,
						active_stereo_binding, record_address);
					(void)engine_stereo_output_merger::begin(active_output_merger,
						active_stereo_binding, record_address);
					(void)engine_stereo_draw_indexed::begin(active_draw_indexed,
						active_stereo_binding, record_address);
					// The old same-device command replay is intentionally not armed.
					// Captured evidence proved this transaction is a fullscreen
					// post-processing chain, and replaying it both adds GPU work and
					// contaminates the exact classifier census below.
				}
			}

			if (active_stereo_binding)
			{
				const auto& left = active_stereo_binding.views.eyes[0];
				const auto& right = active_stereo_binding.views.eyes[1];
				const std::array<std::uint64_t, evidence_metadata_count> metadata{
					GetTickCount64(),
					record_address,
					frontend,
					active_stereo_binding.publication_sequence,
					active_stereo_binding.frontend_epoch,
					active_stereo_binding.frontend_transaction_id,
					left.pair_id,
					left.publication,
				};
				(void)capture_artifact(backend_view_source_before_artifact,
					record_address, metadata);
				(void)capture_artifact(backend_bound_eye_slot_artifacts[0],
					reinterpret_cast<std::uintptr_t>(left.bytes.data()), metadata);
				(void)capture_artifact(backend_bound_eye_slot_artifacts[1],
					reinterpret_cast<std::uintptr_t>(right.bytes.data()), metadata);
			}

			const auto result = reinterpret_cast<backend_view_bind_fn>(
				backend_view_bind_function)(record);
			if (active_stereo_binding)
			{
				const auto& left = active_stereo_binding.views.eyes[0];
				const std::array<std::uint64_t, evidence_metadata_count> metadata{
					GetTickCount64(),
					record_address,
					frontend,
					active_stereo_binding.publication_sequence,
					active_stereo_binding.frontend_epoch,
					active_stereo_binding.frontend_transaction_id,
					left.pair_id,
					left.publication,
				};
				(void)capture_artifact(backend_view_source_after_artifact,
					record_address, metadata);
			}
			return result;
		}

		thread_local void* native_display_record{};
		thread_local bool native_geometry_view_setup{};
		thread_local std::uint32_t native_display_routes{};
		thread_local native_display_contract::route native_display_route{};

		std::uint32_t postfx_destination_stub(void* const record)
		{
			if (record != nullptr && record == native_display_record)
			{
				++native_display_routes;
				return native_display_route.destination;
			}
			return reinterpret_cast<std::uint32_t(*)(void*)>(postfx_destination_function)(record);
		}

		bool render_native_display(void* const record, const native_display_contract::route route)
		{
			if (!record || native_display_record || !route) return false;
			native_display_route = route;
			native_display_record = record;
			native_display_routes = 0;
			const auto scope = gsl::finally([]() noexcept
			{
				native_display_record = nullptr;
				native_display_route = {};
				native_display_routes = 0;
			});
			// RDX is unused by this exact H2 owner. R8 selects the target-scaled
			// viewport; both ping-pong targets match vidConfig.scene.
			reinterpret_cast<backend_record_classifier_fn>(backend_record_classifier_function)(
				record, nullptr, true);
			return native_display_routes == 1 && native_fullscreen_blur::apply(record,route);
		}

		void backend_record_classifier_probe_stub(void* const record,
			void* const frontend, const bool dimensions_match)
		{
			engine_stereo_execution::classifier_scope execution_scope{};
			const auto record_address = reinterpret_cast<std::uintptr_t>(record);
			const auto claimed_record = active_stereo_binding &&
				active_backend_transaction &&
				active_stereo_binding.record == record_address &&
				active_backend_transaction.record == record_address &&
				active_stereo_binding.record_type ==
					native_render_contract::expected_world_record_type &&
				active_backend_transaction.record_type ==
					native_render_contract::expected_world_record_type;
			if (claimed_record)
			{
				(void)engine_stereo_execution::begin_classifier(execution_scope,
					record_address, active_backend_transaction.record_type);
			}
			const auto execution_exit = gsl::finally([&]
			{
				if (execution_scope.active)
				{
					(void)engine_stereo_execution::end_classifier(execution_scope);
				}
			});

			// This is an exact call-site wrapper around H2's natural type-4 record
			// classifier. It never replays, suppresses, or adds renderer work.
			reinterpret_cast<backend_record_classifier_fn>(
				backend_record_classifier_function)(record, frontend, dimensions_match);
		}

		void backend_geometry_view_setup_stub(void* const backend_state,
			const void* const parameters, const void* const primary_view,
			const void* const rebase_view)
		{
			// 787230 prepares one scene draw-list executor, then calls its native
			// callback. Mark only its view initialization; fullscreen/query users
			// of the same 78A340 function remain outside the dynamic-mesh scope.
			const auto previous = native_geometry_view_setup;
			native_geometry_view_setup = true;
			const auto restore = gsl::finally([previous]() noexcept
			{
				native_geometry_view_setup = previous;
			});
			reinterpret_cast<backend_view_setup_fn>(backend_view_setup_function)(
				backend_state, parameters, primary_view, rebase_view);
		}

		void backend_view_copy_probe_stub(void* const backend_state)
		{
			if (native_display_record)
			{
				// A fullscreen PostFX view is not another scene/model dispatch.
				// In particular it must not extend the matched dynamic-index sequence
				// after end_eye has restored the natural arena cursor.
				reinterpret_cast<backend_view_copy_fn>(backend_view_copy_function)(backend_state);
				return;
			}
			const auto previous_copy_calls = active_backend_view_copy.copy_calls;
			const auto previous_substitutions = active_backend_view_copy.substitutions;
			engine_stereo_backend_view::invoke_copy(active_backend_view_copy,
				backend_state, reinterpret_cast<backend_view_copy_fn>(
					backend_view_copy_function));
			// B5D0 has now published both H2 eye-origin coordinate spaces. The
			// production owner validates those exact bytes and invalidates only the
			// XModel placement-pointer cache whose key does not include eye identity.
			std::uintptr_t view_setup_caller{};
			if (backend_view_setup_stack_verified)
			{
				// Exact 78A340 ABI: push rdi, sub rsp,30h, then this call pushes
				// another return address. The caller of view setup is therefore
				// 40h above OUR return slot. Preserve it as evidence only; do not
				// assume all subviews share this hook's constant 78A404 return PC.
				std::memcpy(&view_setup_caller,
					static_cast<const std::uint8_t*>(_AddressOfReturnAddress()) + 0x40,
					sizeof(view_setup_caller));
			}
			engine_stereo_owner_pass::note_backend_view_copy(backend_state,
				native_geometry_view_setup, view_setup_caller);
			if (active_backend_view_copy &&
				active_backend_view_copy.copy_calls > previous_copy_calls)
			{
				const engine_stereo_backend_target::selection_scope scope{
					active_backend_transaction.backend_id,
					active_stereo_binding.publication_sequence,
					active_backend_transaction.record,
					active_backend_transaction.record_type,
					static_cast<bool>(active_backend_transaction),
					static_cast<bool>(active_stereo_binding),
				};
				engine_stereo_backend_target::record_view_copy(
					active_backend_target_route,
					reinterpret_cast<std::uintptr_t>(_ReturnAddress()), scope,
					active_backend_view_copy.copy_calls,
					active_backend_view_copy.substitutions,
					active_backend_view_copy.selected_eye,
					active_backend_view_copy.substitutions > previous_substitutions);
				engine_stereo_output_merger::note_view_copy(active_output_merger,
					active_backend_view_copy.copy_calls,
					active_backend_view_copy.substitutions,
					active_backend_view_copy.selected_eye);
			}
		}

		void backend_depth_hack_projection_stub(void* const backend_state)
		{
			backend_depth_hack_projection_hook.invoke<void>(backend_state);
			if (native_display_record) return;
			// The original function has now published both its projection-cache
			// version and H2's dedicated depth-hack near clip. Production correction
			// is authorized only by the exact active owner/eye/backend identity.
			engine_stereo_owner_pass::note_backend_depth_hack_projection(backend_state);
		}

		void backend_target_select_probe_stub(void* const context,
			const std::uint32_t target_id)
		{
			if (native_display_record)
			{
				backend_target_select_hook.invoke<void>(context, target_id);
				return;
			}
			const auto graphics = d3d11::get_device_snapshot();
			const auto binding_before =
				engine_stereo_output_merger::get_current_binding(graphics.context.Get());
			const engine_stereo_backend_target::selection_scope scope{
				active_backend_transaction.backend_id,
				active_stereo_binding.publication_sequence,
				active_backend_transaction.record,
				active_backend_transaction.record_type,
				static_cast<bool>(active_backend_transaction),
				static_cast<bool>(active_stereo_binding),
			};
			engine_stereo_output_merger::note_target(active_output_merger, target_id);
			engine_stereo_backend_target::invoke_select(active_backend_target_route,
				context, target_id, reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
				scope,
				reinterpret_cast<const void*>(native_render_contract::target_registry_base),
				native_render_contract::target_registry_capacity,
				+[](void* const original_context, const std::uint32_t original_target)
				{
					backend_target_select_hook.invoke<void>(original_context, original_target);
				});
			if (active_owner_pass)
			{
				engine_stereo_owner_pass::note_target_selection(active_owner_pass,
					target_id, graphics.context.Get(), binding_before.sequence);
			}
		}

		void backend_post_bind_probe_stub(void* const record,
			const std::uint32_t target_id)
		{
			const auto observation_enabled = engine_backend_probe::is_enabled() &&
				!engine_stereo_execution::bootstrap_complete();
			const auto record_address = reinterpret_cast<std::uintptr_t>(record);
			if (active_backend_transaction &&
				active_backend_transaction.record != record_address)
			{
				engine_backend_probe::end_backend(active_backend_transaction,
					engine_backend_probe::backend_completion::incomplete, 0,
					engine_backend_probe::fault(
						engine_backend_probe::backend_fault::record_mismatch));
				release_active_stereo_binding();
			}
			if (!observation_enabled)
			{
				reinterpret_cast<backend_post_bind_fn>(backend_post_bind_function)(
					record, target_id);
				return;
			}

			if (!active_backend_transaction)
			{
				active_backend_transaction = engine_backend_probe::begin_backend(
					record_address);
				publish_execution_backend_context();
			}
			reinterpret_cast<backend_post_bind_fn>(backend_post_bind_function)(
				record, target_id);
			engine_backend_probe::record_post_bind(active_backend_transaction,
				record_address, target_id);
			publish_execution_backend_context();
		}

		void backend_dynamic_upload_stub(void* const data)
		{
			const auto original = reinterpret_cast<void(*)(void*)>(
				backend_dynamic_upload_function);
			if (!dynamic_upload_owner_scope) original(data);
			else engine_stereo_owner_pass::dynamic_upload_boundary(data, original);
		}

		void backend_target_prepare_probe_stub(void* const record)
		{
			const region_capture::phase_scope region_backend(region_capture::phase::backend, record);
			thread_local bool observation_active{};
			if (!engine_backend_probe::is_enabled() || observation_active)
			{
				// Reentrant/foreign owners do not inherit the outer stereo lifecycle.
				const auto previous = dynamic_upload_owner_scope;
				dynamic_upload_owner_scope = false;
				const auto scope_exit = gsl::finally([previous]() noexcept
				{
					dynamic_upload_owner_scope = previous;
				});
				backend_target_prepare_hook.invoke<void>(record);
				return;
			}
			observation_active = true;
			const auto observation_exit = gsl::finally([]
			{
				observation_active = false;
			});

			constexpr auto record_size = engine_view_probe::frontend_record_stride;
			const auto capture_observation = !engine_stereo_execution::bootstrap_complete();
			constexpr auto changed_block_size = std::size_t{0x400};
			thread_local std::array<std::uint8_t, record_size> before_bytes{};
			thread_local std::array<std::uint8_t, record_size> after_bytes{};
			engine_backend_probe::target_prepare_observation observation{};
			observation.record = reinterpret_cast<std::uintptr_t>(record);
			observation.caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			observation.record_index = engine_view_probe::invalid_slot_index;
			observation.first_changed = engine_view_probe::invalid_slot_index;
			observation.last_changed = engine_view_probe::invalid_slot_index;

			observation.frontend = reinterpret_cast<std::uintptr_t>(
				*reinterpret_cast<void**>(backend_frontend_data_pointer));
			std::uintptr_t record_arena{};
			std::uint32_t record_count{};
			if (observation.frontend != 0)
			{
				std::memcpy(&record_arena, reinterpret_cast<const void*>(
					observation.frontend + frontend_record_arena_pointer_offset),
					sizeof(record_arena));
				std::memcpy(&record_count, reinterpret_cast<const void*>(
					observation.frontend + engine_view_probe::frontend_record_count_offset),
					sizeof(record_count));
			}
			if (record_arena != 0 && observation.record >= record_arena)
			{
				const auto delta = observation.record - record_arena;
				const auto index = delta / record_size;
				observation.record_valid = delta % record_size == 0 &&
					index < native_render_contract::frontend_record_capacity &&
					index < record_count;
				if (observation.record_valid)
				{
					observation.record_index = static_cast<std::uint32_t>(index);
					if (capture_observation)
					{
						std::memcpy(before_bytes.data(), record, before_bytes.size());
					}
					std::memcpy(&observation.record_type, static_cast<const std::uint8_t*>(record) +
						native_render_contract::record_type_offset,
						sizeof(observation.record_type));
					for (std::size_t target_index{};
						target_index < observation.targets_before.size(); ++target_index)
					{
						std::memcpy(&observation.targets_before[target_index], static_cast<const std::uint8_t*>(record) +
						native_render_contract::record_target_0_offset +
							target_index * sizeof(std::uint32_t),
							sizeof(std::uint32_t));
					}
					std::memcpy(&observation.selector_before, static_cast<const std::uint8_t*>(record) +
						native_render_contract::record_target_selector_offset,
						sizeof(observation.selector_before));
					const std::array<std::uint64_t, evidence_metadata_count> metadata{
						GetTickCount64(),
						observation.record,
						observation.frontend,
						observation.record_index,
						observation.record_type,
						observation.caller,
						engine_backend_probe::pack_u32_pair(
							observation.targets_before[0], observation.targets_before[1]),
						engine_backend_probe::pack_u32_pair(
							observation.targets_before[2], observation.selector_before),
					};
					if (capture_observation)
					{
						(void)capture_artifact(record_before_target_prepare_artifact,
							reinterpret_cast<std::uintptr_t>(before_bytes.data()), metadata);
						(void)capture_artifact(registry_before_target_prepare_artifact,
							native_render_contract::target_registry_base, metadata);
					}
				}
			}

			// The natural owner remains mandatory H2 state progression. Bootstrap
			// first proves an isolated stereo transaction with readback. Production
			// then admits complete eye pairs through that same owner boundary; only
			// its native targets, never the ordinary desktop output, reach OpenVR.
			const auto invoke_owner = [&](void* const owner_record,
				const bool arena_record)
			{
				const auto owner_observation_id =
					target_prepare_owner_sequence.fetch_add(1,
						std::memory_order_relaxed) + 1;
				engine_stereo_execution::set_scene_owner_context({
					owner_observation_id,
					reinterpret_cast<std::uintptr_t>(owner_record),
					observation.frontend,
					observation.caller,
					observation.record_index,
					observation.record_type,
					observation.targets_before,
					observation.selector_before,
					static_cast<std::uint32_t>(GetCurrentThreadId()),
					arena_record && observation.record_valid,
				});
				const auto previous_upload_scope = dynamic_upload_owner_scope;
				dynamic_upload_owner_scope = active_owner_pass &&
					active_owner_pass.current_view < auxiliary_scene::view_count;
				const auto owner_exit = gsl::finally([previous_upload_scope]() noexcept
				{
					dynamic_upload_owner_scope = previous_upload_scope;
					engine_stereo_execution::clear_scene_owner_context();
				});
				backend_target_prepare_hook.invoke<void>(owner_record);
			};

			bool natural_owner_executed{};
			if (observation.record_valid && observation.record_type ==
				native_render_contract::expected_world_record_type &&
				engine_stereo_execution::bootstrap_complete())
			{
				// After bootstrap, this natural owner is the sole consumer. Retire
				// its CPU publication even if GPU admission is closed: otherwise
				// obsolete records accumulate in the ring and outlive H2's owner.
				engine_scene_job_capture::checkpoint(record, observation.frontend, region_capture::phase::claim);
				region_capture::phase_scope region_claim(region_capture::phase::claim, record, observation.frontend);
				auto claim = engine_stereo_binding::acquire_current(record,
					observation.frontend);
				region_claim.finish(claim.publication_sequence);
				const auto release_claim = gsl::finally([&]() noexcept
				{
					if (claim) engine_stereo_binding::release(claim);
				});
				if (claim && engine_scene_resolution::ready() && engine_stereo_owner_pass::ready_to_claim())
				{
					const auto graphics = d3d11::get_device_snapshot();
					if (engine_stereo_owner_pass::begin(active_owner_pass,
						std::move(claim), record, graphics))
					{
						// Drain a deferred H2 upload even on an early C++ unwind. Normal
						// end() retires the transaction, making this guard a no-op.
						const auto pair_exit = gsl::finally([]() noexcept
						{
							if (active_owner_pass) engine_stereo_owner_pass::end(active_owner_pass);
						});
						const auto timing_pair_id = active_owner_pass.records.pair_id;
						const auto timing_generation = graphics.generation;
						auto* const timing_context = graphics.context.Get();
						bool gpu_timing_pair{};
						if constexpr (engine_stereo_gpu_timing::instrumentation_enabled)
						{
							const auto native_status = native_render_session::active().get_status();
							const bool stable_native_pair = active_owner_pass.production &&
								native_status.available && native_status.copy_ring &&
								native_status.accepting_pairs &&
								native_status.expected_pair_id == timing_pair_id &&
								native_status.device_generation == timing_generation &&
								native_status.pair_releases >=
									engine_stereo_gpu_timing::minimum_stable_pair_releases &&
								native_status.pair_acquires == native_status.pair_releases &&
								native_status.conversion_completions >=
									native_status.pair_releases * 2 &&
								native_status.capture_failures == 0 &&
								native_status.conversion_failures == 0 &&
								native_status.command_list_build_failures == 0 &&
								native_status.pair_quarantines == 0 &&
								native_status.pair_exhaustions == 0 &&
								native_status.pair_rejections == 0;
							gpu_timing_pair = active_owner_pass.production &&
								engine_stereo_gpu_timing::begin_pair(timing_pair_id,
									timing_context, timing_generation, stable_native_pair);
						}
						if (!active_owner_pass.production)
						{
							console::info("[VR] isolated outer-owner stereo GPU proof started "
								"(pair=%llu publication=%llu); OpenVR submit remains unarmed\n",
								static_cast<unsigned long long>(active_owner_pass.records.pair_id),
								static_cast<unsigned long long>(
									active_owner_pass.records.publication));
						}
						auto* const left_record = engine_stereo_owner_pass::begin_view(
							active_owner_pass, 0);
						if (left_record != nullptr)
						{
							engine_stereo_owner_pass::begin_owner_invoke(
								active_owner_pass, 0);
							if (gpu_timing_pair)
							{
								(void)engine_stereo_gpu_timing::begin_owner(timing_pair_id,
									0, timing_context, timing_generation);
							}
							{
								const auto timing_exit = gsl::finally([&]() noexcept
								{
									if (gpu_timing_pair)
									{
										(void)engine_stereo_gpu_timing::end_owner(
											timing_pair_id, 0, timing_context,
											timing_generation);
									}
									engine_stereo_owner_pass::end_owner_invoke(
										active_owner_pass, 0);
								});
								invoke_owner(left_record, true);
							}
							natural_owner_executed = true;
							if (engine_stereo_owner_pass::end_view(active_owner_pass, 0, render_native_display))
							{
								if (!active_owner_pass.production)
								{
									console::info("[VR] isolated outer-owner left eye returned "
										"with final scene target captured\n");
								}
								bool auxiliary_ready = true;
								if (active_owner_pass.auxiliary.valid)
								{
									auto* const auxiliary_record = engine_stereo_owner_pass::begin_view(
										active_owner_pass, auxiliary_scene::view_index);
									auxiliary_ready = auxiliary_record != nullptr;
									if (auxiliary_record)
									{
										engine_stereo_owner_pass::begin_owner_invoke(active_owner_pass, auxiliary_scene::view_index);
										{
											const auto auxiliary_exit = gsl::finally([&]() noexcept
											{
												engine_stereo_owner_pass::end_owner_invoke(active_owner_pass, auxiliary_scene::view_index);
											});
											invoke_owner(auxiliary_record, false);
										}
										auxiliary_ready = engine_stereo_owner_pass::end_view(
											active_owner_pass, auxiliary_scene::view_index, render_native_display) &&
											engine_stereo_owner_pass::finish_pending_left(active_owner_pass);
									}
								}
								auto* const right_record = auxiliary_ready ?
									engine_stereo_owner_pass::begin_view(active_owner_pass, 1) : nullptr;
								if (right_record != nullptr)
								{
									engine_stereo_owner_pass::begin_owner_invoke(
										active_owner_pass, 1);
									if (gpu_timing_pair)
									{
										(void)engine_stereo_gpu_timing::begin_owner(timing_pair_id,
											1, timing_context, timing_generation);
									}
									{
										const auto timing_exit = gsl::finally([&]() noexcept
										{
											if (gpu_timing_pair)
											{
												(void)engine_stereo_gpu_timing::end_owner(
													timing_pair_id, 1, timing_context,
													timing_generation);
											}
											engine_stereo_owner_pass::end_owner_invoke(
												active_owner_pass, 1);
										});
										invoke_owner(right_record, false);
									}
									if (engine_stereo_owner_pass::end_view(
										active_owner_pass, 1, render_native_display))
									{
										if (!active_owner_pass.production)
										{
											console::info("[VR] isolated outer-owner right eye returned "
												"with final scene target captured\n");
										}
									}
								}
							}
						}
						engine_stereo_owner_pass::end(active_owner_pass);
						if (gpu_timing_pair)
						{
							const auto published = native_render_session::active().pair_published(
								timing_pair_id);
							engine_stereo_gpu_timing::finish_pair(timing_pair_id,
								timing_context, timing_generation, published);
						}
						}
				}
			}
			if (!natural_owner_executed)
			{
				invoke_owner(record, true);
			}

			if (capture_observation && observation.record_valid)
			{
				std::memcpy(after_bytes.data(), record, after_bytes.size());
				for (std::size_t index{}; index < after_bytes.size(); ++index)
				{
					if (before_bytes[index] == after_bytes[index]) continue;
					if (observation.changed_bytes++ == 0)
					{
						observation.first_changed = static_cast<std::uint32_t>(index);
					}
					observation.last_changed = static_cast<std::uint32_t>(index);
					observation.changed_block_mask |= std::uint64_t{1} <<
						(index / changed_block_size);
				}
				for (std::size_t target_index{};
					target_index < observation.targets_after.size(); ++target_index)
				{
					std::memcpy(&observation.targets_after[target_index], after_bytes.data() +
						native_render_contract::record_target_0_offset +
							target_index * sizeof(std::uint32_t),
						sizeof(std::uint32_t));
				}
				std::memcpy(&observation.selector_after, after_bytes.data() +
					native_render_contract::record_target_selector_offset,
					sizeof(observation.selector_after));
				const std::array<std::uint64_t, evidence_metadata_count> metadata{
					GetTickCount64(),
					observation.record,
					observation.frontend,
					observation.record_index,
					observation.record_type,
					observation.caller,
					engine_backend_probe::pack_u32_pair(
						observation.targets_after[0], observation.targets_after[1]),
					engine_backend_probe::pack_u32_pair(
						observation.targets_after[2], observation.selector_after),
				};
				(void)capture_artifact(record_after_target_prepare_artifact,
					reinterpret_cast<std::uintptr_t>(after_bytes.data()), metadata);
				(void)capture_artifact(registry_after_target_prepare_artifact,
					native_render_contract::target_registry_base, metadata);
			}
			if (capture_observation)
			{
				engine_backend_probe::record_target_prepare(observation);
			}
		}

		void backend_command_dispatch_probe_stub(void* const commands,
			const int* const filter, const bool flagged_mode)
		{
			if ((!engine_backend_probe::is_enabled() ||
				engine_stereo_execution::bootstrap_complete()) && !active_backend_transaction)
			{
				reinterpret_cast<backend_command_dispatch_fn>(
					backend_command_dispatch_function)(commands, filter, flagged_mode);
				return;
			}
			engine_backend_probe::record_dispatch(active_backend_transaction,
				reinterpret_cast<std::uintptr_t>(commands),
				engine_backend_probe::observation_stage::before_call);
			publish_execution_backend_context();
			engine_stereo_backend_target::enter_dispatch(active_backend_target_route);
			engine_stereo_output_merger::enter_dispatch(active_output_merger);
			engine_stereo_draw_indexed::enter_dispatch(active_draw_indexed, commands);
			bool dispatch_observers_active = true;
			const auto dispatch_exit = gsl::finally([&]
			{
				if (!dispatch_observers_active) return;
				engine_stereo_draw_indexed::leave_dispatch(active_draw_indexed,
					reinterpret_cast<const void*>(active_backend_transaction.command_stream));
				engine_stereo_output_merger::leave_dispatch(active_output_merger);
				engine_stereo_backend_target::leave_dispatch(active_backend_target_route);
			});
			reinterpret_cast<backend_command_dispatch_fn>(
				backend_command_dispatch_function)(commands, filter, flagged_mode);
			engine_backend_probe::record_dispatch(active_backend_transaction,
				reinterpret_cast<std::uintptr_t>(commands),
				engine_backend_probe::observation_stage::after_call);
			publish_execution_backend_context();
			engine_stereo_draw_indexed::leave_dispatch(active_draw_indexed,
				reinterpret_cast<const void*>(active_backend_transaction.command_stream));
			engine_stereo_output_merger::leave_dispatch(active_output_merger);
			engine_stereo_backend_target::leave_dispatch(active_backend_target_route);
			dispatch_observers_active = false;
		}

		void backend_record_probe_stub(void* const record)
		{
			if ((!engine_backend_probe::is_enabled() ||
				engine_stereo_execution::bootstrap_complete()) && !active_backend_transaction)
			{
				reinterpret_cast<backend_record_fn>(backend_record_function)(record);
				return;
			}

			const auto record_address = reinterpret_cast<std::uintptr_t>(record);
			if (!active_backend_transaction ||
				active_backend_transaction.record != record_address)
			{
				if (active_backend_transaction)
				{
					engine_backend_probe::end_backend(active_backend_transaction,
						engine_backend_probe::backend_completion::incomplete, 0,
						engine_backend_probe::fault(
							engine_backend_probe::backend_fault::record_mismatch));
					release_active_stereo_binding();
				}
				active_backend_transaction = engine_backend_probe::begin_backend(
					record_address);
				publish_execution_backend_context();
			}
			else
			{
				publish_execution_backend_context();
			}

			const auto expected_commands = active_backend_transaction.command_stream;
			reinterpret_cast<backend_record_fn>(backend_record_function)(record);
			const auto completion = expected_commands == 0
				? engine_backend_probe::backend_completion::dispatch_skipped_null
				: (active_backend_transaction.dispatch_returned
					? engine_backend_probe::backend_completion::cpu_dispatch_return
					: engine_backend_probe::backend_completion::incomplete);
			engine_backend_probe::end_backend(active_backend_transaction, completion,
				expected_commands);
			release_active_stereo_binding(completion ==
				engine_backend_probe::backend_completion::cpu_dispatch_return);
		}

		void h2_query_publish_probe_stub(void* const query)
		{
			if (!engine_backend_probe::is_enabled())
			{
				reinterpret_cast<h2_query_publish_fn>(h2_query_publish_function)(query);
				return;
			}

			const auto before = *reinterpret_cast<const std::uint32_t*>(
				h2_query_publish_generation);
			reinterpret_cast<h2_query_publish_fn>(h2_query_publish_function)(query);
			const auto after = *reinterpret_cast<const std::uint32_t*>(
				h2_query_publish_generation);
			(void)engine_backend_probe::record_query_publish(
				reinterpret_cast<std::uintptr_t>(query), before, after,
				frontend_epoch_sequence.load(std::memory_order_relaxed));
		}

		void record_h2_query_result(const std::uintptr_t query,
			const std::uint32_t generation, const std::int32_t result,
			const std::uint32_t complete) noexcept
		{
			(void)engine_backend_probe::record_query_result(query, generation,
				result, complete != 0);
		}

		template <std::size_t Size>
		bool write_code_bytes(const std::uintptr_t address,
			const std::array<std::uint8_t, Size>& bytes) noexcept
		{
			DWORD old_protection{};
			auto* const destination = reinterpret_cast<void*>(address);
			if (!VirtualProtect(destination, Size, PAGE_EXECUTE_READWRITE, &old_protection))
			{
				return false;
			}

			std::memcpy(destination, bytes.data(), Size);
			const auto flushed = FlushInstructionCache(GetCurrentProcess(), destination, Size) != FALSE;
			DWORD ignored{};
			const auto restored = VirtualProtect(destination, Size, old_protection, &ignored) != FALSE;
			const auto validation = utils::hook_validation::validate_executable_target(destination);
			return flushed && restored && validation && validation.protection == old_protection;
		}

		bool restore_code_protection(const std::uintptr_t address,
			const std::size_t size, const std::uint32_t protection) noexcept
		{
			DWORD ignored{};
			if (!VirtualProtect(reinterpret_cast<void*>(address), size, protection, &ignored))
			{
				return false;
			}
			const auto validation = utils::hook_validation::validate_executable_target(
				reinterpret_cast<const void*>(address));
			return validation && validation.protection == protection;
		}

		template <std::size_t Size>
		bool code_bytes_match(const std::uintptr_t address,
			const std::array<std::uint8_t, Size>& bytes) noexcept
		{
			constexpr std::array<std::uint8_t, Size> mask = []
			{
				std::array<std::uint8_t, Size> result{};
				result.fill(0xFF);
				return result;
			}();
			return static_cast<bool>(utils::hook_validation::verify_masked_bytes(
				reinterpret_cast<const void*>(address), {bytes.data(), mask.data(), Size}));
		}

		bool build_relative_call(const std::uintptr_t call_site, const void* const replacement,
			std::array<std::uint8_t, 5>& output) noexcept
		{
			if (utils::hook::is_relatively_far(reinterpret_cast<const void*>(call_site),
				replacement))
			{
				return false;
			}
			const auto displacement = static_cast<std::int32_t>(
				reinterpret_cast<std::intptr_t>(replacement) -
				static_cast<std::intptr_t>(call_site + output.size()));
			output[0] = 0xE8;
			std::memcpy(output.data() + 1, &displacement, sizeof(displacement));
			return true;
		}

		struct backend_observation_hook_state
		{
			std::array<std::uint32_t, 11> original_protections{};
			void* query_result_thunk{};
			void* query_result_relay{};
			bool prepared{};
			bool installed{};
		};

		backend_observation_hook_state backend_hooks{};

		void* build_h2_query_result_thunk() noexcept
		{
			try
			{
				return utils::hook::assemble([](utils::hook::assembler& a)
				{
					// Entry is an inserted CALL immediately after H2's original GetData.
					// EAX=HRESULT, R14=query, EBX=consume generation, and the BOOL
					// output is at entry RSP+0x68. Replay the displaced global load
					// before calling C++ so its concurrent-read position is unchanged.
					a.push(rax);
					a.mov(rax, h2_query_result_original_global);
					a.mov(ecx, dword_ptr(rax));
					a.push(rcx);
					a.mov(rcx, r14);
					a.mov(edx, ebx);
					a.mov(r8d, dword_ptr(rsp, 0x08));
					a.mov(r9d, dword_ptr(rsp, 0x78));
					// Two pushes leave RSP 8 mod 16. Reserve the mandatory 0x20-byte
					// home area plus 8 bytes so the nested call site is 16-byte aligned.
					a.sub(rsp, 0x28);
					a.mov(rax, reinterpret_cast<std::uint64_t>(&record_h2_query_result));
					a.call(rax);
					a.add(rsp, 0x28);
					a.pop(rcx);
					a.pop(rax);
					a.ret();
				});
			}
			catch (...)
			{
				return nullptr;
			}
		}

		void* build_preserving_near_relay(void* const target) noexcept
		{
			return utils::hook::create_preserving_near_jump(0x140000000, target);
		}

		template <std::size_t Size>
		bool validate_observation_patch(const char* const name,
			const std::uintptr_t site, const std::array<std::uint8_t, Size>& original,
			const void* const replacement) noexcept
		{
			if (!code_bytes_match(site, original))
			{
				console::error("[VR] %s byte validation failed at +0x%llx\n", name,
					static_cast<unsigned long long>(site - 0x140000000));
				return false;
			}
			if (replacement != nullptr && utils::hook::is_relatively_far(
				reinterpret_cast<const void*>(site), replacement))
			{
				console::error("[VR] %s replacement is outside rel32 range at +0x%llx\n",
					name, static_cast<unsigned long long>(site - 0x140000000));
				return false;
			}
			return true;
		}

		bool restore_backend_observation_hooks() noexcept
		{
			const auto depth_hack_projection = backend_depth_hack_projection_hook.clear();
			const auto target_select = backend_target_select_hook.clear();
			const auto target_prepare = backend_target_prepare_hook.clear();
			if (!backend_hooks.prepared)
				return depth_hack_projection && target_select && target_prepare;
			constexpr std::array<std::uint8_t, 5> post_bind_bytes{
				0xE8, 0xB2, 0xEE, 0xFF, 0xFF};
			constexpr std::array<std::uint8_t, 5> backend_record_bytes{
				0xE8, 0x3A, 0xE7, 0xFF, 0xFF};
			constexpr std::array<std::uint8_t, 5> backend_view_bind_bytes{
				0xE8, 0x14, 0x73, 0xFF, 0xFF};
			constexpr std::array<std::uint8_t, 5> backend_record_classifier_bytes{
				0xE8, 0x55, 0x83, 0x00, 0x00};
			constexpr std::array<std::uint8_t, 5> backend_view_copy_bytes{
				0xE8, 0xCC, 0x11, 0x00, 0x00};
			constexpr std::array<std::uint8_t, 5> command_dispatch_bytes{
				0xE8, 0xD6, 0x7D, 0xFF, 0xFF};
			constexpr std::array<std::uint8_t, 5> query_publish_bytes{
				0xE8, 0x36, 0xA6, 0xFA, 0xFF};
			constexpr std::array<std::uint8_t, 6> query_result_bytes{
				0x8B, 0x0D, 0xF3, 0x11, 0x4B, 0x00};

			// Restore producers before consumers. No runtime unpatch path calls this;
			// it is exclusively startup rollback while renderer work is quiescent.
			const auto post_bind = write_code_bytes(backend_post_bind_call_site,
				post_bind_bytes);
			const auto backend_record = write_code_bytes(backend_record_call_site,
				backend_record_bytes);
			const auto view_bind = write_code_bytes(backend_view_bind_call_site,
				backend_view_bind_bytes);
			const auto record_classifier = write_code_bytes(
				backend_record_classifier_call_site, backend_record_classifier_bytes);
			const auto view_copy = write_code_bytes(backend_view_copy_call_site,
				backend_view_copy_bytes);
			const auto geometry_view_setup = write_code_bytes(backend_geometry_view_setup_call_site,
				backend_geometry_view_setup_bytes);
			const auto dynamic_upload = write_code_bytes(backend_dynamic_upload_call_site,
				backend_dynamic_upload_bytes);
			const auto postfx_destination = write_code_bytes(postfx_destination_call_site,
				postfx_destination_bytes);
			const auto command = write_code_bytes(backend_command_dispatch_call_site,
				command_dispatch_bytes);
			const auto publisher = write_code_bytes(h2_query_publish_call_site,
				query_publish_bytes);
			const auto result = write_code_bytes(h2_query_result_observation_site,
				query_result_bytes);
			const std::array sites{
				h2_query_result_observation_site,
				h2_query_publish_call_site,
				backend_command_dispatch_call_site,
				backend_record_call_site,
				backend_view_bind_call_site,
				backend_record_classifier_call_site,
				backend_view_copy_call_site,
				backend_post_bind_call_site,
				backend_dynamic_upload_call_site,
				postfx_destination_call_site,
				backend_geometry_view_setup_call_site,
			};
			bool protections = true;
			for (std::size_t index{}; index < sites.size(); ++index)
			{
				protections = restore_code_protection(sites[index],
					index == 0 ? query_result_bytes.size() : query_publish_bytes.size(),
					backend_hooks.original_protections[index]) && protections;
			}
			const auto bytes = code_bytes_match(backend_post_bind_call_site,
				post_bind_bytes) && code_bytes_match(backend_record_call_site,
				backend_record_bytes) && code_bytes_match(
				backend_view_bind_call_site, backend_view_bind_bytes) && code_bytes_match(
				backend_record_classifier_call_site,
				backend_record_classifier_bytes) && code_bytes_match(
				backend_view_copy_call_site, backend_view_copy_bytes) && code_bytes_match(
				backend_command_dispatch_call_site, command_dispatch_bytes) &&
				code_bytes_match(h2_query_publish_call_site, query_publish_bytes) &&
				code_bytes_match(h2_query_result_observation_site, query_result_bytes) &&
				code_bytes_match(backend_dynamic_upload_call_site, backend_dynamic_upload_bytes) &&
				code_bytes_match(postfx_destination_call_site, postfx_destination_bytes) &&
				code_bytes_match(backend_geometry_view_setup_call_site, backend_geometry_view_setup_bytes);
			const auto complete = depth_hack_projection && target_select && target_prepare &&
				post_bind && backend_record &&
				view_bind && record_classifier &&
				view_copy && geometry_view_setup && dynamic_upload && postfx_destination && command &&
				publisher && result && protections && bytes;
			if (complete) backend_hooks.installed = false;
			return complete;
		}

		bool install_backend_observation_hooks()
		{
			constexpr std::array<std::uint8_t, 17> depth_hack_projection_prologue{
				0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
				0xEC, 0x20, 0x0F, 0x10, 0x81, 0x30, 0x2C, 0x00, 0x00};
			constexpr std::array<std::uint8_t, 16> target_prepare_prologue{
				0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
				0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57};
			constexpr std::array<std::uint8_t, 16> target_select_prologue{
				0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83,
				0xEC, 0x30, 0x48, 0x8B, 0x41, 0x08, 0x48, 0x8B};
			constexpr std::array<std::uint8_t, 5> post_bind_bytes{
				0xE8, 0xB2, 0xEE, 0xFF, 0xFF};
			constexpr std::array<std::uint8_t, 5> backend_record_bytes{
				0xE8, 0x3A, 0xE7, 0xFF, 0xFF};
			constexpr std::array<std::uint8_t, 5> backend_view_bind_bytes{
				0xE8, 0x14, 0x73, 0xFF, 0xFF};
			constexpr std::array<std::uint8_t, 5> backend_record_classifier_bytes{
				0xE8, 0x55, 0x83, 0x00, 0x00};
			constexpr std::array<std::uint8_t, 5> backend_view_copy_bytes{
				0xE8, 0xCC, 0x11, 0x00, 0x00};
			constexpr std::array<std::uint8_t, 5> command_dispatch_bytes{
				0xE8, 0xD6, 0x7D, 0xFF, 0xFF};
			constexpr std::array<std::uint8_t, 5> query_publish_bytes{
				0xE8, 0x36, 0xA6, 0xFA, 0xFF};
			constexpr std::array<std::uint8_t, 6> query_result_bytes{
				0x8B, 0x0D, 0xF3, 0x11, 0x4B, 0x00};

			// Optional diagnostic ABI: a mismatch disables only parent-PC capture.
			// The existing view-copy hook keeps its independent instruction checks.
			backend_view_setup_stack_verified = view_diagnostics_enabled() && code_bytes_match(
				backend_view_setup_function, backend_view_setup_prologue);
			backend_hooks.query_result_thunk = build_h2_query_result_thunk();
			backend_hooks.query_result_relay = build_preserving_near_relay(
				backend_hooks.query_result_thunk);
			if (backend_hooks.query_result_thunk == nullptr ||
				backend_hooks.query_result_relay == nullptr)
			{
				console::error("[VR] H2 query-result observation thunk could not be built\n");
				return false;
			}

			if (!validate_observation_patch("scene geometry view setup",
				backend_geometry_view_setup_call_site, backend_geometry_view_setup_bytes,
				reinterpret_cast<void*>(backend_geometry_view_setup_stub)) ||
				!utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(backend_view_setup_function)) ||
				!validate_observation_patch("native PostFX destination",
				postfx_destination_call_site, postfx_destination_bytes,
				reinterpret_cast<void*>(postfx_destination_stub)) ||
				!utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(postfx_destination_function)) ||
				!validate_observation_patch("backend dynamic upload",
				backend_dynamic_upload_call_site, backend_dynamic_upload_bytes,
				reinterpret_cast<void*>(backend_dynamic_upload_stub)) ||
				!utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(backend_dynamic_upload_function)) ||
				!validate_observation_patch("backend post-bind",
				backend_post_bind_call_site, post_bind_bytes,
				reinterpret_cast<void*>(backend_post_bind_probe_stub)) ||
				!validate_observation_patch("backend record",
					backend_record_call_site, backend_record_bytes,
					reinterpret_cast<void*>(backend_record_probe_stub)) ||
				!validate_observation_patch("backend view bind",
					backend_view_bind_call_site, backend_view_bind_bytes,
					reinterpret_cast<void*>(backend_view_bind_probe_stub)) ||
				!validate_observation_patch("backend record classifier",
					backend_record_classifier_call_site,
					backend_record_classifier_bytes,
					reinterpret_cast<void*>(backend_record_classifier_probe_stub)) ||
				!validate_observation_patch("backend view copy",
					backend_view_copy_call_site, backend_view_copy_bytes,
					reinterpret_cast<void*>(backend_view_copy_probe_stub)) ||
				!validate_observation_patch("backend command dispatcher",
				backend_command_dispatch_call_site, command_dispatch_bytes,
				reinterpret_cast<void*>(backend_command_dispatch_probe_stub)) ||
				!validate_observation_patch("H2 query publisher",
				h2_query_publish_call_site, query_publish_bytes,
				reinterpret_cast<void*>(h2_query_publish_probe_stub)) ||
				!validate_observation_patch("H2 GetData result",
				h2_query_result_observation_site, query_result_bytes,
				backend_hooks.query_result_relay))
			{
				return false;
			}
			if (!utils::hook_validation::validate_executable_target(
				reinterpret_cast<const void*>(backend_record_classifier_function)))
			{
				console::error("[VR] backend record-classifier target is not executable "
					"at +0x%llx\n", static_cast<unsigned long long>(
						backend_record_classifier_function - 0x140000000));
				return false;
			}
			if (!code_bytes_match(backend_depth_hack_projection_function,
				depth_hack_projection_prologue) ||
				!utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(backend_depth_hack_projection_function)))
			{
				console::error("[VR] backend depth-hack projection entry validation failed "
					"at +0x%llx\n", static_cast<unsigned long long>(
						backend_depth_hack_projection_function - 0x140000000));
				return false;
			}
			if (!code_bytes_match(backend_target_prepare_function,
				target_prepare_prologue) ||
				!utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(backend_target_prepare_function)))
			{
				console::error("[VR] backend target-prepare entry validation failed at +0x%llx\n",
					static_cast<unsigned long long>(backend_target_prepare_function -
						0x140000000));
				return false;
			}
			if (!code_bytes_match(backend_target_select_function,
				target_select_prologue) ||
				!utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(backend_target_select_function)))
			{
				console::error("[VR] backend target-select entry validation failed at +0x%llx\n",
					static_cast<unsigned long long>(backend_target_select_function -
						0x140000000));
				return false;
			}

			const std::array sites{
				h2_query_result_observation_site,
				h2_query_publish_call_site,
				backend_command_dispatch_call_site,
				backend_record_call_site,
				backend_view_bind_call_site,
				backend_record_classifier_call_site,
				backend_view_copy_call_site,
				backend_post_bind_call_site,
				backend_dynamic_upload_call_site,
				postfx_destination_call_site,
				backend_geometry_view_setup_call_site,
			};
			for (std::size_t index{}; index < sites.size(); ++index)
			{
				const auto page = utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(sites[index]));
				if (!page)
				{
					console::error("[VR] backend observation page validation failed at +0x%llx\n",
						static_cast<unsigned long long>(sites[index] - 0x140000000));
					return false;
				}
				backend_hooks.original_protections[index] = page.protection;
			}
			backend_hooks.prepared = true;
			try
			{
				backend_depth_hack_projection_hook.create(
					backend_depth_hack_projection_function,
					reinterpret_cast<void*>(backend_depth_hack_projection_stub));
				backend_target_prepare_hook.create(backend_target_prepare_function,
					reinterpret_cast<void*>(backend_target_prepare_probe_stub));
				backend_target_select_hook.create(backend_target_select_function,
					reinterpret_cast<void*>(backend_target_select_probe_stub));
			}
			catch (const std::exception& error)
			{
				console::error("[VR] backend target-prepare observer could not be installed: %s\n",
					error.what());
				(void)backend_target_select_hook.clear();
				(void)backend_target_prepare_hook.clear();
				(void)backend_depth_hack_projection_hook.clear();
				backend_hooks.prepared = false;
				return false;
			}

			std::array<std::uint8_t, 5> dynamic_upload_patch{};
			std::array<std::uint8_t, 5> postfx_destination_patch{};
			std::array<std::uint8_t, 5> post_bind_patch{};
			std::array<std::uint8_t, 5> backend_record_patch{};
			std::array<std::uint8_t, 5> backend_view_bind_patch{};
			std::array<std::uint8_t, 5> backend_record_classifier_patch{};
			std::array<std::uint8_t, 5> backend_view_copy_patch{};
			std::array<std::uint8_t, 5> geometry_view_setup_patch{};
			std::array<std::uint8_t, 5> command_dispatch_patch{};
			std::array<std::uint8_t, 5> query_publish_patch{};
			std::array<std::uint8_t, 5> query_result_call{};
			std::array<std::uint8_t, 6> query_result_patch{};
			if (!build_relative_call(backend_geometry_view_setup_call_site,
				reinterpret_cast<void*>(backend_geometry_view_setup_stub), geometry_view_setup_patch) ||
				!build_relative_call(postfx_destination_call_site,
				reinterpret_cast<void*>(postfx_destination_stub), postfx_destination_patch) ||
				!build_relative_call(backend_dynamic_upload_call_site,
				reinterpret_cast<void*>(backend_dynamic_upload_stub), dynamic_upload_patch) ||
				!build_relative_call(backend_post_bind_call_site,
				reinterpret_cast<void*>(backend_post_bind_probe_stub), post_bind_patch) ||
				!build_relative_call(backend_record_call_site,
					reinterpret_cast<void*>(backend_record_probe_stub), backend_record_patch) ||
				!build_relative_call(backend_view_bind_call_site,
					reinterpret_cast<void*>(backend_view_bind_probe_stub),
					backend_view_bind_patch) ||
				!build_relative_call(backend_record_classifier_call_site,
					reinterpret_cast<void*>(backend_record_classifier_probe_stub),
					backend_record_classifier_patch) ||
				!build_relative_call(backend_view_copy_call_site,
					reinterpret_cast<void*>(backend_view_copy_probe_stub),
					backend_view_copy_patch) ||
				!build_relative_call(backend_command_dispatch_call_site,
				reinterpret_cast<void*>(backend_command_dispatch_probe_stub),
				command_dispatch_patch) ||
				!build_relative_call(h2_query_publish_call_site,
				reinterpret_cast<void*>(h2_query_publish_probe_stub), query_publish_patch) ||
				!build_relative_call(h2_query_result_observation_site,
					backend_hooks.query_result_relay, query_result_call))
			{
				const auto rollback = restore_backend_observation_hooks();
				if (!rollback)
				{
					throw std::runtime_error(
						"VR backend observation rollback failed after patch construction error");
				}
				return false;
			}
			std::copy(query_result_call.begin(), query_result_call.end(),
				query_result_patch.begin());
			query_result_patch.back() = 0x90;

			// Consumer first, then publisher, then the inner-to-outer backend chain.
			const auto installed = backend_depth_hack_projection_hook.is_enabled() &&
				backend_target_prepare_hook.is_enabled() &&
				backend_target_select_hook.is_enabled() &&
				write_code_bytes(backend_geometry_view_setup_call_site, geometry_view_setup_patch) &&
				code_bytes_match(backend_geometry_view_setup_call_site, geometry_view_setup_patch) &&
				write_code_bytes(postfx_destination_call_site, postfx_destination_patch) &&
				code_bytes_match(postfx_destination_call_site, postfx_destination_patch) &&
				write_code_bytes(backend_dynamic_upload_call_site, dynamic_upload_patch) &&
				code_bytes_match(backend_dynamic_upload_call_site, dynamic_upload_patch) &&
				write_code_bytes(h2_query_result_observation_site,
				query_result_patch) && write_code_bytes(h2_query_publish_call_site,
				query_publish_patch) && write_code_bytes(backend_command_dispatch_call_site,
				command_dispatch_patch) && write_code_bytes(backend_view_copy_call_site,
				backend_view_copy_patch) && write_code_bytes(
				backend_record_classifier_call_site,
				backend_record_classifier_patch) && write_code_bytes(backend_record_call_site,
				backend_record_patch) && write_code_bytes(backend_view_bind_call_site,
				backend_view_bind_patch) && write_code_bytes(backend_post_bind_call_site,
				post_bind_patch) && code_bytes_match(h2_query_result_observation_site,
				query_result_patch) && code_bytes_match(h2_query_publish_call_site,
				query_publish_patch) && code_bytes_match(backend_command_dispatch_call_site,
				command_dispatch_patch) && code_bytes_match(backend_view_copy_call_site,
				backend_view_copy_patch) && code_bytes_match(
				backend_record_classifier_call_site,
				backend_record_classifier_patch) && code_bytes_match(backend_record_call_site,
				backend_record_patch) && code_bytes_match(backend_view_bind_call_site,
				backend_view_bind_patch) && code_bytes_match(backend_post_bind_call_site,
				post_bind_patch);
			if (installed)
			{
				backend_hooks.installed = true;
				return true;
			}

			const auto rollback = restore_backend_observation_hooks();
			console::error("[VR] backend observation hook transaction failed; rollback=%s\n",
				rollback ? "complete" : "incomplete");
			if (!rollback)
			{
				throw std::runtime_error(
					"VR backend observation rollback was incomplete; refusing partial H2 code patches");
			}
			return false;
		}

		bool install_hook()
		{
			// This is a one-shot loader operation: component::post_unpack runs before
			// H2 starts the renderer pipeline. Replacing a five-byte rel32 call is not
			// atomic, so this routine must never be reused as a runtime reload/toggle.
			constexpr std::array<std::uint8_t, 5> reserve_scene_record_bytes{
				0xE8, 0xED, 0xFB, 0x3A, 0x00};
			constexpr std::array<std::uint8_t, 5> render_scene_bytes{
				0xE8, 0xDD, 0x15, 0x3B, 0x00};
			constexpr std::array<std::uint8_t, 5> allocate_view_slot_bytes{
				0xE8, 0xE0, 0x1C, 0xFF, 0xFF};
			constexpr std::array<std::uint8_t, 5> initialize_view_slot_bytes{
				0xE8, 0x42, 0x2A, 0x00, 0x00};
			constexpr std::array<std::uint8_t, 5> generate_draw_surfs_bytes{
				0xE8, 0xD8, 0xD1, 0xFF, 0xFF};
			constexpr std::array<std::uint8_t, 5> fx_camera_bytes{
				0xE8, 0x7D, 0x79, 0x00, 0x00};
			constexpr std::array<std::uint8_t, 5> finalize_view_slot_bytes{
				0x48, 0x89, 0x5C, 0x24, 0x10};
			constexpr std::array<std::uint8_t, 5> mask{0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
			const auto validate_call = [&mask](const char* const name,
				const std::uintptr_t call_site, const std::array<std::uint8_t, 5>& bytes,
				const std::uintptr_t expected_target, void* const replacement)
			{
				const auto byte_validation = utils::hook_validation::verify_masked_bytes(
					reinterpret_cast<const void*>(call_site),
					{bytes.data(), mask.data(), bytes.size()});
				if (!byte_validation)
				{
					console::error("[VR] %s callsite validation failed at +0x%llx (%s)\n",
						name, static_cast<unsigned long long>(call_site - 0x140000000),
						utils::hook_validation::to_string(byte_validation.status));
					return false;
				}

				std::int32_t displacement{};
				std::memcpy(&displacement, reinterpret_cast<const void*>(call_site + 1),
					sizeof(displacement));
				const auto actual_target = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(call_site + 5) + displacement);
				if (actual_target != expected_target)
				{
					console::error("[VR] %s target mismatch at +0x%llx (actual=0x%llx expected=0x%llx)\n",
						name, static_cast<unsigned long long>(call_site - 0x140000000),
						static_cast<unsigned long long>(actual_target),
						static_cast<unsigned long long>(expected_target));
					return false;
				}
				const auto target_validation = utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(expected_target));
				if (!target_validation)
				{
					console::error("[VR] %s target is not executable at 0x%llx (%s)\n", name,
						static_cast<unsigned long long>(expected_target),
						utils::hook_validation::to_string(target_validation.status));
					return false;
				}
				if (utils::hook::is_relatively_far(reinterpret_cast<const void*>(call_site),
					replacement))
				{
					console::error("[VR] %s replacement is outside rel32 range at +0x%llx\n",
						name, static_cast<unsigned long long>(call_site - 0x140000000));
					return false;
				}
				return true;
			};

			if (!validate_call("FX camera builder", fx_camera_call_site, fx_camera_bytes,
				fx_camera_function, reinterpret_cast<void*>(build_fx_camera_stub)) ||
				!validate_call("frontend scene-record reservation",
				reserve_scene_record_call_site, reserve_scene_record_bytes,
				reserve_scene_record_function,
				reinterpret_cast<void*>(reserve_scene_record_probe_stub)) ||
				!validate_call("R_RenderScene", render_scene_call_site, render_scene_bytes,
				render_scene_function, reinterpret_cast<void*>(render_scene_stub)) ||
				!validate_call("frontend view-slot allocator", allocate_view_slot_call_site,
					allocate_view_slot_bytes, allocate_view_slot_function,
					reinterpret_cast<void*>(allocate_view_slot_probe_stub)) ||
				!validate_call("frontend view-slot initializer", initialize_view_slot_call_site,
					initialize_view_slot_bytes, initialize_view_slot_function,
					reinterpret_cast<void*>(initialize_view_slot_probe_stub)) ||
				!validate_call("R_GenerateSortedDrawSurfs", generate_draw_surfs_call_site,
					generate_draw_surfs_bytes, generate_draw_surfs_function,
					reinterpret_cast<void*>(generate_draw_surfs_probe_stub)))
			{
				return false;
			}
			const auto finalizer_validation = utils::hook_validation::verify_masked_bytes(
				reinterpret_cast<const void*>(finalize_view_slot_function),
				{finalize_view_slot_bytes.data(), mask.data(), finalize_view_slot_bytes.size()});
			if (!finalizer_validation ||
				!utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(finalize_view_slot_function)))
			{
				console::error("[VR] H2 view-slot finalizer validation failed at +0x%llx\n",
					static_cast<unsigned long long>(finalize_view_slot_function - 0x140000000));
				return false;
			}
			const auto reserve_scene_record_page =
				utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(reserve_scene_record_call_site));
			const auto render_scene_page = utils::hook_validation::validate_executable_target(
				reinterpret_cast<const void*>(render_scene_call_site));
			const auto allocate_view_slot_page = utils::hook_validation::validate_executable_target(
				reinterpret_cast<const void*>(allocate_view_slot_call_site));
			const auto initialize_view_slot_page = utils::hook_validation::validate_executable_target(
				reinterpret_cast<const void*>(initialize_view_slot_call_site));
			const auto generate_draw_surfs_page = utils::hook_validation::validate_executable_target(
				reinterpret_cast<const void*>(generate_draw_surfs_call_site));
			const auto fx_camera_page = utils::hook_validation::validate_executable_target(
				reinterpret_cast<const void*>(fx_camera_call_site));
			if (!reserve_scene_record_page || !render_scene_page ||
				!allocate_view_slot_page || !initialize_view_slot_page ||
				!generate_draw_surfs_page || !fx_camera_page)
			{
				console::error("[VR] observation callsite page protection could not be captured\n");
				return false;
			}
			if (view_diagnostics_enabled() && !diagnostics::write_runtime_code_snapshots())
			{
				console::warn("[VR] one or more pre-hook H2 runtime code snapshots could not be written\n");
			}

			std::array<std::uint8_t, 5> reserve_scene_record_patch{};
			std::array<std::uint8_t, 5> render_scene_patch{};
			std::array<std::uint8_t, 5> allocate_view_slot_patch{};
			std::array<std::uint8_t, 5> initialize_view_slot_patch{};
			std::array<std::uint8_t, 5> generate_draw_surfs_patch{};
			std::array<std::uint8_t, 5> fx_camera_patch{};
			if (!build_relative_call(fx_camera_call_site,
				reinterpret_cast<const void*>(build_fx_camera_stub), fx_camera_patch) ||
				!build_relative_call(reserve_scene_record_call_site,
				reinterpret_cast<const void*>(reserve_scene_record_probe_stub),
				reserve_scene_record_patch) ||
				!build_relative_call(render_scene_call_site,
				reinterpret_cast<const void*>(render_scene_stub), render_scene_patch) ||
				!build_relative_call(allocate_view_slot_call_site,
					reinterpret_cast<const void*>(allocate_view_slot_probe_stub),
					allocate_view_slot_patch) ||
				!build_relative_call(initialize_view_slot_call_site,
					reinterpret_cast<const void*>(initialize_view_slot_probe_stub),
					initialize_view_slot_patch) ||
				!build_relative_call(generate_draw_surfs_call_site,
					reinterpret_cast<const void*>(generate_draw_surfs_probe_stub),
					generate_draw_surfs_patch))
			{
				console::error("[VR] observation hook replacement is outside rel32 range\n");
				return false;
			}
			if (!install_backend_observation_hooks())
			{
				console::error("[VR] backend/query observation hooks were not installed\n");
				return false;
			}

			const auto restore_original_calls = [&]() noexcept
			{
				// Remove the producer first, then the consumer, so no new pending record
				// reservation can be exposed to a partially restored observer set.
				const auto reservation_restored = write_code_bytes(
					reserve_scene_record_call_site, reserve_scene_record_bytes);
				const auto render_restored = write_code_bytes(render_scene_call_site,
					render_scene_bytes);
				const auto generator_restored = write_code_bytes(generate_draw_surfs_call_site,
					generate_draw_surfs_bytes);
				const auto initializer_restored = write_code_bytes(initialize_view_slot_call_site,
					initialize_view_slot_bytes);
				const auto allocator_restored = write_code_bytes(allocate_view_slot_call_site,
					allocate_view_slot_bytes);
				const auto fx_camera_restored = write_code_bytes(fx_camera_call_site,
					fx_camera_bytes);
				// A failed protection restore during the attempted installation may have
				// left a page RWX. Reapply the protections captured before any write;
				// write_code_bytes' immediately preceding state is not authoritative here.
				const auto reservation_protection_restored = restore_code_protection(
					reserve_scene_record_call_site, reserve_scene_record_bytes.size(),
					reserve_scene_record_page.protection);
				const auto render_protection_restored = restore_code_protection(
					render_scene_call_site, render_scene_bytes.size(), render_scene_page.protection);
				const auto generator_protection_restored = restore_code_protection(
					generate_draw_surfs_call_site, generate_draw_surfs_bytes.size(),
					generate_draw_surfs_page.protection);
				const auto initializer_protection_restored = restore_code_protection(
					initialize_view_slot_call_site, initialize_view_slot_bytes.size(),
					initialize_view_slot_page.protection);
				const auto allocator_protection_restored = restore_code_protection(
					allocate_view_slot_call_site, allocate_view_slot_bytes.size(),
					allocate_view_slot_page.protection);
				const auto backend_restored = restore_backend_observation_hooks();
				const auto fx_camera_protection_restored = restore_code_protection(
					fx_camera_call_site, fx_camera_bytes.size(), fx_camera_page.protection);
				return fx_camera_restored && fx_camera_protection_restored &&
					code_bytes_match(fx_camera_call_site, fx_camera_bytes) &&
					backend_restored && reservation_restored && render_restored && generator_restored &&
					initializer_restored &&
					allocator_restored &&
					reservation_protection_restored &&
					render_protection_restored && generator_protection_restored &&
					initializer_protection_restored &&
					allocator_protection_restored &&
					code_bytes_match(reserve_scene_record_call_site,
						reserve_scene_record_bytes) &&
					code_bytes_match(render_scene_call_site, render_scene_bytes) &&
					code_bytes_match(generate_draw_surfs_call_site, generate_draw_surfs_bytes) &&
					code_bytes_match(initialize_view_slot_call_site, initialize_view_slot_bytes) &&
					code_bytes_match(allocate_view_slot_call_site, allocate_view_slot_bytes);
			};

			// Install the inner observers and record consumer first. The record producer
			// is exposed last, so it can never publish into a half-installed consumer.
			// A partial write or post-write mismatch rolls the whole transaction back.
			const auto installed = write_code_bytes(fx_camera_call_site, fx_camera_patch) &&
				write_code_bytes(allocate_view_slot_call_site,
					allocate_view_slot_patch) &&
				write_code_bytes(initialize_view_slot_call_site,
					initialize_view_slot_patch) &&
				write_code_bytes(generate_draw_surfs_call_site, generate_draw_surfs_patch) &&
				write_code_bytes(render_scene_call_site, render_scene_patch) &&
				write_code_bytes(reserve_scene_record_call_site,
					reserve_scene_record_patch) &&
				code_bytes_match(fx_camera_call_site, fx_camera_patch) &&
				code_bytes_match(allocate_view_slot_call_site, allocate_view_slot_patch) &&
				code_bytes_match(initialize_view_slot_call_site,
					initialize_view_slot_patch) &&
				code_bytes_match(generate_draw_surfs_call_site, generate_draw_surfs_patch) &&
				code_bytes_match(render_scene_call_site, render_scene_patch) &&
				code_bytes_match(reserve_scene_record_call_site,
					reserve_scene_record_patch);
			if (installed)
			{
				// The retired material/cursor investigation must not install eleven
				// extra hooks in every production draw-list traversal.
				return true;
			}

			const auto rollback_complete = restore_original_calls();
			console::error("[VR] observation hook transaction failed; rollback=%s\n",
				rollback_complete ? "complete" : "incomplete");
			if (!rollback_complete)
			{
				throw std::runtime_error(
					"VR observation hook rollback was incomplete; refusing to start with partially patched H2 code");
			}
			return false;
		}
	}

	scene_handoff_status get_scene_handoff_status() noexcept
	{
		return {scene_publication_attempts.load(), scene_publication_successes.load(), scene_publication_failures.load()};
	}

	culling_union_status get_culling_union_status() noexcept
	{
		const auto read_float_bits = [](const std::atomic_uint32_t& source) noexcept
		{
			const auto bits = source.load(std::memory_order_relaxed);
			float value{};
			std::memcpy(&value, &bits, sizeof(value));
			return value;
		};
		return {
			culling_union_attempts.load(std::memory_order_relaxed),
			culling_union_applications.load(std::memory_order_relaxed),
			culling_union_failures.load(std::memory_order_relaxed),
			read_float_bits(culling_union_tan_left),
			read_float_bits(culling_union_tan_right),
			read_float_bits(culling_union_tan_down),
			read_float_bits(culling_union_tan_up),
			read_float_bits(culling_union_near_distance),
			read_float_bits(culling_union_horizontal_expansion),
			fx_culling_attempts.load(std::memory_order_relaxed),
			fx_culling_applications.load(std::memory_order_relaxed),
			fx_culling_failures.load(std::memory_order_relaxed),
		};
	}

	bool checkpoint_observation_artifacts() noexcept
	{
		try
		{
			const auto boundary_frame = ownership_boundary_capture_frame.load(
				std::memory_order_acquire);
			std::uint64_t boundary_ready_mask{};
			std::ostringstream boundary_manifest;
			boundary_manifest
				<< "contract=cpu_observation_only\r\n"
				<< "frontend_mutation=none\r\n"
				<< "d3d_calls=0\r\nopenvr_calls=0\r\n"
				<< "capture_frame=" << boundary_frame << "\r\n";
			for (std::size_t boundary_index{};
				boundary_index < ownership_boundary_count; ++boundary_index)
			{
				for (std::size_t stage_index{};
					stage_index < ownership_boundary_stage_count; ++stage_index)
				{
					const auto artifact_index =
						boundary_index * ownership_boundary_stage_count + stage_index;
					const auto& artifact = ownership_boundary_artifacts[artifact_index];
					const auto ready = artifact.state.load(std::memory_order_acquire) == 2;
					if (ready) boundary_ready_mask |= std::uint64_t{1} << artifact_index;
					const auto boundary = static_cast<
						engine_view_probe::ownership_boundary_kind>(boundary_index + 1);
					const auto stage = stage_index == 0
						? engine_view_probe::observation_stage::enter
						: engine_view_probe::observation_stage::leave;
					boundary_manifest << "artifact="
						<< engine_view_probe::to_string(boundary) << '_'
						<< engine_view_probe::to_string(stage)
						<< " state=" << (ready ? "ready" : "empty");
					if (ready)
					{
						const auto& value = artifact.observation;
						boundary_manifest
							<< " frame=" << artifact.frontend_frame_id
							<< " tick=" << artifact.capture_tick
							<< " thread=" << artifact.thread_id
							<< " frontend=0x" << std::hex << value.frontend
							<< " backend_frontend=0x" << value.backend_frontend
							<< " record_arena=0x" << value.record_arena << std::dec
							<< " selector=" << value.selector
							<< " slot_count=" << value.slot_count
							<< " current_record_index=" << value.current_record_index
							<< " record_count=" << value.record_count
							<< " global_record_count=" << value.global_record_count
							<< " owner_record_index=" << value.owner_record_index
							<< " owner_view_hash=0x" << std::hex
							<< value.owner_view_hash << std::dec;
					}
					boundary_manifest << "\r\n";
				}
			}
			constexpr auto complete_boundary_mask =
				(std::uint64_t{1} << (ownership_boundary_count *
					ownership_boundary_stage_count)) - 1;
			const auto boundary_complete = boundary_ready_mask == complete_boundary_mask;
			boundary_manifest << "state=" << (boundary_complete
				? "complete" : "waiting_for_in_level_scene") << "\r\n";
			const auto boundary_signature = (boundary_frame << 8) | boundary_ready_mask;
			if (ownership_boundary_manifest_signature.load(std::memory_order_acquire) !=
				boundary_signature && utils::io::write_file_atomic(
					std::string{evidence_directory} +
					"/ownership-boundary-manifest.txt", boundary_manifest.str()))
			{
				ownership_boundary_manifest_signature.store(boundary_signature,
					std::memory_order_release);
			}

			const auto backend = engine_backend_probe::get_status();
			(void)capture_artifact(baseline_target_registry_artifact,
				native_render_contract::target_registry_base, {
					GetTickCount64(),
					native_render_contract::target_registry_base,
					backend.target_prepare_calls,
					backend.backend_transactions,
					backend.frontend_publications,
					backend.current_device_generation,
					0,
					0,
				});

			bool files_complete = true;
			files_complete = write_artifact(baseline_target_registry_artifact,
				"target-registry-baseline.bin") && files_complete;
			files_complete = write_artifact(descriptor_before_artifact,
				"scene-descriptor-before.bin") && files_complete;
			files_complete = write_artifact(descriptor_after_artifact,
				"scene-descriptor-after.bin") && files_complete;
			files_complete = write_artifact(slot_before_initializer_artifact,
				"view-slot-before-initializer.bin") && files_complete;
			files_complete = write_artifact(slot_after_initializer_artifact,
				"view-slot-after-initializer.bin") && files_complete;
			files_complete = write_artifact(stereo_eye_slot_artifacts[0],
				"stereo-eye-left-slot.bin") && files_complete;
			files_complete = write_artifact(stereo_eye_slot_artifacts[1],
				"stereo-eye-right-slot.bin") && files_complete;
			files_complete = write_artifact(backend_view_source_before_artifact,
				"backend-view-source-before.bin") && files_complete;
			files_complete = write_artifact(backend_view_source_after_artifact,
				"backend-view-source-after.bin") && files_complete;
			files_complete = write_artifact(backend_bound_eye_slot_artifacts[0],
				"backend-bound-left-slot.bin") && files_complete;
			files_complete = write_artifact(backend_bound_eye_slot_artifacts[1],
				"backend-bound-right-slot.bin") && files_complete;
			files_complete = write_artifact(slot_after_generator_artifact,
				"view-slot-after-generator.bin") && files_complete;
			files_complete = write_artifact(output_after_generator_artifact,
				"per-client-output-after-generator.bin") && files_complete;
			files_complete = write_artifact(record_before_target_prepare_artifact,
				"backend-record-before-target-prepare.bin") && files_complete;
			files_complete = write_artifact(record_after_target_prepare_artifact,
				"backend-record-after-target-prepare.bin") && files_complete;
			files_complete = write_artifact(registry_before_target_prepare_artifact,
				"target-registry-before-target-prepare.bin") && files_complete;
			files_complete = write_artifact(registry_after_target_prepare_artifact,
				"target-registry-after-target-prepare.bin") && files_complete;

			const auto baseline_summary = baseline_registry_summary_written.load(
				std::memory_order_acquire) ? std::string{} :
				format_target_registry_artifact("baseline",
					baseline_target_registry_artifact);
			const auto before_summary = before_registry_summary_written.load(
				std::memory_order_acquire) ? std::string{} :
				format_target_registry_artifact("before_target_prepare",
					registry_before_target_prepare_artifact);
			const auto after_summary = after_registry_summary_written.load(
				std::memory_order_acquire) ? std::string{} :
				format_target_registry_artifact("after_target_prepare",
					registry_after_target_prepare_artifact);
			const auto baseline_summary_complete = write_evidence_text_once(
				baseline_registry_summary_written, "target-registry-baseline.txt",
				baseline_summary);
			const auto before_summary_complete = write_evidence_text_once(
				before_registry_summary_written,
				"target-registry-before-target-prepare.txt", before_summary);
			const auto after_summary_complete = write_evidence_text_once(
				after_registry_summary_written,
				"target-registry-after-target-prepare.txt", after_summary);
			const auto summaries_complete = baseline_summary_complete &&
				before_summary_complete && after_summary_complete;

			const auto all_ready = artifact_ready(baseline_target_registry_artifact) &&
				artifact_ready(descriptor_before_artifact) &&
				artifact_ready(descriptor_after_artifact) &&
				artifact_ready(slot_before_initializer_artifact) &&
				artifact_ready(slot_after_initializer_artifact) &&
				artifact_ready(stereo_eye_slot_artifacts[0]) &&
				artifact_ready(stereo_eye_slot_artifacts[1]) &&
				artifact_ready(backend_view_source_before_artifact) &&
				artifact_ready(backend_view_source_after_artifact) &&
				artifact_ready(backend_bound_eye_slot_artifacts[0]) &&
				artifact_ready(backend_bound_eye_slot_artifacts[1]) &&
				artifact_ready(slot_after_generator_artifact) &&
				artifact_ready(output_after_generator_artifact) &&
				artifact_ready(record_before_target_prepare_artifact) &&
				artifact_ready(record_after_target_prepare_artifact) &&
				artifact_ready(registry_before_target_prepare_artifact) &&
				artifact_ready(registry_after_target_prepare_artifact);
			const auto binding = engine_stereo_binding::get_status();
			const auto binding_complete = binding.publications != 0 &&
				binding.claims != 0 && binding.releases != 0 &&
				binding.publication_drops == 0 && binding.mapping_misses == 0 &&
				binding.invalid_publications == 0 && binding.release_mismatches == 0 &&
				binding.active_claims == 0;
			const auto backend_view = engine_stereo_backend_view::get_status();
			const auto backend_view_complete =
				backend_view.state == engine_stereo_backend_view::gate_state::complete &&
				backend_view.attempts == 1 && backend_view.completions == 1 &&
				backend_view.failures == 0 && backend_view.copy_calls != 0 &&
				backend_view.substitutions != 0 &&
				backend_view.foreign_copy_bypasses == 0 &&
				backend_view.destination_mismatches == 0 &&
				backend_view.record_mutations == 0 && backend_view.dispatch_misses == 0;
			const auto backend_target = engine_stereo_backend_target::get_status();
			engine_stereo_backend_target::report backend_target_report{};
			const auto backend_target_report_ready =
				engine_stereo_backend_target::read_report(backend_target_report);
			const auto backend_target_manifest = backend_target_report_ready
				? format_backend_target_route(backend_target_report, backend_target)
				: std::string{};
			const auto backend_target_manifest_complete = write_evidence_text_once(
				backend_target_route_manifest_written, "backend-target-route-manifest.txt",
				backend_target_manifest);
			const auto backend_target_complete =
				backend_target.state == engine_stereo_backend_target::gate_state::complete &&
				backend_target.attempts == 1 && backend_target.completions == 1 &&
				backend_target.failures == 0 && backend_target.select_calls != 0 &&
				backend_target.applied_transitions != 0 &&
				backend_target.unique_targets != 0 &&
				backend_target.invalid_observations == 0 &&
				backend_target.application_mismatches == 0 &&
				backend_target.entry_mutations == 0 &&
				backend_target.route_overflows == 0 &&
				backend_target_manifest_complete;
			const auto backend_target_frame =
				engine_stereo_backend_target::get_frame_status();
			std::unique_ptr<engine_stereo_backend_target::frame_report>
				backend_target_frame_report;
			bool backend_target_frame_report_ready{};
			if (backend_target_frame.state ==
					engine_stereo_backend_target::gate_state::complete ||
				backend_target_frame.state ==
					engine_stereo_backend_target::gate_state::failed)
			{
				backend_target_frame_report =
					std::make_unique<engine_stereo_backend_target::frame_report>();
				backend_target_frame_report_ready =
					engine_stereo_backend_target::read_frame_report(
						*backend_target_frame_report);
			}
			const auto backend_target_frame_manifest = backend_target_frame_report_ready
				? format_backend_target_frame(*backend_target_frame_report,
					backend_target_frame)
				: std::string{};
			const auto backend_target_frame_manifest_complete = write_evidence_text_once(
				backend_target_frame_manifest_written,
				"backend-target-frame-manifest.txt", backend_target_frame_manifest);
			const auto backend_target_frame_report_consistent =
				backend_target_frame_report_ready &&
				backend_target_frame_report->start_publication_sequence ==
					backend_target_frame.start_publication_sequence &&
				backend_target_frame_report->start_record ==
					backend_target_frame.start_record &&
				backend_target_frame_report->start_present_post_frame ==
					backend_target_frame.start_present_post_frame &&
				backend_target_frame_report->start_present_post_thread_id ==
					backend_target_frame.start_present_post_thread_id &&
				backend_target_frame_report->end_present_pre_frame ==
					backend_target_frame.end_present_pre_frame &&
				backend_target_frame_report->device_generation ==
					backend_target_frame.device_generation &&
				backend_target_frame_report->boundary_thread_id ==
					backend_target_frame.boundary_thread_id &&
				backend_target_frame_report->event_count ==
					backend_target_frame.select_calls +
					backend_target_frame.view_copy_events &&
				backend_target_frame_report->view_copy_event_count ==
					backend_target_frame.view_copy_events &&
				backend_target_frame_report->unique_target_count ==
					backend_target_frame.unique_targets;
			const auto backend_target_frame_complete =
				backend_target_frame.state ==
					engine_stereo_backend_target::gate_state::complete &&
				backend_target_frame.attempts == 1 &&
				backend_target_frame.completions == 1 &&
				backend_target_frame.failures == 0 &&
				backend_target_frame.select_calls != 0 &&
				backend_target_frame.view_copy_events == backend_view.copy_calls &&
				backend_target_frame.unique_targets != 0 &&
				backend_target_frame.invalid_observations == 0 &&
				backend_target_frame.application_mismatches == 0 &&
				backend_target_frame.entry_mutations == 0 &&
				backend_target_frame.route_overflows == 0 &&
				backend_target_frame.device_generation_mismatches == 0 &&
				backend_target_frame.device_generation != 0 &&
				backend_target_frame.start_present_post_frame != 0 &&
				backend_target_frame.end_present_pre_frame ==
					backend_target_frame.start_present_post_frame + 1 &&
				backend_target_frame.start_present_post_thread_id != 0 &&
				backend_target_frame.start_present_post_thread_id ==
					backend_target_frame.boundary_thread_id &&
				backend_target_frame_report_consistent &&
				backend_target_frame_manifest_complete;
			const auto output_merger = engine_stereo_output_merger::get_status();
			std::unique_ptr<engine_stereo_output_merger::report> output_merger_report;
			bool output_merger_report_ready{};
			if (output_merger.state == engine_stereo_output_merger::gate_state::complete ||
				output_merger.state == engine_stereo_output_merger::gate_state::failed)
			{
				output_merger_report =
					std::make_unique<engine_stereo_output_merger::report>();
				output_merger_report_ready = engine_stereo_output_merger::read_report(
					*output_merger_report);
			}
			const auto output_merger_manifest = output_merger_report_ready
				? format_backend_output_merger(*output_merger_report, output_merger)
				: std::string{};
			const auto output_merger_manifest_complete = write_evidence_text_once(
				backend_output_merger_manifest_written,
				"backend-output-merger-manifest.txt", output_merger_manifest);
			const auto output_merger_report_consistent = output_merger_report_ready &&
				output_merger_report->expected_context == output_merger.expected_context &&
				output_merger_report->device_generation == output_merger.device_generation &&
				output_merger_report->publication_sequence ==
					output_merger.latest_publication_sequence &&
				output_merger_report->record == output_merger.latest_record &&
				output_merger_report->transaction_thread_id != 0 &&
				output_merger_report->event_count == output_merger.bind_calls;
			const auto output_merger_complete = output_merger.hook_installed &&
				output_merger.extended_hooks_installed &&
				output_merger.hook_target != 0 &&
				output_merger.unordered_access_hook_target != 0 &&
				output_merger.clear_state_hook_target != 0 &&
				output_merger.expected_context != 0 && output_merger.device_generation != 0 &&
				output_merger.hook_failures == 0 &&
				output_merger.state == engine_stereo_output_merger::gate_state::complete &&
				output_merger.attempts == 1 && output_merger.completions == 1 &&
				output_merger.failures == 0 && output_merger.bind_calls != 0 &&
				output_merger.nonnull_bind_calls != 0 &&
				output_merger.context_mismatches == 0 &&
				output_merger.thread_mismatches == 0 &&
				output_merger.query_failures == 0 && output_merger.overflows == 0 &&
				output_merger.metadata_queries != 0 && output_merger_report_consistent &&
				output_merger_manifest_complete;
			const auto draw_indexed = engine_stereo_draw_indexed::get_status();
			std::unique_ptr<engine_stereo_draw_indexed::report> draw_indexed_report;
			bool draw_indexed_report_ready{};
			if (draw_indexed.state == engine_stereo_draw_indexed::gate_state::complete ||
				draw_indexed.state == engine_stereo_draw_indexed::gate_state::failed)
			{
				draw_indexed_report =
					std::make_unique<engine_stereo_draw_indexed::report>();
				draw_indexed_report_ready = engine_stereo_draw_indexed::read_report(
					*draw_indexed_report);
			}
			const auto draw_indexed_manifest = draw_indexed_report_ready
				? format_backend_draw_indexed(*draw_indexed_report, draw_indexed)
				: std::string{};
			const auto draw_indexed_manifest_complete = write_evidence_text_once(
				backend_draw_indexed_manifest_written,
				"backend-draw-indexed-manifest.txt", draw_indexed_manifest);
			const auto draw_indexed_report_consistent = draw_indexed_report_ready &&
				draw_indexed_report->expected_context == draw_indexed.expected_context &&
				draw_indexed_report->device_generation == draw_indexed.device_generation &&
				draw_indexed_report->publication_sequence ==
					draw_indexed.latest_publication_sequence &&
				draw_indexed_report->record == draw_indexed.latest_record &&
				draw_indexed_report->transaction_thread_id != 0 &&
				draw_indexed_report->event_count == draw_indexed.draw_calls;
			const auto draw_indexed_complete = draw_indexed.hook_installed &&
				draw_indexed.hook_target != 0 && draw_indexed.expected_context != 0 &&
				draw_indexed.device_generation != 0 &&
				draw_indexed.hook_failures == 0 &&
				draw_indexed.state == engine_stereo_draw_indexed::gate_state::complete &&
				draw_indexed.attempts == 1 && draw_indexed.completions == 1 &&
				draw_indexed.failures == 0 && draw_indexed.draw_calls != 0 &&
				draw_indexed.context_mismatches == 0 &&
				draw_indexed.thread_mismatches == 0 &&
				draw_indexed.invalid_arguments == 0 && draw_indexed.overflows == 0 &&
				draw_indexed_report_consistent &&
				draw_indexed_manifest_complete;
			const auto execution = engine_stereo_execution::get_status();
			std::unique_ptr<engine_stereo_execution::report> execution_report;
			bool execution_report_ready{};
			const auto execution_terminal =
				execution.state == engine_stereo_execution::gate_state::complete ||
				execution.state == engine_stereo_execution::gate_state::failed;
			if (execution_terminal)
			{
				execution_report = std::make_unique<engine_stereo_execution::report>();
				execution_report_ready = engine_stereo_execution::read_report(
					*execution_report);
			}
			const auto execution_manifest = execution_report_ready
				? format_backend_execution(*execution_report, execution)
				: std::string{};
			const auto execution_manifest_signature = execution_manifest.empty() ? 0 :
				hash_memory_region(reinterpret_cast<std::uintptr_t>(
					execution_manifest.data()), execution_manifest.size());
			const auto execution_manifest_complete = write_evidence_text_versioned(
				backend_execution_manifest_signature,
				"backend-execution-census-manifest.txt", execution_manifest,
				execution_manifest_signature);
			std::uint64_t execution_total_events{};
			std::uint64_t execution_classifier_total{};
			std::uint64_t execution_frame_total{};
			bool execution_targets_complete = execution.targets_distinct &&
				execution.output_merger_target != 0 &&
				execution.output_merger_unordered_access_target != 0 &&
				execution.clear_state_target != 0;
			for (std::size_t index{}; index < engine_stereo_execution::api_count; ++index)
			{
				execution_total_events += execution.per_api[index];
				execution_classifier_total += execution.classifier_per_api[index];
				execution_frame_total += execution.frame_per_api[index];
				execution_targets_complete = execution_targets_complete &&
					execution.hook_targets[index] != 0;
			}
			const auto execution_counts_consistent =
				execution.event_reservations == execution.recorded_events &&
				execution_total_events == execution.recorded_events &&
				execution_classifier_total == execution.classifier_events &&
				execution_frame_total == execution.frame_events &&
				execution.recorded_events == execution.classifier_events +
					execution.frame_events &&
				execution.backend_scoped_events <= execution.recorded_events &&
				execution.backend_scoped_frame_events +
					execution.backend_unscoped_frame_events == execution.frame_events &&
				execution.backend_scoped_frame_events <=
					execution.backend_scoped_events &&
				execution.scene_owner_scoped_events <= execution.recorded_events &&
				execution.scene_owner_scoped_frame_events +
					execution.scene_owner_unscoped_frame_events ==
					execution.frame_events &&
				execution.scene_owner_scoped_frame_events <=
					execution.scene_owner_scoped_events &&
				execution.call_stack_samples <= execution.recorded_events;
			const auto execution_frame_boundary_complete = execution_report_ready &&
				execution.frame_start_present_post != 0 &&
				execution.frame_end_present_pre ==
					execution.frame_start_present_post + 1 &&
				execution.frame_end_present_post == execution.frame_end_present_pre &&
				SUCCEEDED(execution.frame_present_result) &&
				execution_report->frame_start_thread_id != 0 &&
				execution_report->frame_start_thread_id ==
					execution_report->frame_end_thread_id;
			const auto execution_report_consistent = execution_report_ready &&
				execution_report->expected_context == execution.expected_context &&
				execution_report->device_generation == execution.device_generation &&
				execution_report->classifier_record == execution.classifier_record &&
				execution_report->classifier_record_type ==
					execution.classifier_record_type &&
				execution_report->classifier_thread_id == execution.classifier_thread_id &&
				execution_report->classifier_begin_qpc == execution.classifier_begin_qpc &&
				execution_report->classifier_end_qpc == execution.classifier_end_qpc &&
				execution_report->frame_start_present_post ==
					execution.frame_start_present_post &&
				execution_report->frame_end_present_pre == execution.frame_end_present_pre &&
				execution_report->frame_end_present_post ==
					execution.frame_end_present_post &&
				execution_report->frame_present_result == execution.frame_present_result &&
				execution_report->event_count == execution.recorded_events &&
				execution_report->classifier_event_count == execution.classifier_events &&
				execution_report->frame_event_count == execution.frame_events &&
				execution_report->expected_context_frame_events ==
					execution.expected_context_frame_events &&
				execution_report->backend_scoped_events ==
					execution.backend_scoped_events &&
				execution_report->backend_scoped_frame_events ==
					execution.backend_scoped_frame_events &&
				execution_report->backend_unscoped_frame_events ==
					execution.backend_unscoped_frame_events &&
				execution_report->backend_thread_mismatches ==
					execution.backend_thread_mismatches &&
				execution_report->distinct_backend_records ==
					execution.distinct_backend_records &&
				execution_report->scene_owner_scoped_events ==
					execution.scene_owner_scoped_events &&
				execution_report->scene_owner_scoped_frame_events ==
					execution.scene_owner_scoped_frame_events &&
				execution_report->scene_owner_unscoped_frame_events ==
					execution.scene_owner_unscoped_frame_events &&
				execution_report->scene_owner_thread_mismatches ==
					execution.scene_owner_thread_mismatches &&
				execution_report->distinct_scene_owner_records ==
					execution.distinct_scene_owner_records &&
				execution_report->call_stack_samples ==
					execution.call_stack_samples &&
				execution_report->call_stack_capture_failures ==
					execution.call_stack_capture_failures &&
				execution_report->call_stack_key_overflows ==
					execution.call_stack_key_overflows &&
				execution_report->admission_collisions ==
					execution.admission_collisions &&
				execution_report->foreign_context_events ==
					execution.foreign_context_events &&
				execution_report->known_conversion_recordings==execution.known_conversion_recordings &&
				execution_report->known_conversion_executions==execution.known_conversion_executions &&
				execution_report->known_conversion_replays==execution.known_conversion_replays &&
				execution_report->per_api == execution.per_api &&
				execution_report->classifier_per_api == execution.classifier_per_api &&
				execution_report->frame_per_api == execution.frame_per_api &&
				execution_report->deferred_execution_opaque ==
					execution.deferred_execution_opaque &&
				execution_report->identity_truncated == execution.identity_truncated;
			const auto observation_device_identity_consistent =
				backend_target_frame.device_generation != 0 &&
				backend_target_frame.device_generation == output_merger.device_generation &&
				backend_target_frame.device_generation == draw_indexed.device_generation &&
				backend_target_frame.device_generation == execution.device_generation &&
				output_merger.expected_context != 0 &&
				output_merger.expected_context == draw_indexed.expected_context &&
				output_merger.expected_context == execution.expected_context &&
				output_merger_report_consistent && draw_indexed_report_consistent &&
				backend_target_frame_report_consistent;
			const auto terminal_device_identity_consistent =
				observation_device_identity_consistent && execution_report_consistent;
			const auto observation_complete = all_ready && files_complete &&
				summaries_complete && binding_complete && backend_view_complete &&
				backend_target_complete && backend_target_frame_complete &&
				output_merger_complete && draw_indexed_complete &&
				observation_device_identity_consistent;
			const auto execution_census_complete = execution.hooks_installed &&
				!execution.installation_permanently_failed &&
				execution.installed_hook_count == engine_stereo_execution::api_count - 1 &&
				execution_targets_complete &&
				execution.hook_failures == 0 &&
				execution.state == engine_stereo_execution::gate_state::complete &&
				execution.error == engine_stereo_execution::failure::none &&
				execution.attempts == 1 && execution.completions == 1 &&
				execution.failures == 0 && execution.recorded_events != 0 &&
				execution.draw_indexed_forwarding_external &&
				execution.draw_indexed_forwarded_calls != 0 &&
				execution.classifier_events != 0 && execution.frame_events != 0 &&
				execution.expected_context_frame_events != 0 &&
				execution.backend_scoped_frame_events != 0 &&
				execution.distinct_backend_records != 0 &&
				execution.backend_thread_mismatches == 0 &&
				execution.scene_owner_scoped_frame_events != 0 &&
				execution.distinct_scene_owner_records != 0 &&
				execution.scene_owner_thread_mismatches == 0 &&
				execution.call_stack_samples != 0 &&
				execution.call_stack_capture_failures == 0 &&
				execution.call_stack_key_overflows == 0 &&
				execution.foreign_context_events == 0 && execution.overflows == 0 &&
				execution.admission_collisions == 0 &&
				execution.classifier_thread_mismatches == 0 &&
				!execution.deferred_execution_opaque &&
				execution.opaque_execute_command_lists == 0 &&
				!execution.identity_truncated && execution_counts_consistent &&
				execution.classifier_record_type ==
					native_render_contract::expected_world_record_type &&
				execution.classifier_begin_qpc != 0 &&
				execution.classifier_end_qpc >= execution.classifier_begin_qpc &&
				execution_frame_boundary_complete && execution_report_consistent &&
				execution_manifest_complete;
			const auto execution_complete = accepts_complete_execution_evidence({
				observation_complete,
				backend_target_frame_complete,
				output_merger_complete,
				draw_indexed_complete,
				execution_census_complete,
				terminal_device_identity_consistent,
			});
			const auto stereo_replay = engine_stereo_replay::get_status();
			engine_stereo_replay::report stereo_replay_report{};
			const auto stereo_replay_report_ready =
				engine_stereo_replay::read_report(stereo_replay_report);
			const auto stereo_replay_manifest = stereo_replay_report_ready
				? format_backend_stereo_replay(stereo_replay_report, stereo_replay)
				: std::string{};
			const auto stereo_replay_manifest_complete = write_evidence_text_once(
				backend_stereo_replay_manifest_written,
				"backend-stereo-replay-manifest.txt", stereo_replay_manifest);
			const auto stereo_replay_complete =
				stereo_replay.state == engine_stereo_replay::gate_state::complete &&
				stereo_replay.attempts == 1 && stereo_replay.completions == 1 &&
				stereo_replay.failures == 0 && stereo_replay.replay_dispatches == 1 &&
				stereo_replay.replay_draw_calls != 0 && stereo_replay_report_ready &&
				stereo_replay_report.command_immutable &&
				stereo_replay_report.replay_draw_matches &&
				stereo_replay_report.replay_output_bind_calls == 0 &&
				stereo_replay_report.right_changed_from_clear &&
				stereo_replay_report.eyes_distinct &&
				stereo_replay_report.output_restored &&
				stereo_replay_report.view_restored && stereo_replay_manifest_complete;
			std::ostringstream manifest;
			const char* manifest_state = "waiting_for_level";
			if (observation_complete) manifest_state = "execution_pending";
			if (execution_terminal) manifest_state = "incomplete";
			if (execution_terminal && execution.deferred_execution_opaque)
			{
				manifest_state = "opaque";
			}
			if (execution.state == engine_stereo_execution::gate_state::failed)
			{
				manifest_state = "failed";
			}
			if (execution_complete) manifest_state = "complete";
			manifest << "state=" << manifest_state << "\r\n"
				<< "contract=bounded_h2_observation_then_read_only_d3d11_execution_census\r\n"
				<< "renderer_thread_file_io=none\r\n"
				<< "observation_additional_d3d_gpu_commands=0\r\n"
				<< "stereo_replay_probe=retired\r\n"
				<< "execution_census_additional_gpu_commands=0\r\n"
				<< "openvr_calls=0\r\nrecord_mutations=0\r\n"
				<< "stereo_slot_contract=record_local_center_view_derivation\r\n"
				<< "stereo_slot_h2_pure_finalizer_calls_per_family=2\r\n"
				<< "stereo_slot_capture_state="
				<< stereo_eye_slot_capture_state.load(std::memory_order_acquire) << "\r\n"
				<< "backend_binding_contract=immutable_cpu_claim_only\r\n"
				<< "backend_binding_state=" << (binding_complete ? "complete" : "waiting")
				<< " publications=" << binding.publications
				<< " claims=" << binding.claims
				<< " releases=" << binding.releases
				<< " publication_drops=" << binding.publication_drops
				<< " mapping_misses=" << binding.mapping_misses
				<< " invalid_publications=" << binding.invalid_publications
				<< " release_mismatches=" << binding.release_mismatches
				<< " active_claims=" << binding.active_claims
				<< " max_active_claims=" << binding.maximum_active_claims << "\r\n"
				<< "backend_eye_view_copy_contract=one_shot_left_eye_exact_0x170\r\n"
				<< "backend_eye_view_copy_state="
				<< engine_stereo_backend_view::to_string(backend_view.state)
				<< " attempts=" << backend_view.attempts
				<< " completions=" << backend_view.completions
				<< " failures=" << backend_view.failures
				<< " copy_calls=" << backend_view.copy_calls
				<< " substitutions=" << backend_view.substitutions
				<< " foreign_copy_bypasses=" << backend_view.foreign_copy_bypasses
				<< " destination_mismatches=" << backend_view.destination_mismatches
				<< " record_mutations=" << backend_view.record_mutations
				<< " dispatch_misses=" << backend_view.dispatch_misses
				<< " publication_sequence=" << backend_view.latest_publication_sequence
				<< " record=0x" << std::hex << backend_view.latest_record << std::dec << "\r\n"
				<< "backend_target_route_contract=one_shot_cpu_observation_only\r\n"
				<< "backend_target_route_state="
				<< engine_stereo_backend_target::to_string(backend_target.state)
				<< " attempts=" << backend_target.attempts
				<< " completions=" << backend_target.completions
				<< " failures=" << backend_target.failures
				<< " select_calls=" << backend_target.select_calls
				<< " dispatch_select_calls=" << backend_target.dispatch_select_calls
				<< " applied_transitions=" << backend_target.applied_transitions
				<< " retained_targets=" << backend_target.retained_targets
				<< " unique_targets=" << backend_target.unique_targets
				<< " invalid_observations=" << backend_target.invalid_observations
				<< " application_mismatches=" << backend_target.application_mismatches
				<< " entry_mutations=" << backend_target.entry_mutations
				<< " route_overflows=" << backend_target.route_overflows
				<< " report_written=" << (backend_target_manifest_complete ? "yes" : "no")
				<< " publication_sequence=" << backend_target.latest_publication_sequence
				<< " record=0x" << std::hex << backend_target.latest_record << std::dec << "\r\n"
				<< "backend_target_frame_contract=claim_to_following_present_pre_cpu_observation\r\n"
				<< "backend_target_frame_state="
				<< engine_stereo_backend_target::to_string(backend_target_frame.state)
				<< " attempts=" << backend_target_frame.attempts
				<< " completions=" << backend_target_frame.completions
				<< " failures=" << backend_target_frame.failures
				<< " select_calls=" << backend_target_frame.select_calls
				<< " view_copy_events=" << backend_target_frame.view_copy_events
				<< " unique_targets=" << backend_target_frame.unique_targets
				<< " invalid_observations=" << backend_target_frame.invalid_observations
				<< " application_mismatches="
				<< backend_target_frame.application_mismatches
				<< " entry_mutations=" << backend_target_frame.entry_mutations
				<< " route_overflows=" << backend_target_frame.route_overflows
				<< " generation_mismatches="
				<< backend_target_frame.device_generation_mismatches
				<< " max_concurrent_writers="
				<< backend_target_frame.maximum_active_writers
				<< " start_present_post="
				<< backend_target_frame.start_present_post_frame
				<< " start_present_post_thread="
				<< backend_target_frame.start_present_post_thread_id
				<< " end_present_pre=" << backend_target_frame.end_present_pre_frame
				<< " device_generation=" << backend_target_frame.device_generation
				<< " boundary_thread=" << backend_target_frame.boundary_thread_id
				<< " report_written="
				<< (backend_target_frame_manifest_complete ? "yes" : "no")
				<< " publication_sequence="
				<< backend_target_frame.start_publication_sequence
				<< " record=0x" << std::hex << backend_target_frame.start_record
				<< std::dec << "\r\n"
				<< "backend_output_merger_contract=one_shot_read_only_exact_transaction\r\n"
				<< "backend_output_merger_state="
				<< engine_stereo_output_merger::to_string(output_merger.state)
				<< " hook_installed=" << (output_merger.hook_installed ? "yes" : "no")
				<< " extended_hooks_installed="
				<< (output_merger.extended_hooks_installed ? "yes" : "no")
				<< " hook_target=0x" << std::hex << output_merger.hook_target
				<< " uav_hook_target=0x" << output_merger.unordered_access_hook_target
				<< " clear_state_hook_target=0x" << output_merger.clear_state_hook_target
				<< " context=0x" << output_merger.expected_context << std::dec
				<< " generation=" << output_merger.device_generation
				<< " hook_failures=" << output_merger.hook_failures
				<< " attempts=" << output_merger.attempts
				<< " completions=" << output_merger.completions
				<< " failures=" << output_merger.failures
				<< " bind_calls=" << output_merger.bind_calls
				<< " nonnull_bind_calls=" << output_merger.nonnull_bind_calls
				<< " context_mismatches=" << output_merger.context_mismatches
				<< " thread_mismatches=" << output_merger.thread_mismatches
				<< " query_failures=" << output_merger.query_failures
				<< " overflows=" << output_merger.overflows
				<< " metadata_queries=" << output_merger.metadata_queries
				<< " unordered_access_calls=" << output_merger.unordered_access_calls
				<< " clear_state_calls=" << output_merger.clear_state_calls
				<< " binding_invalidations=" << output_merger.binding_invalidations
				<< " report_written="
				<< (output_merger_manifest_complete ? "yes" : "no")
				<< " publication_sequence=" << output_merger.latest_publication_sequence
				<< " record=0x" << std::hex << output_merger.latest_record
				<< std::dec << "\r\n"
				<< "backend_draw_indexed_contract=one_shot_read_only_exact_dispatch\r\n"
				<< "backend_draw_indexed_state="
				<< engine_stereo_draw_indexed::to_string(draw_indexed.state)
				<< " hook_installed=" << (draw_indexed.hook_installed ? "yes" : "no")
				<< " hook_target=0x" << std::hex << draw_indexed.hook_target
				<< " context=0x" << draw_indexed.expected_context << std::dec
				<< " generation=" << draw_indexed.device_generation
				<< " hook_failures=" << draw_indexed.hook_failures
				<< " attempts=" << draw_indexed.attempts
				<< " completions=" << draw_indexed.completions
				<< " failures=" << draw_indexed.failures
				<< " draw_calls=" << draw_indexed.draw_calls
				<< " context_mismatches=" << draw_indexed.context_mismatches
				<< " thread_mismatches=" << draw_indexed.thread_mismatches
				<< " invalid_arguments=" << draw_indexed.invalid_arguments
				<< " overflows=" << draw_indexed.overflows
				<< " report_written="
				<< (draw_indexed_manifest_complete ? "yes" : "no")
				<< " publication_sequence=" << draw_indexed.latest_publication_sequence
				<< " record=0x" << std::hex << draw_indexed.latest_record
				<< std::dec << "\r\n"
				<< "backend_execution_contract=exact_type4_classifier_plus_next_present_to_present_frame_with_outer_record_owner_read_only\r\n"
				<< "backend_execution_state="
				<< engine_stereo_execution::to_string(execution.state)
				<< " error=" << engine_stereo_execution::to_string(execution.error)
				<< " hooks_installed=" << (execution.hooks_installed ? "yes" : "no")
				<< " hook_count=" << execution.installed_hook_count
				<< " targets_distinct=" << (execution.targets_distinct ? "yes" : "no")
				<< " permanent_install_failure="
				<< (execution.installation_permanently_failed ? "yes" : "no")
				<< " draw_indexed_forwarding_external="
				<< (execution.draw_indexed_forwarding_external ? "yes" : "no")
				<< " draw_indexed_forwarded_calls="
				<< execution.draw_indexed_forwarded_calls
				<< " context=0x" << std::hex << execution.expected_context << std::dec
				<< " generation=" << execution.device_generation
				<< " hook_failures=" << execution.hook_failures
				<< " attempts=" << execution.attempts
				<< " completions=" << execution.completions
				<< " failures=" << execution.failures
				<< " reservations=" << execution.event_reservations
				<< " events=" << execution.recorded_events
				<< " classifier_events=" << execution.classifier_events
				<< " frame_events=" << execution.frame_events
				<< " expected_context_frame_events="
				<< execution.expected_context_frame_events
				<< " backend_scoped_events=" << execution.backend_scoped_events
				<< " backend_scoped_frame_events="
				<< execution.backend_scoped_frame_events
				<< " backend_unscoped_frame_events="
				<< execution.backend_unscoped_frame_events
				<< " backend_thread_mismatches="
				<< execution.backend_thread_mismatches
				<< " distinct_backend_records="
				<< execution.distinct_backend_records
				<< " scene_owner_scoped_events="
				<< execution.scene_owner_scoped_events
				<< " scene_owner_scoped_frame_events="
				<< execution.scene_owner_scoped_frame_events
				<< " scene_owner_unscoped_frame_events="
				<< execution.scene_owner_unscoped_frame_events
				<< " scene_owner_thread_mismatches="
				<< execution.scene_owner_thread_mismatches
				<< " distinct_scene_owner_records="
				<< execution.distinct_scene_owner_records
				<< " call_stack_samples=" << execution.call_stack_samples
				<< " call_stack_capture_failures="
				<< execution.call_stack_capture_failures
				<< " call_stack_key_overflows="
				<< execution.call_stack_key_overflows
				<< " admission_collisions=" << execution.admission_collisions
				<< " overflows=" << execution.overflows
				<< " foreign_context_events=" << execution.foreign_context_events
				<< " execute_command_lists=" << execution.opaque_execute_command_lists
				<< " known_conversion_recordings=" << execution.known_conversion_recordings
				<< " known_conversion_executions=" << execution.known_conversion_executions
				<< " known_conversion_replays=" << execution.known_conversion_replays
				<< " deferred_execution_opaque="
				<< (execution.deferred_execution_opaque ? "yes" : "no")
				<< " identity_truncated="
				<< (execution.identity_truncated ? "yes" : "no")
				<< " classifier_thread_mismatches="
				<< execution.classifier_thread_mismatches
				<< " frame=" << execution.frame_start_present_post << "->"
				<< execution.frame_end_present_pre << "->"
				<< execution.frame_end_present_post
				<< " present_result=0x" << std::hex
				<< static_cast<std::uint32_t>(execution.frame_present_result) << std::dec
				<< " report_written="
				<< (execution_manifest_complete ? "yes" : "no")
				<< " classifier_record=0x" << std::hex
				<< execution.classifier_record << std::dec
				<< " classifier_type=" << execution.classifier_record_type
				<< " immutable_identity_hash=0x" << std::hex
				<< execution_manifest_signature << std::dec << "\r\n"
				<< "backend_stereo_replay_contract=retired_no_gpu_work\r\n"
				<< "backend_stereo_replay_state="
				<< engine_stereo_replay::to_string(stereo_replay.state)
				<< " error=" << engine_stereo_replay::to_string(stereo_replay.error)
				<< " preparations=" << stereo_replay.preparations
				<< " attempts=" << stereo_replay.attempts
				<< " completions=" << stereo_replay.completions
				<< " failures=" << stereo_replay.failures
				<< " replay_dispatches=" << stereo_replay.replay_dispatches
				<< " replay_draw_calls=" << stereo_replay.replay_draw_calls
				<< " readback_polls=" << stereo_replay.readback_polls
				<< " report_written="
				<< (stereo_replay_manifest_complete ? "yes" : "no")
				<< " publication_sequence=" << stereo_replay.latest_publication_sequence
				<< " record=0x" << std::hex << stereo_replay.latest_record
				<< std::dec << "\r\n"
				<< "metadata_schema=artifact-specific; m0=tick and pointer fields are runtime-only\r\n"
				<< "baseline_menu_gate=baseline m2 is target_prepare_calls and must be zero\r\n";
			append_artifact_manifest(manifest, "target_registry_baseline",
				"target-registry-baseline.bin", baseline_target_registry_artifact);
			append_artifact_manifest(manifest, "scene_descriptor_before",
				"scene-descriptor-before.bin", descriptor_before_artifact);
			append_artifact_manifest(manifest, "scene_descriptor_after",
				"scene-descriptor-after.bin", descriptor_after_artifact);
			append_artifact_manifest(manifest, "view_slot_before_initializer",
				"view-slot-before-initializer.bin", slot_before_initializer_artifact);
			append_artifact_manifest(manifest, "view_slot_after_initializer",
				"view-slot-after-initializer.bin", slot_after_initializer_artifact);
			append_artifact_manifest(manifest, "stereo_eye_left_slot",
				"stereo-eye-left-slot.bin", stereo_eye_slot_artifacts[0]);
			append_artifact_manifest(manifest, "stereo_eye_right_slot",
				"stereo-eye-right-slot.bin", stereo_eye_slot_artifacts[1]);
			append_artifact_manifest(manifest, "backend_view_source_before",
				"backend-view-source-before.bin", backend_view_source_before_artifact);
			append_artifact_manifest(manifest, "backend_view_source_after",
				"backend-view-source-after.bin", backend_view_source_after_artifact);
			append_artifact_manifest(manifest, "backend_bound_left_slot",
				"backend-bound-left-slot.bin", backend_bound_eye_slot_artifacts[0]);
			append_artifact_manifest(manifest, "backend_bound_right_slot",
				"backend-bound-right-slot.bin", backend_bound_eye_slot_artifacts[1]);
			append_artifact_manifest(manifest, "view_slot_after_generator",
				"view-slot-after-generator.bin", slot_after_generator_artifact);
			append_artifact_manifest(manifest, "per_client_output_after_generator",
				"per-client-output-after-generator.bin", output_after_generator_artifact);
			append_artifact_manifest(manifest, "backend_record_before_target_prepare",
				"backend-record-before-target-prepare.bin",
				record_before_target_prepare_artifact);
			append_artifact_manifest(manifest, "backend_record_after_target_prepare",
				"backend-record-after-target-prepare.bin",
				record_after_target_prepare_artifact);
			append_artifact_manifest(manifest, "target_registry_before_target_prepare",
				"target-registry-before-target-prepare.bin",
				registry_before_target_prepare_artifact);
			append_artifact_manifest(manifest, "target_registry_after_target_prepare",
				"target-registry-after-target-prepare.bin",
				registry_after_target_prepare_artifact);
			append_delta_manifest(manifest, "scene_descriptor",
				descriptor_before_artifact, descriptor_after_artifact);
			append_delta_manifest(manifest, "view_slot_initializer",
				slot_before_initializer_artifact, slot_after_initializer_artifact);
			append_delta_manifest(manifest, "natural_to_left_eye_slot",
				slot_after_initializer_artifact, stereo_eye_slot_artifacts[0]);
			append_delta_manifest(manifest, "natural_to_right_eye_slot",
				slot_after_initializer_artifact, stereo_eye_slot_artifacts[1]);
			append_delta_manifest(manifest, "backend_view_source_stability",
				backend_view_source_before_artifact, backend_view_source_after_artifact);
			append_delta_manifest(manifest, "backend_source_to_bound_left",
				backend_view_source_before_artifact, backend_bound_eye_slot_artifacts[0]);
			append_delta_manifest(manifest, "backend_source_to_bound_right",
				backend_view_source_before_artifact, backend_bound_eye_slot_artifacts[1]);
			append_delta_manifest(manifest, "backend_target_prepare_record",
				record_before_target_prepare_artifact,
				record_after_target_prepare_artifact);
			append_delta_manifest(manifest, "target_registry_during_prepare",
				registry_before_target_prepare_artifact,
				registry_after_target_prepare_artifact);
			const auto manifest_bytes = manifest.str();
			const auto manifest_signature = manifest_bytes.empty() ? 0 :
				hash_memory_region(reinterpret_cast<std::uintptr_t>(
					manifest_bytes.data()), manifest_bytes.size());
			// Version and persist the exact final bytes. The manifest embeds the full
			// execution-report hash above, so a current-process write is tied to the
			// immutable terminal census rather than a repeating readiness pattern.
			const auto aggregate_manifest_complete = write_evidence_text_versioned(
				artifact_manifest_signature, "manifest.txt", manifest_bytes,
				manifest_signature);

			if (observation_complete && !artifact_completion_announced.exchange(true,
				std::memory_order_acq_rel))
			{
				console::info("[VR] backend observation evidence complete; read-only full-frame D3D11 execution census is active\n");
			}
			if (execution_terminal &&
				!backend_execution_completion_announced.exchange(true,
					std::memory_order_acq_rel))
			{
				if (execution.state == engine_stereo_execution::gate_state::failed)
				{
					console::error("[VR] full-frame D3D11 execution census failed: %s; automatic status persistence is pending\n",
						engine_stereo_execution::to_string(execution.error));
				}
				else if (execution.deferred_execution_opaque)
				{
					console::error("[VR] full-frame D3D11 execution census is opaque and unacceptable; automatic status persistence is pending\n");
				}
				else if (execution_complete)
				{
					console::info("[VR] full-frame D3D11 execution census complete; automatic status persistence is pending\n");
				}
				else
				{
					console::error("[VR] full-frame D3D11 execution census reached a terminal state but strict acceptance rejected incomplete evidence; automatic status persistence is pending\n");
				}
			}

			// The pre-R_EndFrame dual-record mutation is retired. Same-process A/B
			// evidence showed that the one frame in which it ran caused H2's
			// "Tried to use (null)" fatal drop, while the next level entry in the
			// same process succeeded after the one-shot state stopped mutating H2.
			// Replace the failed experiment's artifact writer with an explicit retired
			// manifest so stale files cannot be mistaken for a runtime path.
			if (!retired_dual_record_manifest_written.load(std::memory_order_acquire))
			{
				const std::string retired_manifest =
					"state=retired\r\n"
					"contract=none\r\n"
					"frontend_mutation=disabled\r\n"
					"gpu_transport=hard_unarmed\r\n"
					"reason=same_process_A_B_proved_pre_R_EndFrame_scene_synthesis_caused_H2_fatal_drop\r\n"
					"replacement=record_local_stereo_view_slot_derivation\r\n";
				if (utils::io::write_file_atomic(std::string{evidence_directory} +
					"/dual-record-manifest.txt", retired_manifest))
				{
					retired_dual_record_manifest_written.store(true,
						std::memory_order_release);
				}
			}
			if (!retired_owner_stereo_manifest_written.load(std::memory_order_acquire))
			{
				const std::string retired_manifest =
					"state=retired\r\n"
					"contract=none\r\n"
					"activation_command=removed\r\n"
					"scene_owner_detour=removed\r\n"
					"gpu_transport=hard_unarmed\r\n"
					"reason=same_build_A_B_proved_second_owner_call_corrupts_surface_builder_transient_state\r\n"
					"replacement=record_local_stereo_view_slot_derivation\r\n";
				if (utils::io::write_file_atomic(std::string{evidence_directory} +
					"/owner-stereo-manifest.txt", retired_manifest))
				{
					retired_owner_stereo_manifest_written.store(true,
						std::memory_order_release);
				}
			}

			return execution_terminal && execution_report_ready &&
				execution_manifest_complete && aggregate_manifest_complete;
		}
		catch (...)
		{
			// Evidence persistence is diagnostic-only. Never let control-plane I/O
			// unwind into the monitoring thread or alter H2/OpenVR execution.
			return false;
		}
	}

	void observe_camera_state(const camera_observation_source source,
		const camera_observation_stage stage, const void* const input,
		const void* const output, const float scalar,
		const std::uintptr_t caller) noexcept
	{
		if (!view_diagnostics_enabled() || !engine_view_probe::is_enabled()) return;

		engine_view_probe::camera_state_source probe_source{};
		switch (source)
		{
		case camera_observation_source::set_viewpos_now:
			probe_source = engine_view_probe::camera_state_source::set_viewpos_now;
			break;
		case camera_observation_source::camera_helper:
			probe_source = engine_view_probe::camera_state_source::camera_helper;
			break;
		default:
			return;
		}

		engine_view_probe::observation_stage probe_stage{};
		switch (stage)
		{
		case camera_observation_stage::enter:
			probe_stage = engine_view_probe::observation_stage::enter;
			break;
		case camera_observation_stage::before_original:
			probe_stage = engine_view_probe::observation_stage::before_call;
			break;
		case camera_observation_stage::after_original:
			probe_stage = engine_view_probe::observation_stage::after_call;
			break;
		case camera_observation_stage::after_mod:
			probe_stage = engine_view_probe::observation_stage::snapshot;
			break;
		case camera_observation_stage::leave:
			probe_stage = engine_view_probe::observation_stage::leave;
			break;
		default:
			return;
		}

		// 0x140781090 reads only through input+0x18 and writes output+0x24.
		// set_viewpos_now's opaque argument has no recovered extent and is therefore
		// recorded by identity only. game::refdef_t is the proven 0x50-byte prefix.
		const auto helper = source == camera_observation_source::camera_helper;
		const auto observation = engine_view_probe::camera_state_observation{
			probe_source,
			engine_view_probe::float_bits(scalar),
			caller,
			reinterpret_cast<std::uintptr_t>(input),
			reinterpret_cast<std::uintptr_t>(output),
			hash_memory_region(reinterpret_cast<std::uintptr_t>(input), helper ? 0x1C : 0),
			hash_memory_region(reinterpret_cast<std::uintptr_t>(output),
				helper ? camera_globals_size : 0),
			hash_scene_descriptor_prefix(game::refdef),
			hash_memory_region(owner_view_globals, owner_view_globals_size),
		};

		if (active_view_transaction != nullptr && active_view_transaction->token)
		{
			engine_view_probe::record_camera_state(active_view_transaction->token,
				observation, probe_stage, active_view_transaction->flags);
			return;
		}

		const auto correlation = current_frontend_correlation();
		engine_view_probe::record_unscoped_camera_state(correlation.frame_id,
			observation, probe_stage, correlation.flags);
	}

	void begin_frame_state_transition() noexcept
	{
		const auto depth = ++frame_state_transition_depth;
		if (depth == 1)
		{
			active_boundary_frame_id = frontend_epoch_sequence.fetch_add(
				1, std::memory_order_relaxed) + 1;
		}
		record_ownership_boundary(
			engine_view_probe::ownership_boundary_kind::frame_state_transition,
			engine_view_probe::observation_stage::enter, depth);
	}

	void end_frame_state_transition() noexcept
	{
		if (frame_state_transition_depth == 0) return;
		record_ownership_boundary(
			engine_view_probe::ownership_boundary_kind::frame_state_transition,
			engine_view_probe::observation_stage::leave,
			frame_state_transition_depth);
		--frame_state_transition_depth;
	}

	void begin_r_end_frame() noexcept
	{
		// R_EndFrame is the renderer-thread closing boundary for the records built
		// immediately before it. A reservation that survived to this point cannot
		// belong to the following frontend epoch.
		pending_reservation = {};
		const auto depth = ++r_end_frame_depth;
		const auto frame_id = ensure_boundary_frame_id();
		if (depth > r_end_frame_scopes.size()) return;
		auto& boundary = r_end_frame_scopes[depth - 1];
		boundary = {};
		boundary.frame_id = frame_id;
		if (!view_diagnostics_enabled() || !engine_view_probe::is_enabled()) return;
		boundary.before = read_frontend_snapshot();
		boundary.trace_active = true;
		record_ownership_boundary(
			engine_view_probe::ownership_boundary_kind::command_cleanup,
			engine_view_probe::observation_stage::enter, depth);
		engine_view_probe::record_frame_flip(boundary.frame_id, depth, {
			boundary.before.frontend,
			boundary.before.frontend,
			boundary.before.global_selector,
			boundary.before.global_selector,
			boundary.before.slot_count,
			boundary.before.slot_count,
			boundary.before.current_record_index,
			boundary.before.current_record_index,
			boundary.before.record_count,
			boundary.before.record_count,
			boundary.before.global_record_count,
			boundary.before.global_record_count,
			0, // Entering the original H2 R_EndFrame.
		});
	}

	void end_r_end_frame() noexcept
	{
		if (r_end_frame_depth == 0) return;
		const auto depth = r_end_frame_depth;
		if (depth <= r_end_frame_scopes.size())
		{
			auto& boundary = r_end_frame_scopes[depth - 1];
			if (boundary.trace_active)
			{
				const auto after = read_frontend_snapshot();
				record_ownership_boundary(
					engine_view_probe::ownership_boundary_kind::command_cleanup,
					engine_view_probe::observation_stage::leave, depth);
				engine_view_probe::record_frame_flip(boundary.frame_id, depth, {
					boundary.before.frontend,
					after.frontend,
					boundary.before.global_selector,
					after.global_selector,
					boundary.before.slot_count,
					after.slot_count,
					boundary.before.current_record_index,
					after.current_record_index,
					boundary.before.record_count,
					after.record_count,
					boundary.before.global_record_count,
					after.global_record_count,
					1, // Original H2 R_EndFrame returned.
				});
			}
			boundary = {};
		}
		--r_end_frame_depth;
	}

	void begin_frontend_handoff() noexcept
	{
		const auto depth = ++frontend_handoff_depth;
		record_ownership_boundary(
			engine_view_probe::ownership_boundary_kind::frontend_handoff,
			engine_view_probe::observation_stage::enter, depth);
	}

	void end_frontend_handoff() noexcept
	{
		if (frontend_handoff_depth == 0) return;
		const auto depth = frontend_handoff_depth;
		record_ownership_boundary(
			engine_view_probe::ownership_boundary_kind::frontend_handoff,
			engine_view_probe::observation_stage::leave, depth);
		--frontend_handoff_depth;
		if (frontend_handoff_depth == 0)
		{
			active_boundary_frame_id = 0;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			engine_scene_completion::validate();
			engine_scene_resolution::install();
			engine_scene_job_capture::install();
			const auto installed = install_hook();
			engine_stereo_bridge::set_render_hook_installed(installed);
			if (installed)
			{
				console::info("[VR] installed CPU-only H2 frontend/backend/query observation hooks\n");
			}
		}

		void pre_destroy() override
		{
			engine_stereo_bridge::set_render_hook_installed(false);
		}
	};
}

REGISTER_COMPONENT(vr::engine_stereo_renderer::component)
