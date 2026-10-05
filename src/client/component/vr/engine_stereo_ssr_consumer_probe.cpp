#include <std_include.hpp>

#include "engine_stereo_ssr_consumer_probe.hpp"

#include "component/d3d11.hpp"
#include "engine_stereo_execution.hpp"
#include "engine_stereo_output_merger.hpp"
#include "engine_stereo_resource_ops.hpp"
#include "native_conversion_command_list.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <new>

#pragma comment(lib, "d3dcompiler.lib")

namespace vr::engine_stereo_ssr_consumer_probe
{
	namespace
	{
		constexpr std::size_t maximum_reflected_shaders = 256;
		constexpr std::size_t maximum_scene_mip_candidate_shaders = 64;
		constexpr std::size_t maximum_shader_bytecode_bytes = 256 * 1024;
		constexpr UINT maximum_reflected_bindings = 1024;

		struct reflected_shader
		{
			Microsoft::WRL::ComPtr<ID3D11PixelShader> retained_shader{};
			std::uintptr_t identity{};
			bool attempted{};
			bool resolved{};
			bool reflection_resolved{};
			bool disassembly_resolved{};
			bool ssr_name_match{};
			std::uint64_t bytecode_hash{};
			std::uint64_t debug_name_hash{};
			std::array<char, maximum_shader_debug_name> debug_name{};
			engine_stereo_dxbc_declarations::shader_profile profile{
				engine_stereo_dxbc_declarations::shader_profile::unknown};
			std::array<shader_declaration, scanned_srv_slots> shader_resources{};
			std::array<shader_declaration, constant_buffer_slots> constant_buffers{};
		};

		std::mutex probe_mutex;
		std::mutex tracking_transition_mutex;
		report current_report{};
		std::array<reflected_shader, maximum_reflected_shaders> reflected_shaders{};
		std::array<std::atomic_uintptr_t, maximum_scene_mip_candidate_shaders>
			scene_mip_candidate_shader_identities{};
		thread_local std::array<std::byte, maximum_shader_bytecode_bytes>
			shader_bytecode_scratch{};
		std::atomic_bool installed{};
		std::atomic_bool observer_attached{};
		std::atomic_bool query_enabled{};
		std::atomic_bool content_tracking_active{};
		std::atomic_uintptr_t expected_context{};
		std::atomic_uint64_t expected_generation{};
		std::atomic_uint64_t lifecycle_epoch{1};
		std::atomic_uint64_t active_pair{};
		std::atomic_uint32_t owner_thread{};
		std::atomic_uint32_t active_eye{2};
		std::atomic_bool resource_write_tracking_active{};
		using resource_write_registry =
			engine_stereo_ssr_consumer_window::resource_write_window<
				maximum_resource_write_records>;
		resource_write_registry resource_writes{};
		std::size_t active_sample_baseline{};

		void reset_resource_writes_locked() noexcept
		{
			resource_writes.~resource_write_registry();
			::new (static_cast<void*>(&resource_writes)) resource_write_registry{};
		}

		void clear_reflected_shaders_locked() noexcept
		{
			for (auto& entry : reflected_shaders) entry = {};
		}

		void clear_scene_mip_candidate_shaders_locked() noexcept
		{
			for (auto& identity : scene_mip_candidate_shader_identities)
				identity.store(0, std::memory_order_release);
		}

		void publish_scene_mip_candidate_shader(
			const std::uintptr_t shader_identity) noexcept
		{
			if (shader_identity == 0) return;
			for (auto& entry : scene_mip_candidate_shader_identities)
			{
				auto current = entry.load(std::memory_order_acquire);
				if (current == shader_identity) return;
				if (current != 0) continue;
				if (entry.compare_exchange_strong(current, shader_identity,
					std::memory_order_release, std::memory_order_acquire)) return;
				if (current == shader_identity) return;
			}
		}

		void reset_report_locked() noexcept
		{
			// report is several MiB once detailed diagnostic capacity is included.
			// Reconstruct it in place so MSVC cannot materialize a same-sized RHS on
			// H2's render-thread stack.
			current_report.~report();
			::new (static_cast<void*>(&current_report)) report{};
			reset_resource_writes_locked();
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
			std::uint64_t& hash) noexcept
		{
			static_assert(Size > 1);
			destination = {};
			hash = 0;
			if (source == nullptr) return;
			const auto length = std::strlen(source);
			hash = hash_bytes(source, length);
			const auto copied = (std::min)(length, Size - 1);
			std::memcpy(destination.data(), source, copied);
		}

		[[nodiscard]] constexpr bool is_srv_input_type(
			const D3D_SHADER_INPUT_TYPE type) noexcept
		{
			return type == D3D_SIT_TBUFFER || type == D3D_SIT_TEXTURE ||
				type == D3D_SIT_STRUCTURED || type == D3D_SIT_BYTEADDRESS;
		}

		[[nodiscard]] bool ascii_contains_ssr(const char* const value) noexcept
		{
			if (value == nullptr) return false;
			for (std::size_t index{}; value[index] != '\0'; ++index)
			{
				const auto lower = [](const char character) noexcept
				{
					return character >= 'A' && character <= 'Z' ?
						static_cast<char>(character - 'A' + 'a') : character;
				};
				if (lower(value[index]) == 's' && value[index + 1] != '\0' &&
					lower(value[index + 1]) == 's' && value[index + 2] != '\0' &&
					lower(value[index + 2]) == 'r') return true;
			}
			return false;
		}

		void capture_shader_debug_name(ID3D11PixelShader* const shader,
			reflected_shader& output) noexcept
		{
			UINT size = static_cast<UINT>(output.debug_name.size());
			const auto result = shader->GetPrivateData(WKPDID_D3DDebugObjectName,
				&size, output.debug_name.data());
			if (FAILED(result) || result == DXGI_ERROR_MORE_DATA || size == 0 ||
				size >= output.debug_name.size())
			{
				output.debug_name = {};
				return;
			}
			output.debug_name_hash = hash_bytes(output.debug_name.data(), size);
			output.debug_name.back() = '\0';
		}

		[[nodiscard]] reflected_shader* find_or_reflect_shader(
			ID3D11PixelShader* const shader) noexcept
		{
			if (shader == nullptr) return nullptr;
			const auto identity = reinterpret_cast<std::uintptr_t>(shader);
			reflected_shader* available{};
			for (auto& entry : reflected_shaders)
			{
				if (entry.retained_shader.Get() == shader) return &entry;
				if (entry.retained_shader == nullptr && available == nullptr)
					available = &entry;
			}
			if (available == nullptr)
			{
				++current_report.reflection_cache_overflows;
				return nullptr;
			}

			auto& output = *available;
			output = {};
			output.retained_shader = shader;
			output.identity = identity;
			output.attempted = true;
			capture_shader_debug_name(shader, output);
			output.ssr_name_match = ascii_contains_ssr(output.debug_name.data());
			UINT bytecode_size = static_cast<UINT>(shader_bytecode_scratch.size());
			auto result = shader->GetPrivateData(d3d11::guid_shader_bytecode,
				&bytecode_size, shader_bytecode_scratch.data());
			if (result == DXGI_ERROR_MORE_DATA ||
				bytecode_size > shader_bytecode_scratch.size())
			{
				++current_report.bytecode_oversized;
				return &output;
			}
			if (FAILED(result) || bytecode_size == 0)
			{
				++current_report.bytecode_missing;
				return &output;
			}
			output.bytecode_hash = hash_bytes(reinterpret_cast<const char*>(
				shader_bytecode_scratch.data()), bytecode_size);

			ID3D11ShaderReflection* reflection{};
			result = D3DReflect(shader_bytecode_scratch.data(), bytecode_size,
				__uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(&reflection));
			if (SUCCEEDED(result) && reflection != nullptr)
			{
				const auto release_reflection = gsl::finally([reflection]() noexcept
				{
					reflection->Release();
				});
				D3D11_SHADER_DESC description{};
				result = reflection->GetDesc(&description);
				if (SUCCEEDED(result) &&
					D3D11_SHVER_GET_TYPE(description.Version) ==
						D3D11_SHVER_PIXEL_SHADER &&
					description.BoundResources <= maximum_reflected_bindings)
				{
					std::array<shader_declaration, scanned_srv_slots>
						reflected_resources{};
					std::array<shader_declaration, constant_buffer_slots>
						reflected_constant_buffers{};
					bool reflection_complete = true;
					if (description.BoundResources == 0)
						++current_report.reflection_empty;
					for (UINT index{}; index < description.BoundResources; ++index)
					{
						D3D11_SHADER_INPUT_BIND_DESC binding{};
						if (FAILED(reflection->GetResourceBindingDesc(index, &binding)))
						{
							++current_report.reflection_failures;
							reflection_complete = false;
							break;
						}
						const auto srv = is_srv_input_type(binding.Type);
						const auto constant_buffer = binding.Type == D3D_SIT_CBUFFER;
						if ((!srv && !constant_buffer) || binding.BindCount == 0) continue;
						shader_declaration declaration{};
						declaration.declared = true;
						declaration.declared_in_reflection = true;
						declaration.input_type = static_cast<std::uint32_t>(binding.Type);
						declaration.return_type = static_cast<std::uint32_t>(binding.ReturnType);
						declaration.dimension = static_cast<std::uint32_t>(binding.Dimension);
						declaration.bind_point = binding.BindPoint;
						declaration.bind_count = binding.BindCount;
						copy_text(binding.Name, declaration.name, declaration.name_hash);
						if (srv)
						{
							for (std::size_t slot{};
								slot < reflected_resources.size(); ++slot)
							{
								if (slot >= binding.BindPoint &&
									slot - binding.BindPoint < binding.BindCount)
									reflected_resources[slot] = declaration;
							}
						}
						else
						{
							for (std::size_t slot{};
								slot < reflected_constant_buffers.size(); ++slot)
							{
								if (slot >= binding.BindPoint &&
									slot - binding.BindPoint < binding.BindCount)
									reflected_constant_buffers[slot] = declaration;
							}
						}
					}
					if (reflection_complete)
					{
						output.shader_resources = reflected_resources;
						output.constant_buffers = reflected_constant_buffers;
						output.reflection_resolved = true;
					}
				}
				else
				{
					++current_report.reflection_failures;
				}
			}
			else
			{
				++current_report.reflection_failures;
			}

			++current_report.disassembly_attempts;
			ID3DBlob* disassembly{};
			result = D3DDisassemble(shader_bytecode_scratch.data(), bytecode_size,
				0, nullptr, &disassembly);
			if (SUCCEEDED(result) && disassembly != nullptr)
			{
				const auto release_disassembly = gsl::finally([disassembly]() noexcept
				{
					disassembly->Release();
				});
				engine_stereo_dxbc_declarations::declarations<scanned_srv_slots,
					constant_buffer_slots> declarations{};
				const auto parsed = engine_stereo_dxbc_declarations::parse(
					static_cast<const char*>(disassembly->GetBufferPointer()),
					disassembly->GetBufferSize(), declarations);
				output.profile = declarations.profile;
				current_report.disassembly_out_of_range_declarations +=
					declarations.out_of_range_declarations;
				current_report.disassembly_malformed_declarations +=
					declarations.malformed_declarations;
				if (parsed)
				{
					output.disassembly_resolved = true;
					for (std::size_t slot{}; slot < declarations.shader_resources.size();
						++slot)
					{
						if (!declarations.shader_resources[slot]) continue;
						auto& declaration = output.shader_resources[slot];
						declaration.declared_in_disassembly = true;
						if (declaration.declared) continue;
						declaration.declared = true;
						declaration.recovered_from_disassembly = true;
						declaration.input_type = UINT32_MAX;
						declaration.return_type = UINT32_MAX;
						declaration.dimension = UINT32_MAX;
						declaration.bind_point = static_cast<std::uint32_t>(slot);
						declaration.bind_count = 1;
						++current_report.disassembly_recovered_srv_bindings;
					}
					for (std::size_t slot{}; slot < declarations.constant_buffers.size();
						++slot)
					{
						if (!declarations.constant_buffers[slot]) continue;
						auto& declaration = output.constant_buffers[slot];
						declaration.declared_in_disassembly = true;
						if (declaration.declared) continue;
						declaration.declared = true;
						declaration.recovered_from_disassembly = true;
						declaration.input_type = static_cast<std::uint32_t>(D3D_SIT_CBUFFER);
						declaration.return_type = UINT32_MAX;
						declaration.dimension = UINT32_MAX;
						declaration.bind_point = static_cast<std::uint32_t>(slot);
						declaration.bind_count = 1;
						++current_report.disassembly_recovered_constant_buffer_bindings;
					}
				}
				else if (declarations.profile ==
					engine_stereo_dxbc_declarations::shader_profile::unsupported)
				{
					++current_report.disassembly_unsupported_profiles;
				}
				else
				{
					++current_report.disassembly_failures;
				}
			}
			else
			{
				if (disassembly != nullptr) disassembly->Release();
				++current_report.disassembly_failures;
			}
			for (const auto& declaration : output.shader_resources)
				output.resolved = output.resolved || declaration.declared;
			for (const auto& declaration : output.constant_buffers)
				output.resolved = output.resolved || declaration.declared;
			return &output;
		}

		void capture_srv_view_descriptor(ID3D11ShaderResourceView* const view,
			srv_view_descriptor& output) noexcept
		{
			if (view == nullptr) return;
			D3D11_SHADER_RESOURCE_VIEW_DESC description{};
			view->GetDesc(&description);
			output.format = static_cast<std::uint32_t>(description.Format);
			output.dimension = static_cast<std::uint32_t>(description.ViewDimension);
			switch (description.ViewDimension)
			{
			case D3D11_SRV_DIMENSION_BUFFER:
				output.first_element = description.Buffer.FirstElement;
				output.element_count = description.Buffer.NumElements;
				break;
			case D3D11_SRV_DIMENSION_BUFFEREX:
				output.first_element = description.BufferEx.FirstElement;
				output.element_count = description.BufferEx.NumElements;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE1D:
				output.most_detailed_mip = description.Texture1D.MostDetailedMip;
				output.mip_levels = description.Texture1D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
				output.most_detailed_mip = description.Texture1DArray.MostDetailedMip;
				output.mip_levels = description.Texture1DArray.MipLevels;
				output.first_array_slice = description.Texture1DArray.FirstArraySlice;
				output.array_size = description.Texture1DArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2D:
				output.most_detailed_mip = description.Texture2D.MostDetailedMip;
				output.mip_levels = description.Texture2D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
				output.most_detailed_mip = description.Texture2DArray.MostDetailedMip;
				output.mip_levels = description.Texture2DArray.MipLevels;
				output.first_array_slice = description.Texture2DArray.FirstArraySlice;
				output.array_size = description.Texture2DArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
				output.first_array_slice = description.Texture2DMSArray.FirstArraySlice;
				output.array_size = description.Texture2DMSArray.ArraySize;
				break;
			case D3D11_SRV_DIMENSION_TEXTURE3D:
				output.most_detailed_mip = description.Texture3D.MostDetailedMip;
				output.mip_levels = description.Texture3D.MipLevels;
				break;
			case D3D11_SRV_DIMENSION_TEXTURECUBE:
				output.most_detailed_mip = description.TextureCube.MostDetailedMip;
				output.mip_levels = description.TextureCube.MipLevels;
				output.array_size = 6;
				break;
			case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
				output.most_detailed_mip = description.TextureCubeArray.MostDetailedMip;
				output.mip_levels = description.TextureCubeArray.MipLevels;
				output.first_array_slice = description.TextureCubeArray.First2DArrayFace;
				output.array_size = description.TextureCubeArray.NumCubes * 6;
				break;
			default:
				break;
			}
		}

		void capture_resource_descriptor(ID3D11Resource* const resource,
			resource_descriptor& output) noexcept
		{
			if (resource == nullptr) return;
			output.captured = true;
			D3D11_RESOURCE_DIMENSION dimension{};
			resource->GetType(&dimension);
			output.dimension = static_cast<std::uint32_t>(dimension);
			if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
			{
				Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
				if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&buffer)))) return;
				D3D11_BUFFER_DESC description{};
				buffer->GetDesc(&description);
				output.byte_width = description.ByteWidth;
				output.usage = static_cast<std::uint32_t>(description.Usage);
				output.bind_flags = description.BindFlags;
				output.cpu_access_flags = description.CPUAccessFlags;
				output.misc_flags = description.MiscFlags;
				output.structure_byte_stride = description.StructureByteStride;
				output.valid = description.ByteWidth != 0;
				return;
			}
			if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
			{
				Microsoft::WRL::ComPtr<ID3D11Texture1D> texture;
				if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) return;
				D3D11_TEXTURE1D_DESC description{};
				texture->GetDesc(&description);
				output.width = description.Width;
				output.mip_levels = description.MipLevels;
				output.array_size = description.ArraySize;
				output.format = static_cast<std::uint32_t>(description.Format);
				output.usage = static_cast<std::uint32_t>(description.Usage);
				output.bind_flags = description.BindFlags;
				output.cpu_access_flags = description.CPUAccessFlags;
				output.misc_flags = description.MiscFlags;
				output.valid = description.Width != 0 && description.MipLevels != 0 &&
					description.ArraySize != 0;
				return;
			}
			if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
			{
				Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
				if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) return;
				D3D11_TEXTURE2D_DESC description{};
				texture->GetDesc(&description);
				output.width = description.Width;
				output.height = description.Height;
				output.mip_levels = description.MipLevels;
				output.array_size = description.ArraySize;
				output.format = static_cast<std::uint32_t>(description.Format);
				output.sample_count = description.SampleDesc.Count;
				output.sample_quality = description.SampleDesc.Quality;
				output.usage = static_cast<std::uint32_t>(description.Usage);
				output.bind_flags = description.BindFlags;
				output.cpu_access_flags = description.CPUAccessFlags;
				output.misc_flags = description.MiscFlags;
				output.valid = description.Width != 0 && description.Height != 0 &&
					description.MipLevels != 0 && description.ArraySize != 0 &&
					description.SampleDesc.Count != 0;
				return;
			}
			if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
			{
				Microsoft::WRL::ComPtr<ID3D11Texture3D> texture;
				if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) return;
				D3D11_TEXTURE3D_DESC description{};
				texture->GetDesc(&description);
				output.width = description.Width;
				output.height = description.Height;
				output.depth = description.Depth;
				output.mip_levels = description.MipLevels;
				output.format = static_cast<std::uint32_t>(description.Format);
				output.usage = static_cast<std::uint32_t>(description.Usage);
				output.bind_flags = description.BindFlags;
				output.cpu_access_flags = description.CPUAccessFlags;
				output.misc_flags = description.MiscFlags;
				output.valid = description.Width != 0 && description.Height != 0 &&
					description.Depth != 0 && description.MipLevels != 0;
			}
		}

		[[nodiscard]] bool is_material_copy_source_candidate(
			const resource_descriptor& destination,
			const resource_descriptor& source) noexcept
		{
			return destination.valid && source.valid &&
				destination.dimension == D3D11_RESOURCE_DIMENSION_BUFFER &&
				source.dimension == D3D11_RESOURCE_DIMENSION_BUFFER &&
				destination.byte_width == 1088 && source.byte_width == 1088 &&
				(destination.bind_flags & D3D11_BIND_CONSTANT_BUFFER) != 0 &&
				(source.bind_flags & D3D11_BIND_UNORDERED_ACCESS) != 0 &&
				(source.misc_flags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0 &&
				source.structure_byte_stride == 16;
		}

		[[nodiscard]] bool has_static_candidate_evidence(
			const reflected_shader* const shader) noexcept
		{
			return shader != nullptr && shader->bytecode_hash != 0 &&
				shader->disassembly_resolved && shader->ssr_name_match &&
				shader->shader_resources[scene_mip_srv_slot].declared_in_disassembly;
		}

		void record_static_candidate_rejection(
			const reflected_shader* const shader) noexcept
		{
			if (shader == nullptr) return;
			if (!shader->shader_resources[scene_mip_srv_slot].declared_in_disassembly)
			{
				++current_report.scene_mip_declaration_rejections;
			}
			else if (!shader->ssr_name_match)
			{
				++current_report.ssr_name_rejections;
			}
		}

		struct candidate_binding_observation
		{
			bool candidate{};
			bool start_tracking{};
		};

		// Report storage is allowed to fill before the bounded discovery window
		// completes. Keep the exact t10 binding proof independent from that storage.
		// The caller holds probe_mutex, so report and window updates stay atomic.
		[[nodiscard]] candidate_binding_observation observe_candidate_binding_only(
			ID3D11DeviceContext* const context, const reflected_shader& shader,
			const std::uint64_t pair_id, const std::uint32_t eye) noexcept
		{
			ID3D11ShaderResourceView* view{};
			context->PSGetShaderResources(static_cast<UINT>(scene_mip_srv_slot), 1,
				&view);
			const auto release_view = gsl::finally([view]() noexcept
			{
				if (view != nullptr) view->Release();
			});
			if (view == nullptr)
			{
				++current_report.scene_mip_binding_rejections;
				return {};
			}
			++current_report.direct_bound_srv_bindings;

			ID3D11Resource* resource{};
			view->GetResource(&resource);
			if (resource == nullptr)
			{
				++current_report.scene_mip_resource_rejections;
				return {};
			}
			const auto release_resource = gsl::finally([resource]() noexcept
			{
				resource->Release();
			});
			resource_descriptor descriptor{};
			capture_resource_descriptor(resource, descriptor);
			if (!descriptor.valid)
			{
				++current_report.scene_mip_resource_rejections;
				return {};
			}

			++current_report.scene_mip_candidates;
			publish_scene_mip_candidate_shader(shader.identity);
			(void)engine_stereo_ssr_consumer_window::note_consumer(
				current_report.window, pair_id, eye,
				static_cast<std::uintptr_t>(shader.bytecode_hash));
			return {true, !content_tracking_active.load(std::memory_order_acquire)};
		}

		template <typename Value, std::size_t Size>
		void release_all(std::array<Value*, Size>& values) noexcept
		{
			for (auto*& value : values)
			{
				if (value != nullptr) value->Release();
				value = nullptr;
			}
		}

		void note_view_write(ID3D11DeviceContext* const context,
			ID3D11View* const view,
			const engine_stereo_ssr_consumer_window::resource_write_operation operation,
			const std::uintptr_t caller,
			const std::uint32_t destination_subresource =
				engine_stereo_ssr_consumer_window::all_resource_subresources,
			const std::uint32_t output_target =
				engine_stereo_ssr_consumer_window::unknown_output_target) noexcept
		{
			if (view == nullptr || context == nullptr ||
				expected_context.load(std::memory_order_acquire) !=
					reinterpret_cast<std::uintptr_t>(context) ||
				owner_thread.load(std::memory_order_acquire) != GetCurrentThreadId())
			{
				return;
			}
			ID3D11Resource* resource{};
			view->GetResource(&resource);
			if (resource == nullptr) return;
			observe_resource_write(context, resource, operation, caller,
				destination_subresource, nullptr,
				engine_stereo_ssr_consumer_window::all_resource_subresources,
				output_target);
			resource->Release();
		}

		void observe_resource_operation(
			const engine_stereo_resource_ops::event& event) noexcept
		{
			if (!resource_write_tracking_active.load(std::memory_order_acquire) ||
				event.context == nullptr || expected_context.load(
					std::memory_order_acquire) != reinterpret_cast<std::uintptr_t>(
						event.context) || owner_thread.load(std::memory_order_acquire) !=
						GetCurrentThreadId()) return;
			using source_api = engine_stereo_resource_ops::api;
			using write_api =
				engine_stereo_ssr_consumer_window::resource_write_operation;
			write_api operation{write_api::unknown};
			auto destination_subresource =
				engine_stereo_ssr_consumer_window::all_resource_subresources;
			auto source_subresource =
				engine_stereo_ssr_consumer_window::all_resource_subresources;
			ID3D11Resource* destination = event.destination;
			ID3D11Resource* source = event.source;
			ID3D11Resource* view_resource{};
			if (event.view != nullptr) event.view->GetResource(&view_resource);
			const auto release_view_resource = gsl::finally([view_resource]() noexcept
			{
				if (view_resource != nullptr) view_resource->Release();
			});

			switch (event.operation)
			{
			case source_api::copy_subresource_region:
				operation = write_api::copy_subresource;
				destination_subresource = event.destination_subresource;
				source_subresource = event.source_subresource;
				break;
			case source_api::copy_resource:
				operation = write_api::copy_resource;
				break;
			case source_api::update_subresource:
				operation = write_api::update_subresource;
				destination_subresource = event.destination_subresource;
				break;
			case source_api::copy_structure_count:
				operation = write_api::copy_structure_count;
				// event.destination_subresource is a byte offset for this API, not a
				// subresource index. Keep the resource-level sentinel exact.
				source = view_resource;
				break;
			case source_api::clear_uav_uint:
			case source_api::clear_uav_float:
				operation = write_api::clear_unordered_access;
				destination = view_resource;
				break;
			case source_api::generate_mips:
				operation = write_api::generate_mips;
				destination = view_resource;
				break;
			case source_api::resolve_subresource:
				operation = write_api::resolve_subresource;
				destination_subresource = event.destination_subresource;
				source_subresource = event.source_subresource;
				break;
			default:
				return;
			}
			if (destination == nullptr) return;
			observe_resource_write(event.context, destination, operation, event.caller,
				destination_subresource, source, source_subresource);
		}

		void observe_clear_render_target(ID3D11DeviceContext* const context,
			ID3D11RenderTargetView* const view, const std::uintptr_t caller,
			const std::uint32_t, const std::uintptr_t, const std::uint64_t) noexcept
		{
			if (!resource_write_tracking_active.load(std::memory_order_acquire)) return;
			note_view_write(context, view,
				engine_stereo_ssr_consumer_window::resource_write_operation::
					clear_render_target,
				caller);
		}

		void observe_clear_depth_stencil(ID3D11DeviceContext* const context,
			ID3D11DepthStencilView* const view, const std::uint32_t,
			const std::uintptr_t caller, const std::uint32_t,
			const std::uintptr_t, const std::uint64_t) noexcept
		{
			if (!resource_write_tracking_active.load(std::memory_order_acquire)) return;
			note_view_write(context, view,
				engine_stereo_ssr_consumer_window::resource_write_operation::
					clear_depth_stencil,
				caller);
		}

		[[nodiscard]] constexpr bool is_dispatch_api(
			const engine_stereo_execution::api operation) noexcept
		{
			return operation == engine_stereo_execution::api::dispatch ||
				operation == engine_stereo_execution::api::dispatch_indirect;
		}

		[[nodiscard]] constexpr bool is_draw_api(
			const engine_stereo_execution::api operation) noexcept
		{
			return operation != engine_stereo_execution::api::execute_command_list &&
				!is_dispatch_api(operation);
		}

		[[nodiscard]] constexpr bool is_explicit_noop(
			const engine_stereo_execution::api operation,
			const std::array<std::uint64_t, 6>& arguments) noexcept
		{
			switch (operation)
			{
			case engine_stereo_execution::api::draw_indexed:
			case engine_stereo_execution::api::draw:
				return arguments[0] == 0;
			case engine_stereo_execution::api::draw_indexed_instanced:
			case engine_stereo_execution::api::draw_instanced:
				return arguments[0] == 0 || arguments[1] == 0;
			case engine_stereo_execution::api::dispatch:
				return arguments[0] == 0 || arguments[1] == 0 || arguments[2] == 0;
			default:
				return false;
			}
		}

		void record_execution_outputs(ID3D11DeviceContext* const context,
			const engine_stereo_execution::api operation,
			const std::uintptr_t caller, const std::uint32_t output_target) noexcept
		{
			if (!resource_write_tracking_active.load(std::memory_order_acquire) ||
				context == nullptr || expected_context.load(std::memory_order_acquire) !=
					reinterpret_cast<std::uintptr_t>(context) ||
				owner_thread.load(std::memory_order_acquire) != GetCurrentThreadId()) return;
			using write_api =
				engine_stereo_ssr_consumer_window::resource_write_operation;
			if (is_dispatch_api(operation))
			{
				ID3D11ComputeShader* shader{};
				context->CSGetShader(&shader, nullptr, nullptr);
				if (shader == nullptr) return;
				shader->Release();
				std::array<ID3D11UnorderedAccessView*,
					D3D11_PS_CS_UAV_REGISTER_COUNT> views{};
				context->CSGetUnorderedAccessViews(0, static_cast<UINT>(views.size()),
					views.data());
				for (auto* const view : views)
					note_view_write(context, view, write_api::dispatch_unordered_access,
						caller, engine_stereo_ssr_consumer_window::all_resource_subresources,
						output_target);
				release_all(views);
				return;
			}
			if (!is_draw_api(operation)) return;

			ID3D11PixelShader* pixel_shader{};
			context->PSGetShader(&pixel_shader, nullptr, nullptr);
			const auto pixel_shader_active = pixel_shader != nullptr;
			if (pixel_shader != nullptr) pixel_shader->Release();

			std::array<UINT8, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> write_masks{};
			write_masks.fill(D3D11_COLOR_WRITE_ENABLE_ALL);
			ID3D11BlendState* blend_state{};
			FLOAT blend_factor[4]{};
			UINT sample_mask{};
			context->OMGetBlendState(&blend_state, blend_factor, &sample_mask);
			if (blend_state != nullptr)
			{
				D3D11_BLEND_DESC description{};
				blend_state->GetDesc(&description);
				for (std::size_t slot{}; slot < write_masks.size(); ++slot)
				{
					const auto state_slot = description.IndependentBlendEnable ? slot : 0;
					write_masks[slot] =
						description.RenderTarget[state_slot].RenderTargetWriteMask;
				}
				blend_state->Release();
			}

			std::array<ID3D11RenderTargetView*,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets{};
			ID3D11DepthStencilView* depth_stencil{};
			context->OMGetRenderTargets(static_cast<UINT>(render_targets.size()),
				render_targets.data(), &depth_stencil);
			if (pixel_shader_active)
			{
				for (std::size_t slot{}; slot < render_targets.size(); ++slot)
				{
					if (write_masks[slot] == 0) continue;
					note_view_write(context, render_targets[slot],
						write_api::draw_render_target, caller,
						engine_stereo_ssr_consumer_window::all_resource_subresources,
						output_target);
				}
			}

			if (depth_stencil != nullptr)
			{
				bool depth_writable = true;
				bool stencil_writable{};
				ID3D11DepthStencilState* depth_state{};
				UINT stencil_reference{};
				context->OMGetDepthStencilState(&depth_state, &stencil_reference);
				if (depth_state != nullptr)
				{
					D3D11_DEPTH_STENCIL_DESC description{};
					depth_state->GetDesc(&description);
					depth_writable = description.DepthEnable &&
						description.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO;
					stencil_writable = description.StencilEnable &&
						description.StencilWriteMask != 0;
					depth_state->Release();
				}
				D3D11_DEPTH_STENCIL_VIEW_DESC view_description{};
				depth_stencil->GetDesc(&view_description);
				depth_writable = depth_writable &&
					(view_description.Flags & D3D11_DSV_READ_ONLY_DEPTH) == 0;
				stencil_writable = stencil_writable &&
					(view_description.Flags & D3D11_DSV_READ_ONLY_STENCIL) == 0;
				if (depth_writable || stencil_writable)
					note_view_write(context, depth_stencil,
						write_api::draw_depth_stencil, caller,
						engine_stereo_ssr_consumer_window::all_resource_subresources,
						output_target);
			}

			std::array<ID3D11UnorderedAccessView*,
				D3D11_PS_CS_UAV_REGISTER_COUNT> unordered_access{};
			if (pixel_shader_active)
			{
				context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0,
					static_cast<UINT>(unordered_access.size()), unordered_access.data());
				for (auto* const view : unordered_access)
					note_view_write(context, view, write_api::draw_unordered_access,
						caller, engine_stereo_ssr_consumer_window::all_resource_subresources,
						output_target);
			}
			release_all(render_targets);
			release_all(unordered_access);
			if (depth_stencil != nullptr) depth_stencil->Release();
		}

		void observe_execution(ID3D11DeviceContext* const context,
			const engine_stereo_execution::api operation, const std::uintptr_t caller,
			const std::uint32_t output_target, const std::uintptr_t,
			const std::uint64_t, const std::uint8_t,
			const std::array<std::uint64_t, 6>& arguments) noexcept
		{
			if (!resource_write_tracking_active.load(std::memory_order_acquire) ||
				context == nullptr) return;
			if (operation == engine_stereo_execution::api::execute_command_list)
			{
				const auto thread = GetCurrentThreadId();
				auto* const commands = reinterpret_cast<ID3D11CommandList*>(arguments[0]);
				const auto restores_context_state = arguments[1] != 0;
				const std::lock_guard lock(probe_mutex);
				if (resource_write_tracking_active.load(std::memory_order_acquire) &&
					expected_context.load(std::memory_order_acquire) ==
						reinterpret_cast<std::uintptr_t>(context) &&
					owner_thread.load(std::memory_order_acquire) == thread)
				{
					if (restores_context_state &&
						native_conversion_command_list::is_marked(commands))
					{
						// The tagged list only samples H2's resolved eye texture into a
						// VR-owned target and restores the complete immediate-context state.
						// It cannot invalidate producer history for H2-owned SSR inputs.
						++current_report.resource_write_native_conversion_lists;
					}
					else
					{
						// Every unmarked list remains a hard opaque barrier: it may hide
						// arbitrary resource writes, so older records cannot be decisive.
						reset_resource_writes_locked();
						++current_report.resource_write_opaque_barriers;
					}
				}
				return;
			}
			if (caller == exact_consumer_caller &&
				output_target == exact_consumer_target)
			{
				// The dedicated callback samples inputs first, then publishes this
				// draw's output as provenance for subsequent invocations.
				return;
			}
			if (is_explicit_noop(operation, arguments)) return;
			record_execution_outputs(context, operation, caller, output_target);
		}

		void detach_resource_write_observers() noexcept
		{
			engine_stereo_resource_ops::set_observer(
				engine_stereo_resource_ops::observer_channel::ssr_consumer, nullptr);
			engine_stereo_output_merger::set_clear_observers(
				engine_stereo_output_merger::clear_observer_channel::ssr_consumer,
				nullptr, nullptr);
			engine_stereo_execution::set_invocation_observer(
				engine_stereo_execution::invocation_observer_channel::ssr_consumer,
				nullptr);
		}

		void attach_resource_write_observers() noexcept
		{
			engine_stereo_resource_ops::set_observer(
				engine_stereo_resource_ops::observer_channel::ssr_consumer,
				observe_resource_operation);
			engine_stereo_output_merger::set_clear_observers(
				engine_stereo_output_merger::clear_observer_channel::ssr_consumer,
				observe_clear_render_target, observe_clear_depth_stencil);
			engine_stereo_execution::set_invocation_observer(
				engine_stereo_execution::invocation_observer_channel::ssr_consumer,
				observe_execution);
		}

		void stop_observation(const bool detach) noexcept
		{
			bool detach_observer{};
			{
				const std::lock_guard lock(probe_mutex);
				lifecycle_epoch.fetch_add(1, std::memory_order_acq_rel);
				query_enabled.store(false, std::memory_order_release);
				resource_write_tracking_active.store(false, std::memory_order_release);
				active_eye.store(2, std::memory_order_release);
				active_pair.store(0, std::memory_order_release);
				owner_thread.store(0, std::memory_order_release);
				detach_observer = detach && observer_attached.exchange(false,
					std::memory_order_acq_rel);
				if (detach)
				{
					// The cache retains shaders to make pointer identity stable while the
					// bounded observer is active. Release those references as soon as the
					// observer is detached; report samples own all metadata they expose.
					clear_reflected_shaders_locked();
				}
			}
			if (detach_observer)
			{
				engine_stereo_execution::set_draw_indexed_observer(nullptr);
			}
			if (detach) detach_resource_write_observers();
			const std::lock_guard tracking_lock(tracking_transition_mutex);
			if (content_tracking_active.exchange(false, std::memory_order_acq_rel))
			{
				engine_stereo_constant_buffer_probe::set_history_tracking_client(
					engine_stereo_constant_buffer_probe::history_tracking_client::
						ssr_consumer_probe, false);
			}
		}

		void start_content_tracking(const std::uint64_t expected_epoch) noexcept
		{
			const std::lock_guard tracking_lock(tracking_transition_mutex);
			if (!query_enabled.load(std::memory_order_acquire) ||
				lifecycle_epoch.load(std::memory_order_acquire) != expected_epoch)
			{
				return;
			}
			auto expected = false;
			if (!content_tracking_active.compare_exchange_strong(expected, true,
				std::memory_order_acq_rel, std::memory_order_acquire)) return;
			engine_stereo_constant_buffer_probe::set_history_tracking_client(
				engine_stereo_constant_buffer_probe::history_tracking_client::
					ssr_consumer_probe, true);
		}

		[[nodiscard]] const consumer_sample* find_captured_sample(
			const std::uint64_t pair_id,
			const std::uint32_t eye, const std::uintptr_t shader) noexcept
		{
			for (std::size_t index = active_sample_baseline;
				index < current_report.sample_count; ++index)
			{
				const auto& sample = current_report.samples[index];
				if (sample.pair_id == pair_id && sample.eye == eye &&
					sample.shader == shader)
				{
					return &sample;
				}
			}
			return nullptr;
		}

		[[nodiscard]] bool declared_constant_buffer_content_complete(
			const consumer_sample& sample) noexcept
		{
			bool declared{};
			for (const auto& buffer : sample.constant_buffers)
			{
				if (!buffer.declaration.declared) continue;
				declared = true;
				if (buffer.buffer == 0 || !buffer.content.known) return false;
			}
			return declared;
		}
	}

	bool install(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		if (context == nullptr || device_generation == 0) return false;
		const auto resource_operations = engine_stereo_resource_ops::get_status();
		const auto output_merger = engine_stereo_output_merger::get_status();
		if (!resource_operations.hooks_installed ||
			resource_operations.expected_context !=
				reinterpret_cast<std::uintptr_t>(context) ||
			resource_operations.device_generation != device_generation ||
			!output_merger.hook_installed || !output_merger.extended_hooks_installed ||
			output_merger.expected_context != reinterpret_cast<std::uintptr_t>(context) ||
			output_merger.device_generation != device_generation)
		{
			return false;
		}
		installed.store(false, std::memory_order_release);
		stop_observation(true);
		std::uint64_t install_epoch{};
		{
			const std::lock_guard lock(probe_mutex);
			reset_report_locked();
			clear_reflected_shaders_locked();
			clear_scene_mip_candidate_shaders_locked();
			active_sample_baseline = 0;
			current_report.expected_context = reinterpret_cast<std::uintptr_t>(context);
			current_report.device_generation = device_generation;
			install_epoch = lifecycle_epoch.load(std::memory_order_acquire);
		}
		expected_context.store(reinterpret_cast<std::uintptr_t>(context),
			std::memory_order_release);
		expected_generation.store(device_generation, std::memory_order_release);
		engine_stereo_execution::set_draw_indexed_observer(observe_draw_indexed);
		attach_resource_write_observers();
		bool committed{};
		{
			const std::lock_guard lock(probe_mutex);
			committed = lifecycle_epoch.load(std::memory_order_acquire) == install_epoch &&
				expected_context.load(std::memory_order_acquire) ==
					reinterpret_cast<std::uintptr_t>(context) &&
				expected_generation.load(std::memory_order_acquire) == device_generation;
			if (committed)
			{
				observer_attached.store(true, std::memory_order_release);
				installed.store(true, std::memory_order_release);
				current_report.installed = true;
				current_report.observer_attached = true;
			}
		}
		if (!committed)
		{
			engine_stereo_execution::set_draw_indexed_observer(nullptr);
			detach_resource_write_observers();
			return false;
		}
		return true;
	}

	void invalidate_device(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		{
			const std::lock_guard lock(probe_mutex);
			if (expected_context.load(std::memory_order_acquire) !=
					reinterpret_cast<std::uintptr_t>(context) ||
				expected_generation.load(std::memory_order_acquire) != device_generation)
			{
				return;
			}
			installed.store(false, std::memory_order_release);
			expected_context.store(0, std::memory_order_release);
			expected_generation.store(0, std::memory_order_release);
			query_enabled.store(false, std::memory_order_release);
			current_report.installed = false;
			current_report.observer_attached = false;
			current_report.content_tracking_active = false;
			current_report.owner_thread = 0;
			current_report.active_eye = 2;
			current_report.active_pair = 0;
			clear_scene_mip_candidate_shaders_locked();
		}
		stop_observation(true);
	}

	bool is_scene_mip_candidate_shader(
		ID3D11PixelShader* const shader) noexcept
	{
		const auto identity = reinterpret_cast<std::uintptr_t>(shader);
		if (identity == 0) return false;
		for (const auto& entry : scene_mip_candidate_shader_identities)
		{
			if (entry.load(std::memory_order_acquire) == identity) return true;
		}
		return false;
	}

	bool begin_pair(const std::uint64_t pair_id,
		ID3D11DeviceContext* const context, const std::uint64_t device_generation,
		const std::uint32_t pair_owner_thread,
		const bool temporal_history_seeded) noexcept
	{
		if (!installed.load(std::memory_order_acquire) || context == nullptr ||
			pair_owner_thread == 0 || expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context) ||
			expected_generation.load(std::memory_order_acquire) != device_generation)
		{
			return false;
		}
		bool admitted{};
		bool focused_admitted{};
		bool terminal{};
		{
			const std::lock_guard lock(probe_mutex);
			if (!installed.load(std::memory_order_acquire) || context == nullptr ||
				pair_owner_thread == 0 || expected_context.load(
					std::memory_order_acquire) != reinterpret_cast<std::uintptr_t>(context) ||
				expected_generation.load(std::memory_order_acquire) != device_generation)
			{
				return false;
			}
			if (current_report.window.current ==
				engine_stereo_ssr_consumer_window::phase::complete)
			{
				admitted = engine_stereo_ssr_consumer_window::begin_focused_pair(
					current_report.focused, pair_id, temporal_history_seeded);
				focused_admitted = admitted;
				terminal = engine_stereo_ssr_consumer_window::focused_terminal(
					current_report.focused);
				if (admitted)
				{
					// The focused report is deliberately only one candidate and one
					// pair. Retaining discovery's first-eye-heavy samples would make
					// the two-eye evidence ambiguous and needlessly huge.
					current_report.sample_count = 0;
					active_sample_baseline = 0;
				}
			}
			else
			{
				admitted = engine_stereo_ssr_consumer_window::begin_pair(
					current_report.window, pair_id, temporal_history_seeded);
				terminal = engine_stereo_ssr_consumer_window::terminal(
					current_report.window);
			}
			if (admitted)
			{
				if (focused_admitted) reset_resource_writes_locked();
				active_sample_baseline = current_report.sample_count;
				current_report.owner_thread = pair_owner_thread;
				current_report.active_pair = pair_id;
				current_report.active_eye = 2;
				owner_thread.store(pair_owner_thread, std::memory_order_release);
				active_pair.store(pair_id, std::memory_order_release);
				active_eye.store(2, std::memory_order_release);
				query_enabled.store(true, std::memory_order_release);
				resource_write_tracking_active.store(focused_admitted,
					std::memory_order_release);
			}
		}
		if (terminal)
		{
			stop_observation(true);
			return false;
		}
		if (!admitted) return false;
		return true;
	}

	bool begin_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		if (!query_enabled.load(std::memory_order_acquire) || eye >= 2 ||
			active_pair.load(std::memory_order_acquire) != pair_id ||
			owner_thread.load(std::memory_order_acquire) != GetCurrentThreadId())
		{
			return false;
		}
		bool accepted{};
		{
			const std::lock_guard lock(probe_mutex);
			if (!query_enabled.load(std::memory_order_acquire) ||
				active_pair.load(std::memory_order_acquire) != pair_id ||
				owner_thread.load(std::memory_order_acquire) != GetCurrentThreadId())
			{
				return false;
			}
			if (current_report.focused.current ==
				engine_stereo_ssr_consumer_window::focused_phase::pair_active)
			{
				accepted = engine_stereo_ssr_consumer_window::begin_focused_eye(
					current_report.focused, pair_id, eye);
			}
			else
			{
				accepted = engine_stereo_ssr_consumer_window::begin_eye(
					current_report.window, pair_id, eye);
			}
			if (accepted)
			{
				current_report.active_eye = eye;
				active_eye.store(eye, std::memory_order_release);
			}
		}
		return accepted;
	}

	void end_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		if (!query_enabled.load(std::memory_order_acquire) || eye >= 2 ||
			active_pair.load(std::memory_order_acquire) != pair_id) return;
		const std::lock_guard lock(probe_mutex);
		if (!query_enabled.load(std::memory_order_acquire) ||
			active_pair.load(std::memory_order_acquire) != pair_id ||
			active_eye.load(std::memory_order_acquire) != eye) return;
		if (current_report.focused.current ==
			engine_stereo_ssr_consumer_window::focused_phase::pair_active)
		{
			(void)engine_stereo_ssr_consumer_window::end_focused_eye(
				current_report.focused, pair_id, eye);
		}
		else
		{
			(void)engine_stereo_ssr_consumer_window::end_eye(
				current_report.window, pair_id, eye);
		}
		current_report.active_eye = 2;
		active_eye.store(2, std::memory_order_release);
	}

	void end_pair(const std::uint64_t pair_id, const bool successful) noexcept
	{
		if (active_pair.load(std::memory_order_acquire) != pair_id) return;
		bool terminal{};
		{
			const std::lock_guard lock(probe_mutex);
			if (active_pair.load(std::memory_order_acquire) != pair_id) return;
			query_enabled.store(false, std::memory_order_release);
			resource_write_tracking_active.store(false, std::memory_order_release);
			lifecycle_epoch.fetch_add(1, std::memory_order_acq_rel);
			const auto focused_pair = current_report.focused.current ==
				engine_stereo_ssr_consumer_window::focused_phase::pair_active;
			if (!successful) current_report.sample_count = active_sample_baseline;
			if (focused_pair)
			{
				(void)engine_stereo_ssr_consumer_window::end_focused_pair(
					current_report.focused, pair_id, successful);
				terminal = engine_stereo_ssr_consumer_window::focused_terminal(
					current_report.focused);
				if (!terminal)
				{
					// A stable shader permutation may legitimately be absent from an
					// individual pair. Retry with a clean two-eye sample rather than
					// combining observations from different frame families.
					current_report.sample_count = 0;
					active_sample_baseline = 0;
				}
			}
			else
			{
				(void)engine_stereo_ssr_consumer_window::end_pair(
					current_report.window, pair_id, successful);
				terminal = engine_stereo_ssr_consumer_window::terminal(
					current_report.window);
				if (current_report.window.current ==
					engine_stereo_ssr_consumer_window::phase::complete)
				{
					if (engine_stereo_ssr_consumer_window::arm_focused(
						current_report.focused,
						current_report.window.candidate_shader))
					{
						current_report.sample_count = 0;
						active_sample_baseline = 0;
						terminal = false;
					}
				}
			}
			current_report.owner_thread = 0;
			current_report.active_eye = 2;
			current_report.active_pair = 0;
			active_eye.store(2, std::memory_order_release);
			active_pair.store(0, std::memory_order_release);
			owner_thread.store(0, std::memory_order_release);
		}
		if (terminal) stop_observation(true);
	}

	void observe_resource_write(ID3D11DeviceContext* const context,
		ID3D11Resource* const destination,
		const engine_stereo_ssr_consumer_window::resource_write_operation operation,
		const std::uintptr_t caller,
		const std::uint32_t destination_subresource,
		ID3D11Resource* const source,
		const std::uint32_t source_subresource,
		const std::uint32_t output_target) noexcept
	{
		if (!resource_write_tracking_active.load(std::memory_order_acquire)) return;
		const auto thread = GetCurrentThreadId();
		const auto pair_id = active_pair.load(std::memory_order_acquire);
		const auto eye = active_eye.load(std::memory_order_acquire);
		const std::lock_guard lock(probe_mutex);
		++current_report.resource_write_callbacks;
		if (!resource_write_tracking_active.load(std::memory_order_acquire) ||
			context == nullptr || destination == nullptr ||
			expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context) ||
			owner_thread.load(std::memory_order_acquire) != thread || pair_id == 0 ||
			active_pair.load(std::memory_order_acquire) != pair_id || eye >= 2 ||
			active_eye.load(std::memory_order_acquire) != eye)
		{
			++current_report.resource_write_rejections;
			return;
		}

		engine_stereo_ssr_consumer_window::resource_write_metadata metadata{};
		metadata.pair_id = pair_id;
		metadata.context = reinterpret_cast<std::uintptr_t>(context);
		metadata.destination = reinterpret_cast<std::uintptr_t>(destination);
		metadata.source = reinterpret_cast<std::uintptr_t>(source);
		metadata.caller = caller;
		metadata.thread_id = thread;
		metadata.eye = eye;
		metadata.destination_subresource = destination_subresource;
		metadata.source_subresource = source_subresource;
		metadata.output_target = output_target;
		metadata.operation = operation;

		copy_source_lineage_sample lineage{};
		bool lineage_candidate{};
		if (operation == engine_stereo_ssr_consumer_window::
				resource_write_operation::copy_resource && source != nullptr)
		{
			capture_resource_descriptor(destination,
				lineage.destination_descriptor);
			capture_resource_descriptor(source, lineage.source_descriptor);
			lineage_candidate = is_material_copy_source_candidate(
				lineage.destination_descriptor, lineage.source_descriptor) &&
				engine_stereo_ssr_consumer_window::capture_copy_source_lineage(
					resource_writes, metadata, lineage.lineage);
		}
		const auto overflows_before = resource_writes.overflows;
		if (!engine_stereo_ssr_consumer_window::note_resource_write(
			resource_writes, metadata))
		{
			++current_report.resource_write_rejections;
			return;
		}
		++current_report.resource_write_records;
		current_report.resource_write_overflows +=
			resource_writes.overflows - overflows_before;
		if (lineage_candidate)
		{
			++current_report.copy_source_lineage_candidates;
			if (lineage.lineage.source_last_write_known)
				++current_report.copy_source_lineage_known;
			else
				++current_report.copy_source_lineage_unknown;
			if (current_report.copy_source_lineage_sample_count <
				current_report.copy_source_lineage_samples.size())
			{
				current_report.copy_source_lineage_samples[
					current_report.copy_source_lineage_sample_count++] = lineage;
			}
			else
			{
				++current_report.copy_source_lineage_overflows;
			}
		}
	}

	void observe_draw_indexed(ID3D11DeviceContext* const context,
		const std::uintptr_t caller, const std::uint32_t output_target) noexcept
	{
		// This branch is the permanent post-install hot path. Do not add counters,
		// locks or D3D queries before the exact executable call-site comparison.
		if (caller != exact_consumer_caller) return;
		if (!query_enabled.load(std::memory_order_acquire))
		{
			const std::lock_guard lock(probe_mutex);
			++current_report.inactive_rejections;
			return;
		}
		if (output_target != exact_consumer_target)
		{
			const std::lock_guard lock(probe_mutex);
			++current_report.target_rejections;
			return;
		}
		if (context == nullptr || expected_context.load(std::memory_order_acquire) !=
			reinterpret_cast<std::uintptr_t>(context))
		{
			const std::lock_guard lock(probe_mutex);
			++current_report.context_rejections;
			return;
		}
		const auto thread = GetCurrentThreadId();
		if (owner_thread.load(std::memory_order_acquire) != thread)
		{
			const std::lock_guard lock(probe_mutex);
			++current_report.thread_rejections;
			return;
		}
		const auto eye = active_eye.load(std::memory_order_acquire);
		const auto pair_id = active_pair.load(std::memory_order_acquire);
		const auto callback_epoch = lifecycle_epoch.load(std::memory_order_acquire);
		if (eye >= 2 || pair_id == 0)
		{
			const std::lock_guard lock(probe_mutex);
			++current_report.eye_rejections;
			return;
		}
		const auto publish_consumer_outputs = gsl::finally(
			[context, caller, output_target]() noexcept
		{
			record_execution_outputs(context,
				engine_stereo_execution::api::draw_indexed, caller, output_target);
		});

		ID3D11PixelShader* shader{};
		context->PSGetShader(&shader, nullptr, nullptr);
		if (shader == nullptr)
		{
			const std::lock_guard lock(probe_mutex);
			if (!query_enabled.load(std::memory_order_acquire) ||
				lifecycle_epoch.load(std::memory_order_acquire) != callback_epoch ||
				expected_context.load(std::memory_order_acquire) !=
					reinterpret_cast<std::uintptr_t>(context) ||
				owner_thread.load(std::memory_order_acquire) != thread ||
				active_pair.load(std::memory_order_acquire) != pair_id ||
				active_eye.load(std::memory_order_acquire) != eye)
			{
				++current_report.inactive_rejections;
				return;
			}
			++current_report.callback_entries;
			++current_report.shader_queries;
			++current_report.shader_missing;
			return;
		}
		const auto release_shader = gsl::finally([shader]() noexcept
		{
			shader->Release();
		});

		bool start_tracking{};
		{
			const std::lock_guard lock(probe_mutex);
			++current_report.callback_entries;
			++current_report.shader_queries;
			const auto shader_identity = reinterpret_cast<std::uintptr_t>(shader);
			auto* const reflected = find_or_reflect_shader(shader);
			const auto focused_capture = current_report.focused.current ==
				engine_stereo_ssr_consumer_window::focused_phase::pair_active;
			if (focused_capture && (reflected == nullptr ||
				reflected->bytecode_hash != current_report.focused.candidate_shader))
			{
				return;
			}
			const auto* const captured_sample = find_captured_sample(
				pair_id, eye, shader_identity);
			if (captured_sample != nullptr && captured_sample->scene_mip_candidate)
			{
				if (focused_capture)
				{
					(void)engine_stereo_ssr_consumer_window::note_focused_sample(
						current_report.focused, pair_id, eye,
						static_cast<std::uintptr_t>(captured_sample->bytecode_hash),
						declared_constant_buffer_content_complete(*captured_sample));
				}
				else
				{
					(void)engine_stereo_ssr_consumer_window::note_consumer(
						current_report.window, pair_id, eye,
						static_cast<std::uintptr_t>(captured_sample->bytecode_hash));
				}
				return;
			}
			if (focused_capture)
			{
				const auto observation = observe_candidate_binding_only(
					context, *reflected, pair_id, eye);
				if (!observation.candidate) return;
			}
			if (captured_sample != nullptr)
			{
				// The same shader may be rebound later in an eye with a different t10.
				// A negative first sample is diagnostic evidence, not a permanent veto.
				if (!has_static_candidate_evidence(reflected))
				{
					record_static_candidate_rejection(reflected);
					return;
				}
				const auto observation = observe_candidate_binding_only(
					context, *reflected, pair_id, eye);
				start_tracking = observation.start_tracking;
			}
			else
			{
			const auto observation_path =
				engine_stereo_ssr_consumer_window::select_observation_path(
					has_static_candidate_evidence(reflected),
					current_report.sample_count < current_report.samples.size());
			if (observation_path ==
				engine_stereo_ssr_consumer_window::observation_path::ignore)
			{
				++current_report.sample_overflows;
				record_static_candidate_rejection(reflected);
				return;
			}
			if (observation_path ==
				engine_stereo_ssr_consumer_window::observation_path::candidate_only)
			{
				++current_report.sample_overflows;
				const auto observation = observe_candidate_binding_only(
					context, *reflected, pair_id, eye);
				start_tracking = observation.start_tracking;
			}
			else
			{
				auto& sample = current_report.samples[current_report.sample_count++];
				sample = {};
				sample.pair_id = pair_id;
				sample.eye = eye;
				sample.output_target = output_target;
				sample.caller = caller;
				sample.shader = shader_identity;
				if (reflected != nullptr)
				{
					sample.bytecode_hash = reflected->bytecode_hash;
					sample.shader_debug_name_hash = reflected->debug_name_hash;
					sample.shader_debug_name = reflected->debug_name;
					sample.reflection_resolved = reflected->reflection_resolved;
					sample.disassembly_resolved = reflected->disassembly_resolved;
					sample.declarations_resolved = reflected->resolved;
					sample.ssr_name_match = reflected->ssr_name_match;
					sample.shader_profile = reflected->profile;
				}
				if (reflected == nullptr)
				{
					return;
				}

				std::array<ID3D11ShaderResourceView*, scanned_srv_slots> views{};
				std::array<ID3D11Buffer*, constant_buffer_slots> buffers{};
				context->PSGetShaderResources(0, static_cast<UINT>(views.size()),
					views.data());
				context->PSGetConstantBuffers(0, static_cast<UINT>(buffers.size()),
					buffers.data());
				const auto release_bindings = gsl::finally([&]() noexcept
				{
					release_all(views);
					release_all(buffers);
				});
				for (std::size_t slot{}; slot < reflected->shader_resources.size(); ++slot)
				{
					const auto& declaration = reflected->shader_resources[slot];
					auto& captured = sample.shader_resources[slot];
					captured.declaration = declaration;
					captured.slot = static_cast<std::uint32_t>(slot);
					if (declaration.declared)
					{
						++sample.declared_srv_count;
						++current_report.declared_srv_bindings;
					}
					auto* const view = views[slot];
					captured.view = reinterpret_cast<std::uintptr_t>(view);
					if (view == nullptr)
					{
						if (focused_capture && declaration.declared)
							++current_report.declared_srv_last_write_unknown;
						continue;
					}
					++sample.bound_srv_count;
					++current_report.direct_bound_srv_bindings;
					capture_srv_view_descriptor(view, captured.view_descriptor);
					ID3D11Resource* resource{};
					view->GetResource(&resource);
					if (resource != nullptr)
					{
						captured.resource = reinterpret_cast<std::uintptr_t>(resource);
						capture_resource_descriptor(resource,
							captured.resource_descriptor);
						resource->Release();
					}
					if (focused_capture && declaration.declared)
					{
						if (engine_stereo_ssr_consumer_window::find_last_resource_write(
							resource_writes, captured.resource, captured.last_write))
						{
							++current_report.declared_srv_last_write_known;
						}
						else
						{
							++current_report.declared_srv_last_write_unknown;
						}
					}
				}
				for (std::size_t slot{}; slot < reflected->constant_buffers.size(); ++slot)
				{
					const auto& declaration = reflected->constant_buffers[slot];
					auto& captured = sample.constant_buffers[slot];
					captured.declaration = declaration;
					captured.slot = static_cast<std::uint32_t>(slot);
					if (declaration.declared)
					{
						++sample.declared_constant_buffer_count;
						++current_report.declared_constant_buffer_bindings;
					}
					auto* const buffer = buffers[slot];
					captured.buffer = reinterpret_cast<std::uintptr_t>(buffer);
					if (buffer == nullptr) continue;
					++sample.bound_constant_buffer_count;
					++current_report.direct_bound_constant_buffer_bindings;
					D3D11_BUFFER_DESC description{};
					buffer->GetDesc(&description);
					captured.byte_width = description.ByteWidth;
					if (engine_stereo_constant_buffer_probe::query_content_snapshot(
						buffer, captured.content) && captured.content.known)
					{
						++current_report.content_known;
						captured.content_bytes_available =
							engine_stereo_constant_buffer_probe::query_content_bytes(
								buffer, captured.content.upload_generation,
								captured.content_bytes);
						if (captured.content_bytes_available)
							++current_report.content_bytes_known;
						else
							++current_report.content_bytes_unavailable;
					}
					else
					{
						++current_report.content_unknown;
					}
				}
				const auto& scene_mip =
					sample.shader_resources[scene_mip_srv_slot];
				if (!scene_mip.declaration.declared_in_disassembly)
				{
					++current_report.scene_mip_declaration_rejections;
					return;
				}
				if (scene_mip.view == 0)
				{
					++current_report.scene_mip_binding_rejections;
					return;
				}
				if (scene_mip.resource == 0 ||
					!scene_mip.resource_descriptor.valid)
				{
					++current_report.scene_mip_resource_rejections;
					return;
				}
				if (!sample.ssr_name_match)
				{
					++current_report.ssr_name_rejections;
					return;
				}
				if (!sample.disassembly_resolved || sample.bytecode_hash == 0)
				{
					return;
				}
				sample.scene_mip_candidate = true;
				++current_report.scene_mip_candidates;
				publish_scene_mip_candidate_shader(shader_identity);
				if (focused_capture)
				{
					(void)engine_stereo_ssr_consumer_window::note_focused_sample(
						current_report.focused, pair_id, eye,
						static_cast<std::uintptr_t>(sample.bytecode_hash),
						declared_constant_buffer_content_complete(sample));
				}
				else
				{
					(void)engine_stereo_ssr_consumer_window::note_consumer(
						current_report.window, pair_id, eye,
						static_cast<std::uintptr_t>(sample.bytecode_hash));
					start_tracking = !content_tracking_active.load(
						std::memory_order_acquire);
				}
			}
			}
		}
		if (start_tracking) start_content_tracking(callback_epoch);
	}

	void get_report(report& output) noexcept
	{
		const std::lock_guard lock(probe_mutex);
		output = current_report;
		output.installed = installed.load(std::memory_order_acquire);
		output.observer_attached = observer_attached.load(std::memory_order_acquire);
		output.content_tracking_active = content_tracking_active.load(
			std::memory_order_acquire);
		output.expected_context = expected_context.load(std::memory_order_acquire);
		output.device_generation = expected_generation.load(std::memory_order_acquire);
		output.owner_thread = owner_thread.load(std::memory_order_acquire);
		output.active_eye = active_eye.load(std::memory_order_acquire);
		output.active_pair = active_pair.load(std::memory_order_acquire);
	}
}
