#include <std_include.hpp>
#include "menu_sources.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>

namespace vr::diagnostics::menu_sources
{
	namespace
	{
		std::atomic_uint64_t sequence{}, active{}, expires{};
		std::mutex publication_mutex;
		report publication;
		std::uint64_t current() noexcept
		{
			return GetTickCount64() < expires.load(std::memory_order_acquire)
				? active.load(std::memory_order_acquire) : 0;
		}
		bool publish(const std::shared_ptr<image>& value) noexcept
		{
			const std::unique_lock lock(publication_mutex, std::try_to_lock);
			if (!lock.owns_lock()) return false;
			if (publication.request == value->request && active.load() == value->request)
				publication.sources[value->layer.slot] = value;
			return true;
		}
	}
	std::uint64_t request()
	{
		const std::lock_guard lock(publication_mutex);
		const auto id = sequence.fetch_add(1) + 1;
		publication = {id};
		expires.store(GetTickCount64() + 5000, std::memory_order_release);
		active.store(id, std::memory_order_release);
		return id;
	}
	void cancel(std::uint64_t id) noexcept
	{
		active.compare_exchange_strong(id, 0);
	}
	report snapshot(const std::uint64_t id)
	{
		const std::lock_guard lock(publication_mutex);
		return publication.request == id ? publication : report{id};
	}
	void readback::reset() noexcept
	{
		slots_ = {};
		request_ = generation_ = 0;
	}
	void readback::poll(const d3d11::device_snapshot& graphics) noexcept
	{
		const auto id = current();
		if (!id || !graphics)
		{
			if (request_) reset();
			return;
		}
		if (request_ && (request_ != id || generation_ != graphics.generation)) reset();
		request_ = id;
		generation_ = graphics.generation;
		for (auto& slot : slots_)
		{
			if (!slot.value || slot.published) continue;
			auto& value = *slot.value;
			if (slot.staging && !slot.mapped)
			{
				D3D11_MAPPED_SUBRESOURCE mapped{};
				const auto queue = d3d11::acquire_gpu_queue_interop();
				const auto result = graphics.context->Map(slot.staging.Get(), 0,
					D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
				if (result == DXGI_ERROR_WAS_STILL_DRAWING) continue;
				value.result = result;
				if (SUCCEEDED(result))
				{
					for (unsigned y{}; y < value.height; ++y)
						std::memcpy(value.rgba.data() + std::size_t(y) * value.width * 4,
							static_cast<const char*>(mapped.pData) + std::size_t(y) * mapped.RowPitch,
							std::size_t(value.width) * 4);
					graphics.context->Unmap(slot.staging.Get(), 0);
					value.state = "ready";
				}
				else { value.state = "map_failed"; value.rgba.clear(); }
				slot.mapped = true;
				slot.target.Reset(); slot.view.Reset(); slot.staging.Reset();
			}
			slot.published = publish(slot.value);
		}
	}
	void readback::observe(const d3d11::device_snapshot& graphics, ID3D11Texture2D* source,
		texture_blit::renderer& blit, const texture_blit::encoding encoding,
		const menu_preparation_status& menu, const menu_layer_status& layer) noexcept
	{
		if (!request_ || request_ != current() || generation_ != graphics.generation ||
			layer.slot >= slots_.size() || slots_[layer.slot].value || !source) return;
		auto& slot = slots_[layer.slot];
		try
		{
			slot.value = std::make_shared<image>();
			auto& value = *slot.value;
			value.layer = layer; value.request = request_; value.tick = GetTickCount64();
			value.device = graphics.generation; value.session = menu.session; value.revision = menu.revision;
			value.image_session = menu.image_session; value.image_revision = menu.image_revision;
			value.source_encoding = encoding;
			D3D11_TEXTURE2D_DESC description{}; source->GetDesc(&description);
			value.source_format = description.Format;
			if (!description.Width || !description.Height || description.SampleDesc.Count != 1 ||
				description.ArraySize != 1 || description.MipLevels != 1)
			{ value.state = "source_descriptor_unsupported"; return; }
			const float scale = (std::min)(1.f, 256.f / (std::max)(description.Width, description.Height));
			value.width = (std::max)(1u, static_cast<unsigned>(description.Width * scale));
			value.height = (std::max)(1u, static_cast<unsigned>(description.Height * scale));
			value.rgba.resize(std::size_t(value.width) * value.height * 4);
			D3D11_TEXTURE2D_DESC target{value.width, value.height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
				{1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
			auto result = graphics.device->CreateTexture2D(&target, nullptr, &slot.target);
			if (SUCCEEDED(result)) result = graphics.device->CreateRenderTargetView(slot.target.Get(), nullptr, &slot.view);
			target.Usage = D3D11_USAGE_STAGING; target.BindFlags = 0; target.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			if (SUCCEEDED(result)) result = graphics.device->CreateTexture2D(&target, nullptr, &slot.staging);
			value.result = result;
			if (FAILED(result)) { value.state = "allocation_failed"; value.rgba.clear(); slot.staging.Reset(); return; }
			std::string error;
			if (!blit.draw({graphics, source, slot.view.Get(), value.width, value.height, encoding}, error))
			{ value.state = "thumbnail_transfer_failed"; value.rgba.clear(); slot.staging.Reset(); return; }
			const auto queue = d3d11::acquire_gpu_queue_interop();
			graphics.context->CopyResource(slot.staging.Get(), slot.target.Get());
			graphics.context->Flush();
			value.state = "gpu_pending";
		}
		catch (...)
		{
			if (slot.value) { slot.value->state = "capture_exception"; slot.value->rgba.clear(); }
			slot.target.Reset(); slot.view.Reset(); slot.staging.Reset();
		}
	}
	std::string format(const report& value)
	{
		std::ostringstream out;
		out << "OpenXR native menu sources: request=" << value.request << '\n'
			<< "scope=downsampled_native_source_using_production_transfer_shader runtime_image_readback=no hmd_display_proof=no\n"
			<< "Pixels are RGBA8 sRGB with the production alpha convention; thumbnails do not prove full-resolution content.\n";
		for (unsigned index{}; index < value.sources.size(); ++index)
		{
			out << "source[" << index << "]: ";
			const auto& source = value.sources[index];
			if (!source) { out << "state=not_observed_or_gpu_pending\n"; continue; }
			const auto& s = *source;
			out << "state=" << s.state << " result=" << s.result << " tick=" << s.tick
				<< " device=" << s.device << " session=" << s.session << " revision=" << s.revision
				<< " image_session=" << s.image_session << " image_revision=" << s.image_revision << " source_tick=" << s.layer.source_tick
				<< " sequence=" << s.layer.source_sequence << " source_size=" << s.layer.source_width << 'x' << s.layer.source_height
				<< " source_format=" << s.source_format << " source_encoding=" << static_cast<unsigned>(s.source_encoding)
				<< " layer_flags=" << s.layer.flags << " thumbnail=" << s.width << 'x' << s.height;
			if (s.rgba.empty()) { out << '\n'; continue; }
			std::array<unsigned, 4> low{255,255,255,255}, high{};
			std::size_t rgb{}, alpha{};
			for (std::size_t offset{}; offset + 3 < s.rgba.size(); offset += 4)
			{
				for (unsigned channel{}; channel < 4; ++channel)
				{
					const auto byte = static_cast<unsigned char>(s.rgba[offset + channel]);
					low[channel] = (std::min)(low[channel], unsigned(byte)); high[channel] = (std::max)(high[channel], unsigned(byte));
				}
				rgb += s.rgba[offset] || s.rgba[offset+1] || s.rgba[offset+2];
				alpha += s.rgba[offset+3] != 0;
			}
			out << " sampled_pixels=" << s.rgba.size()/4 << " nonzero_rgb=" << rgb << " nonzero_alpha=" << alpha << " rgba_min/max=";
			for (unsigned channel{}; channel < 4; ++channel) out << low[channel] << '/' << high[channel] << (channel == 3 ? '\n' : ',');
			out << "  captured_geometry: head_center=" << s.layer.head_center[0] << ',' << s.layer.head_center[1] << ',' << s.layer.head_center[2]
				<< " facing=" << s.layer.facing << " head_ray_hit=" << s.layer.head_ray_hit << '\n';
		}
		return out.str();
	}
	std::string tga(const image& value)
	{
		if (value.rgba.empty() || !value.width || !value.height || value.width > 256 || value.height > 256 ||
			value.rgba.size() != std::size_t(value.width) * value.height * 4) return {};
		std::string output(18 + value.rgba.size(), '\0');
		output[2] = 2; output[12] = static_cast<char>(value.width); output[13] = static_cast<char>(value.width >> 8);
		output[14] = static_cast<char>(value.height); output[15] = static_cast<char>(value.height >> 8);
		output[16] = 32; output[17] = 0x28;
		for (std::size_t offset{}; offset < value.rgba.size(); offset += 4)
		{
			output[18+offset] = value.rgba[offset+2]; output[19+offset] = value.rgba[offset+1];
			output[20+offset] = value.rgba[offset]; output[21+offset] = value.rgba[offset+3];
		}
		return output;
	}
}
