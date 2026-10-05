#include <std_include.hpp>
#include "component/vr/auxiliary_scene.hpp"
#include "component/vr/engine_stereo_dynamic_upload.hpp"
#include "component/vr/gameplay/optic_geometry.hpp"
#include "component/vr/spatial_panel_renderer.hpp"
#include "component/vr/screen_scope_layout.hpp"
#include <limits>
#include "screen_scope_tests.hpp"
#include "thermal_scene_tests.hpp"
#include "optic_tests.hpp"
#include "component/vr/gameplay/javelin_screen.hpp"
#include "component/vr/eye_composition.hpp"

using Microsoft::WRL::ComPtr;
int main()
{
	int failures{};
	auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<"FAIL "<<message<<'\n';}};
	using namespace vr;
	screen_scope_tests(check);
	thermal_scene_tests(check);
	optic_tests(check);
	check(eye_composition::visible_on_weapon_display(eye_composition::layer::weapon_display) &&
		eye_composition::visible_on_weapon_display(eye_composition::layer::narrative) &&
		!eye_composition::visible_on_weapon_display(eye_composition::layer::spatial_hud) &&
		!eye_composition::visible_on_weapon_display(eye_composition::layer::indicators),
		"independent display retains narrative layers without misprojected world HUD");
	const auto screen_rect=[](float x,float y,float w,float h){return spatial_panel::projected_quad{{{2*x-1,1-2*y,.5f,1},{2*(x+w)-1,1-2*y,.5f,1},{2*x-1,1-2*(y+h),.5f,1},{2*(x+w)-1,1-2*(y+h),.5f,1}}};};
	using record=std::array<std::uint8_t,engine_stereo_view::h2_scene_record_size>;
	using matrix=std::array<float,16>;
	record source{},cropped{};
	const matrix projection{2,0,0,0,0,3,0,0,.2f,-.1f,0,1,0,0,.5f,0};
	const matrix inverse{.5f,0,0,0,0,1.f/3,0,0,0,0,0,2,-.1f,1.f/30,1,0};
	std::memcpy(source.data()+0x40,projection.data(),sizeof(projection));
	std::memcpy(source.data()+0x80,projection.data(),sizeof(projection));
	std::memcpy(source.data()+0xC0,inverse.data(),sizeof(inverse));
	const std::array<float,3> origin{17,-39,6};
	std::memcpy(source.data()+0x100,origin.data(),sizeof(origin));
	float history_scale=1;std::memcpy(source.data()+engine_stereo_view::h2_previous_eye_position_constant_offset+12,&history_scale,4);
	source[0x5000]=0xA7;
	{
		auto centered=source;const std::array<float,3> relative{1,2,3},center{17,-38,6};
		std::memcpy(centered.data()+engine_stereo_view::h2_relative_eye_offset_offset,relative.data(),sizeof(relative));
		check(auxiliary_scene::center_record(centered,center),"screen camera removes source-eye IPD at native center");
		std::array<float,3> actual{},rebase{};std::memcpy(actual.data(),centered.data()+0x100,sizeof(actual));
		std::memcpy(rebase.data(),centered.data()+engine_stereo_view::h2_relative_eye_offset_offset,sizeof(rebase));
		check(actual==center && rebase==std::array<float,3>{1,3,3} && centered[0x5000]==0xA7 &&
			std::memcmp(centered.data()+0x80,source.data()+0x80,64)==0,"center camera preserves relative model space, basis and projection");
		const auto before=centered;check(!auxiliary_scene::center_record(centered,{INFINITY,0,0}) && centered==before,"invalid camera center leaves private record untouched");
	}
	const std::array<float,4> window{.2f,.3f,.1f,.15f};
	check(auxiliary_scene::crop_record(source,window,cropped),"covered off-axis crop admitted");
	{
		auto clipped=cropped;
		check(auxiliary_scene::apply_near(clipped,100),"fixed scope retains its native 100-unit near plane");
		matrix p{},inv{};std::memcpy(p.data(),clipped.data()+0x80,sizeof(p));std::memcpy(inv.data(),clipped.data()+0xc0,sizeof(inv));
		for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
		{float value{};for(unsigned k=0;k<4;++k)value+=p[r*4+k]*inv[k*4+c];check(std::abs(value-(r==c?1.f:0.f))<.00001f,"near-plane substitution keeps exact inverse projection");}
		check(p[14]==100 && p[0]==projection[0]/window[2],"native scope clipping leaves magnification untouched");
		const auto before=clipped;check(!auxiliary_scene::apply_near(clipped,-1) && clipped==before,"invalid scope clipping never mutates the scene");
	}
	matrix vp{},ivp{};std::memcpy(vp.data(),cropped.data()+0x80,sizeof(vp));std::memcpy(ivp.data(),cropped.data()+0xC0,sizeof(ivp));
	for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c)
	{
		float value{};for(unsigned k=0;k<4;++k)value+=vp[r*4+k]*ivp[k*4+c];
		check(std::abs(value-(r==c ? 1.f : 0.f))<.00001f,"cropped VP and inverse remain inverses");
	}
	check(cropped[0x5000]==0xA7 && !std::memcmp(cropped.data()+0x100,origin.data(),sizeof(origin)),
		"crop preserves scene payload and exact eye origin");
	check(vp[0]>projection[0]*9.99f && vp[5]>projection[5]*6.66f,"narrow camera increases angular sampling density");
	for(unsigned corner=0;corner<4;++corner)
	{
		const float u=window[0]+float(corner&1)*window[2],v=window[1]+float(corner>>1)*window[3];
		const std::array<float,4> ray{(2*u-1-projection[8])/projection[0],
			(1-2*v-projection[9])/projection[5],1,1};
		std::array<float,4> clip{};
		for(unsigned c=0;c<4;++c) for(unsigned r=0;r<4;++r) clip[c]+=ray[r]*vp[r*4+c];
		check(std::abs(clip[0]/clip[3]*.5f+.5f-float(corner&1))<.00001f &&
			std::abs(.5f-clip[1]/clip[3]*.5f-float(corner>>1))<.00001f,
			"all four source-eye crop rays map to matching auxiliary texture corners");
	}
	check(!auxiliary_scene::crop_record(source,{-.01f,0,.5f,.5f},cropped),"out-of-eye rays rejected");
	check(!auxiliary_scene::crop_record(source,{0,0,0,1},cropped),"degenerate crop rejected");
	check(!auxiliary_scene::crop_record(source,{0,0,std::numeric_limits<float>::quiet_NaN(),1},cropped),"nonfinite crop rejected");
	auxiliary_scene::request camera{0,window,42,100,true};
	auxiliary_scene::history next{},previous{};
	bool reset{};
	check(auxiliary_scene::crop_record(source,window,cropped) &&
		auxiliary_scene::prepare_history(cropped,camera,1,1,previous,next,reset) && reset,
		"first optic uses current matrices");
	previous=next;
	check(auxiliary_scene::prepare_history(cropped,camera,1,2,previous,next,reset) && !reset,"same optic retains its own history");
	camera.generation++;
	check(auxiliary_scene::prepare_history(cropped,camera,1,2,previous,next,reset) && reset,"replaced weapon resets scope history");
	camera=previous.camera;camera.eye=1;
	check(auxiliary_scene::prepare_history(cropped,camera,1,2,previous,next,reset) && reset,"eye handover resets scope history");
	camera=previous.camera;
	check(auxiliary_scene::prepare_history(cropped,camera,2,2,previous,next,reset) && reset,"device change resets scope history");
	camera.reference++;
	check(auxiliary_scene::prepare_history(cropped,camera,1,2,previous,next,reset) && reset,"recenter resets scope history");
	camera=previous.camera;camera.revision++;
	check(auxiliary_scene::prepare_history(cropped,camera,1,2,previous,next,reset) && reset,"grip ownership change resets scope history");
	{
		camera=previous.camera;camera.full_thermal=true;
		const auto ordinary=source;
		check(!auxiliary_scene::apply_thermal(source,camera) && source==ordinary,"scope cannot manufacture missing native thermal activation");
		source[0x204]=0x81;const auto native=source;
		check(auxiliary_scene::crop_record(source,window,cropped) && auxiliary_scene::apply_thermal(cropped,camera) &&
			cropped[0x204]==0x83 && source==native,"only the private scope selects native full thermal; both world eyes keep native stencil flags");
		check(auxiliary_scene::prepare_history(cropped,camera,1,2,previous,next,reset) && reset,"thermal transition invalidates the independent history");
		const engine_stereo_bridge::eye_projection eye{-.8f,1.2f,-1.f,1.f};
		std::array<float,4> crop{};
		check(screen_scope::window(eye,.2f,.1f,crop) && std::abs(crop[0]+crop[2]/2-.4f)<.00001f,
			"native scope zoom stays on the optical axis of an asymmetric eye");
		check(!screen_scope::window(eye,INFINITY,.1f,crop),"invalid scope extents fail closed");
	}
	{
		using namespace engine_stereo_dynamic_upload;
		cycle c;c.data=123;c.thread=4;
		check(c.boundary(0,123,4)==action::defer && c.finish_eye(0),"left defers native arena advancement");
		check(c.auxiliary_boundary(123,4)==action::defer && c.finish_auxiliary(),"scope consumes the same unmapped allocation");
		check(c.boundary(1,123,4)==action::advance,"only final right eye advances native upload");
		c.returned();check(c.finish_eye(1) && !c.take_deferred(4),"completed three-view cycle never advances twice");
		cycle bad;bad.data=123;bad.thread=4;
		check(bad.auxiliary_boundary(123,4)==action::reject,"scope cannot consume before shared producer");
		cycle duplicate;duplicate.data=123;duplicate.thread=4;(void)duplicate.boundary(0,123,4);
		(void)duplicate.auxiliary_boundary(123,4);
		check(duplicate.auxiliary_boundary(123,4)==action::reject,"duplicate auxiliary consumer rejected");
		check(duplicate.take_deferred(4),"cancelled scope still drains native upload");
	}
	{
		using namespace gameplay::weapons::optics;
		view lens;lens.active=true;lens.axis={{{1,0,0},{0,1,0},{0,0,1}}};lens.radius=.8f;lens.magnification=6;
		const spatial_panel::matrix main_vp{0,0,0,1,-1,0,0,0,0,1,0,0,0,0,1,0};
		auto projected=project(lens,{8,0,0},{0,0,0},main_vp,40,true);
		spatial_panel::vec4 crop{};
		check(projected.valid && sampling_window(projected,crop),"physical lens yields covered scope camera");
		check(std::abs(crop[2]-.1f/6*1.02f)<.00001f,"scope uses lens angle divided by magnification");
		check(projected.corners[0][2]==1 && projected.corners[0][3]==8,"physical lens preserves native reverse-Z depth");
		auto other=project(lens,{8,0,0},{0,4,0},main_vp,40,true);
		check(!sampling_window(other,crop),"outside pupil never produces an extra scene");
		lens.magnification=1;
		check(sampling_window(project(lens,{8,0,0},{0,0,0},main_vp,40,true),crop),
			"1x independent rendering remains visible for alignment checks");
	}

	ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
	if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))
		{std::cerr<<"FAIL WARP device\n";return 1;}
	constexpr unsigned size=64;
	D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=size;td.MipLevels=td.ArraySize=1;
	td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
	ComPtr<ID3D11Texture2D> image,output;
	ComPtr<ID3D11ShaderResourceView> image_view,reticle;
	ComPtr<ID3D11RenderTargetView> image_target,target;
	check(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&image)) && SUCCEEDED(device->CreateTexture2D(&td,nullptr,&output)) &&
		SUCCEEDED(device->CreateShaderResourceView(image.Get(),nullptr,&image_view)) &&
		SUCCEEDED(device->CreateRenderTargetView(image.Get(),nullptr,&image_target)) &&
		SUCCEEDED(device->CreateRenderTargetView(output.Get(),nullptr,&target)),"WARP color resources");
	std::array<std::uint32_t,size*size> transparent{};
	D3D11_SUBRESOURCE_DATA pixels{transparent.data(),size*4,0};ComPtr<ID3D11Texture2D> reticle_texture;
	check(SUCCEEDED(device->CreateTexture2D(&td,&pixels,&reticle_texture)) &&
		SUCCEEDED(device->CreateShaderResourceView(reticle_texture.Get(),nullptr,&reticle)),"transparent reticle");
	auto dd=td;dd.Format=DXGI_FORMAT_D32_FLOAT;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
	ComPtr<ID3D11Texture2D> depth;ComPtr<ID3D11DepthStencilView> dsv;
	check(SUCCEEDED(device->CreateTexture2D(&dd,nullptr,&depth)) &&
		SUCCEEDED(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv)),"WARP depth resources");
	if(failures) return 1;
	const float green[]{0,1,0,1},blue[]{0,0,1,1},red[]{1,0,0,1};
	context->ClearRenderTargetView(image_target.Get(),green);
	context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.25f,0);
	auxiliary_scene::image_copy saved;
	check(saved.capture(context.Get(),image.Get(),dsv.Get(),1),"save color and current-eye depth");
	context->ClearRenderTargetView(image_target.Get(),red);
	context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.9f,0);
	td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> staging;check(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&staging)),"WARP readback");
	auto pixel=[&](unsigned x,unsigned y)
	{
		context->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE read{};
		if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&read))) {++failures;return std::uint32_t{};}
		std::uint32_t value{};std::memcpy(&value,static_cast<const char*>(read.pData)+read.RowPitch*y+x*4,4);
		context->Unmap(staging.Get(),0);return value;
	};
	spatial_panel::projected_quad corners{{{-.5f,.5f,.5f,1},{.5f,.5f,.5f,1},{-.5f,-.5f,.5f,1},{.5f,-.5f,.5f,1}}};
	spatial_panel::renderer renderer;
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.5f,.5f,4,1},
		saved.depth_view.Get(),{.4f,.4f,.2f,.2f}),"compose separately rendered view with saved eye depth");
	check((pixel(32,32)&0xFFFFFF)==0x00FF00,"lens reads saved independent green image after native scratch became red");
	check((pixel(2,2)&0xFFFFFF)==0xFF0000,"outside lens remains blue");
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.5f,.5f,4,1},
		dsv.Get(),{.4f,.4f,.2f,.2f}),"near occluder draw accepted");
	check((pixel(32,32)&0xFFFFFF)==0xFF0000,"closer reverse-Z geometry occludes lens");
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.5f,.5f,4,0},
		saved.depth_view.Get()),"inactive aiming eye draws scope shadow");
	check((pixel(32,32)&0xFFFFFF)==0,"non-aiming eye never sees another eye image");
	// WA2000's art has an opaque annulus followed by transparent texels. Those
	// outer texels must not reveal the sampled scene again beyond the black ring.
	std::array<std::uint32_t,size*size> ring{};
	for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)
	{
		const float radius=std::hypot((x+.5f)/size*2-1,(y+.5f)/size*2-1);
		if(radius>.78f && radius<.94f)ring[y*size+x]=0xFF000000;
	}
	context->UpdateSubresource(reticle_texture.Get(),0,nullptr,ring.data(),size*4,0);
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.5f,.5f,4,1}),
		"transparent-rim regression fixture renders with the default pupil");
	check((pixel(47,31)&0x00FF00)!=0,"fixture reproduces scene leakage beyond a reticle's black ring");
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.5f,.5f,4,1},nullptr,{0,0,1,1},.94f),
		"authored pupil mask renders without changing the physical lens");
	check((pixel(47,31)&0xFFFFFF)==0 && (pixel(32,32)&0xFFFFFF)==0x00FF00 && (pixel(2,2)&0xFFFFFF)==0xFF0000,
		"WA2000 outer rim is black while the clear center and outside world remain unchanged");
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.55f,.5f,4,1},nullptr,{0,0,1,1},.94f) &&
		(pixel(18,28)&0xFFFFFF)==0 && (pixel(33,31)&0xFFFFFF)==0x00FF00,
		"WA2000 rim masking follows the shifted reticle pupil under lateral eye movement");
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.5f,.5f,4,1},nullptr,{0,0,1,1},1,2) &&
		(pixel(47,31)&0x00FF00)!=0,"wide thermal eye box reproduces transparent-tail leakage without artwork calibration");
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.5f,.5f,4,1},nullptr,{0,0,1,1},.94f,2) &&
		(pixel(47,31)&0xFFFFFF)==0 && (pixel(32,32)&0xFFFFFF)==0x00FF00 && (pixel(2,2)&0xFFFFFF)==0xFF0000,
		"thermal artwork rim stays opaque with doubled eye tolerance, clear center and unchanged outside world");
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.55f,.5f,4,1},nullptr,{0,0,1,1},.94f,2) &&
		(pixel(18,28)&0xFFFFFF)==0 && (pixel(33,31)&0xFFFFFF)==0x00FF00,
		"thermal opaque rim follows the aiming-axis UV when the eye moves laterally");
	check(!renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.5f,.5f,4,1},nullptr,{0,0,1,1},0),
		"invalid pupil mask cannot reach the shader");
	context->UpdateSubresource(reticle_texture.Get(),0,nullptr,transparent.data(),size*4,0);
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{1.25f,.5f,4,1}),
		"ordinary oblique lens fixture renders");
	check((pixel(32,32)&0xFFFFFF)==0,"ordinary scope retains its strict exit pupil");
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{1.25f,.5f,4,1},nullptr,{0,0,1,1},1,2),
		"thermal oblique lens uses its wider pupil");
	check((pixel(32,32)&0xFFFFFF)==0x00FF00 && (pixel(2,2)&0xFFFFFF)==0xFF0000,
		"thermal center stays visible past the old eye box without painting outside the physical lens");
	check(!renderer.draw_optic(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),corners,size,size,{.5f,.5f,4,1},nullptr,{0,0,1,1},1,INFINITY),
		"invalid eye-box scale cannot reach the shader");
	context->ClearRenderTargetView(target.Get(),blue);
	check(renderer.draw_screen_scope(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),screen_rect(.2f,.25f,.6f,.5f),size,size),
		"WARP fixed scope composes the native encoded image");
	check((pixel(32,32)&0xFFFFFF)==0x00FF00 && (pixel(2,2)&0xFFFFFF)==0,
		"scope center uses its thermal source and the whole exterior is opaque black");
	ring.fill(0xFF0000FF);context->UpdateSubresource(reticle_texture.Get(),0,nullptr,ring.data(),size*4,0);
	check(renderer.draw_screen_scope(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),screen_rect(.2f,.25f,.6f,.5f),size,size) &&
		(pixel(32,32)&0xFFFFFF)==0x0000FF,"native opaque scope ink masks the thermal scene");
	check(renderer.draw_screen_scope(context.Get(),saved.view.Get(),nullptr,target.Get(),screen_rect(.2f,.25f,.6f,.5f),size,size) &&
		(pixel(32,32)&0xFFFFFF)==0x00FF00 && (pixel(2,2)&0xFFFFFF)==0,
		"missing native reload HUD preserves live scope scene instead of blacking the canvas");
	check(!renderer.draw_screen_scope(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),screen_rect(0,0,0,1),size,size),"degenerate scope cannot reach the shader");
	{
		ComPtr<ID3D11Texture2D> shadow,flash;ComPtr<ID3D11ShaderResourceView> shadow_view,flash_view;
		auto desc=td;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
		ring.fill(0x80404040);D3D11_SUBRESOURCE_DATA shade_data{ring.data(),size*4,0};
		check(SUCCEEDED(device->CreateTexture2D(&desc,&shade_data,&shadow)) && SUCCEEDED(device->CreateShaderResourceView(shadow.Get(),nullptr,&shadow_view)),"captured colored subtractive shadow fixture");
		ring.fill(0x80000080);D3D11_SUBRESOURCE_DATA flash_data{ring.data(),size*4,0};
		check(SUCCEEDED(device->CreateTexture2D(&desc,&flash_data,&flash)) && SUCCEEDED(device->CreateShaderResourceView(flash.Get(),nullptr,&flash_view)),"native pre-shadow flash fixture");
		context->UpdateSubresource(reticle_texture.Get(),0,nullptr,transparent.data(),size*4,0);
		check(renderer.draw_screen_scope(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),screen_rect(0,0,1,1),size,size,shadow_view.Get()),"colored reverse subtraction is composed rather than rejecting the entire scope");
		const auto shaded=pixel(32,32);check(((shaded>>8)&255)>=12 && ((shaded>>8)&255)<=14 && (shaded&255)==0,"scope applies native D*(1-A)-S in encoded space before linear conversion");
		check(renderer.draw_screen_scope(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),screen_rect(0,0,1,1),size,size,shadow_view.Get(),flash_view.Get()) &&
			(pixel(32,32)&0xFFFFFF)==0,"flash remains below the subtractive shadow in native draw order");
		ring.fill(0xFFFFFFFF);context->UpdateSubresource(reticle_texture.Get(),0,nullptr,ring.data(),size*4,0);
		check(renderer.draw_screen_scope(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),screen_rect(0,0,1,1),size,size,shadow_view.Get(),flash_view.Get()) &&
			pixel(32,32)==0xFFFFFFFF,"reticle drawn after subtraction is not incorrectly darkened by a flattened signed layer");
		context->UpdateSubresource(reticle_texture.Get(),0,nullptr,transparent.data(),size*4,0);
		ring.fill(0x00FFFFFF);context->UpdateSubresource(flash.Get(),0,nullptr,ring.data(),size*4,0);
		check(renderer.draw_screen_scope(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),screen_rect(0,0,1,1),size,size,shadow_view.Get(),flash_view.Get()),"native additive flash layer composes");
		const auto flash_then_shadow=pixel(32,32);
		check((flash_then_shadow&255)>=12 && (flash_then_shadow&255)<=14 && ((flash_then_shadow>>8)&255)==(flash_then_shadow&255),
			"additive flash saturates before native subtraction, rather than adding unbounded light after the shadow");
	}
	{
		const std::array<spatial_panel::vec3,3> identity{{{1,0,0},{0,1,0},{0,0,1}}};
		const auto yaw=[](float degrees){const float r=degrees*.017453292519943295f,c=std::cos(r),s=std::sin(r);return std::array<spatial_panel::vec3,3>{{{c,s,0},{-s,c,0},{0,0,1}}};};
		const engine_stereo_bridge::eye_projection eye{-1,1,-1,1};
		screen_scope::anchored_plane plane;spatial_panel::projected_quad canvas;
		plane.update(1,1,{},identity,.1f);plane.update(1,1,{},yaw(20),.1f);
		check(plane.project(eye,0,1920,1080,canvas),"oblique screen fixture");
		for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)ring[y*size+x]=x<size/2?0xFF00FF00:0xFF0000FF;
		context->UpdateSubresource(image.Get(),0,nullptr,ring.data(),size*4,0);
		context->UpdateSubresource(reticle_texture.Get(),0,nullptr,transparent.data(),size*4,0);
		check(renderer.draw_screen_scope(context.Get(),image_view.Get(),reticle.Get(),target.Get(),canvas,size,size),"WARP perspective screen after head yaw");
		for(float u:{.25f,.75f})
		{
			spatial_panel::vec4 clip{};for(unsigned i=0;i<4;++i)clip[i]=((1-u)*(canvas[0][i]+canvas[2][i])+u*(canvas[1][i]+canvas[3][i]))*.5f;
			const auto x=unsigned((clip[0]/clip[3]*.5f+.5f)*size),y=unsigned((.5f-clip[1]/clip[3]*.5f)*size);
			check(x<size && y<size && (pixel(x,y)&0xFFFFFF)==(u<.5f?0x00FF00u:0x0000FFu),
				"head rotation relocates texture landmarks with the plane without panning the source image");
		}
		plane.update(1,1,{},yaw(65),.1f);plane.project(eye,0,1920,1080,canvas);
		check(renderer.draw_screen_scope(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),canvas,size,size) &&
			(pixel(60,32)&0xFFFFFF)==0x00FF00,"plane crossing the eye is partially clipped without losing its visible edge");
		plane.update(1,1,{},yaw(180),.1f);plane.project(eye,0,1920,1080,canvas);
		check(renderer.draw_screen_scope(context.Get(),saved.view.Get(),reticle.Get(),target.Get(),canvas,size,size) &&
			(pixel(32,32)&0xFFFFFF)==0,"looking behind the mounted screen hides it and never reveals unmasked game scenery");
	}
	std::cout<<"vr-optic-render-tests: "<<(failures ? "FAIL" : "PASS")<<'\n';
	return failures ? 1 : 0;
}
