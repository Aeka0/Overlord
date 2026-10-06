#include <std_include.hpp>
#include "fixed_sniper.hpp"
#include "../eye_composition.hpp"
#include "../native_hud_capture.hpp"
#include "../screen_scope_layout.hpp"
#include "../spatial_panel_renderer.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>

namespace vr::gameplay::fixed_sniper
{
	namespace
	{
		struct pair_state
		{
			std::uint64_t id{}, publication{}, epoch{}, device{};
			std::shared_ptr<const native_hud_capture::frame> ink;
			std::shared_ptr<const native_hud_capture::frame> shadow, flash;
			std::array<spatial_panel::projected_quad, 2> canvas{};
			auxiliary_scene::request request{};
		};
		thread_local pair_state pair;
		thread_local spatial_panel::renderer renderer;
		thread_local screen_scope::anchored_plane plane;
		std::atomic_uint64_t planned{}, drawn{}, missing{};
		std::atomic<const char*> reason{"inactive"};
		auxiliary_scene::request plan(const eye_composition::event& event) noexcept
		{
			pair = {};
			pair.id = event.pair_id;
			pair.publication = event.views.eyes[0].publication;
			pair.epoch = event.views.screen_scope_epoch;
			pair.device = event.device_generation;
			try
			{
				const auto owner = current();
				const auto captures = native_hud_capture::latest_screen_scope();
				const auto& ink = captures.ink;
				const auto now = GetTickCount64();
				const auto head = head_pose_bridge::get_status();
				if (!pair.epoch || owner.epoch != pair.epoch || !ink || !ink->view ||
				    ink->screen_scope_epoch != pair.epoch || ink->generation != pair.device ||
				    ink->timestamp > now || now - ink->timestamp > 150 ||
				    ink->reference_generation != head.recenter_count || head.recenter_pending ||
				    !head.enabled || !head.pose_available)
				{
					reason = "waiting for current native scope HUD";
					return {};
				}
				if (event.views.screen_scope_aspect > 0 &&
				    std::abs(event.views.screen_scope_aspect - float(ink->width) / ink->height) > .001f)
				{
					reason = "scope canvas resized after visibility publication";
					return {};
				}
				engine_stereo_bridge::eye_projection source{};
				if (!event.model_origins.valid || !std::isfinite(head.world_scale) || head.world_scale <= 0)
					return {};
				spatial_panel::vec3 head_meters{};
				float separation_squared{};
				for (unsigned i = 0; i < 3; ++i)
				{
					head_meters[i] = head.local_position_units[i] / head.world_scale;
					const auto delta = event.model_origins.eyes[0][i] - event.model_origins.eyes[1][i];
					separation_squared += delta * delta;
				}
				const float half_ipd = std::sqrt(separation_squared) * .5f / head.world_scale;
				if (!plane.update(
				        pair.epoch, head.recenter_count, head_meters, head.local_orientation, head_gain()))
				{
					reason = "scope viewer unavailable";
					return {};
				}
				for (unsigned eye = 0; eye < 2; ++eye)
				{
					engine_stereo_bridge::eye_projection projection{};
					if (!engine_stereo_view::read_projection(event.views.eyes[eye], projection) ||
					    !plane.project(projection,
					                   event.views.eyes[eye].view_eye == 0 ? half_ipd : -half_ipd,
					                   ink->width,
					                   ink->height,
					                   pair.canvas[eye]))
					{
						reason = "scope projection invalid";
						return {};
					}
					if (eye == 0)
						source = projection;
				}
				auxiliary_scene::request request;
				// H2's scene target has the HMD aspect; LUI retains the desktop canvas.
				// Preserve native vertical FOV and rebuild horizontal coverage for that
				// canvas so a round target and the original reticle agree after mapping.
				const float half_y = event.views.native_tan_half[1];
				if (!screen_scope::window(
				        source, half_y * ink->width / ink->height, half_y, request.window) ||
				    !auxiliary_scene::valid_window(request.window))
				{
					reason = "native scope field outside source eye";
					return {};
				}
				request.eye = 0;
				request.owner = static_cast<std::uint64_t>(owner.entity);
				request.generation = pair.epoch;
				request.reference = ink->reference_generation;
				request.full_thermal = true;
				request.valid = true;
				request.near_distance = event.views.native_near_distance;
				pair.ink = ink;
				pair.shadow = captures.shadow;
				pair.flash = captures.flash;
				pair.request = request;
				++planned;
				return request;
			}
			catch (...)
			{
				reason = "scope preparation exception";
				return {};
			}
		}
		void present(const eye_composition::event& event,
		             ID3D11DeviceContext* context,
		             ID3D11ShaderResourceView*,
		             ID3D11RenderTargetView* target) noexcept
		{
			if (!event.views.screen_scope_epoch || event.eye > 1 || !context || !target)
				return;
			// A late/missing/failed scope view must never expose unmasked color or
			// stretch a stale eye image. Native titles/fades are composed afterwards.
			const float black[4]{0, 0, 0, 1};
			const auto fail = [&]
			{
				context->ClearRenderTargetView(target, black);
				++missing;
			};
			try
			{
				if (pair.id != event.pair_id || pair.publication != event.views.eyes[event.eye].publication ||
				    pair.device != event.device_generation || pair.epoch != event.views.screen_scope_epoch ||
				    !pair.ink || pair.ink->context != reinterpret_cast<std::uintptr_t>(context) ||
				    !event.auxiliary || !event.auxiliary->valid || !event.auxiliary->full_thermal ||
				    !event.auxiliary_image || event.auxiliary->owner != pair.request.owner ||
				    event.auxiliary->generation != pair.epoch ||
				    event.auxiliary->reference != pair.request.reference)
				{
					if (pair.request.valid)
						reason = "scope image/HUD pair unavailable";
					fail();
					return;
				}
				if (!renderer.draw_screen_scope(context,
				                                event.auxiliary_image,
				                                pair.ink->view.Get(),
				                                target,
				                                pair.canvas[event.eye],
				                                event.width,
				                                event.height,
				                                pair.shadow ? pair.shadow->view.Get() : nullptr,
				                                pair.flash ? pair.flash->view.Get() : nullptr))
				{
					reason = "scope composition rejected";
					fail();
					return;
				}
				++drawn;
				reason = "native thermal scene and scope HUD composed";
			}
			catch (...)
			{
				reason = "scope composition exception";
				fail();
			}
		}
	}
	class presentation_component final : public component_interface
	{
		eye_composition::consumer_registration composition_registration;
		void post_unpack() override
		{
			auxiliary_scene::screen_plan = plan;
			auxiliary_scene::screen_scope_aspect = +[]() noexcept
			{
				const auto capture = native_hud_capture::latest_screen_scope();
				const auto& ink = capture.ink;
				return ink && ink->height && ink->screen_scope_epoch == current().epoch
				           ? float(ink->width) / ink->height
				           : 0.f;
			};
			composition_registration =
			    eye_composition::register_consumer(present, eye_composition::layer::screen_scope);
			::command::add(
			    "vr_fixedSniper_renderStatus",
			    []
			    {
				    const auto capture = native_hud_capture::latest_screen_scope();
				    const auto& ink = capture.ink;
				    const auto report = std::format(
				        "planned={} eyes={} missing={} reason={}\nepoch={} hud={}x{} shadow={} flash={} age_ms={}\n",
				        planned.load(),
				        drawn.load(),
				        missing.load(),
				        reason.load(),
				        current().epoch,
				        ink ? ink->width : 0,
				        ink ? ink->height : 0,
				        bool(capture.shadow),
				        bool(capture.flash),
				        ink ? GetTickCount64() - ink->timestamp : 0);
				    console::info("[VR fixed sniper] %s", report.c_str());
				    utils::io::write_file_atomic("minidumps/overlord-fixed-sniper.txt", report);
			    });
		}
		void pre_destroy() override
		{
			auxiliary_scene::screen_plan = nullptr;
			auxiliary_scene::screen_scope_aspect = nullptr;
			composition_registration.reset();
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::fixed_sniper::presentation_component)
