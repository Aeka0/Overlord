#include <std_include.hpp>
#include "fixed_sniper.hpp"
#include "../eye_composition.hpp"
#include "../native_hud_capture.hpp"
#include "../screen_scope_layout.hpp"
#include "../spatial_panel_renderer.hpp"
#include "../diagnostics/screen_display.hpp"
#include "../diagnostics/report_paths.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>

namespace vr::gameplay::fixed_sniper
{
	namespace
	{
		namespace probe=diagnostics::screen;
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
			auto observed=probe::observation(event,pair.epoch,GetTickCount64());observed.stage="plan_hud";
			const auto record=gsl::finally([&]{probe::fixed_scope.record(observed);});
			try
			{
				const auto owner = current();
				const auto captures = native_hud_capture::latest_screen_scope();
				const auto& ink = captures.ink;
				const auto now = GetTickCount64();
				const auto head = head_pose_bridge::get_status();
				observed.owner_epoch=owner.epoch;observed.owner_id=owner.entity>=0?static_cast<std::uint64_t>(owner.entity):0;observed.reference_id=head.recenter_count;
				probe::hud(observed,ink.get());
				probe::check(observed.rejected,pair.epoch&&owner.epoch==pair.epoch,probe::epoch);
				probe::check(observed.hud_rejected,bool(ink),probe::hud_missing);
				if(ink)
				{
					probe::check(observed.hud_rejected,bool(ink->view),probe::hud_view);
					probe::check(observed.hud_rejected,ink->screen_scope_epoch==pair.epoch,probe::hud_scope_epoch);
					probe::check(observed.hud_rejected,ink->generation==pair.device,probe::hud_device);
					probe::check(observed.hud_rejected,ink->timestamp<=now&&now-ink->timestamp<=150,probe::hud_age);
					probe::check(observed.hud_rejected,ink->reference_generation==head.recenter_count,probe::hud_reference);
				}
				observed.rejected|=observed.hud_rejected;
				probe::check(observed.rejected,!head.recenter_pending,probe::recenter);
				probe::check(observed.rejected,head.enabled&&head.pose_available,probe::tracking);
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
					observed.rejected|=probe::canvas_aspect;
					return {};
				}
				engine_stereo_bridge::eye_projection source{};
				observed.stage="plan_geometry";
				probe::check(observed.rejected,event.model_origins.valid,probe::origins);
				probe::check(observed.rejected,std::isfinite(head.world_scale)&&head.world_scale>0,probe::world_scale);
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
					observed.rejected|=probe::plane;
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
						observed.rejected|=probe::projection;
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
				observed.stage="plan_crop";
				if (!screen_scope::window(
				        source, half_y * ink->width / ink->height, half_y, request.window) ||
				    !auxiliary_scene::valid_window(request.window))
				{
					reason = "native scope field outside source eye";
					observed.rejected|=probe::window;observed.crop=request.window;
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
				observed.stage="planned";observed.crop=request.window;observed.success=true;
				observed.layer_mask=1u|(captures.shadow?2u:0u)|(captures.flash?4u:0u);
				++planned;
				return request;
			}
			catch (...)
			{
				reason = "scope preparation exception";
				observed.rejected|=probe::exception;
				return {};
			}
		}
		void present(const eye_composition::event& event,
		             ID3D11DeviceContext* context,
		             ID3D11ShaderResourceView*,
		             ID3D11RenderTargetView* target) noexcept
		{
			auto observed=probe::observation(event,event.views.screen_scope_epoch,GetTickCount64());observed.stage="compose";
			observed.expected_pair=pair.id;observed.expected_publication=pair.publication;observed.expected_device=pair.device;
			observed.owner_epoch=pair.epoch;observed.owner_id=pair.request.owner;observed.owner_generation=pair.request.generation;
			observed.reference_id=pair.request.reference;observed.context=reinterpret_cast<std::uintptr_t>(context);probe::hud(observed,pair.ink.get());
			const auto record=gsl::finally([&]{probe::fixed_scope.record(observed);});
			probe::check(observed.rejected,event.eye<2&&context&&target,probe::composition);
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
				probe::check(observed.rejected,pair.id==event.pair_id,probe::pair);
				probe::check(observed.rejected,pair.publication==event.views.eyes[event.eye].publication,probe::publication);
				probe::check(observed.rejected,pair.device==event.device_generation,probe::device);
				probe::check(observed.rejected,pair.epoch==event.views.screen_scope_epoch,probe::epoch);
				probe::check(observed.rejected,bool(pair.ink),probe::hud_missing);
				if(pair.ink)probe::check(observed.rejected,pair.ink->context==observed.context,probe::hud_context);
				probe::check(observed.rejected,event.auxiliary&&event.auxiliary->valid,probe::auxiliary_missing);
				probe::check(observed.rejected,event.auxiliary_image!=nullptr,probe::auxiliary_image);
				if(event.auxiliary)
				{
					probe::check(observed.rejected,event.auxiliary->full_thermal,probe::auxiliary_mode);
					probe::check(observed.rejected,event.auxiliary->owner==pair.request.owner,probe::auxiliary_owner);
					probe::check(observed.rejected,event.auxiliary->generation==pair.epoch,probe::auxiliary_generation);
					probe::check(observed.rejected,event.auxiliary->reference==pair.request.reference,probe::auxiliary_reference);
				}
				observed.canvas_min_w=observed.canvas_max_w=pair.canvas[event.eye][0][3];
				for(const auto& corner:pair.canvas[event.eye]){observed.canvas_min_w=(std::min)(observed.canvas_min_w,corner[3]);observed.canvas_max_w=(std::max)(observed.canvas_max_w,corner[3]);}
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
					probe::draw_result(observed,renderer.last_scope_draw());
					observed.rejected|=probe::composition;
					fail();
					return;
				}
				++drawn;
				probe::draw_result(observed,renderer.last_scope_draw());
				observed.stage="composed";observed.success=true;
				reason = "native thermal scene and scope HUD composed";
			}
			catch (...)
			{
				reason = "scope composition exception";
				observed.rejected|=probe::exception;
				fail();
			}
		}
	}
	std::string format_render_status()
	{
		return std::format("[VR fixed sniper render] planner_registered={} planned={} eyes={} missing={} reason={}\n",
			auxiliary_scene::screen_plan.load()!=nullptr,planned.load(),drawn.load(),missing.load(),reason.load())+
			probe::fixed_scope.format("fixed sniper display",GetTickCount64());
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
				    const auto report=format_render_status();console::print_text(console::con_type_info,report);
				    const auto path=diagnostics::save_named_report("overlord-fixed-sniper.txt",report);
				    if(path.empty())console::error("[VR fixed sniper] Report save failed\n");
				    else console::info("[VR fixed sniper] Saved %s\n",diagnostics::report_path_text(path).c_str());
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
