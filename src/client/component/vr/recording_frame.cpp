#include <std_include.hpp>
#include "recording_frame.hpp"
#include "recording_frame_layout.hpp"
#include "eye_composition.hpp"
#include "spatial_panel_renderer.hpp"
#include "overlay_text_texture.hpp"
#include "settings.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/game_text.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

namespace vr::recording_frame
{
	namespace
	{
		struct viewport
		{
			float width{}, height{}, fov{};
		};
		std::mutex mutex;
		viewport display;
		game::dvar_t* enabled{};
		game::dvar_t* dim{};
		std::atomic_bool alive{true};
		std::atomic_uint64_t draws{}, skips{}, caption_skips{};
		thread_local spatial_panel::renderer renderer;
		thread_local overlay_text_texture label;
		void compose(const eye_composition::event& event,
		             ID3D11DeviceContext* context,
		             ID3D11ShaderResourceView*,
		             ID3D11RenderTargetView* target) noexcept
		{
			if (event.eye > 1 || !event.pair_id || (event.eye == 1 && !event.recording_crop) || !context ||
			    !target || !alive.load())
				return;
			// Both eyes of one image family use the same GUI/config snapshot.
			struct snapshot
			{
				std::uint64_t pair{}, generation{};
				viewport display;
				float dim{};
				bool enabled{};
			};
			thread_local snapshot guide;
			if (guide.pair != event.pair_id || guide.generation != event.device_generation)
			{
				const std::lock_guard lock(mutex);
				const float strength = dim ? dim->current.value : settings::recording_dim.default_value;
				guide = {event.pair_id,
				         event.device_generation,
				         display,
				         std::isfinite(strength) ? std::clamp(strength, 0.f, 100.f) / 100.f : outside_dim,
				         enabled && enabled->current.enabled};
			}
			if (!guide.enabled)
				return;
			engine_stereo_bridge::eye_projection projection, target_projection;
			const auto& right = event.views.eyes[1];
			const auto& eye = event.views.eyes[event.eye];
			if (right.pair_id != event.pair_id || right.output_eye != 1 || eye.pair_id != event.pair_id ||
			    eye.output_eye != event.eye || !engine_stereo_view::read_projection(right, projection) ||
			    !engine_stereo_view::read_projection(eye, target_projection))
			{
				++skips;
				return;
			}
			const auto crop = desktop_mirror::project(projection,
			                                          event.width,
			                                          event.height,
			                                          guide.display.width,
			                                          guide.display.height,
			                                          guide.display.fov);
			const auto frame =
			    make_layout(event.eye == 1 ? crop : other_eye_crop(crop, projection, target_projection),
			                event.width,
			                event.height);
			if (!frame.valid)
			{
				++skips;
				return;
			}
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			context->GetDevice(&device);
			spatial_panel::vec4 caption_pixels{};
			const auto caption = game_text::text(game_text::key::recording_preview, game_text::current());
			if (label.ensure_utf8(device.Get(), caption, caption_font_pixels(event.height)))
				caption_pixels =
				    caption_bounds(frame, event.width, event.height, label.width(), label.height());
			if (caption_pixels[2] <= 0)
				++caption_skips;
			if (renderer.draw_recording_frame(context,
			                                  target,
			                                  event.width,
			                                  event.height,
			                                  frame.protected_pixels,
			                                  frame.line_pixels,
			                                  guide.dim,
			                                  caption_pixels[2] > 0 ? label.view() : nullptr,
			                                  caption_pixels))
			{
				// Freeze the exact crop with the decorated image. GUI changes to FOV
				// or aspect must not sample an older guide using a newer rectangle.
				if (event.eye == 1)
					*event.recording_crop = crop;
				++draws;
			}
			else
				++skips;
		}
	}
	void publish_viewport(float width, float height, float horizontal_fov) noexcept
	{
		const std::lock_guard lock(mutex);
		display = {width, height, horizontal_fov};
	}
	class component final : public component_interface
	{
		eye_composition::consumer_registration composition_registration;
		void post_unpack() override
		{
			enabled = dvars::register_bool(
			    settings::recording_mode.name,
			    settings::recording_mode.default_value,
			    game::DVAR_FLAG_SAVED,
			    "Show the desktop recording angle in both eyes with a border and dimmed exterior");
			dim = dvars::register_float(
			    settings::recording_dim.name,
			    settings::recording_dim.default_value,
			    settings::recording_dim.min,
			    settings::recording_dim.max,
			    game::DVAR_FLAG_SAVED,
			    "Live stream preview exterior dimming percent; 100 hides the exterior completely");
			composition_registration =
			    eye_composition::register_consumer(compose, eye_composition::layer::recording_frame);
			command::add(
			    "vr_recording_status",
			    []
			    {
				    console::info(
				        "[VR recording frame] enabled=%d draws=%llu skipped=%llu caption_skips=%llu eyes=both outside_dim=%.0f%%\n",
				        enabled && enabled->current.enabled,
				        draws.load(),
				        skips.load(),
				        caption_skips.load(),
				        dim ? dim->current.value : settings::recording_dim.default_value);
			    });
		}
		void pre_destroy() override
		{
			alive = false;
			composition_registration.reset();
		}
	};
}
REGISTER_COMPONENT(vr::recording_frame::component)
