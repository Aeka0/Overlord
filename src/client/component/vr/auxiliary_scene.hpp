#pragma once
#include "engine_stereo_view.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <wrl/client.h>

namespace vr::eye_composition { struct event; }
namespace vr::auxiliary_scene
{
	// A single bounded scene consumer, distinct from the two runtime output eyes.
	inline constexpr unsigned view_index = 2, view_count = 3, no_view = 3;
	struct request
	{
		unsigned eye{2};
		std::array<float,4> window{}; // Normalized source-eye x, y, width, height.
		std::uint64_t owner{}, generation{};
		bool valid{};
		std::uint64_t reference{},revision{};
		bool full_thermal{}; // Private optical view only, never the ordinary eye records.
		float near_distance{}; // 0 inherits the source eye; scripted optics retain native clipping.
		bool native_center{}; // Scope camera at the published center origin, not either physical eye.
	};
	using planner = request(*)(const eye_composition::event&) noexcept;
	inline std::atomic<planner> plan{};
	inline std::atomic<planner> screen_plan{};
	inline std::atomic<planner> weapon_display_plan{};
	inline std::atomic<std::uint64_t(*)() noexcept> weapon_display_epoch{};
	inline bool center_record(std::array<std::uint8_t,engine_stereo_view::h2_scene_record_size>& record,const std::array<float,3>& center) noexcept
	{
		using namespace engine_stereo_view;
		std::array<float,3> eye{},relative{};
		std::memcpy(eye.data(),record.data()+h2_view_origin_offset,sizeof(eye));
		std::memcpy(relative.data(),record.data()+h2_relative_eye_offset_offset,sizeof(relative));
		for(unsigned i=0;i<3;++i)
		{
			if(!std::isfinite(center[i]) || !std::isfinite(eye[i]) || !std::isfinite(relative[i]) ||
				std::abs(center[i])>1e7f || std::abs(center[i]-eye[i])>1000)return false;
			relative[i]+=center[i]-eye[i];
		}
		// VP is camera-relative: its rotation/projection needs no translation.
		// Both source-eye frusta already enclose this center camera and narrow crop.
		std::memcpy(record.data()+h2_view_origin_offset,center.data(),sizeof(center));
		std::memcpy(record.data()+h2_relative_eye_offset_offset,relative.data(),sizeof(relative));return true;
	}
	inline std::atomic<std::uint64_t(*)() noexcept> screen_scope_epoch{};
	inline std::atomic<float(*)() noexcept> screen_scope_aspect{};
	inline bool apply_near(std::array<std::uint8_t,engine_stereo_view::h2_scene_record_size>& record,float distance) noexcept
	{
		if(distance==0)return true;
		float previous{};std::memcpy(&previous,record.data()+0x78,4);
		if(!std::isfinite(distance) || distance<=0 || distance>10000 || !std::isfinite(previous) || previous<=0)return false;
		const float scale=distance/previous;
		if(!std::isfinite(scale) || scale<.0001f || scale>10000)return false;
		// Scaling clip Z scales projection/VP column 2 and inverse VP row 2.
		// No native finalizer replay: jitter and the other three clip axes stay exact.
		for(unsigned offset:{0x40u,0x80u})for(unsigned row=0;row<4;++row)
		{float v{};std::memcpy(&v,record.data()+offset+row*16+8,4);v*=scale;std::memcpy(record.data()+offset+row*16+8,&v,4);}
		for(unsigned column=0;column<4;++column)
		{float v{};std::memcpy(&v,record.data()+0xc0+32+column*4,4);v/=scale;std::memcpy(record.data()+0xc0+32+column*4,&v,4);}
		std::memcpy(record.data()+0x148,&distance,4);return true;
	}
	inline bool apply_thermal(std::array<std::uint8_t,engine_stereo_view::h2_scene_record_size>& record,const request& camera) noexcept
	{
		if(!camera.full_thermal)return true;
		// Never invent thermal activation; native vision/heat must already be on.
		if(!(record[0x204]&1))return false;
		record[0x204]|=2;return true;
	}
	struct history
	{
		std::array<float,16> vp{};
		std::array<float,3> origin{};
		request camera{};
		std::uint64_t device{},pair{};
		bool valid{};
	};

	inline bool valid_window(const std::array<float,4>& w) noexcept
	{
		for (float v:w) if (!std::isfinite(v)) return false;
		return w[0]>=0 && w[1]>=0 && w[2]>=.0001f && w[3]>=.0001f &&
			w[0]+w[2]<=1 && w[1]+w[3]<=1;
	}
	// Exact clip-space crop of an already finalized camera. Keeps its origin,
	// near plane, jitter and orientation, so every ray remains inside that eye.
	// Updating inverse VP algebraically avoids running H2's jitter finalizer twice.
	inline bool crop_record(const std::array<std::uint8_t,engine_stereo_view::h2_scene_record_size>& source,
		const std::array<float,4>& window,
		std::array<std::uint8_t,engine_stereo_view::h2_scene_record_size>& output) noexcept
	{
		if (!valid_window(window)) return false;
		output=source;
		const float sx=window[2],sy=window[3],center_x=2*window[0]+sx-1,center_y=1-2*window[1]-sy;
		for (auto offset:{0x40u,0x80u})
		{
			std::array<float,16> m{};
			std::memcpy(m.data(),source.data()+offset,sizeof(m));
			for (unsigned row=0;row<4;++row)
			{
				m[row*4]=(m[row*4]-center_x*m[row*4+3])/sx;
				m[row*4+1]=(m[row*4+1]-center_y*m[row*4+3])/sy;
			}
			for (float v:m) if (!std::isfinite(v)) return false;
			std::memcpy(output.data()+offset,m.data(),sizeof(m));
		}
		std::array<float,16> inverse{},m{};
		std::memcpy(inverse.data(),source.data()+0xC0,sizeof(inverse));m=inverse;
		for (unsigned c=0;c<4;++c)
		{
			m[c]=sx*inverse[c];m[4+c]=sy*inverse[4+c];
			m[12+c]=inverse[12+c]+center_x*inverse[c]+center_y*inverse[4+c];
		}
		for(float v:m) if(!std::isfinite(v)) return false;
		std::memcpy(output.data()+0xC0,m.data(),sizeof(m));
		std::memcpy(m.data(),output.data()+0x40,sizeof(m));
		if(m[0]<=0 || m[5]<=0 || m[11]!=1 || m[14]<=0) return false;
		const float half_x=(std::max)(std::abs((-1-m[8])/m[0]),std::abs((1-m[8])/m[0]));
		const float half_y=(std::max)(std::abs((-1-m[9])/m[5]),std::abs((1-m[9])/m[5]));
		for(auto offset:{0x140u,0x150u}) std::memcpy(output.data()+offset,&half_x,sizeof(float));
		for(auto offset:{0x144u,0x154u}) std::memcpy(output.data()+offset,&half_y,sizeof(float));
		const std::array<float,4> rays{1/m[0],-m[8]/m[0],1/m[5],-m[9]/m[5]};
		std::memcpy(output.data()+engine_stereo_view::h2_inverse_scene_projection_constant_offset,rays.data(),sizeof(rays));
		return true;
	}

	inline bool prepare_history(std::array<std::uint8_t,engine_stereo_view::h2_scene_record_size>& record,
		const request& camera,std::uint64_t device,std::uint64_t pair,const history& previous,
		history& next,bool& reset) noexcept
	{
		using namespace engine_stereo_view;
		next={};next.camera=camera;next.device=device;next.pair=pair;
		std::memcpy(next.vp.data(),record.data()+h2_current_view_projection_offset,sizeof(next.vp));
		std::memcpy(next.origin.data(),record.data()+h2_view_origin_offset,sizeof(next.origin));
		float scale{};std::memcpy(&scale,record.data()+h2_previous_eye_position_constant_offset+12,4);
		if(!camera.valid || !device || !pair || !std::isfinite(scale) || scale==0) return false;
		for(float v:next.vp) if(!std::isfinite(v)) return false;
		for(float v:next.origin) if(!std::isfinite(v)) return false;
		reset=!previous.valid || previous.device!=device || previous.pair>=pair ||
			previous.camera.owner!=camera.owner || previous.camera.generation!=camera.generation ||
			previous.camera.eye!=camera.eye || previous.camera.reference!=camera.reference ||
			previous.camera.revision!=camera.revision || previous.camera.full_thermal!=camera.full_thermal ||
			previous.camera.near_distance!=camera.near_distance || previous.camera.native_center!=camera.native_center;
		if(!reset) for(unsigned i=2;i<4;++i)
			reset|=camera.window[i]<previous.camera.window[i]*.8f || camera.window[i]>previous.camera.window[i]*1.25f;
		const auto& vp=reset ? next.vp : previous.vp;
		const auto& origin=reset ? next.origin : previous.origin;
		std::array<float,4> delta{0,0,0,scale};
		for(unsigned i=0;i<3;++i) {delta[i]=(next.origin[i]-origin[i])*scale;if(!std::isfinite(delta[i])) return false;}
		std::memcpy(record.data()+h2_temporal_history_view_projection_offset,vp.data(),sizeof(vp));
		std::memcpy(record.data()+h2_previous_view_projection_constant_offset,vp.data(),sizeof(vp));
		std::memcpy(record.data()+h2_previous_eye_position_constant_offset,delta.data(),sizeof(delta));
		next.valid=true;
		return true;
	}

	// Owner-thread copies; never retains a native borrowed DSV as next-frame data.
	class image_copy
	{
	public:
		bool capture(ID3D11DeviceContext*,ID3D11Texture2D*,ID3D11DepthStencilView*,std::uint64_t) noexcept;
		void reset() noexcept { *this={}; }
		Microsoft::WRL::ComPtr<ID3D11Texture2D> color,depth;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_view;
	private:
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		std::uint64_t generation_{};
		D3D11_TEXTURE2D_DESC color_desc_{},depth_desc_{};
		D3D11_DEPTH_STENCIL_VIEW_DESC depth_view_desc_{};
	};
}
