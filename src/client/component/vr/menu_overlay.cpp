#include <std_include.hpp>
#include "menu_overlay.hpp"
#include "native_hud_capture.hpp"

namespace vr::menu_overlay
{
	void publish_backdrop(backdrop value) noexcept
	{
		menu_backdrop::publish(std::move(value));
	}
	backdrop latest_backdrop() noexcept
	{
		return menu_backdrop::latest();
	}
	bool presenter::release(surface& s) noexcept
	{
		if(!api_||!s.handle){s={};return true;}
		if(s.shown){(void)api_->HideOverlay(s.handle);s.shown=false;}
		if(s.lease||s.failed_lease)
		{
			const auto error=api_->ClearOverlayTexture(s.handle);
			if(error!=VROverlayError_None){last_error_=error;++errors_;return false;}
		}
		s.lease.reset();s.failed_lease.reset();s.sequence=0;s.id=0;return true;
	}
	void presenter::initialize(IVROverlay* api) noexcept
	{
		api_=api;native_menu::set_requested(api!=nullptr);
	}
	bool presenter::capture_movie(ID3D11DeviceContext* context,ID3D11Texture2D* source,std::uint64_t generation,bool active) noexcept
	{
		if(active!=movie_active_){movie_active_=active;++movie_session_;movie_frame_.reset();}
		if(!active)return true;
		if(!context||!source)return false;
		try
		{
			D3D11_TEXTURE2D_DESC desc;source->GetDesc(&desc);
			if(!desc.Width||!desc.Height||desc.Width>8192||desc.Height>8192||std::uint64_t(desc.Width)*desc.Height>16777216||
				desc.SampleDesc.Count!=1||desc.ArraySize!=1||desc.MipLevels!=1)return false;
			if(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&
				desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return false;
			Microsoft::WRL::ComPtr<ID3D11Device> device,source_device;context->GetDevice(&device);source->GetDevice(&source_device);
			if(device!=source_device||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
			for(auto& slot:movie_slots_)
			{
				if(slot&&slot.use_count()!=1)continue;
				if(!slot)slot=std::make_shared<native_hud_capture::frame>();
				D3D11_TEXTURE2D_DESC old{};if(slot->texture)slot->texture->GetDesc(&old);
				if(slot->generation!=generation||old.Width!=desc.Width||old.Height!=desc.Height||old.Format!=desc.Format)
				{
					*slot={};desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=desc.MiscFlags=0;
					if(FAILED(device->CreateTexture2D(&desc,nullptr,&slot->texture)))return false;
				}
				context->CopyResource(slot->texture.Get(),source);
				slot->width=desc.Width;slot->height=desc.Height;slot->generation=generation;slot->sequence=++movie_sequence_;
				slot->timestamp=GetTickCount64();slot->context=reinterpret_cast<std::uintptr_t>(context);
				movie_frame_=slot;return true;
			}
		}
		catch(...){++errors_;}
		return false;
	}
	void presenter::shutdown() noexcept
	{
		publish_backdrop({});
		native_menu::set_requested(false);
		for(auto& s:surfaces_)
		{
			if(api_&&s.handle)
			{
				(void)api_->HideOverlay(s.handle);
				const auto clear=api_->ClearOverlayTexture(s.handle);
				const auto destroy=api_->DestroyOverlay(s.handle);
				if(clear!=VROverlayError_None&&destroy!=VROverlayError_None)
				{last_error_=destroy;++errors_;continue;}
			}
			s={};
		}
		api_=nullptr;anchor_={};session_=reference_=last_time_=device_=0;pointer_owner_.reset();movie_active_=false;movie_frame_.reset();movie_slots_={};
	}
	void presenter::runtime_detached() noexcept {surfaces_={};api_=nullptr;publish_backdrop({});}
	void presenter::update(const head_pose_bridge::tracking_pose& head,std::uint64_t device,bool render_available,std::uint64_t timestamp) noexcept
	{
		try
		{
			if(!api_)return;
			const auto now=timestamp?timestamp:GetTickCount64();auto current=native_menu::current();auto image=native_menu::latest();
			if(movie_active_)
			{
				current={};current.enabled=current.frontend=current.blocked=true;current.session=movie_session_;current.revision=movie_session_;current.timestamp=now;
				auto movie=std::make_shared<native_menu::images>();movie->owner=current;
				if(movie_frame_&&now>=movie_frame_->timestamp&&now-movie_frame_->timestamp<=250)movie->layers[menu_surface::background_slot]=movie_frame_;
				image=std::move(movie);
			}
			const auto input=controller_input::latest();
			if(!render_available||!current.enabled||current.count>menu_surface::maximum_menus||now<current.timestamp||now-current.timestamp>250||
				(!current.frontend&&!current.count)||!image||image->owner.session!=current.session)
			{for(auto& s:surfaces_)release(s);pointer_owner_.cancel_input();publish_backdrop({});native_menu::clear_pointer();return;}
			if(session_!=current.session||reference_!=input.reference_generation||device_!=device||!anchor_.valid)
			{
				for(auto& s:surfaces_)release(s);
				anchor_=menu_surface::anchored(head);session_=current.session;reference_=input.reference_generation;device_=device;
				if(!anchor_.valid){native_menu::clear_pointer();return;}
			}
			const float elapsed=last_time_&&now>=last_time_?float(now-last_time_)*.001f:0;last_time_=now;
			menu_backdrop::publish_for(current, image, anchor_, input.reference_generation, device, now);
			unsigned active=current.count?current.count-1:menu_surface::surface_count;
			bool active_ready{};
			const auto previous_surfaces=surfaces_;
			for(unsigned i=0;i<surfaces_.size();++i)
			{
				auto& s=surfaces_[i];const auto& texture=image->layers[i];
				// A failed SDK update may have crossed IPC before returning an
				// error. Bound quarantine to one attempted image until explicit clear.
				if(s.failed_lease&&!release(s))continue;
				const bool menu=i<current.count;
				const bool background=i==menu_surface::background_slot&&current.frontend;
				// Preserve the native vignette's RGB in front of the spatial blur.
				// Fullscreen pause darkening is excluded separately at capture.
				const bool backdrop=i==menu_surface::backdrop_slot&&current.count;
				const bool cursor=i==menu_surface::cursor_slot&&current.interactive();
				const bool current_target=menu&&i==active;
				const bool target_fresh=texture&&image->owner.revision==current.revision&&now>=texture->timestamp&&now-texture->timestamp<=250;
				if((!menu&&!background&&!backdrop&&!cursor)||!texture||texture->generation!=device||!texture->width||!texture->height||
					(cursor&&!target_fresh))
				{release(s);continue;}
				const std::uint64_t id=menu?current.menus[i].id:std::uint64_t(i+1);
				if(menu&&image->owner.menus[i].id!=id){release(s);continue;}
				if(!s.handle)
				{
					const auto key="h2mod.native.menu."+std::to_string(GetCurrentProcessId())+"."+std::to_string(i);
					auto error=api_->CreateOverlay(key.c_str(),"H2 native menu",&s.handle);
					if(error==VROverlayError_None)error=api_->SetOverlayFlag(s.handle,VROverlayFlags_IsPremultiplied,true);
					if(error==VROverlayError_None)error=api_->SetOverlayFlag(s.handle,VROverlayFlags_NoBackside,true);
					if(error==VROverlayError_None)error=api_->SetOverlayAlpha(s.handle,1);
					if(error!=VROverlayError_None)
					{last_error_=error;++errors_;if(s.handle)(void)api_->DestroyOverlay(s.handle);s={};continue;}
				}
				auto geometry=menu_surface::layout(anchor_,current.frontend,float(texture->width)/texture->height,
					menu?current.count-i-1:0);
				float target=background?menu_surface::theater_depth+menu_surface::maximum_menus*menu_surface::layer_gap:geometry.distance;
				if(background){geometry.radius=target;geometry.width=target*menu_surface::pi*.5f;}
				if(backdrop)
				{
					// Curved overlays are separate tessellated meshes. Coplanar layers
					// of different widths can intersect despite overlay sort order.
					// Follow the displayed active menu during its return animation too.
					const float front=active<surfaces_.size()&&surfaces_[active].lease?surfaces_[active].depth:geometry.distance;
					target=front+menu_surface::dark_layer_gap;
					if(geometry.radius)
					{
						const float radius=geometry.radius+menu_surface::dark_layer_gap;
						geometry.width*=radius/geometry.radius;geometry.radius=radius;
					}
					else geometry.width*=target/front;
					if(current.frontend)geometry.width*=1.25f;
				}
				if(s.id!=id)
				{
					float prior=target;
					for(const auto& previous:previous_surfaces)if(previous.id==id&&previous.lease)prior=previous.depth;
					s.depth=prior;s.id=id;
				}
				s.depth=backdrop?target:menu_surface::approach_depth(s.depth,target,elapsed);
				if(!s.depth)s.depth=target;
				HmdMatrix34_t transform{};
				const auto center=menu_surface::add(anchor_.position,menu_surface::mul(anchor_.forward,s.depth));
				for(unsigned j=0;j<3;++j){transform.m[j][0]=anchor_.right[j];transform.m[j][1]=anchor_.up[j];transform.m[j][2]=-anchor_.forward[j];transform.m[j][3]=center[j];}
				const auto check=[&](EVROverlayError e){if(e!=VROverlayError_None){last_error_=e;++errors_;return false;}return true;};
				bool ok=true;
				const bool opaque=movie_active_&&background;
				if(s.opaque!=opaque)
				{if(check(api_->SetOverlayFlag(s.handle,VROverlayFlags_IgnoreTextureAlpha,opaque)))s.opaque=opaque;else ok=false;}
				if(std::memcmp(&s.transform,&transform,sizeof(transform)))
				{if(check(api_->SetOverlayTransformAbsolute(s.handle,TrackingUniverseStanding,&transform)))s.transform=transform;else ok=false;}
				if(s.width!=geometry.width)
				{if(check(api_->SetOverlayWidthInMeters(s.handle,geometry.width)))s.width=geometry.width;else ok=false;}
				const float curvature=geometry.radius?geometry.width/(2*menu_surface::pi*geometry.radius):0.f;
				if(s.curvature!=curvature)
				{if(check(api_->SetOverlayCurvature(s.handle,curvature)))s.curvature=curvature;else ok=false;}
				const unsigned order=background?0:cursor?100:backdrop?19+2*active:20+2*i;
				if(s.order!=order){if(check(api_->SetOverlaySortOrder(s.handle,order)))s.order=order;else ok=false;}
				if(s.sequence!=texture->sequence||s.lease!=texture)
				{
					Texture_t source{texture->texture.Get(),TextureType_DirectX,ColorSpace_Gamma};
					// The SDK replaces the overlay's source. Retain the exact immutable
					// publication while attached; failed replacement keeps its old lease.
					if(check(api_->SetOverlayTexture(s.handle,&source))){s.lease=texture;s.sequence=texture->sequence;}
					else {s.failed_lease=texture;ok=false;}
				}
				if(ok&&!s.shown){ok=check(api_->ShowOverlay(s.handle));s.shown=ok;}
				else if(!ok&&s.shown){(void)api_->HideOverlay(s.handle);s.shown=false;}
				if(current_target)active_ready=ok&&target_fresh&&std::abs(s.depth-target)<.001f;
			}
			native_menu::pointer pointer{};pointer.session=current.session;pointer.target=current.revision;pointer.stamp=now;
			pointer.overlay_errors=errors_;pointer.overlay_error=last_error_;
			pointer.ready=active_ready&&current.interactive()&&input.focused;
			pointer_owner_.update(input,pointer.ready,controller_input::clock::now());
			const auto hand=pointer_owner_.hand();pointer.hand=hand;
			if(pointer.ready)
			{
				std::array<VROverlayIntersectionResults_t,2> hits{};std::array<bool,2> valid{};
				for(unsigned h=0;h<2;++h)
				{
					const auto& pose=input.runtime_aim[h];if(!pose.valid)continue;
					VROverlayIntersectionParams_t ray{};ray.eOrigin=TrackingUniverseStanding;
					for(unsigned j=0;j<3;++j){ray.vSource.v[j]=pose.tracking.position_meters[j];ray.vDirection.v[j]=-pose.tracking.orientation[j][2];}
					valid[h]=api_->ComputeOverlayIntersection(surfaces_[active].handle,&ray,&hits[h])&&
						std::isfinite(hits[h].fDistance)&&hits[h].fDistance>0&&hits[h].fDistance<20&&
						std::isfinite(hits[h].vUVs.v[0])&&std::isfinite(hits[h].vUVs.v[1])&&
						hits[h].vUVs.v[0]>=0&&hits[h].vUVs.v[0]<=1&&hits[h].vUVs.v[1]>=0&&hits[h].vUVs.v[1]<=1;
				}
				pointer.hit=valid[hand];
				if(pointer.hit)
				{
					pointer.u=hits[hand].vUVs.v[0];pointer.v=1-hits[hand].vUVs.v[1];
					const auto& canvas=image->layers[active]->canvas;
					if(canvas.valid())pointer.hit=canvas.source_uv(pointer.u,pointer.v);
				}
			}
			native_menu::publish_pointer(pointer);
		}catch(...){++errors_;publish_backdrop({});native_menu::clear_pointer();}
	}
}
