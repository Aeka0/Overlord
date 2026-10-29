#pragma once

#include "../composition_diagnostics.hpp"
#include "../texture_blit.hpp"
#include <memory>
#include <string>

namespace vr::diagnostics::menu_sources
{
	inline constexpr std::size_t capacity = menu_preparation_status{}.geometry.size();
	struct image
	{
		menu_layer_status layer;
		const char* state{"not_observed"};
		std::int64_t result{};
		std::uint64_t request{}, tick{}, device{}, session{}, revision{}, image_session{}, image_revision{};
		std::uint32_t width{}, height{}, source_format{};
		texture_blit::encoding source_encoding{};
		// Bounded RGBA8 sRGB thumbnail, using the production transfer's encoding.
		// This is native source evidence; the runtime-owned image is never read.
		std::string rgba;
	};
	struct report
	{
		std::uint64_t request{};
		std::array<std::shared_ptr<const image>, capacity> sources;
	};
	std::uint64_t request();
	void cancel(std::uint64_t request) noexcept;
	report snapshot(std::uint64_t request);
	std::string format(const report& value);
	std::string tga(const image& value);

	// Owned and polled by the existing Present owner. No new worker, runtime
	// calls, graphics leases in reports, GPU waits, or render-thread file I/O.
	class readback
	{
		struct slot
		{
			Microsoft::WRL::ComPtr<ID3D11Texture2D> target, staging;
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
			std::shared_ptr<image> value;
			bool mapped{}, published{};
		};
		std::array<slot, capacity> slots_;
		std::uint64_t request_{}, generation_{};
	public:
		void poll(const d3d11::device_snapshot& graphics) noexcept;
		void observe(const d3d11::device_snapshot& graphics, ID3D11Texture2D* source,
			texture_blit::renderer& blit, texture_blit::encoding encoding,
			const menu_preparation_status& menu, const menu_layer_status& layer) noexcept;
		void reset() noexcept;
	};
}
