#include <std_include.hpp>
#include "damage_screen.hpp"
#include "native_hud_capture.hpp"
#include "eye_composition.hpp"
#include "spatial_panel_renderer.hpp"
#include "controller_input.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

namespace vr::damage_screen
{
	namespace
	{
		std::atomic_bool alive{true};
		std::atomic_uint64_t pairs{}, failures{};
		std::atomic<const char*> reason{"waiting for native damage"};
		struct pair_snapshot
		{
			std::uint64_t id{};
			std::shared_ptr<const native_hud_capture::frame> ink;
			std::array<spatial_panel::projected_quad, 2> canvas{};
			bool left_drawn{};
		};
		thread_local pair_snapshot pair;
		thread_local spatial_panel::renderer renderer;
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
					const auto* paused = game::Dvar_FindVar("cl_paused");
					if (!engine_stereo_bridge::is_active() || !game::CL_IsCgameInitialized() ||
					    *game::keyCatchers || !paused || paused->current.integer)
					{
						reason = "outside active gameplay";
						return;
					}
					auto ink = native_hud_capture::latest_damage();
					if (!ink || !ink->view || !current(ink->timestamp, GetTickCount64()))
					{
						reason = "no current native blood/low-health draw";
						return;
					}
					if (ink->generation != event.device_generation ||
					    ink->reference_generation != controller_input::latest().reference_generation ||
					    ink->context != reinterpret_cast<std::uintptr_t>(context))
					{
						reason = "capture context or generation changed";
						return;
					}
					std::array<engine_stereo_bridge::eye_projection, 2> projections{};
					for (unsigned eye = 0; eye < 2; ++eye)
					{
						const auto& slot = event.views.eyes[eye];
						if (slot.pair_id != event.pair_id || slot.output_eye != eye ||
						    slot.publication != event.views.eyes[0].publication ||
						    !engine_stereo_view::read_projection(slot, projections[eye]))
						{
							reason = "invalid damage eye projection pair";
							return;
						}
					}
					if (!shared_canvas(projections, pair.canvas))
					{
						reason = "invalid shared damage field of view";
						return;
					}
					pair.ink = std::move(ink);
				}
				if (pair.id != event.pair_id || !pair.ink || (event.eye == 1 && !pair.left_drawn))
					return;
				if (!renderer.draw_screen_layer(context,
				                                pair.ink->view.Get(),
				                                destination,
				                                pair.canvas[event.eye],
				                                event.width,
				                                event.height,
				                                {}))
				{
					++failures;
					reason = "damage composition failed";
					pair = {};
					return;
				}
				if (event.eye == 0)
					pair.left_drawn = true;
				else
				{
					++pairs;
					reason = "native damage pair composed";
					pair = {};
				}
			}
			catch (...)
			{
				++failures;
				reason = "damage composition exception";
				pair = {};
			}
		}
	}
	class component final : public component_interface
	{
		eye_composition::consumer_registration composition_registration;

	  public:
		void post_unpack() override
		{
			composition_registration =
			    eye_composition::register_consumer(present, eye_composition::layer::damage);
			command::add("vr_damage_screen_status",
			             []
			             {
				             const auto ink = native_hud_capture::latest_damage();
				             console::info(
				                 "[VR damage] pairs=%llu failures=%llu reason=%s capture=%ux%u age_ms=%llu\n",
				                 pairs.load(),
				                 failures.load(),
				                 reason.load(),
				                 ink ? ink->width : 0,
				                 ink ? ink->height : 0,
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
REGISTER_COMPONENT(vr::damage_screen::component)
