#include <std_include.hpp>
#include "first_person_mesh_size.hpp"
#include "hands/pose_solver.hpp"
#include "game/assets.hpp"
#include <utils/native_memory.hpp>
#include <wrl/client.h>
#include <bit>
#include <map>
#include <mutex>
#include <tuple>

namespace vr::gameplay::first_person_mesh_size
{
	namespace
	{
		struct witness
		{
			const void* object{};
			const void* matrices{};
			std::uint32_t epoch{};
			float size{1.f};
		};
		struct vertex_copy
		{
			std::vector<game::GfxPackedVertex> vertices;
			Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
			Microsoft::WRL::ComPtr<ID3D11Buffer> source_buffer;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> source_view;
		};
		struct surface_copy
		{
			game::XSurface surface{};
			std::shared_ptr<vertex_copy> geometry;
		};
		using surface_key = std::tuple<const game::XSurface*, ID3D11Buffer*, std::uint32_t>;
		using vertex_key = std::tuple<const game::GfxPackedVertex*, ID3D11Buffer*, ID3D11ShaderResourceView*, unsigned, std::uint32_t>;
		using bind_key = std::tuple<const hands::bone*, unsigned, std::uint32_t>;
		std::mutex mutex;
		std::array<witness, 128> witnesses{};
		std::size_t cursor{};
		std::map<surface_key, std::unique_ptr<surface_copy>> surfaces;
		std::map<vertex_key, std::shared_ptr<vertex_copy>> vertices;
		std::map<bind_key, std::vector<hands::bone>> binds;
		std::atomic_uint64_t packets{}, rejected{};
		std::atomic_bool has_sizes{};

		std::shared_ptr<vertex_copy> sized_vertices(const game::XSurface& original,float size)
		{
			const vertex_key key{original.verts0.packedVerts0,original.vb0,original.vb0View,original.vertCount,std::bit_cast<std::uint32_t>(size)};
			{
				const std::lock_guard lock(mutex);
				if (const auto found=vertices.find(key);found!=vertices.end())return found->second;
			}
			// These are the same packed rigid/skinned vertex formats admitted by
			// the existing first-person part adapter. Topology/materials stay shared.
			if (!original.vertCount || !original.verts0.packedVerts0 || !original.vb0 ||
			    original.subdivLevelCount || original.subdiv || original.blendShapesCount) return nullptr;
			auto copy = std::make_shared<vertex_copy>();
			copy->source_buffer=original.vb0;
			copy->source_view=original.vb0View;
			copy->vertices.assign(original.verts0.packedVerts0, original.verts0.packedVerts0 + original.vertCount);
			for (auto& vertex : copy->vertices) for (auto& coordinate : vertex.xyz) coordinate *= size;
			D3D11_BUFFER_DESC descriptor{};
			original.vb0->GetDesc(&descriptor);
			const auto bytes = copy->vertices.size() * sizeof(game::GfxPackedVertex);
			if (descriptor.ByteWidth < bytes) return nullptr;
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			std::size_t offset{};
			if (original.vb0View)
			{
				original.vb0View->GetDesc(&view);
				if (view.Buffer.FirstElement)
				{
					// Raw skin streams address 32-bit words; structured streams use
					// their declared stride. Keep a subview's original byte origin.
					if (!descriptor.StructureByteStride && view.Format!=DXGI_FORMAT_R32_TYPELESS &&
					    view.Format!=DXGI_FORMAT_R32_FLOAT && view.Format!=DXGI_FORMAT_R32_UINT && view.Format!=DXGI_FORMAT_R32_SINT) return nullptr;
					const auto element=descriptor.StructureByteStride?descriptor.StructureByteStride:4u;
					offset=std::size_t(view.Buffer.FirstElement)*element;
					if (offset>descriptor.ByteWidth-bytes) return nullptr;
				}
			}
			std::vector<std::byte> data(descriptor.ByteWidth);
			std::memcpy(data.data()+offset, copy->vertices.data(), bytes);
			descriptor.Usage = D3D11_USAGE_IMMUTABLE;
			descriptor.CPUAccessFlags = 0;
			D3D11_SUBRESOURCE_DATA initial{data.data(), 0, 0};
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			original.vb0->GetDevice(&device);
			if (FAILED(device->CreateBuffer(&descriptor, &initial, &copy->buffer))) return nullptr;
			if (original.vb0View)
			{
				if (FAILED(device->CreateShaderResourceView(copy->buffer.Get(), &view, &copy->view))) return nullptr;
			}
			const std::lock_guard lock(mutex);
			const auto [entry,inserted]=vertices.try_emplace(key,std::move(copy));
			return entry->second;
		}
		const game::XSurface* sized_surface(const game::XSurface* source,float size)
		{
			const auto original=*source;
			const surface_key key{source,original.vb0,std::bit_cast<std::uint32_t>(size)};
			{
				const std::lock_guard lock(mutex);
				if(const auto found=surfaces.find(key);found!=surfaces.end())return &found->second->surface;
			}
			auto geometry=sized_vertices(original,size);
			if(!geometry)return nullptr;
			auto copy=std::make_unique<surface_copy>();copy->surface=original;copy->geometry=std::move(geometry);
			copy->surface.verts0.packedVerts0=copy->geometry->vertices.data();
			copy->surface.vb0=copy->geometry->buffer.Get();
			copy->surface.vb0View=copy->geometry->view.Get();
			const std::lock_guard lock(mutex);
			const auto [entry,inserted]=surfaces.try_emplace(key,std::move(copy));
			return &entry->second->surface;
		}

		const hands::bone* sized_bind(const hands::bone* source, unsigned count, float size)
		{
			const bind_key key{source, count, std::bit_cast<std::uint32_t>(size)};
			const std::lock_guard lock(mutex);
			if (const auto found = binds.find(key); found != binds.end()) return found->second.data();
			auto& copy = binds[key];
			copy.assign(source, source + count);
			for (auto& bone : copy) for (auto& coordinate : bone.position) coordinate *= size;
			return copy.data();
		}
	}

	void publish(const void* object, const void* matrices, std::uint32_t epoch, float size) noexcept
	{
		if (size==1.f && !has_sizes.load(std::memory_order_relaxed)) return;
		const std::lock_guard lock(mutex);
		witnesses[cursor++ % witnesses.size()] = {object, matrices, epoch, size};
		has_sizes.store(true,std::memory_order_release);
	}
	float for_object(const void* object) noexcept
	{
		if (!has_sizes.load(std::memory_order_acquire)) return 1.f;
		const void* matrices{};
		std::uint32_t epoch{};
		if (!object || !utils::native_memory::read_bytes(&matrices, static_cast<const std::byte*>(object) + 0xa8, sizeof(matrices)) ||
		    !utils::native_memory::read_bytes(&epoch, static_cast<const std::byte*>(object) + 0xb0, sizeof(epoch))) return 1.f;
		const std::lock_guard lock(mutex);
		for (std::size_t n = 0; n < witnesses.size(); ++n)
		{
			const auto& value = witnesses[(cursor + witnesses.size() - 1 - n) % witnesses.size()];
			if (value.object == object && value.matrices == matrices && value.epoch == epoch) return value.size;
		}
		return 1.f;
	}
	bool prepare_packet(void* packet, float size, packet_data data)
	{
		if (size == 1.f) return true;
		auto* bytes = static_cast<std::byte*>(packet);
		const game::XSurface* surface{};
		const hands::bone* inverse_bind{};
		if (data!=packet_data::inverse_bind)
		{
			const game::XSurface* source{};std::memcpy(&source,bytes+0x28,sizeof(source));
			if (!source || !(surface=sized_surface(source,size))) { ++rejected;return false; }
		}
		if (data!=packet_data::surface)
		{
			const hands::bone* bind{};std::memcpy(&bind,bytes+8,sizeof(bind));
			const auto count=std::to_integer<unsigned>(bytes[0x11]);
			if (!bind || !count) { ++rejected;return false; }
			inverse_bind=sized_bind(bind,count,size);
		}
		if (surface) {std::memcpy(bytes+0x28,&surface,sizeof(surface));++packets;}
		if (inverse_bind)std::memcpy(bytes+8,&inverse_bind,sizeof(inverse_bind));
		return true;
	}
	void clear_after_drain() noexcept
	{
		const std::lock_guard lock(mutex);
		witnesses = {}; cursor = 0; surfaces.clear(); vertices.clear(); binds.clear();has_sizes=false;
	}
	std::string status()
	{
		const std::lock_guard lock(mutex);
		return std::format("first_person_mesh_size surfaces={} vertex_buffers={} binds={} packets={} rejected={}\n",
		    surfaces.size(), vertices.size(), binds.size(), packets.load(), rejected.load());
	}
}
