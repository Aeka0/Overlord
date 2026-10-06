#include <std_include.hpp>
#include "weapons/m9/profile.hpp"
#include "vehicles/hud.hpp"
#include "vehicles/runtime.hpp"
#include "weapon_hud.hpp"
#include "weapon_hud_policy.hpp"
#include "weapon_hud_lifetime.hpp"
#include "weapon_hud_source.hpp"
#include "weapon_hud_smoothing.hpp"
#include "weapon_hud_warning.hpp"
#include "weapon_render_pose.hpp"
#include "weapon_interaction.hpp"
#include "weapon_carry_runtime.hpp"
#include "weapons/m9/hud.hpp"
#include "../eye_composition.hpp"
#include "../native_hud_capture.hpp"
#include "../spatial_panel_renderer.hpp"
#include "../overlay_text_texture.hpp"
#include "component/game_text.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>

// Functional weapon HUD; compiler optimization must not select feature availability.
namespace vr::gameplay::weapon_hud
{
	namespace
	{
		toggle_policy toggle;
		game::dvar_t* blur{};
		game::dvar_t* smoothing{};
		std::atomic_bool alive{true};
		bool installed{};
		std::atomic_uint64_t draws{}, skips{}, failures{};
		std::atomic_uint64_t model_origin_pairs{}, model_origin_misses{};
		std::atomic<const char*> reason{"waiting for input"};
		std::atomic<const char*> pose_reason{"not sampled"}, texture_reason{"not sampled"};
		std::atomic_bool positioned{}, texture_retained{};
		struct input_state
		{
			weapons::hold owner{};
			std::uint64_t reference{};
			bool visible{true};
			float blur_pixels{2.f};
			bool smoothing_enabled{true};
		};
		std::mutex state_mutex;
		input_state state;
		struct pair_snapshot
		{
			std::uint64_t id{};
			std::shared_ptr<const native_hud_capture::frame> ink;
			std::array<spatial_panel::projected_quad, 2> corners{};
			std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2> warnings;
			std::array<std::array<spatial_panel::projected_quad, 2>, 2> warning_corners{};
			float blur_pixels{};
			bool left_drawn{};
		};
		thread_local std::array<pair_snapshot, source_count> pairs;
		thread_local std::uint64_t visibility_pair{};
		thread_local bool pair_visible{};
		thread_local bool pair_paused{};
		thread_local spatial_panel::renderer renderer;
		thread_local std::array<overlay_text_texture, static_cast<size_t>(status_line::count)> status_texts;
		struct retained_presentation
		{
			std::shared_ptr<const native_hud_capture::frame> ink;
			spatial_panel::vec3 anchor{};
			profile layout{};
			float units{}, side{1};
			bool positioned{};
			std::uint32_t definition{};
			chamber_warning warning;
		};
		thread_local std::array<presentation_cache<retained_presentation>, source_count> presentations;
		thread_local std::array<anchor_smoother, source_count> smoothers;

		bool prepare(const eye_composition::event& event,
		             ID3D11DeviceContext* context,
		             unsigned index,
		             const weapons::hold& owner,
		             std::uint32_t definition,
		             const warning_messages& messages,
		             bool primary_warning,
		             input_state input)
		{
			const auto hand = source_hand(index);
			const auto channel = source_feed(index);
			auto& pair = pairs[index];
			auto& presentation = presentations[index];
			auto& smoother = smoothers[index];
			pair = {};
			pair.id = event.pair_id;
			// Read the published authority once per stereo pair, including when
			// command input is suspended. Never recover ownership from an old scene.
			input.owner = owner;
			if (presentation.synchronize(input.owner))
			{
				smoother.reset();
				positioned = false;
				texture_retained = false;
			}
			auto& retained = presentation.value;
			if (retained.definition != definition)
			{
				retained = {};
				retained.definition = definition;
				smoother.reset();
			}
			if (!has_presentation_owner(input.owner))
			{
				pose_reason = texture_reason = "no held weapon";
				reason = "weapon ownership ended; presentation released";
				return false;
			}
			if (!alive.load() || !input.visible)
			{
				smoother.reset();
				reason = "closed by primary button";
				return false;
			}
			if (!event.model_origins.valid)
			{
				++model_origin_misses;
				smoother.reset();
				reason = "current scene model origins missing (no history/latest substitution)";
				return false;
			}
			// Use only the bone pose bound to this native scene submission.
			weapon_render_pose::snapshot rendered;
			const bool render_bound =
			    weapon_render_pose::for_scene(event.views, rendered, input.owner.id()) &&
			    presentation.accepts(rendered.muzzle.owner) &&
			    rendered.muzzle.reference_generation == input.reference;
			const auto& muzzle = rendered.muzzle;
			if (render_bound && !muzzle.firing_capable)
			{
				retained = {};
				smoother.reset();
				reason = "held weapon has no ammunition HUD";
				return false;
			}
			pose_reason = render_bound
			                  ? "native skin/submission pose accepted"
			                  : "native scene pose missing (retained only; no latest/freeze substitution)";
			if (render_bound)
			{
				retained.layout = for_feed(muzzle.profile_id == "m9" ? weapons::m9::hud : generic, channel);
				retained.units = muzzle.units_per_meter;
				retained.side = hand == 0 ? -1.f : 1.f;
				const auto placed =
				    weapons::place_model_anchor(rendered.control_grip, event.model_origins.placement);
				for (int c = 0; c < 3; ++c)
				{
					const auto& offset = retained.layout.from_grip_meters;
					retained.anchor[c] =
					    placed[c] + retained.units * (muzzle.axis[0][c] * offset[0] +
					                                  muzzle.axis[1][c] * offset[1] * retained.side +
					                                  muzzle.axis[2][c] * offset[2]);
				}
				retained.positioned = true;
			}
			auto ink = native_hud_capture::latest(input.owner.id(), channel);
			const char* texture_problem = nullptr;
			if (!ink || !ink->view || !ink->width || !ink->height)
				texture_problem = "no complete native capture";
			else if (ink->generation != event.device_generation)
				texture_problem = "capture device mismatch";
			else if (ink->context != reinterpret_cast<std::uintptr_t>(context))
				texture_problem = "capture context mismatch";
			else if (ink->id() != input.owner.id() || ink->rear_revision != input.owner.rear_revision)
				texture_problem = "capture weapon/hand mismatch";
			else if (ink->reference_generation != input.reference)
				texture_problem = "capture reference mismatch";
			else if (ink->definition != definition || ink->channel != channel)
				texture_problem = "capture ammunition feed mismatch";
			texture_reason = texture_problem ? texture_problem : "current capture accepted";
			if (!texture_problem)
				retained.ink = std::move(ink);
			// Retain native pixels during tracking gaps only for this holding
			// lease. Ending/changing ownership cleared both ink and anchor above;
			// the user's button-latched visibility preference is unaffected.
			ink = retained.ink;
			positioned = retained.positioned;
			texture_retained = ink && ink->generation == event.device_generation &&
			                   ink->context == reinterpret_cast<std::uintptr_t>(context);
			if (!retained.positioned)
			{
				reason = "awaiting initial weapon position";
				return false;
			}
			if (!ink || ink->generation != event.device_generation ||
			    ink->context != reinterpret_cast<std::uintptr_t>(context))
			{
				reason = "awaiting compatible native texture";
				return false;
			}
			const auto& config = retained.layout;
			auto anchor = retained.anchor;
			if (render_bound)
			{
				spatial_panel::vec3 model_center{};
				for (unsigned c = 0; c < 3; ++c)
					model_center[c] = (event.model_origins.eyes[0][c] + event.model_origins.eyes[1][c]) * .5f;
				anchor = smoother.apply(
				    anchor,
				    model_center,
				    event.views.natural_camera,
				    retained.units,
				    std::chrono::duration<double>(rendered.committed_at.time_since_epoch()).count(),
				    {input.owner.weapon,
				     input.owner.rear_revision,
				     input.reference,
				     input.owner.instance_generation},
				    input.smoothing_enabled);
			}
			else
				smoother.reset();
			float warning_offset{};
			if (channel == feed::underbarrel && primary_warning)
			{
				const auto& primary = presentations[source_index(hand, feed::primary)].value;
				if (primary.ink && primary.ink->width)
					warning_offset = warning_clearance(
					    primary.layout.width_meters * primary.ink->height / primary.ink->width,
					    config.width_meters * ink->height / ink->width,
					    primary.layout.screen_up_meters - config.screen_up_meters);
			}
			spatial_panel::vec3 right{}, up{}, center{};
			for (int c = 0; c < 3; ++c)
			{
				right[c] = -event.views.natural_camera[6 + c];
				up[c] = event.views.natural_camera[9 + c];
				center[c] = anchor[c] + retained.units * (right[c] * screen_side(config, retained.side < 0) +
				                                          up[c] * (config.screen_up_meters - warning_offset));
			}
			spatial_panel::quad world{};
			const float width = config.width_meters * retained.units;
			if (!spatial_panel::billboard(center, right, up, width, width * ink->height / ink->width, world))
			{
				reason = "invalid billboard basis";
				return false;
			}
			std::array<spatial_panel::quad, 2> warning_world{};
			std::array<bool, 2> warning_visible{
			    status_visible(retained.warning, messages[0], GetTickCount64()),
			    messages[1] != status_line::none};
			std::array<float, 2> text_width{};
			float total_width{};
			const float height = warning_height_meters * retained.units, gap = .006f * retained.units;
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			context->GetDevice(&device);
			for (size_t n = 0; n < messages.size(); ++n)
			{
				const auto line = messages[n];
				if (line == status_line::none)
					continue;
				const auto key = line == status_line::quick_reload     ? game_text::key::weapon_quick_loading
				                 : line == status_line::magazine_empty ? game_text::key::weapon_magazine_empty
				                 : line == status_line::no_ammo        ? game_text::key::weapon_no_ammo
				                                                       : game_text::key::weapon_needs_chamber;
				auto& text = status_texts[static_cast<size_t>(line)];
				if (!text.ensure_utf8(device.Get(),
				                      game_text::text(key, game_text::current()),
				                      48,
				                      true,
				                      {},
				                      status_color(line)))
				{
					warning_visible[n] = false;
					continue;
				}
				text_width[n] = height * text.width() / text.height();
				total_width += text_width[n];
			}
			if (text_width[0] && text_width[1])
				total_width += gap;
			if (total_width > 0)
			{
				const float capture_padding = width * 4.f / ink->width;
				const float right_inset = capture_padding + (width - 2 * capture_padding) * (5.33f / 345.f);
				const float scale = (std::min)(1.f, (width - right_inset) / total_width);
				float edge = width * .5f - right_inset;
				// Right-align the reserve warning independently. Blinking the chamber
				// message must neither blink nor reposition "No ammo".
				for (int n = 1; n >= 0; --n)
				{
					if (!text_width[n])
						continue;
					auto& text = status_texts[static_cast<size_t>(messages[n])];
					const float padding = height * overlay_text_texture::padding_pixels / text.height();
					const float across =
					    (edge - text_width[n] * scale * .5f + padding * scale) * retained.side;
					auto warning_center = center;
					const float down = width * ink->height / ink->width * .5f +
					                   warning_gap_meters * retained.units + height * scale * .5f;
					for (int c = 0; c < 3; ++c)
						warning_center[c] += right[c] * across - up[c] * down;
					warning_visible[n] = spatial_panel::billboard(warning_center,
					                                              right,
					                                              up,
					                                              text_width[n] * scale,
					                                              height * scale,
					                                              warning_world[n]) &&
					                     warning_visible[n];
					edge -= (text_width[n] + gap) * scale;
				}
			}
			for (int eye = 0; eye < 2; ++eye)
			{
				spatial_panel::matrix vp{};
				const auto& slot = event.views.eyes[eye];
				if (slot.pair_id != event.pair_id || slot.output_eye != static_cast<unsigned>(eye))
					return false;
				std::memcpy(vp.data(),
				            slot.bytes.data() + engine_stereo_view::h2_current_view_projection_offset,
				            sizeof(vp));
				const auto& model_origin = event.model_origins.eyes[eye];
				// CURRENT WORLD_MATRIX0: solved model point + current placement
				// - current eye, then current VP. Never mix the previous-frame
				// placement/rebase origin into this presentation chain.
				if (!spatial_panel::project(
				        world, model_origin, vp, retained.units * .12f, pair.corners[eye]))
				{
					reason = "panel too near or behind camera";
					return false;
				}
				for (size_t n = 0; n < messages.size(); ++n)
					if (warning_visible[n] && !spatial_panel::project(warning_world[n],
					                                                  model_origin,
					                                                  vp,
					                                                  retained.units * .12f,
					                                                  pair.warning_corners[n][eye]))
						warning_visible[n] = false;
			}
			for (size_t n = 0; n < messages.size(); ++n)
				if (warning_visible[n])
					pair.warnings[n] = status_texts[static_cast<size_t>(messages[n])].view();
			++model_origin_pairs;
			pair.ink = std::move(ink);
			pair.blur_pixels = input.blur_pixels;
			return true;
		}
		void present(const eye_composition::event& event,
		             ID3D11DeviceContext* context,
		             ID3D11ShaderResourceView* background,
		             ID3D11RenderTargetView* destination) noexcept
		{
			try
			{
				if (event.eye > 1)
					return;
				input_state input;
				if (event.eye == 0)
				{
					const auto* paused = game::Dvar_FindVar("cl_paused");
					{
						const std::lock_guard lock(state_mutex);
						input = state;
					}
					pair_paused = paused && paused->current.integer != 0;
					visibility_pair = event.pair_id;
					pair_visible = alive.load() && input.visible && !pair_paused;
				}
				// One preference snapshot covers every ammunition channel and both
				// eyes, including the vehicle ammunition caption routed here.
				// Pausing retires Lua sources before menu allocation; retained native
				// pixels must not keep drawing after that lease has been yielded.
				if (visibility_pair != event.pair_id || !pair_visible)
				{
					pairs = {};
					reason = pair_paused ? "hidden during native pause" : "closed by primary button";
					return;
				}
				vehicles::present_hud(event, context, background, destination);
				if (vehicles::active())
				{
					pairs = {};
					return;
				}
				std::array<weapons::hold, 2> owners{};
				std::array<source_snapshot, source_count> sources{};
				std::uint64_t reference{};
				if (event.eye == 0)
				{
					sources = source_owners();
					reference = controller_input::latest().reference_generation;
					if (weapons::carry::active())
					{
						const auto held = weapons::carry::held_instances();
						for (unsigned h = 0; h < 2; ++h)
							owners[h] = held[h].owner;
					}
					else
					{
						const auto owner = weapons::current_hold();
						if (vr::valid_hand(owner.holding_hand()))
							owners[unsigned(owner.holding_hand())] = owner;
					}
				}
				for (unsigned index = 0; index < source_count; ++index)
				{
					if (event.eye == 0)
					{
						auto owner = owners[source_hand(index)];
						auto definition = owner.weapon;
						if (source_feed(index) == feed::underbarrel)
						{
							const auto& source = sources[index];
							if (!same_presentation_owner(source.owner, owner) ||
							    source.channel != feed::underbarrel || !source.definition)
								owner = {};
							definition = owner.weapon ? source.definition : 0;
						}
						const auto warning_status = [&](unsigned slot)
						{
							const auto& source = sources[slot];
							const bool matched =
							    same_presentation_owner(source.owner, owner) &&
							    source.reference == reference && source.channel == source_feed(slot) &&
							    source.definition ==
							        (source_feed(slot) == feed::primary ? owner.weapon : definition);
							return matched ? select_messages(source.needs_chamber,
							                                 source.quick_loading,
							                                 source.loaded,
							                                 source.reserve)
							               : warning_messages{};
						};
						const auto messages = warning_status(index);
						const bool primary_warning =
						    has_message(warning_status(source_index(source_hand(index), feed::primary)));
						if (!prepare(
						        event, context, index, owner, definition, messages, primary_warning, input))
						{
							++skips;
							continue;
						}
					}
					auto& pair = pairs[index];
					if (pair.id != event.pair_id || !pair.ink || (event.eye == 1 && !pair.left_drawn))
						continue;
					const bool drawn = renderer.draw(context,
					                                 background,
					                                 pair.ink->view.Get(),
					                                 destination,
					                                 pair.corners[event.eye],
					                                 event.width,
					                                 event.height,
					                                 pair.ink->blur_mask ? pair.blur_pixels : 0.f,
					                                 pair.ink->blur_alpha,
					                                 pair.ink->blur_mask.Get(),
					                                 pair.ink->blur_window);
					if (drawn)
					{
						++draws;
						reason = "per-weapon billboard drawn";
					}
					else
					{
						++failures;
						reason = "panel GPU composition rejected";
					}
					if (drawn)
						for (size_t n = 0; n < pair.warnings.size(); ++n)
							if (pair.warnings[n] && !renderer.draw(context,
							                                       background,
							                                       pair.warnings[n].Get(),
							                                       destination,
							                                       pair.warning_corners[n][event.eye],
							                                       event.width,
							                                       event.height,
							                                       0.f,
							                                       0.f))
							{
								++failures;
								pair.warnings[n].Reset();
							}
					if (event.eye == 0)
						pair.left_drawn = drawn;
					else
						pair = {};
				}
			}
			catch (...)
			{
				pairs = {};
				++failures;
				reason = "panel exception contained";
			}
		}
		std::string status()
		{
			input_state input;
			{
				const std::lock_guard lock(state_mutex);
				input = state;
			}
			const auto capture = native_hud_capture::get_counters();
			std::string sources;
			for (const auto& source : source_owners())
			{
				const auto ink = native_hud_capture::latest(source.owner.id(), source.channel);
				sources += std::format(
				    "hud_source hand={} weapon={} revision={} feed={} definition={} name={} clip={}/{} reserve={} capture_weapon={} capture_revision={} capture_sequence={} native_blur_mask={} native_blur_alpha={}\n",
				    int(source.owner.holding_hand()),
				    source.owner.weapon,
				    source.owner.rear_revision,
				    unsigned(source.channel),
				    source.definition,
				    source.name,
				    source.loaded,
				    source.capacity,
				    source.reserve,
				    ink ? ink->weapon : 0,
				    ink ? ink->rear_revision : 0,
				    ink ? ink->sequence : 0,
				    ink && ink->blur_mask,
				    ink ? ink->blur_alpha : 0);
			}
			return sources + weapon_render_pose::status() +
			       std::format(
			           "stage=weapon_billboard_debug; blur=native_mask_and_alpha_per_eye\n"
			           "pid={} uptime_ms={} visible={} eye_draws={} skipped_pairs={} failures={}\nreason={}\n"
			           "positioned={} pose={}\ntexture_retained={} texture={}\n"
			           "projection_origin=native_current_eye model_origin_pairs={} model_origin_misses={} smoothing={} smoothing_half_life_ms=25\n"
			           "anchor_source=solved_control_grip_plus_current_scene_placement; left_layout=mirrored; motion_audit=disabled; automatic_report=disabled\n"
			           "capture_dispatches={} captures={} copied_draws={} rejected={} selection_count={} selection_avg_us={} selection_last_us={}\n",
			           GetCurrentProcessId(),
			           GetTickCount64(),
			           input.visible,
			           draws.load(),
			           skips.load(),
			           failures.load(),
			           reason.load(),
			           positioned.load(),
			           pose_reason.load(),
			           texture_retained.load(),
			           texture_reason.load(),
			           model_origin_pairs.load(),
			           model_origin_misses.load(),
			           input.smoothing_enabled,
			           capture.dispatches,
			           capture.captures,
			           capture.draws,
			           capture.rejected,
			           capture.selections,
			           capture.selections ? capture.selection_total_us / capture.selections : 0,
			           capture.selection_last_us);
		}
	}
	void suspend() noexcept
	{
		// This is input rearming only, not a visibility transition.
		toggle.reset_input();
	}
	void update(const controller_input::frame& input,
	            const weapons::hold& owner,
	            bool gameplay,
	            controller_input::clock::time_point now) noexcept
	{
		const bool active = installed && alive.load() && gameplay;
		std::array<weapons::hold, 2> owners{};
		if (weapons::carry::active())
		{
			const auto held = weapons::carry::held_instances();
			for (unsigned h = 0; h < 2; ++h)
				owners[h] = held[h].owner;
		}
		else if (vr::valid_hand(owner.holding_hand()))
			owners[unsigned(owner.holding_hand())] = owner;
		const auto toggle_owner = vehicles::active()     ? vehicles::latest().owner
		                          : owners[1].can_fire() ? owners[1]
		                                                 : owners[0];
		(void)toggle.consume(input, toggle_owner, active, now);
		const bool visible = toggle.visible();
		{
			const std::lock_guard lock(state_mutex);
			state.owner = owner;
			state.reference = input.reference_generation;
			state.visible = visible;
			state.blur_pixels = blur ? blur->current.value : 0;
			state.smoothing_enabled = !smoothing || smoothing->current.enabled;
		}
		native_hud_capture::set_requested(
		    installed && alive.load() && visible &&
		    (has_presentation_owner(owners[0]) || has_presentation_owner(owners[1])));
	}
	class component final : public component_interface
	{
		eye_composition::consumer_registration composition_registration;

	  public:
		void post_unpack() override
		{
			blur = dvars::register_float(
			    "vr_weaponHudBlur",
			    2.f,
			    0.f,
			    8.f,
			    0,
			    "Native weapon HUD mask blur radius per eye in render pixels; zero disables blur");
			smoothing = dvars::register_bool(
			    "vr_weaponHudSmoothing",
			    true,
			    0,
			    "Smooth weapon HUD in model-view space (25 ms half-life); zero tests raw scene alignment");
			command::add(
			    "vr_weaponHud_status",
			    []
			    {
				    const auto text = status();
				    console::info("%s", text.c_str());
				    scheduler::once(
				        [text] { utils::io::write_file_atomic("minidumps/overlord-weapon-hud.txt", text); },
				        scheduler::pipeline::async);
			    });
			// Capability, not a visibility control. Native LUI source animation is
			// adapted by ui_scripts/vr_gameplay; only toggle_policy controls output.
			dvars::register_bool("vr_weaponHudNativeSource",
			                     true,
			                     game::DVAR_FLAG_READ,
			                     "Weapon HUD source adapter capability (read only)");
			installed = true;
			native_hud_capture::set_requested(true);
			composition_registration =
			    eye_composition::register_consumer(present, eye_composition::layer::spatial_hud);
		}
		void pre_destroy() override
		{
			alive = false;
			native_hud_capture::set_requested(false);
			composition_registration.reset();
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapon_hud::component)
