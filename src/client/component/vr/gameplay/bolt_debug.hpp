#pragma once
#include "reload_debug.hpp"
#include "../spatial_line_label.hpp"

namespace vr::gameplay::weapons::physical_reload::bolt_debug
{
	struct sample : debug::sample_identity
	{
		hands::anchor gun{};
		hands::vec raw{}, contact{};
		mechanics::state state{};
		bool held{}, eligible{};
	};
	bool enabled() noexcept;
	void publish(const sample&) noexcept;
	inline std::array<spatial_lines::batch, 3> geometry_for(const sample& s, hands::vec placement) noexcept
	{
		using namespace hands;
		std::array<spatial_lines::batch, 3> out{};
		if (!s.definition || !s.definition->interaction.manual_bolt || !std::isfinite(s.units) || s.units <= 0)
			return out;
		const auto& p = *s.definition->interaction.manual_bolt;
		const auto world = [&](vec point) {
			return add(placement, add(s.gun.position, rotate(s.gun.rotation, scale(point, s.units))));
		};
		const auto line = [&](vec a, vec b, spatial_panel::vec4 color) { (void)out[0].add(world(a), world(b), color); };
		const auto cross = [&](vec point, float r, spatial_panel::vec4 color) {
			for (int axis = 0; axis < 3; ++axis)
			{
				auto a = point, b = point;
				a[axis] -= r;
				b[axis] += r;
				line(a, b, color);
			}
		};
		const auto lever = sub(s.contact, p.pivot);
		const auto radius = std::hypot(lever[1], lever[2]);
		const auto angle = std::atan2(lever[2], lever[1]);
		const auto point = [&](float lift, float travel) {
			return vec{s.contact[0] - travel * p.stroke, p.pivot[1] + radius * std::cos(angle + p.radians * lift),
			    p.pivot[2] + radius * std::sin(angle + p.radians * lift)};
		};
		for (int n = 0; n < 24; ++n)
			line(point(float(n) / 24, 0), point(float(n + 1) / 24, 0), {0, 1, 1, .8f});
		line(point(1, 0), point(1, 1), {0, 1, 1, .8f});
		cross(point(1, .85f), .007f, {1, .5f, 0, 1});
		cross(point(1, .95f), .007f, {1, 1, 0, 1});
		cross(point(s.state.bolt.lift, s.state.bolt.travel), .009f,
		    s.held ? spatial_panel::vec4{0, 1, 0, 1} : spatial_panel::vec4{1, 1, 0, 1});
		cross(s.raw, .004f, s.eligible ? spatial_panel::vec4{1, 1, 1, 1} : spatial_panel::vec4{1, 0, 0, 1});
		const std::string_view stage = !s.eligible                ? "RIGHT HAND REQUIRED"
		                               : s.state.bolt.lift == 0   ? "LOCKED"
		                               : s.state.bolt.lift < 1    ? "LIFT OR LOCK"
		                               : s.state.bolt.travel == 0 ? "PULL OR LOCK"
		                                                          : "PUSH OR PULL";
		const std::string_view chamber =
		    s.state.bolt.feeding ? "FEEDING LIVE ROUND"
		    : s.state.chamber_loaded
		        ? (mechanics::ready(s.definition->ammunition, s.state, s.held) ? "LIVE READY" : "LIVE BLOCKED")
		    : s.state.bolt.spent_case ? "SPENT CASE"
		                              : "EMPTY CHAMBER";
		(void)spatial_lines::label(out[1], world({p.pivot[0], .17f, .28f}), rotate(s.gun.rotation, {0, -1, 0}),
		    rotate(s.gun.rotation, {0, 0, 1}), .016f * s.units, stage, {1, 1, 1, 1});
		(void)spatial_lines::label(out[2], world({p.pivot[0], .17f, .25f}), rotate(s.gun.rotation, {0, -1, 0}),
		    rotate(s.gun.rotation, {0, 0, 1}), .016f * s.units, chamber, {1, .7f, .2f, 1});
		return out;
	}
} // namespace vr::gameplay::weapons::physical_reload::bolt_debug
