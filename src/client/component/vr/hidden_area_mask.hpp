#pragma once

#include "engine_stereo_bridge.hpp"
#include "native_conversion_command_list.hpp"
#include "render_tuning.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace vr::hidden_area_mask
{
	struct vertex2
	{
		float x{};
		float y{};
	};

	struct diagnostics_snapshot
	{
		std::uint64_t stamps{};
		std::uint32_t eye_triangles[2]{};
		bool has_mesh[2]{};
	};

	namespace detail
	{
		inline constexpr std::size_t max_vertices = 16384;
		inline constexpr std::size_t max_indices = 65536;

		inline constexpr char vertex_shader_source[] =
			"float4 main(float2 pos : POSITION) : SV_Position\n"
			"{\n"
			"    return float4(pos, 1.0, 1.0);\n"
			"}\n";

		struct eye_gpu_state
		{
			Microsoft::WRL::ComPtr<ID3D11Buffer> vb;
			Microsoft::WRL::ComPtr<ID3D11Buffer> ib;
			UINT index_count{};
			std::uint64_t built_revision{};
		};

		struct state
		{
			std::mutex mutex;
			std::array<std::atomic<bool>, 2> has_mesh{};
			std::atomic<std::uint64_t> stamps{};

			std::array<std::vector<vertex2>, 2> vertices;
			std::array<std::vector<std::uint32_t>, 2> indices;
			std::array<engine_stereo_bridge::eye_projection, 2> projections{};
			std::array<bool, 2> projection_valid{};
			std::array<std::uint64_t, 2> mesh_revision{};

			Microsoft::WRL::ComPtr<ID3D11Device> device_;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
			Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
			Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
			Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
			std::array<eye_gpu_state, 2> eye_gpu;
		};

		inline state& shared() noexcept
		{
			static state instance;
			return instance;
		}

		inline void bump_revision_locked(state& s, const std::uint32_t eye) noexcept
		{
			if (++s.mesh_revision[eye] == 0)
			{
				s.mesh_revision[eye] = 1;
			}
		}

		inline void reset_device_locked(state& s) noexcept
		{
			s.device_.Reset();
			s.deferred_.Reset();
			s.vs_.Reset();
			s.layout_.Reset();
			s.raster_.Reset();
			s.depth_.Reset();
			s.blend_.Reset();
			for (auto& gpu : s.eye_gpu)
			{
				gpu.vb.Reset();
				gpu.ib.Reset();
				gpu.index_count = 0;
				gpu.built_revision = 0;
			}
		}

		[[nodiscard]] inline bool init_device_locked(state& s,
			ID3D11Device* const device) noexcept
		{
			reset_device_locked(s);
			if (device == nullptr)
			{
				return false;
			}

			Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred;
			if (FAILED(device->CreateDeferredContext(0, &deferred)) || !deferred ||
				!native_conversion_command_list::mark_recording_context(deferred.Get()))
			{
				return false;
			}

			Microsoft::WRL::ComPtr<ID3DBlob> vs_blob;
			Microsoft::WRL::ComPtr<ID3DBlob> error_blob;
			if (FAILED(D3DCompile(vertex_shader_source,
					sizeof(vertex_shader_source) - 1,
					"hidden-area-mask",
					nullptr,
					nullptr,
					"main",
					"vs_5_0",
					D3DCOMPILE_OPTIMIZATION_LEVEL3,
					0,
					&vs_blob,
					&error_blob)) ||
				!vs_blob)
			{
				return false;
			}

			Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
			if (FAILED(device->CreateVertexShader(vs_blob->GetBufferPointer(),
					vs_blob->GetBufferSize(), nullptr, &vs)) ||
				!vs)
			{
				return false;
			}

			const D3D11_INPUT_ELEMENT_DESC input_desc{
				"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
				D3D11_INPUT_PER_VERTEX_DATA, 0};
			Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
			if (FAILED(device->CreateInputLayout(&input_desc, 1,
					vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &layout)) ||
				!layout)
			{
				return false;
			}

			D3D11_RASTERIZER_DESC raster_desc{};
			raster_desc.FillMode = D3D11_FILL_SOLID;
			raster_desc.CullMode = D3D11_CULL_NONE;
			raster_desc.DepthClipEnable = TRUE;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
			if (FAILED(device->CreateRasterizerState(&raster_desc, &raster)) || !raster)
			{
				return false;
			}

			D3D11_DEPTH_STENCIL_DESC depth_desc{};
			depth_desc.DepthEnable = TRUE;
			depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depth_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
			depth_desc.StencilEnable = FALSE;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;
			if (FAILED(device->CreateDepthStencilState(&depth_desc, &depth)) || !depth)
			{
				return false;
			}

			D3D11_BLEND_DESC blend_desc{};
			blend_desc.RenderTarget[0].RenderTargetWriteMask = 0;
			Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
			if (FAILED(device->CreateBlendState(&blend_desc, &blend)) || !blend)
			{
				return false;
			}

			s.device_ = device;
			s.deferred_ = std::move(deferred);
			s.vs_ = std::move(vs);
			s.layout_ = std::move(layout);
			s.raster_ = std::move(raster);
			s.depth_ = std::move(depth);
			s.blend_ = std::move(blend);
			return true;
		}

		[[nodiscard]] inline bool rebuild_eye_buffers_locked(state& s,
			const std::uint32_t eye, const std::uint64_t revision) noexcept
		{
			auto& gpu = s.eye_gpu[eye];
			gpu.vb.Reset();
			gpu.ib.Reset();
			gpu.index_count = 0;
			gpu.built_revision = 0;

			if (!s.device_ || !s.projection_valid[eye] || !s.projections[eye].valid() ||
				s.vertices[eye].empty() || s.indices[eye].size() < 3 ||
				(s.indices[eye].size() % 3) != 0)
			{
				return false;
			}

			const auto& proj = s.projections[eye];
			const float tan_width = proj.tan_right - proj.tan_left;
			const float tan_height = proj.tan_up - proj.tan_down;
			if (!std::isfinite(tan_width) || !std::isfinite(tan_height) ||
				tan_width <= 0.0f || tan_height <= 0.0f)
			{
				return false;
			}

			std::vector<vertex2> ndc_vertices;
			try
			{
				ndc_vertices.reserve(s.vertices[eye].size());
				for (const auto& v : s.vertices[eye])
				{
					const float ndc_x = 2.0f * (v.x - proj.tan_left) / tan_width - 1.0f;
					const float ndc_y = 2.0f * (v.y - proj.tan_down) / tan_height - 1.0f;
					if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y))
					{
						return false;
					}
					ndc_vertices.push_back(vertex2{ndc_x, ndc_y});
				}
			}
			catch (...)
			{
				return false;
			}

			D3D11_BUFFER_DESC vb_desc{};
			vb_desc.ByteWidth = static_cast<UINT>(ndc_vertices.size() * sizeof(vertex2));
			vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
			vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA vb_data{};
			vb_data.pSysMem = ndc_vertices.data();

			Microsoft::WRL::ComPtr<ID3D11Buffer> vb;
			if (FAILED(s.device_->CreateBuffer(&vb_desc, &vb_data, &vb)) || !vb)
			{
				return false;
			}

			D3D11_BUFFER_DESC ib_desc{};
			ib_desc.ByteWidth = static_cast<UINT>(s.indices[eye].size() * sizeof(std::uint32_t));
			ib_desc.Usage = D3D11_USAGE_IMMUTABLE;
			ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA ib_data{};
			ib_data.pSysMem = s.indices[eye].data();

			Microsoft::WRL::ComPtr<ID3D11Buffer> ib;
			if (FAILED(s.device_->CreateBuffer(&ib_desc, &ib_data, &ib)) || !ib)
			{
				return false;
			}

			gpu.vb = std::move(vb);
			gpu.ib = std::move(ib);
			gpu.index_count = static_cast<UINT>(s.indices[eye].size());
			gpu.built_revision = revision;
			return true;
		}
	}

	inline void set_eye_mesh(const std::uint32_t eye,
		const vertex2* const vertices, const std::size_t vertex_count,
		const std::uint32_t* const indices, const std::size_t index_count) noexcept
	{
		if (eye >= 2)
		{
			return;
		}

		auto& s = detail::shared();
		if (vertices == nullptr || vertex_count == 0 ||
			indices == nullptr || index_count < 3)
		{
			const std::lock_guard lock(s.mutex);
			s.has_mesh[eye].store(false, std::memory_order_release);
			s.vertices[eye].clear();
			s.indices[eye].clear();
			s.eye_gpu[eye] = {};
			detail::bump_revision_locked(s, eye);
			return;
		}

		if (vertex_count > detail::max_vertices ||
			index_count > detail::max_indices ||
			(index_count % 3) != 0)
		{
			return;
		}

		for (std::size_t i{}; i < vertex_count; ++i)
		{
			if (!std::isfinite(vertices[i].x) || !std::isfinite(vertices[i].y))
			{
				return;
			}
		}

		for (std::size_t i{}; i < index_count; ++i)
		{
			if (indices[i] >= vertex_count)
			{
				return;
			}
		}

		std::vector<vertex2> local_vertices;
		std::vector<std::uint32_t> local_indices;
		try
		{
			local_vertices.assign(vertices, vertices + vertex_count);
			local_indices.assign(indices, indices + index_count);
		}
		catch (...)
		{
			return;
		}

		const std::lock_guard lock(s.mutex);
		s.vertices[eye] = std::move(local_vertices);
		s.indices[eye] = std::move(local_indices);
		detail::bump_revision_locked(s, eye);
		s.has_mesh[eye].store(true, std::memory_order_release);
	}

	inline void set_eye_projection(const std::uint32_t eye,
		const engine_stereo_bridge::eye_projection& projection) noexcept
	{
		if (eye >= 2 || !projection.valid())
		{
			return;
		}

		auto& s = detail::shared();
		const std::lock_guard lock(s.mutex);
		const auto& current = s.projections[eye];
		if (s.projection_valid[eye] &&
			current.tan_left == projection.tan_left &&
			current.tan_right == projection.tan_right &&
			current.tan_down == projection.tan_down &&
			current.tan_up == projection.tan_up)
		{
			return;
		}

		s.projections[eye] = projection;
		s.projection_valid[eye] = true;
		detail::bump_revision_locked(s, eye);
	}

	inline void clear() noexcept
	{
		auto& s = detail::shared();
		const std::lock_guard lock(s.mutex);
		for (std::uint32_t eye{}; eye < 2; ++eye)
		{
			s.has_mesh[eye].store(false, std::memory_order_release);
			s.vertices[eye].clear();
			s.indices[eye].clear();
			s.projections[eye] = {};
			s.projection_valid[eye] = false;
			s.eye_gpu[eye] = {};
			detail::bump_revision_locked(s, eye);
		}
	}

	inline void invalidate_device() noexcept
	{
		auto& s = detail::shared();
		const std::lock_guard lock(s.mutex);
		detail::reset_device_locked(s);
	}

	inline bool stamp_eye_depth(ID3D11DeviceContext* const context,
		ID3D11DepthStencilView* const dsv, const std::uint32_t eye,
		const std::uint32_t expected_width,
		const std::uint32_t expected_height) noexcept
	{
		if (!render_tuning::hidden_area_mesh.load(std::memory_order_relaxed))
		{
			return false;
		}
		if (eye >= 2 || context == nullptr || dsv == nullptr ||
			expected_width == 0 || expected_height == 0)
		{
			return false;
		}
		auto& s = detail::shared();
		if (!s.has_mesh[eye].load(std::memory_order_acquire))
		{
			return false;
		}
		if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
		{
			return false;
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
		dsv->GetDesc(&dsv_desc);
		if (dsv_desc.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D ||
			dsv_desc.Texture2D.MipSlice != 0 ||
			(dsv_desc.Flags & D3D11_DSV_READ_ONLY_DEPTH) != 0)
		{
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11Resource> res;
		dsv->GetResource(&res);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
		if (!res || FAILED(res.As(&tex)) || !tex)
		{
			return false;
		}
		D3D11_TEXTURE2D_DESC tex_desc{};
		tex->GetDesc(&tex_desc);
		if (tex_desc.Width != expected_width ||
			tex_desc.Height != expected_height ||
			tex_desc.SampleDesc.Count != 1 ||
			tex_desc.ArraySize != 1)
		{
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11Device> ctx_dev;
		context->GetDevice(&ctx_dev);
		Microsoft::WRL::ComPtr<ID3D11Device> dsv_dev;
		dsv->GetDevice(&dsv_dev);
		if (!ctx_dev || ctx_dev.Get() != dsv_dev.Get())
		{
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11CommandList> commands;
		{
			const std::lock_guard lock(s.mutex);
			if (!s.has_mesh[eye].load(std::memory_order_acquire) ||
				!s.projection_valid[eye] || !s.projections[eye].valid() ||
				s.vertices[eye].empty() || s.indices[eye].size() < 3 ||
				(s.indices[eye].size() % 3) != 0)
			{
				return false;
			}

			if (s.device_.Get() != ctx_dev.Get() || !s.deferred_ || !s.vs_ ||
				!s.layout_ || !s.raster_ || !s.depth_ || !s.blend_)
			{
				if (!detail::init_device_locked(s, ctx_dev.Get()))
				{
					return false;
				}
			}

			auto& gpu = s.eye_gpu[eye];
			const auto revision = s.mesh_revision[eye];
			if (gpu.built_revision != revision || !gpu.vb || !gpu.ib)
			{
				if (!detail::rebuild_eye_buffers_locked(s, eye, revision))
				{
					return false;
				}
			}
			if (!gpu.vb || !gpu.ib || gpu.index_count < 3)
			{
				return false;
			}

			auto* const deferred = s.deferred_.Get();
			deferred->OMSetRenderTargets(0, nullptr, dsv);
			deferred->OMSetDepthStencilState(s.depth_.Get(), 0);
			deferred->OMSetBlendState(s.blend_.Get(), nullptr, D3D11_DEFAULT_SAMPLE_MASK);
			deferred->RSSetState(s.raster_.Get());
			const D3D11_VIEWPORT vp{
				0.0f, 0.0f,
				static_cast<float>(expected_width),
				static_cast<float>(expected_height),
				0.0f, 1.0f};
			deferred->RSSetViewports(1, &vp);
			deferred->IASetInputLayout(s.layout_.Get());
			deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ID3D11Buffer* const vb = gpu.vb.Get();
			const UINT stride = sizeof(vertex2);
			const UINT offset = 0;
			deferred->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
			deferred->IASetIndexBuffer(gpu.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
			deferred->VSSetShader(s.vs_.Get(), nullptr, 0);
			deferred->PSSetShader(nullptr, nullptr, 0);
			deferred->DrawIndexed(gpu.index_count, 0, 0);

			if (FAILED(deferred->FinishCommandList(FALSE, &commands)) || !commands)
			{
				deferred->ClearState();
				return false;
			}
			if (!native_conversion_command_list::mark(commands.Get()))
			{
				return false;
			}
		}

		// Restores 100% of H2's immediate-context state via D3D11 RestoreContextState=TRUE.
		context->ExecuteCommandList(commands.Get(), TRUE);
		s.stamps.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	inline diagnostics_snapshot get_diagnostics() noexcept
	{
		auto& s = detail::shared();
		diagnostics_snapshot snapshot{};
		snapshot.stamps = s.stamps.load(std::memory_order_relaxed);
		const std::lock_guard lock(s.mutex);
		for (std::size_t eye{}; eye < 2; ++eye)
		{
			snapshot.has_mesh[eye] = s.has_mesh[eye].load(std::memory_order_acquire);
			snapshot.eye_triangles[eye] = static_cast<std::uint32_t>(s.indices[eye].size() / 3u);
		}
		return snapshot;
	}
}
