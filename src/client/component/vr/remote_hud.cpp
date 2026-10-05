#include <std_include.hpp>
#include "remote_hud_policy.hpp"
#include "native_hud_capture.hpp"
#include "eye_composition.hpp"
#include "spatial_panel_renderer.hpp"
#include "gameplay/notebook_runtime.hpp"
#include "controller_input.hpp"
#include "loader/component_loader.hpp"
#include "component/command.hpp"
#include "component/console.hpp"

namespace vr::remote_hud
{
	namespace
	{
		struct pair_state
		{
			std::uint64_t id{},epoch{},publication{};
			native_hud_capture::remote_frames ink;
			std::array<std::array<spatial_panel::projected_quad,2>,frame_count> canvas{};
		};
		thread_local pair_state pair;
		thread_local spatial_panel::renderer renderer;
		std::atomic_uint64_t target_pairs{},instrument_pairs{},composition_failures{};
		std::atomic_uint lease_mask{},projected_mask{};
		void present(const eye_composition::event& event,ID3D11DeviceContext* context,
			ID3D11ShaderResourceView*,ID3D11RenderTargetView* output)noexcept
		{
			if(event.eye>1 || !context || !output)return;
			if(event.eye==0)
			{
				lease_mask=projected_mask=0;
				pair={};const auto epoch=event.views.remote_camera_epoch;
				const auto captures=native_hud_capture::latest_remote();const auto now=GetTickCount64();
				if(!epoch || gameplay::equipment::special::notebook::camera_epoch()!=epoch)return;
				const auto reference=controller_input::latest().reference_generation;
				for(unsigned i=0;i<captures.size();++i)
				{
					const auto& ink=captures[i];
					if(ink && ink->view && ink->remote_camera_epoch==epoch && ink->generation==event.device_generation &&
						ink->context==reinterpret_cast<std::uintptr_t>(context) && now>=ink->timestamp && now-ink->timestamp<=150 &&
						ink->reference_generation==reference){pair.ink[i]=ink;lease_mask.fetch_or(1u<<i);}
				}
				for(unsigned i=0;i<pair.ink.size();++i)if(const auto& ink=pair.ink[i])
				{
					const auto tangents=i/stream_count==unsigned(layer::instruments)?instrument_tangents(ink->width,ink->height):event.views.native_tan_half;
					for(unsigned eye=0;eye<2;++eye)
					{
						engine_stereo_bridge::eye_projection projection;
						if(!engine_stereo_view::read_projection(event.views.eyes[eye],projection) ||
							!project(tangents,projection,pair.canvas[i][eye])){pair.ink[i].reset();break;}
					}
					if(pair.ink[i])projected_mask.fetch_or(1u<<i);
				}
				pair.id=event.pair_id;pair.epoch=epoch;pair.publication=event.views.eyes[0].publication;
			}
			if(pair.id!=event.pair_id || pair.publication!=event.views.eyes[0].publication ||
				pair.epoch!=event.views.remote_camera_epoch || gameplay::equipment::special::notebook::camera_epoch()!=pair.epoch)return;
			std::array<spatial_panel::image_layer,frame_count> layers{};unsigned count{};
			// Expanded native instrument art can contain opaque backing. Target
			// markers must remain above it, at their unchanged optical projection.
			for(const auto i:composition_order)if(const auto& ink=pair.ink[i])layers[count++]={ink->view.Get(),pair.canvas[i][event.eye]};
			if(count)
			{
				if(!renderer.draw_layers(context,output,layers.data(),count,event.width,event.height))
				{++composition_failures;pair={};return;}
				if(event.eye==1)
				{
					if(pair.ink[index(0,layer::targets)] || pair.ink[index(1,layer::targets)])++target_pairs;
					if(pair.ink[index(0,layer::instruments)] || pair.ink[index(1,layer::instruments)])++instrument_pairs;
				}
			}
		}
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			eye_composition::set_consumer(present,eye_composition::layer::remote_hud);
			command::add("vr_uav_hud_status",[] {
				console::info("[VR UAV presentation] epoch=%llu valid_leases=0x%x projected=0x%x target_pairs=%llu instrument_pairs=%llu failures=%llu\n",
					gameplay::equipment::special::notebook::camera_epoch(),lease_mask.load(),projected_mask.load(),
					target_pairs.load(),instrument_pairs.load(),composition_failures.load());
			});
		}
		void pre_destroy()override {eye_composition::set_consumer(nullptr,eye_composition::layer::remote_hud);}
	};
}
REGISTER_COMPONENT(vr::remote_hud::component)
