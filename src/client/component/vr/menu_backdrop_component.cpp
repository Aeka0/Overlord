#include <std_include.hpp>
#include "menu_overlay.hpp"
#include "native_hud_capture.hpp"
#include "eye_composition.hpp"
#include "spatial_panel_renderer.hpp"
#include "head_pose_bridge.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"

namespace vr::menu_backdrop
{
	namespace
	{
		std::atomic_bool alive{true};
		std::atomic_uint64_t pairs{},failures{},skipped{};
		std::atomic<const char*> reason{"waiting for spatial menu canvas"};
		struct pair_state
		{
			std::uint64_t id{};
			std::shared_ptr<const native_hud_capture::frame> canvas;
			std::array<spatial_panel::projected_quad,2> corners;
			bool left_drawn{};
		};
		thread_local pair_state pair;
		thread_local spatial_panel::renderer renderer;
		bool prepare(const eye_composition::event& event,ID3D11DeviceContext* context)
		{
			const auto surface=menu_overlay::latest_backdrop();const auto current=native_menu::current();
			const auto now=GetTickCount64();
			if(!surface.canvas||current.frontend||!current.count||surface.session!=current.session||surface.revision!=current.revision||
				now<surface.stamp||now-surface.stamp>250||now<surface.canvas->timestamp||now-surface.canvas->timestamp>250)
			{reason="no current spatial menu canvas";return false;}
			if(!surface.canvas->view||surface.canvas->generation!=event.device_generation||
				surface.canvas->context!=reinterpret_cast<std::uintptr_t>(context)||!event.model_origins.valid)
			{reason="menu canvas device/context mismatch";return false;}
			head_pose_bridge::spatial_frame frame;
			if(!head_pose_bridge::get_spatial_frame(frame)||frame.generation!=surface.reference)
			{reason="waiting for matching spatial camera";return false;}
			const auto& g=surface.placement;const auto& a=g.basis;
			head_pose_bridge::tracking_pose tracking;
			tracking.position_meters=menu_surface::add(a.position,menu_surface::mul(a.forward,g.distance));
			for(unsigned r=0;r<3;++r){tracking.orientation[r][0]=a.right[r];tracking.orientation[r][1]=a.up[r];tracking.orientation[r][2]=-a.forward[r];}
			head_pose_bridge::world_pose pose;
			if(!a.valid||!head_pose_bridge::tracking_to_world(frame,tracking,pose))
			{reason="invalid menu anchor transform";return false;}
			spatial_panel::vec3 right{};for(unsigned r=0;r<3;++r)right[r]=-pose.axis[1][r];
			spatial_panel::quad quad;
			if(!spatial_panel::billboard(pose.position,right,pose.axis[2],g.width*frame.units_per_meter,g.height*frame.units_per_meter,quad))
			{reason="invalid spatial menu plane";return false;}
			for(unsigned eye=0;eye<2;++eye)
			{
				const auto& slot=event.views.eyes[eye];
				if(slot.pair_id!=event.pair_id||slot.output_eye!=eye){reason="menu eye pair mismatch";return false;}
				spatial_panel::matrix vp;std::memcpy(vp.data(),slot.bytes.data()+engine_stereo_view::h2_current_view_projection_offset,sizeof(vp));
				if(!spatial_panel::project(quad,event.model_origins.eyes[eye],vp,frame.units_per_meter*.01f,pair.corners[eye],true,spatial_panel::clip_mode::hardware))
				{reason="menu plane outside native depth/frustum";return false;}
			}
			pair.canvas=surface.canvas;return true;
		}
		void present(const eye_composition::event& event,ID3D11DeviceContext* context,
			ID3D11ShaderResourceView*,ID3D11RenderTargetView* destination) noexcept
		{
			if(!alive||event.eye>1)return;
			try
			{
				if(event.eye==0){pair={};pair.id=event.pair_id;if(!prepare(event,context)){++skipped;return;}}
				if(pair.id!=event.pair_id||!pair.canvas||(event.eye==1&&!pair.left_drawn))return;
				if(!renderer.draw_panel_blur(context,destination,pair.corners[event.eye],
					event.width,event.height,14.f,event.scene_depth))
				{++failures;reason="spatial menu blur composition failed";pair={};return;}
				if(!event.eye)pair.left_drawn=true;
				else{++pairs;reason="entire menu plane blurred both eyes";pair={};}
			}
			catch(...){++failures;reason="spatial menu blur exception";pair={};}
		}
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			eye_composition::set_consumer(present,eye_composition::layer::menu_backdrop);
			command::add("vr_menu_blur_status",[]{console::info("[VR menu blur] pairs=%llu skipped=%llu failures=%llu reason=%s\n",
				pairs.load(),skipped.load(),failures.load(),reason.load());});
		}
		void pre_destroy() override {alive=false;eye_composition::set_consumer(nullptr,eye_composition::layer::menu_backdrop);}
	};
}
REGISTER_COMPONENT(vr::menu_backdrop::component)
