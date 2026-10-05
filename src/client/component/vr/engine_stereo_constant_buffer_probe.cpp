#include <std_include.hpp>

#include "engine_stereo_constant_buffer_probe.hpp"


#include "component/d3d11.hpp"
#include "utils/hook.hpp"
#include "utils/hook_validation.hpp"

#include <d3dcompiler.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <type_traits>

#pragma comment(lib, "d3dcompiler.lib")

namespace vr::engine_stereo_constant_buffer_probe
{
	namespace
	{
		constexpr std::uint32_t invalid_eye = 2;
		constexpr std::uint32_t observed_slot = 3;
		constexpr std::array<std::size_t, shader_stage_count> set_vtable_slots{
			7, 16, 22, 62, 66, 71,
		};
		constexpr std::size_t map_vtable_slot = 14;
		constexpr std::size_t unmap_vtable_slot = 15;
		constexpr std::size_t maximum_constant_buffer_bytes =
			D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
		constexpr std::uint8_t maximum_private_data_attempts = 4;
		constexpr UINT maximum_reflected_bindings = 1024;
		static_assert((maximum_tracked_buffers & (maximum_tracked_buffers - 1)) == 0);
		static_assert((maximum_tracked_vertex_shaders &
			(maximum_tracked_vertex_shaders - 1)) == 0);

		using set_constant_buffers_fn = void(__stdcall*)(ID3D11DeviceContext*, UINT,
			UINT, ID3D11Buffer* const*);
		using map_fn = HRESULT(__stdcall*)(ID3D11DeviceContext*, ID3D11Resource*, UINT,
			D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
		using unmap_fn = void(__stdcall*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);

		utils::hook::detour vs_set_hook;
		utils::hook::detour ps_set_hook;
		utils::hook::detour gs_set_hook;
		utils::hook::detour hs_set_hook;
		utils::hook::detour ds_set_hook;
		utils::hook::detour cs_set_hook;
		utils::hook::detour map_hook;
		utils::hook::detour unmap_hook;

		std::mutex hook_mutex;
		std::mutex history_tracking_mutex;
		std::mutex lifecycle_mutex;
		std::mutex buffer_identity_mutex;
		std::mutex shader_identity_mutex;
		std::mutex copy_source_sample_mutex;
		constexpr GUID buffer_creation_serial_guid{
			0x1da45675, 0xd8a1, 0x4f76, {0x83, 0xd6, 0x10, 0x36, 0x3c, 0xc9, 0x4b, 0x2f},
		};
		constexpr GUID shader_creation_serial_guid{
			0x75e25078, 0x044c, 0x4716, {0x90, 0xc7, 0x28, 0x18, 0x2d, 0xeb, 0x3f, 0xf1},
		};
		std::atomic_bool hooks_installed{};
		std::atomic_bool installation_poisoned{};
		std::atomic_bool history_tracking_enabled{};
		std::atomic_uint32_t history_tracking_clients{};
		std::atomic_uint64_t history_tracking_epoch{};
		std::atomic_uint32_t active_report_callbacks{};
		std::atomic_uint64_t callback_quiescence_timeouts{};
		// These atomics identify the explicitly selected H2 device. Early hook
		// installation can observe several process-local D3D11 candidates.
		std::atomic_uintptr_t expected_device{};
		std::atomic_uintptr_t expected_context{};
		std::atomic_uint64_t expected_generation{};
		std::atomic_uint64_t candidate_registration_overflows{};
		std::atomic_uint64_t unregistered_context_calls{};
		std::atomic_bool resource_observer_attached{};
		std::atomic_bool resource_observer_covers_future_candidates{};
		std::atomic_bool selected_resource_history_complete{};
		std::array<std::atomic_uintptr_t, shader_stage_count> set_hook_targets{};
		std::atomic_uintptr_t map_hook_target{};
		std::atomic_uintptr_t unmap_hook_target{};
		std::array<std::atomic_uint64_t, shader_stage_count> set_calls{};
		std::atomic_uint64_t hook_failures{};
		std::atomic_uint64_t foreign_context_calls{};
		std::atomic_uint64_t foreign_thread_pair_calls{};
		std::atomic_uint64_t map_calls{};
		std::atomic_uint64_t unmap_calls{};
		std::atomic_uint64_t mapped_uploads{};
		std::atomic_uint64_t unmatched_unmaps{};
		std::atomic_uint64_t pending_map_overflows{};
		std::atomic_uint64_t tracked_buffer_overflows{};
		std::atomic_uint64_t tracked_shader_overflows{};
		std::atomic_uint64_t buffer_identity_tag_failures{};
		std::atomic_uint64_t shader_identity_tag_failures{};
		std::atomic_uint64_t buffer_identity_reuses{};
		std::atomic_uint64_t shader_identity_reuses{};
		std::atomic_uint64_t concurrent_update_drops{};
		std::atomic_uint64_t unstable_content_reads{};
		std::atomic_uint64_t shader_private_data_missing{};
		std::atomic_uint64_t shader_private_data_oversized{};
		std::atomic_uint64_t shader_reflection_failures{};
		std::atomic_uint64_t resource_updates{};
		std::atomic_uint64_t resource_copies{};
		std::atomic_uint64_t resource_unknown_writes{};
		std::atomic_uint64_t copy_source_candidates{};
		std::atomic_uint64_t copy_source_registered{};
		std::atomic_uint64_t copy_source_known{};
		std::atomic_uint64_t copy_source_bytes_propagated{};
		std::atomic_uint64_t next_creation_serial{};
		std::atomic_uint64_t next_shader_creation_serial{};
		std::atomic_uint64_t next_upload_generation{};
		std::atomic_uint64_t next_set_sequence{};

		std::atomic<state> current_state{state::idle};
		std::atomic_uint64_t active_pair{};
		std::atomic_uint32_t active_eye{invalid_eye};
		std::atomic_uint32_t active_owner_thread{};

		struct buffer_entry
		{
			std::atomic_uintptr_t identity{};
			std::atomic_uint64_t sequence{};
			std::atomic_uint64_t device_generation{};
			std::atomic_uint64_t creation_serial{};
			std::atomic_uint64_t upload_generation{};
			std::atomic_uint64_t history_epoch{};
			std::atomic_uint64_t hash_low{};
			std::atomic_uint64_t hash_high{};
			std::atomic_uintptr_t upload_caller{};
			std::atomic_uint32_t byte_width{};
			std::atomic_uint32_t upload_thread{};
			std::atomic<upload_source> source{upload_source::unknown};
			std::atomic_bool known{};
			std::atomic_uint32_t captured_bytes{};
			std::atomic_bool captured_bytes_complete{};
			std::array<std::atomic<std::uint8_t>, maximum_content_byte_snapshot>
				content_bytes{};
		};

		struct shader_entry
		{
			std::atomic_uintptr_t identity{};
			std::atomic_uint64_t device_generation{};
			std::atomic_uint64_t creation_serial{};
			std::atomic_uint8_t private_data_attempts{};
			// 0 is resolving/not observed, 1 unused, 2 used, 3 terminal unknown.
			std::atomic_uint8_t usage{};
		};

		struct global_slot_state
		{
			std::atomic_uint64_t sequence_lock{};
			std::atomic_uint64_t set_sequence{};
			std::atomic_uintptr_t buffer{};
			std::atomic_uintptr_t caller{};
			std::atomic_uint32_t thread{};
			std::atomic<bind_origin> origin{bind_origin::unknown};
		};

		struct pending_map
		{
			ID3D11DeviceContext* context{};
			ID3D11Resource* resource{};
			std::uint64_t device_generation{};
			void* data{};
			std::uintptr_t caller{};
			std::uint32_t subresource{};
			std::uint32_t byte_width{};
			std::uint64_t history_epoch{};
			D3D11_MAP type{D3D11_MAP_READ};
			bool active{};
		};

		struct draw_record
		{
			std::uint64_t ordinal{};
			std::uintptr_t caller{};
			slot_snapshot slot{};
		};

		struct device_candidate
		{
			std::atomic_uintptr_t context{};
			std::atomic_uintptr_t device{};
			std::atomic_uint64_t generation{};
			std::atomic_bool resource_history_complete{};
			// Owned references prevent a destroyed context from being replaced at
			// the same address while it is still present in the candidate table.
			ID3D11Device* held_device{};
			ID3D11DeviceContext* held_context{};
		};

		std::array<buffer_entry, maximum_tracked_buffers> buffers{};
		std::array<shader_entry, maximum_tracked_vertex_shaders> shaders{};
		std::array<device_candidate, maximum_device_candidates> device_candidates{};
		global_slot_state current_slot{};
		thread_local std::array<pending_map, maximum_pending_maps_per_thread>
			pending_maps{};
		thread_local std::uint32_t pending_map_count{};
		thread_local std::array<std::byte, maximum_shader_bytecode_bytes>
			shader_bytecode_scratch{};
		std::array<draw_record, maximum_draws_per_eye> left_draws{};
		std::array<copy_source_sample, maximum_copy_source_samples>
			copy_source_samples{};
		std::size_t copy_source_sample_count{};
		std::uint64_t copy_source_sample_overflows{};
		report working_report{};
		report published_report{};

		template <typename T>
		void clear_trivial(T& value) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			std::memset(&value, 0, sizeof(value));
		}

		[[nodiscard]] constexpr std::size_t stage_index(
			const shader_stage value) noexcept
		{
			return static_cast<std::size_t>(value);
		}

		[[nodiscard]] std::array<utils::hook::detour*, shader_stage_count>
			set_hooks() noexcept
		{
			return {&vs_set_hook, &ps_set_hook, &gs_set_hook,
				&hs_set_hook, &ds_set_hook, &cs_set_hook};
		}

		struct candidate_identity
		{
			std::uintptr_t device{};
			std::uint64_t generation{};
		};

		[[nodiscard]] candidate_identity find_candidate(
			ID3D11DeviceContext* const context) noexcept
		{
			const auto identity = reinterpret_cast<std::uintptr_t>(context);
			if (identity == 0) return {};
			for (const auto& candidate : device_candidates)
			{
				const auto observed = candidate.context.load(std::memory_order_acquire);
				if (observed != identity) continue;
				candidate_identity output{};
				output.device = candidate.device.load(std::memory_order_acquire);
				output.generation = candidate.generation.load(std::memory_order_acquire);
				if (output.device != 0 && output.generation != 0) return output;
				return {};
			}
			return {};
		}

		class active_report_callback
		{
		public:
			explicit active_report_callback(
				ID3D11DeviceContext* const context) noexcept
			{
				const auto identity = reinterpret_cast<std::uintptr_t>(context);
				if (identity == 0 || expected_context.load(std::memory_order_acquire) !=
					identity)
				{
					foreign_context_calls.fetch_add(1, std::memory_order_relaxed);
					return;
				}
				active_report_callbacks.fetch_add(1, std::memory_order_acq_rel);
				if (expected_context.load(std::memory_order_acquire) != identity)
				{
					active_report_callbacks.fetch_sub(1, std::memory_order_release);
					return;
				}
				active_ = true;
			}

			~active_report_callback()
			{
				if (active_)
					active_report_callbacks.fetch_sub(1, std::memory_order_release);
			}

			active_report_callback(const active_report_callback&) = delete;
			active_report_callback& operator=(const active_report_callback&) = delete;
			[[nodiscard]] explicit operator bool() const noexcept { return active_; }

		private:
			bool active_{};
		};

		[[nodiscard]] bool wait_for_report_callbacks() noexcept
		{
			const auto started = GetTickCount64();
			while (active_report_callbacks.load(std::memory_order_acquire) != 0)
			{
				if (GetTickCount64() - started >= 250)
				{
					callback_quiescence_timeouts.fetch_add(1,
						std::memory_order_relaxed);
					return false;
				}
				SwitchToThread();
			}
			return true;
		}

		[[nodiscard]] std::uint64_t registered_candidate_count() noexcept
		{
			std::uint64_t count{};
			for (const auto& candidate : device_candidates)
				if (candidate.context.load(std::memory_order_acquire) != 0) ++count;
			return count;
		}

		[[nodiscard]] bool register_candidate(ID3D11Device* const device,
			ID3D11DeviceContext* const context,
			const std::uint64_t device_generation) noexcept
		{
			const auto context_identity = reinterpret_cast<std::uintptr_t>(context);
			const auto device_identity = reinterpret_cast<std::uintptr_t>(device);
			for (auto& candidate : device_candidates)
			{
				if (candidate.context.load(std::memory_order_acquire) != context_identity)
					continue;
				return candidate.device.load(std::memory_order_acquire) == device_identity &&
					candidate.generation.load(std::memory_order_acquire) == device_generation;
			}
			for (auto& candidate : device_candidates)
			{
				if (candidate.context.load(std::memory_order_acquire) != 0) continue;
				device->AddRef();
				context->AddRef();
				candidate.held_device = device;
				candidate.held_context = context;
				candidate.device.store(device_identity, std::memory_order_relaxed);
				candidate.generation.store(device_generation, std::memory_order_relaxed);
				candidate.resource_history_complete.store(
					resource_observer_attached.load(std::memory_order_acquire) &&
					resource_observer_covers_future_candidates.load(
						std::memory_order_acquire),
					std::memory_order_relaxed);
				candidate.context.store(context_identity, std::memory_order_release);
				return true;
			}
			candidate_registration_overflows.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		struct candidate_references
		{
			ID3D11Device* device{};
			ID3D11DeviceContext* context{};
		};

		[[nodiscard]] candidate_references detach_candidate(
			device_candidate& candidate) noexcept
		{
			candidate.context.store(0, std::memory_order_release);
			candidate.device.store(0, std::memory_order_relaxed);
			candidate.generation.store(0, std::memory_order_relaxed);
			candidate.resource_history_complete.store(false,
				std::memory_order_relaxed);
			const candidate_references output{candidate.held_device,
				candidate.held_context};
			candidate.held_context = nullptr;
			candidate.held_device = nullptr;
			return output;
		}

		void release_candidate_references(
			const candidate_references& references) noexcept
		{
			if (references.context != nullptr) references.context->Release();
			if (references.device != nullptr) references.device->Release();
		}

		[[nodiscard]] bool get_or_assign_creation_serial(
			ID3D11DeviceChild* const object, const GUID& guid,
			std::atomic_uint64_t& serial_source, std::mutex& assignment_mutex,
			std::atomic_uint64_t& failures, std::uint64_t& output) noexcept
		{
			output = 0;
			if (object == nullptr) return false;
			UINT size = sizeof(output);
			auto result = object->GetPrivateData(guid, &size, &output);
			if (SUCCEEDED(result) && size == sizeof(output) && output != 0) return true;

			const std::lock_guard lock(assignment_mutex);
			output = 0;
			size = sizeof(output);
			result = object->GetPrivateData(guid, &size, &output);
			if (SUCCEEDED(result) && size == sizeof(output) && output != 0) return true;

			auto serial = serial_source.fetch_add(1, std::memory_order_relaxed) + 1;
			if (serial == 0)
				serial = serial_source.fetch_add(1, std::memory_order_relaxed) + 1;
			result = object->SetPrivateData(guid, sizeof(serial), &serial);
			if (FAILED(result))
			{
				failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}

			std::uint64_t verified{};
			size = sizeof(verified);
			result = object->GetPrivateData(guid, &size, &verified);
			if (FAILED(result) || size != sizeof(verified) || verified != serial)
			{
				failures.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			output = verified;
			return true;
		}

		void reset_buffer_table() noexcept
		{
			for (auto& entry : buffers)
			{
				entry.identity.store(0, std::memory_order_relaxed);
				entry.sequence.store(0, std::memory_order_relaxed);
				entry.device_generation.store(0, std::memory_order_relaxed);
				entry.creation_serial.store(0, std::memory_order_relaxed);
				entry.upload_generation.store(0, std::memory_order_relaxed);
				entry.history_epoch.store(0, std::memory_order_relaxed);
				entry.hash_low.store(0, std::memory_order_relaxed);
				entry.hash_high.store(0, std::memory_order_relaxed);
				entry.upload_caller.store(0, std::memory_order_relaxed);
				entry.byte_width.store(0, std::memory_order_relaxed);
				entry.upload_thread.store(0, std::memory_order_relaxed);
				entry.source.store(upload_source::unknown, std::memory_order_relaxed);
				entry.known.store(false, std::memory_order_relaxed);
			}
			for (auto& entry : shaders)
			{
				entry.identity.store(0, std::memory_order_relaxed);
				entry.device_generation.store(0, std::memory_order_relaxed);
				entry.creation_serial.store(0, std::memory_order_relaxed);
				entry.private_data_attempts.store(0, std::memory_order_relaxed);
				entry.usage.store(0, std::memory_order_relaxed);
			}
			for (auto& candidate : device_candidates)
			{
				release_candidate_references(detach_candidate(candidate));
			}
			current_slot.sequence_lock.store(0, std::memory_order_relaxed);
			current_slot.set_sequence.store(0, std::memory_order_relaxed);
			current_slot.buffer.store(0, std::memory_order_relaxed);
			current_slot.caller.store(0, std::memory_order_relaxed);
			current_slot.thread.store(0, std::memory_order_relaxed);
			current_slot.origin.store(bind_origin::unknown, std::memory_order_relaxed);
			for (auto& value : pending_maps) value = {};
			pending_map_count = 0;
			clear_trivial(left_draws);
			next_creation_serial.store(0, std::memory_order_relaxed);
			next_shader_creation_serial.store(0, std::memory_order_relaxed);
			next_upload_generation.store(0, std::memory_order_relaxed);
			next_set_sequence.store(0, std::memory_order_relaxed);
		}

		[[nodiscard]] buffer_entry* find_buffer_entry(
			const std::uintptr_t identity) noexcept
		{
			if (identity == 0 || (identity & 1u) != 0) return nullptr;
			auto index = (identity >> 4) & (maximum_tracked_buffers - 1);
			for (std::size_t probe{}; probe < maximum_tracked_buffers; ++probe)
			{
				auto& entry = buffers[index];
				const auto existing = entry.identity.load(std::memory_order_acquire);
				if (existing == identity) return &entry;
				if (existing == (identity | 1u))
				{
					for (std::size_t attempt{}; attempt < 8; ++attempt)
					{
						if (entry.identity.load(std::memory_order_acquire) == identity)
							return &entry;
					}
					return nullptr;
				}
				if (existing == 0) return nullptr;
				index = (index + 1) & (maximum_tracked_buffers - 1);
			}
			return nullptr;
		}

		[[nodiscard]] bool acquire_write(buffer_entry& entry,
			std::uint64_t& stable_sequence) noexcept
		{
			for (std::size_t attempt{}; attempt < 8; ++attempt)
			{
				auto expected = entry.sequence.load(std::memory_order_acquire);
				if (expected & 1u) continue;
				if (entry.sequence.compare_exchange_weak(expected, expected + 1,
					std::memory_order_acq_rel, std::memory_order_acquire))
				{
					stable_sequence = expected;
					return true;
				}
			}
			concurrent_update_drops.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		void release_write(buffer_entry& entry,
			const std::uint64_t stable_sequence) noexcept
		{
			entry.sequence.store(stable_sequence + 2, std::memory_order_release);
		}

		void note_copy_source_sample(const content_snapshot& destination_content,
			ID3D11Resource* const source_resource,
			const D3D11_RESOURCE_DIMENSION source_dimension,
			const D3D11_BUFFER_DESC* const source_description,
			const content_snapshot& source_content, const bool registered,
			const bool known, const bool bytes_available,
			const std::uintptr_t caller) noexcept
		{
			constexpr std::uint32_t material_buffer_bytes = 1088;
			if (destination_content.byte_width != material_buffer_bytes) return;

			const auto source_identity = reinterpret_cast<std::uintptr_t>(source_resource);
			const std::lock_guard lock(copy_source_sample_mutex);
			copy_source_sample* sample{};
			for (std::size_t index{}; index < copy_source_sample_count; ++index)
			{
				auto& candidate = copy_source_samples[index];
				if (candidate.destination == destination_content.buffer &&
					candidate.destination_creation_serial ==
						destination_content.creation_serial &&
					candidate.source == source_identity &&
					candidate.source_creation_serial == source_content.creation_serial)
				{
					sample = &candidate;
					break;
				}
			}
			if (sample == nullptr)
			{
				if (copy_source_sample_count >= copy_source_samples.size())
				{
					++copy_source_sample_overflows;
					return;
				}
				sample = &copy_source_samples[copy_source_sample_count++];
				*sample = {};
				sample->destination = destination_content.buffer;
				sample->source = source_identity;
				sample->device_generation = destination_content.device_generation;
				sample->destination_creation_serial =
					destination_content.creation_serial;
				sample->source_creation_serial = source_content.creation_serial;
				sample->destination_byte_width = destination_content.byte_width;
				sample->source_dimension = static_cast<std::uint32_t>(source_dimension);
				if (source_description != nullptr)
				{
					sample->source_byte_width = source_description->ByteWidth;
					sample->source_usage = static_cast<std::uint32_t>(
						source_description->Usage);
					sample->source_bind_flags = source_description->BindFlags;
					sample->source_cpu_access_flags = source_description->CPUAccessFlags;
					sample->source_misc_flags = source_description->MiscFlags;
					sample->source_structure_stride =
						source_description->StructureByteStride;
				}
			}

			++sample->observations;
			sample->copy_caller = caller;
			sample->copy_thread = GetCurrentThreadId();
			sample->source_upload_generation = source_content.upload_generation;
			sample->source_upload_caller = source_content.upload_caller;
			sample->source_upload_thread = source_content.upload_thread;
			sample->source_content_origin = source_content.source;
			if (registered) ++sample->registered_observations;
			if (known) ++sample->known_observations;
			if (bytes_available) ++sample->byte_snapshot_observations;

			if (source_resource == nullptr) ++sample->null_source_observations;
			else if (source_dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
				++sample->non_buffer_observations;
			else if (source_description == nullptr ||
				source_description->ByteWidth != destination_content.byte_width)
				++sample->size_mismatch_observations;
			else if (!registered) ++sample->registration_failure_observations;
			else if (!known) ++sample->unknown_content_observations;
			else if (!bytes_available) ++sample->byte_snapshot_failure_observations;
		}

		[[nodiscard]] buffer_entry* find_or_register_buffer(
			ID3D11Buffer* const buffer,
			const std::uint64_t device_generation,
			const bool allow_non_constant_copy_source = false,
			const std::uint32_t expected_copy_bytes = 0) noexcept
		{
			if (buffer == nullptr || device_generation == 0) return nullptr;
			const auto identity = reinterpret_cast<std::uintptr_t>(buffer);
			if ((identity & 1u) != 0) return nullptr;
			std::uint64_t object_serial{};
			if (!get_or_assign_creation_serial(buffer, buffer_creation_serial_guid,
				next_creation_serial, buffer_identity_mutex,
				buffer_identity_tag_failures, object_serial))
			{
				return nullptr;
			}
			if (auto* const existing = find_buffer_entry(identity))
			{
				bool stable{};
				std::uint64_t tracked_serial{};
				std::uint64_t tracked_generation{};
				for (std::size_t attempt{}; attempt < 4; ++attempt)
				{
					const auto before = existing->sequence.load(std::memory_order_acquire);
					if (before & 1u) continue;
					tracked_serial = existing->creation_serial.load(
						std::memory_order_relaxed);
					tracked_generation = existing->device_generation.load(
						std::memory_order_relaxed);
					const auto after = existing->sequence.load(std::memory_order_acquire);
					if (before == after && !(after & 1u))
					{
						stable = true;
						break;
					}
				}
				if (!stable)
				{
					unstable_content_reads.fetch_add(1, std::memory_order_relaxed);
					return nullptr;
				}
				if (tracked_serial == object_serial &&
					tracked_generation == device_generation) return existing;

				D3D11_BUFFER_DESC replacement_description{};
				buffer->GetDesc(&replacement_description);
				const auto replacement_constant =
					(replacement_description.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
				const auto replacement_copy_source = allow_non_constant_copy_source &&
					expected_copy_bytes != 0 &&
					replacement_description.ByteWidth == expected_copy_bytes;
				if ((!replacement_constant && !replacement_copy_source) ||
					replacement_description.ByteWidth == 0 ||
					replacement_description.ByteWidth > maximum_constant_buffer_bytes)
				{
					return nullptr;
				}
				std::uint64_t sequence{};
				if (!acquire_write(*existing, sequence)) return nullptr;
				if (existing->creation_serial.load(std::memory_order_relaxed) !=
						object_serial ||
					existing->device_generation.load(std::memory_order_relaxed) !=
						device_generation)
				{
					existing->device_generation.store(device_generation,
						std::memory_order_relaxed);
					existing->creation_serial.store(object_serial,
						std::memory_order_relaxed);
					existing->upload_generation.store(0, std::memory_order_relaxed);
					existing->hash_low.store(0, std::memory_order_relaxed);
					existing->hash_high.store(0, std::memory_order_relaxed);
					existing->upload_caller.store(0, std::memory_order_relaxed);
					existing->byte_width.store(replacement_description.ByteWidth,
						std::memory_order_relaxed);
					existing->upload_thread.store(0, std::memory_order_relaxed);
					existing->source.store(upload_source::unknown,
						std::memory_order_relaxed);
					existing->known.store(false, std::memory_order_relaxed);
					existing->captured_bytes.store(0, std::memory_order_relaxed);
					existing->captured_bytes_complete.store(false,
						std::memory_order_relaxed);
					buffer_identity_reuses.fetch_add(1, std::memory_order_relaxed);
				}
				release_write(*existing, sequence);
				return existing;
			}

			D3D11_BUFFER_DESC description{};
			buffer->GetDesc(&description);
			const auto constant_buffer =
				(description.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
			const auto copy_source = allow_non_constant_copy_source &&
				expected_copy_bytes != 0 && description.ByteWidth == expected_copy_bytes;
			if ((!constant_buffer && !copy_source) || description.ByteWidth == 0 ||
				description.ByteWidth > maximum_constant_buffer_bytes)
			{
				return nullptr;
			}

			auto index = (identity >> 4) & (maximum_tracked_buffers - 1);
			for (std::size_t probe{}; probe < maximum_tracked_buffers; ++probe)
			{
				auto& entry = buffers[index];
				auto expected = std::uintptr_t{};
				if (entry.identity.compare_exchange_strong(expected, identity | 1u,
					std::memory_order_acq_rel, std::memory_order_acquire))
				{
					entry.sequence.store(1, std::memory_order_relaxed);
					entry.device_generation.store(device_generation,
						std::memory_order_relaxed);
					entry.creation_serial.store(object_serial, std::memory_order_relaxed);
					entry.upload_generation.store(0, std::memory_order_relaxed);
					entry.hash_low.store(0, std::memory_order_relaxed);
					entry.hash_high.store(0, std::memory_order_relaxed);
					entry.upload_caller.store(0, std::memory_order_relaxed);
					entry.byte_width.store(description.ByteWidth, std::memory_order_relaxed);
					entry.upload_thread.store(0, std::memory_order_relaxed);
					entry.source.store(upload_source::unknown, std::memory_order_relaxed);
					// Without a CreateBuffer detour, initial data is deliberately
					// unknown until an observed CPU upload supplies exact bytes.
					entry.known.store(false, std::memory_order_relaxed);
					entry.captured_bytes.store(0, std::memory_order_relaxed);
					entry.captured_bytes_complete.store(false,
						std::memory_order_relaxed);
					entry.sequence.store(2, std::memory_order_release);
					entry.identity.store(identity, std::memory_order_release);
					return &entry;
				}
				if (expected == identity) return &entry;
				if (expected == (identity | 1u))
				{
					if (auto* const initialized = find_buffer_entry(identity))
						return initialized;
					return nullptr;
				}
				index = (index + 1) & (maximum_tracked_buffers - 1);
			}
			tracked_buffer_overflows.fetch_add(1, std::memory_order_relaxed);
			return nullptr;
		}

		[[nodiscard]] buffer_entry* find_or_register_resource(
			ID3D11Resource* const resource,
			const std::uint64_t device_generation) noexcept
		{
			if (resource == nullptr || device_generation == 0) return nullptr;
			D3D11_RESOURCE_DIMENSION dimension{};
			resource->GetType(&dimension);
			if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER) return nullptr;
			return find_or_register_buffer(static_cast<ID3D11Buffer*>(resource),
				device_generation);
		}

		[[nodiscard]] bool hash_bytes_guarded(const void* const source,
			const std::size_t size, std::uint64_t& low, std::uint64_t& high) noexcept
		{
			if (source == nullptr || size == 0 || size > maximum_constant_buffer_bytes)
				return false;
			__try
			{
				const auto* bytes = static_cast<const std::uint8_t*>(source);
				low = 1469598103934665603ull;
				high = 1099511628211ull ^ static_cast<std::uint64_t>(size);
				for (std::size_t index{}; index < size; ++index)
				{
					low ^= bytes[index];
					low *= 1099511628211ull;
					high ^= static_cast<std::uint64_t>(bytes[index]) +
						0x9E3779B97F4A7C15ull + (high << 6) + (high >> 2);
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				low = 0;
				high = 0;
				return false;
			}
		}

		void publish_content(buffer_entry& entry, const bool known,
			const std::uint64_t hash_low, const std::uint64_t hash_high,
			const upload_source source, const std::uintptr_t caller,
			const std::uint64_t content_history_epoch,
			const void* const source_bytes = nullptr,
			const std::size_t source_size = 0) noexcept
		{
			std::array<std::uint8_t, maximum_content_byte_snapshot> captured{};
			std::size_t captured_size{};
			bool captured_complete{};
			if (known && source_bytes != nullptr && source_size != 0)
			{
				captured_size = (std::min)(source_size,
					maximum_content_byte_snapshot);
				__try
				{
					std::memcpy(captured.data(), source_bytes, captured_size);
					captured_complete = captured_size == source_size;
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					captured_size = 0;
					captured_complete = false;
				}
			}
			std::uint64_t sequence{};
			if (!acquire_write(entry, sequence)) return;
			entry.upload_generation.store(next_upload_generation.fetch_add(1,
				std::memory_order_relaxed) + 1, std::memory_order_relaxed);
			entry.history_epoch.store(content_history_epoch, std::memory_order_relaxed);
			entry.hash_low.store(known ? hash_low : 0, std::memory_order_relaxed);
			entry.hash_high.store(known ? hash_high : 0, std::memory_order_relaxed);
			entry.upload_caller.store(caller, std::memory_order_relaxed);
			entry.upload_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
			entry.source.store(source, std::memory_order_relaxed);
			entry.known.store(known, std::memory_order_relaxed);
			entry.captured_bytes.store(static_cast<std::uint32_t>(captured_size),
				std::memory_order_relaxed);
			entry.captured_bytes_complete.store(captured_complete,
				std::memory_order_relaxed);
			for (std::size_t index{}; index < captured_size; ++index)
			{
				entry.content_bytes[index].store(captured[index],
					std::memory_order_relaxed);
			}
			release_write(entry, sequence);
		}

		[[nodiscard]] content_snapshot read_content(
			const std::uintptr_t identity,
			const std::uint64_t device_generation) noexcept
		{
			content_snapshot output{};
			output.buffer = identity;
			auto* const entry = find_buffer_entry(identity);
			if (entry == nullptr) return output;
			for (std::size_t attempt{}; attempt < 4; ++attempt)
			{
				const auto before = entry->sequence.load(std::memory_order_acquire);
				if (before & 1u) continue;
				output.device_generation = entry->device_generation.load(
					std::memory_order_relaxed);
				output.creation_serial = entry->creation_serial.load(
					std::memory_order_relaxed);
				output.upload_generation = entry->upload_generation.load(
					std::memory_order_relaxed);
				const auto content_history_epoch = entry->history_epoch.load(
					std::memory_order_relaxed);
				output.hash_low = entry->hash_low.load(std::memory_order_relaxed);
				output.hash_high = entry->hash_high.load(std::memory_order_relaxed);
				output.upload_caller = entry->upload_caller.load(
					std::memory_order_relaxed);
				output.byte_width = entry->byte_width.load(std::memory_order_relaxed);
				output.upload_thread = entry->upload_thread.load(
					std::memory_order_relaxed);
				output.source = entry->source.load(std::memory_order_relaxed);
				output.known = entry->known.load(std::memory_order_relaxed);
				const auto after = entry->sequence.load(std::memory_order_acquire);
				if (before == after && !(after & 1u))
				{
					if (output.device_generation != device_generation ||
						content_history_epoch != history_tracking_epoch.load(
							std::memory_order_acquire))
					{
						output.known = false;
						output.source = upload_source::unknown;
					}
					return output;
				}
			}
			unstable_content_reads.fetch_add(1, std::memory_order_relaxed);
			output.known = false;
			output.source = upload_source::unknown;
			return output;
		}

		[[nodiscard]] bool read_content_bytes(buffer_entry& entry,
			const std::uint64_t device_generation,
			const std::uint64_t content_history_epoch,
			const std::uint64_t upload_generation,
			content_byte_snapshot& output) noexcept
		{
			output = {};
			if (device_generation == 0 || content_history_epoch == 0 ||
				upload_generation == 0) return false;
			for (std::size_t attempt{}; attempt < 4; ++attempt)
			{
				const auto before = entry.sequence.load(std::memory_order_acquire);
				if (before & 1u) continue;
				const auto observed_generation = entry.device_generation.load(
					std::memory_order_relaxed);
				const auto observed_epoch = entry.history_epoch.load(
					std::memory_order_relaxed);
				output.upload_generation = entry.upload_generation.load(
					std::memory_order_relaxed);
				output.byte_width = entry.byte_width.load(std::memory_order_relaxed);
				output.captured_bytes = entry.captured_bytes.load(
					std::memory_order_relaxed);
				output.complete = entry.captured_bytes_complete.load(
					std::memory_order_relaxed);
				if (output.captured_bytes > output.bytes.size()) return false;
				for (std::size_t index{}; index < output.captured_bytes; ++index)
				{
					output.bytes[index] = entry.content_bytes[index].load(
						std::memory_order_relaxed);
				}
				const auto known = entry.known.load(std::memory_order_relaxed);
				const auto after = entry.sequence.load(std::memory_order_acquire);
				if (before != after || (after & 1u)) continue;
				if (observed_generation != device_generation ||
					observed_epoch != content_history_epoch ||
					output.upload_generation != upload_generation || !known ||
					output.captured_bytes == 0)
				{
					output = {};
					return false;
				}
				return true;
			}
			unstable_content_reads.fetch_add(1, std::memory_order_relaxed);
			output = {};
			return false;
		}

		[[nodiscard]] shader_entry* find_or_claim_shader(
			const std::uintptr_t identity, const std::uint64_t device_generation,
			const std::uint64_t creation_serial, bool& claimed) noexcept
		{
			claimed = false;
			if (identity == 0 || device_generation == 0 || creation_serial == 0 ||
				(identity & 1u) != 0)
				return nullptr;
			auto index = (identity >> 4) & (maximum_tracked_vertex_shaders - 1);
			for (std::size_t probe{}; probe < maximum_tracked_vertex_shaders; ++probe)
			{
				auto& entry = shaders[index];
				auto existing = entry.identity.load(std::memory_order_acquire);
				if (existing == identity)
				{
					if (entry.creation_serial.load(std::memory_order_acquire) ==
							creation_serial &&
						entry.device_generation.load(std::memory_order_acquire) ==
							device_generation)
					{
						return &entry;
					}
					const std::lock_guard lock(shader_identity_mutex);
					if (entry.identity.load(std::memory_order_acquire) != identity)
						continue;
					if (entry.creation_serial.load(std::memory_order_acquire) !=
							creation_serial ||
						entry.device_generation.load(std::memory_order_acquire) !=
							device_generation)
					{
						entry.usage.store(0, std::memory_order_relaxed);
						entry.private_data_attempts.store(0, std::memory_order_relaxed);
						entry.device_generation.store(device_generation,
							std::memory_order_relaxed);
						entry.creation_serial.store(creation_serial,
							std::memory_order_release);
						shader_identity_reuses.fetch_add(1, std::memory_order_relaxed);
						claimed = true;
					}
					return &entry;
				}
				if (existing == 0 && entry.identity.compare_exchange_strong(existing,
					identity | 1u, std::memory_order_acq_rel, std::memory_order_acquire))
				{
					entry.usage.store(0, std::memory_order_relaxed);
					entry.private_data_attempts.store(0, std::memory_order_relaxed);
					entry.device_generation.store(device_generation,
						std::memory_order_relaxed);
					entry.creation_serial.store(creation_serial, std::memory_order_release);
					entry.identity.store(identity, std::memory_order_release);
					claimed = true;
					return &entry;
				}
				if (existing == identity) return &entry;
				if (existing == (identity | 1u))
				{
					for (std::size_t attempt{}; attempt < 8; ++attempt)
					{
						if (entry.identity.load(std::memory_order_acquire) == identity &&
							entry.creation_serial.load(std::memory_order_acquire) ==
								creation_serial &&
							entry.device_generation.load(std::memory_order_acquire) ==
								device_generation)
						{
							return &entry;
						}
					}
					return nullptr;
				}
				index = (index + 1) & (maximum_tracked_vertex_shaders - 1);
			}
			tracked_shader_overflows.fetch_add(1, std::memory_order_relaxed);
			return nullptr;
		}

		[[nodiscard]] shader_usage resolve_shader_usage(
			ID3D11VertexShader* const shader,
			const std::uint64_t device_generation) noexcept
		{
			if (shader == nullptr || device_generation == 0)
				return shader_usage::unknown;
			std::uint64_t creation_serial{};
			if (!get_or_assign_creation_serial(shader, shader_creation_serial_guid,
				next_shader_creation_serial, shader_identity_mutex,
				shader_identity_tag_failures, creation_serial))
			{
				return shader_usage::unknown;
			}
			bool claimed{};
			auto* const entry = find_or_claim_shader(
				reinterpret_cast<std::uintptr_t>(shader), device_generation,
				creation_serial, claimed);
			if (entry == nullptr) return shader_usage::unknown;
			if (!claimed)
			{
				const auto cached = entry->usage.load(std::memory_order_acquire);
				if (cached == 1) return shader_usage::unused;
				if (cached == 2) return shader_usage::used;
				if (cached == 3) return shader_usage::unknown;
			}

			UINT bytecode_size = static_cast<UINT>(shader_bytecode_scratch.size());
			auto result = shader->GetPrivateData(d3d11::guid_shader_bytecode,
				&bytecode_size, shader_bytecode_scratch.data());
			if (result == DXGI_ERROR_MORE_DATA ||
				bytecode_size > shader_bytecode_scratch.size())
			{
				shader_private_data_oversized.fetch_add(1, std::memory_order_relaxed);
				entry->usage.store(3, std::memory_order_release);
				return shader_usage::unknown;
			}
			if (FAILED(result) || bytecode_size == 0)
			{
				shader_private_data_missing.fetch_add(1, std::memory_order_relaxed);
				const auto attempts = entry->private_data_attempts.fetch_add(1,
					std::memory_order_relaxed) + 1;
				entry->usage.store(attempts >= maximum_private_data_attempts ? 3 : 0,
					std::memory_order_release);
				return shader_usage::unknown;
			}
			entry->private_data_attempts.store(0, std::memory_order_relaxed);

			ID3D11ShaderReflection* reflection{};
			result = D3DReflect(shader_bytecode_scratch.data(), bytecode_size,
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(&reflection));
			if (FAILED(result) || reflection == nullptr)
			{
				shader_reflection_failures.fetch_add(1, std::memory_order_relaxed);
				entry->usage.store(3, std::memory_order_release);
				return shader_usage::unknown;
			}
			D3D11_SHADER_DESC description{};
			result = reflection->GetDesc(&description);
			bool used{};
			if (SUCCEEDED(result) && (D3D11_SHVER_GET_TYPE(description.Version) !=
				D3D11_SHVER_VERTEX_SHADER))
			{
				result = E_INVALIDARG;
			}
			if (SUCCEEDED(result) && description.BoundResources >
				maximum_reflected_bindings)
			{
				result = E_BOUNDS;
			}
			if (SUCCEEDED(result))
			{
				for (UINT index{}; index < description.BoundResources; ++index)
				{
					D3D11_SHADER_INPUT_BIND_DESC binding{};
					if (FAILED(reflection->GetResourceBindingDesc(index, &binding)))
					{
						result = E_FAIL;
						break;
					}
					if (binding.Type == D3D_SIT_CBUFFER &&
						binding.BindPoint <= observed_slot &&
						binding.BindCount > observed_slot - binding.BindPoint)
					{
						used = true;
						break;
					}
				}
			}
			reflection->Release();
			if (FAILED(result))
			{
				shader_reflection_failures.fetch_add(1, std::memory_order_relaxed);
				entry->usage.store(3, std::memory_order_release);
				return shader_usage::unknown;
			}
			entry->usage.store(used ? 2 : 1, std::memory_order_release);
			return used ? shader_usage::used : shader_usage::unused;
		}

		struct slot_metadata
		{
			std::uint64_t sequence{};
			std::uintptr_t buffer{};
			std::uintptr_t caller{};
			std::uint32_t thread{};
			bind_origin origin{bind_origin::unknown};
		};

		void publish_slot_metadata(const std::uintptr_t buffer,
			const bind_origin origin, const std::uintptr_t caller) noexcept
		{
			for (std::size_t attempt{}; attempt < 8; ++attempt)
			{
				auto before = current_slot.sequence_lock.load(std::memory_order_acquire);
				if (before & 1u) continue;
				if (!current_slot.sequence_lock.compare_exchange_weak(before, before + 1,
					std::memory_order_acq_rel, std::memory_order_acquire)) continue;
				current_slot.set_sequence.store(next_set_sequence.fetch_add(1,
					std::memory_order_relaxed) + 1, std::memory_order_relaxed);
				current_slot.buffer.store(buffer, std::memory_order_relaxed);
				current_slot.caller.store(caller, std::memory_order_relaxed);
				current_slot.thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
				current_slot.origin.store(origin, std::memory_order_relaxed);
				current_slot.sequence_lock.store(before + 2, std::memory_order_release);
				return;
			}
			concurrent_update_drops.fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] slot_metadata read_slot_metadata() noexcept
		{
			slot_metadata output{};
			for (std::size_t attempt{}; attempt < 4; ++attempt)
			{
				const auto before = current_slot.sequence_lock.load(
					std::memory_order_acquire);
				if (before & 1u) continue;
				output.sequence = current_slot.set_sequence.load(std::memory_order_relaxed);
				output.buffer = current_slot.buffer.load(std::memory_order_relaxed);
				output.caller = current_slot.caller.load(std::memory_order_relaxed);
				output.thread = current_slot.thread.load(std::memory_order_relaxed);
				output.origin = current_slot.origin.load(std::memory_order_relaxed);
				const auto after = current_slot.sequence_lock.load(
					std::memory_order_acquire);
				if (before == after && !(after & 1u)) return output;
			}
			return {};
		}

		[[nodiscard]] slot_snapshot make_slot_snapshot(
			ID3D11VertexShader* const shader, ID3D11Buffer* const buffer,
			const bool boundary, const std::uint64_t device_generation) noexcept
		{
			slot_snapshot output{};
			output.shader = reinterpret_cast<std::uintptr_t>(shader);
			output.shader_device_generation = device_generation;
			output.usage = resolve_shader_usage(shader, device_generation);
			const auto identity = reinterpret_cast<std::uintptr_t>(buffer);
			if (buffer != nullptr)
			{
				if (find_or_register_buffer(buffer, device_generation) != nullptr)
					output.content = read_content(identity, device_generation);
				else
				{
					output.content.buffer = identity;
					output.content.device_generation = device_generation;
				}
			}
			const auto metadata = read_slot_metadata();
			if (metadata.buffer == identity && metadata.origin != bind_origin::unknown)
			{
				output.last_set_sequence = metadata.sequence;
				output.last_set_caller = metadata.caller;
				output.last_set_thread = metadata.thread;
				output.origin = metadata.origin;
			}
			else
			{
				output.origin = boundary ? bind_origin::eye_boundary : bind_origin::unknown;
			}
			return output;
		}

		[[nodiscard]] slot_snapshot capture_boundary(
			ID3D11DeviceContext* const context) noexcept
		{
			ID3D11Buffer* buffer{};
			context->VSGetConstantBuffers(observed_slot, 1, &buffer);
			const auto output = make_slot_snapshot(nullptr, buffer, true,
				expected_generation.load(std::memory_order_acquire));
			if (buffer != nullptr) buffer->Release();
			return output;
		}

		void record_active_set_event(const std::uintptr_t buffer,
			const bind_origin origin, const std::uintptr_t caller) noexcept
		{
			if (current_state.load(std::memory_order_acquire) != state::eye_active)
				return;
			const auto eye = active_eye.load(std::memory_order_acquire);
			const auto owner = active_owner_thread.load(std::memory_order_acquire);
			if (eye >= 2 || owner == 0 || owner != GetCurrentThreadId())
			{
				foreign_thread_pair_calls.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			auto& output = working_report.eyes[eye];
			++output.setter_touches;
			switch (origin)
			{
			case bind_origin::explicit_bind: ++output.explicit_binds; break;
			case bind_origin::explicit_null: ++output.explicit_nulls; break;
			case bind_origin::clear_state: ++output.clear_states; break;
			case bind_origin::opaque_state_change: ++output.opaque_state_changes; break;
			default: break;
			}
			if (output.set_event_count < output.set_events.size())
			{
				const auto metadata = read_slot_metadata();
				output.set_events[output.set_event_count++] = {metadata.sequence, caller,
					buffer, GetCurrentThreadId(), origin};
			}
			else ++output.set_event_overflows;
		}

		void observe_set_constant_buffers(const shader_stage stage,
			ID3D11DeviceContext* const context, const UINT start_slot,
			const UINT count, ID3D11Buffer* const* const values,
			const std::uintptr_t caller) noexcept
		{
			if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
			const auto candidate = find_candidate(context);
			if (candidate.generation == 0)
			{
				unregistered_context_calls.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			set_calls[stage_index(stage)].fetch_add(1, std::memory_order_relaxed);
			if (stage != shader_stage::vs || start_slot > observed_slot ||
				count <= observed_slot - start_slot) return;
			const auto index = observed_slot - start_slot;
			auto* const buffer = values == nullptr ? nullptr : values[index];
			if (buffer != nullptr)
				(void)find_or_register_buffer(buffer, candidate.generation);
			if (expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context)) return;
			const active_report_callback callback(context);
			if (!callback) return;
			const auto identity = reinterpret_cast<std::uintptr_t>(buffer);
			const auto origin = buffer != nullptr ? bind_origin::explicit_bind :
				bind_origin::explicit_null;
			publish_slot_metadata(identity, origin, caller);
			record_active_set_event(identity, origin, caller);
		}

		void __stdcall vs_set_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11Buffer* const* const values)
		{
			const auto original = reinterpret_cast<set_constant_buffers_fn>(
				vs_set_hook.get_original());
			if (original == nullptr) return;
			original(context, start, count, values);
			if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			observe_set_constant_buffers(shader_stage::vs, context, start, count,
				values, caller);
		}

		void __stdcall ps_set_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11Buffer* const* const values)
		{
			const auto original = reinterpret_cast<set_constant_buffers_fn>(
				ps_set_hook.get_original());
			if (original == nullptr) return;
			original(context, start, count, values);
			if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			observe_set_constant_buffers(shader_stage::ps, context, start, count,
				values, caller);
		}

		void __stdcall gs_set_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11Buffer* const* const values)
		{
			const auto original = reinterpret_cast<set_constant_buffers_fn>(
				gs_set_hook.get_original());
			if (original == nullptr) return;
			original(context, start, count, values);
			if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			observe_set_constant_buffers(shader_stage::gs, context, start, count,
				values, caller);
		}

		void __stdcall hs_set_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11Buffer* const* const values)
		{
			const auto original = reinterpret_cast<set_constant_buffers_fn>(
				hs_set_hook.get_original());
			if (original == nullptr) return;
			original(context, start, count, values);
			if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			observe_set_constant_buffers(shader_stage::hs, context, start, count,
				values, caller);
		}

		void __stdcall ds_set_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11Buffer* const* const values)
		{
			const auto original = reinterpret_cast<set_constant_buffers_fn>(
				ds_set_hook.get_original());
			if (original == nullptr) return;
			original(context, start, count, values);
			if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			observe_set_constant_buffers(shader_stage::ds, context, start, count,
				values, caller);
		}

		void __stdcall cs_set_stub(ID3D11DeviceContext* const context,
			const UINT start, const UINT count, ID3D11Buffer* const* const values)
		{
			const auto original = reinterpret_cast<set_constant_buffers_fn>(
				cs_set_hook.get_original());
			if (original == nullptr) return;
			original(context, start, count, values);
			if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			observe_set_constant_buffers(shader_stage::cs, context, start, count,
				values, caller);
		}

		[[nodiscard]] bool map_writes(const D3D11_MAP value) noexcept
		{
			return value == D3D11_MAP_WRITE || value == D3D11_MAP_READ_WRITE ||
				value == D3D11_MAP_WRITE_DISCARD ||
				value == D3D11_MAP_WRITE_NO_OVERWRITE;
		}

		HRESULT __stdcall map_stub(ID3D11DeviceContext* const context,
			ID3D11Resource* const resource, const UINT subresource,
			const D3D11_MAP type, const UINT flags,
			D3D11_MAPPED_SUBRESOURCE* const mapped)
		{
			const auto original = reinterpret_cast<map_fn>(map_hook.get_original());
			if (original == nullptr) return E_FAIL;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto result = original(context, resource, subresource, type, flags, mapped);
			if (!history_tracking_enabled.load(std::memory_order_acquire)) return result;
			const auto tracking_epoch = history_tracking_epoch.load(
				std::memory_order_acquire);
			const auto candidate = find_candidate(context);
			buffer_entry* tracked{};
			if (candidate.generation != 0 && map_writes(type))
				tracked = find_or_register_resource(resource, candidate.generation);
			if (candidate.generation == 0)
			{
				unregistered_context_calls.fetch_add(1, std::memory_order_relaxed);
				return result;
			}
			map_calls.fetch_add(1, std::memory_order_relaxed);
			if (FAILED(result) || mapped == nullptr || mapped->pData == nullptr ||
				!map_writes(type)) return result;
			if (tracked == nullptr) return result;
			const auto content = read_content(reinterpret_cast<std::uintptr_t>(resource),
				candidate.generation);
			for (auto& pending : pending_maps)
			{
				if (!pending.active || pending.context != context ||
					pending.resource != resource ||
					pending.device_generation != candidate.generation)
				{
					continue;
				}
				pending = {};
				if (pending_map_count != 0) --pending_map_count;
				unmatched_unmaps.fetch_add(1, std::memory_order_relaxed);
			}
			for (auto& pending : pending_maps)
			{
				if (pending.active) continue;
				pending.context = context;
				pending.resource = resource;
				pending.device_generation = candidate.generation;
				pending.data = mapped->pData;
				pending.caller = caller;
				pending.subresource = subresource;
				pending.byte_width = content.byte_width;
				pending.history_epoch = tracking_epoch;
				pending.type = type;
				pending.active = true;
				++pending_map_count;
				return result;
			}
			pending_map_overflows.fetch_add(1, std::memory_order_relaxed);
			return result;
		}

		void __stdcall unmap_stub(ID3D11DeviceContext* const context,
			ID3D11Resource* const resource, const UINT subresource)
		{
			const auto original = reinterpret_cast<unmap_fn>(unmap_hook.get_original());
			if (original == nullptr) return;
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const auto tracking = history_tracking_enabled.load(std::memory_order_acquire);
			// A gate transition may occur between a tracked Map and its matching Unmap.
			// Keep the disabled hot path O(1) unless this thread actually owns a pending
			// pointer, then retire that pointer without hashing or publishing history.
			if (!tracking && pending_map_count == 0)
			{
				original(context, resource, subresource);
				return;
			}
			pending_map* found{};
			for (auto& pending : pending_maps)
			{
				if (pending.active && pending.context == context &&
					pending.resource == resource && pending.subresource == subresource)
				{
					found = &pending;
					break;
				}
			}
			if (!tracking)
			{
				if (found != nullptr)
				{
					*found = {};
					if (pending_map_count != 0) --pending_map_count;
				}
				original(context, resource, subresource);
				return;
			}
			const auto candidate = find_candidate(context);
			if (candidate.generation != 0)
			{
				unmap_calls.fetch_add(1, std::memory_order_relaxed);
				const auto current_epoch = history_tracking_epoch.load(
					std::memory_order_acquire);
				if (found != nullptr &&
					found->device_generation == candidate.generation &&
					found->history_epoch == current_epoch)
				{
					std::uint64_t low{}, high{};
					const auto known = hash_bytes_guarded(found->data,
						found->byte_width, low, high);
					if (auto* const entry = find_or_register_resource(resource,
						candidate.generation))
					{
						publish_content(*entry, known, low, high,
							upload_source::map_unmap, found->caller,
							found->history_epoch, known ? found->data : nullptr,
							known ? found->byte_width : 0);
						mapped_uploads.fetch_add(1, std::memory_order_relaxed);
					}
					*found = {};
					if (pending_map_count != 0) --pending_map_count;
				}
				else
				{
					if (found != nullptr)
					{
						*found = {};
						if (pending_map_count != 0) --pending_map_count;
					}
					if (auto* const entry = find_or_register_resource(resource,
						candidate.generation))
					{
						unmatched_unmaps.fetch_add(1, std::memory_order_relaxed);
						publish_content(*entry, false, 0, 0,
							upload_source::unknown, caller, current_epoch);
					}
				}
			}
			else unregistered_context_calls.fetch_add(1, std::memory_order_relaxed);
			original(context, resource, subresource);
		}

		void fill_runtime_status(report& output) noexcept
		{
			output.hooks_installed = hooks_installed.load(std::memory_order_acquire);
			output.history_tracking_enabled = history_tracking_enabled.load(
				std::memory_order_acquire);
			output.history_tracking_clients = history_tracking_clients.load(
				std::memory_order_acquire);
			output.expected_device = expected_device.load(std::memory_order_acquire);
			output.expected_context = expected_context.load(std::memory_order_acquire);
			output.device_generation = expected_generation.load(std::memory_order_acquire);
			output.registered_device_candidates = registered_candidate_count();
			output.candidate_registration_overflows =
				candidate_registration_overflows.load(std::memory_order_acquire);
			output.unregistered_context_calls = unregistered_context_calls.load(
				std::memory_order_acquire);
			output.selected_resource_history_complete =
				selected_resource_history_complete.load(std::memory_order_acquire);
			for (std::size_t index{}; index < shader_stage_count; ++index)
			{
				output.set_hook_targets[index] = set_hook_targets[index].load(
					std::memory_order_acquire);
				output.set_calls[index] = set_calls[index].load(std::memory_order_acquire);
			}
			output.map_hook_target = map_hook_target.load(std::memory_order_acquire);
			output.unmap_hook_target = unmap_hook_target.load(std::memory_order_acquire);
			output.hook_failures = hook_failures.load(std::memory_order_acquire);
			output.callback_quiescence_timeouts = callback_quiescence_timeouts.load(
				std::memory_order_acquire);
			output.foreign_context_calls = foreign_context_calls.load(
				std::memory_order_acquire);
			output.foreign_thread_pair_calls = foreign_thread_pair_calls.load(
				std::memory_order_acquire);
			output.map_calls = map_calls.load(std::memory_order_acquire);
			output.unmap_calls = unmap_calls.load(std::memory_order_acquire);
			output.mapped_uploads = mapped_uploads.load(std::memory_order_acquire);
			output.unmatched_unmaps = unmatched_unmaps.load(std::memory_order_acquire);
			output.pending_map_overflows = pending_map_overflows.load(
				std::memory_order_acquire);
			output.tracked_buffer_overflows = tracked_buffer_overflows.load(
				std::memory_order_acquire);
			output.tracked_shader_overflows = tracked_shader_overflows.load(
				std::memory_order_acquire);
			output.buffer_identity_tag_failures = buffer_identity_tag_failures.load(
				std::memory_order_acquire);
			output.shader_identity_tag_failures = shader_identity_tag_failures.load(
				std::memory_order_acquire);
			output.buffer_identity_reuses = buffer_identity_reuses.load(
				std::memory_order_acquire);
			output.shader_identity_reuses = shader_identity_reuses.load(
				std::memory_order_acquire);
			output.concurrent_update_drops = concurrent_update_drops.load(
				std::memory_order_acquire);
			output.unstable_content_reads = unstable_content_reads.load(
				std::memory_order_acquire);
			output.shader_private_data_missing = shader_private_data_missing.load(
				std::memory_order_acquire);
			output.shader_private_data_oversized = shader_private_data_oversized.load(
				std::memory_order_acquire);
			output.shader_reflection_failures = shader_reflection_failures.load(
				std::memory_order_acquire);
			output.resource_updates = resource_updates.load(std::memory_order_acquire);
			output.resource_copies = resource_copies.load(std::memory_order_acquire);
			output.resource_unknown_writes = resource_unknown_writes.load(
				std::memory_order_acquire);
			output.copy_source_candidates = copy_source_candidates.load(
				std::memory_order_acquire);
			output.copy_source_registered = copy_source_registered.load(
				std::memory_order_acquire);
			output.copy_source_known = copy_source_known.load(std::memory_order_acquire);
			output.copy_source_bytes_propagated = copy_source_bytes_propagated.load(
				std::memory_order_acquire);
			{
				const std::lock_guard lock(copy_source_sample_mutex);
				output.copy_source_samples = copy_source_samples;
				output.copy_source_sample_count = copy_source_sample_count;
				output.copy_source_sample_overflows = copy_source_sample_overflows;
			}
		}

		void reset_counters() noexcept
		{
			for (auto& value : set_calls) value.store(0, std::memory_order_relaxed);
			active_report_callbacks.store(0, std::memory_order_relaxed);
			callback_quiescence_timeouts.store(0, std::memory_order_relaxed);
			foreign_context_calls.store(0, std::memory_order_relaxed);
			foreign_thread_pair_calls.store(0, std::memory_order_relaxed);
			map_calls.store(0, std::memory_order_relaxed);
			unmap_calls.store(0, std::memory_order_relaxed);
			mapped_uploads.store(0, std::memory_order_relaxed);
			unmatched_unmaps.store(0, std::memory_order_relaxed);
			pending_map_overflows.store(0, std::memory_order_relaxed);
			tracked_buffer_overflows.store(0, std::memory_order_relaxed);
			tracked_shader_overflows.store(0, std::memory_order_relaxed);
			buffer_identity_tag_failures.store(0, std::memory_order_relaxed);
			shader_identity_tag_failures.store(0, std::memory_order_relaxed);
			buffer_identity_reuses.store(0, std::memory_order_relaxed);
			shader_identity_reuses.store(0, std::memory_order_relaxed);
			candidate_registration_overflows.store(0, std::memory_order_relaxed);
			unregistered_context_calls.store(0, std::memory_order_relaxed);
			concurrent_update_drops.store(0, std::memory_order_relaxed);
			unstable_content_reads.store(0, std::memory_order_relaxed);
			shader_private_data_missing.store(0, std::memory_order_relaxed);
			shader_private_data_oversized.store(0, std::memory_order_relaxed);
			shader_reflection_failures.store(0, std::memory_order_relaxed);
			resource_updates.store(0, std::memory_order_relaxed);
			resource_copies.store(0, std::memory_order_relaxed);
			resource_unknown_writes.store(0, std::memory_order_relaxed);
			copy_source_candidates.store(0, std::memory_order_relaxed);
			copy_source_registered.store(0, std::memory_order_relaxed);
			copy_source_known.store(0, std::memory_order_relaxed);
			copy_source_bytes_propagated.store(0, std::memory_order_relaxed);
			{
				const std::lock_guard lock(copy_source_sample_mutex);
				copy_source_samples = {};
				copy_source_sample_count = 0;
				copy_source_sample_overflows = 0;
			}
		}

		void fail_active_pair() noexcept
		{
			working_report.current_state = state::failed;
			fill_runtime_status(working_report);
			published_report = working_report;
			current_state.store(state::failed, std::memory_order_release);
			active_eye.store(invalid_eye, std::memory_order_release);
			active_owner_thread.store(0, std::memory_order_release);
			active_pair.store(0, std::memory_order_release);
		}
	}

	bool install_early(ID3D11Device* const device,
		ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		if (device == nullptr || context == nullptr || device_generation == 0)
		{
			hook_failures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (installation_poisoned.load(std::memory_order_acquire)) return false;
		bool hook_mutation_started{};
		try
		{
			Microsoft::WRL::ComPtr<ID3D11Device> owner;
			context->GetDevice(owner.GetAddressOf());
			if (owner.Get() != device)
				throw std::runtime_error("constant-buffer probe device mismatch");
			auto* const vtable = *reinterpret_cast<void***>(context);
			if (vtable == nullptr)
				throw std::runtime_error("constant-buffer probe context vtable is null");
			std::array<void*, shader_stage_count> set_targets{};
			for (std::size_t index{}; index < set_targets.size(); ++index)
				set_targets[index] = vtable[set_vtable_slots[index]];
			void* const map_target = vtable[map_vtable_slot];
			void* const unmap_target = vtable[unmap_vtable_slot];
			for (const auto target : set_targets)
				if (!utils::hook_validation::validate_executable_target(target))
					throw std::runtime_error("constant-buffer set target is invalid");
			if (!utils::hook_validation::validate_executable_target(map_target) ||
				!utils::hook_validation::validate_executable_target(unmap_target))
			{
				throw std::runtime_error("constant-buffer map target is invalid");
			}
			for (std::size_t index{}; index < set_targets.size(); ++index)
			{
				for (std::size_t previous{}; previous < index; ++previous)
					if (set_targets[index] == set_targets[previous])
						throw std::runtime_error("constant-buffer set targets alias");
				if (set_targets[index] == map_target || set_targets[index] == unmap_target)
					throw std::runtime_error("constant-buffer context targets alias");
			}
			if (map_target == unmap_target)
				throw std::runtime_error("constant-buffer map/unmap targets alias");

			const std::lock_guard lock(hook_mutex);
			if (installation_poisoned.load(std::memory_order_acquire)) return false;
			const auto first_install = !hooks_installed.load(std::memory_order_acquire);
			if (first_install)
			{
				reset_buffer_table();
				reset_counters();
				const std::lock_guard lifecycle_lock(lifecycle_mutex);
				clear_trivial(working_report);
				clear_trivial(published_report);
				current_state.store(state::idle, std::memory_order_release);
				active_pair.store(0, std::memory_order_release);
				active_eye.store(invalid_eye, std::memory_order_release);
				active_owner_thread.store(0, std::memory_order_release);
				expected_device.store(0, std::memory_order_release);
				expected_context.store(0, std::memory_order_release);
				expected_generation.store(0, std::memory_order_release);
				selected_resource_history_complete.store(false,
					std::memory_order_release);
			}
			const auto hooks = set_hooks();
			const std::array<void*, shader_stage_count> stubs{
				reinterpret_cast<void*>(vs_set_stub),
				reinterpret_cast<void*>(ps_set_stub),
				reinterpret_cast<void*>(gs_set_stub),
				reinterpret_cast<void*>(hs_set_stub),
				reinterpret_cast<void*>(ds_set_stub),
				reinterpret_cast<void*>(cs_set_stub),
			};
			for (std::size_t index{}; index < hooks.size(); ++index)
			{
				const auto existing = set_hook_targets[index].load(
					std::memory_order_acquire);
				if (existing != 0 && existing != reinterpret_cast<std::uintptr_t>(
					set_targets[index]))
				{
					throw std::runtime_error("constant-buffer set target changed");
				}
				if (!hooks[index]->is_enabled())
				{
					hook_mutation_started = true;
					if (hooks[index]->get_original() == nullptr)
						hooks[index]->create(set_targets[index], stubs[index]);
					else hooks[index]->enable();
				}
				set_hook_targets[index].store(reinterpret_cast<std::uintptr_t>(
					set_targets[index]), std::memory_order_release);
			}
			if (!map_hook.is_enabled())
			{
				hook_mutation_started = true;
				if (map_hook.get_original() == nullptr)
					map_hook.create(map_target, reinterpret_cast<void*>(map_stub));
				else map_hook.enable();
			}
			if (!unmap_hook.is_enabled())
			{
				hook_mutation_started = true;
				if (unmap_hook.get_original() == nullptr)
					unmap_hook.create(unmap_target, reinterpret_cast<void*>(unmap_stub));
				else unmap_hook.enable();
			}
			for (const auto* const hook : hooks)
				if (!hook->is_enabled())
					throw std::runtime_error("constant-buffer set hook enable failed");
			if (!map_hook.is_enabled() || !unmap_hook.is_enabled())
				throw std::runtime_error("constant-buffer map hook enable failed");

			map_hook_target.store(reinterpret_cast<std::uintptr_t>(map_target),
				std::memory_order_release);
			unmap_hook_target.store(reinterpret_cast<std::uintptr_t>(unmap_target),
				std::memory_order_release);
			const auto selected_context = expected_context.load(std::memory_order_acquire);
			if (selected_context != 0 && selected_context !=
				reinterpret_cast<std::uintptr_t>(context))
			{
				const auto selected_generation = expected_generation.load(
					std::memory_order_acquire);
				if (device_generation <= selected_generation)
				{
					// Older/equal foreign candidates can never replace the explicitly
					// selected generation. Their ABI was still validated above.
					hooks_installed.store(true, std::memory_order_release);
					return true;
				}
			}
			if (selected_context == reinterpret_cast<std::uintptr_t>(context) &&
				(expected_device.load(std::memory_order_acquire) !=
					reinterpret_cast<std::uintptr_t>(device) ||
				expected_generation.load(std::memory_order_acquire) != device_generation))
			{
				throw std::runtime_error("selected constant-buffer device identity changed");
			}
			if (!register_candidate(device, context, device_generation))
				throw std::runtime_error("constant-buffer candidate registration failed");
			hooks_installed.store(true, std::memory_order_release);
			return true;
		}
		catch (...)
		{
			if (hook_mutation_started)
				installation_poisoned.store(true, std::memory_order_release);
			hook_failures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
	}

	bool select_device(ID3D11Device* const device,
		ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		if (device == nullptr || context == nullptr || device_generation == 0 ||
			!hooks_installed.load(std::memory_order_acquire))
		{
			return false;
		}
		std::unique_lock hook_lock(hook_mutex);
		device_candidate* selected{};
		for (auto& candidate : device_candidates)
		{
			if (candidate.context.load(std::memory_order_acquire) ==
					reinterpret_cast<std::uintptr_t>(context) &&
				candidate.device.load(std::memory_order_acquire) ==
					reinterpret_cast<std::uintptr_t>(device) &&
				candidate.generation.load(std::memory_order_acquire) == device_generation)
			{
				selected = &candidate;
				break;
			}
		}
		if (selected == nullptr) return false;
		std::unique_lock lifecycle_lock(lifecycle_mutex);
		const auto prior_context = expected_context.load(std::memory_order_acquire);
		const auto prior_generation = expected_generation.load(
			std::memory_order_acquire);
		const auto same_selection = prior_context ==
				reinterpret_cast<std::uintptr_t>(context) &&
			expected_device.load(std::memory_order_acquire) ==
				reinterpret_cast<std::uintptr_t>(device) &&
			prior_generation == device_generation;
		const auto gated_same_selection = prior_context == 0 &&
			expected_device.load(std::memory_order_acquire) ==
				reinterpret_cast<std::uintptr_t>(device) &&
			prior_generation == device_generation;
		if (!same_selection && !gated_same_selection && prior_generation != 0 &&
			device_generation <= prior_generation)
		{
			return false;
		}
		if (!same_selection)
		{
			if (prior_context != 0)
				expected_context.store(0, std::memory_order_release);
			if (!wait_for_report_callbacks())
			{
				if (prior_context != 0)
					expected_context.store(prior_context, std::memory_order_release);
				return false;
			}
		}

		std::array<candidate_references, maximum_device_candidates> releases{};
		std::size_t release_count{};
		if (!same_selection &&
			(current_state.load(std::memory_order_acquire) == state::pair_active ||
				current_state.load(std::memory_order_acquire) == state::eye_active))
		{
			fail_active_pair();
		}
		expected_device.store(reinterpret_cast<std::uintptr_t>(device),
			std::memory_order_release);
		expected_generation.store(device_generation, std::memory_order_release);
		expected_context.store(reinterpret_cast<std::uintptr_t>(context),
			std::memory_order_release);
		selected_resource_history_complete.store(
			selected->resource_history_complete.load(std::memory_order_acquire),
			std::memory_order_release);
		publish_slot_metadata(0, bind_origin::unknown, 0);
		for (auto& candidate : device_candidates)
			if (&candidate != selected &&
				candidate.context.load(std::memory_order_acquire) != 0 &&
				candidate.generation.load(std::memory_order_acquire) < device_generation)
			{
				releases[release_count++] = detach_candidate(candidate);
			}
		lifecycle_lock.unlock();
		hook_lock.unlock();
		for (std::size_t index{}; index < release_count; ++index)
			release_candidate_references(releases[index]);
		return true;
	}

	void set_history_tracking_client(const history_tracking_client client,
		const bool enabled) noexcept
	{
		const auto index = static_cast<std::uint32_t>(client);
		if (index >= static_cast<std::uint32_t>(history_tracking_client::count)) return;
		const auto bit = 1u << index;
		const std::lock_guard lock(history_tracking_mutex);
		const auto before = history_tracking_clients.load(std::memory_order_acquire);
		const auto after = enabled ? before | bit : before & ~bit;
		if (before == after) return;
		history_tracking_clients.store(after, std::memory_order_release);
		const auto was_enabled = before != 0;
		const auto now_enabled = after != 0;
		if (was_enabled == now_enabled) return;
		// Every closed/open transition invalidates pending Map provenance from the
		// previous interval. Adding or removing one lease while another remains
		// active deliberately preserves the shared history epoch.
		history_tracking_epoch.fetch_add(1, std::memory_order_acq_rel);
		if (now_enabled)
		{
			// Writes are deliberately ignored while the gate is closed. The new
			// epoch makes every prior content hash unknown without scanning the table.
			publish_slot_metadata(0, bind_origin::unknown, 0);
		}
		history_tracking_enabled.store(now_enabled, std::memory_order_release);
	}

	void set_history_tracking_enabled(const bool enabled) noexcept
	{
		set_history_tracking_client(history_tracking_client::gpu_census, enabled);
	}

	void set_resource_observer_attached(const bool attached) noexcept
	{
		const std::lock_guard lock(hook_mutex);
		resource_observer_attached.store(attached, std::memory_order_release);
		if (attached)
		{
			// Only an observer present before every candidate can prove complete
			// Update/Copy history. Promotion-time attachment is never retroactive
			// and does not claim coverage for later foreign candidates.
			resource_observer_covers_future_candidates.store(
				registered_candidate_count() == 0 &&
				expected_context.load(std::memory_order_acquire) == 0,
				std::memory_order_release);
			return;
		}
		resource_observer_covers_future_candidates.store(false,
			std::memory_order_release);
		for (auto& candidate : device_candidates)
			candidate.resource_history_complete.store(false,
				std::memory_order_release);
		selected_resource_history_complete.store(false, std::memory_order_release);
	}

	void invalidate_device(ID3D11Device* const device,
		ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		std::unique_lock hook_lock(hook_mutex);
		device_candidate* found{};
		for (auto& candidate : device_candidates)
		{
			if (candidate.context.load(std::memory_order_acquire) ==
					reinterpret_cast<std::uintptr_t>(context) &&
				candidate.device.load(std::memory_order_acquire) ==
					reinterpret_cast<std::uintptr_t>(device) &&
				candidate.generation.load(std::memory_order_acquire) == device_generation)
			{
				found = &candidate;
				break;
			}
		}
		if (found == nullptr)
		{
			return;
		}
		const auto was_selected = expected_device.load(std::memory_order_acquire) ==
				reinterpret_cast<std::uintptr_t>(device) &&
			(expected_context.load(std::memory_order_acquire) ==
				reinterpret_cast<std::uintptr_t>(context) ||
				expected_context.load(std::memory_order_acquire) == 0) &&
			expected_generation.load(std::memory_order_acquire) == device_generation;
		if (!was_selected)
		{
			const auto references = detach_candidate(*found);
			hook_lock.unlock();
			release_candidate_references(references);
			return;
		}
		set_history_tracking_enabled(false);
		set_history_tracking_client(history_tracking_client::ssr_consumer_probe, false);
		std::unique_lock lifecycle_lock(lifecycle_mutex);
		expected_context.store(0, std::memory_order_release);
		if (!wait_for_report_callbacks())
		{
			expected_context.store(reinterpret_cast<std::uintptr_t>(context),
				std::memory_order_release);
			return;
		}
		expected_device.store(0, std::memory_order_release);
		expected_generation.store(0, std::memory_order_release);
		selected_resource_history_complete.store(false, std::memory_order_release);
		if (current_state.load(std::memory_order_acquire) == state::pair_active ||
			current_state.load(std::memory_order_acquire) == state::eye_active)
		{
			fail_active_pair();
		}
		const auto references = detach_candidate(*found);
		lifecycle_lock.unlock();
		hook_lock.unlock();
		release_candidate_references(references);
	}

	bool begin_pair(const std::uint64_t pair_id,
		ID3D11DeviceContext* const context, const std::uint32_t owner_thread) noexcept
	{
		const std::lock_guard lock(lifecycle_mutex);
		if (!hooks_installed.load(std::memory_order_acquire) || pair_id == 0 ||
			context == nullptr || owner_thread == 0 || owner_thread != GetCurrentThreadId() ||
			expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context) ||
			current_state.load(std::memory_order_acquire) == state::pair_active ||
			current_state.load(std::memory_order_acquire) == state::eye_active)
		{
			return false;
		}
		clear_trivial(working_report);
		working_report.current_state = state::pair_active;
		working_report.pair_id = pair_id;
		working_report.owner_thread = owner_thread;
		clear_trivial(left_draws);
		active_pair.store(pair_id, std::memory_order_release);
		active_eye.store(invalid_eye, std::memory_order_release);
		active_owner_thread.store(owner_thread, std::memory_order_release);
		current_state.store(state::pair_active, std::memory_order_release);
		return true;
	}

	bool begin_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		const std::lock_guard lock(lifecycle_mutex);
		const auto expected_eye = working_report.completed_eye_mask == 0 ? 0u : 1u;
		if (current_state.load(std::memory_order_acquire) != state::pair_active ||
			active_pair.load(std::memory_order_acquire) != pair_id || eye >= 2 ||
			eye != expected_eye || GetCurrentThreadId() !=
				active_owner_thread.load(std::memory_order_acquire))
		{
			if (current_state.load(std::memory_order_acquire) == state::pair_active)
				fail_active_pair();
			return false;
		}
		auto* const context = reinterpret_cast<ID3D11DeviceContext*>(
			expected_context.load(std::memory_order_acquire));
		working_report.eyes[eye].begin = capture_boundary(context);
		active_eye.store(eye, std::memory_order_release);
		working_report.current_state = state::eye_active;
		current_state.store(state::eye_active, std::memory_order_release);
		return true;
	}

	void observe_vs_draw(ID3D11DeviceContext* const context,
		const std::uint64_t ordinal, const std::uintptr_t caller,
		ID3D11VertexShader* const shader, ID3D11Buffer* const slot3_buffer) noexcept
	{
		const active_report_callback callback(context);
		if (!callback || current_state.load(std::memory_order_acquire) !=
			state::eye_active)
		{
			return;
		}
		const auto eye = active_eye.load(std::memory_order_acquire);
		if (eye >= 2 || GetCurrentThreadId() !=
			active_owner_thread.load(std::memory_order_acquire))
		{
			foreign_thread_pair_calls.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		auto& eye_output = working_report.eyes[eye];
		const auto index = eye_output.draws++;
		const auto snapshot = make_slot_snapshot(shader, slot3_buffer, false,
			expected_generation.load(std::memory_order_acquire));
		switch (snapshot.usage)
		{
		case shader_usage::used: ++eye_output.shader_used_draws; break;
		case shader_usage::unused: ++eye_output.shader_unused_draws; break;
		case shader_usage::unknown: ++eye_output.shader_unknown_draws; break;
		}
		if (index >= left_draws.size())
		{
			++working_report.draw_overflows;
			return;
		}
		if (eye == 0)
		{
			left_draws[index] = {ordinal, caller, snapshot};
			return;
		}

		++working_report.comparisons;
		const auto& left = left_draws[index];
		if (left.ordinal != ordinal)
		{
			++working_report.ordinal_mismatches;
			if (working_report.sample_count < working_report.samples.size())
			{
				working_report.samples[working_report.sample_count++] = {
					ordinal, caller, left.caller, caller, left.slot, snapshot};
			}
			return;
		}
		const auto left_bound = left.slot.content.buffer != 0;
		const auto right_bound = snapshot.content.buffer != 0;
		bool noteworthy = left.slot.content.buffer != snapshot.content.buffer ||
			left.slot.content.upload_generation != snapshot.content.upload_generation ||
			left.slot.origin != snapshot.origin;
		if (left_bound && !right_bound)
		{
			if (left.slot.shader == snapshot.shader &&
				left.slot.usage == shader_usage::used)
				++working_report.used_left_bound_right_null;
			else if (left.slot.shader == snapshot.shader &&
				left.slot.usage == shader_usage::unused)
				++working_report.unused_left_bound_right_null;
			else ++working_report.unknown_usage_left_bound_right_null;
		}
		if (left_bound && right_bound)
		{
			if (left.slot.content.buffer == snapshot.content.buffer &&
				left.slot.content.upload_generation !=
					snapshot.content.upload_generation)
			{
				++working_report.same_identity_new_generation;
			}
			if (left.slot.content.known && snapshot.content.known)
			{
				if (left.slot.content.hash_low == snapshot.content.hash_low &&
					left.slot.content.hash_high == snapshot.content.hash_high &&
					left.slot.content.byte_width == snapshot.content.byte_width)
				{
					++working_report.both_bound_same_content;
				}
				else ++working_report.both_bound_different_content;
			}
			else ++working_report.both_bound_content_unknown;
		}
		if (left.slot.origin == bind_origin::unknown ||
			snapshot.origin == bind_origin::unknown)
		{
			++working_report.provenance_unknown;
			noteworthy = true;
		}
		if (noteworthy && working_report.sample_count < working_report.samples.size())
		{
			working_report.samples[working_report.sample_count++] = {
				ordinal, caller, left.caller, caller, left.slot, snapshot};
		}
	}

	bool end_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		const std::lock_guard lock(lifecycle_mutex);
		if (current_state.load(std::memory_order_acquire) != state::eye_active ||
			active_pair.load(std::memory_order_acquire) != pair_id || eye >= 2 ||
			active_eye.load(std::memory_order_acquire) != eye ||
			GetCurrentThreadId() != active_owner_thread.load(std::memory_order_acquire))
		{
			if (current_state.load(std::memory_order_acquire) == state::eye_active)
				fail_active_pair();
			return false;
		}
		auto* const context = reinterpret_cast<ID3D11DeviceContext*>(
			expected_context.load(std::memory_order_acquire));
		working_report.eyes[eye].end = capture_boundary(context);
		working_report.completed_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		working_report.current_state = state::pair_active;
		active_eye.store(invalid_eye, std::memory_order_release);
		current_state.store(state::pair_active, std::memory_order_release);
		return true;
	}

	bool end_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(lifecycle_mutex);
		if (current_state.load(std::memory_order_acquire) != state::pair_active ||
			active_pair.load(std::memory_order_acquire) != pair_id ||
			working_report.completed_eye_mask != 0x3 ||
			GetCurrentThreadId() != active_owner_thread.load(std::memory_order_acquire))
		{
			if (current_state.load(std::memory_order_acquire) == state::pair_active)
				fail_active_pair();
			return false;
		}
		working_report.current_state = state::complete;
		fill_runtime_status(working_report);
		published_report = working_report;
		current_state.store(state::complete, std::memory_order_release);
		active_pair.store(0, std::memory_order_release);
		active_eye.store(invalid_eye, std::memory_order_release);
		active_owner_thread.store(0, std::memory_order_release);
		return true;
	}

	bool abort_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(lifecycle_mutex);
		const auto live_state = current_state.load(std::memory_order_acquire);
		if (pair_id == 0 || active_pair.load(std::memory_order_acquire) != pair_id ||
			active_owner_thread.load(std::memory_order_acquire) != GetCurrentThreadId() ||
			(live_state != state::pair_active && live_state != state::eye_active))
		{
			return false;
		}
		fail_active_pair();
		return true;
	}

	void observe_resource_operation(
		const engine_stereo_resource_ops::event& event) noexcept
	{
		if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
		const auto tracking_epoch = history_tracking_epoch.load(
			std::memory_order_acquire);
		const auto candidate = find_candidate(event.context);
		if (candidate.generation == 0)
		{
			unregistered_context_calls.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		switch (event.operation)
		{
		case engine_stereo_resource_ops::api::copy_resource:
		{
			resource_copies.fetch_add(1, std::memory_order_relaxed);
			auto* const destination = find_or_register_resource(event.destination,
				candidate.generation);
			if (destination == nullptr) return;
			const auto destination_content = read_content(
				reinterpret_cast<std::uintptr_t>(event.destination), candidate.generation);
			copy_source_candidates.fetch_add(1, std::memory_order_relaxed);
			buffer_entry* source{};
			D3D11_RESOURCE_DIMENSION source_dimension{D3D11_RESOURCE_DIMENSION_UNKNOWN};
			D3D11_BUFFER_DESC source_description{};
			bool source_description_available{};
			if (event.source != nullptr)
			{
				event.source->GetType(&source_dimension);
				if (source_dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
				{
					auto* const source_buffer = static_cast<ID3D11Buffer*>(event.source);
					source_buffer->GetDesc(&source_description);
					source_description_available = true;
					source = find_or_register_buffer(source_buffer, candidate.generation,
						true, destination_content.byte_width);
				}
			}
			if (source != nullptr)
				copy_source_registered.fetch_add(1, std::memory_order_relaxed);
			const auto source_content = read_content(
				reinterpret_cast<std::uintptr_t>(event.source), candidate.generation);
			const auto known = source != nullptr && source_content.known &&
				source_content.byte_width == destination_content.byte_width;
			content_byte_snapshot source_bytes{};
			const auto bytes_available = known && source_content.upload_generation != 0 &&
				read_content_bytes(*source, candidate.generation, tracking_epoch,
					source_content.upload_generation, source_bytes) &&
				source_bytes.complete &&
				source_bytes.byte_width == destination_content.byte_width;
			if (known) copy_source_known.fetch_add(1, std::memory_order_relaxed);
			if (bytes_available)
				copy_source_bytes_propagated.fetch_add(1, std::memory_order_relaxed);
			note_copy_source_sample(destination_content, event.source, source_dimension,
				source_description_available ? &source_description : nullptr,
				source_content, source != nullptr, known, bytes_available, event.caller);
			publish_content(*destination, known, source_content.hash_low,
				source_content.hash_high, upload_source::copy_resource, event.caller,
				tracking_epoch, bytes_available ? source_bytes.bytes.data() : nullptr,
				bytes_available ? source_bytes.byte_width : 0);
			if (!known) resource_unknown_writes.fetch_add(1,
				std::memory_order_relaxed);
			return;
		}
		case engine_stereo_resource_ops::api::copy_subresource_region:
		{
			resource_copies.fetch_add(1, std::memory_order_relaxed);
			if (auto* const destination = find_or_register_resource(event.destination,
				candidate.generation))
			{
				// The current resource_ops event does not retain the D3D11_BOX, so
				// full-buffer coverage cannot be proven and must remain unknown.
				publish_content(*destination, false, 0, 0,
					upload_source::partial_copy, event.caller, tracking_epoch);
				resource_unknown_writes.fetch_add(1, std::memory_order_relaxed);
			}
			return;
		}
		case engine_stereo_resource_ops::api::update_subresource:
		{
			resource_updates.fetch_add(1, std::memory_order_relaxed);
			if (auto* const destination = find_or_register_resource(event.destination,
				candidate.generation))
			{
				const auto current = read_content(
					reinterpret_cast<std::uintptr_t>(event.destination),
					candidate.generation);
				std::uint64_t low{}, high{};
				// D3D11 buffer updates ignore row/depth pitch. A null box and
				// subresource zero prove complete constant-buffer coverage.
				const auto known = event.destination_subresource == 0 &&
					event.update_box == nullptr && event.source_data != nullptr &&
					hash_bytes_guarded(event.source_data, current.byte_width, low, high);
				publish_content(*destination, known, low, high,
					upload_source::update_subresource, event.caller, tracking_epoch,
					known ? event.source_data : nullptr,
					known ? current.byte_width : 0);
				if (!known) resource_unknown_writes.fetch_add(1,
					std::memory_order_relaxed);
			}
			return;
		}
		default: return;
		}
	}

	void observe_update_content(ID3D11DeviceContext* const context,
		ID3D11Resource* const destination, const void* const source_data,
		const std::size_t source_size, const bool complete_resource,
		const std::uintptr_t caller) noexcept
	{
		if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
		const auto tracking_epoch = history_tracking_epoch.load(
			std::memory_order_acquire);
		const auto candidate = find_candidate(context);
		if (candidate.generation == 0)
		{
			unregistered_context_calls.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		resource_updates.fetch_add(1, std::memory_order_relaxed);
		auto* const entry = find_or_register_resource(destination,
			candidate.generation);
		if (entry == nullptr) return;
		const auto current = read_content(reinterpret_cast<std::uintptr_t>(destination),
			candidate.generation);
		std::uint64_t low{}, high{};
		const auto known = complete_resource && source_size == current.byte_width &&
			hash_bytes_guarded(source_data, source_size, low, high);
		publish_content(*entry, known, low, high, upload_source::update_subresource,
			caller, tracking_epoch, known ? source_data : nullptr,
			known ? source_size : 0);
		if (!known) resource_unknown_writes.fetch_add(1, std::memory_order_relaxed);
	}

	void observe_clear_state(ID3D11DeviceContext* const context,
		const std::uintptr_t caller) noexcept
	{
		if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
		const active_report_callback callback(context);
		if (!callback) return;
		publish_slot_metadata(0, bind_origin::clear_state, caller);
		record_active_set_event(0, bind_origin::clear_state, caller);
	}

	void observe_opaque_state_change(ID3D11DeviceContext* const context,
		ID3D11CommandList*, const bool restore_context_state,
		const std::uintptr_t caller) noexcept
	{
		if (!history_tracking_enabled.load(std::memory_order_acquire)) return;
		// D3D11 guarantees that ExecuteCommandList(TRUE) restores the immediate
		// context state. Resource contents may change, but its CB bindings do not.
		if (restore_context_state) return;
		const active_report_callback callback(context);
		if (!callback) return;
		publish_slot_metadata(0, bind_origin::opaque_state_change, caller);
		record_active_set_event(0, bind_origin::opaque_state_change, caller);
	}

	bool query_content_snapshot(ID3D11Buffer* const buffer,
		content_snapshot& output) noexcept
	{
		output = {};
		if (buffer == nullptr || !hooks_installed.load(std::memory_order_acquire))
			return false;
		const auto generation = expected_generation.load(std::memory_order_acquire);
		if (generation == 0 || find_or_register_buffer(buffer, generation) == nullptr)
			return false;
		output = read_content(reinterpret_cast<std::uintptr_t>(buffer), generation);
		return output.buffer == reinterpret_cast<std::uintptr_t>(buffer) &&
			output.device_generation == generation;
	}

	bool query_content_bytes(ID3D11Buffer* const buffer,
		const std::uint64_t upload_generation,
		content_byte_snapshot& output) noexcept
	{
		output = {};
		if (buffer == nullptr || upload_generation == 0 ||
			!hooks_installed.load(std::memory_order_acquire))
		{
			return false;
		}
		const auto generation = expected_generation.load(std::memory_order_acquire);
		auto* const entry = generation == 0 ? nullptr : find_or_register_buffer(
			buffer, generation);
		if (entry == nullptr) return false;
		const auto epoch = history_tracking_epoch.load(std::memory_order_acquire);
		return read_content_bytes(*entry, generation, epoch, upload_generation, output);
	}

	void get_report(report& output) noexcept
	{
		const std::lock_guard lock(lifecycle_mutex);
		const auto live_state = current_state.load(std::memory_order_acquire);
		if (live_state == state::pair_active || live_state == state::eye_active)
		{
			// Working eye arrays are owned by the D3D11 owner thread. Do not copy
			// them concurrently; publish only the stable lifecycle header.
			clear_trivial(output);
			output.current_state = live_state;
			output.pair_id = active_pair.load(std::memory_order_acquire);
			output.owner_thread = active_owner_thread.load(std::memory_order_acquire);
		}
		else
		{
			output = published_report;
			output.current_state = live_state;
		}
		fill_runtime_status(output);
	}

	const char* to_string(const state value) noexcept
	{
		switch (value)
		{
		case state::idle: return "idle";
		case state::pair_active: return "pair_active";
		case state::eye_active: return "eye_active";
		case state::complete: return "complete";
		case state::failed: return "failed";
		}
		return "unknown";
	}

	const char* to_string(const shader_usage value) noexcept
	{
		switch (value)
		{
		case shader_usage::unknown: return "unknown";
		case shader_usage::unused: return "unused";
		case shader_usage::used: return "used";
		}
		return "unknown";
	}

	const char* to_string(const bind_origin value) noexcept
	{
		switch (value)
		{
		case bind_origin::unknown: return "unknown";
		case bind_origin::eye_boundary: return "eye_boundary";
		case bind_origin::explicit_bind: return "explicit_bind";
		case bind_origin::explicit_null: return "explicit_null";
		case bind_origin::clear_state: return "clear_state";
		case bind_origin::opaque_state_change: return "opaque_state_change";
		}
		return "unknown";
	}

	const char* to_string(const upload_source value) noexcept
	{
		switch (value)
		{
		case upload_source::unknown: return "unknown";
		case upload_source::map_unmap: return "map_unmap";
		case upload_source::update_subresource: return "update_subresource";
		case upload_source::copy_resource: return "copy_resource";
		case upload_source::partial_copy: return "partial_copy";
		}
		return "unknown";
	}
}
