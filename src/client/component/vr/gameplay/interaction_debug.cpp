#include <std_include.hpp>
#include "interaction_debug.hpp"
#include "weapon_interaction.hpp"
#include "../eye_composition.hpp"
#include "../spatial_lines_renderer.hpp"
#include "../native_waypoints.hpp"
#include "../engine_stereo_bridge.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>
#include <utils/hook.hpp>
#include <sstream>

namespace vr::gameplay::interaction::debug
{
#ifdef DEBUG
	namespace
	{
		std::atomic_bool world_on{}, body_on{}, alive{true};
		game::dvar_t *world_setting{}, *body_setting{};
		std::mutex mutex;
		std::array<world_sample, 2> world{};
		body_sample body{};
		supply_sample supply{};
		std::atomic_uint64_t draws{}, failures{}, misses{};
		using batch = spatial_lines::batch;
		using color = spatial_panel::vec4;
		constexpr color cyan{0, 1, 1, 1}, green{.2f, 1, .2f, 1}, yellow{1, .85f, 0, 1}, red{1, .15f, .15f, 1},
		    white{1, 1, 1, 1}, purple{.8f, .3f, 1, 1};
		void cross(batch& b, vec p, float radius, color c)
		{
			for (unsigned n = 0; n < 3; ++n)
			{
				auto a = p, z = p;
				a[n] -= radius;
				z[n] += radius;
				b.add(a, z, c);
			}
		}
		void box(batch& b, const oriented_volume& v, color c)
		{
			if (!v.valid)
				return;
			std::array<vec, 8> corners;
			for (unsigned n = 0; n < 8; ++n)
			{
				auto p = v.center;
				for (unsigned a = 0; a < 3; ++a)
					p[a] += ((n >> a) & 1 ? 1 : -1) * v.half[a];
				corners[n] = v.world(p);
			}
			for (unsigned n = 0; n < 8; ++n)
				for (unsigned a = 0; a < 3; ++a)
					if (!((n >> a) & 1))
						b.add(corners[n], corners[n | (1u << a)], c);
		}
		void circle(batch& b, vec center, vec a, vec z, float radius, color c, unsigned sides = 16)
		{
			for (unsigned n = 0; n < sides; ++n)
			{
				const float x = n * 6.28318530718f / sides, y = (n + 1) * 6.28318530718f / sides;
				const auto point = [&](float angle)
				{
					return hands::add(center,
					                  hands::scale(hands::add(hands::scale(a, std::cos(angle)),
					                                          hands::scale(z, std::sin(angle))),
					                               radius));
				};
				b.add(point(x), point(y), c);
			}
		}
		void sphere(batch& b, vec center, float radius, color c)
		{
			circle(b, center, {1, 0, 0}, {0, 1, 0}, radius, c);
			circle(b, center, {1, 0, 0}, {0, 0, 1}, radius, c);
			circle(b, center, {0, 1, 0}, {0, 0, 1}, radius, c);
		}
		bool fresh(clock::time_point at,
		           std::uint64_t reference,
		           const head_pose_bridge::spatial_frame& current)
		{
			const auto now = clock::now();
			return at != clock::time_point{} && now >= at && now - at <= 150ms &&
			       reference == current.generation;
		}
		batch world_lines(const world_sample& s)
		{
			batch out;
			const auto& r = s.aim;
			if (!valid(r))
				return out;
			const auto end = hands::add(r.origin, hands::scale(r.forward, r.units * r.distance_meters));
			out.add(r.origin, end, white);
			cross(out, r.origin, .025f * r.units, white);
			// Contact is sphere-versus-model-box distance, not a translated fixed
			// pickup sphere. Show the 12 cm hand tolerance as well as each model box.
			circle(out, r.origin, {1, 0, 0}, {0, 1, 0}, .12f * r.units, cyan, 12);
			circle(out, r.origin, {1, 0, 0}, {0, 0, 1}, .12f * r.units, cyan, 12);
			circle(out, r.origin, {0, 1, 0}, {0, 0, 1}, .12f * r.units, cyan, 12);
			const auto a = hands::rotate(hands::from_to({1, 0, 0}, r.forward), {0, 1, 0});
			const auto z = hands::rotate(hands::from_to({1, 0, 0}, r.forward), {0, 0, 1});
			const float radius = std::tan(r.half_angle_degrees * .01745329252f) * r.units * r.distance_meters;
			circle(out, end, a, z, radius, white);
			for (const auto side : {a, hands::scale(a, -1), z, hands::scale(z, -1)})
				out.add(r.origin, hands::add(end, hands::scale(side, radius)), white);
			for (unsigned n = 0; n < s.count; ++n)
			{
				const auto& v = s.candidates[n];
				const auto c = v.result == verdict::selected   ? green
				               : v.result == verdict::admitted ? cyan
				               : v.result == verdict::occluded ? red
				                                               : yellow;
				box(out, v.volume, c);
				// Pink cross is the old entity origin. It is deliberately separate
				// from the model box to expose short-weapon origin offsets.
				cross(out, v.native_center, .025f * r.units, purple);
				if (v.result == verdict::selected)
				{
					cross(out, v.scored.position, .03f * r.units, green);
					out.add(r.origin, v.scored.position, green);
				}
			}
			return out;
		}
		void reach_lines(batch& out, const body_reach_volume& v, color c)
		{
			// Three exact rounded cross sections, 36 segments per volume. All three
			// holsters plus hand markers fit the fixed 192-segment diagnostic batch.
			for (unsigned a = 0; a < 3; ++a)
			{
				const unsigned b = (a + 1) % 3, d = (a + 2) % 3;
				hands::vec center{};
				center[a] = (v.low[a] + v.high[a]) * .5f;
				for (unsigned edge = 0; edge < 2; ++edge)
				{
					const auto along = edge ? d : b, normal = edge ? b : d;
					for (unsigned side = 0; side < 2; ++side)
					{
						auto x = center, y = center;
						x[normal] = y[normal] = side ? v.high[normal] + v.radius : v.low[normal] - v.radius;
						x[along] = v.low[along];
						y[along] = v.high[along];
						out.add(v.world(x), v.world(y), c);
					}
				}
				for (unsigned corner = 0; corner < 4; ++corner)
				{
					auto p = center;
					p[b] = (corner & 1) ? v.high[b] : v.low[b];
					p[d] = (corner & 2) ? v.high[d] : v.low[d];
					const float sb = (corner & 1) ? 1.f : -1.f, sd = (corner & 2) ? 1.f : -1.f;
					const auto point = [&](unsigned k)
					{
						auto q = p;
						const float t = k * 1.57079632679f / 2;
						q[b] += sb * v.radius * std::cos(t);
						q[d] += sd * v.radius * std::sin(t);
						return v.world(q);
					};
					for (unsigned k = 0; k < 2; ++k)
						out.add(point(k), point(k + 1), c);
				}
			}
		}
		batch body_lines(const body_sample& s)
		{
			batch out;
			if (!s.slots.valid)
				return out;
			for (unsigned n = 0; n < 3; ++n)
			{
				bool contact = false;
				for (const auto p : s.hands)
					contact |= s.slots.volumes[n].contains(p);
				reach_lines(out, s.slots.volumes[n], contact ? green : s.weapons[n] ? purple : cyan);
			}
			for (const auto p : s.hands)
				cross(out, p, .03f * s.units, white);
			return out;
		}
		batch supply_lines(const supply_sample& s)
		{
			batch out;
			for (const auto& v : s.volumes)
				reach_lines(out, v, v.contains(s.wrist) ? green : yellow);
			cross(out, s.wrist, .03f * s.units, white);
			return out;
		}
		thread_local spatial_lines::renderer renderer;
		struct eye_pair
		{
			std::uint64_t id{}, device{};
			std::array<std::array<batch, 4>, 2> lines{};
			bool ready{}, left{};
		};
		thread_local eye_pair pair;
		void present(const eye_composition::event& event,
		             ID3D11DeviceContext* context,
		             ID3D11ShaderResourceView*,
		             ID3D11RenderTargetView* output) noexcept
		{
			if (event.eye > 1 || !alive)
				return;
			if (!event.eye)
			{
				pair.id = event.pair_id;
				pair.device = event.device_generation;
				pair.ready = pair.left = false;
				if (!world_on && !body_on)
					return;
				head_pose_bridge::spatial_frame frame;
				if (!event.model_origins.valid || !head_pose_bridge::get_spatial_frame(frame))
				{
					++misses;
					return;
				}
				std::array<world_sample, 2> w;
				body_sample b;
				supply_sample s;
				{
					const std::lock_guard lock(mutex);
					w = world;
					b = body;
					s = supply;
				}
				std::array<batch, 4> lines{};
				if (world_on)
					for (unsigned h = 0; h < 2; ++h)
						if (fresh(w[h].at, w[h].reference, frame))
							lines[h] = world_lines(w[h]);
				if (body_on)
				{
					if (fresh(b.at, b.reference, frame))
						lines[2] = body_lines(b);
					const auto owner = weapons::current_hold();
					if (fresh(s.at, s.reference, frame) && owner.id() == s.owner.id() &&
					    owner.rear_revision == s.owner.rear_revision)
						lines[3] = supply_lines(s);
				}
				for (unsigned eye = 0; eye < 2; ++eye)
				{
					if (event.views.eyes[eye].pair_id != event.pair_id ||
					    event.views.eyes[eye].output_eye != eye)
						return;
					spatial_panel::matrix vp{};
					std::memcpy(vp.data(),
					            event.views.eyes[eye].bytes.data() +
					                engine_stereo_view::h2_current_view_projection_offset,
					            sizeof(vp));
					for (unsigned n = 0; n < 4; ++n)
						pair.lines[eye][n] = spatial_lines::project(
						    lines[n], event.model_origins.eyes[eye], vp, .003f * frame.units_per_meter);
				}
				pair.ready = true;
			}
			if (!pair.ready || pair.id != event.pair_id || pair.device != event.device_generation ||
			    (event.eye && !pair.left))
				return;
			bool ok = true;
			for (const auto& lines : pair.lines[event.eye])
				if (lines.count)
				{
					auto outline = lines;
					for (unsigned n = 0; n < outline.count; ++n)
						outline.lines[n].color = {0, 0, 0, .95f};
					ok = renderer.draw(context, output, outline, event.width, event.height, 5.5f) && ok;
					ok = renderer.draw(context, output, lines, event.width, event.height) && ok;
				}
			if (!event.eye)
				pair.left = ok;
			if (ok)
				++draws;
			else
				++failures;
		}
		void dump()
		{
			std::array<world_sample, 2> w;
			body_sample b;
			supply_sample s;
			{
				const std::lock_guard lock(mutex);
				w = world;
				b = body;
				s = supply;
			}
			const auto age = [](clock::time_point at)
			{
				return at == clock::time_point{}
				           ? -1ll
				           : std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - at).count();
			};
			std::ostringstream out;
			out << "world=" << world_on << " body=" << body_on << " draws=" << draws
			    << " failures=" << failures << " misses=" << misses << '\n';
			for (unsigned h = 0; h < 2; ++h)
			{
				out << "hand=" << h << " age_ms=" << age(w[h].at)
				    << " native_candidates=" << w[h].native_count << " selected=" << w[h].selected.key.entity
				    << '\n';
				for (unsigned n = 0; n < w[h].count; ++n)
				{
					const auto& v = w[h].candidates[n];
					const auto c = v.volume.world(v.volume.center);
					constexpr std::array names{"geometry/range/cone",
					                           "occluded",
					                           "admitted",
					                           "selected",
					                           "native eligibility/identity"};
					constexpr std::array visibility_names{"not-tested", "blocked", "clear"};
					out << " entity=" << v.key.entity << " weapon=" << v.weapon
					    << " result=" << names[static_cast<unsigned>(v.result)]
					    << " contact=" << v.scored.contact << " cosine=" << v.scored.cosine
					    << " distance=" << v.scored.distance << " script_model=" << v.script_model
					    << " head_visibility=" << visibility_names[static_cast<unsigned>(v.head_visibility)]
					    << " hand_visibility=" << visibility_names[static_cast<unsigned>(v.hand_visibility)]
					    << " trace_point=" << v.trace_point[0] << ',' << v.trace_point[1] << ','
					    << v.trace_point[2] << " model_center=" << c[0] << ',' << c[1] << ',' << c[2]
					    << " native_center=" << v.native_center[0] << ',' << v.native_center[1] << ','
					    << v.native_center[2] << '\n';
				}
			}
			out << "slots_age_ms=" << age(b.at) << " supply_age_ms=" << age(s.at)
			    << " supply_weapon=" << s.owner.weapon << " supply_hand=" << s.hand << '\n';
			for (unsigned n = 0; n < 3; ++n)
				out << "slot=" << n << " weapon=" << b.weapons[n] << " center=" << b.slots.centers[n][0]
				    << ',' << b.slots.centers[n][1] << ',' << b.slots.centers[n][2]
				    << " radius=" << b.slots.radii[n] << '\n';
			const auto text = out.str();
			console::info("%s", text.c_str());
			scheduler::once(
			    [text] { utils::io::write_file_atomic("minidumps/overlord-interaction-debug.txt", text); },
			    scheduler::pipeline::async);
		}
		void labels()
		{
			if ((!world_on && !body_on) || !alive || !engine_stereo_bridge::is_active() ||
			    !game::CL_IsCgameInitialized() || *game::keyCatchers)
				return;
			head_pose_bridge::spatial_frame frame;
			if (!head_pose_bridge::get_spatial_frame(frame))
				return;
			const auto* placement = game::ScrPlace_GetViewPlacement();
			auto* font = game::R_RegisterFont("fonts/defaultBold.otf", 20);
			if (!placement || !font)
				return;
			std::array<world_sample, 2> w;
			body_sample b;
			supply_sample s;
			{
				const std::lock_guard lock(mutex);
				w = world;
				b = body;
				s = supply;
			}
			const auto label = [&](vec position, const char* text, color c)
			{
				const auto width = game::R_TextWidth(text, 128, font);
				if (width <= 0 || width > 1024)
					return;
				directional_ui::waypoint marker;
				marker.world = position;
				marker.world[2] += .07f * frame.units_per_meter;

				std::copy_n(placement->realViewportSize, 2, marker.viewport.begin());
				marker.tangent = {1, 1};
				marker.pixels_per_meter = 900;
				marker.anchor = {marker.viewport[0] * .5f, marker.viewport[1] * .5f};
				const float scale = std::min(1.f, 440.f / width), x = marker.anchor[0] - width * scale * .5f,
				            y = marker.anchor[1];
				native_waypoints::draw_world_marker(
				    marker,
				    [&] { game::R_AddCmdDrawText(text, 128, font, x, y, scale, scale, 0, c.data(), 4); },
				    directional_ui::rectangle{
				        x - 10 * scale, y - 30 * scale, x + width * scale + 10 * scale, y + 10 * scale});
			};
			if (world_on)
				for (unsigned h = 0; h < 2; ++h)
					if (fresh(w[h].at, w[h].reference, frame))
						for (unsigned n = 0; n < w[h].count; ++n)
						{
							const auto& v = w[h].candidates[n];
							if (!v.volume.valid)
								continue;
							constexpr std::array names{"RANGE/AIM", "BLOCKED", "CANDIDATE", "SELECTED"};
							char text[96];
							snprintf(text,
							         sizeof(text),
							         "%c #%d %s",
							         h ? 'R' : 'L',
							         v.key.entity,
							         names[static_cast<unsigned>(v.result)]);
							auto position = v.volume.world(v.volume.center);
							position[2] += (h ? .09f : .035f) * frame.units_per_meter;
							label(position,
							      text,
							      v.result == verdict::selected   ? green
							      : v.result == verdict::admitted ? cyan
							      : v.result == verdict::occluded ? red
							                                      : yellow);
						}
			if (body_on && fresh(b.at, b.reference, frame) && b.slots.valid)
			{
				constexpr std::array names{"LEFT WAIST", "RIGHT WAIST", "BACK"};
				for (unsigned n = 0; n < 3; ++n)
				{
					char text[96];
					snprintf(text, sizeof(text), "%s [%u]", names[n], b.weapons[n]);
					label(b.slots.centers[n], text, cyan);
				}
			}
			const auto owner = weapons::current_hold();
			if (body_on && fresh(s.at, s.reference, frame) && owner.id() == s.owner.id() &&
			    owner.rear_revision == s.owner.rear_revision)
				for (const auto& v : s.volumes)
					label(v.world(hands::scale(hands::add(v.low, v.high), .5f)), "AMMO SUPPLY", yellow);
		}
	}
	bool world_enabled() noexcept
	{
		return alive && world_on;
	}
	bool body_enabled() noexcept
	{
		return alive && body_on;
	}
	void publish_world(unsigned hand, const world_sample& s) noexcept
	{
		if (world_enabled() && hand < 2)
		{
			const std::lock_guard lock(mutex);
			world[hand] = s;
		}
	}
	void publish_body(const body_sample& s) noexcept
	{
		if (body_enabled())
		{
			const std::lock_guard lock(mutex);
			body = s;
		}
	}
	void publish_supply(const supply_sample& s) noexcept
	{
		if (body_enabled())
		{
			const std::lock_guard lock(mutex);
			supply = s;
		}
	}
	class component final : public component_interface
	{
		eye_composition::consumer_registration composition_registration;
		void post_unpack() override
		{
			world_setting = dvars::register_bool(
			    "vr_interactionDebug",
			    false,
			    0,
			    "World pickup x-ray: model bounds, aim cone, rejected/admitted/selected candidates");
			body_setting = dvars::register_bool(
			    "vr_bodySlotsDebug",
			    false,
			    0,
			    "Body slot and active ammunition supply volumes with actual hand sample");
			scheduler::loop(
			    []
			    {
				    if (alive)
				    {
					    world_on = world_setting->current.enabled;
					    body_on = body_setting->current.enabled;
				    }
			    },
			    scheduler::pipeline::main,
			    16ms);
			command::add("vr_interaction_debug_status", dump);
			scheduler::loop(labels, scheduler::pipeline::lui);
			composition_registration =
			    eye_composition::register_consumer(present, eye_composition::layer::interaction_diagnostics);
		}
		void pre_destroy() override
		{
			alive = false;
			world_on = body_on = false;
			composition_registration.reset();
		}
	};
#else
	bool world_enabled() noexcept
	{
		return false;
	}
	bool body_enabled() noexcept
	{
		return false;
	}
	void publish_world(unsigned, const world_sample&) noexcept
	{
	}
	void publish_body(const body_sample&) noexcept
	{
	}
	void publish_supply(const supply_sample&) noexcept
	{
	}
#endif
}
#ifdef DEBUG
REGISTER_COMPONENT(vr::gameplay::interaction::debug::component)
#endif
