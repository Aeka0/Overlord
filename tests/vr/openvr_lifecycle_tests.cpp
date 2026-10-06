// Compile the production implementation exactly once in this test target so
// the friend fixture can inject SDK interfaces without a shipping test API.
#include <openvr.h>
namespace vr
{
	unsigned int test_runtime_detach_count{};
	void test_runtime_detach() { ++test_runtime_detach_count; }
}
// Retain injected interfaces through Present-post. Clearing the fake system
// early would leave pose sampling with a null pointer; calling the real SDK
// shutdown would instead interfere with a live headset owned by the user.
#define VR_Shutdown test_runtime_detach
#include "component/vr/openvr_runtime.cpp"
#undef VR_Shutdown
#include "openvr_test_interfaces.hpp"
#include "test_support.hpp"
#include "native_menu_test_boundary.hpp"
#include <thread>

namespace vr::openvr
{
	struct runtime_test_access
	{
		static void attach(runtime_backend& runtime, const d3d11::device_snapshot& graphics,
			IVRSystem* system, IVRCompositor* compositor, const bool armed, const bool probing)
		{
			auto& impl = *runtime.implementation_;
			impl.system_ = system;
			impl.compositor_api_ = compositor;
			impl.direct_graphics_ = graphics;
			impl.auto_initialize_allowed_ = false;
			impl.initialization_device_generation_ = graphics.generation;
			impl.frame_phase_ = armed ? frame_phase::need_pose : frame_phase::idle_unarmed;
			auto& status = impl.status_;
			status.device_generation = graphics.generation;
			status.desired_enabled = status.applied_enabled = status.session_running = true;
			status.requested_scene_mode = scene_mode::engine_stereo;
			status.initialization_attempt_count = 1;
			status.state = runtime_state::running;
			status.native_renderer_ready = status.submission_on_game_device = armed;
			status.graphics_transport = armed ? "h2_device_direct" : "gpu_transport_unarmed";
			status.color_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			status.eyes[0].width = status.eyes[1].width = 256;
			status.eyes[0].height = status.eyes[1].height = 256;
			status.submit_format_probe.active = probing;
			status.submit_format_probe.complete = !probing;
			status.submit_format_probe.selected_format = probing ? 0 : DXGI_FORMAT_R16G16B16A16_FLOAT;
			status.submit_format_probe.state = probing ? "probing" : "lossless_format_selected";
			impl.publish_status_locked();
		}

		static bool submit(runtime_backend& runtime, const std::uint64_t pair)
		{
			auto& impl = *runtime.implementation_;
			tests::require(native_render_session::active().acquire_published_pair(
				pair, impl.prepared_native_pair_), "fixture could not acquire the published pair");
			impl.prepared_native_pair_id_ = pair;
			const auto result = impl.submit_prepared_pair();
			impl.publish_status_locked();
			return result;
		}

		static void detach_system(runtime_backend& runtime)
		{
			// No VR_Init occurred; the fixture must never call the real VR_Shutdown.
			runtime.implementation_->system_ = nullptr;
		}
	};
}

namespace
{
	using namespace vr;
	using tests::require;
	using access = openvr::runtime_test_access;

	class compositor final : public tests::IVRCompositor_stub
	{
	public:
		EVRCompositorError pose_result{VRCompositorError_None};
		std::array<EVRCompositorError, 2> submit_results{};
		unsigned clear_calls{}, wait_calls{}, submit_calls{};
		bool grid{};float fade_alpha{};
		void FadeGrid(float,bool enabled) override {grid=enabled;}
		void FadeToColor(float,float,float,float,float alpha,bool) override {fade_alpha=alpha;}
		EVRCompositorError WaitGetPoses(TrackedDevicePose_t* poses, uint32_t count,
			TrackedDevicePose_t*, uint32_t) override
		{
			++wait_calls;
			require(count > k_unTrackedDeviceIndex_Hmd, "missing HMD pose slot");
			auto& pose = poses[k_unTrackedDeviceIndex_Hmd];
			pose.bDeviceIsConnected = pose.bPoseIsValid = true;
			for (unsigned axis = 0; axis < 3; ++axis) pose.mDeviceToAbsoluteTracking.m[axis][axis] = 1;
			return pose_result;
		}
		EVRCompositorError Submit(EVREye eye, const Texture_t* texture,
			const VRTextureBounds_t*, EVRSubmitFlags) override
		{
			require(texture && texture->handle, "Submit lost its retained texture");
			++submit_calls;
			return submit_results[eye];
		}
		void ClearLastSubmittedFrame() override { ++clear_calls; }
		bool GetFrameTiming(Compositor_FrameTiming*, uint32_t) override { return false; }
	};

	class system_api final : public tests::IVRSystem_stub
	{
	public:
		bool input_available{};
		bool PollNextEvent(VREvent_t*, uint32_t) override { return false; }
		bool IsInputAvailable() override { return input_available; }
		void GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin origin,float,TrackedDevicePose_t* poses,uint32_t count) override
		{
			require(origin==TrackingUniverseStanding&&count==1,"unexpected paused tracking query");
			poses[0]={};poses[0].bDeviceIsConnected=poses[0].bPoseIsValid=true;
			for(unsigned i=0;i<3;++i)poses[0].mDeviceToAbsoluteTracking.m[i][i]=1;
		}
		HmdMatrix34_t GetEyeToHeadTransform(EVREye eye) override
		{
			HmdMatrix34_t result{};
			for (unsigned axis = 0; axis < 3; ++axis) result.m[axis][axis] = 1;
			result.m[0][3] = eye == Eye_Left ? -.032f : .032f;
			return result;
		}
		void GetProjectionRaw(EVREye, float* left, float* right, float* top, float* bottom) override
		{ *left = *top = -1; *right = *bottom = 1; }
	};

	d3d11::device_snapshot create_warp(const std::uint64_t generation)
	{
		d3d11::device_snapshot graphics;
		require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
			nullptr, 0, D3D11_SDK_VERSION, &graphics.device, &graphics.feature_level,
			&graphics.context)), "WARP device creation failed");
		graphics.generation = generation;
		return graphics;
	}

	struct fixture
	{
		system_api system;
		compositor api;
		openvr::runtime_backend runtime;
		d3d11::present_event event{};
		Microsoft::WRL::ComPtr<ID3D11Texture2D> source;

		fixture(const d3d11::device_snapshot& graphics, const bool armed = true,
			const bool probing = false)
		{
			event.graphics = graphics;
			event.frame_index = 100;
			engine_stereo_bridge::reset();
			engine_stereo_bridge::configure_target(true);
			engine_stereo_bridge::set_enabled(true);
			auto& ring = native_render_session::active();
			ring.invalidate();
			if (armed)
			{
				D3D11_TEXTURE2D_DESC desc{256, 256, 1, 1, DXGI_FORMAT_R11G11B10_FLOAT,
					{1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET |
					D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0};
				std::string error;
				require(ring.ensure_copy_ring(graphics, desc, DXGI_FORMAT_R16G16B16A16_FLOAT,
					error), "copy ring creation failed: " + error);
				require(SUCCEEDED(graphics.device->CreateTexture2D(&desc, nullptr, &source)),
					"display source creation failed");
				Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
				require(SUCCEEDED(graphics.device->CreateRenderTargetView(source.Get(), nullptr, &view)),
					"display source view creation failed");
				const float color[]{.25f, .5f, .75f, 1.f};
				graphics.context->ClearRenderTargetView(view.Get(), color);
			}
			access::attach(runtime, graphics, &system, &api, armed, probing);
		}
		~fixture()
		{
			access::detach_system(runtime);
			runtime.shutdown();
			if (!runtime.shutdown_complete()) runtime.on_present_post(event, S_OK);
			engine_stereo_bridge::reset();
		}
		void present()
		{
			++event.frame_index;
			runtime.on_present(event);
			runtime.on_present_post(event, S_OK);
		}
		bool submit()
		{
			const auto pair = runtime.get_status().frame_context_id;
			auto& ring = native_render_session::active();
			require(ring.accepts_pair(pair), "fresh pose did not admit its exact pair");
			for (unsigned eye = 0; eye < 2; ++eye)
				require(ring.copy_eye(pair, eye, source.Get(), 5, event.graphics.context.Get()),
					"fixture eye copy failed");
			return access::submit(runtime, pair);
		}
	};

	void expect_initial_focus_recovery(const d3d11::device_snapshot& graphics, const bool armed)
	{
		fixture f{graphics, armed};
		f.api.pose_result = VRCompositorError_DoNotHaveFocus;
		for (unsigned attempt = 0; attempt < 3; ++attempt)
		{
				f.present();
				const auto status = f.runtime.get_status();
				const auto input=controller_input::latest();
				require(input.source.gate.reason==controller_input::input_reason::compositor_focus_lost &&
					input.source.gate.code==VRCompositorError_DoNotHaveFocus && !input.source.runtime_focus_known,
					"OpenVR compositor focus loss must preserve its cause without inventing an input-focus query");
			require(status.applied_enabled && status.state == runtime_state::recoverable_error &&
				status.cleanup_count == 0 && status.initialization_attempt_count == 1,
				"focus loss before first Submit tore down or restarted OpenVR");
		}
		f.api.pose_result = VRCompositorError_None;
		f.present();
		const auto status = f.runtime.get_status();
		require(status.state == (armed ? runtime_state::running : runtime_state::session_idle) &&
			status.last_compositor_error.empty() && status.last_xr_result == VRCompositorError_None &&
			status.tracking_pose_sample_count == 1 && status.cleanup_count == 0,
			"focus recovery did not resume pose publication in the existing session");
	}

	void expect_frontend_tracking(const d3d11::device_snapshot& graphics)
	{
		fixture f{graphics,true};
		f.system.input_available=false; // Rendering can succeed while controllers/system input are unavailable.
		vr::tests::menu_state={};
		vr::tests::menu_state.enabled=vr::tests::menu_state.frontend=true;
		vr::tests::menu_state.timestamp=GetTickCount64();
		f.present();const auto first=f.runtime.get_status().tracking_pose_sample_count;
		f.present();
		require(f.runtime.get_status().tracking_pose_sample_count==first+1,
			"frontend stopped tracking while waiting for a nonexistent scene");
		require(!native_render_session::active().get_status().accepting_pairs,
			"frontend admitted a stale gameplay eye pair");
		require(f.api.grid&&f.api.fade_alpha==1,"frontend did not enter an explicit dark theater");
		f.api.pose_result=VRCompositorError_DoNotHaveFocus;f.present();
		require(!f.api.grid&&f.api.fade_alpha==0,"focus loss retained the application's compositor fade");
		f.api.pose_result=VRCompositorError_None;f.present();f.present();
		require(f.api.grid,"frontend focus recovery did not restore theater");
		vr::tests::menu_state={};f.present();
		require(f.runtime.get_status().state==runtime_state::running&&
			native_render_session::active().get_status().accepting_pairs,
			"return from frontend did not restore native stereo acquisition");
		require(f.api.grid,"theater exposed old scene before a new pair was submitted");
		require(f.submit()&&!f.api.grid&&f.api.fade_alpha==0,"accepted native pair did not leave theater");
	}

	void expect_loading_movie_tracking(const d3d11::device_snapshot& graphics)
	{
		fixture f{graphics,true};vr::tests::menu_state={}; // LUI VM can be stopped throughout loading.
		f.present(); // Loading starts with a prepared scene that will never render.
		vr::tests::presentation_override=native_menu::presentation{.enabled=true,.frontend=false,.scene=false,.video=true,.fullscreen_video=true};
		const auto cleanup=gsl::finally([]{vr::tests::presentation_override.reset();});
		f.present();f.present();const auto count=f.runtime.get_status().tracking_pose_sample_count;
		f.present();require(f.runtime.get_status().tracking_pose_sample_count==count+1&&f.api.grid&&f.api.fade_alpha==1,
			"loading movie without LUI stopped compositor tracking/presentation");
		require(!native_render_session::active().get_status().accepting_pairs,"loading movie waited for a nonexistent stereo scene");
		vr::tests::presentation_override->scene=true;f.present();
		require(f.api.grid&&!native_render_session::active().get_status().accepting_pairs,"map initialization cut off a still-playing movie");
		vr::tests::presentation_override->video=false;f.present();
		require(f.api.grid&&native_render_session::active().get_status().accepting_pairs,"movie end lost pending handoff to stereo");
		require(f.submit()&&!f.api.grid&&f.api.fade_alpha==0,"fresh stereo did not retire movie theater");
	}
	void expect_pause_page_dim(const d3d11::device_snapshot& graphics)
	{
		fixture f{graphics,true};auto& menu=vr::tests::menu_state;menu={};
		menu.enabled=true;menu.count=1;menu.session=menu.revision=1;menu.scene_dim=140.f/255.f;menu.timestamp=GetTickCount64();
		f.present();f.present();require(!f.api.grid&&std::abs(f.api.fade_alpha-menu.scene_dim)<.001f,"pause did not dim scene independently of UI pixels");
		++menu.revision;menu.count=2;menu.menus[0].element=0;menu.timestamp=GetTickCount64();
		f.present();require(std::abs(f.api.fade_alpha-menu.scene_dim)<.001f,"closing pause page for achievements removed dimming");
		menu.count=0;menu.timestamp=GetTickCount64();f.present();
		require(std::abs(f.api.fade_alpha-menu.scene_dim)<.001f,"temporary empty transition removed pause-session dimming");
		menu.scene_dim=0;f.present();require(f.api.fade_alpha==0,"leaving pause retained scene dimming");menu={};
	}
	void expect_submit_focus_recovery(const d3d11::device_snapshot& graphics,
		const std::array<EVRCompositorError, 2> results, const bool probing)
	{
		fixture f{graphics, true, probing};
		f.present();
		const auto old_pair = f.runtime.get_status().frame_context_id;
		f.api.submit_results = results;
		require(!f.submit(), "focus-interrupted pair was counted as submitted");
		const auto ring_before = native_render_session::active().get_status();
		f.api.pose_result = VRCompositorError_DoNotHaveFocus;
		for (unsigned attempt = 0; attempt < 3; ++attempt)
		{
			f.present();
			require(native_render_session::active().pair_published(old_pair) &&
				native_render_session::active().get_status().pair_releases == ring_before.pair_releases &&
				f.runtime.get_status().cleanup_count == 0 && f.api.submit_calls == 2,
				"failed WaitGetPoses retired or resubmitted compositor-visible textures");
		}
		f.api.pose_result = VRCompositorError_None;
		f.present();
		auto status = f.runtime.get_status();
		require(status.applied_enabled && status.state == runtime_state::running &&
			status.frame_phase == "collecting" && status.cleanup_count == 0 &&
			status.direct_pairs_retired == 1 && status.submit_format_probe.active == probing &&
			status.submit_format_probe.candidate_rejections == 0 &&
			native_render_session::active().get_status().pair_releases == ring_before.pair_releases + 1 &&
			native_render_session::active().get_status().invalidations == ring_before.invalidations &&
			!native_render_session::active().pair_published(old_pair),
			"focus recovery changed format, recreated the ring, or failed to retire exactly once");
		f.api.submit_results = {VRCompositorError_None, VRCompositorError_None};
		require(f.submit(), "fresh stereo pair was not submitted after focus recovery");
		if (probing)
			require(!f.runtime.get_status().submit_format_probe.candidates[0].retired,
				"retried format candidate still claimed its new pair was retired");
		f.present();
		status = f.runtime.get_status();
		require(status.state == runtime_state::running && status.submitted_frames == 1 &&
			status.submit_format_probe.complete && !status.submit_format_probe.active &&
			status.submit_format_probe.selected_format == DXGI_FORMAT_R16G16B16A16_FLOAT,
			"same lossless candidate did not complete after focus recovery");
	}

	void expect_hard_submit_failure(const d3d11::device_snapshot& graphics, const bool focus_left)
	{
		fixture f{graphics};
		f.present();
		f.api.submit_results = focus_left
			? std::array{VRCompositorError_DoNotHaveFocus, VRCompositorError_InvalidTexture}
			: std::array{VRCompositorError_InvalidTexture, VRCompositorError_DoNotHaveFocus};
		require(!f.submit(), "hard Submit error succeeded");
		++f.event.frame_index;
		f.runtime.on_present(f.event);
		access::detach_system(f.runtime);
		f.runtime.on_present_post(f.event, S_OK);
		const auto status = f.runtime.get_status();
		require(status.state == runtime_state::fatal_for_vr && !status.applied_enabled &&
			status.cleanup_count == 1 && status.direct_pairs_retired == 1,
			"focus masked a hard error in the other eye");
	}

	void expect_hard_pose_failure(const d3d11::device_snapshot& graphics)
	{
		fixture f{graphics, false};
		f.api.pose_result = VRCompositorError_IsNotSceneApplication;
		++f.event.frame_index;
		f.runtime.on_present(f.event);
		access::detach_system(f.runtime);
		f.runtime.on_present_post(f.event, S_OK);
		require(f.runtime.get_status().state == runtime_state::fatal_for_vr &&
			!f.runtime.applied_enabled(), "non-focus pose error became recoverable");
	}

	void expect_resize_recovery(const d3d11::device_snapshot& graphics)
	{
		fixture f{graphics};
		f.present();
		require(f.submit(), "resize fixture did not submit its pair");
		const auto retired=native_render_session::active().get_status().pair_releases;
		d3d11::resize_event resize{};
		resize.graphics=graphics;resize.width=1639;resize.height=1351;
		// The request may arrive on the renderer's thread; it must never detach
		// OpenVR or destroy a submitted pair within ResizeBuffers.
		std::thread requester([&] {f.runtime.on_resize_before(resize);f.runtime.on_resize_before(resize);});
		requester.join();
		require(f.runtime.applied_enabled() && f.runtime.get_status().cleanup_count==0 &&
			native_render_session::active().get_status().pair_releases==retired,
			"resize callback released compositor-owned textures before Present");
		// Fail before VR_Init so this WARP test never touches the live headset.
		engine_stereo_bridge::reset();access::detach_system(f.runtime);
		f.present();
		require(f.runtime.get_status().initialization_attempt_count==2 &&
			f.runtime.get_status().cleanup_count==1 && !native_render_session::active().get_status().available,
			"same-device resize did not retire the old ring and attempt automatic recovery");
		f.present();
		require(f.runtime.get_status().initialization_attempt_count==2,"resize burst caused an initialization retry loop");
		resize.graphics.generation++;
		f.runtime.on_resize_before(resize);f.present();
		require(f.runtime.get_status().initialization_attempt_count==2,"foreign device resize retried this device");
		f.runtime.set_desired_enabled(false);
		resize.graphics=graphics;f.runtime.on_resize_before(resize);f.present();
		require(f.runtime.get_status().initialization_attempt_count==2,"resize reenabled disabled VR");
		f.runtime.shutdown();f.runtime.on_resize_before(resize);f.present();
		require(f.runtime.shutdown_complete() && f.runtime.get_status().initialization_attempt_count==2,
			"resize revived a terminally shut down runtime");
	}

	void expect_terminal_shutdown(const d3d11::device_snapshot& graphics, const bool present_pending)
	{
		fixture f{graphics};
		const auto detach_count = test_runtime_detach_count;
		if (present_pending)
		{
			f.present();
			require(f.submit(), "shutdown fixture could not submit its pair");
			++f.event.frame_index;
			f.runtime.on_present(f.event);
		}
		std::thread requester([&] { f.runtime.shutdown(); });
		requester.join();
		if (present_pending)
		{
			require(!f.runtime.shutdown_complete() && f.runtime.get_status().cleanup_count == 0 &&
				test_runtime_detach_count == detach_count,
				"shutdown destroyed resources before the Present owner returned");
			f.runtime.on_present_post(f.event, S_OK);
		}
		require(f.runtime.shutdown_complete() && f.runtime.get_status().cleanup_count == 1 &&
			!native_render_session::active().get_status().available &&
			test_runtime_detach_count == detach_count + 1,
			"shutdown did not retire the runtime and submitted textures");
		f.runtime.shutdown();
		f.runtime.request_reinitialize();
		f.present();
		require(f.runtime.shutdown_complete() && f.runtime.get_status().cleanup_count == 1 &&
			f.runtime.get_status().initialization_attempt_count == 1 &&
			test_runtime_detach_count == detach_count + 1,
			"repeated shutdown or a late callback revived the stopped runtime");
	}

	void expect_device_recovery(const d3d11::device_snapshot& graphics)
	{
		engine_stereo_bridge::reset(); // Reject init before any live OpenVR lookup.
		{
			openvr::runtime_backend direct_change;
			access::attach(direct_change, graphics, nullptr, nullptr, false, false);
			d3d11::present_event changed{};
			changed.graphics = create_warp(graphics.generation + 10);
			changed.frame_index = 1;
			direct_change.on_present(changed);
			direct_change.on_present_post(changed, S_OK);
			require(direct_change.get_status().initialization_attempt_count == 2,
				"generation change without a destroy callback lost initialization");
		}
		openvr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		d3d11::present_event event{};
		auto present = [&](HRESULT result = S_OK)
		{
			++event.frame_index;
			runtime.on_present(event);
			runtime.on_present_post(event, result);
		};
		present();
		require(runtime.get_status().initialization_attempt_count == 0,
			"missing graphics consumed an initialization attempt");
		event.graphics = graphics;
		present();
		require(runtime.get_status().initialization_attempt_count == 1, "initial attempt missing");
		present();
		require(runtime.get_status().initialization_attempt_count == 1, "failed init retried every Present");
		// Seed an established session, then exercise both real lifecycle paths.
		access::attach(runtime, graphics, nullptr, nullptr, false, false);
		runtime.on_device_destroying(graphics);
		event.graphics = create_warp(graphics.generation + 1);
		present();
		require(runtime.get_status().initialization_attempt_count == 2, "device destroy lost automatic initialization");
		present();
		require(runtime.get_status().initialization_attempt_count == 2, "new-device failure caused a retry loop");
		runtime.request_reinitialize();
		present();
		require(runtime.get_status().initialization_attempt_count == 3, "explicit reinit stopped working");
		// A failed Present can erase the active generation before device destruction.
		present(DXGI_ERROR_DEVICE_REMOVED);
		runtime.on_device_destroying(event.graphics);
		event.graphics = create_warp(graphics.generation + 2);
		present();
		require(runtime.get_status().initialization_attempt_count == 4, "Present-failure cleanup hid the new device");
		runtime.set_desired_enabled(false);
		event.graphics.generation++;
		present();
		require(runtime.get_status().initialization_attempt_count == 4, "new device reenabled disabled VR");
		runtime.set_desired_enabled(true);
		present();
		require(runtime.get_status().initialization_attempt_count == 5, "reenable lost its initialization attempt");
		runtime.shutdown();
		event.graphics.generation++;
		present();
		require(runtime.get_status().initialization_attempt_count == 5 && runtime.shutdown_complete(),
			"new device revived a terminally shut down runtime");
	}
}

int main(const int argc, const char* const argv[])
{
	try
	{
		const auto graphics = create_warp(200);
		if (argc == 2 && std::string_view(argv[1]) == "--shutdown")
		{
			expect_terminal_shutdown(graphics, false);
			expect_terminal_shutdown(graphics, true);
			std::cout << "vr-openvr-lifecycle-tests (shutdown): PASS\n";
			return 0;
		}
		expect_initial_focus_recovery(graphics, false);
		expect_initial_focus_recovery(graphics, true);
		expect_frontend_tracking(graphics);
		expect_loading_movie_tracking(graphics);
		expect_pause_page_dim(graphics);
		for (const bool probing : {false, true})
			for (const auto results : {std::array{VRCompositorError_DoNotHaveFocus, VRCompositorError_None},
				std::array{VRCompositorError_None, VRCompositorError_DoNotHaveFocus},
				std::array{VRCompositorError_DoNotHaveFocus, VRCompositorError_DoNotHaveFocus}})
				expect_submit_focus_recovery(graphics, results, probing);
		expect_hard_submit_failure(graphics, true);
		expect_hard_submit_failure(graphics, false);
		expect_hard_pose_failure(graphics);
		expect_terminal_shutdown(graphics, false);
		expect_terminal_shutdown(graphics, true);
		expect_device_recovery(graphics);
		expect_resize_recovery(graphics);
		std::cout << "vr-openvr-lifecycle-tests: PASS\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "vr-openvr-lifecycle-tests: FAIL: " << error.what() << '\n';
		return 1;
	}
}
