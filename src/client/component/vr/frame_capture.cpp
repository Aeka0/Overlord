#include <std_include.hpp>

#include "frame_capture.hpp"
#include "diagnostics.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <utility>
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>

namespace vr
{
	namespace
	{
		bool same_description(const D3D11_TEXTURE2D_DESC& left,
			const D3D11_TEXTURE2D_DESC& right) noexcept
		{
			return left.Width == right.Width && left.Height == right.Height &&
				left.Format == right.Format && left.ArraySize == right.ArraySize &&
				left.MipLevels == right.MipLevels && left.SampleDesc.Count == right.SampleDesc.Count &&
				left.SampleDesc.Quality == right.SampleDesc.Quality;
		}

		D3D11_TEXTURE2D_DESC capture_description(const D3D11_TEXTURE2D_DESC& source) noexcept
		{
			auto result = source;
			result.Usage = D3D11_USAGE_DEFAULT;
			result.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			result.CPUAccessFlags = 0;
			result.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
			result.SampleDesc.Count = 1;
			result.SampleDesc.Quality = 0;
			result.ArraySize = 1;
			result.MipLevels = 1;
			return result;
		}

		D3D11_TEXTURE2D_DESC staging_description(const D3D11_TEXTURE2D_DESC& source) noexcept
		{
			auto result = source;
			result.Usage = D3D11_USAGE_STAGING;
			result.BindFlags = 0;
			result.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			result.MiscFlags = 0;
			result.SampleDesc.Count = 1;
			result.SampleDesc.Quality = 0;
			result.ArraySize = 1;
			result.MipLevels = 1;
			return result;
		}

		bool same_device(ID3D11Texture2D* const texture,
			ID3D11Device* const expected) noexcept
		{
			if (texture == nullptr || expected == nullptr)
			{
				return false;
			}
			Microsoft::WRL::ComPtr<ID3D11Device> actual;
			texture->GetDevice(&actual);
			if (!actual)
			{
				return false;
			}
			Microsoft::WRL::ComPtr<IUnknown> actual_identity;
			Microsoft::WRL::ComPtr<IUnknown> expected_identity;
			return SUCCEEDED(actual.As(&actual_identity)) &&
				SUCCEEDED(expected->QueryInterface(IID_PPV_ARGS(&expected_identity))) &&
				actual_identity.Get() == expected_identity.Get();
		}

	}

	frame_capture::~frame_capture()
	{
		revoke_all();
	}

	void frame_capture::clear_shared_imports_locked(const std::uint64_t generation) noexcept
	{
		for (auto& imported : shared_imports_)
		{
			if (generation == 0 || imported.device_generation == generation)
			{
				imported = {};
			}
		}
		if (generation == 0 || std::ranges::none_of(shared_imports_,
			[](const shared_import& imported) { return imported.texture != nullptr; }))
		{
			import_device_.Reset();
		}
	}

	HRESULT frame_capture::import_shared_locked(ID3D11Device* const consumer_device,
		const std::uint64_t generation, const HANDLE handle, const bool nt_handle,
		Microsoft::WRL::ComPtr<ID3D11Texture2D>& output) noexcept
	{
		output.Reset();
		if (consumer_device == nullptr || generation == 0 || handle == nullptr)
		{
			return E_INVALIDARG;
		}

		if (import_device_.Get() != consumer_device)
		{
			clear_shared_imports_locked();
			import_device_ = consumer_device;
		}
		for (const auto& imported : shared_imports_)
		{
			if (imported.handle == handle && imported.nt_handle == nt_handle &&
				imported.device_generation == generation &&
				imported.texture != nullptr)
			{
				output = imported.texture;
				++status_.shared_import_reused;
				return S_OK;
			}
		}

		auto available = std::ranges::find_if(shared_imports_,
			[](const shared_import& imported) { return imported.texture == nullptr; });
		if (available == shared_imports_.end())
		{
			// A native target set contains exactly four stable textures (two pairs).
			// A fifth handle means that target ownership was recreated; retire the old
			// imports as one set instead of reopening stable handles every frame.
			clear_shared_imports_locked();
			import_device_ = consumer_device;
			available = shared_imports_.begin();
		}

		HRESULT result{};
		if (nt_handle)
		{
			Microsoft::WRL::ComPtr<ID3D11Device1> device1;
			result = consumer_device->QueryInterface(IID_PPV_ARGS(&device1));
			if (SUCCEEDED(result) && device1 != nullptr)
			{
				result = device1->OpenSharedResource1(handle, IID_PPV_ARGS(&output));
			}
		}
		else
		{
			result = consumer_device->OpenSharedResource(handle, IID_PPV_ARGS(&output));
		}
		diagnostics::record_trace(diagnostics::trace_event::capture_open_shared,
			static_cast<std::uint32_t>(result),
			reinterpret_cast<std::uintptr_t>(output.Get()));
		if (SUCCEEDED(result) && output != nullptr)
		{
			available->handle = handle;
			available->nt_handle = nt_handle;
			available->device_generation = generation;
			available->texture = output;
			++status_.shared_opened;
		}
		return result;
	}

	void frame_capture::recycle_slot_locked(slot& value) noexcept
	{
		value.state = slot_state::free;
		value.frame_id = 0;
		value.cpu_pixels.clear();
		value.row_pitch = 0;
		value.tag = {};
	}

	void frame_capture::reset_slot_locked(slot& value) noexcept
	{
		recycle_slot_locked(value);
		value.texture.Reset();
		value.staging.Reset();
		value.query.Reset();
		value.shared_handle = nullptr;
		value.shared_nt_handle = false;
		value.description = {};
		value.device_generation = 0;
	}

	frame_capture::slot* frame_capture::find_free_slot_locked() noexcept
	{
		for (auto& value : slots_)
		{
			if (value.state == slot_state::free)
			{
				return &value;
			}
		}
		return nullptr;
	}

	frame_capture::slot* frame_capture::find_oldest_ready_slot_locked() noexcept
	{
		slot* result{};
		for (auto& value : slots_)
		{
			if (value.state != slot_state::ready)
			{
				continue;
			}
			if (result == nullptr || value.frame_id < result->frame_id)
			{
				result = &value;
			}
		}
		return result;
	}

	void frame_capture::discard_ready_except_locked(const slot* const retained) noexcept
	{
		slot* newest_opposite_eye{};
		if (retained != nullptr && retained->tag.stereo)
		{
			for (auto& value : slots_)
			{
				if (&value == retained || value.state != slot_state::ready ||
					!value.tag.stereo || value.tag.eye_index == retained->tag.eye_index)
				{
					continue;
				}
				if (newest_opposite_eye == nullptr || value.frame_id > newest_opposite_eye->frame_id)
				{
					newest_opposite_eye = &value;
				}
			}
		}
		for (auto& value : slots_)
		{
			if (&value != retained && &value != newest_opposite_eye && value.state == slot_state::ready)
			{
				++status_.dropped;
				recycle_slot_locked(value);
			}
		}
	}

	bool frame_capture::ensure_slot_locked(const d3d11::device_snapshot& graphics,
		const D3D11_TEXTURE2D_DESC& source, slot& value,
		const bool require_cpu_fallback) noexcept
	{
		if (!graphics || source.Width == 0 || source.Height == 0 ||
			source.Format == DXGI_FORMAT_UNKNOWN || source.ArraySize != 1 ||
			source.MipLevels != 1 || source.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ||
			source.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS)
		{
			return false;
		}

		const auto description = capture_description(source);
		if (value.texture && value.query && (!require_cpu_fallback || value.staging) &&
			value.device_generation == graphics.generation &&
			same_description(value.description, description))
		{
			return true;
		}

		reset_slot_locked(value);
		value.description = description;
		value.device_generation = graphics.generation;
		if (FAILED(graphics.device->CreateTexture2D(&description, nullptr, &value.texture)))
		{
			reset_slot_locked(value);
			return false;
		}

		if (require_cpu_fallback)
		{
			const auto staging = staging_description(description);
			if (FAILED(graphics.device->CreateTexture2D(&staging, nullptr, &value.staging)))
			{
				reset_slot_locked(value);
				return false;
			}
		}

		const D3D11_QUERY_DESC query_description{D3D11_QUERY_EVENT, 0};
		if (FAILED(graphics.device->CreateQuery(&query_description, &value.query)))
		{
			reset_slot_locked(value);
			return false;
		}

		Microsoft::WRL::ComPtr<IDXGIResource> resource;
		if (SUCCEEDED(value.texture.As(&resource)))
		{
			(void)resource->GetSharedHandle(&value.shared_handle);
		}
		return true;
	}

	void frame_capture::poll_queries_locked(const d3d11::device_snapshot& graphics) noexcept
	{
		if (!graphics)
		{
			return;
		}
		for (auto& value : slots_)
		{
			if (value.state != slot_state::producing)
			{
				continue;
			}
			diagnostics::record_trace(diagnostics::trace_event::capture_poll,
				static_cast<std::uint64_t>(&value - slots_.data()), value.frame_id);
			if (value.device_generation != graphics.generation)
			{
				++status_.stale_generation;
				reset_slot_locked(value);
				continue;
			}

			BOOL complete{};
			diagnostics::record_trace(diagnostics::trace_event::capture_poll_begin,
				static_cast<std::uint64_t>(&value - slots_.data()), value.frame_id);
			HRESULT result{};
			{
				// H2 does not enable ID3D11Multithread protection. GetData is
				// non-blocking, but it still touches the immediate context and must not
				// overlap Present/ResizeBuffers on the split renderer/present threads.
				auto gpu_queue = d3d11::acquire_gpu_queue_interop(
					d3d11::gpu_queue_client::openvr);
				result = graphics.context->GetData(value.query.Get(), &complete,
					sizeof(complete), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			}
			diagnostics::record_trace(diagnostics::trace_event::capture_poll_end,
				static_cast<std::uint64_t>(static_cast<std::uint32_t>(result)),
				complete ? 1 : 0);
			diagnostics::record_trace(diagnostics::trace_event::capture_poll,
				static_cast<std::uint64_t>(static_cast<std::uint32_t>(result)),
				complete ? 1 : 0);
			if (FAILED(result))
			{
				if (value.tag.native && value.tag.pair_id != 0)
				{
					failed_native_pair_id_ = value.tag.pair_id;
					failed_native_eye_ = value.tag.eye_index;
					failed_native_result_ = result;
				}
				++status_.dropped;
				reset_slot_locked(value);
				continue;
			}
			if (result == S_FALSE || !complete)
			{
				const auto removed_reason = graphics.device->GetDeviceRemovedReason();
				if (FAILED(removed_reason))
				{
					if (value.tag.native && value.tag.pair_id != 0)
					{
						failed_native_pair_id_ = value.tag.pair_id;
						failed_native_eye_ = value.tag.eye_index;
						failed_native_result_ = removed_reason;
					}
					diagnostics::record_trace(diagnostics::trace_event::capture_poll_end,
						static_cast<std::uint64_t>(static_cast<std::uint32_t>(removed_reason)), 2);
					++status_.dropped;
					reset_slot_locked(value);
					continue;
				}
				++status_.query_pending;
				continue;
			}
			if (!value.staging)
			{
				if (value.tag.native && value.texture != nullptr)
				{
					value.state = slot_state::ready;
					++status_.ready;
					continue;
				}
				++status_.dropped;
				reset_slot_locked(value);
				continue;
			}

			// Preserve completed pixels even when a shared handle exists. A driver can
			// reject OpenSharedResource after GetSharedHandle has succeeded.
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const auto map_result = graphics.context->Map(value.staging.Get(), 0,
				D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
			if (map_result == DXGI_ERROR_WAS_STILL_DRAWING)
			{
				++status_.query_pending;
				continue;
			}
			if (FAILED(map_result))
			{
				++status_.dropped;
				reset_slot_locked(value);
				continue;
			}
			value.row_pitch = mapped.RowPitch;
			value.cpu_pixels.resize(static_cast<std::size_t>(mapped.RowPitch) *
				value.description.Height);
			for (std::uint32_t row = 0; row < value.description.Height; ++row)
			{
				std::memcpy(value.cpu_pixels.data() + static_cast<std::size_t>(row) * mapped.RowPitch,
					static_cast<const std::byte*>(mapped.pData) +
						static_cast<std::size_t>(row) * mapped.RowPitch,
					mapped.RowPitch);
			}
			graphics.context->Unmap(value.staging.Get(), 0);
			++status_.cpu_fallback_ready;
			value.state = slot_state::ready;
			++status_.ready;
		}
	}

	void frame_capture::poll(const d3d11::device_snapshot& graphics) noexcept
	{
		const std::lock_guard lock(mutex_);
		poll_queries_locked(graphics);
	}

	bool frame_capture::produce_texture_locked(const d3d11::device_snapshot& graphics,
		ID3D11Texture2D* const source_texture, const capture_frame_tag tag) noexcept
	{
		if (!graphics || source_texture == nullptr ||
			!same_device(source_texture, graphics.device.Get()))
		{
			if (graphics && source_texture != nullptr)
			{
				++status_.dropped;
			}
			return false;
		}

		slot* value = find_free_slot_locked();
		if (value == nullptr)
		{
			value = find_oldest_ready_slot_locked();
			if (value != nullptr)
			{
				++status_.dropped;
				recycle_slot_locked(*value);
			}
		}
		if (value == nullptr)
		{
			++status_.slot_exhausted;
			return false;
		}

		D3D11_TEXTURE2D_DESC source{};
		source_texture->GetDesc(&source);
		const bool native_direct_capture = tag.native && tag.stereo &&
			tag.pair_id != 0 && tag.eye_index < 2;
		if (native_direct_capture)
		{
			// Retain and publish the exact ordinary H2 target. The consumer is required
			// to use the same D3D11 device and object, so normal immediate-context order
			// followed by IVRCompositor::Submit is the complete synchronization contract.
			reset_slot_locked(*value);
			if (source.SampleDesc.Count != 1 || source.ArraySize != 1 || source.MipLevels != 1)
			{
				++status_.dropped;
				return false;
			}
			value->texture = source_texture;
			value->description = source;
			value->device_generation = graphics.generation;
			value->frame_id = tag.pair_id;
			value->tag = tag;
			value->state = slot_state::ready;
			next_frame_id_ = (std::max)(next_frame_id_, tag.pair_id);
			diagnostics::record_trace(diagnostics::trace_event::capture_direct,
				reinterpret_cast<std::uintptr_t>(source_texture),
				graphics.generation);
			diagnostics::record_trace(diagnostics::trace_event::capture_produce,
				reinterpret_cast<std::uintptr_t>(source_texture),
				(tag.pair_id << 2) | (static_cast<std::uint64_t>(tag.eye_index) << 1) | 1ull);
			++status_.produced;
			++status_.native_produced;
			++status_.ready;
			return true;
		}
		if (!ensure_slot_locked(graphics, source, *value, true))
		{
			++status_.dropped;
			return false;
		}

		value->state = slot_state::producing;
		diagnostics::record_trace(diagnostics::trace_event::capture_produce,
			reinterpret_cast<std::uintptr_t>(source_texture),
			(tag.pair_id << 2) | (static_cast<std::uint64_t>(tag.eye_index) << 1) |
				(tag.native ? 1ull : 0ull));
		// Native eye captures carry the immutable engine view-family id. Preserve
		// it for both eyes so the compositor can enforce same-family pairing; a
		// monotonically generated id remains sufficient for explicit backbuffer
		// diagnostics.
		const auto capture_id = ++next_frame_id_;
		value->frame_id = capture_id;
		value->device_generation = graphics.generation;
		value->cpu_pixels.clear();
		value->row_pitch = 0;
		value->tag = tag.stereo && tag.pair_id != 0 && tag.eye_index < 2
			? tag : capture_frame_tag{};
		if (source.SampleDesc.Count > 1)
		{
			diagnostics::record_trace(diagnostics::trace_event::capture_copy,
				reinterpret_cast<std::uintptr_t>(source_texture),
				reinterpret_cast<std::uintptr_t>(value->texture.Get()));
			diagnostics::record_trace(diagnostics::trace_event::capture_copy_begin,
				reinterpret_cast<std::uintptr_t>(source_texture),
				reinterpret_cast<std::uintptr_t>(value->texture.Get()));
			graphics.context->ResolveSubresource(value->texture.Get(), 0, source_texture, 0,
				source.Format);
			diagnostics::record_trace(diagnostics::trace_event::capture_copy_end,
				reinterpret_cast<std::uintptr_t>(source_texture), 1);
		}
		else
		{
			diagnostics::record_trace(diagnostics::trace_event::capture_copy,
				reinterpret_cast<std::uintptr_t>(source_texture),
				reinterpret_cast<std::uintptr_t>(value->texture.Get()));
			diagnostics::record_trace(diagnostics::trace_event::capture_copy_begin,
				reinterpret_cast<std::uintptr_t>(source_texture),
				reinterpret_cast<std::uintptr_t>(value->texture.Get()));
			graphics.context->CopyResource(value->texture.Get(), source_texture);
			diagnostics::record_trace(diagnostics::trace_event::capture_copy_end,
				reinterpret_cast<std::uintptr_t>(source_texture), 1);
		}
		if (value->staging != nullptr)
		{
			diagnostics::record_trace(diagnostics::trace_event::capture_copy_begin,
				reinterpret_cast<std::uintptr_t>(value->texture.Get()),
				reinterpret_cast<std::uintptr_t>(value->staging.Get()));
			graphics.context->CopyResource(value->staging.Get(), value->texture.Get());
			diagnostics::record_trace(diagnostics::trace_event::capture_copy_end,
				reinterpret_cast<std::uintptr_t>(value->texture.Get()), 1);
		}
		diagnostics::record_trace(diagnostics::trace_event::capture_query_end_begin,
			reinterpret_cast<std::uintptr_t>(value->query.Get()), value->frame_id);
		{
			// Generic diagnostic captures use an event marker without flushing or
			// blocking the producer queue.
			auto gpu_queue = d3d11::acquire_gpu_queue_interop(
				d3d11::gpu_queue_client::openvr);
			graphics.context->End(value->query.Get());
		}
		diagnostics::record_trace(diagnostics::trace_event::capture_query_end_end,
			reinterpret_cast<std::uintptr_t>(value->query.Get()), 1);
		const auto removed_reason = graphics.device->GetDeviceRemovedReason();
		if (FAILED(removed_reason))
		{
			diagnostics::record_trace(diagnostics::trace_event::capture_query_end_end,
				static_cast<std::uint32_t>(removed_reason), 2);
			reset_slot_locked(*value);
			++status_.dropped;
			return false;
		}
		diagnostics::record_trace(diagnostics::trace_event::capture_query_end,
			reinterpret_cast<std::uintptr_t>(value->query.Get()), value->frame_id);
		++status_.produced;
		if (value->tag.native) ++status_.native_produced;
		return true;
	}

	void frame_capture::produce(const d3d11::present_event& event,
		const capture_frame_tag tag) noexcept
	{
		if (!event.graphics || event.swap_chain == nullptr)
		{
			return;
		}
		const std::lock_guard lock(mutex_);

		Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain3;
		UINT buffer_index = 0;
		if (SUCCEEDED(event.swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3))))
		{
			buffer_index = swap_chain3->GetCurrentBackBufferIndex();
		}
		Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
		if (FAILED(event.swap_chain->GetBuffer(buffer_index, IID_PPV_ARGS(&backbuffer))) || !backbuffer)
		{
			++status_.dropped;
			return;
		}
		(void)produce_texture_locked(event.graphics, backbuffer.Get(), tag);
	}

	bool frame_capture::produce_texture(const d3d11::device_snapshot& graphics,
		ID3D11Texture2D* const source, const capture_frame_tag tag) noexcept
	{
		const std::lock_guard lock(mutex_);
		const auto produced = produce_texture_locked(graphics, source, tag);
		if (!produced && tag.native && tag.stereo && tag.pair_id != 0 && tag.eye_index < 2 &&
			failed_native_pair_id_ != tag.pair_id)
		{
			failed_native_pair_id_ = tag.pair_id;
			failed_native_eye_ = tag.eye_index;
			failed_native_result_ = E_FAIL;
		}
		return produced;
	}

	bool frame_capture::acquire(ID3D11Device* const consumer_device,
		const std::uint64_t generation, captured_frame& output,
		std::string& error) noexcept
	{
		output = {};
		std::lock_guard lock(mutex_);
		slot* selected{};
		for (auto& value : slots_)
		{
			if (value.state != slot_state::ready)
			{
				continue;
			}
			if (value.device_generation != generation)
			{
				++status_.stale_generation;
				reset_slot_locked(value);
				continue;
			}
			if (selected == nullptr || value.frame_id > selected->frame_id)
			{
				selected = &value;
			}
		}
		if (selected == nullptr)
		{
			error = "no completed game capture is available";
			return false;
		}

		discard_ready_except_locked(selected);
		selected->state = slot_state::acquired;
		++status_.acquired;
		output.producer_texture = selected->texture;
		output.description = selected->description;
		output.frame_id = selected->frame_id;
		output.device_generation = selected->device_generation;
		output.slot_index = static_cast<std::uint32_t>(selected - slots_.data());
		output.row_pitch = selected->row_pitch;
		output.cpu_pixels = selected->cpu_pixels;
		output.tag = selected->tag;
		if (selected->shared_handle != nullptr && consumer_device != nullptr)
		{
			const auto result = import_shared_locked(consumer_device, generation,
				selected->shared_handle, selected->shared_nt_handle, output.texture);
			// Cross-device diagnostic captures may still use the generic shared-import
			// path. OpenVR native stereo never reaches this branch.
			if (SUCCEEDED(result) && output.texture)
			{
				output.shared = true;
				return true;
			}
			output.texture.Reset();
			++status_.shared_failed;
		}
		if (output.cpu_pixels.empty() || output.row_pitch == 0)
		{
			selected->state = slot_state::ready;
			error = "capture interop failed and CPU fallback has no completed pixels";
			return false;
		}
		++status_.cpu_fallback_requested;
		return true;
	}

	bool frame_capture::open_slot_locked(ID3D11Device* const consumer_device,
		const std::uint64_t generation, slot& value, captured_frame& output,
		std::string& error, const bool require_gpu_texture) noexcept
	{
		if (value.device_generation != generation)
		{
			++status_.stale_generation;
			reset_slot_locked(value);
			error = "stereo capture belongs to a stale game D3D11 generation";
			return false;
		}
		value.state = slot_state::acquired;
		++status_.acquired;
		output.description = value.description;
		output.producer_texture = value.texture;
		output.frame_id = value.frame_id;
		output.device_generation = value.device_generation;
		output.slot_index = static_cast<std::uint32_t>(&value - slots_.data());
		output.row_pitch = value.row_pitch;
		output.cpu_pixels = value.cpu_pixels;
		output.tag = value.tag;
		if (value.texture != nullptr && consumer_device != nullptr &&
			same_device(value.texture.Get(), consumer_device))
		{
			// Strict native acquisition retains the exact producer object directly.
			output.texture = value.texture;
			output.shared = false;
			++status_.native_direct_acquired;
			diagnostics::record_trace(diagnostics::trace_event::capture_retain_direct,
				reinterpret_cast<std::uintptr_t>(output.texture.Get()), output.frame_id);
			return true;
		}
		if (value.shared_handle != nullptr && consumer_device != nullptr)
		{
			const auto result = import_shared_locked(consumer_device, generation,
				value.shared_handle, value.shared_nt_handle, output.texture);
			if (SUCCEEDED(result) && output.texture)
			{
				output.shared = true;
				return true;
			}
			output.texture.Reset();
			++status_.shared_failed;
		}
		if (require_gpu_texture)
		{
			value.state = slot_state::ready;
			output = {};
			error = "native stereo capture is not an exact H2-device GPU texture";
			if (status_.acquired != 0) --status_.acquired;
			return false;
		}
		if (output.cpu_pixels.empty() || output.row_pitch == 0)
		{
			value.state = slot_state::ready;
			error = "stereo capture interop failed and CPU fallback has no completed pixels";
			return false;
		}
		++status_.cpu_fallback_requested;
		return true;
	}

	stereo_pair_acquire_result frame_capture::acquire_stereo_pair(ID3D11Device* const consumer_device,
		const std::uint64_t generation, const std::uint64_t expected_pair_id,
		std::array<captured_frame, 2>& output,
		std::string& error) noexcept
	{
		output = {};
		error.clear();
		std::lock_guard lock(mutex_);
		if (expected_pair_id != 0 && failed_native_pair_id_ == expected_pair_id)
		{
			for (auto& value : slots_)
			{
				if (value.tag.native && value.tag.pair_id == expected_pair_id)
				{
					reset_slot_locked(value);
				}
			}
			error = std::format("native pair {} eye {} producer failed (HRESULT=0x{:08X})",
				expected_pair_id, failed_native_eye_,
				static_cast<std::uint32_t>(failed_native_result_));
			failed_native_pair_id_ = 0;
			failed_native_eye_ = 0;
			failed_native_result_ = S_OK;
			++status_.native_pair_misses;
			return stereo_pair_acquire_result::failed;
		}
		std::array<slot*, 2> selected{};
		std::uint64_t selected_pair{};
		for (auto& value : slots_)
		{
			if (value.state != slot_state::ready || !value.tag.stereo || !value.tag.native ||
				value.tag.pair_id == 0 || value.tag.eye_index >= 2 ||
				value.device_generation != generation)
			{
				continue;
			}
			const auto pair = value.tag.pair_id;
			if (expected_pair_id != 0 && pair != expected_pair_id) continue;
			if (selected_pair != 0 && pair < selected_pair) continue;
			slot* opposite{};
			for (auto& candidate : slots_)
			{
				if (candidate.state == slot_state::ready && candidate.tag.stereo && candidate.tag.native &&
					candidate.tag.pair_id == pair && candidate.tag.eye_index == (value.tag.eye_index ^ 1u) &&
					candidate.device_generation == generation && candidate.frame_id == value.frame_id)
				{
					opposite = &candidate;
					break;
				}
			}
			if (opposite == nullptr) continue;
			selected_pair = pair;
			selected[value.tag.eye_index] = &value;
			selected[value.tag.eye_index ^ 1u] = opposite;
		}
		if (selected[0] == nullptr || selected[1] == nullptr)
		{
			++status_.native_pair_misses;
			error = "no complete native stereo capture pair is available";
			return stereo_pair_acquire_result::pending;
		}
		if (consumer_device == nullptr || selected[0]->texture == nullptr ||
			selected[1]->texture == nullptr ||
			!same_device(selected[0]->texture.Get(), consumer_device) ||
			!same_device(selected[1]->texture.Get(), consumer_device))
		{
			failed_native_pair_id_ = selected_pair;
			failed_native_eye_ = 0;
			failed_native_result_ = DXGI_ERROR_INVALID_CALL;
			++status_.native_pair_misses;
			error = "native stereo pair crossed a D3D11 device boundary";
			return stereo_pair_acquire_result::failed;
		}
		if (!open_slot_locked(consumer_device, generation, *selected[0], output[0], error, true))
		{
			++status_.native_pair_misses;
			return stereo_pair_acquire_result::failed;
		}
		if (!open_slot_locked(consumer_device, generation, *selected[1], output[1], error, true))
		{
			// Keep the first slot recoverable if the opposite device import fails.
			selected[0]->state = slot_state::ready;
			if (status_.acquired != 0) --status_.acquired;
			output[0] = {};
			++status_.native_pair_misses;
			return stereo_pair_acquire_result::failed;
		}
		++status_.native_pairs_acquired;
		return stereo_pair_acquire_result::ready;
	}

	void frame_capture::release(captured_frame& frame) noexcept
	{
		const std::lock_guard lock(mutex_);
		if (frame.slot_index < slots_.size())
		{
			auto& value = slots_[frame.slot_index];
			if (value.state == slot_state::acquired && value.frame_id == frame.frame_id)
			{
				if (!frame.shared && !frame.cpu_pixels.empty())
				{
					++status_.cpu_fallback_consumed;
				}
				recycle_slot_locked(value);
				++status_.released;
			}
		}
		frame = {};
	}

	void frame_capture::invalidate(const std::uint64_t generation) noexcept
	{
		const std::lock_guard lock(mutex_);
		for (auto& value : slots_)
		{
			if (generation == 0 || value.device_generation == generation)
			{
				if (value.state != slot_state::free) ++status_.invalidated;
				reset_slot_locked(value);
			}
		}
		clear_shared_imports_locked(generation);
		if (generation == 0 || failed_native_pair_id_ != 0)
		{
			failed_native_pair_id_ = 0;
			failed_native_eye_ = 0;
			failed_native_result_ = S_OK;
		}
	}

	void frame_capture::revoke_all() noexcept
	{
		invalidate(0);
	}

	frame_capture_status frame_capture::get_status() const noexcept
	{
		const std::lock_guard lock(mutex_);
		return status_;
	}
	bool frame_capture::try_get_status(frame_capture_status& output) const noexcept
	{
		const std::unique_lock lock(mutex_,std::try_to_lock);
		if(!lock.owns_lock())return false;
		output=status_;return true;
	}
}
