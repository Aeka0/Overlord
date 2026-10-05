#include <std_include.hpp>
#include "directional_ui.hpp"
#include "native_hud_capture.hpp"
#include "native_waypoints.hpp"
#include "narrative_ui.hpp"
#include "eye_composition.hpp"
#include "spatial_panel_renderer.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"

namespace vr::directional_ui
{
	namespace
	{
		std::atomic_bool alive{true};
		std::atomic_uint64_t pairs{}, failures{}, waypoints{}, warnings{};
		struct snapshot
		{
			std::uint64_t id{};
			std::shared_ptr<const native_hud_capture::indicators> ink;
			std::array<std::array<spatial_panel::image_layer,waypoint_capacity+1>,2> layers{};
			unsigned count{};
			bool left_drawn{};
		};
		thread_local snapshot pair;
		thread_local spatial_panel::renderer renderer;
		bool prepare(const eye_composition::event& event, ID3D11DeviceContext* context)
		{
			pair={}; pair.id=event.pair_id;
			if (!event.model_origins.valid) return false;
			auto ink=native_hud_capture::latest_indicators(); if (!ink) return false;
			const auto valid=[&](const std::shared_ptr<const native_hud_capture::frame>& f) {
				return f && f->view && f->generation==event.device_generation &&
					f->context==reinterpret_cast<std::uintptr_t>(context) && narrative_ui::current(f->timestamp,GetTickCount64());
			};
			const auto units=engine_stereo_bridge::get_status().world_scale;
			std::array<matrix,2> vp{};
			for (unsigned eye=0;eye<2;++eye)
			{
				const auto& slot=event.views.eyes[eye];
				if (slot.pair_id!=event.pair_id || slot.output_eye!=eye) return false;
				std::memcpy(vp[eye].data(),slot.bytes.data()+engine_stereo_view::h2_current_view_projection_offset,sizeof(matrix));
			}
			const auto add=[&](const waypoint& marker, ID3D11ShaderResourceView* texture,const vec4& uv) {
				quad world{}; if (!world_quad(marker,event.views.natural_camera,units,world)) return false;
				std::array<projected_quad,2> projected{};
				for (unsigned eye=0;eye<2;++eye)
					if (!project(world,event.model_origins.eyes[eye],vp[eye],units*.12f,projected[eye])) return false;
				for (unsigned eye=0;eye<2;++eye) pair.layers[eye][pair.count]={texture,projected[eye],uv};
				++pair.count; return true;
			};
			if (valid(ink->atlas)) for (unsigned i=0;i<ink->count && i<waypoint_capacity;++i)
			{
				const auto& m=ink->markers[i];
				const vec4 uv{static_cast<float>((i%4)*tile_width)/atlas_size,static_cast<float>((i/4)*tile_height)/atlas_size,
					(m.crop.right-m.crop.x)/atlas_size,(m.crop.bottom-m.crop.y)/atlas_size};
				if (add(m,ink->atlas->view.Get(),uv)) ++waypoints;
			}
			if (valid(ink->warnings))
			{
				waypoint canvas{}; canvas.clamped=true;
				canvas.viewport={static_cast<float>(ink->warnings->width),static_cast<float>(ink->warnings->height)};
				canvas.tangent={1,1}; canvas.crop={0,0,canvas.viewport[0],canvas.viewport[1]};
				if (add(canvas,ink->warnings->view.Get(),{0,0,1,1})) ++warnings;
			}
			pair.ink=std::move(ink); return pair.count!=0;
		}
		void present(const eye_composition::event& event, ID3D11DeviceContext* context,
			ID3D11ShaderResourceView*, ID3D11RenderTargetView* destination) noexcept
		{
			if (!alive.load() || event.eye>1) return;
			try
			{
				if (event.eye==0 && !prepare(event,context)) return;
				if (pair.id!=event.pair_id || !pair.ink || !pair.count || (event.eye==1 && !pair.left_drawn)) return;
				if (!renderer.draw_layers(context,destination,pair.layers[event.eye].data(),pair.count,event.width,event.height))
				{ ++failures; pair={}; return; }
				if (!event.eye) pair.left_drawn=true;
				else { ++pairs; pair={}; }
			}
			catch (...) { ++failures; pair={}; }
		}
	}
	class component final: public component_interface
	{
	public:
		void post_unpack() override
		{
			eye_composition::set_consumer(present,eye_composition::layer::indicators);
			command::add("vr_indicators_status",[] {
				const auto ink=native_hud_capture::latest_indicators();
				console::info("[VR indicators] pairs=%llu failures=%llu waypoint_draws=%llu warning_draws=%llu markers=%u\n",
					pairs.load(),failures.load(),waypoints.load(),warnings.load(),ink ? ink->count : 0);
				const auto native=native_waypoints::get_counters();
				console::info("[VR waypoint source] calls=%llu recorded=%llu matched=%llu rejected=%llu arena_resets=%llu\n",
					native.calls,native.recorded,native.matched,native.rejected,native.resets);
			});
		}
		void pre_destroy() override { alive.store(false); eye_composition::set_consumer(nullptr,eye_composition::layer::indicators); }
	};
}
REGISTER_COMPONENT(vr::directional_ui::component)
