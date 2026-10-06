#include <std_include.hpp>

#include "component/vr/engine_stereo_bridge.hpp"
#include "component/vr/openxr_runtime.hpp"
#include "component/vr/steamvr_runtime.hpp"
#include "mock_control.hpp"
#include "test_support.hpp"
#include "component/vr/engine_scene_resolution.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <json.hpp>

namespace
{
	std::string utf8(const std::wstring_view value)
	{
		const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
			static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		vr::tests::require(required > 0, "could not encode a test path as UTF-8");
		std::string result(static_cast<std::size_t>(required), '\0');
		(void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
			static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
		return result;
	}

	void expect_steamvr_manifest_integration()
	{
		const auto module = GetModuleHandleW(L"openxr_loader.dll");
		vr::tests::require(module != nullptr, "mock loader is unavailable for manifest integration");
		std::wstring module_path(32768, L'\0');
		const auto length = GetModuleFileNameW(module, module_path.data(),
			static_cast<DWORD>(module_path.size()));
		vr::tests::require(length != 0 && length < module_path.size(), "mock loader path is unavailable");
		module_path.resize(length);

		const auto manifest_path = std::filesystem::temp_directory_path() /
			std::format("h2v-steamxr-manifest-{}-{}.json", GetCurrentProcessId(), GetTickCount64());
		{
			nlohmann::json manifest;
			manifest["file_format_version"] = "1.0.0";
			manifest["runtime"]["library_path"] = utf8(module_path);
			std::ofstream stream(manifest_path, std::ios::binary);
			stream << manifest;
		}

		constexpr wchar_t environment[] = L"XR_RUNTIME_JSON";
		const auto existing_size = GetEnvironmentVariableW(environment, nullptr, 0);
		std::wstring existing(existing_size, L'\0');
		if (existing_size != 0)
		{
			const auto written = GetEnvironmentVariableW(environment, existing.data(), existing_size);
			existing.resize(written);
		}
		SetEnvironmentVariableW(environment, manifest_path.c_str());
		const auto location = vr::steamvr::locate_active_runtime();
		SetEnvironmentVariableW(environment, existing_size == 0 ? nullptr : existing.c_str());
		std::error_code remove_error;
		std::filesystem::remove(manifest_path, remove_error);

		vr::tests::require(location.steamvr_manifest && location.valid,
			"the active SteamVR manifest was not resolved for diagnostics");
		vr::tests::require(std::filesystem::equivalent(std::filesystem::path(module_path),
			std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(
				location.client_library_path.c_str())))),
			"manifest integration resolved a different runtime library");
	}

	class mock_loader_control final
	{
	public:
		mock_loader_control()
		{
			std::wstring path(32768, L'\0');
			const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
			vr::tests::require(length != 0 && length < path.size(), "GetModuleFileNameW failed");
			path.resize(length);
			const auto separator = path.find_last_of(L"\\/");
			vr::tests::require(separator != std::wstring::npos, "executable path has no directory separator");
			path.resize(separator + 1);
			path += L"openxr_loader.dll";

			module_ = LoadLibraryExW(path.c_str(), nullptr,
				LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
			vr::tests::require(module_ != nullptr, std::format("failed to load mock openxr_loader.dll (Win32={})",
				GetLastError()));

			reset = load<vr::tests::mock::reset_fn>("h2vMockReset");
			set_scenario = load<vr::tests::mock::set_scenario_fn>("h2vMockSetScenario");
			fail_once = load<vr::tests::mock::fail_once_fn>("h2vMockFailOnce");
			destroy_acquired = load<vr::tests::mock::destroy_acquired_fn>("h2vMockDestroyAcquiredSwapchain");
			set_graphics_requirements = load<vr::tests::mock::set_graphics_requirements_fn>(
				"h2vMockSetGraphicsRequirements");
			set_should_render = load<vr::tests::mock::set_should_render_fn>("h2vMockSetShouldRender");
			set_action_value=load<vr::tests::mock::set_action_value_fn>("h2vMockSetActionValue");
			set_eye_extent=load<vr::tests::mock::set_eye_extent_fn>("h2vMockSetEyeExtent");
			set_cylinder_supported=load<vr::tests::mock::set_cylinder_supported_fn>("h2vMockSetCylinderSupported");
			set_synthetic_checks=load<vr::tests::mock::set_synthetic_checks_fn>("h2vMockSetSyntheticChecks");
			set_view_flags = load<vr::tests::mock::set_view_flags_fn>("h2vMockSetViewFlags");
			set_head_height = load<vr::tests::mock::set_head_height_fn>("h2vMockSetHeadHeight");
			set_runtime_name = load<vr::tests::mock::set_runtime_name_fn>("h2vMockSetRuntimeName");
			set_interaction_profile = load<vr::tests::mock::set_interaction_profile_fn>("h2vMockSetInteractionProfile");
			queue_session_state = load<vr::tests::mock::queue_session_state_fn>("h2vMockQueueSessionState");
			get_statistics = load<vr::tests::mock::get_statistics_fn>("h2vMockGetStatistics");
		}

		~mock_loader_control()
		{
			unload();
		}

		void unload()
		{
			if (module_ != nullptr)
			{
				FreeLibrary(module_);
				module_ = nullptr;
			}
		}

		mock_loader_control(const mock_loader_control&) = delete;
		mock_loader_control& operator=(const mock_loader_control&) = delete;

		vr::tests::mock::statistics statistics() const
		{
			vr::tests::mock::statistics result{};
			get_statistics(&result);
			return result;
		}

		vr::tests::mock::reset_fn reset{};
		vr::tests::mock::set_scenario_fn set_scenario{};
		vr::tests::mock::fail_once_fn fail_once{};
		vr::tests::mock::destroy_acquired_fn destroy_acquired{};
		vr::tests::mock::set_graphics_requirements_fn set_graphics_requirements{};
		vr::tests::mock::set_should_render_fn set_should_render{};
		vr::tests::mock::set_action_value_fn set_action_value{};
		vr::tests::mock::set_cylinder_supported_fn set_cylinder_supported;
		vr::tests::mock::set_synthetic_checks_fn set_synthetic_checks{};
		vr::tests::mock::set_eye_extent_fn set_eye_extent{};
		vr::tests::mock::set_view_flags_fn set_view_flags{};
		vr::tests::mock::set_head_height_fn set_head_height{};
		vr::tests::mock::set_runtime_name_fn set_runtime_name{};
		vr::tests::mock::set_interaction_profile_fn set_interaction_profile{};
		vr::tests::mock::queue_session_state_fn queue_session_state{};
		vr::tests::mock::get_statistics_fn get_statistics{};

	private:
		template <typename Function>
		Function load(const char* const name)
		{
			const auto address = GetProcAddress(module_, name);
			vr::tests::require(address != nullptr, std::format("mock loader export {} is missing", name));
			return reinterpret_cast<Function>(address);
		}

		HMODULE module_{};
	};

	void configure(mock_loader_control& loader, const d3d11::device_snapshot& graphics,
		const vr::tests::mock::scenario scenario);

	void configure(mock_loader_control& loader, const d3d11::device_snapshot& graphics,
		const vr::tests::mock::scenario scenario)
	{
		loader.reset();
		loader.set_graphics_requirements(vr::tests::adapter_luid(graphics), graphics.feature_level);
		loader.set_scenario(scenario);
	}

	void expect_failed_scenario(mock_loader_control& loader, const d3d11::device_snapshot& graphics,
		const vr::tests::mock::scenario scenario, const vr::runtime_state expected_state)
	{
		configure(loader, graphics, scenario);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(!runtime.initialize(graphics), "failure scenario unexpectedly initialized");
		const auto status = runtime.get_status();
		vr::tests::require(status.sdk_headers_available, "OpenXR SDK headers are unavailable");
		vr::tests::require(status.state == expected_state,
			std::format("failure scenario expected {}, got {}", vr::to_string(expected_state),
				vr::to_string(status.state)));
		vr::tests::require(!status.applied_enabled, "failure scenario left VR applied");
		vr::tests::require(!status.loader_loaded, "failure scenario left the loader owned by production");
		runtime.shutdown();
	}

	void expect_swapchain_failure_result(mock_loader_control& loader,
		const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::swapchain_image_failure);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(!runtime.initialize(graphics), "swapchain image failure unexpectedly initialized");
		const auto status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::runtime_unavailable,
			std::format("swapchain image failure state={} stage={} error={}",vr::to_string(status.state),status.last_initialization_stage,status.last_error));
		vr::tests::require(status.last_xr_result == XR_ERROR_RUNTIME_FAILURE &&
			status.last_xr_result_name == "XR_ERROR_RUNTIME_FAILURE",
			"swapchain cleanup overwrote the initialization failure XrResult");
		vr::tests::require(status.last_initialization_stage == "swapchains",
			"swapchain failure stage was not preserved");
		runtime.shutdown();
	}

	void expect_wait_timeout_result(mock_loader_control& loader, const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::wait_timeout);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "wait-timeout scenario failed to initialize");
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(graphics, 1);
		const auto status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::recoverable_error,
			std::format("wait timeout did not become recoverable_error (actual={})",
				vr::to_string(status.state)));
		vr::tests::require(status.last_xr_result == XR_TIMEOUT_EXPIRED &&
			status.last_xr_result_name == "XR_TIMEOUT_EXPIRED",
			"frame cleanup or xrEndFrame overwrote the wait timeout XrResult");
		vr::tests::require(status.last_error.find("xrWaitSwapchainImage") != std::string::npos,
			"wait timeout error did not retain the failing operation");
		const auto statistics = loader.statistics();
		vr::tests::require(statistics.images_acquired == 1 && statistics.images_waited == 1 &&
			statistics.images_released == 1,
			"wait timeout did not leave the acquired image eligible for a later legal wait/release");
		runtime.shutdown();
	}

	void expect_recreate_waits_for_ready(mock_loader_control& loader, const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "recreation initialization failed");
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(graphics, 1);
		runtime.request_reinitialize();
		runtime.on_present(graphics, 2);
		const auto idle = runtime.get_status();
		vr::tests::require(idle.state == vr::runtime_state::session_idle && idle.session_generation == 2 &&
			!idle.session_running && loader.statistics().frames_waited == 1,
			"recreated session must not inherit running state or submit before READY");
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(graphics, 3);
		vr::tests::require(runtime.get_status().submitted_frames == 2, "recreated session did not resume after READY");
		runtime.shutdown();
	}

	void expect_session_end_failure(mock_loader_control& loader, const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "session-end failure initialization failed");
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(graphics, 1);
		loader.fail_once(vr::tests::mock::failure_point::end_session, XR_ERROR_RUNTIME_FAILURE);
		loader.queue_session_state(XR_SESSION_STATE_STOPPING);
		runtime.on_present(graphics, 2);
		const auto status = runtime.get_status();
		vr::tests::require(!status.session_running && status.last_xr_result == XR_ERROR_RUNTIME_FAILURE &&
			status.last_error.find("xrEndSession") != std::string::npos,
			"xrEndSession must retire running state even when it returns an error");
		runtime.shutdown();
		vr::tests::require(!runtime.get_status().loader_loaded && loader.statistics().sessions_ended == 1,
			"shutdown must not repeat an already completed session-end transition");
	}

	void expect_tracking_recovery(mock_loader_control& loader, const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "tracking recovery initialization failed");
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(graphics, 1);
		for(const auto flags : {XrViewStateFlags{0}, XR_VIEW_STATE_POSITION_VALID_BIT, XR_VIEW_STATE_ORIENTATION_VALID_BIT})
		{
			loader.set_view_flags(flags);
			runtime.on_present(graphics, 2);
			const auto status = runtime.get_status();
			vr::tests::require(status.state == vr::runtime_state::running && status.session_running &&
				status.applied_enabled && status.session_generation == 1 && status.submitted_frames == 1,
				"temporary invalid tracking disabled or rebuilt the OpenXR object graph");
		}
		const auto skipped = loader.statistics();
		vr::tests::require(skipped.zero_layer_frames == 3 && skipped.images_acquired == 2 &&
			skipped.frames_waited == skipped.frames_begun && skipped.frames_begun == skipped.frames_ended,
			"invalid poses must close frames without acquiring or rendering images");
		loader.set_view_flags(XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT);
		runtime.on_present(graphics, 3);
		vr::tests::require(runtime.get_status().submitted_frames == 2 && loader.statistics().instances_created == 1,
			"valid tracking did not resume on the existing session");
		runtime.shutdown();
		vr::tests::require(!runtime.get_status().loader_loaded && loader.statistics().invalid_session_end_rejected == 0,
			"shutdown must not call xrEndSession outside STOPPING");
	}

	void expect_positive_wait_result(mock_loader_control& loader, const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "positive-wait scenario failed to initialize");
		loader.fail_once(vr::tests::mock::failure_point::wait_swapchain_image, XR_SESSION_LOSS_PENDING);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(graphics, 1);
		const auto status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::running && status.submitted_frames == 1,
			"XR_SESSION_LOSS_PENDING wait result was not treated as a successful wait");
		const auto statistics = loader.statistics();
		vr::tests::require(statistics.images_waited == 2 && statistics.images_released == 2,
			"positive wait result did not allow both acquired images to be released");
		runtime.shutdown();
	}

	void expect_frame_root_cause(mock_loader_control& loader, const d3d11::device_snapshot& graphics,
		const vr::tests::mock::failure_point failure, const char* const operation)
	{
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "frame-failure scenario failed to initialize");
		loader.fail_once(failure, XR_ERROR_RUNTIME_FAILURE);
		loader.fail_once(vr::tests::mock::failure_point::destroy_swapchain, XR_ERROR_HANDLE_INVALID);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(graphics, 1);
		const auto status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::recoverable_error,
			"frame failure did not become recoverable_error");
		vr::tests::require(status.last_xr_result == XR_ERROR_RUNTIME_FAILURE &&
			status.last_xr_result_name == "XR_ERROR_RUNTIME_FAILURE",
			"secondary frame cleanup overwrote the root XrResult");
		vr::tests::require(status.last_error.find(operation) != std::string::npos,
			"frame failure did not retain the failing operation");
		runtime.shutdown();
	}

	void expect_destroy_acquired_rejected(mock_loader_control& loader,
		const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::wait_timeout);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "acquired-destroy scenario failed to initialize");
		loader.fail_once(vr::tests::mock::failure_point::wait_swapchain_image, XR_ERROR_RUNTIME_FAILURE);
		loader.fail_once(vr::tests::mock::failure_point::wait_swapchain_image, XR_ERROR_RUNTIME_FAILURE);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(graphics, 1);
		vr::tests::require(loader.destroy_acquired() == XR_ERROR_CALL_ORDER_INVALID,
			"mock accepted destruction of a still-acquired swapchain image");
		auto status = runtime.get_status();
		vr::tests::require(status.instance_created && status.session_created && status.eyes[0].width == 64,
			"failed acquired-image cleanup discarded the live object graph");
		runtime.request_reinitialize();
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(graphics, 2);
		status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::running && status.session_generation == 2,
			"reinitialize retry did not destroy and rebuild the retained object graph");
		vr::tests::require(loader.statistics().destroy_while_acquired_rejected == 1,
			"mock did not count the acquired-swapchain destruction rejection");
		runtime.shutdown();
	}

	void expect_teardown_retry(mock_loader_control& loader, const d3d11::device_snapshot& graphics,
		const vr::tests::mock::failure_point failure, const char* const operation)
	{
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		vr::engine_stereo_bridge::set_enabled(false);
		vr::engine_stereo_bridge::reset();
		vr::engine_stereo_bridge::configure_target(true);
		vr::engine_stereo_bridge::set_render_hook_installed(true);
		vr::engine_stereo_bridge::set_enabled(true);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "teardown-failure scenario failed to initialize");
		vr::engine_stereo_bridge::publish_views(
			{-0.032f, 0.0f, 0.0f}, {0.032f, 0.0f, 0.0f},
			{{{-1.0f, 1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f, 1.0f}}});
		vr::tests::require(vr::engine_stereo_bridge::is_active(),
			"teardown regression did not create a valid stereo view configuration");
		loader.fail_once(failure, XR_ERROR_RUNTIME_FAILURE);
		runtime.request_reinitialize();
		runtime.on_present(graphics, 1);
		auto status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::recoverable_error && status.loader_loaded,
			"failed teardown did not retain a retryable object graph");
		vr::tests::require(!vr::engine_stereo_bridge::is_active(),
			"failed teardown left the engine stereo camera gate active");
		vr::tests::require(status.last_xr_result == XR_ERROR_RUNTIME_FAILURE &&
			status.last_error.find(operation) != std::string::npos,
			"failed teardown did not report its injected root operation");
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(graphics, 2);
		status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::running && status.session_generation == 2,
			"reinitialize retry did not finish teardown and rebuild");

		loader.fail_once(failure, XR_ERROR_RUNTIME_FAILURE);
		runtime.shutdown();
		status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::recoverable_error && status.loader_loaded,
			"failed shutdown did not retain a retryable object graph");
		runtime.shutdown();
		status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::disabled && !status.loader_loaded,
			"shutdown retry did not destroy the retained object graph");
		const auto statistics = loader.statistics();
		vr::tests::require(statistics.instances_created == 2 && statistics.instances_destroyed == 2 &&
			statistics.sessions_created == 2 && statistics.sessions_destroyed == 2 &&
			statistics.spaces_created == 12 && statistics.spaces_destroyed == 12 &&
			statistics.swapchains_created == 4 && statistics.swapchains_destroyed == 4,
			"teardown retry leaked or double-destroyed part of the object graph");
		vr::engine_stereo_bridge::set_enabled(false);
	}

	void expect_destroy_instance_null_proc(mock_loader_control& loader,
		const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::destroy_instance_proc_null);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(!runtime.initialize(graphics), "null destroy proc scenario unexpectedly initialized");
		const auto status = runtime.get_status();
		vr::tests::require(status.last_xr_result == XR_ERROR_FUNCTION_UNSUPPORTED &&
			status.last_xr_result_name == "XR_ERROR_FUNCTION_UNSUPPORTED",
			"XR_SUCCESS plus null cleanup proc was not mapped to XR_ERROR_FUNCTION_UNSUPPORTED");
		loader.set_scenario(vr::tests::mock::scenario::happy);
		runtime.shutdown();
	}

	void expect_strict_scene_source_rejection(mock_loader_control& loader,const d3d11::device_snapshot& graphics)
	{
		configure(loader,graphics,vr::tests::mock::scenario::happy);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics),"strict source initialization failed");
		vr::engine_stereo_bridge::configure_target(true);vr::engine_stereo_bridge::set_render_hook_installed(true);
		vr::engine_stereo_bridge::set_enabled(true);runtime.set_scene_mode(vr::scene_mode::engine_stereo);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		const d3d11::present_event event{.graphics=graphics,.frame_index=1};
		runtime.on_present(event);runtime.on_present_post(event,S_OK);
		const auto status=runtime.get_status();
		vr::tests::require(status.applied_enabled&&!status.native_renderer_ready&&status.submitted_frames==0&&
			loader.statistics().images_acquired==0&&loader.statistics().zero_layer_frames==1,
			"unproven native source must not acquire images or submit a substitute");
		vr::tests::require(vr::engine_stereo_bridge::is_active(),"bootstrap observations must retain the valid CPU view family");
		vr::engine_stereo_bridge::set_enabled(false);runtime.shutdown();
	}

		void run_happy_path(mock_loader_control& loader, const d3d11::device_snapshot& first_graphics)
	{
		configure(loader, first_graphics, vr::tests::mock::scenario::happy);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(first_graphics), "happy-path initialization failed");

		auto status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::session_idle, "initialized runtime is not session_idle");
		vr::tests::require(status.applied_enabled && status.loader_loaded && status.instance_created &&
			status.session_created, "happy path did not create the OpenXR object graph");
		vr::tests::require(status.view_count == 2 && status.eyes[0].width == 64 && status.eyes[1].height == 64,
			"happy path did not create two real-texture eye swapchains");
		vr::tests::require(status.runtime_name == "h2v mock OpenXR runtime" && status.system_name == "h2v mock HMD",
			"mock runtime identity was not propagated");

		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(first_graphics, 1);
		status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::running && status.session_running,
			"READY did not begin the session");
		vr::tests::require(status.submitted_frames == 1, "first READY frame was not submitted");
		vr::tests::require(status.eyes[0].acquired == 1 && status.eyes[1].released == 1,
			"first frame did not acquire and release both eye images");

		for (std::uint64_t frame = 2; frame <= 4; ++frame)
		{
			runtime.on_present(first_graphics, frame);
		}
		status = runtime.get_status();
		vr::tests::require(status.submitted_frames == 4, "multi-frame happy path did not submit four frames");

		loader.set_should_render(FALSE);
		runtime.on_present(first_graphics, 5);
		status = runtime.get_status();
		vr::tests::require(status.submitted_frames == 4 && status.state == vr::runtime_state::running,
			"shouldRender=false changed submitted frames or stopped the session");
		loader.set_should_render(TRUE);

		loader.queue_session_state(XR_SESSION_STATE_STOPPING);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(first_graphics, 6);
		status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::running && status.session_running,
			"STOPPING -> READY did not return to running");
		vr::tests::require(status.session_stopping_count == 1 && status.session_ready_count == 2 &&
			status.session_begin_count == 2 && status.session_end_count == 1,
			"STOPPING -> READY counters are inconsistent");
		vr::tests::require(status.submitted_frames == 5, "STOPPING -> READY frame was not submitted");

		auto second_graphics = vr::tests::create_graphics(2);
		loader.set_graphics_requirements(vr::tests::adapter_luid(second_graphics), second_graphics.feature_level);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(second_graphics, 7);
		status = runtime.get_status();
		vr::tests::require(status.device_generation == 2 && status.session_generation == 2,
			"device recreation did not rebuild the OpenXR session");
		vr::tests::require(status.state == vr::runtime_state::running && status.submitted_frames == 6,
			"device recreation did not resume rendering");

		for (std::uint64_t reinit = 0; reinit < 2; ++reinit)
		{
			runtime.request_reinitialize();
			loader.queue_session_state(XR_SESSION_STATE_READY);
			runtime.on_present(second_graphics, 8 + reinit);
			status = runtime.get_status();
			vr::tests::require(status.state == vr::runtime_state::running && status.applied_enabled,
				"repeated reinitialize did not return to running");
		}
		vr::tests::require(status.initialization_attempt_count == 4 && status.session_generation == 4,
			"repeated reinitialize did not rebuild exactly twice");
		vr::tests::require(status.submitted_frames == 8, "repeated reinitialize frames were not submitted");

		runtime.set_desired_enabled(false);
		runtime.on_present(second_graphics, 10);
		status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::disabled && !status.desired_enabled &&
			!status.applied_enabled && !status.loader_loaded, "disable did not fully tear down VR");

		runtime.set_desired_enabled(true);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		runtime.on_present(second_graphics, 11);
		status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::running && status.session_generation == 5,
			"enable after disable did not rebuild and run");

		runtime.shutdown();
		status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::disabled && !status.loader_loaded,
			"shutdown did not leave a disabled unloaded runtime");

		const auto statistics = loader.statistics();
		vr::tests::require(statistics.instances_created == 5 && statistics.instances_destroyed == 5,
			"instance lifecycle count mismatch");
		vr::tests::require(statistics.sessions_created == 5 && statistics.sessions_destroyed == 5,
			"session lifecycle count mismatch");
		vr::tests::require(statistics.swapchains_created == 10 && statistics.swapchains_destroyed == 10,
			"swapchain lifecycle count mismatch");
		vr::tests::require(statistics.textures_created == 20, "mock runtime did not create real D3D11 textures");
		vr::tests::require(statistics.sessions_begun == 6 && statistics.sessions_ended >= 1,
			"session begin/end ABI was not exercised");
		vr::tests::require(statistics.frames_waited == 10 && statistics.frames_begun == 10 &&
			statistics.frames_ended == 10, "frame ABI call counts are inconsistent");
		vr::tests::require(statistics.projection_frames == 9 && statistics.zero_layer_frames == 1 &&
			statistics.eye_color_frames_validated == 9 && statistics.texture_write_epochs_validated == 9,
			"projection colors, write epochs, or shouldRender=false submissions are inconsistent");
		vr::tests::require(statistics.views_located == 9 && statistics.images_acquired == 18 &&
			statistics.images_waited == 18 && statistics.images_released == 18,
			"view and swapchain image ABI counts are inconsistent");
	}
}

#include "openxr_adaptation_tests.hpp"

int main()
{
	try
	{
		SetEnvironmentVariableA("H2V_VR_BACKEND", "openxr");
		mock_loader_control loader;
		expect_steamvr_manifest_integration();
		const auto graphics = vr::tests::create_graphics(1);

		expect_failed_scenario(loader, graphics, vr::tests::mock::scenario::runtime_unavailable,
			vr::runtime_state::runtime_unavailable);
		expect_failed_scenario(loader, graphics, vr::tests::mock::scenario::no_hmd,
			vr::runtime_state::no_hmd);
		expect_failed_scenario(loader, graphics, vr::tests::mock::scenario::graphics_mismatch,
			vr::runtime_state::graphics_mismatch);
		expect_swapchain_failure_result(loader, graphics);
		expect_recreate_waits_for_ready(loader, graphics);
		expect_session_end_failure(loader, graphics);
		expect_tracking_recovery(loader, graphics);
		expect_wait_timeout_result(loader, graphics);
		expect_positive_wait_result(loader, graphics);
		expect_frame_root_cause(loader, graphics,
			vr::tests::mock::failure_point::acquire_swapchain_image, "xrAcquireSwapchainImage");
		expect_frame_root_cause(loader, graphics,
			vr::tests::mock::failure_point::release_swapchain_image, "xrReleaseSwapchainImage");
		expect_destroy_acquired_rejected(loader, graphics);
		expect_teardown_retry(loader, graphics,
			vr::tests::mock::failure_point::destroy_swapchain, "xrDestroySwapchain");
		expect_teardown_retry(loader, graphics,
			vr::tests::mock::failure_point::destroy_space, "xrDestroySpace");
		expect_teardown_retry(loader, graphics,
			vr::tests::mock::failure_point::destroy_session, "xrDestroySession");
		expect_teardown_retry(loader, graphics,
			vr::tests::mock::failure_point::destroy_instance, "xrDestroyInstance");
		expect_destroy_instance_null_proc(loader, graphics);
			expect_strict_scene_source_rejection(loader, graphics);
		run_happy_path(loader, graphics);
		openxr_adaptation_tests::calibrated_grip_input(loader,graphics);
		openxr_adaptation_tests::focused_startup_height(loader,graphics);
		openxr_adaptation_tests::virtualdesktop_grip_reference(loader,graphics);
		openxr_adaptation_tests::focused_input(loader,graphics);
		openxr_adaptation_tests::native_pair(loader,graphics);
		openxr_adaptation_tests::native_pair(loader,graphics,vr::tests::mock::scenario::parallel_views);
		openxr_adaptation_tests::native_pair(loader,graphics,vr::tests::mock::scenario::scaled_view_quaternions);
		openxr_adaptation_tests::frontend_menu(loader,graphics,false);
		openxr_adaptation_tests::frontend_menu(loader,graphics,true);
		openxr_adaptation_tests::canted_native_views(loader,graphics);
		loader.unload();
		vr::tests::require(GetModuleHandleW(L"openxr_loader.dll") == nullptr,
			"production runtime leaked an openxr_loader.dll module reference");

		std::cout << "vr-runtime-mock-smoke: PASS\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "vr-runtime-mock-smoke: FAIL: " << error.what() << '\n';
		return 1;
	}
}
