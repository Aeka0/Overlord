#include <std_include.hpp>
#include "vehicle_hud.hpp"
#include "vehicle_runtime.hpp"
#include "hand_attachment_pose.hpp"
#include "weapon_hud_warning.hpp"
#include "../overlay_text_texture.hpp"
#include "../spatial_panel_renderer.hpp"
#include "component/game_text.hpp"

namespace vr::gameplay::vehicles
{
	namespace
	{
		struct scene {std::uint64_t pair{},publication{};std::uintptr_t record{};std::array<float,12> camera{};};
		std::mutex mutex;std::array<scene,32> scenes{};size_t cursor{};
		thread_local overlay_text_texture text;
		thread_local spatial_panel::renderer renderer;
		struct prepared {std::uint64_t pair{},publication{};bool valid{};std::array<spatial_panel::projected_quad,2> corners;};
		thread_local prepared pair;
	}
	void begin_scene(const engine_stereo_view::slot_pair& views,std::uintptr_t record) noexcept
	{
		if(!active() || !record)return;
		const std::lock_guard lock(mutex);scenes[cursor++%scenes.size()]={views.eyes[0].pair_id,views.eyes[0].publication,record,views.natural_camera};
	}
	void present_hud(const eye_composition::event& event,ID3D11DeviceContext* context,ID3D11ShaderResourceView* background,ID3D11RenderTargetView* output) noexcept
	{
		if(event.eye>1)return;
		if(event.eye==0)
		{
			pair={};pair.pair=event.pair_id;pair.publication=event.views.eyes[0].publication;
			const auto s=latest();if(!presentation_allowed() || !s.owner.can_fire() || !event.model_origins.valid)return;
			scene source;{const std::lock_guard lock(mutex);for(size_t n=0;n<scenes.size();++n){const auto& v=scenes[(cursor+scenes.size()-1-n)%scenes.size()];if(v.pair==pair.pair && v.publication==pair.publication){source=v;break;}}}
			hands::attachments::solved pose;if(!source.record || !hands::attachments::for_record(reinterpret_cast<void*>(source.record),source.camera,pose) || pose.reference!=s.reference)return;
			Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);
			const auto caption=s.quick_loading?std::string(game_text::text(game_text::key::weapon_quick_loading,game_text::current())):std::to_string(s.inserted?std::clamp(s.rounds,0,32):0)+" / 32";
			const auto font=s.quick_loading?nullptr:native_caption_font::bank();
			if(!s.quick_loading && !font)return;
			if(!text.ensure_utf8(device.Get(),caption,48,true,font))return;
			hands::vec right{},up{};for(unsigned i=0;i<3;++i){right[i]=-source.camera[6+i];up[i]=source.camera[9+i];}
			auto center=hands::add(pose.wrists[unsigned(s.owner.rear)].position,event.model_origins.placement);
			center=hands::add(center,hands::scale(up,-.04f*pose.units));
			const float height=weapon_hud::warning_height_meters*pose.units;spatial_panel::quad quad;
			if(!spatial_panel::billboard(center,right,up,height*float(text.width())/float(text.height()),height,quad))return;
			for(unsigned eye=0;eye<2;++eye)
			{
				spatial_panel::matrix matrix;std::memcpy(matrix.data(),event.views.eyes[eye].bytes.data()+engine_stereo_view::h2_current_view_projection_offset,sizeof(matrix));
				if(!spatial_panel::project(quad,event.model_origins.eyes[eye],matrix,pose.units*.12f,pair.corners[eye]))return;
			}
			pair.valid=true;
		}
		if(pair.valid && pair.pair==event.pair_id && pair.publication==event.views.eyes[0].publication)
			(void)renderer.draw(context,background,text.view(),output,pair.corners[event.eye],event.width,event.height,0,0);
	}
}
