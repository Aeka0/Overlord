#include <std_include.hpp>
#include "narrative_ui.hpp"
#include "native_hud_capture.hpp"
#include "eye_composition.hpp"
#include "spatial_panel_renderer.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "gameplay/campaign/scripted_sequences.hpp"
#include "component/game_text.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

namespace vr::narrative_ui
{
	namespace
	{
		std::atomic_bool alive{true};
		std::atomic_uint64_t pairs{}, failures{};
		std::atomic<const char*> reason{"waiting for native narrative UI"};
		std::atomic<std::shared_ptr<const std::vector<std::string>>> labels;
		void update_labels()
		{
			if (!game::CL_IsCgameInitialized() || !gameplay::sequences::latest().dsm_progress)
			{
				labels.store({});
				return;
			}
			// These are native progress-plane labels, not action replacements.
			// Rebuild their copied classification data when the game language changes.
			static auto previous_language = game_text::locale::count;
			const auto language = game_text::current();
			if (labels.load() && previous_language == language)
				return;
			previous_language = language;
			labels.store({});
			constexpr std::array keys{"ESTATE_DSM_FRAME",
			                          "ESTATE_DSM_WORKING",
			                          "ESTATE_DSM_NETWORK_FOUND",
			                          "ESTATE_DSM_IRONBOX",
			                          "ESTATE_DSM_BYPASS",
			                          "ESTATE_DSM_PROGRESS",
			                          "ESTATE_DSM_SLASH_TOTALFILES",
			                          "ESTATE_DSM_DLTIMELEFT",
			                          "ESTATE_DSM_DLTIMELEFT_MINS",
			                          "ESTATE_DSM_DLTIMELEFT_SECS",
			                          "ESTATE_DSM_DLRATE"};
			auto next = std::make_shared<std::vector<std::string>>();
			for (const auto* key : keys)
				if (const auto* value = game::UI_SafeTranslateString(key);
				    value && std::string_view(value) != key)
				{
					const std::string_view text(value);
					if (!text.empty() && text.size() <= 512)
						next->push_back(plain_label(text));
				}
			if (!next->empty())
				labels.store(next);
		}
		struct canvas_layer
		{
			std::shared_ptr<const native_hud_capture::frame> ink;
			std::array<spatial_panel::projected_quad, 2> canvas{};
			std::array<spatial_panel::blur_region, spatial_panel::blur_region_capacity> backdrops{};
			unsigned backdrop_count{};
		};
		struct pair_snapshot
		{
			std::uint64_t id{};
			std::array<canvas_layer, static_cast<unsigned>(channel::count)> layers;
			bool left_drawn{};
		};
		thread_local pair_snapshot pair;
		thread_local spatial_panel::renderer renderer;

		bool prepare_layer(const eye_composition::event& event,
		                   ID3D11DeviceContext* context,
		                   channel which,
		                   std::shared_ptr<const native_hud_capture::frame> ink,
		                   canvas_layer& target)
		{
			if (!ink || ink->hud_hidden != !presentation_options::show_hud() || !ink->view || !ink->width ||
			    !ink->height || !current(ink->timestamp, GetTickCount64()))
			{
				reason = "no current native subtitle/title/fade";
				return false;
			}
			if (!event.model_origins.valid || ink->generation != event.device_generation ||
			    ink->context != reinterpret_cast<std::uintptr_t>(context))
			{
				reason = "incompatible scene or device";
				return false;
			}
			if (ink->backdrop_count > target.backdrops.size())
				return false;
			if (ink->subtractive &&
			    (!ink->subtractive->view || ink->subtractive->generation != ink->generation ||
			     ink->subtractive->sequence != ink->sequence || ink->subtractive->context != ink->context ||
			     ink->subtractive->width != ink->width || ink->subtractive->height != ink->height))
				return false;
			for (unsigned i = 0; i < ink->backdrop_count; ++i)
			{
				if (!valid_backdrop(ink->backdrops[i]))
					return false;
				target.backdrops[target.backdrop_count++] =
				    normalize_backdrop(ink->backdrops[i], ink->width, ink->height);
			}
			const auto units = engine_stereo_bridge::get_status().world_scale;
			if (!std::isfinite(units) || units <= 0)
				return false;
			spatial_panel::vec3 center{}, right{}, up{};
			const auto& camera = event.views.natural_camera;
			const auto placement = layout(which);
			const float distance_meters = placement.distance_meters;
			for (unsigned c = 0; c < 3; ++c)
			{
				right[c] = -camera[6 + c];
				up[c] = camera[9 + c];
				center[c] = camera[c] + camera[3 + c] * distance_meters * units;
			}
			// A 60-degree head-relative native canvas at the channel's depth. Lower
			// left announcements and bottom-centered subtitles retain line wrapping,
			// localization, shadows, glow and typewriter timing without rebuilding.
			const float width = 2 * distance_meters * units * .57735026919f;
			for (unsigned c = 0; c < 3; ++c)
				center[c] -= up[c] * placement.lower_fraction * width * ink->height / ink->width;
			spatial_panel::quad world{};
			if (!spatial_panel::billboard(center, right, up, width, width * ink->height / ink->width, world))
				return false;
			for (unsigned eye = 0; eye < 2; ++eye)
			{
				const auto& slot = event.views.eyes[eye];
				if (slot.pair_id != event.pair_id || slot.output_eye != eye)
					return false;
				spatial_panel::matrix vp{};
				std::memcpy(vp.data(),
				            slot.bytes.data() + engine_stereo_view::h2_current_view_projection_offset,
				            sizeof(vp));
				if (!spatial_panel::project(
				        world, event.model_origins.eyes[eye], vp, units * .12f, target.canvas[eye]))
					return false;
			}
			target.ink = std::move(ink);
			return true;
		}
		void present(const eye_composition::event& event,
		             ID3D11DeviceContext* context,
		             ID3D11ShaderResourceView*,
		             ID3D11RenderTargetView* destination) noexcept
		{
			if (!alive.load() || event.eye > 1)
				return;
			try
			{
				if (event.eye == 0)
				{
					pair = {};
					pair.id = event.pair_id;
					const auto captures = native_hud_capture::latest_narrative_layers();
					constexpr std::array order{channel::progress, channel::story, channel::announcement};
					for (unsigned i = 0; i < order.size(); ++i)
						(void)prepare_layer(event,
						                    context,
						                    order[i],
						                    captures[static_cast<unsigned>(order[i])],
						                    pair.layers[i]);
				}
				if (pair.id != event.pair_id || (event.eye == 1 && !pair.left_drawn))
					return;
				bool any{};
				// Story carries the scene fade once. Announcement ink already includes
				// attenuation from later native fades; titles above black stay visible.
				for (const auto& layer : pair.layers)
					if (layer.ink)
					{
						any = true;
						if (!renderer.draw_screen_layer(
						        context,
						        layer.ink->view.Get(),
						        destination,
						        layer.canvas[event.eye],
						        event.width,
						        event.height,
						        layer.ink->fade,
						        {layer.backdrops.data(), layer.backdrop_count},
						        layer.ink->subtractive ? layer.ink->subtractive->view.Get() : nullptr))
						{
							++failures;
							reason = "narrative composition failed";
							pair = {};
							return;
						}
					}
				if (!any)
					return;
				if (event.eye == 0)
					pair.left_drawn = true;
				else
				{
					++pairs;
					reason = "native narrative pair composed";
					pair = {};
				}
			}
			catch (...)
			{
				++failures;
				reason = "narrative composition exception";
				pair = {};
			}
		}
	}
	std::shared_ptr<const std::vector<std::string>> progress_labels() noexcept
	{
		return labels.load();
	}
	class component final : public component_interface
	{
		eye_composition::consumer_registration composition_registration;

	  public:
		void post_unpack() override
		{
			scheduler::loop(update_labels, scheduler::pipeline::main);
			composition_registration =
			    eye_composition::register_consumer(present, eye_composition::layer::narrative);
			command::add(
			    "vr_narrative_status",
			    []
			    {
				    const auto ink = native_hud_capture::latest_narrative();
				    console::info(
				        "[VR narrative] pairs=%llu failures=%llu reason=%s capture=%ux%u fade=%.3f age_ms=%llu\n",
				        pairs.load(),
				        failures.load(),
				        reason.load(),
				        ink ? ink->width : 0,
				        ink ? ink->height : 0,
				        ink ? ink->fade[3] : 0.f,
				        ink ? GetTickCount64() - ink->timestamp : 0);
			    });
		}
		void pre_destroy() override
		{
			alive.store(false);
			composition_registration.reset();
		}
	};
}
REGISTER_COMPONENT(vr::narrative_ui::component)
