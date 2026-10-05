#include <std_include.hpp>

#include "engine_stereo_effect_timeline.hpp"

#include "engine_stereo_constant_buffer_probe.hpp"
#include "engine_stereo_gpu_census.hpp"
#include "engine_stereo_ssr_consumer_probe.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <wrl/client.h>

namespace vr::engine_stereo_effect_timeline
{
	namespace
	{
		constexpr std::size_t record_size = 0x8090;
		constexpr std::size_t current_view_offset = 0x80;
		constexpr std::size_t current_view_size = 0x90;
		constexpr std::size_t history_view_offset = 0x2D40;
		constexpr std::size_t history_view_size = 0x90;
		constexpr std::size_t ssr_source_offset = 0xF90;
		constexpr std::size_t ssr_source_size = 0x80;
		constexpr std::size_t eye_offset = 0xA70;
		constexpr std::size_t ssr_previous_eye_offset = 0xFD0;
		constexpr std::size_t ssr_parameter_offset = 0xFE0;
		constexpr std::size_t ssr_clip_lookup_offset = 0xFF0;
		constexpr std::size_t ssr_clip_to_fade_offset = 0x1000;
		constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
		constexpr std::uint64_t fnv_prime = 1099511628211ull;
		static_assert(std::is_trivially_copyable_v<report>);

		static_assert(current_view_offset + current_view_size <= record_size);
		static_assert(history_view_offset + history_view_size <= record_size);
		static_assert(ssr_source_offset + ssr_source_size <= record_size);

		struct watched_resource
		{
			std::uintptr_t identity{};
			resource_writer_sample last_writer{};
		};

		std::atomic<state> current_state{state::idle};
		std::atomic_bool installed{};
		std::atomic_bool observers_attached{};
		std::atomic_bool history_tracking_active{};
		std::atomic_uintptr_t expected_context{};
		std::atomic_uint64_t expected_generation{};
		std::atomic_uint64_t arm_requests{};
		std::atomic_uint64_t arm_applications{};
		std::atomic_uint64_t mark_requests{};
		std::atomic_uint64_t mark_applications{};
		std::atomic_uint64_t pairs_started{};
		std::atomic_uint64_t pairs_completed{};
		std::atomic_uint64_t pairs_incomplete{};
		std::atomic_uint64_t pairs_overwritten{};
		std::atomic_uint64_t invocation_callbacks{};
		std::atomic_uint64_t resource_callbacks{};
		std::atomic_uint64_t clear_callbacks{};
		std::atomic_uint64_t foreign_thread_callbacks{};
		std::atomic_uint64_t foreign_context_callbacks{};
		std::atomic_uint64_t watched_resource_count{};
		std::atomic_uint64_t watched_resource_overflows{};
		std::atomic_uint64_t watched_resource_writes{};
		std::atomic_uint64_t compared_stream_overflows{};
		std::atomic_uint64_t capture_first_pair{};
		std::atomic_uint64_t capture_last_pair{};
		std::atomic_uint64_t frozen_pair{};

		// These fields have exactly one writer: the proven H2 scene-owner thread.
		std::array<pair_sample, maximum_pairs> ring{};
		std::array<watched_resource, maximum_watched_resources> watched_resources{};
		std::size_t ring_write_index{};
		std::size_t ring_count{};
		std::size_t watched_count{};
		std::uint64_t capture_sequence{};
		std::uint64_t resource_write_sequence{};
		pair_sample* active_sample{};
		std::uint64_t active_pair{};
		std::uint32_t active_eye{2};
		std::atomic_uint32_t active_owner_thread{};
		std::atomic_uintptr_t active_context_identity{};
		const void* active_record{};
		struct captured_invocation
		{
			invocation_fingerprint fingerprint{};
			std::array<shader_resource_binding_sample,
				sampled_dynamic_srv_slots> shader_resources{};
			std::array<constant_buffer_binding_sample,
				sampled_dynamic_constant_buffer_slots> constant_buffers{};
		};
		using invocation_stream = std::array<captured_invocation,
			maximum_compared_invocations>;
		std::array<std::array<invocation_stream, compared_stream_count>, 2>
			compared_invocations{};
		std::array<std::array<std::uint32_t, compared_stream_count>, 2>
			compared_invocation_counts{};
		std::array<std::array<bool, compared_stream_count>, 2>
			compared_invocation_overflows{};

		// The ring is normalized into this immutable snapshot before state=frozen is
		// published. The mutex covers the rare re-arm/freeze versus vr_status race.
		std::mutex publication_mutex;
		report published_report{};

		[[nodiscard]] bool capture_active(const state value) noexcept
		{
			return value == state::capturing || value == state::mark_pending;
		}

		void hash_bytes(std::uint64_t& hash, const void* const data,
			const std::size_t size) noexcept
		{
			const auto* bytes = static_cast<const std::uint8_t*>(data);
			for (std::size_t index{}; index < size; ++index)
			{
				hash ^= bytes[index];
				hash *= fnv_prime;
			}
		}

		template <typename Value>
		void hash_value(std::uint64_t& hash, const Value& value) noexcept
		{
			hash_bytes(hash, &value, sizeof(value));
		}

		[[nodiscard]] std::uint64_t canonical_srv_descriptor_hash(
			const D3D11_SHADER_RESOURCE_VIEW_DESC& descriptor) noexcept
		{
			auto hash = fnv_offset;
			hash_value(hash, descriptor.Format);
			hash_value(hash, descriptor.ViewDimension);
			switch (descriptor.ViewDimension)
			{
			case D3D11_SRV_DIMENSION_BUFFER:
				hash_value(hash, descriptor.Buffer.FirstElement);
				hash_value(hash, descriptor.Buffer.NumElements);
				break;
			case D3D11_SRV_DIMENSION_TEXTURE1D:
				hash_value(hash, descriptor.Texture1D.MostDetailedMip);
				hash_value(hash, descriptor.Texture1D.MipLevels);
				break;
			case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
				hash_value(hash, descriptor.Texture1DArray.MostDetailedMip);
				hash_value(hash, descriptor.Texture1DArray.MipLevels);
				hash_value(hash, descriptor.Texture1DArray.FirstArraySlice);
				hash_value(hash, descriptor.Texture1DArray.ArraySize);
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2D:
				hash_value(hash, descriptor.Texture2D.MostDetailedMip);
				hash_value(hash, descriptor.Texture2D.MipLevels);
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
				hash_value(hash, descriptor.Texture2DArray.MostDetailedMip);
				hash_value(hash, descriptor.Texture2DArray.MipLevels);
				hash_value(hash, descriptor.Texture2DArray.FirstArraySlice);
				hash_value(hash, descriptor.Texture2DArray.ArraySize);
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DMS:
				break;
			case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
				hash_value(hash, descriptor.Texture2DMSArray.FirstArraySlice);
				hash_value(hash, descriptor.Texture2DMSArray.ArraySize);
				break;
			case D3D11_SRV_DIMENSION_TEXTURE3D:
				hash_value(hash, descriptor.Texture3D.MostDetailedMip);
				hash_value(hash, descriptor.Texture3D.MipLevels);
				break;
			case D3D11_SRV_DIMENSION_TEXTURECUBE:
				hash_value(hash, descriptor.TextureCube.MostDetailedMip);
				hash_value(hash, descriptor.TextureCube.MipLevels);
				break;
			case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
				hash_value(hash, descriptor.TextureCubeArray.MostDetailedMip);
				hash_value(hash, descriptor.TextureCubeArray.MipLevels);
				hash_value(hash, descriptor.TextureCubeArray.First2DArrayFace);
				hash_value(hash, descriptor.TextureCubeArray.NumCubes);
				break;
			case D3D11_SRV_DIMENSION_BUFFEREX:
				hash_value(hash, descriptor.BufferEx.FirstElement);
				hash_value(hash, descriptor.BufferEx.NumElements);
				hash_value(hash, descriptor.BufferEx.Flags);
				break;
			default:
				break;
			}
			return hash;
		}

		template <std::size_t Count>
		void read_words(const std::uint8_t* const record, const std::size_t offset,
			std::array<std::uint32_t, Count>& output) noexcept
		{
			static_assert(Count * sizeof(std::uint32_t) <= record_size);
			std::memcpy(output.data(), record + offset,
				Count * sizeof(std::uint32_t));
		}

		[[nodiscard]] record_snapshot capture_record(const void* const value) noexcept
		{
			record_snapshot output{};
			if (value == nullptr) return output;
			const auto* const record = static_cast<const std::uint8_t*>(value);
			output.valid = true;
			output.record = reinterpret_cast<std::uintptr_t>(record);
			output.current_view_hash = fnv_offset;
			hash_bytes(output.current_view_hash, record + current_view_offset,
				current_view_size);
			output.history_view_hash = fnv_offset;
			hash_bytes(output.history_view_hash, record + history_view_offset,
				history_view_size);
			output.ssr_source_hash = fnv_offset;
			hash_bytes(output.ssr_source_hash, record + ssr_source_offset,
				ssr_source_size);
			read_words(record, eye_offset, output.eye_offset_bits);
			read_words(record, ssr_previous_eye_offset, output.ssr_previous_eye_bits);
			read_words(record, ssr_parameter_offset, output.ssr_parameter_bits);
			read_words(record, ssr_clip_lookup_offset, output.ssr_clip_lookup_bits);
			read_words(record, ssr_clip_to_fade_offset,
				output.ssr_clip_to_fade_bits);
			return output;
		}

		template <typename Values>
		void release_all(Values& values) noexcept
		{
			for (auto*& value : values)
			{
				if (value != nullptr) value->Release();
				value = nullptr;
			}
		}

		[[nodiscard]] constant_buffer_sample capture_constant_buffer(
			ID3D11Buffer* const buffer) noexcept
		{
			constant_buffer_sample output{};
			output.buffer = reinterpret_cast<std::uintptr_t>(buffer);
			if (buffer == nullptr) return output;
			engine_stereo_constant_buffer_probe::content_snapshot content{};
			if (!engine_stereo_constant_buffer_probe::query_content_snapshot(
				buffer, content)) return output;
			output.upload_caller = content.upload_caller;
			output.upload_generation = content.upload_generation;
			output.hash_low = content.hash_low;
			output.hash_high = content.hash_high;
			output.byte_width = content.byte_width;
			output.source = static_cast<std::uint8_t>(content.source);
			output.known = content.known;
			return output;
		}

		[[nodiscard]] resource_writer_sample find_last_writer(
			const std::uintptr_t resource) noexcept
		{
			for (std::size_t index{}; index < watched_count; ++index)
			{
				if (watched_resources[index].identity == resource)
					return watched_resources[index].last_writer;
			}
			return {};
		}

		void watch_resource(const std::uintptr_t resource) noexcept
		{
			if (resource == 0) return;
			for (std::size_t index{}; index < watched_count; ++index)
			{
				if (watched_resources[index].identity == resource) return;
			}
			if (watched_count >= watched_resources.size())
			{
				watched_resource_overflows.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			watched_resources[watched_count++].identity = resource;
			watched_resource_count.store(watched_count, std::memory_order_release);
		}

		[[nodiscard]] std::size_t tracked_target_index(
			const std::uint32_t target_id) noexcept
		{
			for (std::size_t index{}; index < tracked_target_ids.size(); ++index)
			{
				if (tracked_target_ids[index] == target_id) return index;
			}
			return tracked_target_ids.size();
		}

		[[nodiscard]] std::uintptr_t query_view_resource(
			ID3D11View* const view) noexcept
		{
			if (view == nullptr) return 0;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			view->GetResource(&resource);
			return reinterpret_cast<std::uintptr_t>(resource.Get());
		}

		[[nodiscard]] std::uintptr_t note_output_view(
			target_lifecycle_sample& target, const std::uintptr_t view_identity) noexcept
		{
			if (view_identity == 0) return 0;
			for (std::size_t index{}; index < target.output_view_count; ++index)
			{
				const auto& observed = target.output_views[index];
				if (observed.view == view_identity) return observed.resource;
			}

			auto* const view = reinterpret_cast<ID3D11RenderTargetView*>(view_identity);
			const auto resource = query_view_resource(view);
			if (resource == 0) ++target.output_resource_query_failures;
			if (target.output_view_count >= target.output_views.size())
			{
				++target.output_view_overflow;
				return resource;
			}
			target.output_views[target.output_view_count++] = {view_identity, resource};
			return resource;
		}

		void observe_target_invocation(eye_sample& eye,
			const engine_stereo_execution::api operation,
			const std::uint32_t output_target, const std::uintptr_t output_rtv,
			const std::uint64_t event_sequence) noexcept
		{
			const auto index = tracked_target_index(output_target);
			if (index >= eye.targets.size()) return;
			auto& target = eye.targets[index];
			target.target_id = output_target;
			++target.invocation_calls;
			const auto operation_index = static_cast<std::size_t>(operation);
			if (operation_index < target.per_api.size())
				++target.per_api[operation_index];
			const auto resource = note_output_view(target, output_rtv);
			if (target.first_invocation_event == 0)
			{
				target.first_invocation_event = event_sequence;
				target.first_output_view = output_rtv;
				target.first_output_resource = resource;
			}
			target.last_invocation_event = event_sequence;
			target.last_output_view = output_rtv;
			target.last_output_resource = resource;
		}

		[[nodiscard]] bool target_uses_resource(
			const target_lifecycle_sample& target,
			const std::uintptr_t resource) noexcept
		{
			if (resource == 0) return false;
			for (std::size_t index{}; index < target.output_view_count; ++index)
			{
				if (target.output_views[index].resource == resource) return true;
			}
			return false;
		}

		void finalize_target_lifecycles(pair_sample& sample) noexcept
		{
			for (auto& eye : sample.eyes)
			{
				for (std::size_t target_index{};
					target_index < eye.targets.size(); ++target_index)
				{
					auto& target = eye.targets[target_index];
					target.target_id = tracked_target_ids[target_index];
					for (std::size_t clear_index{};
						clear_index < eye.clear_event_count; ++clear_index)
					{
						const auto& clear = eye.clear_events[clear_index];
						const auto binding_match =
							clear.binding_target == target.target_id;
						const auto resource_match =
							target_uses_resource(target, clear.clear_resource);
						if (!binding_match && !resource_match) continue;
						if (binding_match) ++target.binding_clear_calls;
						if (resource_match) ++target.resource_clear_calls;
						if (binding_match && resource_match)
							++target.exact_clear_calls;
						else
							++target.mismatched_clear_calls;
						if (target.first_clear_event == 0)
						{
							target.first_clear_event = clear.event_sequence;
							target.first_clear_view = clear.clear_view;
							target.first_clear_resource = clear.clear_resource;
						}
						target.last_clear_event = clear.event_sequence;
						target.last_clear_view = clear.clear_view;
						target.last_clear_resource = clear.clear_resource;
					}
				}
			}
		}

		void capture_pipeline(ID3D11DeviceContext* const context,
			const std::uintptr_t caller, draw_fingerprint& output) noexcept
		{
			output = {};
			if (context == nullptr) return;
			output.captured = true;
			output.caller = caller;
			output.source = capture_record(active_record);

			ID3D11PixelShader* shader{};
			context->PSGetShader(&shader, nullptr, nullptr);
			output.pixel_shader = reinterpret_cast<std::uintptr_t>(shader);
			if (shader != nullptr) shader->Release();

			ID3D11BlendState* blend{};
			std::array<float, 4> blend_factor{};
			context->OMGetBlendState(&blend, blend_factor.data(), &output.sample_mask);
			output.blend_state = reinterpret_cast<std::uintptr_t>(blend);
			if (blend != nullptr) blend->Release();
			std::memcpy(output.blend_factor_bits.data(), blend_factor.data(),
				sizeof(blend_factor));

			std::array<ID3D11ShaderResourceView*, sampled_dynamic_srv_slots> srvs{};
			context->PSGetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			output.shader_resource_hash = fnv_offset;
			for (auto* const srv : srvs)
			{
				const auto identity = reinterpret_cast<std::uintptr_t>(srv);
				hash_value(output.shader_resource_hash, identity);
			}
			release_all(srvs);

			std::array<ID3D11SamplerState*, sampled_dynamic_sampler_slots> samplers{};
			context->PSGetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
			output.sampler_hash = fnv_offset;
			for (const auto* const sampler : samplers)
			{
				const auto identity = reinterpret_cast<std::uintptr_t>(sampler);
				hash_value(output.sampler_hash, identity);
			}
			release_all(samplers);

			std::array<ID3D11Buffer*, sampled_dynamic_constant_buffer_slots> buffers{};
			context->PSGetConstantBuffers(0, static_cast<UINT>(buffers.size()),
				buffers.data());
			output.constant_buffer_hash = fnv_offset;
			for (auto* const buffer : buffers)
			{
				const auto content = capture_constant_buffer(buffer);
				hash_value(output.constant_buffer_hash, content.buffer);
				hash_value(output.constant_buffer_hash, content.upload_generation);
				hash_value(output.constant_buffer_hash, content.hash_low);
				hash_value(output.constant_buffer_hash, content.hash_high);
				if (buffer == nullptr) continue;
				if (content.known) ++output.constant_buffers_known;
				else ++output.constant_buffers_unknown;
			}
			release_all(buffers);
		}

		void capture_ssr_consumer(ID3D11DeviceContext* const context,
			ssr_consumer_sample& output) noexcept
		{
			++output.calls;
			output.source = capture_record(active_record);

			ID3D11PixelShader* shader{};
			context->PSGetShader(&shader, nullptr, nullptr);
			output.pixel_shader = reinterpret_cast<std::uintptr_t>(shader);
			if (shader != nullptr) shader->Release();

			ID3D11BlendState* blend{};
			std::array<float, 4> blend_factor{};
			context->OMGetBlendState(&blend, blend_factor.data(), &output.sample_mask);
			output.blend_state = reinterpret_cast<std::uintptr_t>(blend);
			if (blend != nullptr) blend->Release();
			std::memcpy(output.blend_factor_bits.data(), blend_factor.data(),
				sizeof(blend_factor));

			std::array<ID3D11ShaderResourceView*, ssr_srv_slots> srvs{};
			context->PSGetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			output.shader_resource_hash = fnv_offset;
			output.writer_known = 0;
			output.writer_unknown = 0;
			for (std::size_t slot{}; slot < srvs.size(); ++slot)
			{
				auto& sample = output.shader_resources[slot];
				sample = {};
				sample.view = reinterpret_cast<std::uintptr_t>(srvs[slot]);
				Microsoft::WRL::ComPtr<ID3D11Resource> resource;
				if (srvs[slot] != nullptr) srvs[slot]->GetResource(&resource);
				sample.resource = reinterpret_cast<std::uintptr_t>(resource.Get());
				const auto tracks_history_writer = slot == 10 || slot == 13;
				if (tracks_history_writer)
					sample.last_writer = find_last_writer(sample.resource);
				if (sample.resource != 0 && tracks_history_writer)
				{
					if (sample.last_writer.known) ++output.writer_known;
					else ++output.writer_unknown;
					watch_resource(sample.resource);
				}
				hash_value(output.shader_resource_hash, sample.view);
				hash_value(output.shader_resource_hash, sample.resource);
			}
			release_all(srvs);

			std::array<ID3D11Buffer*, ssr_constant_buffer_slots> buffers{};
			context->PSGetConstantBuffers(0, static_cast<UINT>(buffers.size()),
				buffers.data());
			output.constant_buffer_hash = fnv_offset;
			for (std::size_t slot{}; slot < buffers.size(); ++slot)
			{
				output.constant_buffers[slot] = capture_constant_buffer(buffers[slot]);
				const auto& sample = output.constant_buffers[slot];
				hash_value(output.constant_buffer_hash, sample.buffer);
				hash_value(output.constant_buffer_hash, sample.hash_low);
				hash_value(output.constant_buffer_hash, sample.hash_high);
			}
			release_all(buffers);
		}

		[[nodiscard]] std::size_t dynamic_index(
			const engine_stereo_gpu_census::dynamic_fx_family family) noexcept
		{
			switch (family)
			{
			case engine_stereo_gpu_census::dynamic_fx_family::code_trans: return 0;
			case engine_stereo_gpu_census::dynamic_fx_family::glass: return 1;
			case engine_stereo_gpu_census::dynamic_fx_family::mark: return 2;
			case engine_stereo_gpu_census::dynamic_fx_family::spark: return 3;
			default: return dynamic_family_count;
			}
		}

		[[nodiscard]] compared_stream compared_stream_for_dynamic(
			const std::size_t family_index) noexcept
		{
			return static_cast<compared_stream>(family_index + 1);
		}

		[[nodiscard]] bool invocation_equal(const invocation_fingerprint& left,
			const invocation_fingerprint& right) noexcept
		{
			if (left.operation != right.operation || left.caller != right.caller ||
				left.output_target != right.output_target ||
				left.argument_count != right.argument_count)
			{
				return false;
			}
			const auto count = (std::min)(static_cast<std::size_t>(left.argument_count),
				left.arguments.size());
			return std::equal(left.arguments.begin(), left.arguments.begin() + count,
				right.arguments.begin());
		}

		void capture_invocation_state(ID3D11DeviceContext* const context,
			captured_invocation& captured) noexcept
		{
			if (context == nullptr) return;
			auto& output = captured.fingerprint;

			ID3D11PixelShader* shader{};
			context->PSGetShader(&shader, nullptr, nullptr);
			output.pixel_shader = reinterpret_cast<std::uintptr_t>(shader);
			if (shader != nullptr) shader->Release();

			ID3D11BlendState* blend{};
			std::array<float, 4> blend_factor{};
			context->OMGetBlendState(&blend, blend_factor.data(), &output.sample_mask);
			output.blend_state = reinterpret_cast<std::uintptr_t>(blend);
			if (blend != nullptr) blend->Release();
			std::memcpy(output.blend_factor_bits.data(), blend_factor.data(),
				sizeof(blend_factor));

			std::array<ID3D11ShaderResourceView*, sampled_dynamic_srv_slots> srvs{};
			context->PSGetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
			output.shader_resource_identity_hash = fnv_offset;
			output.shader_resource_descriptor_hash = fnv_offset;
			for (std::size_t slot{}; slot < srvs.size(); ++slot)
			{
				auto* const srv = srvs[slot];
				auto& binding = captured.shader_resources[slot];
				const auto identity = reinterpret_cast<std::uintptr_t>(srv);
				binding.view = identity;
				hash_value(output.shader_resource_identity_hash, identity);
				D3D11_SHADER_RESOURCE_VIEW_DESC descriptor{};
				if (srv != nullptr)
				{
					srv->GetDesc(&descriptor);
					Microsoft::WRL::ComPtr<ID3D11Resource> resource;
					srv->GetResource(&resource);
					binding.resource = reinterpret_cast<std::uintptr_t>(resource.Get());
				}
				binding.descriptor_hash = canonical_srv_descriptor_hash(descriptor);
				hash_value(output.shader_resource_descriptor_hash,
					binding.descriptor_hash);
			}
			release_all(srvs);

			std::array<ID3D11Buffer*, sampled_dynamic_constant_buffer_slots> buffers{};
			context->PSGetConstantBuffers(0, static_cast<UINT>(buffers.size()),
				buffers.data());
			output.constant_buffer_identity_hash = fnv_offset;
			output.constant_buffer_content_hash = fnv_offset;
			for (std::size_t slot{}; slot < buffers.size(); ++slot)
			{
				const auto content = capture_constant_buffer(buffers[slot]);
				auto& binding = captured.constant_buffers[slot];
				binding.buffer = content.buffer;
				binding.hash_low = content.hash_low;
				binding.hash_high = content.hash_high;
				binding.byte_width = content.byte_width;
				binding.known = content.known;
				hash_value(output.constant_buffer_identity_hash, content.buffer);
				hash_value(output.constant_buffer_content_hash, content.known);
				hash_value(output.constant_buffer_content_hash, content.hash_low);
				hash_value(output.constant_buffer_content_hash, content.hash_high);
				if (buffers[slot] == nullptr) continue;
				const auto bit = static_cast<std::uint16_t>(1u << slot);
				if (content.known) output.constant_buffer_known_mask |= bit;
				else output.constant_buffer_unknown_mask |= bit;
			}
			release_all(buffers);
		}

		void record_compared_invocation(ID3D11DeviceContext* const context,
			const compared_stream stream,
			const engine_stereo_execution::api operation, const std::uintptr_t caller,
			const std::uint32_t output_target, const std::uint8_t argument_count,
			const std::array<std::uint64_t, 6>& arguments) noexcept
		{
			if (active_eye >= compared_invocations.size()) return;
			const auto stream_index = static_cast<std::size_t>(stream);
			if (stream_index >= compared_stream_count) return;
			auto& count = compared_invocation_counts[active_eye][stream_index];
			const auto ordinal = count++;
			if (ordinal >= maximum_compared_invocations)
			{
				compared_invocation_overflows[active_eye][stream_index] = true;
				compared_stream_overflows.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			auto& captured = compared_invocations[active_eye][stream_index][ordinal];
			captured = {};
			auto& output = captured.fingerprint;
			output.present = true;
			output.operation = static_cast<std::uint8_t>(operation);
			output.argument_count = argument_count;
			output.caller = caller;
			output.output_target = output_target;
			output.arguments = arguments;
			capture_invocation_state(context, captured);
		}

		void finalize_stream_divergences(pair_sample& sample) noexcept
		{
			for (std::size_t stream_index{}; stream_index < compared_stream_count;
				++stream_index)
			{
				auto& output = sample.divergences[stream_index];
				output = {};
				output.stream = static_cast<compared_stream>(stream_index);
				output.output0_count = compared_invocation_counts[0][stream_index];
				output.output1_count = compared_invocation_counts[1][stream_index];
				output.output0_overflow =
					compared_invocation_overflows[0][stream_index];
				output.output1_overflow =
					compared_invocation_overflows[1][stream_index];
				const auto stored0 = (std::min)(output.output0_count,
					static_cast<std::uint32_t>(maximum_compared_invocations));
				const auto stored1 = (std::min)(output.output1_count,
					static_cast<std::uint32_t>(maximum_compared_invocations));
				const auto compared = (std::min)(stored0, stored1);
				std::uint32_t first = compared;
				for (std::uint32_t ordinal{}; ordinal < compared; ++ordinal)
				{
					if (!invocation_equal(
						compared_invocations[0][stream_index][ordinal].fingerprint,
						compared_invocations[1][stream_index][ordinal].fingerprint))
					{
						first = ordinal;
						break;
					}
				}
				output.observed = first < compared || output.output0_count !=
					output.output1_count;
				if (output.observed)
				{
					if (first == compared)
					{
						output.alignment = stream_alignment::tail_count;
					}
					else
					{
						constexpr std::uint32_t maximum_lookahead = 4;
						for (std::uint32_t skip = 1; skip <= maximum_lookahead; ++skip)
						{
							if (first + skip < stored0 && invocation_equal(
								compared_invocations[0][stream_index][first + skip].fingerprint,
								compared_invocations[1][stream_index][first].fingerprint))
							{
								output.alignment = stream_alignment::extra_output0;
								output.output0_skip = static_cast<std::uint8_t>(skip);
								break;
							}
							if (first + skip < stored1 && invocation_equal(
								compared_invocations[0][stream_index][first].fingerprint,
								compared_invocations[1][stream_index][first + skip].fingerprint))
							{
								output.alignment = stream_alignment::extra_output1;
								output.output1_skip = static_cast<std::uint8_t>(skip);
								break;
							}
						}
						if (output.alignment == stream_alignment::unclassified)
							output.alignment = stream_alignment::replacement;
					}
					output.first_ordinal = static_cast<std::uint64_t>(first) + 1;
					const auto window_first = first > 2 ? first - 2 : 0;
					output.window_first_ordinal =
						static_cast<std::uint64_t>(window_first) + 1;
					for (std::size_t window{}; window < divergence_window_size; ++window)
					{
						const auto ordinal = window_first + static_cast<std::uint32_t>(window);
						if (ordinal < stored0)
							output.output0[window] =
								compared_invocations[0][stream_index][ordinal].fingerprint;
						if (ordinal < stored1)
							output.output1[window] =
								compared_invocations[1][stream_index][ordinal].fingerprint;
					}
				}

				auto& semantics = output.semantics;
				const auto record_first_semantic = [&](const std::uint32_t output0_ordinal,
					const std::uint32_t output1_ordinal,
					const captured_invocation& left,
					const captured_invocation& right) noexcept
				{
					semantics.observed = true;
					if (semantics.first_output0_ordinal != 0) return;
					semantics.first_output0_ordinal =
						static_cast<std::uint64_t>(output0_ordinal) + 1;
					semantics.first_output1_ordinal =
						static_cast<std::uint64_t>(output1_ordinal) + 1;
					semantics.first_output0 = left.fingerprint;
					semantics.first_output1 = right.fingerprint;
				};

				std::uint32_t output0_ordinal{};
				std::uint32_t output1_ordinal{};
				constexpr std::uint32_t maximum_resync_lookahead = 4;
				while (output0_ordinal < stored0 && output1_ordinal < stored1)
				{
					const auto& left = compared_invocations[0][stream_index][output0_ordinal];
					const auto& right = compared_invocations[1][stream_index][output1_ordinal];
					if (!invocation_equal(left.fingerprint, right.fingerprint))
					{
						bool resynchronized{};
						for (std::uint32_t skip = 1; skip <= maximum_resync_lookahead; ++skip)
						{
							if (output0_ordinal + skip < stored0 && invocation_equal(
								compared_invocations[0][stream_index][output0_ordinal + skip].fingerprint,
								right.fingerprint))
							{
								output0_ordinal += skip;
								resynchronized = true;
								break;
							}
							if (output1_ordinal + skip < stored1 && invocation_equal(
								left.fingerprint,
								compared_invocations[1][stream_index][output1_ordinal + skip].fingerprint))
							{
								output1_ordinal += skip;
								resynchronized = true;
								break;
							}
						}
						++semantics.structural_resyncs;
						if (!resynchronized)
						{
							++output0_ordinal;
							++output1_ordinal;
						}
						continue;
					}

					++semantics.aligned_invocations;
					bool invocation_semantic_mismatch{};
					if (left.fingerprint.pixel_shader != right.fingerprint.pixel_shader)
					{
						++semantics.pixel_shader_mismatches;
						invocation_semantic_mismatch = true;
					}
					if (left.fingerprint.blend_state != right.fingerprint.blend_state)
					{
						++semantics.blend_state_mismatches;
						invocation_semantic_mismatch = true;
					}
					if (left.fingerprint.sample_mask != right.fingerprint.sample_mask)
					{
						++semantics.sample_mask_mismatches;
						invocation_semantic_mismatch = true;
					}
					if (left.fingerprint.blend_factor_bits !=
						right.fingerprint.blend_factor_bits)
					{
						++semantics.blend_factor_mismatches;
						invocation_semantic_mismatch = true;
					}

					for (std::size_t slot{}; slot < left.shader_resources.size(); ++slot)
					{
						const auto& left_binding = left.shader_resources[slot];
						const auto& right_binding = right.shader_resources[slot];
						if (left_binding.view == 0 && right_binding.view == 0) continue;
						auto& difference = semantics.shader_resources[slot];
						++difference.comparisons;
						bool slot_mismatch{};
						if (left_binding.view != right_binding.view)
						{
							++difference.view_identity_mismatches;
							slot_mismatch = true;
						}
						if (left_binding.resource != right_binding.resource)
						{
							++difference.resource_identity_mismatches;
							slot_mismatch = true;
						}
						if (left_binding.descriptor_hash != right_binding.descriptor_hash)
						{
							++difference.descriptor_mismatches;
							slot_mismatch = true;
						}
						if (slot_mismatch && difference.first_output0_ordinal == 0)
						{
							difference.first_output0_ordinal =
								static_cast<std::uint64_t>(output0_ordinal) + 1;
							difference.first_output1_ordinal =
								static_cast<std::uint64_t>(output1_ordinal) + 1;
							difference.output0 = left_binding;
							difference.output1 = right_binding;
						}
						invocation_semantic_mismatch |= slot_mismatch;
					}

					for (std::size_t slot{}; slot < left.constant_buffers.size(); ++slot)
					{
						const auto& left_binding = left.constant_buffers[slot];
						const auto& right_binding = right.constant_buffers[slot];
						if (left_binding.buffer == 0 && right_binding.buffer == 0) continue;
						auto& difference = semantics.constant_buffers[slot];
						++difference.comparisons;
						bool slot_mismatch{};
						if (left_binding.buffer != right_binding.buffer)
						{
							++difference.identity_mismatches;
							slot_mismatch = true;
						}
						if (left_binding.known != right_binding.known)
						{
							++difference.known_state_mismatches;
							slot_mismatch = true;
						}
						if (left_binding.known && right_binding.known)
						{
							++difference.content_comparisons;
							if (left_binding.byte_width != right_binding.byte_width ||
								left_binding.hash_low != right_binding.hash_low ||
								left_binding.hash_high != right_binding.hash_high)
							{
								++difference.content_mismatches;
								slot_mismatch = true;
							}
						}
						if (slot_mismatch && difference.first_output0_ordinal == 0)
						{
							difference.first_output0_ordinal =
								static_cast<std::uint64_t>(output0_ordinal) + 1;
							difference.first_output1_ordinal =
								static_cast<std::uint64_t>(output1_ordinal) + 1;
							difference.output0 = left_binding;
							difference.output1 = right_binding;
						}
						invocation_semantic_mismatch |= slot_mismatch;
					}

					if (invocation_semantic_mismatch)
						record_first_semantic(output0_ordinal, output1_ordinal, left, right);
					++output0_ordinal;
					++output1_ordinal;
				}
			}
		}

		[[nodiscard]] bool detailed_dynamic_sample(const std::uint64_t call) noexcept
		{
			// Preserve the first effects and then sample sparsely. This keeps the rolling
			// probe low-perturbation while still observing long CODE_TRANS/GLASS lists.
			return call <= 4 || (call % 32) == 0;
		}

		void fill_header(report& output, const state phase) noexcept
		{
			output.current = phase;
			output.installed = installed.load(std::memory_order_acquire);
			output.observers_attached = observers_attached.load(std::memory_order_acquire);
			output.history_tracking_active = history_tracking_active.load(
				std::memory_order_acquire);
			output.expected_context = expected_context.load(std::memory_order_acquire);
			output.device_generation = expected_generation.load(std::memory_order_acquire);
			output.arm_requests = arm_requests.load(std::memory_order_relaxed);
			output.arm_applications = arm_applications.load(std::memory_order_relaxed);
			output.mark_requests = mark_requests.load(std::memory_order_relaxed);
			output.mark_applications = mark_applications.load(std::memory_order_relaxed);
			output.pairs_started = pairs_started.load(std::memory_order_relaxed);
			output.pairs_completed = pairs_completed.load(std::memory_order_relaxed);
			output.pairs_incomplete = pairs_incomplete.load(std::memory_order_relaxed);
			output.pairs_overwritten = pairs_overwritten.load(std::memory_order_relaxed);
			output.invocation_callbacks = invocation_callbacks.load(
				std::memory_order_relaxed);
			output.resource_callbacks = resource_callbacks.load(std::memory_order_relaxed);
			output.clear_callbacks = clear_callbacks.load(std::memory_order_relaxed);
			output.foreign_thread_callbacks = foreign_thread_callbacks.load(
				std::memory_order_relaxed);
			output.foreign_context_callbacks = foreign_context_callbacks.load(
				std::memory_order_relaxed);
			output.watched_resource_count = watched_resource_count.load(
				std::memory_order_relaxed);
			output.watched_resource_overflows = watched_resource_overflows.load(
				std::memory_order_relaxed);
			output.watched_resource_writes = watched_resource_writes.load(
				std::memory_order_relaxed);
			output.compared_stream_overflows = compared_stream_overflows.load(
				std::memory_order_relaxed);
			output.capture_first_pair = capture_first_pair.load(std::memory_order_relaxed);
			output.capture_last_pair = capture_last_pair.load(std::memory_order_relaxed);
			output.frozen_pair = frozen_pair.load(std::memory_order_relaxed);
		}

		void clear_live_capture() noexcept
		{
			std::memset(ring.data(), 0, sizeof(ring));
			std::memset(watched_resources.data(), 0, sizeof(watched_resources));
			ring_write_index = 0;
			ring_count = 0;
			watched_count = 0;
			capture_sequence = 0;
			resource_write_sequence = 0;
			active_sample = nullptr;
			active_pair = 0;
			active_eye = 2;
			active_owner_thread.store(0, std::memory_order_release);
			active_context_identity.store(0, std::memory_order_release);
			active_record = nullptr;
			pairs_started.store(0, std::memory_order_relaxed);
			pairs_completed.store(0, std::memory_order_relaxed);
			pairs_incomplete.store(0, std::memory_order_relaxed);
			pairs_overwritten.store(0, std::memory_order_relaxed);
			invocation_callbacks.store(0, std::memory_order_relaxed);
			resource_callbacks.store(0, std::memory_order_relaxed);
			clear_callbacks.store(0, std::memory_order_relaxed);
			foreign_thread_callbacks.store(0, std::memory_order_relaxed);
			foreign_context_callbacks.store(0, std::memory_order_relaxed);
			watched_resource_count.store(0, std::memory_order_relaxed);
			watched_resource_overflows.store(0, std::memory_order_relaxed);
			watched_resource_writes.store(0, std::memory_order_relaxed);
			compared_stream_overflows.store(0, std::memory_order_relaxed);
			capture_first_pair.store(0, std::memory_order_relaxed);
			capture_last_pair.store(0, std::memory_order_relaxed);
			frozen_pair.store(0, std::memory_order_relaxed);
		}

		void set_history_tracking(const bool enabled) noexcept
		{
			engine_stereo_constant_buffer_probe::set_history_tracking_client(
				engine_stereo_constant_buffer_probe::history_tracking_client::effect_timeline,
				enabled);
			history_tracking_active.store(enabled, std::memory_order_release);
		}

		void freeze_capture() noexcept
		{
			if (!capture_active(current_state.load(std::memory_order_acquire))) return;
			set_history_tracking(false);
			const auto applied = mark_applications.fetch_add(1,
				std::memory_order_relaxed) + 1;
			if (ring_count != 0)
			{
				const auto last_index = (ring_write_index + ring.size() - 1) % ring.size();
				const auto last = ring[last_index].pair_id;
				frozen_pair.store(last, std::memory_order_relaxed);
			}
			{
				const std::lock_guard lock(publication_mutex);
				std::memset(&published_report, 0, sizeof(published_report));
				fill_header(published_report, state::frozen);
				published_report.mark_applications = applied;
				published_report.sample_count = static_cast<std::uint32_t>(ring_count);
				const auto first = ring_count == ring.size() ? ring_write_index : 0;
				for (std::size_t index{}; index < ring_count; ++index)
				{
					published_report.samples[index] =
						ring[(first + index) % ring.size()];
				}
			}
			active_sample = nullptr;
			active_pair = 0;
			active_eye = 2;
			active_owner_thread.store(0, std::memory_order_release);
			active_context_identity.store(0, std::memory_order_release);
			active_record = nullptr;
			current_state.store(state::frozen, std::memory_order_release);
		}

		void fail_capture() noexcept
		{
			set_history_tracking(false);
			active_sample = nullptr;
			active_pair = 0;
			active_eye = 2;
			active_owner_thread.store(0, std::memory_order_release);
			active_context_identity.store(0, std::memory_order_release);
			active_record = nullptr;
			current_state.store(state::failed, std::memory_order_release);
		}
	}

	bool install(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		if (context == nullptr || device_generation == 0) return false;
		const auto previous_context = expected_context.load(std::memory_order_acquire);
		const auto previous_generation = expected_generation.load(std::memory_order_acquire);
		if (installed.load(std::memory_order_acquire) &&
			(previous_context != reinterpret_cast<std::uintptr_t>(context) ||
				previous_generation != device_generation))
		{
			return false;
		}
		expected_context.store(reinterpret_cast<std::uintptr_t>(context),
			std::memory_order_release);
		expected_generation.store(device_generation, std::memory_order_release);
		engine_stereo_execution::set_invocation_observer(
			engine_stereo_execution::invocation_observer_channel::effect_timeline,
			observe_invocation);
		engine_stereo_resource_ops::set_observer(
			engine_stereo_resource_ops::observer_channel::effect_timeline,
			observe_resource_operation);
		engine_stereo_output_merger::set_clear_observers(
			engine_stereo_output_merger::clear_observer_channel::effect_timeline,
			observe_clear_render_target, nullptr);
		observers_attached.store(true, std::memory_order_release);
		installed.store(true, std::memory_order_release);
		if (current_state.load(std::memory_order_acquire) == state::failed)
			current_state.store(state::idle, std::memory_order_release);
		return true;
	}

	void invalidate_device(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		if (expected_context.load(std::memory_order_acquire) !=
			reinterpret_cast<std::uintptr_t>(context) ||
			expected_generation.load(std::memory_order_acquire) != device_generation)
		{
			return;
		}
		fail_capture();
		engine_stereo_execution::set_invocation_observer(
			engine_stereo_execution::invocation_observer_channel::effect_timeline,
			nullptr);
		engine_stereo_resource_ops::set_observer(
			engine_stereo_resource_ops::observer_channel::effect_timeline, nullptr);
		engine_stereo_output_merger::set_clear_observers(
			engine_stereo_output_merger::clear_observer_channel::effect_timeline,
			nullptr, nullptr);
		observers_attached.store(false, std::memory_order_release);
		installed.store(false, std::memory_order_release);
		expected_context.store(0, std::memory_order_release);
		expected_generation.store(0, std::memory_order_release);
	}

	bool request_arm() noexcept
	{
		if (!installed.load(std::memory_order_acquire) ||
			expected_context.load(std::memory_order_acquire) == 0) return false;
		auto observed = current_state.load(std::memory_order_acquire);
		for (;;)
		{
			if (observed != state::idle && observed != state::frozen &&
				observed != state::failed) return false;
			if (current_state.compare_exchange_weak(observed, state::arm_pending,
				std::memory_order_acq_rel, std::memory_order_acquire)) break;
		}
		arm_requests.fetch_add(1, std::memory_order_relaxed);
		// Start the exact CPU-upload history lease immediately. Some persistent
		// per-view buffers are populated before the next owner boundary; deferring
		// the lease until begin_pair would label those real inputs as unknown.
		set_history_tracking(true);
		return true;
	}

	bool request_mark() noexcept
	{
		auto observed = state::capturing;
		if (!current_state.compare_exchange_strong(observed, state::mark_pending,
			std::memory_order_acq_rel, std::memory_order_acquire)) return false;
		mark_requests.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	bool begin_pair(const std::uint64_t pair_id, ID3D11DeviceContext* const context,
		const std::uint64_t device_generation, const std::uint32_t owner_thread,
		const void* const left_record, const void* const right_record) noexcept
	{
		auto phase = current_state.load(std::memory_order_acquire);
		if (phase == state::arm_pending)
		{
			if (context == nullptr || pair_id == 0 || owner_thread == 0 ||
				left_record == nullptr || right_record == nullptr ||
				expected_context.load(std::memory_order_acquire) !=
					reinterpret_cast<std::uintptr_t>(context) ||
				expected_generation.load(std::memory_order_acquire) != device_generation)
			{
				fail_capture();
				return false;
			}
			clear_live_capture();
			set_history_tracking(true);
			arm_applications.fetch_add(1, std::memory_order_relaxed);
			current_state.store(state::capturing, std::memory_order_release);
			phase = state::capturing;
		}
		else if (phase == state::mark_pending && active_sample == nullptr)
		{
			freeze_capture();
			return false;
		}
		if (phase != state::capturing || context == nullptr || pair_id == 0 ||
			owner_thread == 0 || left_record == nullptr || right_record == nullptr ||
			active_sample != nullptr ||
			expected_context.load(std::memory_order_acquire) !=
				reinterpret_cast<std::uintptr_t>(context) ||
			expected_generation.load(std::memory_order_acquire) != device_generation)
		{
			if (phase == state::capturing) fail_capture();
			return false;
		}

		if (ring_count == ring.size())
			pairs_overwritten.fetch_add(1, std::memory_order_relaxed);
		else ++ring_count;
		auto& sample = ring[ring_write_index];
		sample = {};
		compared_invocation_counts = {};
		compared_invocation_overflows = {};
		sample.capture_sequence = ++capture_sequence;
		sample.pair_id = pair_id;
		sample.started_tick = GetTickCount64();
		sample.device_generation = device_generation;
		sample.owner_thread = owner_thread;
		sample.eyes[0].begin_record = capture_record(left_record);
		sample.eyes[1].begin_record = capture_record(right_record);
		active_sample = &sample;
		active_pair = pair_id;
		active_eye = 2;
		active_context_identity.store(reinterpret_cast<std::uintptr_t>(context),
			std::memory_order_release);
		active_owner_thread.store(owner_thread, std::memory_order_release);
		active_record = nullptr;
		pairs_started.fetch_add(1, std::memory_order_relaxed);
		capture_last_pair.store(pair_id, std::memory_order_relaxed);
		auto first = std::uint64_t{};
		(void)capture_first_pair.compare_exchange_strong(first, pair_id,
			std::memory_order_relaxed);
		return true;
	}

	bool begin_eye(const std::uint64_t pair_id, const std::uint32_t eye,
		const void* const record) noexcept
	{
		const auto phase = current_state.load(std::memory_order_acquire);
		if (!capture_active(phase) || active_sample == nullptr || active_pair != pair_id ||
			eye >= 2 || record == nullptr || active_eye < 2 ||
			GetCurrentThreadId() != active_owner_thread.load(std::memory_order_acquire))
		{
			if (capture_active(phase)) fail_capture();
			return false;
		}
		active_eye = eye;
		active_record = record;
		active_sample->eyes[eye].began = true;
		active_sample->eyes[eye].begin_record = capture_record(record);
		return true;
	}

	void end_eye(const std::uint64_t pair_id, const std::uint32_t eye,
		const void* const record) noexcept
	{
		const auto phase = current_state.load(std::memory_order_acquire);
		if (!capture_active(phase) || active_sample == nullptr || active_pair != pair_id ||
			eye >= 2 || active_eye != eye || record == nullptr ||
			GetCurrentThreadId() != active_owner_thread.load(std::memory_order_acquire))
		{
			if (capture_active(phase)) fail_capture();
			return;
		}
		active_sample->eyes[eye].ended = true;
		active_sample->eyes[eye].end_record = capture_record(record);
		active_sample->completed_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		active_eye = 2;
		active_record = nullptr;
	}

	void end_pair(const std::uint64_t pair_id, const bool completed) noexcept
	{
		const auto phase = current_state.load(std::memory_order_acquire);
		if (!capture_active(phase) || active_sample == nullptr || active_pair != pair_id ||
			GetCurrentThreadId() != active_owner_thread.load(std::memory_order_acquire))
		{
			if (capture_active(phase)) fail_capture();
			return;
		}
		finalize_target_lifecycles(*active_sample);
		finalize_stream_divergences(*active_sample);
		active_sample->completed = completed && active_sample->completed_eye_mask == 0x3;
		active_sample->completed_tick = GetTickCount64();
		if (active_sample->completed)
			pairs_completed.fetch_add(1, std::memory_order_relaxed);
		else pairs_incomplete.fetch_add(1, std::memory_order_relaxed);
		ring_write_index = (ring_write_index + 1) % ring.size();
		active_sample = nullptr;
		active_pair = 0;
		active_eye = 2;
		active_owner_thread.store(0, std::memory_order_release);
		active_context_identity.store(0, std::memory_order_release);
		active_record = nullptr;
		if (current_state.load(std::memory_order_acquire) == state::mark_pending)
			freeze_capture();
	}

	void observe_invocation(ID3D11DeviceContext* const context,
		const engine_stereo_execution::api operation, const std::uintptr_t caller,
		const std::uint32_t output_target, const std::uintptr_t output_rtv,
		const std::uint64_t, const std::uint8_t argument_count,
		const std::array<std::uint64_t, 6>& arguments) noexcept
	{
		const auto phase = current_state.load(std::memory_order_acquire);
		if (!capture_active(phase)) return;
		const auto owner_thread = active_owner_thread.load(std::memory_order_acquire);
		if (owner_thread == 0) return;
		if (GetCurrentThreadId() != owner_thread)
		{
			foreign_thread_callbacks.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (context == nullptr || reinterpret_cast<std::uintptr_t>(context) !=
			active_context_identity.load(std::memory_order_acquire))
		{
			foreign_context_callbacks.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		invocation_callbacks.fetch_add(1, std::memory_order_relaxed);
		if (active_sample == nullptr || active_eye >= 2 || active_record == nullptr)
			return;
		auto& eye = active_sample->eyes[active_eye];
		++eye.invocations;
		const auto event_sequence = ++eye.gpu_events;
		observe_target_invocation(eye, operation, output_target, output_rtv,
			event_sequence);

		if (operation == engine_stereo_execution::api::draw_indexed &&
			caller == engine_stereo_ssr_consumer_probe::exact_consumer_caller &&
			output_target == engine_stereo_ssr_consumer_probe::exact_consumer_target)
		{
			ID3D11PixelShader* shader{};
			context->PSGetShader(&shader, nullptr, nullptr);
			const auto candidate =
				engine_stereo_ssr_consumer_probe::is_scene_mip_candidate_shader(shader);
			if (shader != nullptr) shader->Release();
			if (candidate)
			{
				record_compared_invocation(context, compared_stream::ssr, operation,
					caller, output_target, argument_count, arguments);
				capture_ssr_consumer(context, eye.ssr);
			}
		}

		const auto family = engine_stereo_gpu_census::classify_dynamic_fx_caller(caller);
		const auto family_index = dynamic_index(family);
		if (family_index >= eye.dynamic.size()) return;
		record_compared_invocation(context, compared_stream_for_dynamic(family_index),
			operation, caller, output_target, argument_count, arguments);
		auto& dynamic = eye.dynamic[family_index];
		const auto call = ++dynamic.calls;
		if (dynamic.stream_hash == 0) dynamic.stream_hash = fnv_offset;
		hash_value(dynamic.stream_hash, operation);
		hash_value(dynamic.stream_hash, caller);
		hash_value(dynamic.stream_hash, output_target);
		hash_value(dynamic.stream_hash, argument_count);
		for (std::size_t index{}; index < argument_count && index < arguments.size(); ++index)
			hash_value(dynamic.stream_hash, arguments[index]);
		if (!detailed_dynamic_sample(call)) return;
		draw_fingerprint captured{};
		capture_pipeline(context, caller, captured);
		if (!dynamic.first.captured) dynamic.first = captured;
		dynamic.last = captured;
		++dynamic.detailed_samples;
		if (dynamic.pipeline_hash == 0) dynamic.pipeline_hash = fnv_offset;
		hash_value(dynamic.pipeline_hash, captured.pixel_shader);
		hash_value(dynamic.pipeline_hash, captured.blend_state);
		hash_value(dynamic.pipeline_hash, captured.sample_mask);
		hash_value(dynamic.pipeline_hash, captured.shader_resource_hash);
		hash_value(dynamic.pipeline_hash, captured.sampler_hash);
		hash_value(dynamic.pipeline_hash, captured.constant_buffer_hash);
	}

	void observe_resource_operation(
		const engine_stereo_resource_ops::event& event) noexcept
	{
		const auto phase = current_state.load(std::memory_order_acquire);
		if (!capture_active(phase)) return;
		const auto owner_thread = active_owner_thread.load(std::memory_order_acquire);
		if (owner_thread == 0) return;
		if (GetCurrentThreadId() != owner_thread)
		{
			foreign_thread_callbacks.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (event.context == nullptr || reinterpret_cast<std::uintptr_t>(event.context) !=
			active_context_identity.load(std::memory_order_acquire))
		{
			foreign_context_callbacks.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		resource_callbacks.fetch_add(1, std::memory_order_relaxed);
		ID3D11Resource* destination = event.destination;
		Microsoft::WRL::ComPtr<ID3D11Resource> view_resource;
		if (destination == nullptr && event.view != nullptr)
		{
			event.view->GetResource(&view_resource);
			destination = view_resource.Get();
		}
		const auto identity = reinterpret_cast<std::uintptr_t>(destination);
		if (identity == 0) return;
		for (std::size_t index{}; index < watched_count; ++index)
		{
			auto& watched = watched_resources[index];
			if (watched.identity != identity) continue;
			auto& writer = watched.last_writer;
			writer.known = true;
			writer.sequence = ++resource_write_sequence;
			writer.pair_id = active_pair;
			writer.eye = active_eye;
			writer.destination_subresource = event.destination_subresource;
			writer.operation = static_cast<std::uint8_t>(event.operation);
			writer.caller = event.caller;
			writer.source = reinterpret_cast<std::uintptr_t>(event.source);
			watched_resource_writes.fetch_add(1, std::memory_order_relaxed);
			return;
		}
	}

	void observe_clear_render_target(ID3D11DeviceContext* const context,
		ID3D11RenderTargetView* const view, const std::uintptr_t caller,
		const std::uint32_t output_target, const std::uintptr_t output_rtv,
		const std::uint64_t) noexcept
	{
		const auto phase = current_state.load(std::memory_order_acquire);
		if (!capture_active(phase)) return;
		const auto owner_thread = active_owner_thread.load(std::memory_order_acquire);
		if (owner_thread == 0) return;
		if (GetCurrentThreadId() != owner_thread)
		{
			foreign_thread_callbacks.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (context == nullptr || reinterpret_cast<std::uintptr_t>(context) !=
			active_context_identity.load(std::memory_order_acquire))
		{
			foreign_context_callbacks.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		clear_callbacks.fetch_add(1, std::memory_order_relaxed);
		if (active_sample == nullptr || active_eye >= 2 || active_record == nullptr)
			return;

		auto& eye = active_sample->eyes[active_eye];
		const auto event_sequence = ++eye.gpu_events;
		if (eye.clear_event_count >= eye.clear_events.size())
		{
			++eye.clear_event_overflow;
			return;
		}
		auto& event = eye.clear_events[eye.clear_event_count++];
		event.event_sequence = event_sequence;
		event.caller = caller;
		event.binding_target = output_target;
		event.clear_view = reinterpret_cast<std::uintptr_t>(view);
		event.clear_resource = query_view_resource(view);
		event.output_view = output_rtv;
		if (output_rtv == event.clear_view)
			event.output_resource = event.clear_resource;
		else
			event.output_resource = query_view_resource(
				reinterpret_cast<ID3D11RenderTargetView*>(output_rtv));
	}

	void get_report(report& output) noexcept
	{
		output = {};
		const auto phase = current_state.load(std::memory_order_acquire);
		if (phase == state::frozen)
		{
			const std::lock_guard lock(publication_mutex);
			output = published_report;
			return;
		}
		fill_header(output, phase);
	}

	const char* to_string(const state value) noexcept
	{
		switch (value)
		{
		case state::idle: return "idle";
		case state::arm_pending: return "arm_pending";
		case state::capturing: return "capturing";
		case state::mark_pending: return "mark_pending";
		case state::frozen: return "frozen";
		case state::failed: return "failed";
		default: return "unknown";
		}
	}
}
