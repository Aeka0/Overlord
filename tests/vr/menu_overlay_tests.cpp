#include <std_include.hpp>
#include "component/vr/menu_overlay.hpp"
#include "component/vr/native_hud_capture.hpp"
#include "openvr_overlay_test_interface.hpp"
#include "native_menu_test_boundary.hpp"
#include "test_support.hpp"
#include <map>

namespace
{
	using namespace vr;
	using tests::require;
	struct overlay final:tests::overlay_api
	{
		struct surface {HmdMatrix34_t pose{};Texture_t texture{};bool visible{},premultiplied{},opaque{};float curvature{},width{};};
		std::map<VROverlayHandle_t,surface> surfaces;
		bool hit{true},reject_texture{},reject_cleanup{},miss_right{};
		float hit_u{.25f};
		VROverlayHandle_t next{1};
		EVROverlayError CreateOverlay(const char*,const char*,VROverlayHandle_t* handle) override {*handle=next++;surfaces[*handle]={};return VROverlayError_None;}
		EVROverlayError DestroyOverlay(VROverlayHandle_t h) override {if(reject_cleanup)return VROverlayError_RequestFailed;surfaces.erase(h);return VROverlayError_None;}
		EVROverlayError HideOverlay(VROverlayHandle_t h) override {surfaces.at(h).visible=false;return VROverlayError_None;}
		EVROverlayError ShowOverlay(VROverlayHandle_t h) override {surfaces.at(h).visible=true;return VROverlayError_None;}
		EVROverlayError ClearOverlayTexture(VROverlayHandle_t h) override {if(reject_cleanup)return VROverlayError_RequestFailed;surfaces.at(h).texture={};return VROverlayError_None;}
		EVROverlayError SetOverlayTexture(VROverlayHandle_t h,const Texture_t* texture) override
		{if(reject_texture)return VROverlayError_InvalidTexture;surfaces.at(h).texture=*texture;return VROverlayError_None;}
		EVROverlayError SetOverlayFlag(VROverlayHandle_t h,VROverlayFlags flag,bool enabled) override
		{if(flag==VROverlayFlags_IsPremultiplied)surfaces.at(h).premultiplied=enabled;
			if(flag==VROverlayFlags_IgnoreTextureAlpha)surfaces.at(h).opaque=enabled;return VROverlayError_None;}
		EVROverlayError SetOverlayTransformAbsolute(VROverlayHandle_t h,ETrackingUniverseOrigin origin,const HmdMatrix34_t* pose) override
		{require(origin==TrackingUniverseStanding,"mixed tracking spaces");surfaces.at(h).pose=*pose;return VROverlayError_None;}
		EVROverlayError SetOverlayWidthInMeters(VROverlayHandle_t h,float width) override
		{require(std::isfinite(width)&&width>0,"invalid physical width");surfaces.at(h).width=width;return VROverlayError_None;}
		EVROverlayError SetOverlayCurvature(VROverlayHandle_t h,float c) override {surfaces.at(h).curvature=c;return VROverlayError_None;}
		EVROverlayError SetOverlaySortOrder(VROverlayHandle_t,uint32_t) override {return VROverlayError_None;}
		EVROverlayError SetOverlayAlpha(VROverlayHandle_t,float) override {return VROverlayError_None;}
		bool ComputeOverlayIntersection(VROverlayHandle_t h,const VROverlayIntersectionParams_t* ray,VROverlayIntersectionResults_t* result) override
		{require(surfaces.at(h).visible&&ray->eOrigin==TrackingUniverseStanding,"intersected invisible/wrong-space surface");
			*result={};result->fDistance=2;result->vUVs={{hit_u,.75f}};return hit&&!(miss_right&&ray->vSource.v[0]>0);}
	};
	void run()
	{
		{
			std::array<std::shared_ptr<native_hud_capture::frame>,3> slots;
			for(auto& f:slots){f=std::make_shared<native_hud_capture::frame>();f->subtractive=std::make_shared<native_hud_capture::frame>();}
			auto published=slots[1];std::weak_ptr<const native_hud_capture::frame> retired=slots[0]->subtractive;
			native_hud_capture::release_retired_subtractive(slots);
			require(retired.expired()&&!slots[0]->subtractive&&published->subtractive,"retired backing leaked or immutable published backing changed");
			published.reset();native_hud_capture::release_retired_subtractive(slots);
			require(!slots[1]->subtractive,"retired companion pool cannot recycle");
		}
		Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
		require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)),"WARP creation");
		auto texture=[&](std::uint64_t time,std::uint64_t sequence,unsigned width=64,unsigned height=32)
		{
			auto f=std::make_shared<native_hud_capture::frame>();f->generation=1;f->timestamp=time;f->sequence=sequence;f->width=width;f->height=height;
			D3D11_TEXTURE2D_DESC d{width,height,1,1,DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,0,0};
			require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&f->texture)),"UI texture creation");return f;
		};
		head_pose_bridge::tracking_pose head{{0,1.6f,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
		controller_input::frame input;input.sequence=1;input.focused=true;input.reference_generation=1;
		input.sampled_at=controller_input::clock::now();
		for(unsigned h=0;h<2;++h)
		{input.aim[h].valid=input.grip[h].valid=true;input.aim[h].tracking=input.grip[h].tracking=head;
			input.aim[h].tracking.position_meters[0]=h?.2f:-.2f;input.trigger[h].active=true;input.trigger[h].generation=1;}
		controller_input::publish(input);
		auto& state=tests::menu_state;state={};state.enabled=true;state.session=state.revision=1;state.count=1;state.menus[0].id=11;
		auto image=std::make_shared<native_menu::images>();image->layers[0]=texture(1000,1);
		image->layers[menu_surface::background_slot]=texture(1000,1);
		image->layers[menu_surface::backdrop_slot]=texture(1000,1);
		auto publish=[&](std::uint64_t time){state.timestamp=time;image->owner=state;tests::menu_images=std::make_shared<native_menu::images>(*image);};
		overlay api;menu_overlay::presenter presenter;presenter.initialize(&api);
		publish(1000);presenter.update(head,1,true,1000);
		require(tests::menu_pointer.ready&&tests::menu_pointer.hit,"initial menu not interactive");
		require(api.surfaces.size()==2&&api.surfaces.at(2).visible,"in-game vignette missing or theater movie background leaked");
		require(std::abs(api.surfaces.at(2).pose.m[2][3]-api.surfaces.at(1).pose.m[2][3]+menu_surface::dark_layer_gap)<.001f,
			"dark layer overlaps active menu depth");
		require(menu_overlay::latest_backdrop().canvas==image->layers[0],"spatial blur geometry depends on vignette rather than active menu");
		require(tests::menu_pointer.hand==1,"initial pointer is not right-handed");
		api.miss_right=true;presenter.update(head,1,true,1000);
		require(!tests::menu_pointer.hit&&tests::menu_pointer.hand==1,"right edge miss transferred pointer to left hand");
		input.trigger[0].down=true;++input.trigger[0].presses;++input.sequence;input.sampled_at=controller_input::clock::now();controller_input::publish(input);
		presenter.update(head,1,true,1000);require(tests::menu_pointer.hand==0&&tests::menu_pointer.hit,"left trigger failed to acquire pointer");
		api.miss_right=false;input.trigger[0].down=false;++input.sequence;input.sampled_at=controller_input::clock::now();controller_input::publish(input);presenter.update(head,1,true,1000);
		input.trigger[1].down=true;++input.trigger[1].presses;++input.sequence;input.sampled_at=controller_input::clock::now();controller_input::publish(input);presenter.update(head,1,true,1000);
		require(tests::menu_pointer.hand==1,"right trigger failed to reacquire pointer");input.trigger[1].down=false;
		image->layers[menu_surface::background_slot].reset();image->layers[menu_surface::backdrop_slot].reset();
		require(std::abs(tests::menu_pointer.u-.25f)<.001f&&std::abs(tests::menu_pointer.v-.25f)<.001f,"overlay UV origin conversion");
		const auto original_ink=image->layers[0];auto mapped_ink=texture(1000,99,32,18);mapped_ink->canvas=ui_canvas::fit(16,16,32,18);
		image->layers[0]=mapped_ink;publish(1000);presenter.update(head,1,true,1000);
		require(menu_overlay::latest_backdrop().canvas==mapped_ink,"missing vignette disabled the menu rectangle blur");
		const auto square_blur=menu_overlay::latest_backdrop().placement;
		require(std::abs(square_blur.width-.50625f)<.0001f&&std::abs(square_blur.height-square_blur.width)<.0001f,
			"blur included transparent 16:9 side padding");
		require(tests::menu_pointer.hit&&std::abs(tests::menu_pointer.u-1.f/18)<.001f&&std::abs(tests::menu_pointer.v-.25f)<.001f,"canvas hit did not map back to native UI");
		api.hit_u=.1f;presenter.update(head,1,true,1000);
		require(!tests::menu_pointer.hit&&tests::menu_pointer.hand==1,"transparent canvas padding accepted a hit or transferred hands");
		auto wide_ink=texture(1000,100,32,18);wide_ink->canvas=ui_canvas::fit(32,9,32,18);
		image->layers[0]=wide_ink;publish(1000);presenter.update(head,1,true,1000);
		const auto wide_blur=menu_overlay::latest_backdrop().placement;
		require(std::abs(wide_blur.width-.9f)<.0001f&&std::abs(wide_blur.height-.253125f)<.0001f,
			"blur included transparent 16:9 top/bottom padding");
		api.hit_u=.25f;image->layers[0]=original_ink;publish(1000);presenter.update(head,1,true,1000);
		require(api.surfaces.at(1).premultiplied&&api.surfaces.at(1).texture.eColorSpace==ColorSpace_Gamma,"native alpha/color metadata lost");
		require(std::abs(api.surfaces.at(1).pose.m[2][3]+.5f)<.001f,"active depth changed");
		input.focused=false;++input.sequence;input.sampled_at=controller_input::clock::now();controller_input::publish(input);
		publish(1001);presenter.update(head,1,true,1001);
		require(api.surfaces.at(1).visible&&!tests::menu_pointer.ready,"input focus incorrectly hid the menu or allowed input");
		input.focused=true;++input.sequence;input.sampled_at=controller_input::clock::now();controller_input::publish(input);
		publish(1002);presenter.update(head,1,true,1002);
		require(api.surfaces.at(1).visible&&tests::menu_pointer.ready,"input focus recovery lost the menu");
		state.count=2;++state.revision;state.menus[1].id=22;image->layers[1]=texture(1020,2);publish(1020);
		presenter.update(head,1,true,1020);
		require(api.surfaces.at(1).pose.m[2][3]<-.5f&&std::abs(api.surfaces.at(3).pose.m[2][3]+.5f)<.001f,"child moved toward head instead of parent receding");
		publish(1150);presenter.update(head,1,true,1150);publish(1170);presenter.update(head,1,true,1170);
		require(std::abs(api.surfaces.at(1).pose.m[2][3]+.75f)<.001f,"parent did not settle behind child");
		api.hit=false;publish(1180);presenter.update(head,1,true,1180);
		require(!tests::menu_pointer.hit,"miss clicked through to parent");api.hit=true;
		state.count=1;++state.revision;state.timestamp=1190;presenter.update(head,1,true,1190);
		require(!tests::menu_pointer.ready&&api.surfaces.at(1).visible,"restoring parent lost its noninteractive visual history");
		image->layers[0]=texture(1200,3);image->layers[menu_surface::backdrop_slot]=texture(1200,104);
		publish(1200);presenter.update(head,1,true,1200);
		require(!tests::menu_pointer.ready,"moving parent became clickable before return");
		require(std::abs(api.surfaces.at(2).pose.m[2][3]-api.surfaces.at(1).pose.m[2][3]+menu_surface::dark_layer_gap)<.001f,
			"dark layer crossed the returning menu during animation");
		publish(1350);presenter.update(head,1,true,1350);
		publish(1360);presenter.update(head,1,true,1360);
		require(tests::menu_pointer.ready&&std::abs(api.surfaces.at(1).pose.m[2][3]+.5f)<.001f,"parent did not regain active depth/input");
		// A full-page replacement hides old ink immediately, including the frame
		// before the new page's capture is published. It starts at active depth.
		state.menus[0].id=33;++state.revision;state.timestamp=1360;
		presenter.update(head,1,true,1360);
		require(!api.surfaces.at(1).visible&&!tests::menu_pointer.ready,"old page survived replacement before fresh capture");
		image->layers[0]=texture(1360,103);publish(1360);presenter.update(head,1,true,1360);
		require(api.surfaces.at(1).visible&&tests::menu_pointer.ready&&std::abs(api.surfaces.at(1).pose.m[2][3]+.5f)<.001f,
			"replacement page inherited a receding ancestor depth");
		// Paused recenter need not advance the gameplay reference generation.
		++state.session;head.position_meters[0]=.3f;publish(1361);presenter.update(head,1,true,1361);
		require(std::abs(api.surfaces.at(1).pose.m[0][3]-.3f)<.001f&&tests::menu_pointer.ready,"paused recenter kept old menu anchor");
		presenter.update(head,1,false,1370);require(!tests::menu_pointer.ready&&!api.surfaces.at(1).visible,"focus loss retained input");
		api.reject_texture=true;image->layers[0]=texture(1380,4);publish(1380);presenter.update(head,1,true,1380);
		require(!tests::menu_pointer.ready&&presenter.errors()>0,"rejected GPU publication stayed interactive");
		std::weak_ptr<const native_hud_capture::frame> failed=image->layers[0];image->layers[0].reset();tests::menu_images.reset();
		require(!failed.expired(),"failed SDK update lost its quarantined source");api.reject_texture=false;
		state.frontend=true;++state.session;image->layers[0]=texture(1400,5);publish(1400);presenter.update(head,1,true,1400);
		require(failed.expired(),"acknowledged clear did not retire quarantined source");
		require(api.surfaces.at(1).curvature>0&&std::abs(api.surfaces.at(1).pose.m[2][3]+3)<.001f,"frontend did not select curved theater distance");
		require(!menu_overlay::latest_backdrop().canvas,"in-game blur mask survived transition to frontend");
		image->layers[menu_surface::background_slot]=texture(1400,101);
		image->layers[menu_surface::backdrop_slot]=texture(1400,102);publish(1400);presenter.update(head,1,true,1400);
		for(const auto& [handle,s]:api.surfaces)
		{
			if(s.texture.handle==image->layers[menu_surface::backdrop_slot]->texture.Get())
				require(std::abs(s.width-(menu_surface::theater_depth+menu_surface::dark_layer_gap)*menu_surface::pi*.5f*1.25f)<.001f&&
					std::abs(s.curvature-.3125f)<.001f&&std::abs(s.pose.m[2][3]+3.05f)<.001f,
					"frontend dark mask lost coverage or overlaps foreground curvature");
			if(s.texture.handle==image->layers[menu_surface::background_slot]->texture.Get())
				require(std::abs(s.width-4.f*menu_surface::pi*.5f)<.001f&&std::abs(s.curvature-.25f)<.001f,
					"expanding the dark mask changed movie geometry");
		}
		image->layers[menu_surface::background_slot].reset();image->layers[menu_surface::backdrop_slot].reset();
		presenter.update(head,2,true,1410);require(!tests::menu_pointer.ready,"old-device texture accepted");
		presenter.shutdown();require(api.surfaces.empty(),"overlay handles leaked");tests::menu_images.reset();state={};
		presenter.runtime_detached();presenter.initialize(&api);
		state.enabled=true;state.session=2;state.revision=1;state.count=1;state.menus[0].id=33;
		image->layers={};image->layers[0]=texture(1500,6);publish(1500);
		presenter.update(head,1,true,1500);
		std::weak_ptr<const native_hud_capture::frame> retained=image->layers[0];
		image->layers[0].reset();tests::menu_images.reset();api.reject_cleanup=true;
		presenter.shutdown();require(!retained.expired(),"failed overlay cleanup released compositor-owned texture");
		presenter.runtime_detached();require(retained.expired(),"runtime detach did not release retained texture");
		api.surfaces.clear();state={};
		// Explicit 2D movie input works without a live LUI tree or alpha in the
		// native swap-chain image. Source is copied, never retained as a backbuffer.
		api.reject_cleanup=false;presenter.initialize(&api);auto source=texture(GetTickCount64(),99);
		require(presenter.capture_movie(context.Get(),source->texture.Get(),1,true),"movie GPU copy failed");
		presenter.update(head,1,true);
		require(api.surfaces.size()==1&&api.surfaces.begin()->second.visible&&api.surfaces.begin()->second.opaque&&
			api.surfaces.begin()->second.curvature>0&&!tests::menu_pointer.ready,"movie overlay depends on LUI/input or source alpha");
		require(api.surfaces.begin()->second.texture.handle!=source->texture.Get(),"swap-chain source retained instead of copied");
		require(presenter.capture_movie(nullptr,nullptr,1,false),"movie exit failed");presenter.update(head,1,true);
		require(!api.surfaces.begin()->second.visible,"movie image survived playback exit");presenter.shutdown();
	}
}
int main()
{
	try{run();std::cout<<"vr-menu-overlay-tests: PASS\n";return 0;}
	catch(const std::exception& e){std::cerr<<"vr-menu-overlay-tests: FAIL: "<<e.what()<<'\n';return 1;}
}
