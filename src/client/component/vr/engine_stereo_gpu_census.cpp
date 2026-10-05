#include <std_include.hpp>
#include "component/vr/native_render_contract.hpp"
#include "engine_stereo_gpu_census.hpp"

#include "engine_stereo_material_buffer_probe.hpp"
#include "engine_stereo_particle_buffer_probe.hpp"
#include "component/d3d11.hpp"
#include "engine_backend_probe.hpp"
#include "engine_stereo_dynamic_arena.hpp"
#include "engine_stereo_execution.hpp"
#include "engine_stereo_output_merger.hpp"
#include "engine_stereo_resource_ops.hpp"
#include <d3dcompiler.h>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

#pragma comment(lib, "d3dcompiler.lib")

namespace vr::engine_stereo_gpu_census
{
	static_assert(static_cast<std::uint8_t>(api::dispatch_indirect) ==
		static_cast<std::uint8_t>(engine_stereo_execution::api::dispatch_indirect));
	static_assert(dynamic_fx_stream_window_size == 3);

	dynamic_fx_family classify_dynamic_fx_caller(const std::uintptr_t caller) noexcept
	{
		// These are the exact x64 .pdata extents for H2's four dynamic-mesh
		// consumers. End addresses are exclusive, matching RUNTIME_FUNCTION.
		if (caller >= 0x1407B9780ull && caller < 0x1407B9AFBull)
			return dynamic_fx_family::code_trans;
		if (caller >= 0x1407B9B00ull && caller < 0x1407B9D76ull)
			return dynamic_fx_family::glass;
		if (caller >= 0x1407B9D80ull && caller < 0x1407BA3DBull)
			return dynamic_fx_family::mark;
		// The real particle-cloud DrawIndexed return address (0x1407BA988) lies
		// inside this exact SPARK .pdata extent.
		if (caller >= 0x1407BA6B0ull && caller < 0x1407BA9DBull)
			return dynamic_fx_family::spark;
		return dynamic_fx_family::unknown;
	}

	namespace
	{
		template <typename T>
		void reset_large_object(T& value) noexcept
		{
			value.~T();
			::new (static_cast<void*>(&value)) T{};
		}

		std::mutex census_mutex;
		report census;
		std::uint32_t active_eye{2};
		std::uint64_t call_sequence{};
		ID3D11DeviceContext* census_context{};
		std::atomic_uint32_t observation_thread_id{};
		std::atomic_uint64_t foreign_thread_observations{};
		constexpr std::size_t maximum_reflected_shaders = 2048;
		constexpr std::size_t maximum_srv_resource_descriptors = 4096;
		constexpr std::size_t maximum_shader_bytecode_bytes = 256 * 1024;
		constexpr UINT maximum_reflected_bindings = 1024;
		constexpr std::size_t maximum_reflected_constant_buffers = 512;
		constexpr std::size_t maximum_reflected_constant_variables = 8192;

		struct reflected_srv_binding
		{
			bool declared{};
			std::uint64_t name_hash{};
			std::uint32_t input_type{}, return_type{}, dimension{}, bind_point{},
				bind_count{};
			std::array<char, maximum_shader_binding_name> name{};
		};

		struct reflected_shader
		{
			std::uintptr_t identity{};
			shader_stage stage{shader_stage::vs};
			bool attempted{}, resolved{}, constants_attempted{}, constants_resolved{};
			std::uint64_t debug_name_hash{};
			std::array<char, maximum_shader_debug_name> debug_name{};
			std::array<reflected_srv_binding, scanned_srv_slots> bindings{};
			std::size_t constant_buffer_begin{}, constant_buffer_count{};
		};

		struct reflected_constant_variable
		{
			std::uint64_t name_hash{};
			std::uint32_t start_offset{}, size{};
			std::array<char, maximum_shader_binding_name> name{};
		};

		struct reflected_constant_buffer
		{
			std::uintptr_t shader{};
			shader_stage stage{shader_stage::vs};
			std::uint32_t bind_point{}, size{};
			std::uint64_t name_hash{};
			std::array<char, maximum_shader_binding_name> name{};
			std::size_t variable_begin{}, variable_count{};
		};

		struct srv_resource_descriptor_entry
		{
			std::uintptr_t identity{};
			resource_descriptor descriptor{};
		};

		std::array<reflected_shader, maximum_reflected_shaders> reflected_shaders{};
		std::array<reflected_constant_buffer, maximum_reflected_constant_buffers>
			reflected_constant_buffers{};
		std::array<reflected_constant_variable, maximum_reflected_constant_variables>
			reflected_constant_variables{};
		std::size_t reflected_constant_buffer_count{},
			reflected_constant_variable_count{};
		std::array<srv_resource_descriptor_entry,
			maximum_srv_resource_descriptors> srv_resource_descriptors{};
		thread_local std::array<std::byte, maximum_shader_bytecode_bytes>
			shader_bytecode_scratch{};
		struct constant_buffer_slot_identity
		{
			std::uint8_t stage{}, slot{};
			std::uintptr_t identity{};
			engine_stereo_constant_buffer_probe::content_snapshot content{};
		};

		struct shader_resource_slot_identity
		{
			std::uint8_t stage{}, slot{};
			srv_binding_identity binding{};
		};

		struct sampler_slot_identity
		{
			std::uint8_t stage{}, slot{};
			std::uintptr_t identity{};
			sampler_descriptor descriptor{};
		};

		struct detailed_binding_snapshot
		{
			std::array<constant_buffer_slot_identity,
				maximum_observation_constant_buffers> constant_buffers{};
			std::array<shader_resource_slot_identity,
				maximum_observation_shader_resources> shader_resources{};
			std::array<sampler_slot_identity,
				D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers{};
			std::uint16_t constant_buffer_count{}, shader_resource_count{},
				sampler_count{};
			std::uint16_t constant_buffer_dropped{}, shader_resource_dropped{};
			std::array<std::uint16_t, shader_stage_count>
				constant_buffer_dropped_by_stage{}, shader_resource_dropped_by_stage{};
		};

		struct detailed_observation
		{
			observation_signature signature{};
			detailed_binding_snapshot bindings{};
		};

		// The detailed snapshots exist only for the single armed pair. Fixed static
		// storage avoids allocations in D3D11 invocation hooks and is never copied
		// into the public report; only bounded category samples are published.
		std::array<detailed_observation, maximum_observations_per_eye>
			left_signatures{};
		detailed_observation right_signature{};
		std::array<std::uint64_t, maximum_observations_per_eye>
			left_dynamic_fx_family_ordinals{}, left_dynamic_fx_arena_sequences{};
		dynamic_fx_stream_entry previous_right_stream_entry{};
		std::array<dynamic_fx_family_comparison, dynamic_fx_family_count>
			dynamic_fx_binding_working{};
		constexpr std::size_t maximum_dynamic_fx_content_captures_per_eye = 4096;
		struct dynamic_fx_content_capture
		{
			std::uint64_t output_ordinal{};
			std::uint8_t stage{}, slot{};
			engine_stereo_constant_buffer_probe::content_byte_snapshot content{};
		};
		std::array<std::array<dynamic_fx_content_capture,
			maximum_dynamic_fx_content_captures_per_eye>, 2>
			dynamic_fx_content_captures{};
		std::array<std::size_t, 2> dynamic_fx_content_capture_counts{};

		void capture_dynamic_fx_content_bytes(const std::uint32_t eye,
			const std::uint64_t output_ordinal, const shader_stage stage,
			const std::uint8_t slot, ID3D11Buffer* const buffer,
			const engine_stereo_constant_buffer_probe::content_snapshot& metadata) noexcept
		{
			if (eye >= 2 || buffer == nullptr || !metadata.known ||
				metadata.byte_width == 0 || metadata.byte_width >
					engine_stereo_constant_buffer_probe::maximum_content_byte_snapshot)
			{
				return;
			}
			++census.dynamic_fx.content_byte_capture_attempts;
			auto& count = dynamic_fx_content_capture_counts[eye];
			if (count >= dynamic_fx_content_captures[eye].size())
			{
				++census.dynamic_fx.content_byte_capture_overflows;
				return;
			}
			auto& output = dynamic_fx_content_captures[eye][count];
			if (!engine_stereo_constant_buffer_probe::query_content_bytes(buffer,
				metadata.upload_generation, output.content))
			{
				++census.dynamic_fx.content_byte_capture_unavailable;
				return;
			}
			output.output_ordinal = output_ordinal;
			output.stage = static_cast<std::uint8_t>(stage);
			output.slot = slot;
			++count;
			++census.dynamic_fx.content_byte_capture_completions;
		}

		[[nodiscard]] const dynamic_fx_content_capture* find_dynamic_fx_content_capture(
			const std::uint32_t eye, const std::uint64_t output_ordinal,
			const std::uint8_t stage, const std::uint8_t slot) noexcept
		{
			if (eye >= 2) return nullptr;
			for (auto index = dynamic_fx_content_capture_counts[eye]; index != 0; --index)
			{
				const auto& candidate = dynamic_fx_content_captures[eye][index - 1];
				if (candidate.output_ordinal == output_ordinal &&
					candidate.stage == stage && candidate.slot == slot)
				{
					return &candidate;
				}
			}
			return nullptr;
		}

		[[nodiscard]] constexpr bool is_dispatch(const api operation) noexcept
		{
			return operation == api::dispatch || operation == api::dispatch_indirect;
		}

		[[nodiscard]] constexpr std::size_t stage_index(
			const shader_stage stage) noexcept
		{
			return static_cast<std::size_t>(stage);
		}

		[[nodiscard]] std::uint64_t hash_bytes(const char* const data,
			const std::size_t size) noexcept
		{
			std::uint64_t hash = 1469598103934665603ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			for (std::size_t index{}; index < size; ++index)
			{
				hash ^= static_cast<std::uint8_t>(data[index]);
				hash *= prime;
			}
			return hash;
		}

		template <std::size_t Size>
		void copy_text(const char* const source, std::array<char, Size>& destination,
			std::uint64_t& hash, std::uint64_t& truncations) noexcept
		{
			static_assert(Size > 1);
			destination = {};
			if (source == nullptr) return;
			const auto length = std::strlen(source);
			hash = hash_bytes(source, length);
			const auto copied = (std::min)(length, Size - 1);
			std::memcpy(destination.data(), source, copied);
			if (copied != length) ++truncations;
		}

		[[nodiscard]] constexpr UINT expected_shader_version(
			const shader_stage stage) noexcept
		{
			switch (stage)
			{
			case shader_stage::vs: return D3D11_SHVER_VERTEX_SHADER;
			case shader_stage::ps: return D3D11_SHVER_PIXEL_SHADER;
			case shader_stage::cs: return D3D11_SHVER_COMPUTE_SHADER;
			case shader_stage::gs: return D3D11_SHVER_GEOMETRY_SHADER;
			case shader_stage::hs: return D3D11_SHVER_HULL_SHADER;
			case shader_stage::ds: return D3D11_SHVER_DOMAIN_SHADER;
			default: return ~0u;
			}
		}

		[[nodiscard]] constexpr bool is_srv_input_type(
			const D3D_SHADER_INPUT_TYPE type) noexcept
		{
			return type == D3D_SIT_TBUFFER || type == D3D_SIT_TEXTURE ||
				type == D3D_SIT_STRUCTURED || type == D3D_SIT_BYTEADDRESS;
		}

		[[nodiscard]] reflected_shader* find_reflected_shader(
			const std::uintptr_t identity, const shader_stage stage,
			const bool create) noexcept
		{
			if (!identity) return nullptr;
			static_assert((maximum_reflected_shaders &
				(maximum_reflected_shaders - 1)) == 0);
			auto index = ((identity >> 4) ^ (stage_index(stage) * 131u)) &
				(maximum_reflected_shaders - 1);
			for (std::size_t probe{}; probe < maximum_reflected_shaders; ++probe)
			{
				auto& entry = reflected_shaders[index];
				if (entry.identity == identity && entry.stage == stage) return &entry;
				if (!entry.identity)
				{
					if (!create) return nullptr;
					entry.identity = identity;
					entry.stage = stage;
					return &entry;
				}
				index = (index + 1) & (maximum_reflected_shaders - 1);
			}
			if (create)
			{
				auto& usage = census.ordered.shader_resource_usage;
				++usage.shader_cache_overflows;
				usage.classification_complete = false;
			}
			return nullptr;
		}

		void capture_shader_debug_name(ID3D11DeviceChild* const shader,
			reflected_shader& entry) noexcept
		{
			UINT size = static_cast<UINT>(entry.debug_name.size());
			const auto result = shader->GetPrivateData(WKPDID_D3DDebugObjectName,
				&size, entry.debug_name.data());
			auto& usage = census.ordered.shader_resource_usage;
			if (result == DXGI_ERROR_MORE_DATA || size >= entry.debug_name.size())
			{
				entry.debug_name = {};
				++usage.shader_debug_name_truncations;
				return;
			}
			if (FAILED(result) || size == 0)
			{
				entry.debug_name = {};
				++usage.shader_debug_name_missing;
				return;
			}
			const auto hashed_size = (std::min)(static_cast<std::size_t>(size),
				entry.debug_name.size());
			entry.debug_name_hash = hash_bytes(entry.debug_name.data(), hashed_size);
			entry.debug_name.back() = '\0';
		}

		void reflect_shader(ID3D11DeviceChild* const shader,
			const shader_stage stage) noexcept
		{
			if (shader == nullptr) return;
			auto* const entry = find_reflected_shader(
				reinterpret_cast<std::uintptr_t>(shader), stage, true);
			if (entry == nullptr || entry->attempted) return;
			entry->attempted = true;
			capture_shader_debug_name(shader, *entry);
			auto& usage = census.ordered.shader_resource_usage;
			UINT bytecode_size = static_cast<UINT>(shader_bytecode_scratch.size());
			auto result = shader->GetPrivateData(d3d11::guid_shader_bytecode,
				&bytecode_size, shader_bytecode_scratch.data());
			if (result == DXGI_ERROR_MORE_DATA ||
				bytecode_size > shader_bytecode_scratch.size())
			{
				++usage.shader_bytecode_oversized;
				usage.classification_complete = false;
				return;
			}
			if (FAILED(result) || bytecode_size == 0)
			{
				++usage.shader_bytecode_missing;
				usage.classification_complete = false;
				return;
			}

			ID3D11ShaderReflection* reflection{};
			result = D3DReflect(shader_bytecode_scratch.data(), bytecode_size,
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(&reflection));
			if (FAILED(result) || reflection == nullptr)
			{
				++usage.shader_reflection_failures;
				usage.classification_complete = false;
				return;
			}
			D3D11_SHADER_DESC description{};
			result = reflection->GetDesc(&description);
			if (SUCCEEDED(result) && D3D11_SHVER_GET_TYPE(description.Version) !=
				expected_shader_version(stage))
			{
				++usage.shader_stage_mismatches;
				result = E_INVALIDARG;
			}
			if (SUCCEEDED(result) && description.BoundResources >
				maximum_reflected_bindings)
			{
				++usage.shader_binding_overflows;
				result = E_BOUNDS;
			}
			if (SUCCEEDED(result))
			{
				for (UINT binding_index{}; binding_index < description.BoundResources;
					++binding_index)
				{
					D3D11_SHADER_INPUT_BIND_DESC binding{};
					if (FAILED(reflection->GetResourceBindingDesc(binding_index,
						&binding)))
					{
						result = E_FAIL;
						break;
					}
					if (!is_srv_input_type(binding.Type) || binding.BindCount == 0)
						continue;
					++usage.reflected_bindings;
					reflected_srv_binding reflected{};
					reflected.declared = true;
					reflected.input_type = static_cast<std::uint32_t>(binding.Type);
					reflected.return_type = static_cast<std::uint32_t>(binding.ReturnType);
					reflected.dimension = static_cast<std::uint32_t>(binding.Dimension);
					reflected.bind_point = binding.BindPoint;
					reflected.bind_count = binding.BindCount;
					copy_text(binding.Name, reflected.name, reflected.name_hash,
						usage.binding_name_truncations);
					for (std::size_t slot{}; slot < entry->bindings.size(); ++slot)
					{
						if (slot < binding.BindPoint ||
							slot - binding.BindPoint >= binding.BindCount) continue;
						entry->bindings[slot] = reflected;
					}
				}
			}
			reflection->Release();
			if (FAILED(result))
			{
				++usage.shader_reflection_failures;
				usage.classification_complete = false;
				return;
			}
			entry->resolved = true;
			++usage.reflected_shaders;
		}

		void reflect_constant_buffers(ID3D11DeviceChild* const shader,
			const shader_stage stage) noexcept
		{
			if (shader == nullptr) return;
			auto* const entry = find_reflected_shader(
				reinterpret_cast<std::uintptr_t>(shader), stage, true);
			if (entry == nullptr || entry->constants_attempted) return;
			entry->constants_attempted = true;
			auto& dynamic_fx = census.dynamic_fx;
			++dynamic_fx.reflection_attempts;

			UINT bytecode_size = static_cast<UINT>(shader_bytecode_scratch.size());
			auto result = shader->GetPrivateData(d3d11::guid_shader_bytecode,
				&bytecode_size, shader_bytecode_scratch.data());
			if (result == DXGI_ERROR_MORE_DATA ||
				bytecode_size > shader_bytecode_scratch.size())
			{
				++dynamic_fx.reflection_bytecode_oversized;
				return;
			}
			if (FAILED(result) || bytecode_size == 0)
			{
				++dynamic_fx.reflection_bytecode_missing;
				return;
			}

			ID3D11ShaderReflection* reflection{};
			result = D3DReflect(shader_bytecode_scratch.data(), bytecode_size,
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(&reflection));
			if (FAILED(result) || reflection == nullptr)
			{
				++dynamic_fx.reflection_failures;
				return;
			}

			const auto original_buffer_count = reflected_constant_buffer_count;
			const auto original_variable_count = reflected_constant_variable_count;
			D3D11_SHADER_DESC shader_description{};
			result = reflection->GetDesc(&shader_description);
			if (SUCCEEDED(result) && D3D11_SHVER_GET_TYPE(shader_description.Version) !=
				expected_shader_version(stage))
			{
				++dynamic_fx.reflection_stage_mismatches;
				result = E_INVALIDARG;
			}
			entry->constant_buffer_begin = original_buffer_count;
			if (SUCCEEDED(result))
			{
				for (UINT binding_index{};
					binding_index < shader_description.BoundResources; ++binding_index)
				{
					D3D11_SHADER_INPUT_BIND_DESC binding{};
					if (FAILED(reflection->GetResourceBindingDesc(binding_index,
						&binding)))
					{
						result = E_FAIL;
						break;
					}
					if (binding.Type != D3D_SIT_CBUFFER || binding.BindCount == 0)
						continue;
					auto* const reflected_buffer =
						reflection->GetConstantBufferByName(binding.Name);
					D3D11_SHADER_BUFFER_DESC buffer_description{};
					if (reflected_buffer == nullptr ||
						FAILED(reflected_buffer->GetDesc(&buffer_description)))
					{
						result = E_FAIL;
						break;
					}
					for (UINT binding_offset{}; binding_offset < binding.BindCount;
						++binding_offset)
					{
						if (reflected_constant_buffer_count >=
							reflected_constant_buffers.size())
						{
							++dynamic_fx.reflection_buffer_overflows;
							result = E_BOUNDS;
							break;
						}
						if (buffer_description.Variables >
							reflected_constant_variables.size() -
								reflected_constant_variable_count)
						{
							++dynamic_fx.reflection_variable_overflows;
							result = E_BOUNDS;
							break;
						}
						auto& output = reflected_constant_buffers[
							reflected_constant_buffer_count++];
						output.shader = entry->identity;
						output.stage = stage;
						output.bind_point = binding.BindPoint + binding_offset;
						output.size = buffer_description.Size;
						output.variable_begin = reflected_constant_variable_count;
						copy_text(binding.Name, output.name, output.name_hash,
							dynamic_fx.reflection_name_truncations);
						for (UINT variable_index{};
							variable_index < buffer_description.Variables;
							++variable_index)
						{
							auto* const variable =
								reflected_buffer->GetVariableByIndex(variable_index);
							D3D11_SHADER_VARIABLE_DESC variable_description{};
							if (variable == nullptr ||
								FAILED(variable->GetDesc(&variable_description)))
							{
								result = E_FAIL;
								break;
							}
							auto& reflected_variable = reflected_constant_variables[
								reflected_constant_variable_count++];
							reflected_variable.start_offset =
								variable_description.StartOffset;
							reflected_variable.size = variable_description.Size;
							copy_text(variable_description.Name, reflected_variable.name,
								reflected_variable.name_hash,
								dynamic_fx.reflection_name_truncations);
						}
						if (FAILED(result)) break;
						output.variable_count = reflected_constant_variable_count -
							output.variable_begin;
					}
					if (FAILED(result)) break;
				}
			}
			reflection->Release();
			if (FAILED(result))
			{
				reflected_constant_buffer_count = original_buffer_count;
				reflected_constant_variable_count = original_variable_count;
				entry->constant_buffer_count = 0;
				++dynamic_fx.reflection_failures;
				return;
			}
			entry->constant_buffer_count = reflected_constant_buffer_count -
				entry->constant_buffer_begin;
			entry->constants_resolved = true;
			++dynamic_fx.reflection_completions;
		}

		[[nodiscard]] std::uint64_t hash_arguments(
			const invocation_arguments& arguments) noexcept
		{
			std::uint64_t hash = 1469598103934665603ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			const auto count = (std::min)(arguments.count,
				static_cast<std::uint8_t>(arguments.values.size()));
			for (std::uint8_t index{}; index < count; ++index)
			{
				auto value = arguments.values[index];
				for (std::size_t byte{}; byte < sizeof(value); ++byte)
				{
					hash ^= static_cast<std::uint8_t>(value & 0xFFu);
					hash *= prime;
					value >>= 8;
				}
			}
			hash ^= count;
			hash *= prime;
			return hash;
		}

		[[nodiscard]] bool arguments_equal(const invocation_arguments& left,
			const invocation_arguments& right) noexcept
		{
			if (left.count != right.count) return false;
			const auto count = (std::min)(left.count,
				static_cast<std::uint8_t>(left.values.size()));
			return std::equal(left.values.begin(), left.values.begin() + count,
				right.values.begin());
		}

		[[nodiscard]] observation_context make_context(
			const observation_signature& value) noexcept
		{
			return {value.operation, value.caller, value.output_target_id};
		}

		void hash_binding(std::uint64_t& hash, std::uint32_t& count,
			const std::uint8_t domain, const std::uint8_t slot,
			const std::uintptr_t identity) noexcept
		{
			if (identity == 0) return;
			constexpr std::uint64_t prime = 1099511628211ull;
			const auto mix = [&](const std::uint8_t byte)
			{
				hash ^= byte;
				hash *= prime;
			};
			mix(domain);
			mix(slot);
			for (std::size_t index{}; index < sizeof(identity); ++index)
				mix(static_cast<std::uint8_t>(identity >> (index * 8)));
			++count;
		}

		void record_constant_buffer(detailed_binding_snapshot& output,
			const shader_stage stage, const std::uint8_t slot,
			const std::uintptr_t identity,
			const engine_stereo_constant_buffer_probe::content_snapshot* const
				content = nullptr) noexcept
		{
			if (!identity) return;
			if (output.constant_buffer_count >= output.constant_buffers.size())
			{
				++output.constant_buffer_dropped;
				++output.constant_buffer_dropped_by_stage[stage_index(stage)];
				return;
			}
			auto& entry = output.constant_buffers[output.constant_buffer_count++];
			entry.stage = static_cast<std::uint8_t>(stage);
			entry.slot = slot;
			entry.identity = identity;
			if (content) entry.content = *content;
		}

		void record_shader_resource(detailed_binding_snapshot& output,
			const shader_stage stage, const std::uint8_t slot,
			const srv_binding_identity& binding) noexcept
		{
			if (!binding.view) return;
			if (output.shader_resource_count >= output.shader_resources.size())
			{
				++output.shader_resource_dropped;
				++output.shader_resource_dropped_by_stage[stage_index(stage)];
				return;
			}
			auto& entry = output.shader_resources[output.shader_resource_count++];
			entry.stage = static_cast<std::uint8_t>(stage);
			entry.slot = slot;
			entry.binding = binding;
		}

		[[nodiscard]] sampler_descriptor capture_sampler_descriptor(
			ID3D11SamplerState* const sampler) noexcept
		{
			sampler_descriptor output{};
			if (sampler == nullptr) return output;
			D3D11_SAMPLER_DESC description{};
			sampler->GetDesc(&description);
			output.captured = true;
			output.filter = static_cast<std::uint32_t>(description.Filter);
			output.address_u = static_cast<std::uint32_t>(description.AddressU);
			output.address_v = static_cast<std::uint32_t>(description.AddressV);
			output.address_w = static_cast<std::uint32_t>(description.AddressW);
			std::memcpy(&output.mip_lod_bias_bits, &description.MipLODBias,
				sizeof(output.mip_lod_bias_bits));
			output.maximum_anisotropy = description.MaxAnisotropy;
			output.comparison_function = static_cast<std::uint32_t>(
				description.ComparisonFunc);
			for (std::size_t index{}; index < output.border_color_bits.size(); ++index)
			{
				std::memcpy(&output.border_color_bits[index],
					&description.BorderColor[index], sizeof(std::uint32_t));
			}
			std::memcpy(&output.minimum_lod_bits, &description.MinLOD,
				sizeof(output.minimum_lod_bits));
			std::memcpy(&output.maximum_lod_bits, &description.MaxLOD,
				sizeof(output.maximum_lod_bits));
			return output;
		}

		void record_sampler(detailed_binding_snapshot& output,
			const shader_stage stage, const std::uint8_t slot,
			ID3D11SamplerState* const sampler) noexcept
		{
			if (sampler == nullptr || output.sampler_count >= output.samplers.size())
				return;
			auto& entry = output.samplers[output.sampler_count++];
			entry.stage = static_cast<std::uint8_t>(stage);
			entry.slot = slot;
			entry.identity = reinterpret_cast<std::uintptr_t>(sampler);
			entry.descriptor = capture_sampler_descriptor(sampler);
		}

		void observe_execution(ID3D11DeviceContext* const context,
			const engine_stereo_execution::api operation, const std::uintptr_t caller,
			const std::uint32_t output_target_id,
			const std::uintptr_t output_render_target_view,
			const std::uint64_t output_binding_sequence,
			const std::uint8_t argument_count,
			const std::array<std::uint64_t, 6>& arguments) noexcept
		{
			if (operation >= engine_stereo_execution::api::execute_command_list) return;
			observe(context, static_cast<api>(operation), caller, output_target_id,
				output_render_target_view, output_binding_sequence,
				{argument_count, arguments});
		}

		void record_overflow(std::uint64_t& domain) noexcept
		{
			++domain;
			++census.overflows;
		}

		void detach_observers() noexcept
		{
			engine_stereo_output_merger::set_clear_observers(
				engine_stereo_output_merger::clear_observer_channel::gpu_census,
				nullptr, nullptr);
			engine_stereo_execution::set_invocation_observer(
				engine_stereo_execution::invocation_observer_channel::gpu_census,
				nullptr);
			engine_stereo_resource_ops::set_observer(
				engine_stereo_resource_ops::observer_channel::gpu_census, nullptr);
			engine_stereo_constant_buffer_probe::set_history_tracking_enabled(false);
		}

		void fail_lifecycle_locked() noexcept
		{
			if (census.pair_id != 0)
			{
				(void)engine_stereo_constant_buffer_probe::abort_pair(census.pair_id);
				engine_stereo_constant_buffer_probe::get_report(
					census.constant_buffer_probe);
			}
			census.resource_operation_hooks = engine_stereo_resource_ops::get_status();
			census.current_state = state::failed;
			active_eye = 2;
			census_context = nullptr;
			observation_thread_id.store(0, std::memory_order_release);
			detach_observers();
		}

		template <typename T, std::size_t Size>
		void add_identity(std::array<T, Size>& entries, std::size_t& count,
			const std::uintptr_t identity) noexcept
		{
			if (!identity) return;
			for (std::size_t i{}; i < count; ++i) if (entries[i].identity == identity)
			{
				++entries[i].calls; return;
			}
			if (count == Size) { record_overflow(census.identity_overflows); return; }
			entries[count++] = {identity, 1};
		}

		void add_binding(eye_report& eye, const std::uint8_t stage,
			const std::uint8_t slot, const std::uintptr_t identity) noexcept
		{
			if (!identity) return;
			for (std::size_t i{}; i < eye.constant_buffer_count; ++i)
			{
				auto& value = eye.constant_buffers[i];
				if (value.stage == stage && value.slot == slot && value.identity == identity)
				{ ++value.calls; return; }
			}
			if (eye.constant_buffer_count == eye.constant_buffers.size())
			{ record_overflow(census.binding_overflows); return; }
			eye.constant_buffers[eye.constant_buffer_count++] = {stage, slot, identity, 1};
		}

		resource_access* find_resource(const std::uintptr_t identity,
			const bool create) noexcept
		{
			if (!identity) return nullptr;
			static_assert((maximum_identities & (maximum_identities - 1)) == 0);
			auto index = (identity >> 4) & (maximum_identities - 1);
			for (std::size_t probe{}; probe < maximum_identities; ++probe)
			{
				auto& entry = census.resources[index];
				if (entry.identity == identity) return &entry;
				if (!entry.identity)
				{
					if (!create) return nullptr;
					entry.identity = identity; ++census.resource_count; return &entry;
				}
				index = (index + 1) & (maximum_identities - 1);
			}
			if (create) record_overflow(census.resource_overflows);
			return nullptr;
		}

		void capture_resource_descriptor(ID3D11Resource* const resource,
			resource_descriptor& output) noexcept
		{
			if (output.captured) return;
			output.captured = true;
			D3D11_RESOURCE_DIMENSION dimension{};
			resource->GetType(&dimension);
			++census.query_calls;
			output.dimension = static_cast<std::uint32_t>(dimension);
			switch (dimension)
			{
			case D3D11_RESOURCE_DIMENSION_BUFFER:
			{
				D3D11_BUFFER_DESC description{};
				static_cast<ID3D11Buffer*>(resource)->GetDesc(&description);
				++census.query_calls;
				output.byte_width = description.ByteWidth;
				output.usage = description.Usage;
				output.bind_flags = description.BindFlags;
				output.cpu_access_flags = description.CPUAccessFlags;
				output.misc_flags = description.MiscFlags;
				output.structure_byte_stride = description.StructureByteStride;
				output.valid = description.ByteWidth != 0;
				break;
			}
			case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
			{
				D3D11_TEXTURE1D_DESC description{};
				static_cast<ID3D11Texture1D*>(resource)->GetDesc(&description);
				++census.query_calls;
				output.width = description.Width;
				output.mip_levels = description.MipLevels;
				output.array_size = description.ArraySize;
				output.format = description.Format;
				output.usage = description.Usage;
				output.bind_flags = description.BindFlags;
				output.cpu_access_flags = description.CPUAccessFlags;
				output.misc_flags = description.MiscFlags;
				output.valid = description.Width != 0;
				break;
			}
			case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
			{
				D3D11_TEXTURE2D_DESC description{};
				static_cast<ID3D11Texture2D*>(resource)->GetDesc(&description);
				++census.query_calls;
				output.width = description.Width;
				output.height = description.Height;
				output.mip_levels = description.MipLevels;
				output.array_size = description.ArraySize;
				output.format = description.Format;
				output.sample_count = description.SampleDesc.Count;
				output.sample_quality = description.SampleDesc.Quality;
				output.usage = description.Usage;
				output.bind_flags = description.BindFlags;
				output.cpu_access_flags = description.CPUAccessFlags;
				output.misc_flags = description.MiscFlags;
				output.valid = description.Width != 0 && description.Height != 0;
				break;
			}
			case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
			{
				D3D11_TEXTURE3D_DESC description{};
				static_cast<ID3D11Texture3D*>(resource)->GetDesc(&description);
				++census.query_calls;
				output.width = description.Width;
				output.height = description.Height;
				output.depth = description.Depth;
				output.mip_levels = description.MipLevels;
				output.format = description.Format;
				output.usage = description.Usage;
				output.bind_flags = description.BindFlags;
				output.cpu_access_flags = description.CPUAccessFlags;
				output.misc_flags = description.MiscFlags;
				output.valid = description.Width != 0 && description.Height != 0 &&
					description.Depth != 0;
				break;
			}
			default:
				break;
			}
		}

		void remember_srv_resource_descriptor(ID3D11Resource* const resource) noexcept
		{
			if (resource == nullptr) return;
			const auto identity = reinterpret_cast<std::uintptr_t>(resource);
			static_assert((maximum_srv_resource_descriptors &
				(maximum_srv_resource_descriptors - 1)) == 0);
			auto index = (identity >> 4) & (maximum_srv_resource_descriptors - 1);
			for (std::size_t probe{}; probe < maximum_srv_resource_descriptors; ++probe)
			{
				auto& entry = srv_resource_descriptors[index];
				if (entry.identity == identity) return;
				if (!entry.identity)
				{
					entry.identity = identity;
					capture_resource_descriptor(resource, entry.descriptor);
					return;
				}
				index = (index + 1) & (maximum_srv_resource_descriptors - 1);
			}
			auto& usage = census.ordered.shader_resource_usage;
			++usage.descriptor_cache_overflows;
			usage.classification_complete = false;
		}

		[[nodiscard]] const resource_descriptor* find_srv_resource_descriptor(
			const std::uintptr_t identity) noexcept
		{
			if (!identity) return nullptr;
			auto index = (identity >> 4) & (maximum_srv_resource_descriptors - 1);
			for (std::size_t probe{}; probe < maximum_srv_resource_descriptors; ++probe)
			{
				const auto& entry = srv_resource_descriptors[index];
				if (entry.identity == identity) return &entry.descriptor;
				if (!entry.identity) break;
				index = (index + 1) & (maximum_srv_resource_descriptors - 1);
			}
			auto& usage = census.ordered.shader_resource_usage;
			++usage.descriptor_cache_misses;
			usage.classification_complete = false;
			return nullptr;
		}

		void capture_view_descriptor(ID3D11View* const view, const access_site site,
			access_observation& output) noexcept
		{
			++census.query_calls;
			switch (site)
			{
			case access_site::vs_srv:
			case access_site::ps_srv:
			case access_site::gs_srv:
			case access_site::hs_srv:
			case access_site::ds_srv:
			case access_site::cs_srv:
			case access_site::generate_mips_srv:
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC description{};
				static_cast<ID3D11ShaderResourceView*>(view)->GetDesc(&description);
				output.view_format = description.Format;
				output.view_dimension = static_cast<std::uint32_t>(description.ViewDimension);
				break;
			}
			case access_site::om_rtv:
			{
				D3D11_RENDER_TARGET_VIEW_DESC description{};
				static_cast<ID3D11RenderTargetView*>(view)->GetDesc(&description);
				output.view_format = description.Format;
				output.view_dimension = static_cast<std::uint32_t>(description.ViewDimension);
				break;
			}
			case access_site::om_dsv:
			{
				D3D11_DEPTH_STENCIL_VIEW_DESC description{};
				static_cast<ID3D11DepthStencilView*>(view)->GetDesc(&description);
				output.view_format = description.Format;
				output.view_dimension = static_cast<std::uint32_t>(description.ViewDimension);
				break;
			}
			case access_site::om_uav:
			case access_site::cs_uav:
			case access_site::structure_count_source:
			case access_site::clear_uav:
			{
				D3D11_UNORDERED_ACCESS_VIEW_DESC description{};
				static_cast<ID3D11UnorderedAccessView*>(view)->GetDesc(&description);
				output.view_format = description.Format;
				output.view_dimension = static_cast<std::uint32_t>(description.ViewDimension);
				break;
			}
			default:
				break;
			}
		}

		void capture_access(ID3D11View* const view, const access_site site,
			const std::uint8_t slot, const program_identity& program,
			const api operation, const std::uintptr_t caller,
			const std::uint32_t output_target_id,
			const std::uintptr_t output_render_target_view,
			const std::uint64_t output_binding_sequence,
			access_observation& output) noexcept
		{
			if (output.call) return;
			output.call = call_sequence;
			output.output_binding_sequence = output_binding_sequence;
			output.site = site;
			output.operation = operation;
			output.slot = slot;
			output.view_identity = reinterpret_cast<std::uintptr_t>(view);
			output.caller = caller;
			output.output_target_id = output_target_id;
			output.output_render_target_view = output_render_target_view;
			if ((site == access_site::om_rtv || site == access_site::clear_rtv) &&
				slot == 0 &&
				output.view_identity == output_render_target_view)
			{
				output.resource_target_id = output_target_id;
			}
			output.program = program;
			capture_view_descriptor(view, site, output);
		}

		void capture_target_entry(resource_access& entry,
			const std::uint32_t target_id) noexcept
		{
			if (entry.target_entry_captured ||
				target_id >= native_render_contract::target_registry_capacity)
			{
				return;
			}
			native_render_contract::target_registry_entry before{};
			native_render_contract::target_registry_entry after{};
			if (!engine_backend_probe::read_target_registry_entry(target_id, before) ||
				!engine_backend_probe::read_target_registry_entry(target_id, after))
			{
				return;
			}
			entry.target_entry_id = target_id;
			entry.target_entry_before = before;
			entry.target_entry_after = after;
			entry.target_entry_stable = before == after;
			entry.target_entry_captured = true;
		}

		void capture_resource_access(const access_site site,
			const std::uint32_t subresource,
			const api operation, const std::uintptr_t caller,
			const engine_stereo_output_merger::binding_snapshot& binding,
			access_observation& output) noexcept
		{
			if (output.call) return;
			output.call = call_sequence;
			output.output_binding_sequence = binding.sequence;
			output.site = site;
			output.operation = operation;
			output.slot = static_cast<std::uint8_t>((std::min)(subresource, 0xFFu));
			output.caller = caller;
			output.output_target_id = binding.target_id;
			output.output_render_target_view = binding.render_target_0;
		}

		void note_resource(ID3D11Resource* const resource, const bool write,
			const access_site site, const std::uint32_t subresource,
			const api operation, const std::uintptr_t caller,
			const engine_stereo_output_merger::binding_snapshot& binding) noexcept
		{
			if (resource == nullptr) return;
			auto* entry = find_resource(reinterpret_cast<std::uintptr_t>(resource), false);
			if (entry == nullptr && write && active_eye == 0)
			{
				D3D11_RESOURCE_DIMENSION dimension{};
				resource->GetType(&dimension);
				++census.query_calls;
				if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return;
				D3D11_TEXTURE2D_DESC description{};
				static_cast<ID3D11Texture2D*>(resource)->GetDesc(&description);
				++census.query_calls;
				constexpr UINT output_bindings = D3D11_BIND_RENDER_TARGET |
					D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS;
				if ((description.BindFlags & output_bindings) == 0) return;
				entry = find_resource(reinterpret_cast<std::uintptr_t>(resource), true);
			}
			if (entry == nullptr) return;
			capture_resource_descriptor(resource, entry->descriptor);
			const auto bit = static_cast<std::uint8_t>(1u << active_eye);
			if (write)
			{
				entry->write_mask |= bit;
				capture_resource_access(site, subresource, operation, caller,
					binding, active_eye == 0 ? entry->first_left_write :
					entry->first_right_write);
			}
			else
			{
				entry->read_mask |= bit;
				if (active_eye == 1)
					capture_resource_access(site, subresource, operation, caller,
						binding, entry->first_right_read);
				if (active_eye == 1 && (entry->write_mask & 1u) &&
					!(entry->write_mask & 2u) &&
					!entry->right_first_read_before_write_candidate)
				{
					entry->right_first_read_before_write_candidate = true;
					++census.hazard_candidates;
				}
			}
		}

		void note_view(ID3D11View* const view, const bool write,
			const access_site site, const std::uint8_t slot,
			const program_identity& program, const api operation,
			const std::uintptr_t caller, const std::uint32_t output_target_id,
			const std::uintptr_t output_render_target_view,
			const std::uint64_t output_binding_sequence) noexcept
		{
			if (!view) return;
			ID3D11Resource* resource{};
			++census.query_calls;
			view->GetResource(&resource);
			if (!resource) { ++census.query_failures; return; }
			// Only left writes populate the table. Right reads/writes merely probe that
			// bounded set, so ordinary material textures never consume census capacity.
			auto* const entry = find_resource(reinterpret_cast<std::uintptr_t>(resource),
				write && active_eye == 0);
			if (!entry) { resource->Release(); return; }
			capture_resource_descriptor(resource, entry->descriptor);
			resource->Release();
			const auto bit = static_cast<std::uint8_t>(1u << active_eye);
			if (write)
			{
				entry->write_mask |= bit;
				if (active_eye == 0)
				{
					capture_access(view, site, slot, program, operation, caller,
						output_target_id, output_render_target_view,
						output_binding_sequence, entry->first_left_write);
					if ((site == access_site::om_rtv ||
						site == access_site::clear_rtv) && slot == 0 &&
						reinterpret_cast<std::uintptr_t>(view) ==
							output_render_target_view)
					{
						capture_target_entry(*entry, output_target_id);
					}
				}
				else
					capture_access(view, site, slot, program, operation, caller,
						output_target_id, output_render_target_view,
						output_binding_sequence, entry->first_right_write);
			}
			else
			{
				entry->read_mask |= bit;
				if (active_eye == 1)
					capture_access(view, site, slot, program, operation, caller,
						output_target_id, output_render_target_view,
						output_binding_sequence, entry->first_right_read);
				if (active_eye == 1 && (entry->write_mask & 1u) &&
					!(entry->write_mask & 2u) && !entry->right_first_read_before_write_candidate)
				{
					entry->right_first_read_before_write_candidate = true;
					++census.hazard_candidates;
				}
			}
		}

		api census_api(const engine_stereo_resource_ops::api value) noexcept
		{
			switch (value)
			{
			case engine_stereo_resource_ops::api::copy_subresource_region:
				return api::copy_subresource_region;
			case engine_stereo_resource_ops::api::copy_resource:
				return api::copy_resource;
			case engine_stereo_resource_ops::api::update_subresource:
				return api::update_subresource;
			case engine_stereo_resource_ops::api::copy_structure_count:
				return api::copy_structure_count;
			case engine_stereo_resource_ops::api::clear_uav_uint:
				return api::clear_uav_uint;
			case engine_stereo_resource_ops::api::clear_uav_float:
				return api::clear_uav_float;
			case engine_stereo_resource_ops::api::generate_mips:
				return api::generate_mips;
			case engine_stereo_resource_ops::api::resolve_subresource:
				return api::resolve_subresource;
			default: return api::copy_resource;
			}
		}

		void observe_resource_operation(
			const engine_stereo_resource_ops::event& value) noexcept
		{
			const auto expected_thread = observation_thread_id.load(
				std::memory_order_acquire);
			if (expected_thread == 0) return;
			if (GetCurrentThreadId() != expected_thread)
			{
				foreign_thread_observations.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			std::lock_guard lock(census_mutex);
			if (census.current_state != state::eye_active || active_eye >= 2) return;
			if (value.context != census_context)
			{
				++census.foreign_context_observations;
				return;
			}
			const auto operation_index = static_cast<std::size_t>(value.operation);
			if (operation_index >= engine_stereo_resource_ops::api_count) return;
			++call_sequence;
			++census.eyes[active_eye].resource_operations[operation_index];
			const auto operation = census_api(value.operation);
			const auto binding = engine_stereo_output_merger::get_current_binding(
				value.context);
			const auto note_bound_view = [&](const bool write, const access_site site)
			{
				note_view(value.view, write, site, 0, {}, operation, value.caller,
					binding.target_id, binding.render_target_0, binding.sequence);
			};
			switch (value.operation)
			{
			case engine_stereo_resource_ops::api::copy_subresource_region:
			case engine_stereo_resource_ops::api::copy_resource:
				note_resource(value.source, false, access_site::copy_source,
					value.source_subresource, operation, value.caller, binding);
				note_resource(value.destination, true, access_site::copy_destination,
					value.destination_subresource, operation, value.caller, binding);
				break;
			case engine_stereo_resource_ops::api::update_subresource:
				note_resource(value.destination, true, access_site::update_destination,
					value.destination_subresource, operation, value.caller, binding);
				break;
			case engine_stereo_resource_ops::api::copy_structure_count:
				note_bound_view(false, access_site::structure_count_source);
				note_resource(value.destination, true, access_site::copy_destination,
					value.destination_subresource, operation, value.caller, binding);
				break;
			case engine_stereo_resource_ops::api::clear_uav_uint:
			case engine_stereo_resource_ops::api::clear_uav_float:
				note_bound_view(true, access_site::clear_uav);
				break;
			case engine_stereo_resource_ops::api::generate_mips:
				note_bound_view(false, access_site::generate_mips_srv);
				note_bound_view(true, access_site::generate_mips_srv);
				break;
			case engine_stereo_resource_ops::api::resolve_subresource:
				note_resource(value.source, false, access_site::resolve_source,
					value.source_subresource, operation, value.caller, binding);
				note_resource(value.destination, true, access_site::resolve_destination,
					value.destination_subresource, operation, value.caller, binding);
				break;
			default:
				break;
			}
		}

		template <typename T, std::size_t Size>
		void release_all(std::array<T*, Size>& values) noexcept
		{
			for (auto*& value : values) { if (value) value->Release(); value = nullptr; }
		}

		std::uintptr_t resource_identity(ID3D11View* const view) noexcept
		{
			if (!view) return 0;
			ID3D11Resource* resource{};
			view->GetResource(&resource);
			++census.query_calls;
			const auto output = reinterpret_cast<std::uintptr_t>(resource);
			if (resource) resource->Release();
			else ++census.query_failures;
			return output;
		}

		srv_binding_identity capture_srv_binding(
			ID3D11ShaderResourceView* const view) noexcept
		{
			srv_binding_identity output{};
			if (!view) return output;
			output.view = reinterpret_cast<std::uintptr_t>(view);
			ID3D11Resource* resource{};
			view->GetResource(&resource);
			++census.query_calls;
			output.resource = reinterpret_cast<std::uintptr_t>(resource);
			if (resource)
			{
				remember_srv_resource_descriptor(resource);
				resource->Release();
			}
			else ++census.query_failures;

			D3D11_SHADER_RESOURCE_VIEW_DESC description{};
			view->GetDesc(&description);
			++census.query_calls;
			auto& range = output.range;
			range.format = static_cast<std::uint32_t>(description.Format);
			range.dimension = static_cast<std::uint32_t>(description.ViewDimension);
			switch (description.ViewDimension)
			{
			case D3D11_SRV_DIMENSION_BUFFER:
				range.first_element = description.Buffer.FirstElement;
				range.element_count = description.Buffer.NumElements;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE1D:
				range.most_detailed_mip = description.Texture1D.MostDetailedMip;
				range.mip_levels = description.Texture1D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
				range.most_detailed_mip = description.Texture1DArray.MostDetailedMip;
				range.mip_levels = description.Texture1DArray.MipLevels;
				range.first_array_slice = description.Texture1DArray.FirstArraySlice;
				range.array_size = description.Texture1DArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2D:
				range.most_detailed_mip = description.Texture2D.MostDetailedMip;
				range.mip_levels = description.Texture2D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
				range.most_detailed_mip = description.Texture2DArray.MostDetailedMip;
				range.mip_levels = description.Texture2DArray.MipLevels;
				range.first_array_slice = description.Texture2DArray.FirstArraySlice;
				range.array_size = description.Texture2DArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
				range.first_array_slice = description.Texture2DMSArray.FirstArraySlice;
				range.array_size = description.Texture2DMSArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE3D:
				range.most_detailed_mip = description.Texture3D.MostDetailedMip;
				range.mip_levels = description.Texture3D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURECUBE:
				range.most_detailed_mip = description.TextureCube.MostDetailedMip;
				range.mip_levels = description.TextureCube.MipLevels;
				range.array_size = 6;
				break;
			case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
				range.most_detailed_mip =
					description.TextureCubeArray.MostDetailedMip;
				range.mip_levels = description.TextureCubeArray.MipLevels;
				range.first_array_slice =
					description.TextureCubeArray.First2DArrayFace;
				range.array_size = description.TextureCubeArray.NumCubes * 6;
				break;
			case D3D11_SRV_DIMENSION_BUFFEREX:
				range.first_element = description.BufferEx.FirstElement;
				range.element_count = description.BufferEx.NumElements;
				break;
			default:
				break;
			}
			return output;
		}

		pipeline_snapshot capture_pipeline(ID3D11DeviceContext* const context,
			eye_report& eye) noexcept
		{
			pipeline_snapshot output{};
			ID3D11RenderTargetView* render_target{};
			ID3D11DepthStencilView* depth_stencil{};
			ID3D11DepthStencilState* depth_state{};
			ID3D11BlendState* blend_state{};
			ID3D11RasterizerState* rasterizer_state{};
			ID3D11InputLayout* input_layout{};
			ID3D11Buffer* index_buffer{};
			std::array<ID3D11Buffer*, dynamic_fx_vertex_binding_count> vertex_buffers{};
			std::array<UINT, dynamic_fx_vertex_binding_count> vertex_strides{};
			std::array<UINT, dynamic_fx_vertex_binding_count> vertex_offsets{};
			DXGI_FORMAT index_format{};
			UINT index_offset{};
			D3D11_PRIMITIVE_TOPOLOGY topology{};
			FLOAT blend_factor[4]{};
			std::array<D3D11_VIEWPORT,
				D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
			std::array<D3D11_RECT,
				D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
			UINT viewport_count = static_cast<UINT>(viewports.size());
			UINT scissor_count = static_cast<UINT>(scissors.size());
			context->OMGetRenderTargets(1, &render_target, &depth_stencil);
			context->OMGetDepthStencilState(&depth_state, &output.stencil_reference);
			context->OMGetBlendState(&blend_state, blend_factor, &output.sample_mask);
			context->RSGetState(&rasterizer_state);
			context->RSGetViewports(&viewport_count, viewports.data());
			context->RSGetScissorRects(&scissor_count, scissors.data());
			context->IAGetInputLayout(&input_layout);
			context->IAGetPrimitiveTopology(&topology);
			context->IAGetIndexBuffer(&index_buffer, &index_format, &index_offset);
			context->IAGetVertexBuffers(0,
				static_cast<UINT>(vertex_buffers.size()), vertex_buffers.data(),
				vertex_strides.data(), vertex_offsets.data());
			census.query_calls += 10;
			output.render_target_view = reinterpret_cast<std::uintptr_t>(render_target);
			output.render_target_resource = resource_identity(render_target);
			output.depth_stencil_view = reinterpret_cast<std::uintptr_t>(depth_stencil);
			output.depth_stencil_resource = resource_identity(depth_stencil);
			output.depth_stencil_state = reinterpret_cast<std::uintptr_t>(depth_state);
			output.blend_state = reinterpret_cast<std::uintptr_t>(blend_state);
			output.rasterizer_state = reinterpret_cast<std::uintptr_t>(rasterizer_state);
			output.input_layout = reinterpret_cast<std::uintptr_t>(input_layout);
			output.index_buffer = reinterpret_cast<std::uintptr_t>(index_buffer);
			static_assert(sizeof(output.blend_factor_bits) == sizeof(blend_factor));
			std::memcpy(output.blend_factor_bits.data(), blend_factor,
				sizeof(blend_factor));
			output.viewport_count = viewport_count;
			output.scissor_count = scissor_count;
			output.viewport_hash = hash_bytes(
				reinterpret_cast<const char*>(viewports.data()),
				static_cast<std::size_t>(viewport_count) * sizeof(viewports[0]));
			output.scissor_hash = hash_bytes(
				reinterpret_cast<const char*>(scissors.data()),
				static_cast<std::size_t>(scissor_count) * sizeof(scissors[0]));
			output.primitive_topology = static_cast<std::uint32_t>(topology);
			output.index_format = static_cast<std::uint32_t>(index_format);
			output.index_offset = index_offset;
			for (std::size_t slot{}; slot < vertex_buffers.size(); ++slot)
			{
				output.vertex_buffers[slot].buffer = reinterpret_cast<std::uintptr_t>(
					vertex_buffers[slot]);
				output.vertex_buffers[slot].stride = vertex_strides[slot];
				output.vertex_buffers[slot].offset = vertex_offsets[slot];
			}
			add_identity(eye.depth_states, eye.depth_state_count,
				output.depth_stencil_state);
			add_identity(eye.blend_states, eye.blend_state_count, output.blend_state);
			add_identity(eye.rasterizer_states, eye.rasterizer_state_count,
				output.rasterizer_state);
			if (depth_state)
			{
				D3D11_DEPTH_STENCIL_DESC description{};
				depth_state->GetDesc(&description);
				++census.query_calls;
				output.depth_reads = description.DepthEnable != FALSE;
				output.depth_writes = output.depth_reads &&
					description.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO;
				output.stencil_reads = description.StencilEnable != FALSE &&
					description.StencilReadMask != 0;
				output.stencil_writes = description.StencilEnable != FALSE &&
					description.StencilWriteMask != 0;
			}
			else
			{
				// D3D11's null depth-stencil state is the documented default:
				// depth test enabled, depth writes enabled, stencil disabled.
				output.depth_reads = true;
				output.depth_writes = true;
			}
			if (blend_state)
			{
				D3D11_BLEND_DESC description{};
				blend_state->GetDesc(&description);
				++census.query_calls;
				for (std::size_t slot{};
					slot < output.render_target_write_masks.size(); ++slot)
				{
					const auto source = description.IndependentBlendEnable ? slot : 0;
					output.render_target_write_masks[slot] =
						description.RenderTarget[source].RenderTargetWriteMask;
				}
			}
			else
			{
				output.render_target_write_masks.fill(
					static_cast<std::uint8_t>(D3D11_COLOR_WRITE_ENABLE_ALL));
			}
			if (render_target) render_target->Release();
			if (depth_stencil) depth_stencil->Release();
			if (depth_state) depth_state->Release();
			if (blend_state) blend_state->Release();
			if (rasterizer_state) rasterizer_state->Release();
			if (input_layout) input_layout->Release();
			if (index_buffer) index_buffer->Release();
			for (auto* const vertex_buffer : vertex_buffers)
			{
				if (vertex_buffer) vertex_buffer->Release();
			}
			return output;
		}

		[[nodiscard]] bool srv_binding_equal(const srv_binding_identity& left,
			const srv_binding_identity& right) noexcept
		{
			return left.view == right.view && left.resource == right.resource &&
				left.range.format == right.range.format &&
				left.range.dimension == right.range.dimension &&
				left.range.most_detailed_mip == right.range.most_detailed_mip &&
				left.range.mip_levels == right.range.mip_levels &&
				left.range.first_array_slice == right.range.first_array_slice &&
				left.range.array_size == right.range.array_size &&
				left.range.first_element == right.range.first_element &&
				left.range.element_count == right.range.element_count;
		}

		[[nodiscard]] std::uintptr_t shader_for_stage(
			const program_identity& program, const shader_stage stage) noexcept
		{
			switch (stage)
			{
			case shader_stage::vs: return program.vs;
			case shader_stage::ps: return program.ps;
			case shader_stage::cs: return program.cs;
			case shader_stage::gs: return program.gs;
			case shader_stage::hs: return program.hs;
			case shader_stage::ds: return program.ds;
			default: return 0;
			}
		}

		using shader_binding_classification =
			shader_resource_mismatch_sample::shader_binding_classification;

		[[nodiscard]] shader_binding_classification classify_shader_binding(
			const program_identity& program, const shader_stage stage,
			const std::uint8_t slot) noexcept
		{
			shader_binding_classification output{};
			output.shader = shader_for_stage(program, stage);
			if (!output.shader || slot >= scanned_srv_slots) return output;
			const auto* const entry = find_reflected_shader(output.shader, stage, false);
			if (entry == nullptr)
			{
				auto& usage = census.ordered.shader_resource_usage;
				++usage.shader_cache_misses;
				usage.classification_complete = false;
				return output;
			}
			output.shader_debug_name_hash = entry->debug_name_hash;
			output.shader_debug_name = entry->debug_name;
			if (!entry->resolved) return output;
			const auto& binding = entry->bindings[slot];
			if (!binding.declared)
			{
				output.usage = srv_shader_usage::unused;
				return output;
			}
			output.usage = srv_shader_usage::used;
			output.binding_name_hash = binding.name_hash;
			output.input_type = binding.input_type;
			output.return_type = binding.return_type;
			output.dimension = binding.dimension;
			output.bind_point = binding.bind_point;
			output.bind_count = binding.bind_count;
			output.binding_name = binding.name;
			return output;
		}

		[[nodiscard]] bool resource_descriptor_equal(
			const resource_descriptor& left,
			const resource_descriptor& right) noexcept
		{
			return left.captured == right.captured && left.valid == right.valid &&
				left.dimension == right.dimension && left.width == right.width &&
				left.height == right.height && left.depth == right.depth &&
				left.mip_levels == right.mip_levels &&
				left.array_size == right.array_size && left.format == right.format &&
				left.sample_count == right.sample_count &&
				left.sample_quality == right.sample_quality &&
				left.usage == right.usage && left.bind_flags == right.bind_flags &&
				left.cpu_access_flags == right.cpu_access_flags &&
				left.misc_flags == right.misc_flags &&
				left.byte_width == right.byte_width &&
				left.structure_byte_stride == right.structure_byte_stride;
		}

		void increment_usage_counter(const srv_shader_usage value,
			std::uint64_t& used, std::uint64_t& unused,
			std::uint64_t& unknown) noexcept
		{
			switch (value)
			{
			case srv_shader_usage::used: ++used; break;
			case srv_shader_usage::unused: ++unused; break;
			default: ++unknown; break;
			}
		}

		[[nodiscard]] constexpr char ascii_lower(const char value) noexcept
		{
			return value >= 'A' && value <= 'Z' ?
				static_cast<char>(value + ('a' - 'A')) : value;
		}

		[[nodiscard]] bool contains_case_insensitive(const char* const text,
			const char* const token) noexcept
		{
			if (text == nullptr || token == nullptr || !*token) return false;
			for (auto* start = text; *start; ++start)
			{
				auto* candidate = start;
				auto* expected = token;
				while (*candidate && *expected &&
					ascii_lower(*candidate) == ascii_lower(*expected))
				{
					++candidate;
					++expected;
				}
				if (!*expected) return true;
			}
			return false;
		}

		[[nodiscard]] bool has_dynamic_effect_semantic(
			const shader_binding_classification& shader) noexcept
		{
			constexpr std::array tokens{"breakable", "glass", "decal", "muzzle",
				"particle", "spark", "flare", "effect", "fx", "lit2d"};
			for (const auto* const token : tokens)
			{
				if (contains_case_insensitive(shader.shader_debug_name.data(), token) ||
					contains_case_insensitive(shader.binding_name.data(), token))
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] std::uint8_t srv_sample_priority(
			const std::uint8_t stage, const std::uint8_t slot,
			const shader_binding_classification& left,
			const shader_binding_classification& right) noexcept
		{
			if (has_dynamic_effect_semantic(left) ||
				has_dynamic_effect_semantic(right))
				return 3;
			if (stage == static_cast<std::uint8_t>(shader_stage::ps) &&
				(slot == 6 || slot == 7)) return 2;
			if (stage == static_cast<std::uint8_t>(shader_stage::ps)) return 1;
			return 0;
		}

		void record_srv_usage_difference(const std::uint64_t ordinal,
			const observation_signature& left, const observation_signature& right,
			const std::uint8_t stage_value, const std::uint8_t slot,
			const srv_binding_identity& left_binding,
			const srv_binding_identity& right_binding) noexcept
		{
			if (stage_value >= shader_stage_count || slot >= scanned_srv_slots) return;
			const auto stage = static_cast<shader_stage>(stage_value);
			const auto left_shader = classify_shader_binding(left.program, stage, slot);
			const auto right_shader = classify_shader_binding(right.program, stage, slot);
			auto& usage = census.ordered.shader_resource_usage;
			auto& slot_usage = usage.slots[stage_value][slot];
			++usage.slot_mismatches;
			++slot_usage.mismatches;
			increment_usage_counter(left_shader.usage, slot_usage.left_used,
				slot_usage.left_unused, slot_usage.left_unknown);
			increment_usage_counter(right_shader.usage, slot_usage.right_used,
				slot_usage.right_unused, slot_usage.right_unknown);
			if (left_shader.usage == srv_shader_usage::unknown ||
				right_shader.usage == srv_shader_usage::unknown)
			{
				++usage.unknown;
				++slot_usage.unknown;
			}
			else if (left_shader.usage == srv_shader_usage::used ||
				right_shader.usage == srv_shader_usage::used)
			{
				++usage.used;
				++slot_usage.used;
			}
			else
			{
				++usage.unused;
				++slot_usage.unused;
			}

			const auto* const left_resource = find_srv_resource_descriptor(
				left_binding.resource);
			const auto* const right_resource = find_srv_resource_descriptor(
				right_binding.resource);
			if (left_resource && right_resource && left_resource->captured &&
				left_resource->valid && right_resource->captured &&
				right_resource->valid)
			{
				if (resource_descriptor_equal(*left_resource, *right_resource))
				{
					++usage.descriptor_same;
					++slot_usage.descriptor_same;
				}
				else
				{
					++usage.descriptor_different;
					++slot_usage.descriptor_different;
				}
			}
			else
			{
				++usage.descriptor_unknown;
				++slot_usage.descriptor_unknown;
			}

			auto& comparison = census.ordered;
			std::size_t sample_index{};
			if (comparison.shader_resource_sample_count <
				comparison.shader_resource_samples.size())
			{
				sample_index = comparison.shader_resource_sample_count++;
			}
			else
			{
				++usage.sample_overflows;
				const auto priority = srv_sample_priority(stage_value, slot,
					left_shader, right_shader);
				auto lowest_priority = priority;
				sample_index = comparison.shader_resource_samples.size();
				for (std::size_t index{};
					index < comparison.shader_resource_samples.size(); ++index)
				{
					const auto& candidate = comparison.shader_resource_samples[index];
					const auto candidate_priority = srv_sample_priority(candidate.stage,
						candidate.slot, candidate.left_shader,
						candidate.right_shader);
					if (candidate_priority >= lowest_priority) continue;
					lowest_priority = candidate_priority;
					sample_index = index;
				}
				if (sample_index == comparison.shader_resource_samples.size()) return;
			}

			auto& sample = comparison.shader_resource_samples[sample_index];
			sample = {};
			sample.ordinal = ordinal;
			sample.left = make_context(left);
			sample.right = make_context(right);
			sample.left_arguments = left.arguments;
			sample.right_arguments = right.arguments;
			sample.stage = stage_value;
			sample.slot = slot;
			sample.left_binding = left_binding;
			sample.right_binding = right_binding;
			sample.left_shader = left_shader;
			sample.right_shader = right_shader;
			if (left_resource) sample.left_resource = *left_resource;
			if (right_resource) sample.right_resource = *right_resource;
		}

		[[nodiscard]] const constant_buffer_slot_identity* find_constant_buffer(
			const detailed_binding_snapshot& value, const std::uint8_t stage,
			const std::uint8_t slot) noexcept
		{
			for (std::uint16_t index{}; index < value.constant_buffer_count; ++index)
			{
				const auto& candidate = value.constant_buffers[index];
				if (candidate.stage == stage && candidate.slot == slot) return &candidate;
			}
			return nullptr;
		}

		[[nodiscard]] const shader_resource_slot_identity* find_shader_resource(
			const detailed_binding_snapshot& value, const std::uint8_t stage,
			const std::uint8_t slot) noexcept
		{
			for (std::uint16_t index{}; index < value.shader_resource_count; ++index)
			{
				const auto& candidate = value.shader_resources[index];
				if (candidate.stage == stage && candidate.slot == slot) return &candidate;
			}
			return nullptr;
		}

		[[nodiscard]] constexpr std::size_t dynamic_fx_index(
			const dynamic_fx_family family) noexcept
		{
			return static_cast<std::size_t>(family);
		}

		constexpr std::uintptr_t h2_dynamic_fx_global_data_pointer =
			0x150F91188ull;
		constexpr std::size_t h2_dynamic_fx_mesh_bytes = 0x38;
		static_assert(h2_dynamic_fx_mesh_bytes ==
			dynamic_fx_arena_raw_qword_count * sizeof(std::uint64_t));
		static_assert(dynamic_fx_arena_mesh_count ==
			engine_stereo_dynamic_arena::mesh_count);

		enum class dynamic_fx_arena_output_phase : std::uint8_t
		{
			waiting_begin,
			inside_eye,
			ended,
		};
		std::array<dynamic_fx_arena_output_phase, 2> arena_output_phases{};
		std::array<std::uint64_t, 2> latest_arena_view_copy_sequences{};

		[[nodiscard]] bool checked_add(const std::uintptr_t base,
			const std::uintptr_t offset, std::uintptr_t& output) noexcept
		{
			if (base > (std::numeric_limits<std::uintptr_t>::max)() - offset)
				return false;
			output = base + offset;
			return true;
		}

		[[nodiscard]] bool readable_page_protection(const DWORD protection) noexcept
		{
			if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
			switch (protection & 0xFFu)
			{
			case PAGE_READONLY:
			case PAGE_READWRITE:
			case PAGE_WRITECOPY:
			case PAGE_EXECUTE_READ:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] bool readable_committed_range(const std::uintptr_t address,
			const std::size_t size) noexcept
		{
			const auto maximum = (std::numeric_limits<std::uintptr_t>::max)();
			if (!address || !size || size > maximum || address > maximum - size)
			{
				return false;
			}
			const auto end = address + size;
			auto cursor = address;
			while (cursor < end)
			{
				MEMORY_BASIC_INFORMATION information{};
				if (VirtualQuery(reinterpret_cast<const void*>(cursor), &information,
					sizeof(information)) != sizeof(information) ||
					information.State != MEM_COMMIT ||
					!readable_page_protection(information.Protect))
				{
					return false;
				}
				const auto region_base = reinterpret_cast<std::uintptr_t>(
					information.BaseAddress);
				if (region_base > (std::numeric_limits<std::uintptr_t>::max)() -
					information.RegionSize)
				{
					return false;
				}
				const auto region_end = region_base + information.RegionSize;
				if (region_end <= cursor) return false;
				cursor = (std::min)(end, region_end);
			}
			return true;
		}

		// Keep SEH in a destructor-free leaf. VirtualQuery proves the complete range;
		// the exception guard closes the remaining TOCTOU window without ever writing
		// to H2-owned memory.
		[[nodiscard]] bool copy_readable_bytes(const std::uintptr_t source,
			void* const destination, const std::size_t size) noexcept
		{
			if (!destination || !readable_committed_range(source, size)) return false;
			__try
			{
				std::memcpy(destination, reinterpret_cast<const void*>(source), size);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		void capture_dynamic_fx_arena_meshes(
			dynamic_fx_arena_candidate_snapshot& candidate) noexcept
		{
			if (!candidate.pointer_slot_readable || !candidate.data_identity) return;
			candidate.complete = true;
			for (std::size_t index{}; index < candidate.meshes.size(); ++index)
			{
				auto& mesh = candidate.meshes[index];
				if (!checked_add(candidate.data_identity,
					engine_stereo_dynamic_arena::mesh_offset(index), mesh.address) ||
					!copy_readable_bytes(mesh.address, mesh.raw_qwords.data(),
						h2_dynamic_fx_mesh_bytes))
				{
					candidate.complete = false;
					continue;
				}
				mesh.readable = true;
			}
		}

		void capture_dynamic_fx_arena_pointer(const std::uintptr_t pointer_slot,
			dynamic_fx_arena_candidate_snapshot& candidate) noexcept
		{
			candidate = {};
			candidate.applicable = true;
			candidate.pointer_slot = pointer_slot;
			candidate.pointer_slot_readable = copy_readable_bytes(pointer_slot,
				&candidate.data_identity, sizeof(candidate.data_identity));
		}

		void capture_dynamic_fx_arena_candidate(const std::uintptr_t pointer_slot,
			dynamic_fx_arena_candidate_snapshot& candidate) noexcept
		{
			capture_dynamic_fx_arena_pointer(pointer_slot, candidate);
			capture_dynamic_fx_arena_meshes(candidate);
		}

		[[nodiscard]] std::uint8_t changed_qword_mask(
			const dynamic_fx_arena_mesh_snapshot& from,
			const dynamic_fx_arena_mesh_snapshot& to) noexcept
		{
			std::uint8_t output{};
			for (std::size_t index{}; index < from.raw_qwords.size(); ++index)
			{
				if (from.raw_qwords[index] != to.raw_qwords[index])
					output |= static_cast<std::uint8_t>(1u << index);
			}
			return output;
		}

		void compare_dynamic_fx_arena_candidates(
			const dynamic_fx_arena_candidate_snapshot& from,
			const dynamic_fx_arena_candidate_snapshot& to,
			bool& identity_comparable, bool& identity_equal,
			bool& raw_comparable,
			std::array<std::uint8_t, dynamic_fx_arena_mesh_count>& masks) noexcept
		{
			identity_comparable = from.applicable && to.applicable &&
				from.pointer_slot_readable && to.pointer_slot_readable &&
				from.data_identity != 0 && to.data_identity != 0;
			identity_equal = identity_comparable &&
				from.data_identity == to.data_identity;
			raw_comparable = identity_equal && from.complete && to.complete;
			if (!raw_comparable) return;
			for (std::size_t index{}; index < masks.size(); ++index)
			{
				masks[index] = changed_qword_mask(from.meshes[index], to.meshes[index]);
			}
		}

		[[nodiscard]] const dynamic_fx_arena_snapshot* find_arena_snapshot(
			const std::uint32_t output, const dynamic_fx_arena_phase phase,
			const bool latest) noexcept
		{
			if (output >= census.dynamic_fx.arena.outputs.size()) return nullptr;
			const auto& report = census.dynamic_fx.arena.outputs[output];
			const dynamic_fx_arena_snapshot* match{};
			for (std::size_t index{}; index < report.snapshot_count; ++index)
			{
				const auto& candidate = report.snapshots[index];
				if (!candidate.present || candidate.phase != phase) continue;
				match = &candidate;
				if (!latest) break;
			}
			return match;
		}

		void finalize_dynamic_fx_arena_transition(
			const std::size_t index, const dynamic_fx_arena_transition_kind kind,
			const dynamic_fx_arena_snapshot* const from,
			const dynamic_fx_arena_snapshot* const to) noexcept
		{
			auto& transition = census.dynamic_fx.arena.transitions[index];
			transition = {};
			transition.kind = kind;
			if (!from || !to) return;
			transition.observed = true;
			transition.from_sequence = from->sequence;
			transition.to_sequence = to->sequence;
			compare_dynamic_fx_arena_candidates(from->global, to->global,
				transition.global_identity_comparable,
				transition.global_identity_equal, transition.global_raw_comparable,
				transition.global_changed_qword_masks);
			compare_dynamic_fx_arena_candidates(from->backend, to->backend,
				transition.backend_identity_comparable,
				transition.backend_identity_equal, transition.backend_raw_comparable,
				transition.backend_changed_qword_masks);
		}

		void finalize_dynamic_fx_arena() noexcept
		{
			auto& arena = census.dynamic_fx.arena;
			arena.finalized = true;
			const auto output0_begin = find_arena_snapshot(0,
				dynamic_fx_arena_phase::eye_begin, false);
			const auto output0_view = find_arena_snapshot(0,
				dynamic_fx_arena_phase::backend_view_copy, true);
			const auto output0_end = find_arena_snapshot(0,
				dynamic_fx_arena_phase::eye_end, false);
			const auto output1_begin = find_arena_snapshot(1,
				dynamic_fx_arena_phase::eye_begin, false);
			const auto output1_view = find_arena_snapshot(1,
				dynamic_fx_arena_phase::backend_view_copy, true);
			const auto output1_end = find_arena_snapshot(1,
				dynamic_fx_arena_phase::eye_end, false);
			finalize_dynamic_fx_arena_transition(0,
				dynamic_fx_arena_transition_kind::output0_begin_to_view,
				output0_begin, output0_view);
			finalize_dynamic_fx_arena_transition(1,
				dynamic_fx_arena_transition_kind::output0_view_to_end,
				output0_view, output0_end);
			finalize_dynamic_fx_arena_transition(2,
				dynamic_fx_arena_transition_kind::output0_end_to_output1_begin,
				output0_end, output1_begin);
			finalize_dynamic_fx_arena_transition(3,
				dynamic_fx_arena_transition_kind::output1_begin_to_view,
				output1_begin, output1_view);
			finalize_dynamic_fx_arena_transition(4,
				dynamic_fx_arena_transition_kind::output1_view_to_end,
				output1_view, output1_end);
			finalize_dynamic_fx_arena_transition(5,
				dynamic_fx_arena_transition_kind::output0_view_to_output1_view,
				output0_view, output1_view);
			const auto& cross_output = arena.transitions[5];
			if (cross_output.global_identity_comparable)
			{
				++arena.cross_output_global_identity_comparisons;
				if (!cross_output.global_identity_equal)
					++arena.cross_output_global_identity_mismatches;
			}
			if (cross_output.backend_identity_comparable)
			{
				++arena.cross_output_backend_identity_comparisons;
				if (!cross_output.backend_identity_equal)
					++arena.cross_output_backend_identity_mismatches;
			}

			if (arena.overflows || arena.order_mismatches || arena.foreign_pair ||
				arena.foreign_output || arena.foreign_thread)
				arena.evidence = dynamic_fx_arena_evidence::truncated;
			else if (arena.snapshot_attempts == 0 || !output0_view || !output1_view)
				arena.evidence = dynamic_fx_arena_evidence::not_observed;
			else if (arena.unreadable)
				arena.evidence = dynamic_fx_arena_evidence::unreadable;
			else if (output0_begin && output0_end && output1_begin && output1_end)
				arena.evidence = dynamic_fx_arena_evidence::complete;
			else
				arena.evidence = dynamic_fx_arena_evidence::truncated;
		}

		void observe_dynamic_fx_arena_boundary_locked(const std::uint64_t pair_id,
			const std::uint32_t output, const dynamic_fx_arena_phase phase,
			const std::uintptr_t owner_record, const std::uintptr_t global_pointer_slot,
			void* const backend_state) noexcept
		{
			if (census.current_state != state::eye_active) return;
			auto& arena = census.dynamic_fx.arena;
			if (census.pair_id != pair_id)
			{
				++arena.foreign_pair;
				return;
			}
			if (output >= arena.outputs.size() || active_eye != output)
			{
				++arena.foreign_output;
				return;
			}
			if (GetCurrentThreadId() != census.owner_thread_id)
			{
				++arena.foreign_thread;
				return;
			}

			auto& output_report = arena.outputs[output];
			++arena.snapshot_attempts;
			++output_report.snapshot_attempts;
			const auto expected_begin = arena_output_phases[output] ==
				dynamic_fx_arena_output_phase::waiting_begin;
			const auto expected_inside = arena_output_phases[output] ==
				dynamic_fx_arena_output_phase::inside_eye;
			const auto order_valid =
				(phase == dynamic_fx_arena_phase::eye_begin && expected_begin) ||
				(phase == dynamic_fx_arena_phase::backend_view_copy && expected_inside) ||
				(phase == dynamic_fx_arena_phase::eye_end && expected_inside);
			if (!order_valid)
			{
				++arena.order_mismatches;
				++output_report.order_mismatches;
				return;
			}
			if (phase == dynamic_fx_arena_phase::backend_view_copy)
			{
				++output_report.view_copy_attempts;
				if (output_report.view_copy_stored >=
					maximum_dynamic_fx_arena_view_copies_per_output)
				{
					++arena.overflows;
					++output_report.overflows;
					latest_arena_view_copy_sequences[output] = 0;
					return;
				}
			}
			if (output_report.snapshot_count >= output_report.snapshots.size())
			{
				++arena.overflows;
				++output_report.overflows;
				return;
			}

			auto& snapshot = output_report.snapshots[output_report.snapshot_count++];
			snapshot = {};
			snapshot.present = true;
			snapshot.sequence = ++arena.next_sequence;
			snapshot.output = output;
			snapshot.phase = phase;
			snapshot.owner_record = owner_record;
			snapshot.backend_state = reinterpret_cast<std::uintptr_t>(backend_state);
			capture_dynamic_fx_arena_candidate(global_pointer_slot, snapshot.global);
			if (phase == dynamic_fx_arena_phase::backend_view_copy)
			{
				snapshot.view_copy_ordinal = output_report.view_copy_attempts;
				std::uintptr_t backend_pointer_slot{};
				if (checked_add(snapshot.backend_state,
					engine_stereo_dynamic_arena::backend_data_pointer_offset,
					backend_pointer_slot))
				{
					capture_dynamic_fx_arena_pointer(backend_pointer_slot,
						snapshot.backend);
				}
				else
				{
					snapshot.backend.applicable = true;
				}
				if (snapshot.global.pointer_slot_readable &&
					snapshot.backend.pointer_slot_readable &&
					snapshot.global.data_identity && snapshot.backend.data_identity)
				{
					snapshot.data_identity_comparable = true;
					snapshot.data_identity_equal = snapshot.global.data_identity ==
						snapshot.backend.data_identity;
					++arena.data_identity_comparisons;
					if (!snapshot.data_identity_equal)
						++arena.data_identity_mismatches;
				}
				if (snapshot.data_identity_equal)
				{
					// Same identity: preserve both roles while reading the mutable arena
					// payload exactly once at this nominal boundary.
					snapshot.backend.meshes = snapshot.global.meshes;
					snapshot.backend.complete = snapshot.global.complete;
				}
				else
				{
					capture_dynamic_fx_arena_meshes(snapshot.backend);
				}
				++output_report.view_copy_stored;
				latest_arena_view_copy_sequences[output] = snapshot.sequence;
			}

			// The source-matching backend boundary has its own authoritative arena
			// pointer. H2 leaves the legacy global slot null during owner rendering,
			// so requiring both candidates turns a complete backend sample into a
			// false unreadable result. Eye begin/end still use the global candidate.
			const auto complete = phase == dynamic_fx_arena_phase::backend_view_copy ?
				snapshot.backend.complete : snapshot.global.complete;
			if (complete)
			{
				++arena.snapshot_completions;
				++output_report.snapshot_completions;
			}
			else
			{
				++arena.unreadable;
				++output_report.unreadable;
			}
			if (phase == dynamic_fx_arena_phase::eye_begin)
			{
				arena_output_phases[output] = dynamic_fx_arena_output_phase::inside_eye;
				latest_arena_view_copy_sequences[output] = 0;
			}
			else if (phase == dynamic_fx_arena_phase::eye_end)
			{
				arena_output_phases[output] = dynamic_fx_arena_output_phase::ended;
			}
		}

		void compare_dynamic_fx_bindings(dynamic_fx_family family,
			std::uint64_t family_ordinal, const dynamic_fx_invocation& left,
			const detailed_binding_snapshot& left_detail,
			const dynamic_fx_invocation& right,
			const detailed_binding_snapshot& right_detail) noexcept;

		[[nodiscard]] bool dynamic_fx_semantic_invocation(
			const dynamic_fx_family family, const api operation,
			const invocation_arguments& arguments) noexcept
		{
			return family != dynamic_fx_family::unknown &&
				operation == api::draw_indexed && arguments.count == 3;
		}

		[[nodiscard]] dynamic_fx_invocation make_dynamic_fx_invocation(
			const std::uint64_t output_ordinal, const api operation,
			const std::uintptr_t caller, const invocation_arguments& arguments,
			const program_identity& program, const std::uint32_t output_target_id,
			const pipeline_snapshot& pipeline, const binding_signature& bindings,
			const std::uint64_t family_ordinal,
			const std::uint64_t arena_sequence) noexcept
		{
			dynamic_fx_invocation output{};
			output.output_ordinal = output_ordinal;
			output.family_ordinal = family_ordinal;
			output.arena_view_copy_snapshot_sequence = arena_sequence;
			output.operation = operation;
			output.caller = caller;
			output.arguments = arguments;
			output.program = program;
			output.output_target_id = output_target_id;
			output.input_layout = pipeline.input_layout;
			output.depth_stencil_state = pipeline.depth_stencil_state;
			output.blend_state = pipeline.blend_state;
			output.rasterizer_state = pipeline.rasterizer_state;
			output.stencil_reference = pipeline.stencil_reference;
			output.sample_mask = pipeline.sample_mask;
			output.primitive_topology = pipeline.primitive_topology;
			output.viewport_count = pipeline.viewport_count;
			output.scissor_count = pipeline.scissor_count;
			output.blend_factor_bits = pipeline.blend_factor_bits;
			output.viewport_hash = pipeline.viewport_hash;
			output.scissor_hash = pipeline.scissor_hash;
			output.index_buffer = pipeline.index_buffer;
			output.index_format = pipeline.index_format;
			output.index_offset = pipeline.index_offset;
			output.vertex_buffers = pipeline.vertex_buffers;
			output.bindings = bindings;

			std::uint64_t index_size{};
			if (pipeline.index_format == DXGI_FORMAT_R16_UINT) index_size = 2;
			else if (pipeline.index_format == DXGI_FORMAT_R32_UINT) index_size = 4;
			if (operation == api::draw_indexed && arguments.count >= 2 && index_size)
			{
				const auto index_count = static_cast<std::uint32_t>(arguments.values[0]);
				const auto start_index = static_cast<std::uint32_t>(arguments.values[1]);
				output.index_range.exact = true;
				output.index_range.byte_offset = static_cast<std::uint64_t>(
					pipeline.index_offset) + static_cast<std::uint64_t>(start_index) *
					index_size;
				output.index_range.byte_count = static_cast<std::uint64_t>(index_count) *
					index_size;
				output.index_range.hash_eligible = pipeline.index_buffer != 0 &&
					output.index_range.byte_count != 0;
			}
			return output;
		}

		struct dynamic_fx_record_result
		{
			dynamic_fx_family family{dynamic_fx_family::unknown};
			bool semantic{};
			dynamic_fx_invocation invocation{};
		};

		[[nodiscard]] dynamic_fx_record_result record_dynamic_fx_invocation(
			const std::uint32_t eye_index, const std::uint64_t output_ordinal,
			const api operation, const std::uintptr_t caller,
			const invocation_arguments& arguments, const program_identity& program,
			const std::uint32_t output_target_id, const pipeline_snapshot& pipeline,
			const binding_signature& bindings) noexcept
		{
			dynamic_fx_record_result result{};
			result.family = classify_dynamic_fx_caller(caller);
			result.invocation = make_dynamic_fx_invocation(output_ordinal, operation,
				caller, arguments, program, output_target_id, pipeline, bindings, 0,
				eye_index < latest_arena_view_copy_sequences.size() ?
					latest_arena_view_copy_sequences[eye_index] : 0);
			if (result.family == dynamic_fx_family::unknown || eye_index >= 2)
				return result;
			const auto family_index = dynamic_fx_index(result.family);
			if (family_index >= dynamic_fx_family_count) return result;

			auto& report = census.dynamic_fx;
			auto& eye = report.eyes[eye_index];
			auto& eye_family = eye.families[family_index];
			++report.range_hits;
			++eye.range_hits;
			++eye_family.range_hits;
			// The four consumers issue raw DrawIndexed(IndexCount, StartIndex,
			// BaseVertex). A range hit with any other shape is retained as an explicit
			// invalid invocation and is never interpreted as those three semantics.
			if (!dynamic_fx_semantic_invocation(result.family, operation, arguments))
			{
				++report.invalid_invocations;
				++eye.invalid_invocations;
				++eye_family.invalid_invocations;
				return result;
			}

			result.semantic = true;
			++report.semantic_hits;
			++eye.semantic_hits;
			const auto family_ordinal = ++eye_family.calls;
			result.invocation.family_ordinal = family_ordinal;
			const auto arena_sequence = latest_arena_view_copy_sequences[eye_index];
			if (arena_sequence)
			{
				++report.arena.linked_invocations;
				++report.arena.outputs[eye_index].linked_invocations;
			}
			else
			{
				++report.arena.unlinked_invocations;
				++report.arena.outputs[eye_index].unlinked_invocations;
			}
			if (static_cast<std::uint32_t>(arguments.values[0]) == 0)
				++eye_family.zero_index_counts;
			if (eye_family.observation_count >= eye_family.observations.size())
			{
				++eye_family.observation_overflows;
				++report.observation_overflows;
				return result;
			}

			eye_family.observations[eye_family.observation_count++] = result.invocation;
			return result;
		}

		[[nodiscard]] dynamic_fx_stream_entry make_dynamic_fx_stream_entry(
			const detailed_observation& observation, const std::uint64_t output_ordinal,
			const std::uint64_t family_ordinal,
			const std::uint64_t arena_sequence) noexcept
		{
			dynamic_fx_stream_entry entry{};
			entry.present = true;
			const auto& signature = observation.signature;
			entry.family = classify_dynamic_fx_caller(signature.caller);
			entry.semantic = dynamic_fx_semantic_invocation(entry.family,
				signature.operation, signature.arguments);
			entry.invocation = make_dynamic_fx_invocation(output_ordinal,
				signature.operation, signature.caller, signature.arguments,
				signature.program, signature.output_target_id, signature.pipeline,
				signature.bindings, family_ordinal, arena_sequence);
			return entry;
		}

		[[nodiscard]] dynamic_fx_stream_entry make_dynamic_fx_stream_entry(
			const dynamic_fx_record_result& result) noexcept
		{
			dynamic_fx_stream_entry entry{};
			entry.present = true;
			entry.semantic = result.semantic;
			entry.family = result.family;
			entry.invocation = result.invocation;
			return entry;
		}

		[[nodiscard]] bool get_left_stream_entry(const std::uint64_t output_ordinal,
			dynamic_fx_stream_entry& output) noexcept
		{
			if (!output_ordinal || output_ordinal > census.eyes[0].observations ||
				output_ordinal > left_signatures.size())
			{
				return false;
			}
			const auto index = static_cast<std::size_t>(output_ordinal - 1);
			output = make_dynamic_fx_stream_entry(left_signatures[index],
				output_ordinal, left_dynamic_fx_family_ordinals[index],
				left_dynamic_fx_arena_sequences[index]);
			return true;
		}

		void store_stream_window_entry(dynamic_fx_stream_window& window,
			const std::uint32_t output, const dynamic_fx_stream_entry& entry) noexcept
		{
			if (!window.captured || output >= 2 || !entry.present ||
				!entry.invocation.output_ordinal)
			{
				return;
			}
			const auto ordinal = entry.invocation.output_ordinal;
			if (ordinal + 1 < window.event_output_ordinal ||
				ordinal > window.event_output_ordinal + 1)
			{
				return;
			}
			const auto slot = static_cast<std::size_t>(ordinal + 1 -
				window.event_output_ordinal);
			window.ordinals[slot].output_ordinal = ordinal;
			window.ordinals[slot].outputs[output] = entry;
		}

		void capture_stream_window(dynamic_fx_stream_window& window,
			const std::uint64_t event_output_ordinal,
			const dynamic_fx_stream_entry* const current_right) noexcept
		{
			if (window.captured || !event_output_ordinal) return;
			window.captured = true;
			window.event_output_ordinal = event_output_ordinal;
			for (std::size_t slot{}; slot < window.ordinals.size(); ++slot)
			{
				if (slot == 0 && event_output_ordinal == 1) continue;
				const auto ordinal = event_output_ordinal + slot - 1;
				window.ordinals[slot].output_ordinal = ordinal;
				dynamic_fx_stream_entry left{};
				if (get_left_stream_entry(ordinal, left))
					window.ordinals[slot].outputs[0] = left;
			}
			store_stream_window_entry(window, 1, previous_right_stream_entry);
			if (current_right) store_stream_window_entry(window, 1, *current_right);
		}

		void update_stream_window_right_neighbors(
			const dynamic_fx_stream_entry& right) noexcept
		{
			auto& stream = census.dynamic_fx.stream;
			store_stream_window_entry(stream.first_family_transition, 1, right);
			store_stream_window_entry(stream.first_semantic_mismatch, 1, right);
			store_stream_window_entry(stream.first_missing_output0, 1, right);
			store_stream_window_entry(stream.first_missing_output1, 1, right);
		}

		void observe_dynamic_fx_stream_right(const std::uint64_t output_ordinal,
			const detailed_observation& right_observation,
			const dynamic_fx_record_result& right_result) noexcept
		{
			auto& stream = census.dynamic_fx.stream;
			const auto right = make_dynamic_fx_stream_entry(right_result);
			update_stream_window_right_neighbors(right);

			dynamic_fx_stream_entry left{};
			const auto left_present = get_left_stream_entry(output_ordinal, left);
			if (left_present) ++stream.compared_ordinals;
			const auto left_dynamic = left_present &&
				left.family != dynamic_fx_family::unknown;
			const auto right_dynamic = right.family != dynamic_fx_family::unknown;
			if (left_dynamic || right_dynamic) ++stream.dynamic_ordinals;

			if (left_dynamic && right_dynamic)
			{
				if (left.family != right.family)
				{
					++stream.family_transitions;
					capture_stream_window(stream.first_family_transition,
						output_ordinal, &right);
				}
				else if (left.semantic != right.semantic)
				{
					++stream.semantic_mismatches;
					capture_stream_window(stream.first_semantic_mismatch,
						output_ordinal, &right);
				}
				else if (left.semantic)
				{
					++stream.matched_family_invocations;
					++stream.binding_comparisons;
					const auto family_index = dynamic_fx_index(right.family);
					if (family_index < dynamic_fx_binding_working.size())
						++dynamic_fx_binding_working[family_index].global_ordinal_matches;
					const auto left_index = static_cast<std::size_t>(output_ordinal - 1);
					compare_dynamic_fx_bindings(right.family,
						right.invocation.family_ordinal, left.invocation,
						left_signatures[left_index].bindings, right.invocation,
						right_observation.bindings);
				}
			}
			else if (left_dynamic)
			{
				++stream.missing_output1;
				capture_stream_window(stream.first_missing_output1,
					output_ordinal, &right);
			}
			else if (right_dynamic)
			{
				++stream.missing_output0;
				capture_stream_window(stream.first_missing_output0,
					output_ordinal, &right);
			}
			previous_right_stream_entry = right;
		}

		void finalize_dynamic_fx_stream() noexcept
		{
			auto& stream = census.dynamic_fx.stream;
			stream.finalized = true;
			stream.output0_observations = census.eyes[0].observations;
			stream.output1_observations = census.eyes[1].observations;
			stream.complete = census.observation_overflows == 0 &&
				stream.output0_observations <= left_signatures.size() &&
				stream.output1_observations <= left_signatures.size();

			for (auto ordinal = stream.output1_observations + 1;
				ordinal <= stream.output0_observations; ++ordinal)
			{
				dynamic_fx_stream_entry left{};
				if (!get_left_stream_entry(ordinal, left) ||
					left.family == dynamic_fx_family::unknown)
				{
					continue;
				}
				++stream.dynamic_ordinals;
				++stream.missing_output1;
				capture_stream_window(stream.first_missing_output1, ordinal, nullptr);
			}
		}

		void add_dynamic_fx_mismatch_sample(dynamic_fx_family_comparison& comparison,
			const dynamic_fx_family family, const std::uint64_t family_ordinal,
			const dynamic_fx_invocation* const left,
			const dynamic_fx_invocation* const right) noexcept
		{
			if (comparison.mismatch_sample_count >= comparison.mismatch_samples.size())
			{
				++comparison.mismatch_sample_overflows;
				return;
			}
			auto& sample = comparison.mismatch_samples[
				comparison.mismatch_sample_count++];
			sample.family = family;
			sample.family_ordinal = family_ordinal;
			sample.output0_present = left != nullptr;
			sample.output1_present = right != nullptr;
			if (left) sample.output0 = *left;
			if (right) sample.output1 = *right;
		}

		[[nodiscard]] const constant_buffer_slot_identity*
			find_dynamic_fx_constant_buffer(const detailed_binding_snapshot& bindings,
				const std::uint8_t stage, const std::uint8_t slot) noexcept
		{
			for (std::uint16_t index{}; index < bindings.constant_buffer_count; ++index)
			{
				const auto& value = bindings.constant_buffers[index];
				if (value.stage == stage && value.slot == slot) return &value;
			}
			return nullptr;
		}

		[[nodiscard]] const shader_resource_slot_identity*
			find_dynamic_fx_ps_shader_resource(const detailed_binding_snapshot& bindings,
				const std::uint8_t slot) noexcept
		{
			for (std::uint16_t index{}; index < bindings.shader_resource_count; ++index)
			{
				const auto& value = bindings.shader_resources[index];
				if (value.stage == static_cast<std::uint8_t>(shader_stage::ps) &&
					value.slot == slot)
				{
					return &value;
				}
			}
			return nullptr;
		}

		[[nodiscard]] const sampler_slot_identity* find_dynamic_fx_ps_sampler(
			const detailed_binding_snapshot& bindings, const std::uint8_t slot) noexcept
		{
			for (std::uint16_t index{}; index < bindings.sampler_count; ++index)
			{
				const auto& value = bindings.samplers[index];
				if (value.stage == static_cast<std::uint8_t>(shader_stage::ps) &&
					value.slot == slot)
				{
					return &value;
				}
			}
			return nullptr;
		}

		[[nodiscard]] bool sampler_descriptor_equal(
			const sampler_descriptor& left, const sampler_descriptor& right) noexcept
		{
			return left.captured == right.captured && left.filter == right.filter &&
				left.address_u == right.address_u && left.address_v == right.address_v &&
				left.address_w == right.address_w &&
				left.mip_lod_bias_bits == right.mip_lod_bias_bits &&
				left.maximum_anisotropy == right.maximum_anisotropy &&
				left.comparison_function == right.comparison_function &&
				left.border_color_bits == right.border_color_bits &&
				left.minimum_lod_bits == right.minimum_lod_bits &&
				left.maximum_lod_bits == right.maximum_lod_bits;
		}

		[[nodiscard]] bool dynamic_fx_constant_buffer_content_equal(
			const engine_stereo_constant_buffer_probe::content_snapshot& left,
			const engine_stereo_constant_buffer_probe::content_snapshot& right) noexcept
		{
			return left.byte_width == right.byte_width && left.hash_low == right.hash_low &&
				left.hash_high == right.hash_high;
		}

		void annotate_dynamic_fx_constant_buffer(
			dynamic_fx_constant_buffer_reflection& output,
			const program_identity& program, const shader_stage stage,
			const std::uint8_t slot,
			const engine_stereo_constant_buffer_probe::content_byte_snapshot& left,
			const engine_stereo_constant_buffer_probe::content_byte_snapshot& right) noexcept
		{
			output.shader = shader_for_stage(program, stage);
			if (!output.shader) return;
			const auto* const shader = find_reflected_shader(output.shader, stage, false);
			if (shader == nullptr) return;
			output.attempted = shader->constants_attempted;
			output.available = shader->constants_resolved;
			output.shader_debug_name_hash = shader->debug_name_hash;
			output.shader_debug_name = shader->debug_name;
			if (!shader->constants_resolved ||
				shader->constant_buffer_begin > reflected_constant_buffer_count ||
				shader->constant_buffer_count > reflected_constant_buffer_count -
					shader->constant_buffer_begin)
			{
				return;
			}

			const reflected_constant_buffer* reflected_buffer{};
			for (std::size_t index{}; index < shader->constant_buffer_count; ++index)
			{
				const auto& candidate = reflected_constant_buffers[
					shader->constant_buffer_begin + index];
				if (candidate.bind_point == slot)
				{
					reflected_buffer = &candidate;
					break;
				}
			}
			if (reflected_buffer == nullptr)
			{
				output.complete = true;
				return;
			}

			output.binding_declared = true;
			output.buffer_size = reflected_buffer->size;
			output.buffer_name_hash = reflected_buffer->name_hash;
			output.buffer_name = reflected_buffer->name;
			if (reflected_buffer->variable_begin > reflected_constant_variable_count ||
				reflected_buffer->variable_count > reflected_constant_variable_count -
					reflected_buffer->variable_begin)
			{
				return;
			}

			const auto compared = (std::min)(left.captured_bytes, right.captured_bytes);
			for (std::uint32_t byte{}; byte < compared; ++byte)
			{
				if (left.bytes[byte] == right.bytes[byte]) continue;
				bool mapped{};
				for (std::size_t variable_index{};
					variable_index < reflected_buffer->variable_count; ++variable_index)
				{
					const auto& variable = reflected_constant_variables[
						reflected_buffer->variable_begin + variable_index];
					const auto variable_end = static_cast<std::uint64_t>(
						variable.start_offset) + variable.size;
					if (byte >= variable.start_offset && byte < variable_end)
					{
						mapped = true;
						break;
					}
				}
				if (mapped) ++output.mapped_differing_bytes;
				else ++output.unmapped_differing_bytes;
			}

			for (std::size_t variable_index{};
				variable_index < reflected_buffer->variable_count; ++variable_index)
			{
				const auto& variable = reflected_constant_variables[
					reflected_buffer->variable_begin + variable_index];
				const auto variable_end = (std::min)(static_cast<std::uint64_t>(compared),
					static_cast<std::uint64_t>(variable.start_offset) + variable.size);
				std::uint32_t differences{};
				std::uint32_t first{}, last{};
				for (auto byte = static_cast<std::uint64_t>(variable.start_offset);
					byte < variable_end; ++byte)
				{
					if (left.bytes[byte] == right.bytes[byte]) continue;
					if (differences == 0) first = static_cast<std::uint32_t>(byte);
					last = static_cast<std::uint32_t>(byte);
					++differences;
				}
				if (differences == 0) continue;
				if (output.variable_count >= output.variables.size())
				{
					++output.variable_overflows;
					continue;
				}
				auto& sample = output.variables[output.variable_count++];
				sample.name_hash = variable.name_hash;
				sample.start_offset = variable.start_offset;
				sample.size = variable.size;
				sample.differing_bytes = differences;
				sample.first_difference = first;
				sample.last_difference = last;
				sample.name = variable.name;
			}
			output.complete = left.complete && right.complete &&
				left.byte_width == right.byte_width &&
				left.captured_bytes == right.captured_bytes &&
				output.variable_overflows == 0;
		}

		void add_dynamic_fx_constant_buffer_sample(
			dynamic_fx_family_comparison& comparison, const dynamic_fx_family family,
			const std::uint64_t family_ordinal, const std::uint64_t output_ordinal,
			const std::uint64_t output0_family_ordinal,
			const std::uint64_t output1_family_ordinal,
			const std::uint64_t output0_output_ordinal,
			const std::uint64_t output1_output_ordinal,
			const std::uint8_t stage, const std::uint8_t slot,
			const program_identity& left_program,
			const program_identity& right_program,
			const constant_buffer_slot_identity* const left,
			const constant_buffer_slot_identity* const right,
			const bool identity_mismatch, const bool content_unknown,
			const bool content_mismatch) noexcept
		{
			dynamic_fx_constant_buffer_mismatch_sample sample{};
			sample.family = family;
			sample.output_ordinal = output_ordinal;
			sample.last_output_ordinal = output_ordinal;
			sample.family_ordinal = family_ordinal;
			sample.output0_family_ordinal = output0_family_ordinal;
			sample.output1_family_ordinal = output1_family_ordinal;
			sample.output0_output_ordinal = output0_output_ordinal;
			sample.output1_output_ordinal = output1_output_ordinal;
			sample.stage = stage;
			sample.slot = slot;
			sample.output0_present = left != nullptr;
			sample.output1_present = right != nullptr;
			sample.identity_mismatch = identity_mismatch;
			sample.content_unknown = content_unknown;
			sample.content_mismatch = content_mismatch;
			sample.output0_program = left_program;
			sample.output1_program = right_program;
			const auto copy = [](dynamic_fx_constant_buffer_binding& destination,
				const constant_buffer_slot_identity& source) noexcept
			{
				destination.stage = source.stage;
				destination.slot = source.slot;
				destination.identity = source.identity;
				destination.content = source.content;
			};
			if (left) copy(sample.output0, *left);
			if (right) copy(sample.output1, *right);
			if (left && right && content_mismatch)
			{
				const auto* const left_bytes = find_dynamic_fx_content_capture(0,
					output0_output_ordinal, stage, slot);
				const auto* const right_bytes = find_dynamic_fx_content_capture(1,
					output1_output_ordinal, stage, slot);
				if (left_bytes != nullptr && right_bytes != nullptr &&
					left_bytes->content.upload_generation ==
						left->content.upload_generation &&
					right_bytes->content.upload_generation ==
						right->content.upload_generation)
				{
					sample.byte_comparison_available = true;
					sample.byte_compared = (std::min)(left_bytes->content.captured_bytes,
						right_bytes->content.captured_bytes);
					sample.byte_comparison_complete = left_bytes->content.complete &&
						right_bytes->content.complete &&
						left_bytes->content.byte_width == right_bytes->content.byte_width &&
						left_bytes->content.captured_bytes ==
							right_bytes->content.captured_bytes;
					for (std::uint32_t index{}; index < sample.byte_compared; ++index)
					{
						const auto left_value = left_bytes->content.bytes[index];
						const auto right_value = right_bytes->content.bytes[index];
						if (left_value == right_value) continue;
						if (sample.byte_difference_count == 0)
							sample.first_byte_difference = index;
						sample.last_byte_difference = index;
						++sample.byte_difference_count;
						if (sample.byte_difference_sample_count <
							sample.byte_difference_offsets.size())
						{
							const auto destination = sample.byte_difference_sample_count++;
							sample.byte_difference_offsets[destination] =
								static_cast<std::uint16_t>(index);
							sample.output0_difference_values[destination] = left_value;
							sample.output1_difference_values[destination] = right_value;
						}
					}
					sample.word_compared = sample.byte_compared /
						static_cast<std::uint32_t>(sizeof(std::uint32_t));
					for (std::uint32_t word{}; word < sample.word_compared; ++word)
					{
						const auto offset = word * static_cast<std::uint32_t>(
							sizeof(std::uint32_t));
						std::uint32_t left_value{};
						std::uint32_t right_value{};
						std::memcpy(&left_value,
							left_bytes->content.bytes.data() + offset,
							sizeof(left_value));
						std::memcpy(&right_value,
							right_bytes->content.bytes.data() + offset,
							sizeof(right_value));
						if (left_value == right_value) continue;
						if (sample.word_difference_count == 0)
							sample.first_word_difference = offset;
						sample.last_word_difference = offset;
						++sample.word_difference_count;
						if (sample.word_difference_sample_count <
							sample.word_difference_offsets.size())
						{
							const auto destination =
								sample.word_difference_sample_count++;
							sample.word_difference_offsets[destination] =
								static_cast<std::uint16_t>(offset);
							sample.output0_word_values[destination] = left_value;
							sample.output1_word_values[destination] = right_value;
						}
					}
					const auto reflected_stage = static_cast<shader_stage>(stage);
					annotate_dynamic_fx_constant_buffer(sample.output0_reflection,
						left_program, reflected_stage, slot, left_bytes->content,
						right_bytes->content);
					annotate_dynamic_fx_constant_buffer(sample.output1_reflection,
						right_program, reflected_stage, slot, left_bytes->content,
						right_bytes->content);
				}
			}

			const auto same_shape = [](const dynamic_fx_constant_buffer_mismatch_sample&
				left_sample, const dynamic_fx_constant_buffer_mismatch_sample& right_sample)
			{
				if (left_sample.family != right_sample.family ||
					left_sample.stage != right_sample.stage ||
					left_sample.slot != right_sample.slot ||
					left_sample.output0_present != right_sample.output0_present ||
					left_sample.output1_present != right_sample.output1_present ||
					left_sample.identity_mismatch != right_sample.identity_mismatch ||
					left_sample.content_unknown != right_sample.content_unknown ||
					left_sample.content_mismatch != right_sample.content_mismatch ||
					std::memcmp(&left_sample.output0_program,
						&right_sample.output0_program, sizeof(program_identity)) != 0 ||
					std::memcmp(&left_sample.output1_program,
						&right_sample.output1_program, sizeof(program_identity)) != 0 ||
					left_sample.output0.content.byte_width !=
						right_sample.output0.content.byte_width ||
					left_sample.output1.content.byte_width !=
						right_sample.output1.content.byte_width ||
					left_sample.byte_comparison_available !=
						right_sample.byte_comparison_available ||
					left_sample.byte_comparison_complete !=
						right_sample.byte_comparison_complete ||
					left_sample.byte_compared != right_sample.byte_compared ||
					left_sample.byte_difference_count !=
						right_sample.byte_difference_count ||
					left_sample.first_byte_difference !=
						right_sample.first_byte_difference ||
					left_sample.last_byte_difference !=
						right_sample.last_byte_difference ||
					left_sample.byte_difference_sample_count !=
						right_sample.byte_difference_sample_count ||
					left_sample.word_compared != right_sample.word_compared ||
					left_sample.word_difference_count !=
						right_sample.word_difference_count ||
					left_sample.first_word_difference !=
						right_sample.first_word_difference ||
					left_sample.last_word_difference !=
						right_sample.last_word_difference ||
					left_sample.word_difference_sample_count !=
						right_sample.word_difference_sample_count)
				{
					return false;
				}
				for (std::size_t index{};
					index < left_sample.byte_difference_sample_count; ++index)
				{
					if (left_sample.byte_difference_offsets[index] !=
						right_sample.byte_difference_offsets[index]) return false;
				}
				for (std::size_t index{};
					index < left_sample.word_difference_sample_count; ++index)
				{
					if (left_sample.word_difference_offsets[index] !=
						right_sample.word_difference_offsets[index]) return false;
				}
				return true;
			};
			for (std::size_t index{}; index < comparison.constant_buffer_sample_count;
				++index)
			{
				auto& existing = comparison.constant_buffer_samples[index];
				if (!same_shape(existing, sample)) continue;
				++existing.occurrences;
				existing.last_output_ordinal = output_ordinal;
				return;
			}
			if (comparison.constant_buffer_sample_count >=
				comparison.constant_buffer_samples.size())
			{
				++comparison.constant_buffer_sample_overflows;
				return;
			}
			comparison.constant_buffer_samples[
				comparison.constant_buffer_sample_count++] = sample;
		}

		void add_dynamic_fx_ps_shader_resource_sample(
			dynamic_fx_family_comparison& comparison, const dynamic_fx_family family,
			const std::uint64_t family_ordinal, const std::uint64_t output_ordinal,
			const std::uint64_t output0_family_ordinal,
			const std::uint64_t output1_family_ordinal, const std::uint8_t slot,
			const shader_resource_slot_identity* const left,
			const shader_resource_slot_identity* const right) noexcept
		{
			if (comparison.ps_shader_resource_sample_count >=
				comparison.ps_shader_resource_samples.size())
			{
				++comparison.ps_shader_resource_sample_overflows;
				return;
			}
			auto& sample = comparison.ps_shader_resource_samples[
				comparison.ps_shader_resource_sample_count++];
			sample.family = family;
			sample.output_ordinal = output_ordinal;
			sample.family_ordinal = family_ordinal;
			sample.output0_family_ordinal = output0_family_ordinal;
			sample.output1_family_ordinal = output1_family_ordinal;
			sample.slot = slot;
			sample.output0_present = left != nullptr;
			sample.output1_present = right != nullptr;
			if (left)
			{
				sample.output0.slot = left->slot;
				sample.output0.binding = left->binding;
			}
			if (right)
			{
				sample.output1.slot = right->slot;
				sample.output1.binding = right->binding;
			}
		}

		void add_dynamic_fx_ps_sampler_sample(
			dynamic_fx_family_comparison& comparison, const dynamic_fx_family family,
			const std::uint64_t family_ordinal, const std::uint64_t output_ordinal,
			const std::uint64_t output0_family_ordinal,
			const std::uint64_t output1_family_ordinal, const std::uint8_t slot,
			const sampler_slot_identity* const left,
			const sampler_slot_identity* const right,
			const bool identity_mismatch, const bool descriptor_mismatch) noexcept
		{
			if (comparison.ps_sampler_sample_count >=
				comparison.ps_sampler_samples.size())
			{
				++comparison.ps_sampler_sample_overflows;
				return;
			}
			auto& sample = comparison.ps_sampler_samples[
				comparison.ps_sampler_sample_count++];
			sample.family = family;
			sample.output_ordinal = output_ordinal;
			sample.family_ordinal = family_ordinal;
			sample.output0_family_ordinal = output0_family_ordinal;
			sample.output1_family_ordinal = output1_family_ordinal;
			sample.slot = slot;
			sample.output0_present = left != nullptr;
			sample.output1_present = right != nullptr;
			sample.identity_mismatch = identity_mismatch;
			sample.descriptor_mismatch = descriptor_mismatch;
			const auto copy = [](dynamic_fx_sampler_binding& destination,
				const sampler_slot_identity& source) noexcept
			{
				destination.slot = source.slot;
				destination.identity = source.identity;
				destination.descriptor = source.descriptor;
			};
			if (left) copy(sample.output0, *left);
			if (right) copy(sample.output1, *right);
		}

		void compare_dynamic_fx_bindings(const dynamic_fx_family family,
			const std::uint64_t family_ordinal, const dynamic_fx_invocation& left,
			const detailed_binding_snapshot& left_detail,
			const dynamic_fx_invocation& right,
			const detailed_binding_snapshot& right_detail) noexcept
		{
			const auto family_index = dynamic_fx_index(family);
			if (family_index >= dynamic_fx_binding_working.size()) return;
			auto& comparison = dynamic_fx_binding_working[family_index];
			if (left.bindings.constant_buffers != right.bindings.constant_buffers ||
				left.bindings.constant_buffer_count !=
					right.bindings.constant_buffer_count)
			{
				++comparison.constant_buffer_signature_mismatches;
			}
			if (left.bindings.shader_resources != right.bindings.shader_resources ||
				left.bindings.shader_resource_count !=
					right.bindings.shader_resource_count)
			{
				++comparison.shader_resource_signature_mismatches;
			}
			if (left.bindings.samplers != right.bindings.samplers ||
				left.bindings.sampler_count != right.bindings.sampler_count)
			{
				++comparison.sampler_signature_mismatches;
			}
			comparison.binding_detail_drops +=
				left_detail.constant_buffer_dropped_by_stage[
					stage_index(shader_stage::vs)] +
				left_detail.constant_buffer_dropped_by_stage[
					stage_index(shader_stage::ps)] +
				right_detail.constant_buffer_dropped_by_stage[
					stage_index(shader_stage::vs)] +
				right_detail.constant_buffer_dropped_by_stage[
					stage_index(shader_stage::ps)] +
				left_detail.shader_resource_dropped_by_stage[
					stage_index(shader_stage::ps)] +
				right_detail.shader_resource_dropped_by_stage[
					stage_index(shader_stage::ps)];

			// Resolve the GPU-only 1088-byte material upload once per already-paired
			// draw. VS and PS commonly bind the same object/generation, so retain one
			// exact generation pair rather than duplicating the later readback.
			bool material_reference_recorded{};

			for (std::uint8_t stage{}; stage < 2; ++stage)
			{
				for (std::uint8_t slot{}; slot < constant_buffer_slots; ++slot)
				{
					const auto* const left_binding = find_dynamic_fx_constant_buffer(
						left_detail, stage, slot);
					const auto* const right_binding = find_dynamic_fx_constant_buffer(
						right_detail, stage, slot);
					if (!left_binding && !right_binding) continue;
					++comparison.constant_buffer_slot_comparisons;
					const auto identity_mismatch = !left_binding || !right_binding ||
						left_binding->identity != right_binding->identity;
					if (identity_mismatch)
						++comparison.constant_buffer_identity_mismatches;
					bool content_unknown{};
					bool content_mismatch{};
					if (left_binding && right_binding)
					{
						if (!material_reference_recorded &&
							left_binding->content.byte_width ==
								engine_stereo_material_buffer_probe::material_buffer_bytes &&
							right_binding->content.byte_width ==
								engine_stereo_material_buffer_probe::material_buffer_bytes)
						{
							engine_stereo_material_buffer_probe::note_dynamic_fx_reference(
								static_cast<std::uint8_t>(family), left.output_ordinal,
								right.output_ordinal,
								left.family_ordinal, right.family_ordinal,
								stage, slot, left_binding->identity,
								right_binding->identity,
								left_binding->content.upload_generation,
								right_binding->content.upload_generation);
							material_reference_recorded = true;
						}
						if (!left_binding->content.known || !right_binding->content.known)
						{
							content_unknown = true;
							++comparison.constant_buffer_content_unknown;
						}
						else
						{
							++comparison.constant_buffer_content_comparisons;
							content_mismatch = !dynamic_fx_constant_buffer_content_equal(
								left_binding->content, right_binding->content);
							if (content_mismatch)
								++comparison.constant_buffer_content_mismatches;
						}
					}
					if (identity_mismatch || content_unknown || content_mismatch)
					{
						add_dynamic_fx_constant_buffer_sample(comparison, family,
							family_ordinal, right.output_ordinal,
							left.family_ordinal, right.family_ordinal,
							left.output_ordinal, right.output_ordinal, stage, slot,
							left.program, right.program,
							left_binding, right_binding,
							identity_mismatch, content_unknown, content_mismatch);
					}
				}
			}
			for (std::uint8_t slot{}; slot < scanned_srv_slots; ++slot)
			{
				const auto* const left_binding =
					find_dynamic_fx_ps_shader_resource(left_detail, slot);
				const auto* const right_binding =
					find_dynamic_fx_ps_shader_resource(right_detail, slot);
				if (!left_binding && !right_binding) continue;
				++comparison.ps_shader_resource_comparisons;
				if (left_binding && right_binding &&
					srv_binding_equal(left_binding->binding, right_binding->binding))
				{
					continue;
				}
				++comparison.ps_shader_resource_mismatches;
				add_dynamic_fx_ps_shader_resource_sample(comparison, family,
					family_ordinal, right.output_ordinal, left.family_ordinal,
					right.family_ordinal, slot, left_binding, right_binding);
			}
			for (std::uint8_t slot{};
				slot < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; ++slot)
			{
				const auto* const left_binding = find_dynamic_fx_ps_sampler(
					left_detail, slot);
				const auto* const right_binding = find_dynamic_fx_ps_sampler(
					right_detail, slot);
				if (!left_binding && !right_binding) continue;
				++comparison.ps_sampler_comparisons;
				const auto identity_mismatch = !left_binding || !right_binding ||
					left_binding->identity != right_binding->identity;
				const auto descriptor_mismatch = !left_binding || !right_binding ||
					!sampler_descriptor_equal(left_binding->descriptor,
						right_binding->descriptor);
				if (identity_mismatch) ++comparison.ps_sampler_identity_mismatches;
				if (descriptor_mismatch) ++comparison.ps_sampler_descriptor_mismatches;
				if (!identity_mismatch && !descriptor_mismatch) continue;
				add_dynamic_fx_ps_sampler_sample(comparison, family, family_ordinal,
					right.output_ordinal, left.family_ordinal, right.family_ordinal,
					slot, left_binding, right_binding, identity_mismatch,
					descriptor_mismatch);
			}
		}

		void finalize_dynamic_fx_comparison() noexcept
		{
			auto& report = census.dynamic_fx;
			report.comparison_finalized = true;
			report.evidence_available = report.semantic_hits != 0;
			report.comparison_complete = report.evidence_available &&
				report.invalid_invocations == 0 && report.observation_overflows == 0;
			if (report.observation_overflows != 0)
				report.evidence = dynamic_fx_evidence::truncated;
			else if (report.semantic_hits != 0)
				report.evidence = dynamic_fx_evidence::observed;
			else if (report.range_hits != 0)
				report.evidence = dynamic_fx_evidence::invalid_shape;
			else
				report.evidence = dynamic_fx_evidence::not_observed;
			for (std::size_t family_index{}; family_index < dynamic_fx_family_count;
				++family_index)
			{
				const auto family = static_cast<dynamic_fx_family>(family_index);
				const auto& left = report.eyes[0].families[family_index];
				const auto& right = report.eyes[1].families[family_index];
				auto& comparison = report.families[family_index];
				comparison = dynamic_fx_binding_working[family_index];
				comparison.output0_calls = left.calls;
				comparison.output1_calls = right.calls;
				comparison.paired_calls = (std::min)(left.calls, right.calls);
				comparison.compared_calls = (std::min)(left.observation_count,
					right.observation_count);
				comparison.missing_output1 = left.calls > right.calls ?
					left.calls - right.calls : 0;
				comparison.missing_output0 = right.calls > left.calls ?
					right.calls - left.calls : 0;
				comparison.output0_zero_index_counts = left.zero_index_counts;
				comparison.output1_zero_index_counts = right.zero_index_counts;
				comparison.comparison_finalized = true;
				comparison.evidence_available = left.calls != 0 || right.calls != 0;
				comparison.comparison_complete = comparison.evidence_available &&
					left.calls == right.calls &&
					left.invalid_invocations == 0 && right.invalid_invocations == 0 &&
					left.observation_overflows == 0 && right.observation_overflows == 0;

				for (std::size_t index{}; index < comparison.compared_calls; ++index)
				{
					const auto& left_value = left.observations[index];
					const auto& right_value = right.observations[index];
					bool mismatch{};
					const auto left_index_count = static_cast<std::uint32_t>(
						left_value.arguments.values[0]);
					const auto right_index_count = static_cast<std::uint32_t>(
						right_value.arguments.values[0]);
					if (left_index_count != right_index_count)
					{
						++comparison.index_count_mismatches;
						mismatch = true;
					}
					const auto left_base_vertex = static_cast<std::int32_t>(
						left_value.arguments.values[2]);
					const auto right_base_vertex = static_cast<std::int32_t>(
						right_value.arguments.values[2]);
					if (left_base_vertex != right_base_vertex)
					{
						++comparison.base_vertex_mismatches;
						mismatch = true;
					}
					if (left_value.output_ordinal != right_value.output_ordinal)
					{
						++comparison.ordinal_mismatches;
						mismatch = true;
					}
					if (std::memcmp(&left_value.program, &right_value.program,
						sizeof(left_value.program)) != 0)
					{
						++comparison.program_mismatches;
						mismatch = true;
					}
					if (left_value.caller != right_value.caller)
					{
						++comparison.caller_mismatches;
						mismatch = true;
					}
					if (left_value.output_target_id != right_value.output_target_id)
					{
						++comparison.output_target_mismatches;
						mismatch = true;
					}
					if (left_value.input_layout != right_value.input_layout)
					{
						++comparison.input_layout_mismatches;
						mismatch = true;
					}
					if (left_value.depth_stencil_state !=
						right_value.depth_stencil_state ||
						left_value.stencil_reference != right_value.stencil_reference)
					{
						++comparison.depth_state_mismatches;
						mismatch = true;
					}
					if (left_value.blend_state != right_value.blend_state ||
						left_value.sample_mask != right_value.sample_mask)
					{
						++comparison.blend_state_mismatches;
						mismatch = true;
					}
					if (left_value.blend_factor_bits != right_value.blend_factor_bits)
					{
						++comparison.blend_factor_mismatches;
						mismatch = true;
					}
					if (left_value.rasterizer_state != right_value.rasterizer_state)
					{
						++comparison.rasterizer_state_mismatches;
						mismatch = true;
					}
					if (left_value.primitive_topology != right_value.primitive_topology)
					{
						++comparison.topology_mismatches;
						mismatch = true;
					}
					if (left_value.viewport_count != right_value.viewport_count ||
						left_value.viewport_hash != right_value.viewport_hash)
					{
						++comparison.viewport_mismatches;
						mismatch = true;
					}
					if (left_value.scissor_count != right_value.scissor_count ||
						left_value.scissor_hash != right_value.scissor_hash)
					{
						++comparison.scissor_mismatches;
						mismatch = true;
					}
					if (left_value.index_buffer != right_value.index_buffer ||
						left_value.index_format != right_value.index_format ||
						left_value.index_offset != right_value.index_offset)
					{
						++comparison.index_buffer_mismatches;
						mismatch = true;
					}
					bool vertex_bindings_equal = true;
					for (std::size_t slot{}; slot < dynamic_fx_vertex_binding_count; ++slot)
					{
						const auto& left_binding = left_value.vertex_buffers[slot];
						const auto& right_binding = right_value.vertex_buffers[slot];
						if (left_binding.buffer != right_binding.buffer)
						{
							++comparison.vertex_buffer_mismatches;
							++comparison.vertex_buffer_mismatches_by_slot[slot];
							vertex_bindings_equal = false;
							mismatch = true;
						}
						if (left_binding.stride != right_binding.stride)
						{
							++comparison.vertex_stride_mismatches;
							++comparison.vertex_stride_mismatches_by_slot[slot];
							vertex_bindings_equal = false;
							mismatch = true;
						}
						if (left_binding.offset != right_binding.offset)
						{
							++comparison.vertex_offset_mismatches;
							++comparison.vertex_offset_mismatches_by_slot[slot];
							vertex_bindings_equal = false;
							mismatch = true;
						}
					}
					if (left_value.arguments.count != 3 ||
						right_value.arguments.count != 3)
					{
						++comparison.argument_shape_mismatches;
						mismatch = true;
					}

					bool delta_mismatch{};
					bool first_nonzero_delta{};
					const auto delta_eligible = left_index_count != 0 &&
						left_index_count == right_index_count &&
						left_base_vertex == right_base_vertex &&
						left_value.caller == right_value.caller &&
						left_value.output_target_id == right_value.output_target_id &&
						left_value.index_format == right_value.index_format &&
						left_value.index_offset == right_value.index_offset &&
						vertex_bindings_equal &&
						std::memcmp(&left_value.program, &right_value.program,
							sizeof(left_value.program)) == 0;
					if (delta_eligible)
					{
						++comparison.start_index_delta_eligible;
						const auto left_start_index = static_cast<std::uint32_t>(
							left_value.arguments.values[1]);
						const auto right_start_index = static_cast<std::uint32_t>(
							right_value.arguments.values[1]);
						const auto delta = static_cast<std::int64_t>(right_start_index) -
							static_cast<std::int64_t>(left_start_index);
						if (delta != 0)
						{
							first_nonzero_delta = comparison.nonzero_start_index_deltas == 0;
							++comparison.nonzero_start_index_deltas;
						}
						if (!comparison.start_index_delta_observed)
						{
							comparison.start_index_delta_observed = true;
							comparison.start_index_delta_constant = true;
							comparison.start_index_delta_first = delta;
							comparison.start_index_delta_min = delta;
							comparison.start_index_delta_max = delta;
						}
						else
						{
							comparison.start_index_delta_min = (std::min)(
								comparison.start_index_delta_min, delta);
							comparison.start_index_delta_max = (std::max)(
								comparison.start_index_delta_max, delta);
							delta_mismatch = delta !=
								comparison.start_index_delta_first;
							if (delta_mismatch)
								comparison.start_index_delta_constant = false;
						}
					}
					else
					{
						++comparison.start_index_delta_ineligible;
					}
					if (mismatch || delta_mismatch || first_nonzero_delta)
					{
						add_dynamic_fx_mismatch_sample(comparison, family, index + 1,
							&left_value, &right_value);
					}
				}

				const auto paired_stored = comparison.compared_calls;
				for (std::size_t index = paired_stored;
					index < left.observation_count; ++index)
				{
					add_dynamic_fx_mismatch_sample(comparison, family, index + 1,
						&left.observations[index], nullptr);
				}
				for (std::size_t index = paired_stored;
					index < right.observation_count; ++index)
				{
					add_dynamic_fx_mismatch_sample(comparison, family, index + 1,
						nullptr, &right.observations[index]);
				}
				if (comparison.mismatch_sample_overflows != 0 ||
					comparison.binding_detail_drops != 0 ||
					comparison.constant_buffer_content_unknown != 0 ||
					comparison.constant_buffer_sample_overflows != 0 ||
					comparison.ps_shader_resource_sample_overflows != 0)
				{
					comparison.comparison_complete = false;
				}
				if (comparison.evidence_available && !comparison.comparison_complete)
					report.comparison_complete = false;
			}
			if (report.evidence == dynamic_fx_evidence::observed &&
				!report.comparison_complete)
			{
				report.evidence = dynamic_fx_evidence::incomplete;
			}
		}

		void compare_ordered(const std::size_t ordinal,
			const detailed_observation& right_value) noexcept
		{
			if (ordinal >= census.eyes[0].observations ||
				ordinal >= left_signatures.size()) return;
			const auto& left_value = left_signatures[ordinal];
			const auto& left = left_value.signature;
			const auto& right = right_value.signature;
			auto& comparison = census.ordered;
			++comparison.comparisons;
			std::uint32_t mask{};
			const auto mismatch = [&](const bool differs, const std::uint32_t bit,
				std::uint64_t& counter)
			{
				if (!differs) return;
				mask |= bit;
				++counter;
			};
			mismatch(left.operation != right.operation, mismatch_api,
				comparison.api_mismatches);
			mismatch(left.caller != right.caller, mismatch_caller,
				comparison.caller_mismatches);
			const auto argument_differs = !arguments_equal(left.arguments,
				right.arguments);
			mismatch(argument_differs, mismatch_arguments,
				comparison.argument_mismatches);
			mismatch(std::memcmp(&left.program, &right.program,
				sizeof(left.program)) != 0, mismatch_program,
				comparison.program_mismatches);
			mismatch(left.output_target_id != right.output_target_id,
				mismatch_output_target, comparison.output_target_mismatches);
			mismatch(left.pipeline.render_target_resource !=
				right.pipeline.render_target_resource, mismatch_render_target,
				comparison.render_target_mismatches);
			mismatch(left.pipeline.depth_stencil_resource !=
				right.pipeline.depth_stencil_resource, mismatch_depth_target,
				comparison.depth_target_mismatches);
			mismatch(left.pipeline.depth_stencil_state !=
				right.pipeline.depth_stencil_state ||
				left.pipeline.stencil_reference != right.pipeline.stencil_reference,
				mismatch_depth_state, comparison.depth_state_mismatches);
			mismatch(left.pipeline.blend_state != right.pipeline.blend_state ||
				left.pipeline.sample_mask != right.pipeline.sample_mask,
				mismatch_blend_state, comparison.blend_state_mismatches);
			mismatch(left.pipeline.input_layout != right.pipeline.input_layout,
				mismatch_input_layout, comparison.input_layout_mismatches);
			mismatch(left.pipeline.blend_factor_bits !=
				right.pipeline.blend_factor_bits, mismatch_blend_factor,
				comparison.blend_factor_mismatches);
			mismatch(left.pipeline.rasterizer_state != right.pipeline.rasterizer_state,
				mismatch_rasterizer_state, comparison.rasterizer_state_mismatches);
			mismatch(left.pipeline.primitive_topology !=
				right.pipeline.primitive_topology, mismatch_topology,
				comparison.topology_mismatches);
			mismatch(left.pipeline.index_buffer != right.pipeline.index_buffer ||
				left.pipeline.index_format != right.pipeline.index_format ||
				left.pipeline.index_offset != right.pipeline.index_offset,
				mismatch_index_buffer, comparison.index_buffer_mismatches);
			mismatch(left.pipeline.viewport_count != right.pipeline.viewport_count ||
				left.pipeline.viewport_hash != right.pipeline.viewport_hash,
				mismatch_viewport, comparison.viewport_mismatches);
			mismatch(left.pipeline.scissor_count != right.pipeline.scissor_count ||
				left.pipeline.scissor_hash != right.pipeline.scissor_hash,
				mismatch_scissor, comparison.scissor_mismatches);
			const auto constant_buffers_differ =
				left.bindings.constant_buffers != right.bindings.constant_buffers ||
				left.bindings.constant_buffer_count !=
					right.bindings.constant_buffer_count;
			const auto record_constant_buffer_difference = [&]
				(const std::uint8_t stage, const std::uint8_t slot,
				const std::uintptr_t left_identity,
				const std::uintptr_t right_identity)
			{
				if (left_identity == right_identity ||
					comparison.constant_buffer_sample_count >=
						comparison.constant_buffer_samples.size()) return;
				auto& sample = comparison.constant_buffer_samples[
					comparison.constant_buffer_sample_count++];
				sample.ordinal = ordinal + 1;
				sample.left = make_context(left);
				sample.right = make_context(right);
				sample.stage = stage;
				sample.slot = slot;
				sample.left_identity = left_identity;
				sample.right_identity = right_identity;
			};
			for (std::uint16_t index{};
				index < left_value.bindings.constant_buffer_count; ++index)
			{
				const auto& left_entry =
					left_value.bindings.constant_buffers[index];
				const auto* right_entry = find_constant_buffer(right_value.bindings,
					left_entry.stage, left_entry.slot);
				if (!right_entry && right_value.bindings.constant_buffer_dropped) continue;
				record_constant_buffer_difference(left_entry.stage, left_entry.slot,
					left_entry.identity, right_entry ? right_entry->identity : 0);
			}
			for (std::uint16_t index{};
				index < right_value.bindings.constant_buffer_count; ++index)
			{
				const auto& right_entry =
					right_value.bindings.constant_buffers[index];
				if (find_constant_buffer(left_value.bindings, right_entry.stage,
					right_entry.slot)) continue;
				if (left_value.bindings.constant_buffer_dropped) continue;
				record_constant_buffer_difference(right_entry.stage, right_entry.slot,
					0, right_entry.identity);
			}
			mismatch(constant_buffers_differ, mismatch_constant_buffers,
				comparison.constant_buffer_mismatches);

			const auto shader_resources_differ =
				left.bindings.shader_resources != right.bindings.shader_resources ||
				left.bindings.shader_resource_count !=
					right.bindings.shader_resource_count;
			const auto record_shader_resource_difference = [&]
				(const std::uint8_t stage, const std::uint8_t slot,
				const srv_binding_identity& left_binding,
				const srv_binding_identity& right_binding)
			{
				if (srv_binding_equal(left_binding, right_binding)) return;
				record_srv_usage_difference(ordinal + 1, left, right, stage, slot,
					left_binding, right_binding);
			};
			for (std::uint16_t index{};
				index < left_value.bindings.shader_resource_count; ++index)
			{
				const auto& left_entry =
					left_value.bindings.shader_resources[index];
				const auto* right_entry = find_shader_resource(right_value.bindings,
					left_entry.stage, left_entry.slot);
				if (!right_entry && right_value.bindings.shader_resource_dropped) continue;
				record_shader_resource_difference(left_entry.stage, left_entry.slot,
					left_entry.binding, right_entry ? right_entry->binding :
						srv_binding_identity{});
			}
			for (std::uint16_t index{};
				index < right_value.bindings.shader_resource_count; ++index)
			{
				const auto& right_entry =
					right_value.bindings.shader_resources[index];
				if (find_shader_resource(left_value.bindings, right_entry.stage,
					right_entry.slot)) continue;
				if (left_value.bindings.shader_resource_dropped) continue;
				record_shader_resource_difference(right_entry.stage, right_entry.slot,
					{}, right_entry.binding);
			}
			mismatch(shader_resources_differ, mismatch_shader_resources,
				comparison.shader_resource_mismatches);
			mismatch(left.bindings.unordered_access != right.bindings.unordered_access ||
				left.bindings.unordered_access_count !=
					right.bindings.unordered_access_count,
				mismatch_unordered_access, comparison.unordered_access_mismatches);
			mismatch(left.bindings.outputs != right.bindings.outputs ||
				left.bindings.output_count != right.bindings.output_count,
				mismatch_outputs, comparison.output_binding_mismatches);
			if (!mask) return;
			++comparison.mismatches;
			if (argument_differs && comparison.argument_sample_count <
				comparison.argument_samples.size())
			{
				auto& sample = comparison.argument_samples[
					comparison.argument_sample_count++];
				sample.ordinal = ordinal + 1;
				sample.left = left;
				sample.right = right;
			}
			if (!comparison.first_mismatch_ordinal)
			{
				comparison.first_mismatch_ordinal = ordinal + 1;
				comparison.first_mismatch_mask = mask;
				comparison.first_left = left;
				comparison.first_right = right;
			}
		}

		program_identity capture_program(ID3D11DeviceContext* const context,
			eye_report& eye, const api operation,
			binding_signature& bindings,
			detailed_binding_snapshot& detailed,
			const std::uint64_t ordinal,
			const std::uintptr_t caller) noexcept
		{
			ID3D11VertexShader* vs{};
			ID3D11PixelShader* ps{};
			ID3D11ComputeShader* cs{};
			ID3D11GeometryShader* gs{};
			ID3D11HullShader* hs{};
			ID3D11DomainShader* ds{};
			context->VSGetShader(&vs, nullptr, nullptr);
			context->PSGetShader(&ps, nullptr, nullptr);
			context->CSGetShader(&cs, nullptr, nullptr);
			context->GSGetShader(&gs, nullptr, nullptr);
			context->HSGetShader(&hs, nullptr, nullptr);
			context->DSGetShader(&ds, nullptr, nullptr);
			census.query_calls += 6;

			const auto dispatch = is_dispatch(operation);
			const auto capture_dynamic_content = !dispatch &&
				classify_dynamic_fx_caller(caller) != dynamic_fx_family::unknown;
			program_identity output{};
			if (dispatch)
			{
				output.cs = reinterpret_cast<std::uintptr_t>(cs);
				add_identity(eye.cs_shaders, eye.cs_shader_count, output.cs);
			}
			else
			{
				output.vs = reinterpret_cast<std::uintptr_t>(vs);
				output.ps = reinterpret_cast<std::uintptr_t>(ps);
				output.gs = reinterpret_cast<std::uintptr_t>(gs);
				output.hs = reinterpret_cast<std::uintptr_t>(hs);
				output.ds = reinterpret_cast<std::uintptr_t>(ds);
				add_identity(eye.vs_shaders, eye.vs_shader_count, output.vs);
				add_identity(eye.ps_shaders, eye.ps_shader_count, output.ps);
			}
			if (dispatch)
			{
				reflect_shader(cs, shader_stage::cs);
			}
			else
			{
				reflect_shader(vs, shader_stage::vs);
				reflect_shader(ps, shader_stage::ps);
				reflect_shader(gs, shader_stage::gs);
				reflect_shader(hs, shader_stage::hs);
				reflect_shader(ds, shader_stage::ds);
				if (capture_dynamic_content)
				{
					reflect_constant_buffers(vs, shader_stage::vs);
					reflect_constant_buffers(ps, shader_stage::ps);
				}
			}

			std::array<ID3D11Buffer*,
				D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers{};
			const auto record_buffers = [&](const shader_stage stage)
			{
				for (std::size_t slot{}; slot < buffers.size(); ++slot)
				{
					const auto identity = reinterpret_cast<std::uintptr_t>(
						buffers[slot]);
					hash_binding(bindings.constant_buffers,
						bindings.constant_buffer_count,
						static_cast<std::uint8_t>(stage),
						static_cast<std::uint8_t>(slot), identity);
					engine_stereo_constant_buffer_probe::content_snapshot content{};
					const engine_stereo_constant_buffer_probe::content_snapshot*
						content_pointer{};
					if (identity && capture_dynamic_content &&
						(stage == shader_stage::vs || stage == shader_stage::ps))
					{
						(void)engine_stereo_constant_buffer_probe::query_content_snapshot(
							buffers[slot], content);
						content_pointer = &content;
						if (content.byte_width == engine_stereo_material_buffer_probe::
								material_buffer_bytes)
						{
							engine_stereo_material_buffer_probe::capture_bound_buffer(
								context, active_eye, ordinal, buffers[slot],
								content.upload_generation, caller);
						}
						capture_dynamic_fx_content_bytes(active_eye, ordinal, stage,
							static_cast<std::uint8_t>(slot), buffers[slot], content);
					}
					record_constant_buffer(detailed, stage,
						static_cast<std::uint8_t>(slot), identity, content_pointer);
					add_binding(eye, static_cast<std::uint8_t>(stage),
						static_cast<std::uint8_t>(slot), identity);
				}
				release_all(buffers);
			};
			if (output.vs)
			{
				context->VSGetConstantBuffers(0, static_cast<UINT>(buffers.size()),
					buffers.data());
				++census.query_calls;
				engine_stereo_constant_buffer_probe::observe_vs_draw(context, ordinal,
					caller, vs, buffers[3]);
				record_buffers(shader_stage::vs);
			}
			if (output.ps)
			{
				context->PSGetConstantBuffers(0, static_cast<UINT>(buffers.size()),
					buffers.data());
				++census.query_calls;
				record_buffers(shader_stage::ps);
				if (capture_dynamic_content)
				{
					std::array<ID3D11SamplerState*,
						D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers{};
					context->PSGetSamplers(0, static_cast<UINT>(samplers.size()),
						samplers.data());
					++census.query_calls;
					for (std::size_t slot{}; slot < samplers.size(); ++slot)
					{
						auto* const sampler = samplers[slot];
						if (sampler == nullptr) continue;
						hash_binding(bindings.samplers, bindings.sampler_count,
							static_cast<std::uint8_t>(shader_stage::ps),
							static_cast<std::uint8_t>(slot),
							reinterpret_cast<std::uintptr_t>(sampler));
						record_sampler(detailed, shader_stage::ps,
							static_cast<std::uint8_t>(slot), sampler);
					}
					release_all(samplers);
				}
			}
			if (output.cs)
			{
				context->CSGetConstantBuffers(0, static_cast<UINT>(buffers.size()),
					buffers.data());
				++census.query_calls;
				record_buffers(shader_stage::cs);
			}
			if (output.gs)
			{
				context->GSGetConstantBuffers(0, static_cast<UINT>(buffers.size()),
					buffers.data());
				++census.query_calls;
				record_buffers(shader_stage::gs);
			}
			if (output.hs)
			{
				context->HSGetConstantBuffers(0, static_cast<UINT>(buffers.size()),
					buffers.data());
				++census.query_calls;
				record_buffers(shader_stage::hs);
			}
			if (output.ds)
			{
				context->DSGetConstantBuffers(0, static_cast<UINT>(buffers.size()),
					buffers.data());
				++census.query_calls;
				record_buffers(shader_stage::ds);
			}

			if (vs) vs->Release();
			if (ps) ps->Release();
			if (cs) cs->Release();
			if (gs) gs->Release();
			if (hs) hs->Release();
			if (ds) ds->Release();
			return output;
		}

		void capture_viewports(ID3D11DeviceContext* const context, eye_report& eye) noexcept
		{
			std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> values{};
			UINT count = static_cast<UINT>(values.size()); context->RSGetViewports(&count, values.data());
			++census.query_calls;
			if (count > values.size()) { ++census.query_failures; return; }
			for (UINT i{}; i < count; ++i)
			{
				std::uint32_t minimum{}, maximum{};
				std::memcpy(&minimum, &values[i].MinDepth, sizeof(minimum));
				std::memcpy(&maximum, &values[i].MaxDepth, sizeof(maximum));
				bool found{};
				for (std::size_t j{}; j < eye.depth_range_count; ++j)
				{
					auto& entry = eye.depth_ranges[j];
					if (entry.min_depth_bits == minimum && entry.max_depth_bits == maximum)
					{ ++entry.viewports; found = true; break; }
				}
				if (!found)
				{
					if (eye.depth_range_count == eye.depth_ranges.size())
						record_overflow(census.depth_overflows);
					else eye.depth_ranges[eye.depth_range_count++] = {minimum, maximum, 1};
				}
			}
		}

		void capture_resources(ID3D11DeviceContext* const context,
			const program_identity& program, const api operation,
			const std::uintptr_t caller, const std::uint32_t output_target_id,
			const std::uintptr_t output_render_target_view,
			const std::uint64_t output_binding_sequence,
			const pipeline_snapshot& pipeline,
			binding_signature& bindings,
			detailed_binding_snapshot& detailed) noexcept
		{
			auto& eye = census.eyes[active_eye];
			const auto dispatch = is_dispatch(operation);
			std::array<ID3D11ShaderResourceView*, scanned_srv_slots> srvs{};
			const auto inspect_srvs = [&](const shader_stage stage,
				const access_site site, const bool shader_active)
			{
				const auto stage_slot = stage_index(stage);
				for (std::size_t slot{}; slot < srvs.size(); ++slot)
				{
					if (srvs[slot] == nullptr) continue;
					++eye.srv_bindings_seen[stage_slot];
					if (shader_active)
					{
						++eye.srv_bindings_admitted[stage_slot];
						record_shader_resource(detailed, stage,
							static_cast<std::uint8_t>(slot),
							capture_srv_binding(srvs[slot]));
						hash_binding(bindings.shader_resources,
							bindings.shader_resource_count,
							static_cast<std::uint8_t>(stage),
							static_cast<std::uint8_t>(slot),
							reinterpret_cast<std::uintptr_t>(srvs[slot]));
						note_view(srvs[slot], false, site,
							static_cast<std::uint8_t>(slot), program, operation,
							caller, output_target_id, output_render_target_view,
							output_binding_sequence);
					}
					else
					{
						++eye.srv_bindings_ignored[stage_slot];
						if (dispatch && stage != shader_stage::cs)
							++census.rejected_dispatch_graphics_srv;
						else if (!dispatch && stage == shader_stage::cs)
							++census.rejected_draw_cs;
						else ++census.rejected_null_shader;
					}
				}
				release_all(srvs);
			};

			context->VSGetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			++census.query_calls;
			inspect_srvs(shader_stage::vs, access_site::vs_srv, program.vs != 0);
			context->PSGetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			++census.query_calls;
			inspect_srvs(shader_stage::ps, access_site::ps_srv, program.ps != 0);
			context->GSGetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			++census.query_calls;
			inspect_srvs(shader_stage::gs, access_site::gs_srv, program.gs != 0);
			context->HSGetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			++census.query_calls;
			inspect_srvs(shader_stage::hs, access_site::hs_srv, program.hs != 0);
			context->DSGetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			++census.query_calls;
			inspect_srvs(shader_stage::ds, access_site::ds_srv, program.ds != 0);
			context->CSGetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			++census.query_calls;
			inspect_srvs(shader_stage::cs, access_site::cs_srv, program.cs != 0);

			std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};
			ID3D11DepthStencilView* dsv{};
			std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> om_uavs{};
			context->OMGetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), &dsv);
			// Pixel UAV slots share the eight OM output slots. Query them separately
			// with zero RTV outputs so the full [0,8) range is valid.
			context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0,
				static_cast<UINT>(om_uavs.size()), om_uavs.data());
			census.query_calls += 2;
			for (std::size_t slot{}; slot < rtvs.size(); ++slot)
			{
				if (rtvs[slot] == nullptr) continue;
				++eye.om_rtv_bindings_seen;
				if (!dispatch && program.ps != 0 &&
					pipeline.render_target_write_masks[slot] != 0)
				{
					++eye.om_rtv_bindings_admitted;
					hash_binding(bindings.outputs, bindings.output_count, 0,
						static_cast<std::uint8_t>(slot),
						reinterpret_cast<std::uintptr_t>(rtvs[slot]));
					note_view(rtvs[slot], true, access_site::om_rtv,
						static_cast<std::uint8_t>(slot), program, operation, caller,
						output_target_id, output_render_target_view,
						output_binding_sequence);
				}
				else
				{
					++eye.om_rtv_bindings_ignored;
					if (dispatch) ++census.rejected_dispatch_om;
					else if (program.ps == 0) ++census.rejected_null_shader;
					else ++census.rejected_no_color_write;
				}
			}
			for (std::size_t slot{}; slot < om_uavs.size(); ++slot)
			{
				if (om_uavs[slot] == nullptr) continue;
				++eye.om_uav_bindings_seen;
				if (!dispatch && program.ps != 0)
				{
					++eye.om_uav_bindings_admitted;
					hash_binding(bindings.unordered_access,
						bindings.unordered_access_count, 0,
						static_cast<std::uint8_t>(slot),
						reinterpret_cast<std::uintptr_t>(om_uavs[slot]));
					note_view(om_uavs[slot], true, access_site::om_uav,
						static_cast<std::uint8_t>(slot), program, operation, caller,
						output_target_id, output_render_target_view,
						output_binding_sequence);
				}
				else
				{
					++eye.om_uav_bindings_ignored;
					if (dispatch) ++census.rejected_dispatch_om;
					else ++census.rejected_null_shader;
				}
			}
			if (dsv)
			{
				++eye.om_dsv_bindings_seen;
				D3D11_DEPTH_STENCIL_VIEW_DESC description{};
				dsv->GetDesc(&description);
				++census.query_calls;
				const auto has_stencil = description.Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
					description.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
				const auto depth_writable = pipeline.depth_writes &&
					(description.Flags & D3D11_DSV_READ_ONLY_DEPTH) == 0;
				const auto stencil_writable = pipeline.stencil_writes && has_stencil &&
					(description.Flags & D3D11_DSV_READ_ONLY_STENCIL) == 0;
				const auto reads = !dispatch &&
					(pipeline.depth_reads || pipeline.stencil_reads);
				const auto writes = !dispatch && (depth_writable || stencil_writable);
				if (reads || writes)
				{
					++eye.om_dsv_bindings_admitted;
					hash_binding(bindings.outputs, bindings.output_count, 1, 0,
						reinterpret_cast<std::uintptr_t>(dsv));
				}
				else
				{
					++eye.om_dsv_bindings_ignored;
					if (dispatch) ++census.rejected_dispatch_om;
				}
				if (reads)
				{
					note_view(dsv, false, access_site::om_dsv, 0, program,
						operation, caller, output_target_id,
						output_render_target_view, output_binding_sequence);
				}
				if (writes)
				{
					note_view(dsv, true, access_site::om_dsv, 0, program,
						operation, caller, output_target_id,
						output_render_target_view, output_binding_sequence);
				}
			}
			release_all(rtvs); release_all(om_uavs); if (dsv) dsv->Release();
			std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> uavs{};
			context->CSGetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data());
			++census.query_calls;
			for (std::size_t slot{}; slot < uavs.size(); ++slot)
			{
				if (uavs[slot] == nullptr) continue;
				++eye.cs_uav_bindings_seen;
				if (dispatch && program.cs != 0)
				{
					++eye.cs_uav_bindings_admitted;
					hash_binding(bindings.unordered_access,
						bindings.unordered_access_count, 1,
						static_cast<std::uint8_t>(slot),
						reinterpret_cast<std::uintptr_t>(uavs[slot]));
					note_view(uavs[slot], true, access_site::cs_uav,
						static_cast<std::uint8_t>(slot), program, operation, caller,
						output_target_id, output_render_target_view,
						output_binding_sequence);
				}
				else
				{
					++eye.cs_uav_bindings_ignored;
					if (!dispatch) ++census.rejected_draw_cs;
					else ++census.rejected_null_shader;
				}
			}
			release_all(uavs);
		}
	}

	void note_dynamic_fx_arena_boundary(const std::uint64_t pair_id,
		const std::uint32_t output, const dynamic_fx_arena_phase phase,
		const std::uintptr_t owner_record, void* const backend_state) noexcept
	{
		if (observation_thread_id.load(std::memory_order_acquire) == 0) return;
		std::lock_guard lock(census_mutex);
		observe_dynamic_fx_arena_boundary_locked(pair_id, output, phase,
			owner_record, h2_dynamic_fx_global_data_pointer, backend_state);
	}

	void note_dynamic_fx_arena_boundary_for_test(const std::uint64_t pair_id,
		const std::uint32_t output, const dynamic_fx_arena_phase phase,
		const std::uintptr_t owner_record, const std::uintptr_t global_pointer_slot,
		void* const backend_state) noexcept
	{
		std::lock_guard lock(census_mutex);
		observe_dynamic_fx_arena_boundary_locked(pair_id, output, phase,
			owner_record, global_pointer_slot, backend_state);
	}

	bool begin_pair(const std::uint64_t pair_id,
		ID3D11DeviceContext* const context) noexcept
	{
		const auto resource_hooks = engine_stereo_resource_ops::get_status();
		if (!resource_hooks.hooks_installed || context == nullptr ||
			resource_hooks.expected_context != reinterpret_cast<std::uintptr_t>(context) ||
			resource_hooks.device_generation == 0)
		{
			return false;
		}
		std::lock_guard lock(census_mutex);
		if (census.current_state != state::idle || !pair_id || context == nullptr)
			return false;
		engine_stereo_constant_buffer_probe::set_history_tracking_enabled(true);
		if (!engine_stereo_constant_buffer_probe::begin_pair(pair_id, context,
			GetCurrentThreadId()))
		{
			engine_stereo_constant_buffer_probe::set_history_tracking_enabled(false);
			return false;
		}
		reset_large_object(census);
		reset_large_object(dynamic_fx_binding_working);
		dynamic_fx_content_capture_counts = {};
		left_dynamic_fx_family_ordinals.fill(0);
		left_dynamic_fx_arena_sequences.fill(0);
		previous_right_stream_entry = {};
		reflected_shaders.fill({});
		reflected_constant_buffer_count = 0;
		reflected_constant_variable_count = 0;
		srv_resource_descriptors.fill({});
		census.current_state = state::pair_active;
		census.pair_id = pair_id;
		census.ordered.snapshot_static_storage_bytes = sizeof(left_signatures) +
			sizeof(right_signature);
		census.resource_operation_hooks = resource_hooks;
		engine_stereo_output_merger::set_clear_observers(
			engine_stereo_output_merger::clear_observer_channel::gpu_census,
			observe_clear_render_target, observe_clear_depth_stencil);
		engine_stereo_execution::set_invocation_observer(
			engine_stereo_execution::invocation_observer_channel::gpu_census,
			observe_execution);
		engine_stereo_resource_ops::set_observer(
			engine_stereo_resource_ops::observer_channel::gpu_census,
			observe_resource_operation);
		census.context_identity = reinterpret_cast<std::uintptr_t>(context);
		census.owner_thread_id = GetCurrentThreadId();
		active_eye = 2; call_sequence = 0; census_context = context;
		arena_output_phases.fill(dynamic_fx_arena_output_phase::waiting_begin);
		latest_arena_view_copy_sequences.fill(0);
		foreign_thread_observations.store(0, std::memory_order_release);
		observation_thread_id.store(0, std::memory_order_release);
		return true;
	}
	bool begin_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		std::lock_guard lock(census_mutex);
		const auto expected_eye = census.completed_eye_mask == 0 ? 0u : 1u;
		if (census.current_state != state::pair_active || census.pair_id != pair_id ||
			eye >= 2 || eye != expected_eye || GetCurrentThreadId() != census.owner_thread_id ||
			(census.completed_eye_mask & (1u << eye)))
		{
			if (census.current_state == state::pair_active) fail_lifecycle_locked();
			return false;
		}
		if (!engine_stereo_constant_buffer_probe::begin_eye(pair_id, eye))
		{
			fail_lifecycle_locked();
			return false;
		}
		active_eye = eye; census.current_state = state::eye_active;
		observation_thread_id.store(census.owner_thread_id, std::memory_order_release);
		return true;
	}
	bool end_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		std::lock_guard lock(census_mutex);
		if (census.current_state != state::eye_active || census.pair_id != pair_id ||
			active_eye != eye || GetCurrentThreadId() != census.owner_thread_id)
		{
			fail_lifecycle_locked();
			return false;
		}
		if (!engine_stereo_constant_buffer_probe::end_eye(pair_id, eye))
		{
			fail_lifecycle_locked();
			return false;
		}
		census.completed_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		active_eye = 2; census.current_state = state::pair_active;
		observation_thread_id.store(0, std::memory_order_release);
		return true;
	}
	bool end_pair(const std::uint64_t pair_id) noexcept
	{
		std::lock_guard lock(census_mutex);
		if (census.current_state != state::pair_active || census.pair_id != pair_id ||
			census.completed_eye_mask != 3 || GetCurrentThreadId() != census.owner_thread_id)
		{
			fail_lifecycle_locked();
			return false;
		}
		if (!engine_stereo_constant_buffer_probe::end_pair(pair_id))
		{
			fail_lifecycle_locked();
			return false;
		}
		engine_stereo_constant_buffer_probe::get_report(
			census.constant_buffer_probe);
		finalize_dynamic_fx_stream();
		finalize_dynamic_fx_comparison();
		finalize_dynamic_fx_arena();
		census.resource_operation_hooks = engine_stereo_resource_ops::get_status();
		census.current_state = census.resource_overflows || census.depth_overflows ||
			census.observation_overflows || census.query_failures ||
			census.eyes[0].observations == 0 || census.eyes[1].observations == 0 ||
			census.constant_buffer_probe.current_state !=
				engine_stereo_constant_buffer_probe::state::complete ?
			state::failed : state::complete;
		census_context = nullptr;
		observation_thread_id.store(0, std::memory_order_release);
		detach_observers();
		return census.current_state == state::complete;
	}
	void observe(ID3D11DeviceContext* const context, const api operation,
		const std::uintptr_t caller, const std::uint32_t output_target_id,
		const std::uintptr_t output_render_target_view,
		const std::uint64_t output_binding_sequence,
		const invocation_arguments& arguments) noexcept
	{
		const auto expected_thread = observation_thread_id.load(std::memory_order_acquire);
		if (expected_thread == 0) return;
		if (GetCurrentThreadId() != expected_thread)
		{
			foreign_thread_observations.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		std::lock_guard lock(census_mutex);
		if (census.current_state != state::eye_active || active_eye >= 2) return;
		if (context != census_context)
		{
			++census.foreign_context_observations;
			return;
		}
		auto& eye = census.eyes[active_eye];
		if (eye.observations >= maximum_observations_per_eye)
		{
			record_overflow(census.observation_overflows);
			fail_lifecycle_locked();
			return;
		}
		++call_sequence;
		++eye.observations;
		if (operation == api::dispatch || operation == api::dispatch_indirect) ++eye.dispatch_calls;
		else ++eye.draw_calls;
		const auto ordinal = static_cast<std::size_t>(eye.observations - 1);
		auto& captured = active_eye == 0 ? left_signatures[ordinal] : right_signature;
		captured = {};
		auto& signature = captured.signature;
		auto& bindings = signature.bindings;
		const auto program = capture_program(context, eye, operation, bindings,
			captured.bindings, ordinal + 1, caller);
		const auto pipeline = is_dispatch(operation) ? pipeline_snapshot{} :
			capture_pipeline(context, eye);
		if (!is_dispatch(operation)) capture_viewports(context, eye);
		capture_resources(context, program, operation, caller, output_target_id,
			output_render_target_view, output_binding_sequence, pipeline, bindings,
			captured.bindings);
		if (captured.bindings.constant_buffer_dropped)
		{
			++census.ordered.constant_buffer_snapshot_truncations;
			census.ordered.constant_buffer_bindings_dropped +=
				captured.bindings.constant_buffer_dropped;
		}
		if (captured.bindings.shader_resource_dropped)
		{
			++census.ordered.shader_resource_snapshot_truncations;
			census.ordered.shader_resource_bindings_dropped +=
				captured.bindings.shader_resource_dropped;
			census.ordered.shader_resource_usage.classification_complete = false;
		}
		signature.operation = operation;
		signature.caller = caller;
		signature.arguments = arguments;
		if (signature.arguments.count > signature.arguments.values.size())
			signature.arguments.count = static_cast<std::uint8_t>(
				signature.arguments.values.size());
		signature.argument_hash = hash_arguments(signature.arguments);
		signature.program = program;
		signature.output_target_id = output_target_id;
		signature.pipeline = pipeline;
		const auto dynamic_fx_result = record_dynamic_fx_invocation(active_eye,
			ordinal + 1, operation, caller,
			signature.arguments, program, output_target_id, pipeline, bindings);
		if (operation == api::draw_indexed && signature.arguments.count >= 3)
		{
			engine_stereo_particle_buffer_probe::draw particle_draw{};
			if (dynamic_fx_result.semantic)
			{
				particle_draw.family = static_cast<std::uint8_t>(
					dynamic_fx_result.family);
			}
			particle_draw.output_ordinal = ordinal + 1;
			particle_draw.caller = caller;
			particle_draw.vertex_shader = program.vs;
			particle_draw.pixel_shader = program.ps;
			particle_draw.index_buffer = pipeline.index_buffer;
			particle_draw.vertex_buffer = pipeline.vertex_buffers[0].buffer;
			particle_draw.output_target_id = output_target_id;
			particle_draw.index_count = static_cast<std::uint32_t>(
				signature.arguments.values[0]);
			particle_draw.start_index = static_cast<std::uint32_t>(
				signature.arguments.values[1]);
			particle_draw.base_vertex = static_cast<std::int32_t>(
				signature.arguments.values[2]);
			particle_draw.index_format = pipeline.index_format;
			particle_draw.index_offset = pipeline.index_offset;
			particle_draw.vertex_stride = pipeline.vertex_buffers[0].stride;
			particle_draw.vertex_offset = pipeline.vertex_buffers[0].offset;
			engine_stereo_particle_buffer_probe::capture_draw(context, active_eye,
				particle_draw);
		}
		if (active_eye == 0)
		{
			left_dynamic_fx_family_ordinals[ordinal] =
				dynamic_fx_result.invocation.family_ordinal;
			left_dynamic_fx_arena_sequences[ordinal] =
				dynamic_fx_result.invocation.arena_view_copy_snapshot_sequence;
		}
		else
		{
			observe_dynamic_fx_stream_right(ordinal + 1, captured,
				dynamic_fx_result);
		}
		if (active_eye == 1) compare_ordered(ordinal, captured);
	}

	void observe_clear_render_target(ID3D11DeviceContext* const context,
		ID3D11RenderTargetView* const view, const std::uintptr_t caller,
		const std::uint32_t output_target_id,
		const std::uintptr_t output_render_target_view,
		const std::uint64_t output_binding_sequence) noexcept
	{
		const auto expected_thread = observation_thread_id.load(std::memory_order_acquire);
		if (!expected_thread || GetCurrentThreadId() != expected_thread) return;
		std::lock_guard lock(census_mutex);
		if (census.current_state != state::eye_active || active_eye >= 2 ||
			context != census_context || !view) return;
		++call_sequence;
		++census.eyes[active_eye].clear_render_target_calls;
		note_view(view, true, access_site::clear_rtv, 0, {},
			api::clear_render_target, caller, output_target_id,
			output_render_target_view, output_binding_sequence);
	}

	void observe_clear_depth_stencil(ID3D11DeviceContext* const context,
		ID3D11DepthStencilView* const view, const std::uint32_t clear_flags,
		const std::uintptr_t caller, const std::uint32_t output_target_id,
		const std::uintptr_t output_render_target_view,
		const std::uint64_t output_binding_sequence) noexcept
	{
		const auto expected_thread = observation_thread_id.load(std::memory_order_acquire);
		if (!expected_thread || GetCurrentThreadId() != expected_thread) return;
		std::lock_guard lock(census_mutex);
		if (census.current_state != state::eye_active || active_eye >= 2 ||
			context != census_context || !view) return;
		++call_sequence;
		auto& eye = census.eyes[active_eye];
		++eye.clear_depth_stencil_calls;
		if (clear_flags & D3D11_CLEAR_DEPTH) ++eye.clear_depth_calls;
		if (clear_flags & D3D11_CLEAR_STENCIL) ++eye.clear_stencil_calls;
		note_view(view, true, access_site::clear_dsv, 0, {},
			api::clear_depth_stencil, caller, output_target_id,
			output_render_target_view, output_binding_sequence);
	}
	state get_status() noexcept { std::lock_guard lock(census_mutex); return census.current_state; }
	void get_report(report& output) noexcept
	{
		std::lock_guard lock(census_mutex);
		if (census.current_state != state::complete &&
			census.current_state != state::failed)
		{
			census.resource_operation_hooks = engine_stereo_resource_ops::get_status();
			// Keep the focused VS b3 evidence in the same census snapshot.  Reading it
			// separately in diagnostics can straddle end_pair() and combine two
			// different capture epochs into one apparently coherent report.  Once the
			// census is terminal, preserve the exact report captured by end/fail rather
			// than grafting a later device generation onto an older eye pair.
			engine_stereo_constant_buffer_probe::get_report(
				census.constant_buffer_probe);
		}
		census.foreign_thread_observations = foreign_thread_observations.load(
			std::memory_order_acquire);
		output = census;
	}
	bool reset() noexcept
	{
		std::lock_guard lock(census_mutex);
		if (census.current_state == state::pair_active ||
			census.current_state == state::eye_active)
		{
			return false;
		}
		reset_large_object(census);
		reset_large_object(dynamic_fx_binding_working);
		left_dynamic_fx_family_ordinals.fill(0);
		left_dynamic_fx_arena_sequences.fill(0);
		previous_right_stream_entry = {};
		reflected_shaders.fill({});
		reflected_constant_buffer_count = 0;
		reflected_constant_variable_count = 0;
		srv_resource_descriptors.fill({});
		active_eye = 2;
		call_sequence = 0;
		census_context = nullptr;
		arena_output_phases.fill(dynamic_fx_arena_output_phase::waiting_begin);
		latest_arena_view_copy_sequences.fill(0);
		foreign_thread_observations.store(0, std::memory_order_relaxed);
		observation_thread_id.store(0, std::memory_order_release);
		detach_observers();
		return true;
	}

	void cancel() noexcept
	{
		std::lock_guard lock(census_mutex);
		if (census.current_state == state::pair_active ||
			census.current_state == state::eye_active)
		{
			fail_lifecycle_locked();
			return;
		}
		detach_observers();
	}
}
