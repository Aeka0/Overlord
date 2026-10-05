#pragma once
#include "component/scene_surface_layout.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace vr::gameplay::weapons
{
	struct skin_partition { bool valid{}; unsigned removed{}; std::vector<std::uint16_t> retained; };
	// H2 blend stream: vertices are grouped by influence count; each vertex
	// stores first boneOffset, then (boneOffset, weight) for added influences.
	// Only complete single-part triangles can be hidden. Mixed ownership fails
	// before publication, never truncating the receiver at the part boundary.
	inline skin_partition partition_skin(std::span<const std::int16_t,8> counts,
		std::span<const std::uint16_t> blend, std::span<const std::uint16_t> triangles,
		unsigned vertices,unsigned bones,const std::array<bool,256>& selected)
	{
		if (!vertices || vertices>65535 || !bones || bones>256 ||
			triangles.empty() || triangles.size()%3 || triangles.size()>65535*3) return {};
		size_t expected{}, vertex_count{};
		for (size_t i=0;i<8;++i) { if (counts[i]<0) return {}; vertex_count+=counts[i]; expected+=counts[i]*(2*i+1); }
		if (vertex_count!=vertices || expected!=blend.size()) return {};
		std::vector<bool> member(vertices); size_t at{},vertex{};
		for (size_t extra=0;extra<8;++extra) for (int n=0;n<counts[extra];++n,++vertex)
		{
			bool inside=false,outside=false;
			for (size_t k=0;k<=extra;++k)
			{
				const unsigned offset=blend[at+(k ? 2*k-1 : 0)];
				if (offset%64 || offset/64>=bones) return {};
				(selected[offset/64] ? inside : outside)=true;
			}
			if (inside && outside) return {};
			member[vertex]=inside; at+=2*extra+1;
		}
		skin_partition result; result.retained.reserve(triangles.size());
		for (size_t t=0;t<triangles.size();t+=3)
		{
			unsigned inside{};
			for (size_t k=0;k<3;++k) { if (triangles[t+k]>=vertices) return {}; inside+=member[triangles[t+k]]; }
			if (inside && inside!=3) return {};
			if (inside) ++result.removed;
			else result.retained.insert(result.retained.end(),triangles.begin()+t,triangles.begin()+t+3);
		}
		result.valid=true; return result;
	}
	inline skin_partition partition_skin(std::span<const std::int16_t,8> counts,
		std::span<const std::uint16_t> blend, std::span<const std::uint16_t> triangles,
		unsigned vertices,unsigned bones,unsigned selected)
	{
		if (selected>=bones || selected>=256) return {};
		std::array<bool,256> mask{}; mask[selected]=true;
		return partition_skin(counts,blend,triangles,vertices,bones,mask);
	}
	struct skin_packet_plan
	{
		bool valid{}; size_t count{};
		std::array<size_t,512> skinned{};
	};
	// Validate the ENTIRE active SkinSceneDObj stream before writes. This MOD
	// consumer must share the storage module's ABI too: a hardcoded old 4-byte
	// hidden stride rejects 8-byte streams and leaves the gun's magazine visible
	// while the independent magazine drops. Native patch checks cannot catch a
	// mismatched C++ parser. Keep the existing supported rigid-group bound.
	inline skin_packet_plan plan_skin_packets(std::span<const std::byte> bytes,unsigned surfaces) noexcept
	{
		if (!surfaces || surfaces>512 || bytes.empty() || bytes.size()>0x4000) return {};
		skin_packet_plan out; size_t offset{};
		for (unsigned i=0;i<surfaces;++i)
		{
			if (bytes.size()-offset<4) return {};
			std::int32_t header{}; std::memcpy(&header,bytes.data()+offset,4);
			const auto stride=scene_surface_storage::record_bytes(header);
			if (!stride || (header<-3 && -3-static_cast<std::int64_t>(header)>32)) return {};
			if (header>=0) out.skinned[out.count++]=offset;
			if (*stride>bytes.size()-offset) return {};
			offset+=*stride;
		}
		out.valid=offset==bytes.size(); return out;
	}
}
