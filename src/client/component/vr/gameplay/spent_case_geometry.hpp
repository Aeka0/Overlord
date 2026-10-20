#pragma once
#include "component/scene_rigid_part.hpp"
#include "game/assets.hpp"
#include "hands/pose_math.hpp"
#include <map>
#include <climits>
#include <numeric>
#include <vector>

namespace vr::gameplay::weapons::spent_case
{
	using hands::vec;
	struct recipe
	{
		unsigned live_faces, case_faces, mouth_edges;
		vec live_extent, case_extent; // Native units, measured on the single-round source.
		float wall, depth;
		bool separate_body;
		vec long_axis{1, 0, 0};     // Source-space axis; some magazine cartridges are inclined.
		float cut_from_base{};      // Zero uses case_extent[0] for the original axial recipes.
		unsigned collapsed_faces{}; // Reviewed point triangles only; never arbitrary mesh repair.
	};
	// Authored from the matching live cartridge, not from a generic shell scale.
	inline constexpr recipe cheytac{286,
	                                202,
	                                12,
	                                {11.025465f / 2.54f, 1.732991f / 2.54f, 1.732988f / 2.54f},
	                                {7.9418f / 2.54f, 1.732991f / 2.54f, 1.732988f / 2.54f},
	                                .04f / 2.54f,
	                                5.f / 2.54f,
	                                false};
	inline constexpr recipe barrett{424,
	                                294,
	                                14,
	                                {14.9069f / 2.54f, 1.9962f / 2.54f, 2.0476f / 2.54f},
	                                {10.6661f / 2.54f, 1.9962f / 2.54f, 2.0476f / 2.54f},
	                                .05f / 2.54f,
	                                7.f / 2.54f,
	                                true};
	inline constexpr recipe deserteagle{322,
	                                    196,
	                                    18,
	                                    {4.16821349f / 2.54f, 1.51743406f / 2.54f, 1.75906420f / 2.54f},
	                                    {3.58373916f / 2.54f, 1.51743406f / 2.54f, 1.75906420f / 2.54f},
	                                    .04f / 2.54f,
	                                    2.2f / 2.54f,
	                                    false,
	                                    {.99700127f, 0, .07738519f},
	                                    3.47863990f / 2.54f};
	inline constexpr recipe wa2000{457,
	                               300,
	                               20,
	                               {7.95762444f / 2.54f, 1.41064227f / 2.54f, 1.41063786f / 2.54f},
	                               {5.92725468f / 2.54f, 1.41064227f / 2.54f, 1.41063786f / 2.54f},
	                               .04f / 2.54f,
	                               4.f / 2.54f,
	                               false,
	                               {1, 0, 0},
	                               0,
	                               1};
	inline constexpr recipe m14{545,
	                            368,
	                            16,
	                            {8.56777382f / 2.54f, 1.43720821f / 2.54f, 1.43707085f / 2.54f},
	                            {6.45314980f / 2.54f, 1.43720821f / 2.54f, 1.43707085f / 2.54f},
	                            .035f / 2.54f,
	                            4.f / 2.54f,
	                            true};

	inline vec position(const game::GfxPackedVertex& v) noexcept
	{
		return {v.xyz[0], v.xyz[1], v.xyz[2]};
	}
	inline vec unpack(unsigned p) noexcept
	{
		return {float(p & 1023) / 511.5f - 1,
		        float((p >> 10) & 1023) / 511.5f - 1,
		        float((p >> 20) & 1023) / 511.5f - 1};
	}
	inline unsigned pack(vec v, unsigned original) noexcept
	{
		v = hands::unit(v);
		unsigned result = original & 0xc0000000u;
		for (unsigned i = 0; i < 3; ++i)
			result |= unsigned(std::lround((std::clamp(v[i], -1.f, 1.f) + 1) * 511.5f)) << (10 * i);
		return result;
	}
	inline bool bounds(const std::vector<game::GfxPackedVertex>& vertices,
	                   std::span<const game::Face> faces,
	                   vec expected,
	                   vec& low,
	                   vec& high) noexcept
	{
		low.fill(10000);
		high.fill(-10000);
		for (const auto& f : faces)
			for (auto index : {f.v1, f.v2, f.v3})
			{
				if (index >= vertices.size())
					return false;
				const auto p = position(vertices[index]);
				for (unsigned a = 0; a < 3; ++a)
				{
					if (!std::isfinite(p[a]) || std::abs(p[a]) > 10000)
						return false;
					low[a] = std::min(low[a], p[a]);
					high[a] = std::max(high[a], p[a]);
				}
			}
		for (unsigned a = 0; a < 3; ++a)
			if (std::abs(high[a] - low[a] - expected[a]) > .002f)
				return false;
		return true;
	}
	// Bounded, once-per-asset authoring on a copied single-cartridge surface.
	// Welding only identifies the reviewed casing component and mouth boundary;
	// original outer vertices, UVs and materials remain unchanged.
	inline bool finish(std::vector<game::GfxPackedVertex>& vertices,
	                   std::vector<game::Face>& faces,
	                   const recipe& p)
	{
		using namespace hands;
		if (faces.size() != p.live_faces || faces.size() > 1024 || vertices.empty() ||
		    vertices.size() > 65535 || p.mouth_edges < 3 || p.mouth_edges > 64 || p.wall <= 0 || p.depth <= 0)
			return false;
		for (float value : p.long_axis)
			if (!std::isfinite(value))
				return false;
		if (std::abs(length(p.long_axis) - 1.f) > .001f || !std::isfinite(p.cut_from_base) ||
		    p.cut_from_base < 0)
			return false;
		vec low, high;
		if (!bounds(vertices, faces, p.live_extent, low, high))
			return false;
		std::map<std::array<int, 3>, unsigned> welded;
		std::vector<unsigned> ids(vertices.size(), UINT_MAX), representatives;
		for (const auto& f : faces)
			for (auto index : {f.v1, f.v2, f.v3})
			{
				const auto v = position(vertices[index]);
				std::array<int, 3> key;
				for (unsigned a = 0; a < 3; ++a)
					key[a] = int(std::lround(v[a] * 10000.f));
				auto [it, added] = welded.emplace(key, unsigned(welded.size()));
				if (added)
					representatives.push_back(index);
				ids[index] = it->second;
			}
		std::vector<unsigned> parents(welded.size());
		std::iota(parents.begin(), parents.end(), 0);
		const auto root = [&](unsigned n)
		{
			while (parents[n] != n)
			{
				parents[n] = parents[parents[n]];
				n = parents[n];
			}
			return n;
		};
		for (const auto& f : faces)
		{
			parents[root(ids[f.v2])] = root(ids[f.v1]);
			parents[root(ids[f.v3])] = root(ids[f.v1]);
		}
		std::vector<game::Face> body;
		if (p.separate_body)
		{
			std::map<unsigned, unsigned> counts;
			for (const auto& f : faces)
				++counts[root(ids[f.v1])];
			unsigned selected = UINT_MAX;
			for (const auto& [id, count] : counts)
				if (count == p.case_faces)
				{
					if (selected != UINT_MAX)
						return false;
					selected = id;
				}
			if (selected == UINT_MAX)
				return false;
			for (const auto& f : faces)
				if (root(ids[f.v1]) == selected)
					body.push_back(f);
		}
		else
		{
			float base = 10000;
			for (const auto& f : faces)
				for (auto index : {f.v1, f.v2, f.v3})
					base = std::min(base, dot(position(vertices[index]), p.long_axis));
			const float mouth = base + (p.cut_from_base > 0 ? p.cut_from_base : p.case_extent[0]);
			for (const auto& f : faces)
			{
				const float xs[]{dot(position(vertices[f.v1]), p.long_axis),
				                 dot(position(vertices[f.v2]), p.long_axis),
				                 dot(position(vertices[f.v3]), p.long_axis)};
				const float lo = *std::min_element(std::begin(xs), std::end(xs)),
				            hi = *std::max_element(std::begin(xs), std::end(xs));
				if (lo < mouth - .0005f && hi > mouth + .0005f)
					return false; // Never cut through a triangle.
				if (hi <= mouth + .0005f)
					body.push_back(f);
			}
		}
		// WA2000's casing contains one source triangle whose three positions
		// coincide. Validate/remove it only from the selected private case:
		// discarded bullet-tip geometry must not affect casing admission.
		const auto original_count = body.size();
		std::erase_if(body,
		              [&](const game::Face& f)
		              {
			              return position(vertices[f.v1]) == position(vertices[f.v2]) &&
			                     position(vertices[f.v1]) == position(vertices[f.v3]);
		              });
		if (original_count - body.size() != p.collapsed_faces)
			return false;
		if (body.size() != p.case_faces || !bounds(vertices, body, p.case_extent, low, high))
			return false;
		std::map<std::array<unsigned, 2>, unsigned> edges;
		std::map<std::array<unsigned, 2>, std::array<unsigned, 2>> directions;
		for (const auto& f : body)
		{
			const unsigned v[]{ids[f.v1], ids[f.v2], ids[f.v3]};
			for (unsigned i = 0; i < 3; ++i)
			{
				const auto a = v[i], b = v[(i + 1) % 3];
				if (a == b)
					return false;
				const std::array<unsigned, 2> key{std::min(a, b), std::max(a, b)};
				if (++edges[key] > 2)
					return false;
				directions[key] = {a, b};
			}
		}
		std::map<unsigned, std::vector<unsigned>> neighbors;
		for (const auto& [edge, count] : edges)
			if (count == 1)
			{
				neighbors[edge[0]].push_back(edge[1]);
				neighbors[edge[1]].push_back(edge[0]);
			}
		if (neighbors.size() != p.mouth_edges)
			return false;
		for (const auto& [id, adjacent] : neighbors)
			if (adjacent.size() != 2)
				return false;
		std::vector<unsigned> ring;
		unsigned previous = UINT_MAX, current = neighbors.begin()->first;
		do
		{
			if (ring.size() >= p.mouth_edges)
				return false;
			ring.push_back(representatives[current]);
			const auto& adjacent = neighbors.at(current);
			const unsigned next = adjacent[0] == previous ? adjacent[1] : adjacent[0];
			previous = current;
			current = next;
		} while (current != neighbors.begin()->first);
		if (ring.size() != p.mouth_edges)
			return false;
		vec mouth{}, axis{};
		for (auto v : ring)
			mouth = add(mouth, position(vertices[v]));
		mouth = scale(mouth, 1.f / float(ring.size()));
		for (size_t i = 0; i < ring.size(); ++i)
			axis = add(axis,
			           cross(sub(position(vertices[ring[i]]), mouth),
			                 sub(position(vertices[ring[(i + 1) % ring.size()]]), mouth)));
		if (length(axis) < .001f)
			return false;
		axis = unit(axis);
		if (dot(axis, p.long_axis) < 0)
			axis = scale(axis, -1);
		float axial_low = 10000, axial_high = -10000;
		for (const auto& f : body)
			for (auto index : {f.v1, f.v2, f.v3})
			{
				const auto axial = dot(position(vertices[index]), axis);
				axial_low = std::min(axial_low, axial);
				axial_high = std::max(axial_high, axial);
			}
		if (dot(axis, p.long_axis) < .98f || dot(mouth, axis) < axial_high - .1f ||
		    p.depth >= axial_high - axial_low)
			return false;
		std::vector<vec> inner, deep;
		for (auto v : ring)
		{
			const auto radial = sub(position(vertices[v]), mouth);
			if (std::abs(dot(radial, axis)) > .002f || length(radial) < p.wall * 4)
				return false;
			inner.push_back(sub(position(vertices[v]), scale(unit(radial), p.wall)));
			deep.push_back(sub(inner.back(), scale(axis, p.depth)));
		}
		faces = std::move(body);
		const auto first = position(vertices[ring[0]]), second = position(vertices[ring[1]]);
		const unsigned a_id = ids[ring[0]], b_id = ids[ring[1]];
		const auto directed = directions.at({std::min(a_id, b_id), std::max(a_id, b_id)});
		const float rim_sign = dot(cross(sub(second, first), sub(inner[1], first)), axis);
		const float winding = (rim_sign * (directed[0] == a_id ? -1.f : 1.f)) >= 0 ? 1.f : -1.f;
		const auto triangle =
		    [&](std::array<vec, 3> points, vec normal, const game::GfxPackedVertex& source, float shade)
		{
			if (dot(cross(sub(points[1], points[0]), sub(points[2], points[0])), normal) * winding < 0)
				std::swap(points[1], points[2]);
			game::Face f{};
			unsigned short* indices[]{&f.v1, &f.v2, &f.v3};
			for (unsigned i = 0; i < 3; ++i)
			{
				auto v = source;
				std::copy(points[i].begin(), points[i].end(), v.xyz);
				v.normal.packed = pack(normal, v.normal.packed);
				const auto tangent =
				    std::abs(dot(normal, axis)) > .9f ? unit(sub(points[1], points[0])) : axis;
				v.tangent.packed = pack(tangent, v.tangent.packed);
				for (unsigned c = 0; c < 3; ++c)
					v.color.array[c] = static_cast<unsigned char>(float(v.color.array[c]) * shade);
				*indices[i] = static_cast<unsigned short>(vertices.size());
				vertices.push_back(v);
			}
			faces.push_back(f);
		};
		if (vertices.size() + ring.size() * 15 > 65535)
			return false;
		const auto floor = sub(mouth, scale(axis, p.depth));
		for (size_t i = 0; i < ring.size(); ++i)
		{
			const size_t j = (i + 1) % ring.size();
			const auto source = vertices[ring[i]];
			const auto a = position(vertices[ring[i]]), b = position(vertices[ring[j]]);
			const auto inward = unit(sub(mouth, scale(add(a, b), .5f)));
			triangle({a, b, inner[j]}, axis, source, 1);
			triangle({a, inner[j], inner[i]}, axis, source, 1);
			triangle({inner[i], inner[j], deep[j]}, inward, source, .6f);
			triangle({inner[i], deep[j], deep[i]}, inward, source, .6f);
			triangle({deep[i], deep[j], floor}, axis, source, .25f);
		}
		// Canonical shell-FX frame: mouth toward -Y, transverse +X, centered.
		// This matches the native rifle-shell orientation without carrying its scale.
		const auto center = scale(add(low, high), .5f);
		const auto right = unit(cross(vec{0, 0, 1}, axis)), up = cross(axis, right);
		const auto rotate = [&](vec v) { return vec{dot(v, right), -dot(v, axis), dot(v, up)}; };
		for (auto& v : vertices)
		{
			const auto xyz = rotate(sub(position(v), center));
			std::copy(xyz.begin(), xyz.end(), v.xyz);
			v.normal.packed = pack(rotate(unpack(v.normal.packed)), v.normal.packed);
			v.tangent.packed = pack(rotate(unpack(v.tangent.packed)), v.tangent.packed);
		}
		return true;
	}
	inline bool create(scene_models::rigid_part& part, game::XModel* source, unsigned bone, const recipe& p)
	{
		if (!part.create(source, bone) || !part.model() || part.model()->numsurfs != 1)
			return false;
		return part.edit_mesh(0, [&](auto& vertices, auto& faces) { return finish(vertices, faces, p); });
	}
}
