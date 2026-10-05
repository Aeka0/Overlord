#include <std_include.hpp>

#include "openxr_runtime.hpp"

#include "engine_stereo_bridge.hpp"
#include "frame_capture.hpp"
#include "head_pose_bridge.hpp"
#include "native_render_session.hpp"
#include "openxr_layer_policy.hpp"
#include "openxr_d3d11.hpp"
#include "runtime_backend.hpp"
#include "scene_compositor.hpp"

#include <d3d11_4.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cmath>
#include <format>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace vr
{
	const char* to_string(const runtime_state state) noexcept
	{
		switch (state)
		{
		case runtime_state::disabled: return "disabled";
		case runtime_state::waiting_for_graphics: return "waiting_for_graphics";
		case runtime_state::sdk_headers_unavailable: return "sdk_headers_unavailable";
		case runtime_state::loader_missing: return "loader_missing";
		case runtime_state::runtime_unavailable: return "runtime_unavailable";
		case runtime_state::no_hmd: return "no_hmd";
		case runtime_state::graphics_mismatch: return "graphics_mismatch";
		case runtime_state::session_idle: return "session_idle";
		case runtime_state::running: return "running";
		case runtime_state::recoverable_error: return "recoverable_error";
		case runtime_state::fatal_for_vr: return "fatal_for_vr";
		}
		return "unknown";
	}
}

namespace vr::openxr
{
#if H2V_OPENXR_HEADERS_AVAILABLE
	namespace
	{
		const char* result_name(const XrResult result) noexcept
		{
#define H2V_XR_RESULT(value) case value: return #value
			switch (result)
			{
			H2V_XR_RESULT(XR_SUCCESS);
			H2V_XR_RESULT(XR_TIMEOUT_EXPIRED);
			H2V_XR_RESULT(XR_SESSION_LOSS_PENDING);
			H2V_XR_RESULT(XR_EVENT_UNAVAILABLE);
			H2V_XR_RESULT(XR_SPACE_BOUNDS_UNAVAILABLE);
			H2V_XR_RESULT(XR_SESSION_NOT_FOCUSED);
			H2V_XR_RESULT(XR_FRAME_DISCARDED);
			H2V_XR_RESULT(XR_ERROR_VALIDATION_FAILURE);
			H2V_XR_RESULT(XR_ERROR_RUNTIME_FAILURE);
			H2V_XR_RESULT(XR_ERROR_OUT_OF_MEMORY);
			H2V_XR_RESULT(XR_ERROR_API_VERSION_UNSUPPORTED);
			H2V_XR_RESULT(XR_ERROR_INITIALIZATION_FAILED);
			H2V_XR_RESULT(XR_ERROR_FUNCTION_UNSUPPORTED);
			H2V_XR_RESULT(XR_ERROR_FEATURE_UNSUPPORTED);
			H2V_XR_RESULT(XR_ERROR_EXTENSION_NOT_PRESENT);
			H2V_XR_RESULT(XR_ERROR_LIMIT_REACHED);
			H2V_XR_RESULT(XR_ERROR_SIZE_INSUFFICIENT);
			H2V_XR_RESULT(XR_ERROR_HANDLE_INVALID);
			H2V_XR_RESULT(XR_ERROR_INSTANCE_LOST);
			H2V_XR_RESULT(XR_ERROR_SESSION_RUNNING);
			H2V_XR_RESULT(XR_ERROR_SESSION_NOT_RUNNING);
			H2V_XR_RESULT(XR_ERROR_SESSION_LOST);
			H2V_XR_RESULT(XR_ERROR_SYSTEM_INVALID);
			H2V_XR_RESULT(XR_ERROR_PATH_INVALID);
			H2V_XR_RESULT(XR_ERROR_PATH_COUNT_EXCEEDED);
			H2V_XR_RESULT(XR_ERROR_PATH_FORMAT_INVALID);
			H2V_XR_RESULT(XR_ERROR_PATH_UNSUPPORTED);
			H2V_XR_RESULT(XR_ERROR_LAYER_INVALID);
			H2V_XR_RESULT(XR_ERROR_LAYER_LIMIT_EXCEEDED);
			H2V_XR_RESULT(XR_ERROR_SWAPCHAIN_RECT_INVALID);
			H2V_XR_RESULT(XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED);
			H2V_XR_RESULT(XR_ERROR_ACTION_TYPE_MISMATCH);
			H2V_XR_RESULT(XR_ERROR_SESSION_NOT_READY);
			H2V_XR_RESULT(XR_ERROR_SESSION_NOT_STOPPING);
			H2V_XR_RESULT(XR_ERROR_TIME_INVALID);
			H2V_XR_RESULT(XR_ERROR_REFERENCE_SPACE_UNSUPPORTED);
			H2V_XR_RESULT(XR_ERROR_FILE_ACCESS_ERROR);
			H2V_XR_RESULT(XR_ERROR_FILE_CONTENTS_INVALID);
			H2V_XR_RESULT(XR_ERROR_FORM_FACTOR_UNSUPPORTED);
			H2V_XR_RESULT(XR_ERROR_FORM_FACTOR_UNAVAILABLE);
			H2V_XR_RESULT(XR_ERROR_API_LAYER_NOT_PRESENT);
			H2V_XR_RESULT(XR_ERROR_CALL_ORDER_INVALID);
			H2V_XR_RESULT(XR_ERROR_GRAPHICS_DEVICE_INVALID);
			H2V_XR_RESULT(XR_ERROR_POSE_INVALID);
			H2V_XR_RESULT(XR_ERROR_INDEX_OUT_OF_RANGE);
			H2V_XR_RESULT(XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED);
			H2V_XR_RESULT(XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED);
			H2V_XR_RESULT(XR_ERROR_NAME_DUPLICATED);
			H2V_XR_RESULT(XR_ERROR_NAME_INVALID);
			H2V_XR_RESULT(XR_ERROR_ACTIONSET_NOT_ATTACHED);
			H2V_XR_RESULT(XR_ERROR_ACTIONSETS_ALREADY_ATTACHED);
			H2V_XR_RESULT(XR_ERROR_LOCALIZED_NAME_DUPLICATED);
			H2V_XR_RESULT(XR_ERROR_LOCALIZED_NAME_INVALID);
			H2V_XR_RESULT(XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING);
			H2V_XR_RESULT(XR_ERROR_RUNTIME_UNAVAILABLE);
			default: return "XR_UNKNOWN_RESULT";
			}
#undef H2V_XR_RESULT
		}

		const char* session_state_name(const XrSessionState state) noexcept
		{
			switch (state)
			{
			case XR_SESSION_STATE_UNKNOWN: return "XR_SESSION_STATE_UNKNOWN";
			case XR_SESSION_STATE_IDLE: return "XR_SESSION_STATE_IDLE";
			case XR_SESSION_STATE_READY: return "XR_SESSION_STATE_READY";
			case XR_SESSION_STATE_SYNCHRONIZED: return "XR_SESSION_STATE_SYNCHRONIZED";
			case XR_SESSION_STATE_VISIBLE: return "XR_SESSION_STATE_VISIBLE";
			case XR_SESSION_STATE_FOCUSED: return "XR_SESSION_STATE_FOCUSED";
			case XR_SESSION_STATE_STOPPING: return "XR_SESSION_STATE_STOPPING";
			case XR_SESSION_STATE_LOSS_PENDING: return "XR_SESSION_STATE_LOSS_PENDING";
			case XR_SESSION_STATE_EXITING: return "XR_SESSION_STATE_EXITING";
			default: return "XR_SESSION_STATE_UNKNOWN_VALUE";
			}
		}
	}

	class runtime_backend::implementation final
	{
	public:
		~implementation() { shutdown(); }

		void set_desired_enabled(const bool enabled)
		{
			const std::lock_guard lock(mutex_);
			if (enabled && !status_.desired_enabled)
			{
				auto_initialize_allowed_ = true;
			}
			status_.desired_enabled = enabled;
			publish_status_locked();
		}

		void set_scene_mode(const scene_mode mode)
		{
			const std::lock_guard lock(mutex_);
			status_.requested_scene_mode = mode;
		}

		void capture_present(const d3d11::present_event& event)
		{
			// Engine stereo owns its source exclusively through native eye targets;
			// never turn an unrelated Present backbuffer into a VR source.
			if (status_.requested_scene_mode != scene_mode::backbuffer)
			{
				return;
			}
			const auto tag = engine_stereo_bridge::consume_capture_tag();
			if (tag.native)
			{
				return;
			}
			capture_.produce(event, {tag.valid, tag.pair_id, tag.eye_index, false});
		}

		bool capture_engine_texture(const d3d11::device_snapshot& graphics,
			ID3D11Texture2D* const source, const capture_frame_tag tag)
		{
			if (!capture_.produce_texture(graphics, source, tag)) return false;
			if (!tag.native) return true;
			if (native_render_session::active().complete_rendered_eye(
				tag.pair_id, tag.eye_index, source))
			{
				return true;
			}
			capture_.invalidate(graphics.generation);
			return false;
		}

		void poll_capture(const d3d11::device_snapshot& graphics)
		{
			capture_.poll(graphics);
		}

		void request_reinitialize()
		{
			const std::lock_guard lock(mutex_);
			auto_initialize_allowed_ = true;
			status_.reinitialize_pending = true;
			publish_status_locked();
		}

		// OpenXR keeps wait/begin/locate/end inside its worker-owned frame
		// transaction. The renderer-thread frame contract is implemented by the
		// OpenVR backend first; keep this explicit no-op until OpenXR is moved to
		// the same transaction model.
		void prepare_frame(const d3d11::device_snapshot&, std::uint64_t) {}

		bool initialize(const d3d11::device_snapshot& graphics)
		{
			const std::lock_guard lock(mutex_);
			const auto initialized = initialize_locked(graphics);
			if (initialized)
			{
				complete_progress_locked("backend.initialize");
			}
			else
			{
				const auto failed_operation = status_.worker_current_operation;
				status_.worker_last_completed_operation = failed_operation.empty()
					? "backend.initialize (failed)" : failed_operation + " (failed)";
				status_.worker_current_operation = "idle";
				status_.worker_last_status_update = GetTickCount64();
				publish_status_locked();
			}
			return initialized;
		}

		void on_present(const d3d11::present_event& event)
		{
			std::unique_lock lock(mutex_, std::try_to_lock);
			if (!lock) return;
			on_present_locked(event.graphics, event.swap_chain, event.frame_index);
			publish_status_locked();
		}

		void on_present(const d3d11::device_snapshot& graphics, const std::uint64_t frame)
		{
			std::unique_lock lock(mutex_, std::try_to_lock);
			if (!lock) return;
			on_present_locked(graphics, nullptr, frame);
			publish_status_locked();
		}

		void on_present_post(const d3d11::present_event&, const HRESULT)
		{
			// OpenXR remains worker-driven. xrEndFrame completes its runtime frame;
			// it does not consume the synchronous DXGI Present result.
		}

		void on_resize_before(const d3d11::resize_event& event) noexcept
		{
			const std::lock_guard lock(mutex_);
			compositor_.invalidate();
			capture_.invalidate(event.graphics.generation);
		}

		void on_device_destroying(const d3d11::device_snapshot& graphics) noexcept
		{
			const std::lock_guard lock(mutex_);
			capture_.invalidate(graphics.generation);
			if (status_.device_generation == graphics.generation)
			{
				status_.reinitialize_pending = true;
				(void)teardown_locked(false);
			}
		}

		void shutdown() noexcept
		{
			const std::lock_guard lock(mutex_);
			status_.desired_enabled = false;
			status_.reinitialize_pending = false;
			(void)teardown_locked(true);
			publish_status_locked();
		}

		bool requested_enabled() const
		{
			const std::lock_guard lock(mutex_);
			return status_.desired_enabled;
		}

		bool applied_enabled() const
		{
			const std::lock_guard lock(mutex_);
			return status_.applied_enabled;
		}

		runtime_status get_status() const
		{
			runtime_status snapshot;
			{
				const std::lock_guard lock(status_mutex_);
				snapshot = status_snapshot_;
			}
			snapshot.capture = capture_.get_status();
			return snapshot;
		}

	private:
		void set_result(const XrResult result) noexcept
		{
			status_.last_xr_result = result;
			status_.last_xr_result_name = result_name(result);
		}

		bool fail(const runtime_state state, const char* operation, const XrResult result)
		{
			status_.state = state;
			status_.last_error = std::format("{} failed (XrResult={} {})", operation,
				static_cast<std::int64_t>(result), result_name(result));
			set_result(result);
			return false;
		}

		void publish_status_locked() noexcept
		{
			const std::lock_guard lock(status_mutex_);
			status_snapshot_ = status_;
		}

		void publish_progress_locked(const char* const stage, const std::string& operation)
		{
			status_.last_initialization_stage = stage;
			status_.worker_current_operation = operation;
			status_.worker_last_status_update = GetTickCount64();
			publish_status_locked();
		}

		void complete_progress_locked(const char* const operation)
		{
			status_.worker_last_completed_operation = operation;
			status_.worker_current_operation = "idle";
			status_.worker_last_status_update = GetTickCount64();
			publish_status_locked();
		}

		bool call_ok(const XrResult result, const char* operation, const runtime_state state)
		{
			return XR_FAILED(result) ? fail(state, operation, result) : true;
		}

		bool initialize_locked(const d3d11::device_snapshot& graphics)
		{
			++status_.initialization_attempt_count;
			status_.sdk_headers_available = true;
			status_.last_error.clear();
			publish_progress_locked("graphics", "validate graphics and prior session");
			if (!status_.desired_enabled)
			{
				status_.state = runtime_state::disabled;
				return false;
			}
			if (!graphics)
			{
				status_.state = runtime_state::waiting_for_graphics;
				status_.last_error = "D3D11 graphics device is unavailable";
				return false;
			}
			if (has_objects() && !teardown_locked(false))
			{
				return false;
			}

			publish_progress_locked("layer_policy", "isolate incompatible implicit OpenXR layers");
			const auto layer_policy = apply_native_implicit_layer_policy();
			status_.implicit_layer_policy_applied = layer_policy.applied;
			status_.implicit_layer_manifest_count = static_cast<std::uint32_t>(layer_policy.manifest_count);
			status_.implicit_layers_disabled = static_cast<std::uint32_t>(layer_policy.disabled_layers.size());
			status_.disabled_implicit_layers.clear();
			for (const auto& layer : layer_policy.disabled_layers)
			{
				if (!status_.disabled_implicit_layers.empty()) status_.disabled_implicit_layers += ", ";
				status_.disabled_implicit_layers += layer;
			}
			status_.implicit_layer_policy_warning.clear();
			for (const auto& warning : layer_policy.warnings)
			{
				if (!status_.implicit_layer_policy_warning.empty()) status_.implicit_layer_policy_warning += "; ";
				status_.implicit_layer_policy_warning += warning;
			}
			if (!layer_policy.blocking_error.empty())
			{
				status_.state = runtime_state::runtime_unavailable;
				status_.last_error = layer_policy.blocking_error;
				return false;
			}

			publish_progress_locked("loader", "discover openxr_loader.dll");
			std::vector<std::wstring> loader_paths;
			std::vector<wchar_t> module_buffer(MAX_PATH);
			const auto append_module_loader_path = [&loader_paths, &module_buffer](HMODULE module)
			{
				if (module == nullptr) return;
				for (;;)
				{
					const auto length = GetModuleFileNameW(module, module_buffer.data(),
						static_cast<DWORD>(module_buffer.size()));
					if (length == 0) return;
					if (length < module_buffer.size() - 1)
					{
						const std::wstring module_path(module_buffer.data(), length);
						const auto separator = module_path.find_last_of(L"\\/");
						if (separator != std::wstring::npos)
						{
							const auto loader_path = module_path.substr(0, separator + 1) + L"openxr_loader.dll";
							if (std::ranges::find(loader_paths, loader_path) == loader_paths.end())
								loader_paths.push_back(loader_path);
						}
						return;
					}
					module_buffer.resize(module_buffer.size() * 2);
				}
			};
			HMODULE current_module{};
			(void)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&result_name), &current_module);
			append_module_loader_path(current_module);
			append_module_loader_path(nullptr);
			const auto last_error_before_load = GetLastError();
			for (const auto& loader_path : loader_paths)
			{
				publish_progress_locked("loader", "LoadLibraryExW candidate");
				loader_ = LoadLibraryExW(loader_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
				if (loader_ != nullptr) break;
			}
			if (loader_ == nullptr)
			{
				const auto load_error = GetLastError();
				SetLastError(last_error_before_load);
				status_.state = runtime_state::loader_missing;
				status_.last_error = std::format(
					"openxr_loader.dll was not loadable beside the active h2-mod-vr/game modules (Win32={})",
					load_error);
				return false;
			}
			status_.loader_loaded = true;
			publish_progress_locked("loader", "resolve xrGetInstanceProcAddr");
			auto entry = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader_, "xrGetInstanceProcAddr"));
			XrResult result{XR_SUCCESS};
			std::string error;
			publish_progress_locked("loader", "load global OpenXR dispatch");
			if (!dispatch_.load_global(entry, error, result))
			{
				status_.last_error = error;
				set_result(result);
				status_.state = runtime_state::runtime_unavailable;
				(void)teardown_preserving_error();
				return false;
			}

			publish_progress_locked("extensions", "xrEnumerateInstanceExtensionProperties(count)");
			std::uint32_t extension_count{};
			result = dispatch_.enumerate_instance_extension_properties(nullptr, 0, &extension_count, nullptr);
			if (!call_ok(result, "xrEnumerateInstanceExtensionProperties", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			publish_progress_locked("extensions", "xrEnumerateInstanceExtensionProperties(data)");
			std::vector<XrExtensionProperties> extensions(extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
			result = dispatch_.enumerate_instance_extension_properties(nullptr, extension_count, &extension_count,
				extensions.data());
			if (!call_ok(result, "xrEnumerateInstanceExtensionProperties", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			const auto supports_d3d11 = std::ranges::any_of(extensions, [](const auto& item)
			{
				return std::strcmp(item.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
			});
			if (!supports_d3d11)
			{
				fail(runtime_state::runtime_unavailable, "XR_KHR_D3D11_enable", XR_ERROR_EXTENSION_NOT_PRESENT);
				(void)teardown_preserving_error(); return false;
			}

			publish_progress_locked("instance", "xrCreateInstance");
			constexpr auto requested_api_version = XR_MAKE_VERSION(1, 0, 0);
			const char* enabled_extensions[]{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
			XrInstanceCreateInfo instance_info{XR_TYPE_INSTANCE_CREATE_INFO};
			strcpy_s(instance_info.applicationInfo.applicationName, "h2-mod-vr");
			instance_info.applicationInfo.applicationVersion = 1;
			strcpy_s(instance_info.applicationInfo.engineName, "h2-mod");
			instance_info.applicationInfo.engineVersion = 1;
			instance_info.applicationInfo.apiVersion = requested_api_version;
			instance_info.enabledExtensionCount = 1;
			instance_info.enabledExtensionNames = enabled_extensions;
			result = dispatch_.create_instance(&instance_info, &instance_);
			if (!call_ok(result, "xrCreateInstance", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			status_.instance_created = true;
			publish_progress_locked("instance", "load instance OpenXR dispatch");
			if (!dispatch_.load_instance(instance_, error, result))
			{
				status_.state = runtime_state::runtime_unavailable;
				status_.last_error = error;
				set_result(result);
				(void)teardown_preserving_error(); return false;
			}

			publish_progress_locked("instance", "xrGetInstanceProperties");
			XrInstanceProperties instance_properties{XR_TYPE_INSTANCE_PROPERTIES};
			result = dispatch_.get_instance_properties(instance_, &instance_properties);
			if (!call_ok(result, "xrGetInstanceProperties", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			status_.runtime_name = instance_properties.runtimeName;
			status_.last_runtime_name = status_.runtime_name;

			publish_progress_locked("system", "xrGetSystem");
			const XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO, nullptr, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
			result = dispatch_.get_system(instance_, &system_info, &system_id_);
			if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
			{
				fail(runtime_state::no_hmd, "xrGetSystem", result);
				(void)teardown_preserving_error(); return false;
			}
			if (!call_ok(result, "xrGetSystem", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			publish_progress_locked("system", "xrGetSystemProperties");
			XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES};
			result = dispatch_.get_system_properties(instance_, system_id_, &system_properties);
			if (!call_ok(result, "xrGetSystemProperties", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			status_.system_name = system_properties.systemName;
			status_.last_system_name = status_.system_name;

			publish_progress_locked("views", "xrEnumerateViewConfigurationViews(count)");
			std::uint32_t view_count{};
			result = dispatch_.enumerate_view_configuration_views(instance_, system_id_, view_type_, 0,
				&view_count, nullptr);
			if (!call_ok(result, "xrEnumerateViewConfigurationViews", runtime_state::runtime_unavailable) || view_count != 2)
			{
				if (!XR_FAILED(result)) fail(runtime_state::runtime_unavailable, "stereo view count", XR_ERROR_RUNTIME_FAILURE);
				(void)teardown_preserving_error(); return false;
			}
			publish_progress_locked("views", "xrEnumerateViewConfigurationViews(data)");
			view_configs_.assign(view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
			result = dispatch_.enumerate_view_configuration_views(instance_, system_id_, view_type_, view_count,
				&view_count, view_configs_.data());
			if (!call_ok(result, "xrEnumerateViewConfigurationViews", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			status_.view_count = view_count;

			publish_progress_locked("views", "xrEnumerateEnvironmentBlendModes(count)");
			std::uint32_t blend_count{};
			result = dispatch_.enumerate_environment_blend_modes(instance_, system_id_, view_type_, 0,
				&blend_count, nullptr);
			if (!call_ok(result, "xrEnumerateEnvironmentBlendModes", runtime_state::runtime_unavailable) || blend_count == 0)
			{
				(void)teardown_preserving_error(); return false;
			}
			publish_progress_locked("views", "xrEnumerateEnvironmentBlendModes(data)");
			std::vector<XrEnvironmentBlendMode> blend_modes(blend_count);
			result = dispatch_.enumerate_environment_blend_modes(instance_, system_id_, view_type_, blend_count,
				&blend_count, blend_modes.data());
			if (!call_ok(result, "xrEnumerateEnvironmentBlendModes", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			blend_mode_ = blend_modes.front();
			status_.blend_mode = blend_mode_ == XR_ENVIRONMENT_BLEND_MODE_OPAQUE ? "opaque" : "other";

			publish_progress_locked("graphics_requirements", "xrGetD3D11GraphicsRequirementsKHR");
			XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
			result = dispatch_.get_d3d11_graphics_requirements(instance_, system_id_, &requirements);
			if (!call_ok(result, "xrGetD3D11GraphicsRequirementsKHR", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			if (!graphics_requirements_match(requirements, graphics, error))
			{
				status_.state = runtime_state::graphics_mismatch;
				status_.last_error = error;
				set_result(XR_ERROR_GRAPHICS_DEVICE_INVALID);
				(void)teardown_preserving_error(); return false;
			}

			publish_progress_locked("graphics_requirements", "create private D3D11 device");
			d3d11::device_snapshot private_graphics;
			if (!create_private_device(requirements, private_graphics, error))
			{
				status_.state = runtime_state::graphics_mismatch;
				status_.last_error = error;
				set_result(XR_ERROR_GRAPHICS_DEVICE_INVALID);
				(void)teardown_preserving_error(); return false;
			}

			publish_progress_locked("session", "xrCreateSession");
			const XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR, nullptr, private_graphics.device.Get()};
			const XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO, &binding, 0, system_id_};
			result = dispatch_.create_session(instance_, &session_info, &session_);
			if (!call_ok(result, "xrCreateSession", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			status_.session_created = true;

			publish_progress_locked("spaces", "xrCreateReferenceSpace(local)");
			XrReferenceSpaceCreateInfo space_info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
			space_info.poseInReferenceSpace.orientation.w = 1.0f;
			space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
			result = dispatch_.create_reference_space(session_, &space_info, &local_space_);
			if (!call_ok(result, "xrCreateReferenceSpace(local)", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			publish_progress_locked("spaces", "xrCreateReferenceSpace(view)");
			space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
			result = dispatch_.create_reference_space(session_, &space_info, &view_space_);
			if (!call_ok(result, "xrCreateReferenceSpace(view)", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}

			publish_progress_locked("swapchains", "xrEnumerateSwapchainFormats(count)");
			std::uint32_t format_count{};
			result = dispatch_.enumerate_swapchain_formats(session_, 0, &format_count, nullptr);
			if (!call_ok(result, "xrEnumerateSwapchainFormats", runtime_state::runtime_unavailable) || format_count == 0)
			{
				(void)teardown_preserving_error(); return false;
			}
			publish_progress_locked("swapchains", "xrEnumerateSwapchainFormats(data)");
			std::vector<std::int64_t> formats(format_count);
			result = dispatch_.enumerate_swapchain_formats(session_, format_count, &format_count, formats.data());
			if (!call_ok(result, "xrEnumerateSwapchainFormats", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error(); return false;
			}
			const std::array preferred{static_cast<std::int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),
				static_cast<std::int64_t>(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB),
				static_cast<std::int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM)};
			color_format_ = formats.front();
			for (const auto candidate : preferred)
				if (std::ranges::find(formats, candidate) != formats.end()) { color_format_ = candidate; break; }
			status_.color_format = color_format_;
			for (std::size_t index = 0; index < eyes_.size(); ++index)
			{
				publish_progress_locked("swapchains", std::format("create eye swapchain {}", index));
				if (!create_eye_swapchain(dispatch_, session_, private_graphics.device.Get(), color_format_, view_configs_[index],
					eyes_[index], error, result))
				{
					status_.state = runtime_state::runtime_unavailable;
					status_.last_error = error;
					set_result(result);
					(void)teardown_preserving_error(); return false;
				}
				status_.eyes[index].width = eyes_[index].width;
				status_.eyes[index].height = eyes_[index].height;
			}

			graphics_ = private_graphics;
			status_.device_generation = graphics.generation;
			++status_.session_generation;
			status_.applied_enabled = true;
			status_.reinitialize_pending = false;
			status_.state = runtime_state::session_idle;
			publish_progress_locked("complete", "initialization complete");
			status_.last_error.clear();
			set_result(XR_SUCCESS);
			return true;
		}

		bool teardown_preserving_error() noexcept
		{
			const auto state = status_.state;
			const auto error = status_.last_error;
			const auto result = static_cast<XrResult>(status_.last_xr_result);
			const auto result_text = status_.last_xr_result_name;
			const auto stage = status_.last_initialization_stage;
			const auto operation = status_.worker_current_operation;
			const auto completed_operation = status_.worker_last_completed_operation;
			const auto progress_timestamp = status_.worker_last_status_update;
			const bool ok = teardown_locked(false);
			status_.state = state;
			status_.last_error = error;
			status_.last_xr_result = result;
			status_.last_xr_result_name = result_text;
			status_.last_initialization_stage = stage;
			status_.worker_current_operation = operation;
			status_.worker_last_completed_operation = completed_operation;
			status_.worker_last_status_update = progress_timestamp;
			publish_status_locked();
			return ok;
		}

		bool has_objects() const noexcept
		{
			return loader_ != nullptr || instance_ != XR_NULL_HANDLE || session_ != XR_NULL_HANDLE ||
				local_space_ != XR_NULL_HANDLE || view_space_ != XR_NULL_HANDLE ||
				eyes_[0].handle != XR_NULL_HANDLE || eyes_[1].handle != XR_NULL_HANDLE;
		}

		bool teardown_failure(const char* operation, const XrResult result) noexcept
		{
			status_.state = runtime_state::recoverable_error;
			status_.last_error = std::format("{} failed during teardown (XrResult={} {})", operation,
				static_cast<std::int64_t>(result), result_name(result));
			set_result(result);
			status_.applied_enabled = false;
			publish_status_locked();
			return false;
		}

		bool settle_eye(eye_swapchain& eye, XrResult& result) noexcept
		{
			if (!eye.acquired) return true;
			if (!eye.waited)
			{
				const XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, 10'000'000};
				result = dispatch_.wait_swapchain_image(eye.handle, &wait_info);
				if (XR_FAILED(result) || result == XR_TIMEOUT_EXPIRED) return false;
				eye.waited = true;
			}
			return release_acquired_image(dispatch_, eye, result);
		}

		bool teardown_locked(const bool final_shutdown) noexcept
		{
			head_pose_bridge::invalidate_pose();
			if (!has_objects())
			{
				status_.applied_enabled = false;
				status_.session_running = false;
				status_.state = final_shutdown || !status_.desired_enabled ? runtime_state::disabled : status_.state;
				return true;
			}
			++status_.cleanup_count;
			engine_stereo_bridge::invalidate_views();
			compositor_.invalidate();
				capture_.invalidate(status_.device_generation);
			status_.applied_enabled = false;
			XrResult result{XR_SUCCESS};
			for (auto& eye : eyes_)
			{
				if (eye.acquired && !settle_eye(eye, result))
					return teardown_failure(eye.waited ? "xrReleaseSwapchainImage" : "xrWaitSwapchainImage", result);
			}
			if (session_running_ && dispatch_.end_session != nullptr)
			{
				result = dispatch_.end_session(session_);
				if (XR_FAILED(result) && result != XR_ERROR_SESSION_NOT_RUNNING)
					return teardown_failure("xrEndSession", result);
				session_running_ = false;
				status_.session_running = false;
				++status_.session_end_count;
			}
			if (graphics_ && !openxr::wait_for_gpu_idle(graphics_.device.Get(), graphics_.context.Get(), status_.last_error))
				return teardown_failure("D3D11 GPU idle wait", XR_ERROR_RUNTIME_FAILURE);
			for (auto& eye : eyes_)
			{
				if (eye.handle != XR_NULL_HANDLE)
				{
					if (!destroy_eye_swapchain(dispatch_, eye, result))
						return teardown_failure("xrDestroySwapchain", result);
				}
			}
			for (auto* space : {&view_space_, &local_space_})
			{
				if (*space != XR_NULL_HANDLE)
				{
					if (dispatch_.destroy_space == nullptr) return teardown_failure("xrDestroySpace", XR_ERROR_FUNCTION_UNSUPPORTED);
					result = dispatch_.destroy_space(*space);
					if (XR_FAILED(result)) return teardown_failure("xrDestroySpace", result);
					*space = XR_NULL_HANDLE;
				}
			}
			if (session_ != XR_NULL_HANDLE)
			{
				if (dispatch_.destroy_session == nullptr) return teardown_failure("xrDestroySession", XR_ERROR_FUNCTION_UNSUPPORTED);
				result = dispatch_.destroy_session(session_);
				if (XR_FAILED(result)) return teardown_failure("xrDestroySession", result);
				session_ = XR_NULL_HANDLE;
				status_.session_created = false;
			}
			if (instance_ != XR_NULL_HANDLE)
			{
				if (dispatch_.destroy_instance == nullptr && dispatch_.get_instance_proc_addr != nullptr)
				{
					PFN_xrVoidFunction function{};
					result = dispatch_.get_instance_proc_addr(instance_, "xrDestroyInstance", &function);
					if (XR_SUCCEEDED(result) && function != nullptr)
					{
						dispatch_.destroy_instance = reinterpret_cast<PFN_xrDestroyInstance>(function);
					}
					else
					{
						return teardown_failure("xrDestroyInstance",
							XR_FAILED(result) ? result : XR_ERROR_FUNCTION_UNSUPPORTED);
					}
				}
				if (dispatch_.destroy_instance == nullptr)
					return teardown_failure("xrDestroyInstance", XR_ERROR_FUNCTION_UNSUPPORTED);
				result = dispatch_.destroy_instance(instance_);
				if (XR_FAILED(result)) return teardown_failure("xrDestroyInstance", result);
				instance_ = XR_NULL_HANDLE;
				status_.instance_created = false;
			}
			dispatch_.reset();
			if (loader_ != nullptr) { FreeLibrary(loader_); loader_ = nullptr; }
			status_.loader_loaded = false;
			status_.session_created = false;
			status_.instance_created = false;
			status_.session_running = false;
			status_.view_count = 0;
			status_.color_format = 0;
			status_.session_state = XR_SESSION_STATE_UNKNOWN;
			status_.session_state_name = session_state_name(XR_SESSION_STATE_UNKNOWN);
			status_.eyes = {};
			status_.runtime_name.clear();
			status_.system_name.clear();
			graphics_ = {};
			system_id_ = XR_NULL_SYSTEM_ID;
			status_.state = final_shutdown || !status_.desired_enabled ? runtime_state::disabled : runtime_state::waiting_for_graphics;
			return true;
		}

		bool poll_events()
		{
			for (;;)
			{
				XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
				const auto result = dispatch_.poll_event(instance_, &event);
				if (result == XR_EVENT_UNAVAILABLE) return true;
				if (XR_FAILED(result)) return fail(runtime_state::recoverable_error, "xrPollEvent", result);
				if (event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
				const auto& changed = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
				status_.session_state = changed.state;
				status_.session_state_name = session_state_name(changed.state);
				if (changed.state == XR_SESSION_STATE_READY)
				{
					++status_.session_ready_count;
					const XrSessionBeginInfo begin_info{XR_TYPE_SESSION_BEGIN_INFO, nullptr, view_type_};
					const auto begin_result = dispatch_.begin_session(session_, &begin_info);
					if (XR_FAILED(begin_result)) return fail(runtime_state::recoverable_error, "xrBeginSession", begin_result);
					session_running_ = true; status_.session_running = true; ++status_.session_begin_count;
					status_.state = runtime_state::running;
				}
				else if (changed.state == XR_SESSION_STATE_STOPPING)
				{
					++status_.session_stopping_count;
					if (session_running_)
					{
						const auto end_result = dispatch_.end_session(session_);
						if (XR_FAILED(end_result)) return fail(runtime_state::recoverable_error, "xrEndSession", end_result);
						session_running_ = false; status_.session_running = false; ++status_.session_end_count;
					}
					status_.state = runtime_state::session_idle;
				}
				else if (changed.state == XR_SESSION_STATE_EXITING || changed.state == XR_SESSION_STATE_LOSS_PENDING)
				{
					status_.reinitialize_pending = true;
				}
			}
		}

		bool prepare_scene(const d3d11::device_snapshot& graphics, IDXGISwapChain* swap_chain)
		{
			status_.effective_scene_mode = status_.requested_scene_mode;
			status_.last_compositor_error.clear();
			if (status_.requested_scene_mode == scene_mode::synthetic)
			{
				return true;
			}
			if (status_.requested_scene_mode != scene_mode::engine_stereo)
			{
				status_.last_compositor_error = "strict VR requires scene_mode engine_stereo";
				++status_.compositor_source_miss_count;
				status_.state = runtime_state::recoverable_error;
				return false;
			}
			++status_.compositor_prepare_count;
			std::string error;
			if (status_.requested_scene_mode == scene_mode::engine_stereo)
			{
				std::array<captured_frame, 2> pair{};
				if (capture_.acquire_stereo_pair(graphics.device.Get(), status_.device_generation,
					0, pair, error) == stereo_pair_acquire_result::ready)
				{
					std::array<stereo_capture_source, 2> sources{};
					bool valid = true;
					for (std::size_t index{}; index < pair.size(); ++index)
					{
						const auto& capture = pair[index];
						if (capture.texture == nullptr)
						{
							error = "native stereo capture did not produce a shared GPU texture";
							valid = false;
							continue;
						}
						sources[index] = {capture.texture.Get(), capture.description,
							capture.frame_id, capture.device_generation, capture.tag.pair_id,
							capture.tag.eye_index};
					}
					const auto prepared = valid &&
						compositor_.prepare_stereo_pair(graphics, sources, error);
					const auto consumed_pair_id = pair[0].tag.pair_id;
					capture_.release(pair[0]);
					capture_.release(pair[1]);
					if (prepared && compositor_.stereo_source_available(graphics.generation))
					{
						(void)native_render_session::active().release_pair(consumed_pair_id);
						return true;
					}
					native_render_session::active().quarantine_pair(consumed_pair_id);
				}
				status_.last_compositor_error = error.empty()
					? "no complete native stereo capture pair is available" : error;
				++status_.compositor_source_miss_count;
				status_.state = runtime_state::recoverable_error;
				return false;
			}
			(void)swap_chain;
			status_.last_compositor_error = error.empty()
				? "no complete native stereo capture pair is available" : error;
			++status_.compositor_source_miss_count;
			status_.state = runtime_state::recoverable_error;
			return false;
		}

		bool acquire_render_release(eye_swapchain& eye, const std::size_t eye_index,
			const d3d11::device_snapshot& graphics, XrResult& root_result, std::string& root_operation)
		{
			const XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
			auto result = dispatch_.acquire_swapchain_image(eye.handle, &acquire_info, &eye.acquired_index);
			if (XR_FAILED(result)) { root_result = result; root_operation = "xrAcquireSwapchainImage"; return false; }
			eye.acquired = true; ++status_.eyes[eye_index].acquired;
			const XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, 10'000'000};
			result = dispatch_.wait_swapchain_image(eye.handle, &wait_info);
			if (XR_FAILED(result) || result == XR_TIMEOUT_EXPIRED)
			{
				root_result = result; root_operation = "xrWaitSwapchainImage"; return false;
			}
			eye.waited = true;
			if (eye.acquired_index >= eye.render_targets.size())
			{
				root_result = XR_ERROR_RUNTIME_FAILURE; root_operation = "swapchain image index"; return false;
			}
			bool rendered = false;
			std::string compositor_error;
			if (status_.effective_scene_mode == scene_mode::synthetic)
			{
				const float pulse = static_cast<float>((status_.submitted_frames % 253) + 1) / 255.0f;
				const float color[4]{eye_index == 0 ? 1.0f : 0.0f,
					eye_index == 0 ? 0.0f : 1.0f, 1.0f, pulse};
				graphics.context->ClearRenderTargetView(
					eye.render_targets[eye.acquired_index].Get(), color);
				rendered = true;
			}
			else
			{
				rendered = compositor_.render_eye(graphics,
					{eye.render_targets[eye.acquired_index].Get(), eye.width, eye.height,
						projections_[eye_index], false}, compositor_error,
					static_cast<std::uint32_t>(2 + eye_index));
			}
			if (rendered) ++status_.compositor_render_count;
			if (!rendered)
			{
				status_.last_compositor_error = compositor_error.empty()
					? "strict native stereo compositor render failed" : compositor_error;
				root_result = XR_ERROR_RUNTIME_FAILURE;
				root_operation = "native stereo compositor render";
				return false;
			}
			const XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
			result = dispatch_.release_swapchain_image(eye.handle, &release_info);
			if (XR_FAILED(result)) { root_result = result; root_operation = "xrReleaseSwapchainImage"; return false; }
			eye.acquired = false; eye.waited = false; ++status_.eyes[eye_index].released;
			return true;
		}

		void frame(const d3d11::device_snapshot& graphics, IDXGISwapChain* swap_chain)
		{
			XrFrameState frame_state{XR_TYPE_FRAME_STATE};
			const XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
				auto result = dispatch_.wait_frame(session_, &wait_info, &frame_state);
			if (XR_FAILED(result)) { fail(runtime_state::recoverable_error, "xrWaitFrame", result); return; }
			const XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
				result = dispatch_.begin_frame(session_, &begin_info);
			if (XR_FAILED(result)) { fail(runtime_state::recoverable_error, "xrBeginFrame", result); return; }
			if (!frame_state.shouldRender)
			{
				const XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO, nullptr, frame_state.predictedDisplayTime, blend_mode_, 0, nullptr};
				result = dispatch_.end_frame(session_, &end);
				if (XR_FAILED(result)) fail(runtime_state::recoverable_error, "xrEndFrame", result);
				return;
			}

			std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
			XrViewState view_state{XR_TYPE_VIEW_STATE};
			std::uint32_t count{};
			const XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO, nullptr, view_type_, frame_state.predictedDisplayTime, local_space_};
			result = dispatch_.locate_views(session_, &locate, &view_state, static_cast<std::uint32_t>(views.size()), &count, views.data());
			XrResult root_result{result}; std::string root_operation;
			const auto required_view_flags = XR_VIEW_STATE_POSITION_VALID_BIT |
				XR_VIEW_STATE_ORIENTATION_VALID_BIT;
			bool ok = !XR_FAILED(result) && count == 2 &&
				(view_state.viewStateFlags & required_view_flags) == required_view_flags;
			if (!ok)
			{
				root_operation = "xrLocateViews";
				if (XR_SUCCEEDED(root_result)) root_result = XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
			}
			if (ok)
			{
				const auto& orientation = views[0].pose.orientation;
				const float xx = orientation.x * orientation.x;
				const float yy = orientation.y * orientation.y;
				const float zz = orientation.z * orientation.z;
				const float xy = orientation.x * orientation.y;
				const float xz = orientation.x * orientation.z;
				const float yz = orientation.y * orientation.z;
				const float wx = orientation.w * orientation.x;
				const float wy = orientation.w * orientation.y;
				const float wz = orientation.w * orientation.z;
				head_pose_bridge::publish_tracking_pose({
					{
						(views[0].pose.position.x + views[1].pose.position.x) * 0.5f,
						(views[0].pose.position.y + views[1].pose.position.y) * 0.5f,
						(views[0].pose.position.z + views[1].pose.position.z) * 0.5f,
					},
					{{
						{1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy)},
						{2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx)},
						{2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy)},
					}},
				});
				for (std::size_t index{}; index < projections_.size(); ++index)
				{
					const auto& fov = views[index].fov;
					projections_[index] = {
						std::tan(fov.angleLeft),
						std::tan(fov.angleRight),
						std::tan(fov.angleDown),
						std::tan(fov.angleUp),
					};
				}
				status_.effective_scene_mode = status_.requested_scene_mode;
				if (status_.effective_scene_mode == scene_mode::engine_stereo)
				{
					const auto published = engine_stereo_bridge::publish_view_family(status_.submitted_frames + 1,
						{views[0].pose.position.x, views[0].pose.position.y, views[0].pose.position.z},
						{views[1].pose.position.x, views[1].pose.position.y, views[1].pose.position.z},
						projections_);
					if (!published)
					{
						status_.state = runtime_state::fatal_for_vr;
						status_.last_error = "engine stereo bridge rejected the predicted OpenXR view family";
						const XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO, nullptr,
							frame_state.predictedDisplayTime, blend_mode_, 0, nullptr};
						(void)dispatch_.end_frame(session_, &end);
						return;
					}
					if (!prepare_scene(graphics, swap_chain))
					{
						const XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO, nullptr,
							frame_state.predictedDisplayTime, blend_mode_, 0, nullptr};
						(void)dispatch_.end_frame(session_, &end);
						return;
					}
				}
				for (std::size_t index = 0; index < eyes_.size(); ++index)
					if (!acquire_render_release(eyes_[index], index, graphics, root_result, root_operation)) { ok = false; break; }
			}

			if (!ok)
			{
				head_pose_bridge::invalidate_pose();
				for (auto& eye : eyes_)
				{
					XrResult cleanup{XR_SUCCESS};
					(void)settle_eye(eye, cleanup);
				}
				const XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO, nullptr, frame_state.predictedDisplayTime, blend_mode_, 0, nullptr};
				(void)dispatch_.end_frame(session_, &end);
				fail(runtime_state::recoverable_error, root_operation.c_str(), root_result);
				(void)teardown_preserving_error();
				return;
			}

			std::array<XrCompositionLayerProjectionView, 2> projection_views{};
			for (std::size_t index = 0; index < projection_views.size(); ++index)
			{
				auto& projection = projection_views[index];
				projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
				projection.pose = views[index].pose;
				projection.fov = views[index].fov;
				projection.subImage.swapchain = eyes_[index].handle;
				projection.subImage.imageRect.extent = {static_cast<std::int32_t>(eyes_[index].width), static_cast<std::int32_t>(eyes_[index].height)};
			}
			const XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION, nullptr, 0, local_space_,
				static_cast<std::uint32_t>(projection_views.size()), projection_views.data()};
			const XrCompositionLayerBaseHeader* layers[]{reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
			const XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO, nullptr, frame_state.predictedDisplayTime, blend_mode_, 1, layers};
			result = dispatch_.end_frame(session_, &end);
			if (XR_FAILED(result)) { fail(runtime_state::recoverable_error, "xrEndFrame", result); return; }
			++status_.submitted_frames;
			status_.state = runtime_state::running;
		}

		void on_present_locked(const d3d11::device_snapshot& graphics, IDXGISwapChain* swap_chain, std::uint64_t)
		{
			if (!status_.desired_enabled)
			{
				(void)teardown_locked(true); return;
			}
			if (status_.reinitialize_pending || (status_.applied_enabled && status_.device_generation != graphics.generation))
			{
				if (!teardown_locked(false)) return;
				status_.reinitialize_pending = false;
			}
			if (!status_.applied_enabled)
			{
				if (!auto_initialize_allowed_) return;
				auto_initialize_allowed_ = false;
				if (!initialize_locked(graphics)) return;
			}
			if (!poll_events() || !session_running_) return;
			frame(graphics_, swap_chain);
		}

		mutable std::mutex mutex_;
		runtime_status status_{runtime_state::disabled, true};
		HMODULE loader_{};
		dispatch_table dispatch_{};
		XrInstance instance_{XR_NULL_HANDLE};
		XrSystemId system_id_{XR_NULL_SYSTEM_ID};
		XrSession session_{XR_NULL_HANDLE};
		XrSpace local_space_{XR_NULL_HANDLE};
		XrSpace view_space_{XR_NULL_HANDLE};
		std::array<eye_swapchain, 2> eyes_{};
		std::array<engine_stereo_bridge::eye_projection, 2> projections_{};
		std::vector<XrViewConfigurationView> view_configs_;
		d3d11::device_snapshot graphics_{};
		scene_compositor compositor_{};
		frame_capture capture_;
		XrViewConfigurationType view_type_{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
		XrEnvironmentBlendMode blend_mode_{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
		std::int64_t color_format_{};
		bool session_running_{};
		bool auto_initialize_allowed_{true};
		mutable std::mutex status_mutex_;
		runtime_status status_snapshot_{runtime_state::disabled, true};
	};
#else
	class runtime_backend::implementation final
	{
	public:
		implementation() { status_.state = runtime_state::sdk_headers_unavailable; }
		void set_desired_enabled(bool value) { status_.desired_enabled = value; }
		void set_scene_mode(scene_mode mode) { status_.requested_scene_mode = mode; }
		void request_reinitialize() { status_.reinitialize_pending = true; }
		void prepare_frame(const d3d11::device_snapshot&, std::uint64_t) {}
		bool initialize(const d3d11::device_snapshot&) { status_.state = runtime_state::sdk_headers_unavailable; return false; }
		void on_present(const d3d11::device_snapshot&, std::uint64_t) {}
		void on_present(const d3d11::present_event&) {}
		void on_present_post(const d3d11::present_event&, HRESULT) {}
		void capture_present(const d3d11::present_event&) {}
		bool capture_engine_texture(const d3d11::device_snapshot&,
			ID3D11Texture2D*, capture_frame_tag) { return false; }
		void poll_capture(const d3d11::device_snapshot&) {}
		void on_resize_before(const d3d11::resize_event&) noexcept {}
		void on_device_destroying(const d3d11::device_snapshot&) noexcept {}
		void shutdown() noexcept { status_.state = runtime_state::disabled; status_.applied_enabled = false; }
		bool requested_enabled() const { return status_.desired_enabled; }
		bool applied_enabled() const { return false; }
		runtime_status get_status() const { return status_; }
	private: runtime_status status_{};
	};
#endif

	runtime_backend::runtime_backend() : implementation_(std::make_unique<implementation>()) {}
	runtime_backend::~runtime_backend()
	{
		if (implementation_ != nullptr)
		{
			implementation_->shutdown();
		}
	}
	void runtime_backend::set_desired_enabled(const bool enabled) { implementation_->set_desired_enabled(enabled); }
	void runtime_backend::set_scene_mode(const scene_mode mode) { implementation_->set_scene_mode(mode); }
	void runtime_backend::request_reinitialize() { implementation_->request_reinitialize(); }
	void runtime_backend::prepare_frame(const d3d11::device_snapshot& graphics, const std::uint64_t frame_index)
	{
		implementation_->prepare_frame(graphics, frame_index);
	}
	bool runtime_backend::initialize(const d3d11::device_snapshot& graphics) { return implementation_->initialize(graphics); }
	void runtime_backend::on_present(const d3d11::device_snapshot& graphics, const std::uint64_t frame) { implementation_->on_present(graphics, frame); }
	void runtime_backend::on_present(const d3d11::present_event& event) { implementation_->on_present(event); }
	void runtime_backend::on_present_post(const d3d11::present_event& event, const HRESULT result)
	{
		implementation_->on_present_post(event, result);
	}
	void runtime_backend::capture_present(const d3d11::present_event& event) { implementation_->capture_present(event); }
	bool runtime_backend::capture_engine_texture(const d3d11::device_snapshot& graphics,
		ID3D11Texture2D* const source, const capture_frame_tag tag)
	{
		return implementation_->capture_engine_texture(graphics, source, tag);
	}
	void runtime_backend::poll_capture(const d3d11::device_snapshot& graphics) { implementation_->poll_capture(graphics); }
	void runtime_backend::on_resize_before(const d3d11::resize_event& event) noexcept { implementation_->on_resize_before(event); }
	void runtime_backend::on_device_destroying(const d3d11::device_snapshot& graphics) noexcept { implementation_->on_device_destroying(graphics); }
	void runtime_backend::shutdown() noexcept { implementation_->shutdown(); }
	bool runtime_backend::requested_enabled() const { return implementation_->requested_enabled(); }
	bool runtime_backend::applied_enabled() const { return implementation_->applied_enabled(); }
	runtime_status runtime_backend::get_status() const { return implementation_->get_status(); }
}

namespace vr
{
	class runtime::implementation final
	{
	public:
		implementation() : present_owner_execution(backend.requires_present_owner_execution())
		{
			if (present_owner_execution)
			{
				worker_phase = "present_owner";
			}
			else
			{
				worker = std::thread([this] { run(); });
			}
		}

		~implementation()
		{
			stop_worker();
		}

		void set_enabled(const bool enabled)
		{
			if (present_owner_execution)
			{
				backend.set_desired_enabled(enabled);
				return;
			}
			{
				const std::lock_guard lock(mailbox_mutex);
				desired_enabled = enabled;
				++configuration_generation;
				configuration_pending = true;
				wake = true;
			}
			mailbox_cv.notify_one();
		}

		void set_mode(const scene_mode mode)
		{
			if (present_owner_execution)
			{
				backend.set_scene_mode(mode);
				return;
			}
			{
				const std::lock_guard lock(mailbox_mutex);
				desired_scene_mode = mode;
				++configuration_generation;
				configuration_pending = true;
				wake = true;
			}
			mailbox_cv.notify_one();
		}

		void reinitialize()
		{
			if (present_owner_execution)
			{
				backend.request_reinitialize();
				return;
			}
			{
				const std::lock_guard lock(mailbox_mutex);
				reinitialize_pending = true;
				++configuration_generation;
				configuration_pending = true;
				wake = true;
			}
			mailbox_cv.notify_one();
		}

		void capture_present(const d3d11::present_event& event)
		{
			if (present_owner_execution)
			{
				backend.capture_present(event);
				return;
			}
			bool capture_enabled{};
			{
				const std::lock_guard lock(mailbox_mutex);
				capture_enabled = desired_enabled && desired_scene_mode == scene_mode::backbuffer && !stop;
			}
			if (capture_enabled)
			{
				backend.capture_present(event);
			}
		}

		bool capture_engine_texture(const d3d11::device_snapshot& graphics,
			ID3D11Texture2D* const source, const capture_frame_tag tag)
		{
			if (present_owner_execution)
			{
				return backend.capture_engine_texture(graphics, source, tag);
			}
			bool capture_enabled{};
			{
				const std::lock_guard lock(mailbox_mutex);
				capture_enabled = desired_enabled && desired_scene_mode == scene_mode::engine_stereo && !stop;
			}
			if (capture_enabled)
			{
				return backend.capture_engine_texture(graphics, source, tag);
			}
			return false;
		}

		void poll_capture(const d3d11::device_snapshot& graphics)
		{
			if (present_owner_execution)
			{
				backend.poll_capture(graphics);
				return;
			}
			bool capture_enabled{};
			{
				const std::lock_guard lock(mailbox_mutex);
				capture_enabled = desired_enabled && desired_scene_mode != scene_mode::synthetic && !stop;
			}
			if (capture_enabled)
			{
				backend.poll_capture(graphics);
			}
		}

		void present(const d3d11::present_event& event)
		{
			if (present_owner_execution)
			{
				backend.on_present(event);
				return;
			}
			{
				const std::lock_guard lock(mailbox_mutex);
				if (!event.graphics) return;
				if (retiring_generation != 0 && event.graphics.generation <= retiring_generation)
				{
					++worker_dropped_present_count;
					return;
				}
				if (event.graphics.generation > retiring_generation)
				{
					retiring_generation = 0;
				}
			if (has_present) ++worker_dropped_present_count;
				latest_graphics = event.graphics;
			latest_swap_chain = event.swap_chain;
			latest_frame = event.frame_index;
			++present_sequence;
			latest_present_sequence = present_sequence;
			has_present = true;
			wake = true;
			}
			mailbox_cv.notify_one();
		}

		void present(const d3d11::device_snapshot& graphics, const std::uint64_t frame)
		{
			if (present_owner_execution)
			{
				backend.on_present(graphics, frame);
				return;
			}
			d3d11::present_event event{};
			event.graphics = graphics;
			event.frame_index = frame;
			present(event);
		}

		void present_post(const d3d11::present_event& event, const HRESULT result)
		{
			if (present_owner_execution)
			{
				backend.on_present_post(event, result);
				if (backend.shutdown_complete())
				{
					const std::lock_guard lock(mailbox_mutex);
					present_owner_shutdown_complete = true;
					worker_phase = "present_owner_stopped";
				}
			}
		}

		void device_destroying(const d3d11::device_snapshot& graphics) noexcept
		{
			if (present_owner_execution)
			{
				backend.on_device_destroying(graphics);
				return;
			}
			{
				const std::lock_guard lock(mailbox_mutex);
				retiring_generation = graphics.generation;
				latest_graphics = graphics;
				has_device_destroying = true;
				wake = true;
			}
			mailbox_cv.notify_one();
		}

		void resize(const d3d11::resize_event& event) noexcept
		{
			if (present_owner_execution)
			{
				backend.on_resize_before(event);
				return;
			}
			{
				const std::lock_guard lock(mailbox_mutex);
				pending_resize = event;
				has_resize = true;
				wake = true;
			}
			mailbox_cv.notify_one();
		}

		void stop_worker() noexcept
		{
			if (present_owner_execution)
			{
				{
					const std::lock_guard lock(mailbox_mutex);
					if (present_owner_shutdown_complete) return;
					worker_phase = "present_owner_stopping";
				}
				backend.shutdown();
				const auto complete = backend.shutdown_complete();
				{
					const std::lock_guard lock(mailbox_mutex);
					present_owner_shutdown_complete = complete;
					worker_phase = present_owner_shutdown_complete
						? "present_owner_stopped" : "present_owner_waiting_post";
				}
				return;
			}
			if (!worker.joinable())
			{
				return;
			}
			{
				const std::lock_guard lock(mailbox_mutex);
				worker_phase = "stopping";
				stop = true;
				wake = true;
			}
			mailbox_cv.notify_one();
			worker.join();
		}

		bool shutdown_complete() noexcept
		{
			if (present_owner_execution) return backend.shutdown_complete();
			const std::lock_guard lock(mailbox_mutex);
			return !worker.joinable() && !worker_active;
		}

		void run()
		{
			{
				const std::lock_guard lock(mailbox_mutex);
				worker_active = true;
				worker_phase = "waiting";
				worker_last_error.clear();
			}
			try
			{
				for (;;)
				{
				d3d11::device_snapshot graphics;
				std::uint64_t frame{};
				bool destroying{};
				bool apply_configuration{};
				bool apply_reinitialize{};
				bool resize{};
				bool has_present_snapshot{};
				bool enabled{};
				std::uint64_t present_sequence_snapshot{};
				std::uint64_t configuration_generation_snapshot{};
				d3d11::resize_event resize_event{};
				scene_mode mode{};
				Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_reference;
				{

					std::unique_lock lock(mailbox_mutex);
					mailbox_cv.wait(lock, [this] { return wake; });
					++worker_wakeup_count;
					worker_phase = "consuming";
					wake = false;
					if (stop) break;
					if (has_present) ++worker_present_count;
					graphics = latest_graphics;
					swap_chain_reference = latest_swap_chain;
					frame = latest_frame;
					present_sequence_snapshot = latest_present_sequence;
					destroying = has_device_destroying;
					has_present_snapshot = has_present;
					resize = has_resize;
					resize_event = pending_resize;
					apply_configuration = configuration_pending;
					configuration_generation_snapshot = configuration_generation;
					apply_reinitialize = reinitialize_pending;
					enabled = desired_enabled;
					mode = desired_scene_mode;
					has_device_destroying = false;
					has_resize = false;
					has_present = false;
					configuration_pending = false;
					reinitialize_pending = false;
				}
				if (resize)
				{
					backend.on_resize_before(resize_event);
				}
				backend.set_desired_enabled(enabled);
				backend.set_scene_mode(mode);
				if (apply_reinitialize) backend.request_reinitialize();
				if (destroying)
				{
					backend.on_device_destroying(graphics);
					const std::lock_guard lock(mailbox_mutex);
					worker_last_generation = graphics.generation;
					worker_phase = "waiting";
					continue;
				}
				if (!backend.requested_enabled())
				{
					backend.shutdown();
					continue;
				}
				if (has_present_snapshot)
				{
					const auto now = std::chrono::steady_clock::now();
					const auto backend_status = backend.get_status();
					const bool fatal_initialization_block =
						backend_status.state == runtime_state::fatal_for_vr;
					const auto should_initialize = apply_reinitialize ||
						(!backend.applied_enabled() && !fatal_initialization_block &&
							now >= next_initialization_attempt);
					{
						const std::lock_guard lock(mailbox_mutex);
						++worker_backend_call_count;
						worker_graphics_available = static_cast<bool>(graphics);
								worker_current_operation = should_initialize
									? "backend.initialize" : "backend initialization cooldown";
								worker_last_status_update = GetTickCount64();
						worker_phase = should_initialize ? "initializing" : "waiting_for_retry";
					}
					if (should_initialize)
					{
						if (backend.initialize(graphics))
						{
							next_initialization_attempt = {};
						}
						else
						{
							next_initialization_attempt = std::chrono::steady_clock::now() +
								std::chrono::seconds(2);
						}
					}
					if (backend.applied_enabled())
					{
						{
							const std::lock_guard lock(mailbox_mutex);
							worker_phase = "frame";
						}
						backend.on_present(d3d11::present_event{swap_chain_reference.Get(), graphics, frame});
					}
					{
						const std::lock_guard lock(mailbox_mutex);
						worker_last_present_sequence = present_sequence_snapshot;
						worker_last_generation = graphics.generation;
						worker_last_configuration_generation = configuration_generation_snapshot;
						worker_phase = "waiting";
					}
				}
				}
			backend.shutdown();
		}
		catch (const std::exception& error)
			{
				backend.shutdown();
				const std::lock_guard lock(mailbox_mutex);
				worker_last_error = error.what();
			}
			catch (...)
			{
				backend.shutdown();
				const std::lock_guard lock(mailbox_mutex);
				worker_last_error = "Unknown exception in OpenXR worker";
			}
			{
				const std::lock_guard lock(mailbox_mutex);
				worker_active = false;
				worker_phase = worker_last_error.empty() ? "stopped" : "failed";
			}
		}

		runtime_backend backend;
		const bool present_owner_execution;
		std::thread worker;
		std::mutex mailbox_mutex;
		std::condition_variable mailbox_cv;
		d3d11::device_snapshot latest_graphics;
		Microsoft::WRL::ComPtr<IDXGISwapChain> latest_swap_chain;
		std::uint64_t latest_frame{};
		std::uint64_t present_sequence{};
		std::uint64_t latest_present_sequence{};
		std::uint64_t retiring_generation{};
		std::uint64_t configuration_generation{};
		bool desired_enabled{};
		scene_mode desired_scene_mode{scene_mode::backbuffer};
		bool configuration_pending{};
		bool reinitialize_pending{};
		bool has_present{};
		bool has_device_destroying{};
		d3d11::resize_event pending_resize{};
		bool has_resize{};
		bool wake{};
		bool stop{};
		bool worker_active{};
		bool present_owner_shutdown_complete{};
		std::uint64_t worker_wakeup_count{};
		std::uint64_t worker_present_count{};
		std::uint64_t worker_dropped_present_count{};
		std::uint64_t worker_backend_call_count{};
		bool worker_graphics_available{};
		std::string worker_phase{"starting"};
		std::string worker_last_error;
		std::uint64_t worker_last_present_sequence{};
		std::uint64_t worker_last_generation{};
		std::uint64_t worker_last_configuration_generation{};
		std::chrono::steady_clock::time_point next_initialization_attempt{};
			std::uint64_t worker_last_status_update{};
			std::uint64_t worker_watchdog_count{};
			bool worker_progress_stalled{};
			std::string worker_current_operation;
	};

	runtime& runtime::get() { static runtime value; return value; }
	runtime::runtime() : implementation_(std::make_unique<implementation>()) {}
	runtime::~runtime() = default;
	void runtime::set_desired_enabled(const bool enabled) { implementation_->set_enabled(enabled); }
	void runtime::set_scene_mode(const scene_mode mode) { implementation_->set_mode(mode); }
	void runtime::request_reinitialize() { implementation_->reinitialize(); }
	void runtime::prepare_frame(const d3d11::device_snapshot& graphics, const std::uint64_t frame_index)
	{
		implementation_->backend.prepare_frame(graphics, frame_index);
	}
	bool runtime::initialize(const d3d11::device_snapshot& graphics) { return implementation_->backend.initialize(graphics); }
	void runtime::on_present(const d3d11::device_snapshot& graphics, const std::uint64_t frame) { implementation_->present(graphics, frame); }
	void runtime::on_present(const d3d11::present_event& event) { implementation_->present(event); }
	void runtime::on_present_post(const d3d11::present_event& event, const HRESULT result)
	{
		implementation_->present_post(event, result);
	}
	void runtime::capture_present(const d3d11::present_event& event) { implementation_->capture_present(event); }
	bool runtime::capture_engine_texture(const d3d11::device_snapshot& graphics,
		ID3D11Texture2D* const source, const capture_frame_tag tag)
	{
		return implementation_->capture_engine_texture(graphics, source, tag);
	}
	void runtime::poll_capture(const d3d11::device_snapshot& graphics) { implementation_->poll_capture(graphics); }
	void runtime::on_resize_before(const d3d11::resize_event& event) noexcept { implementation_->resize(event); }
	void runtime::on_device_destroying(const d3d11::device_snapshot& graphics) noexcept { implementation_->device_destroying(graphics); }
	void runtime::shutdown() noexcept { implementation_->stop_worker(); }
	bool runtime::shutdown_complete() noexcept { return implementation_->shutdown_complete(); }
	bool runtime::requested_enabled() const { return implementation_->backend.requested_enabled(); }
	bool runtime::applied_enabled() const { return implementation_->backend.applied_enabled(); }
	runtime_status runtime::get_status() const
	{
		auto status = implementation_->backend.get_status();
		const std::lock_guard lock(implementation_->mailbox_mutex);
		status.worker_active = implementation_->worker_active;
		status.worker_wakeup_count = implementation_->worker_wakeup_count;
		status.worker_present_count = implementation_->worker_present_count;
		status.worker_dropped_present_count = implementation_->worker_dropped_present_count;
		status.worker_backend_call_count = implementation_->worker_backend_call_count;
		status.worker_graphics_available = implementation_->worker_graphics_available;
		status.worker_phase = implementation_->worker_phase;
		status.worker_last_error = implementation_->worker_last_error;
		status.worker_last_present_sequence = implementation_->worker_last_present_sequence;
		status.worker_last_generation = implementation_->worker_last_generation;
		status.worker_last_configuration_generation = implementation_->worker_last_configuration_generation;
			status.worker_last_status_update = std::max(status.worker_last_status_update, implementation_->worker_last_status_update);
			if (status.worker_current_operation.empty())
			{
				status.worker_current_operation = implementation_->worker_current_operation;
			}
			if (status.worker_last_status_update != 0)
			{
				const auto now = GetTickCount64();
				status.worker_progress_age_ms = now >= status.worker_last_status_update
					? now - status.worker_last_status_update : 0;
				status.worker_progress_stalled = status.worker_active &&
					status.worker_phase == "initializing" && status.worker_progress_age_ms > 10'000;
				status.worker_watchdog_count = implementation_->worker_watchdog_count;
			}
			return status;
	}
}
