#include <std_include.hpp>
#include "reload_well_geometry.hpp"
#include "hk_slap_geometry.hpp"
#include "bolt_debug.hpp"
#include "cover_push_debug.hpp"
#include "weapon_render_pose.hpp"
#include "../eye_composition.hpp"
#include "../debug_options.hpp"
#include "../spatial_lines_renderer.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>
#include <sstream>

namespace vr::gameplay::weapons::physical_reload::debug
{
	namespace
	{
		std::atomic_bool alive{true};
		template <class Sample, size_t Capacity = 128> struct channel
		{
			std::atomic_bool requested{};
			std::mutex mutex;
			std::unique_ptr<sample_history<Sample, Capacity>> history;
			std::atomic_uint64_t draws{}, misses{}, failures{};
			bool enabled() const noexcept
			{
				return alive.load(std::memory_order_relaxed) && requested.load(std::memory_order_acquire);
			}
			void initialize(debug_options::probe option)
			{
				if (!debug_options::enabled(option))
					return;
				history = std::make_unique<sample_history<Sample, Capacity>>();
				requested.store(true, std::memory_order_release);
			}
			void publish(const Sample& sample) noexcept
			{
				if (!enabled())
					return;
				const std::lock_guard lock(mutex);
				history->push(sample);
			}
			Sample newest() noexcept
			{
				const std::lock_guard lock(mutex);
				return history ? history->newest() : Sample{};
			}
		};
		channel<well_debug::sample> well;
		channel<hk_slap_debug::sample> slap;
		channel<bolt_debug::sample> bolt;
		channel<cover_debug::sample, 4096> cover;
		struct overlay
		{
			std::array<std::array<spatial_lines::batch, 3>, 2> lines{};
			bool ready{}, left_drawn{};
		};
		struct eye_pair
		{
			std::uint64_t id{}, device{};
			std::array<overlay, 4> layers{};
		};
		thread_local eye_pair pair;
		thread_local spatial_lines::renderer renderer;
		template <class Sample, size_t Capacity, class Geometry>
		void prepare(channel<Sample, Capacity>& source,
		             overlay& target,
		             const eye_composition::event& event,
		             const weapon_render_pose::snapshot& rendered,
		             Geometry&& geometry) noexcept
		{
			if (!source.enabled())
				return;
			Sample selected;
			{
				const std::lock_guard lock(source.mutex);
				if (!source.history->select(rendered.object,
				                            rendered.matrices,
				                            rendered.epoch,
				                            rendered.muzzle.owner,
				                            rendered.muzzle.reference_generation,
				                            clock::now(),
				                            selected))
				{
					++source.misses;
					return;
				}
			}
			const auto world = geometry(selected, event.model_origins.placement);
			for (unsigned eye = 0; eye < 2; ++eye)
			{
				if (event.views.eyes[eye].pair_id != event.pair_id || event.views.eyes[eye].output_eye != eye)
				{
					++source.misses;
					return;
				}
				spatial_panel::matrix vp{};
				std::memcpy(vp.data(),
				            event.views.eyes[eye].bytes.data() +
				                engine_stereo_view::h2_current_view_projection_offset,
				            sizeof(vp));
				for (size_t n = 0; n < world.size(); ++n)
					target.lines[eye][n] = spatial_lines::project(
					    world[n], event.model_origins.eyes[eye], vp, .003f * selected.units);
			}
			target.ready = true;
		}
		template <class Sample, size_t Capacity>
		void draw(channel<Sample, Capacity>& source,
		          overlay& target,
		          const eye_composition::event& event,
		          ID3D11DeviceContext* context,
		          ID3D11RenderTargetView* output) noexcept
		{
			if (!target.ready || (event.eye == 1 && !target.left_drawn))
				return;
			bool ok = true;
			for (size_t n = 0; n < target.lines[event.eye].size(); ++n)
			{
				const auto& lines = target.lines[event.eye][n];
				if (!lines.count)
					continue;
				if (n)
				{
					auto outline = lines;
					for (size_t i = 0; i < outline.count; ++i)
						outline.lines[i].color = {0, 0, 0, .9f};
					ok = renderer.draw(context, output, outline, event.width, event.height, 5.5f) && ok;
				}
				ok = renderer.draw(context, output, lines, event.width, event.height) && ok;
			}
			if (ok)
				++source.draws;
			else
				++source.failures;
			if (event.eye == 0)
				target.left_drawn = ok;
		}
		void present(const eye_composition::event& event,
		             ID3D11DeviceContext* context,
		             ID3D11ShaderResourceView*,
		             ID3D11RenderTargetView* output) noexcept
		{
			if (event.eye > 1)
				return;
			if (event.eye == 0)
			{
				pair.id = event.pair_id;
				pair.device = event.device_generation;
				for (auto& layer : pair.layers)
				{
					layer.ready = false;
					layer.left_drawn = false;
				}
				if (!well.enabled() && !slap.enabled() && !bolt.enabled() && !cover.enabled())
					return;
				weapon_render_pose::snapshot rendered;
				if (!event.model_origins.valid || !weapon_render_pose::for_scene(event.views, rendered))
				{
					if (well.enabled())
						++well.misses;
					if (slap.enabled())
						++slap.misses;
					if (cover.enabled())
						++cover.misses;
					return;
				}
				prepare(
				    well,
				    pair.layers[0],
				    event,
				    rendered,
				    [](const auto& s, hands::vec placement)
				    { return std::array<spatial_lines::batch, 1>{well_debug::geometry_for(s, placement)}; });
				prepare(slap, pair.layers[1], event, rendered, hk_slap_debug::geometry_for);
				prepare(bolt, pair.layers[2], event, rendered, bolt_debug::geometry_for);
				prepare(cover, pair.layers[3], event, rendered, cover_debug::geometry_for);
			}
			// Freeze both toggles and all geometry at the left-eye boundary.
			if (pair.id != event.pair_id || pair.device != event.device_generation)
				return;
			draw(well, pair.layers[0], event, context, output);
			draw(slap, pair.layers[1], event, context, output);
			draw(bolt, pair.layers[2], event, context, output);
			draw(cover, pair.layers[3], event, context, output);
		}
		long long age(clock::time_point at) noexcept
		{
			return at == clock::time_point{}
			           ? -1
			           : std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - at).count();
		}
		void cover_text(std::ostream& out, const cover_debug::sample& s)
		{
			out << "seq=" << s.input_sequence << " sim_seq=" << s.simulation_sequence
			    << " age_ms=" << age(s.at)
			    << " profile=" << (s.definition ? s.definition->id : std::string_view{})
			    << " cover=" << s.cover << " shown=" << s.shown_cover << " stage=" << s.reason
			    << " input_gate=" << cover_debug::input_gate(s) << " controller=" << s.decision
			    << " track=" << s.tracking << " available=" << s.available << " trigger=" << s.trigger
			    << " squeeze=" << s.squeeze_active << ':' << s.squeeze << " palm_valid=" << s.raw.valid
			    << " raw_m=" << s.raw.point[0] << ',' << s.raw.point[1] << ',' << s.raw.point[2]
			    << " palm_normal=" << s.raw.palm[0] << ',' << s.raw.palm[1] << ',' << s.raw.palm[2]
			    << " palm_long_axis=" << s.raw.along[0] << ',' << s.raw.along[1] << ',' << s.raw.along[2]
			    << " visual_m=" << s.visual[0] << ',' << s.visual[1] << ',' << s.visual[2];
			if (s.definition && s.definition->interaction.belt)
			{
				const auto g = belt_feed::query_push(*s.definition->interaction.belt, s.cover, s.raw);
				out << " skin_cm=" << g.point[0] * 100 << ',' << g.point[1] * 100 << ',' << g.point[2] * 100
				    << " gap_cm=" << g.gap * 100 << " inside=" << g.inside << " facing=" << g.facing
				    << " palm_dot=" << g.normal[2];
			}
			out << '\n';
		}
		void observation_text(std::ostream& out, const char* label, const slap_observation& s)
		{
			const auto& i = s.impact;
			out << label << " seq=" << s.sequence << " age_ms=" << age(s.at) << " reason=" << name(s.reason)
			    << " eligible=" << s.eligible << " examined=" << s.examined;
			if (!s.examined)
			{
				out << '\n';
				return;
			}
			out << " point=" << i.point << " dt_ms=" << i.dt * 1000 << " distance_cm=" << i.distance * 100
			    << " separation_cm=" << i.separation * 100 << " contact_speed=" << i.speed
			    << " wrist_speed=" << s.world_speed
			    << " angle_deg=" << std::acos(std::clamp(i.direction_cosine, -1.f, 1.f)) * 57.2957795f
			    << " travel_cm=" << i.travel * 100 << " max_step_cm=" << i.max_step * 100
			    << " allowed_step_cm=" << i.step_limit * 100 << " jump_point=" << i.jump_point << '\n';
			out << "gates(continuous/separated/armed_before/armed_after/speed/direction/travel/above/radius/world_speed)="
			    << i.continuous << '/' << i.separated << '/' << i.armed_before << '/' << i.armed_after << '/'
			    << i.speed_ok << '/' << i.direction_ok << '/' << i.travel_ok << '/' << i.above << '/'
			    << i.radius_ok << '/' << s.world_speed_ok << " entered=" << i.entered
			    << " geometric_hit=" << i.hit << '\n';
			out << "segment_cm=(" << i.previous[0] * 100 << ',' << i.previous[1] * 100 << ','
			    << i.previous[2] * 100 << ")->(" << i.current[0] * 100 << ',' << i.current[1] * 100 << ','
			    << i.current[2] * 100 << ") start_cm=(" << i.start[0] * 100 << ',' << i.start[1] * 100 << ','
			    << i.start[2] * 100 << ")\n";
		}
		std::string slap_status(const hk_slap_debug::sample& s)
		{
			std::ostringstream out;
			out << "hk_slap_debug=" << slap.enabled() << " pose_age_ms=" << age(s.at)
			    << " draws=" << slap.draws.load() << " misses=" << slap.misses.load()
			    << " failures=" << slap.failures.load() << '\n';
			if (!s.definition || !s.definition->interaction.manual_catch)
				return out.str();
			const auto& p = *s.definition->interaction.manual_catch;
			out << "profile=" << s.definition->id << " weapon=" << s.owner.weapon
			    << " instance=" << s.instance << " reference=" << s.reference
			    << " pose_input=" << s.input_sequence << " radius_cm=" << p.slap.radius * 100
			    << " rearm_cm=" << p.slap.rearm_radius * 100 << " min_speed=" << p.slap.min_speed
			    << " max_wrist_speed=" << p.max_slap_speed << " min_travel_cm=" << p.slap.min_travel * 100
			    << " max_angle_deg=" << std::acos(p.slap.direction_cosine) * 57.2957795f
			    << " base_max_step_cm=" << s.definition->interaction.max_contact_step * 100
			    << " travel_origin=" << (p.slap.follow_approach_peak ? "approach_peak" : "rearm_position")
			    << " contacts=" << s.trace.contacts << " accepted=" << s.trace.accepted << '\n';
			observation_text(out, "NOW", s.trace.latest);
			observation_text(out, "LAST_CONTACT", s.trace.last_contact);
			out << "Contact indices: 0..14 thumb/index/middle/ring/pinky joints (3 each); 15..19 tips; 20 palm.\n"
			    << "NOW and LAST are simulation samples, not verdicts on the current white glove. LAST persists until another contact.\n";
			return out.str();
		}
	}
	class component final : public component_interface
	{
		eye_composition::consumer_registration composition_registration;
		void post_unpack() override
		{
			// Startup selection is identical in optimized and Debug builds. Allocate
			// only selected histories; no live-toggle polling belongs in gameplay.
			well.initialize(debug_options::probe::reload_well);
			slap.initialize(debug_options::probe::hk_slap);
			bolt.initialize(debug_options::probe::bolt);
			cover.initialize(debug_options::probe::cover);
			command::add("vr_coverPush_status",
			             []
			             {
				             std::ostringstream out;
				             out << "cover_debug=" << cover.enabled() << " draws=" << cover.draws.load()
				                 << " misses=" << cover.misses.load() << " failures=" << cover.failures.load()
				                 << '\n';
				             cover_text(out, cover.newest());
				             console::info("%s", out.str().c_str());
			             });
			command::add(
			    "vr_coverPush_dump",
			    []
			    {
				    // The extended capture belongs on the heap, never a multi-MB game
				    // command stack. Formatting and disk I/O run outside the short lock.
				    auto history = std::make_unique<sample_history<cover_debug::sample, 4096>>();
				    {
					    const std::lock_guard lock(cover.mutex);
					    if (cover.history)
						    *history = *cover.history;
				    }
				    std::ostringstream out;
				    out << "cover_debug=" << cover.enabled() << " draws=" << cover.draws.load()
				        << " misses=" << cover.misses.load() << " failures=" << cover.failures.load() << '\n';
				    std::uint64_t previous{};
				    for (size_t n = history->cursor > history->samples.size()
				                        ? history->cursor - history->samples.size()
				                        : 0;
				         n < history->cursor;
				         ++n)
				    {
					    const auto& s = history->samples[n % history->samples.size()];
					    if (s.input_sequence == previous)
						    continue;
					    previous = s.input_sequence;
					    cover_text(out, s);
				    }
				    const bool ok = utils::io::write_file("h2-mod-vr-cover-push.txt", out.str());
				    console::info("cover push dump %s: h2-mod-vr-cover-push.txt\n", ok ? "saved" : "failed");
			    });
			command::add(
			    "vr_reloadWell_status",
			    []
			    {
				    const auto s = well.newest();
				    console::info("well_debug=%d age_ms=%lld draws=%llu misses=%llu failures=%llu\n",
				                  well.enabled(),
				                  age(s.at),
				                  well.draws.load(),
				                  well.misses.load(),
				                  well.failures.load());
				    if (!s.definition)
					    return;
				    const auto& p = s.definition->interaction;
				    console::info(
				        "profile=%.*s radius=%.1fcm z=[%.1f,%.1f]cm retention_margin=%.1fcm withdrawal_margin=%.1fcm occupied=%d held=%d\n",
				        static_cast<int>(std::min(s.definition->id.size(), size_t{128})),
				        s.definition->id.data(),
				        p.well_radius * 100,
				        -p.well_capture_below * 100,
				        p.well_contact_depth * 100,
				        p.well_release_margin * 100,
				        p.well_withdraw_margin * 100,
				        s.occupied,
				        s.held);
				    console::info("current_tip_cm=(%.2f,%.2f,%.2f) alignment=%.3f min=%.3f input=%llu\n",
				                  s.raw_tip[0] * 100,
				                  s.raw_tip[1] * 100,
				                  s.raw_tip[2] * 100,
				                  s.alignment,
				                  p.insertion_cosine,
				                  s.input_sequence);
				    console::info(
				        "last_simulation: contact=%d withdrawal=%d decision=%s (not current-tip verdict)\n",
				        s.well_contact,
				        s.requires_withdrawal,
				        s.decision ? s.decision : "unavailable");
			    });
			command::add("vr_hkSlap_status", [] { console::info("%s", slap_status(slap.newest()).c_str()); });
			command::add(
			    "vr_hkSlap_dump",
			    []
			    {
				    // Copy under the short lock; format/write only on the command thread.
				    sample_history<hk_slap_debug::sample> history;
				    {
					    const std::lock_guard lock(slap.mutex);
					    if (slap.history)
						    history = *slap.history;
				    }
				    std::ostringstream out;
				    out << slap_status(history.newest());
				    std::uint64_t previous{};
				    for (size_t n = history.cursor > history.samples.size()
				                        ? history.cursor - history.samples.size()
				                        : 0;
				         n < history.cursor;
				         ++n)
				    {
					    const auto& s = history.samples[n % history.samples.size()];
					    if (!s.trace.latest.sequence || s.trace.latest.sequence == previous)
						    continue;
					    previous = s.trace.latest.sequence;
					    out << "FRAME profile=" << (s.definition ? s.definition->id : std::string_view{})
					        << " weapon=" << s.owner.weapon << " instance=" << s.instance
					        << " reference=" << s.reference << " pose_input=" << s.input_sequence << '\n';
					    observation_text(out, "HISTORY", s.trace.latest);
				    }
				    const bool ok = utils::io::write_file("h2-mod-vr-hk-slap.txt", out.str());
				    console::info("hk_slap dump %s: h2-mod-vr-hk-slap.txt\n", ok ? "saved" : "failed");
			    });
			if (well.enabled() || slap.enabled() || bolt.enabled() || cover.enabled())
				composition_registration =
				    eye_composition::register_consumer(present, eye_composition::layer::diagnostics);
		}
		void pre_destroy() override
		{
			alive = false;
			well.requested = false;
			slap.requested = false;
			bolt.requested = false;
			cover.requested = false;
			composition_registration.reset();
		}
	};
}
namespace vr::gameplay::weapons::physical_reload::well_debug
{
	bool enabled() noexcept
	{
		return debug::well.enabled();
	}
	void publish(const sample& s) noexcept
	{
		debug::well.publish(s);
	}
}
namespace vr::gameplay::weapons::physical_reload::hk_slap_debug
{
	bool enabled() noexcept
	{
		return debug::slap.enabled();
	}
	void publish(const sample& s) noexcept
	{
		debug::slap.publish(s);
	}
}
REGISTER_COMPONENT(vr::gameplay::weapons::physical_reload::debug::component)
namespace vr::gameplay::weapons::physical_reload::bolt_debug
{
	bool enabled() noexcept
	{
		return debug::bolt.enabled();
	}
	void publish(const sample& s) noexcept
	{
		debug::bolt.publish(s);
	}
}
namespace vr::gameplay::weapons::physical_reload::cover_debug
{
	bool enabled() noexcept
	{
		return debug::cover.enabled();
	}
	void publish(const sample& s) noexcept
	{
		debug::cover.publish(s);
	}
}
