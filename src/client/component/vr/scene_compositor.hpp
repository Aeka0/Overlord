#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include "component/d3d11.hpp"
#include "engine_stereo_bridge.hpp"
namespace vr
{
	struct scene_uv_mapping
	{
		float scale_u{1.0f};
		float scale_v{1.0f};
		float offset_u{};
		float offset_v{};
	};

	[[nodiscard]] bool calculate_projection_uv_mapping(
		const engine_stereo_bridge::eye_projection& projection,
		scene_uv_mapping& output) noexcept;

	struct scene_compositor_target
	{
		ID3D11RenderTargetView* render_target{};
		std::uint32_t width{};
		std::uint32_t height{};
		engine_stereo_bridge::eye_projection projection{};
		bool remap_projection{};
	};

	struct stereo_capture_source
	{
		ID3D11Texture2D* texture{};
		D3D11_TEXTURE2D_DESC description{};
		std::uint64_t frame_id{};
		std::uint64_t game_device_generation{};
		std::uint64_t pair_id{};
		std::uint32_t eye_index{};
	};

	class scene_compositor final
	{
	public:
		[[nodiscard]] bool prepare(const d3d11::device_snapshot& graphics, IDXGISwapChain* swap_chain,
			std::string& error);
		[[nodiscard]] bool prepare_capture(const d3d11::device_snapshot& graphics,
			ID3D11Texture2D* source, const D3D11_TEXTURE2D_DESC& description,
			std::uint64_t frame_id, std::uint64_t game_device_generation, std::string& error);
		[[nodiscard]] bool prepare_cpu_capture(const d3d11::device_snapshot& graphics,
			const std::vector<std::byte>& pixels, std::uint32_t row_pitch,
			const D3D11_TEXTURE2D_DESC& description, std::uint64_t frame_id,
			std::uint64_t game_device_generation, std::string& error);
		// Imports and copies one immutable left/right family as a single transaction.
		// A partial eye can never become an active compositor source.
		[[nodiscard]] bool prepare_stereo_pair(const d3d11::device_snapshot& graphics,
			const std::array<stereo_capture_source, 2>& sources, std::string& error);
		[[nodiscard]] bool prepare_cpu_stereo_capture(const d3d11::device_snapshot& graphics,
			const std::vector<std::byte>& pixels, std::uint32_t row_pitch,
			const D3D11_TEXTURE2D_DESC& description, std::uint64_t frame_id,
			std::uint64_t game_device_generation, std::uint64_t pair_id,
			std::uint32_t eye_index, std::string& error);
		[[nodiscard]] bool monoscopic_source_available(std::uint64_t device_generation) const noexcept;
		[[nodiscard]] bool stereo_source_available(std::uint64_t device_generation) const noexcept;
		[[nodiscard]] bool render_eye(const d3d11::device_snapshot& graphics,
			const scene_compositor_target& target, std::string& error,
			std::uint32_t source_index = 0);
		void revoke_sources() noexcept;
		void invalidate() noexcept;

	private:
		struct source_metadata
		{
			bool available{};
			bool stereo{};
			std::uint64_t frame_id{};
			std::uint64_t lifecycle_epoch{};
			std::uint64_t device_generation{};
			std::uint64_t pair_id{};
			std::uint32_t eye_index{};
		};

		[[nodiscard]] bool ensure_pipeline(ID3D11Device* device, std::uint64_t generation, std::string& error);
		[[nodiscard]] bool ensure_source(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& source_description,
			std::uint64_t generation, std::string& error);
		[[nodiscard]] bool copy_source(ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer,
			const D3D11_TEXTURE2D_DESC& description, std::uint32_t source_index, std::string& error);
		[[nodiscard]] bool copy_source_synchronized(ID3D11DeviceContext* context,
			ID3D11Texture2D* source, ID3D11Texture2D* destination, std::string& error);
		[[nodiscard]] bool prepare_source(const d3d11::device_snapshot& graphics, IDXGISwapChain* swap_chain,
			std::uint32_t source_index, const source_metadata& metadata, std::string& error);
		[[nodiscard]] bool activate_stereo_pair(const d3d11::device_snapshot& graphics) noexcept;
		std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 4> source_textures_{};
		std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 4> source_views_{};
		std::array<source_metadata, 4> source_metadata_{};
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> blit_parameters_;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_state_;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_state_;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blend_state_;
		Microsoft::WRL::ComPtr<ID3D11Query> copy_fence_;
		D3D11_TEXTURE2D_DESC source_description_{};
		std::uint64_t generation_{};
	};
}
