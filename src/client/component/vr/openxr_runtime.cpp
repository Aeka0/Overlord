#include <std_include.hpp>

#include "openxr_runtime.hpp"
#include "product.hpp"

#include "engine_stereo_bridge.hpp"
#include "frame_capture.hpp"
#include "head_pose_bridge.hpp"
#include "native_render_session.hpp"
#include "openxr_layer_policy.hpp"
#include "openxr_d3d11.hpp"
#include "runtime_backend.hpp"
#include "scene_compositor.hpp"
#include "openxr_input.hpp"
#include "openxr_menu.hpp"
#include "native_stereo_source.hpp"
#include "present_transaction.hpp"
#include "pose_filter.hpp"
#include "touch_controller_reference.hpp"
#include "engine_scene_resolution.hpp"
#include "movie_presentation.hpp"
#include "presentation_options.hpp"

#include <d3d11_4.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <mutex>
#include <vector>

namespace vr::openxr
{
#if H2V_OPENXR_HEADERS_AVAILABLE
	namespace
	{
		const char* result_name(const XrResult result) noexcept
		{
#define H2V_XR_RESULT(value)                                                                                 \
	case value:                                                                                              \
		return #value
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
			default:
				return "XR_UNKNOWN_RESULT";
			}
#undef H2V_XR_RESULT
		}

		head_pose_bridge::reference_policy head_reference_policy(XrSessionState session,
		                                                       XrViewStateFlags views,
		                                                       XrSpaceLocationFlags head) noexcept
		{
			constexpr auto tracked_views = XR_VIEW_STATE_POSITION_TRACKED_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
			constexpr auto tracked_head = XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
			const bool ready = session == XR_SESSION_STATE_FOCUSED &&
			                   (views & tracked_views) == tracked_views && (head & tracked_head) == tracked_head;
			return ready ? head_pose_bridge::reference_policy::allow_recenter
			             : head_pose_bridge::reference_policy::retain_reference;
		}

		const char* session_state_name(const XrSessionState state) noexcept
		{
			switch (state)
			{
			case XR_SESSION_STATE_UNKNOWN:
				return "XR_SESSION_STATE_UNKNOWN";
			case XR_SESSION_STATE_IDLE:
				return "XR_SESSION_STATE_IDLE";
			case XR_SESSION_STATE_READY:
				return "XR_SESSION_STATE_READY";
			case XR_SESSION_STATE_SYNCHRONIZED:
				return "XR_SESSION_STATE_SYNCHRONIZED";
			case XR_SESSION_STATE_VISIBLE:
				return "XR_SESSION_STATE_VISIBLE";
			case XR_SESSION_STATE_FOCUSED:
				return "XR_SESSION_STATE_FOCUSED";
			case XR_SESSION_STATE_STOPPING:
				return "XR_SESSION_STATE_STOPPING";
			case XR_SESSION_STATE_LOSS_PENDING:
				return "XR_SESSION_STATE_LOSS_PENDING";
			case XR_SESSION_STATE_EXITING:
				return "XR_SESSION_STATE_EXITING";
			default:
				return "XR_SESSION_STATE_UNKNOWN_VALUE";
			}
		}
	}

	class runtime_backend::implementation final
	{
		enum class world_submission
		{
			omit,
			include
		};
		enum class publication_policy
		{
			invalidate,
			retain_bootstrap
		};
		enum class eye_content
		{
			native_texture,
			diagnostic_color
		};
		struct prediction
		{
			bool sdk_frame_open{}, views_valid{}, native_pair_admitted{}, ui_only{}, menu_layers_ready{};
			unsigned pending_presents{};
			std::uint64_t pair{};
			XrFrameState state{};
			std::array<XrView, 2> views{};
			head_pose_bridge::tracking_pose head;
		} prediction_;
		present_transaction::key present_;
		std::uint64_t owner_generation_{};
		std::uint32_t owner_thread_{};
		float pause_dim_{};
		XrTime reference_change_{};
		bool cylinder_supported_{};
		DXGI_FORMAT menu_format_{};
		input_actions inputs_;

		menu_layers menus_;
		texture_blit::renderer blit_;

		startup_query startup_query_{};

	  public:
		explicit implementation(startup_query query)
		    : startup_query_(query)
		{
		}

		~implementation()
		{
			shutdown();
		}

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
		                            ID3D11Texture2D* const source,
		                            const capture_frame_tag tag)
		{
			if (!capture_.produce_texture(graphics, source, tag))
				return false;
			if (!tag.native)
				return true;
			if (native_render_session::active().complete_rendered_eye(tag.pair_id, tag.eye_index, source))
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

		void prepare_frame(const d3d11::device_snapshot&, std::uint64_t)
		{
			const std::lock_guard lock(mutex_);
			status_.direct_renderer_thread_id = GetCurrentThreadId();
			publish_status_locked();
		}

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
				status_.worker_last_completed_operation =
				    failed_operation.empty() ? "backend.initialize (failed)" : failed_operation + " (failed)";
				status_.worker_current_operation = "idle";
				status_.worker_last_status_update = GetTickCount64();
				publish_status_locked();
			}
			return initialized;
		}

		void on_present(const d3d11::present_event& event)
		{
			const std::lock_guard lock(mutex_);
			if (present_.active || d3d11::is_inside_present_gpu_scope())
			{
				fail(runtime_state::fatal_for_vr, "OpenXR Present ownership", XR_ERROR_CALL_ORDER_INVALID);
				publish_status_locked();
				return;
			}
			if (present_transaction::owner_changed(status_.applied_enabled,
			                                       status_.reinitialize_pending,
			                                       owner_generation_,
			                                       owner_thread_,
			                                       event.graphics.generation,
			                                       GetCurrentThreadId()))
			{
				fail(
				    runtime_state::fatal_for_vr, "OpenXR Present owner changed", XR_ERROR_CALL_ORDER_INVALID);
				publish_status_locked();
				return;
			}
			present_ = {true, event.frame_index, event.graphics.generation, GetCurrentThreadId()};
			++status_.present_owner_pre_count;
			status_.present_owner_transaction_active = true;
			status_.present_owner_transaction_thread_id = GetCurrentThreadId();
			if (maintain_session(event.graphics) && prediction_.sdk_frame_open)
			{
				if (status_.requested_scene_mode == scene_mode::synthetic)
					(void)render_synthetic();
				else
					(void)complete_native(event.swap_chain);
			}
			publish_status_locked();
		}
		void on_present_post(const d3d11::present_event& event, HRESULT result)
		{
			const std::lock_guard lock(mutex_);
			++status_.present_owner_post_count;
			const auto match = present_transaction::validate(present_, event, GetCurrentThreadId(), result);
			present_ = {};
			status_.present_owner_transaction_active = false;
			status_.present_owner_transaction_thread_id = 0;
			status_.present_owner_last_post_hresult = result;
				if (match != present_transaction::validation::matched || d3d11::is_inside_present_gpu_scope())
				{
					inputs_.invalidate(controller_input::input_reason::present_mismatch,XR_ERROR_CALL_ORDER_INVALID);
				head_pose_bridge::invalidate_pose();
				engine_stereo_bridge::invalidate_views();
				fail(
				    runtime_state::fatal_for_vr, "OpenXR Present-post mismatch", XR_ERROR_CALL_ORDER_INVALID);
				publish_status_locked();
				return;
			}
			status_.direct_present_owner_contract_valid = true;
			status_.present_owner_last_completed_frame = event.frame_index;
			if (status_.desired_enabled && status_.applied_enabled && !prediction_.sdk_frame_open &&
			    status_.state != runtime_state::fatal_for_vr && poll_events() && session_running_)
			{
				(void)begin_prediction(event.frame_index);
			}
			publish_status_locked();
		}
		// CPU/WARP fixture entry: never substitutes a virtual Present for H2 stereo.
		void on_present(const d3d11::device_snapshot& graphics, std::uint64_t frame)
		{
			const std::lock_guard lock(mutex_);
			if (status_.requested_scene_mode != scene_mode::synthetic)
			{
				fail(runtime_state::fatal_for_vr,
				     "native OpenXR requires real Present pre/post",
				     XR_ERROR_CALL_ORDER_INVALID);
				publish_status_locked();
				return;
			}
			if (maintain_session(graphics) && !prediction_.sdk_frame_open && begin_prediction(frame))
				(void)render_synthetic();
			publish_status_locked();
		}

		void on_resize_before(const d3d11::resize_event& event) noexcept
		{
			// Resize only records recovery intent. The next Present owner retires
			// SDK and GPU resources, outside the resize callback's call stack.
			resize_generation_.store(event.graphics.generation, std::memory_order_release);
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

		bool shutdown_complete() const noexcept
		{
			const std::lock_guard lock(mutex_);
			return !has_objects();
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
			status_.last_error = std::format("{} failed (XrResult={} {})",
			                                 operation,
			                                 static_cast<std::int64_t>(result),
			                                 result_name(result));
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

			// Selection and static controller metadata share one short OpenVR query,
			// after teardown and before any OpenXR loader/instance/session exists.
			publish_progress_locked("runtime_selection", "identify the connected runtime");
			auto startup = startup_query_ ? startup_query_() : startup_configuration{};
			auto controller_reference = std::move(startup.controller_reference);
			const auto preference = apply_runtime_preference(startup.preferred_runtime, startup.preference_source);
			const auto restore_preference = gsl::finally([&] { restore_runtime_preference(preference); });
			status_.runtime_override_active = preference.override_active;
			status_.runtime_override_set_by_policy = preference.override_set_by_policy;
			status_.runtime_override_source = preference.source;
			status_.runtime_override_manifest = preference.manifest_path;
			status_.runtime_manifest = preference.override_active ? preference.manifest_path : startup.runtime_manifest;
			status_.runtime_library = status_.runtime_manifest == startup.runtime_manifest ? startup.runtime_library : "";
			status_.runtime_selection_diagnostic = startup.selection_diagnostic;
			if (!preference.warning.empty())
				status_.runtime_selection_diagnostic += "; " + preference.warning;
			if (!preference.blocking_error.empty())
			{
				status_.state = runtime_state::runtime_unavailable;
				status_.last_error = preference.blocking_error;
				return false;
			}
			if (!startup.preferred_runtime.empty() && !preference.override_active)
			{
				status_.state = runtime_state::runtime_unavailable;
				status_.last_error = "selected OpenXR runtime became unavailable: " + preference.warning;
				return false;
			}

			publish_progress_locked("layer_policy", "isolate incompatible implicit OpenXR layers");
			const auto layer_policy = apply_native_implicit_layer_policy();
			status_.implicit_layer_policy_applied = layer_policy.applied;
			status_.implicit_layer_manifest_count = static_cast<std::uint32_t>(layer_policy.manifest_count);
			status_.implicit_layers_disabled =
			    static_cast<std::uint32_t>(layer_policy.disabled_layers.size());
			status_.disabled_implicit_layers.clear();
			for (const auto& layer : layer_policy.disabled_layers)
			{
				if (!status_.disabled_implicit_layers.empty())
					status_.disabled_implicit_layers += ", ";
				status_.disabled_implicit_layers += layer;
			}
			status_.implicit_layer_policy_warning.clear();
			for (const auto& warning : layer_policy.warnings)
			{
				if (!status_.implicit_layer_policy_warning.empty())
					status_.implicit_layer_policy_warning += "; ";
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
				if (module == nullptr)
					return;
				for (;;)
				{
					const auto length = GetModuleFileNameW(
					    module, module_buffer.data(), static_cast<DWORD>(module_buffer.size()));
					if (length == 0)
						return;
					if (length < module_buffer.size() - 1)
					{
						const std::wstring module_path(module_buffer.data(), length);
						const auto separator = module_path.find_last_of(L"\\/");
						if (separator != std::wstring::npos)
						{
							const auto loader_path =
							    module_path.substr(0, separator + 1) + L"openxr_loader.dll";
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
			                         reinterpret_cast<LPCWSTR>(&result_name),
			                         &current_module);
			append_module_loader_path(current_module);
			append_module_loader_path(nullptr);
			const auto last_error_before_load = GetLastError();
			for (const auto& loader_path : loader_paths)
			{
				publish_progress_locked("loader", "LoadLibraryExW candidate");
				loader_ = LoadLibraryExW(loader_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
				if (loader_ != nullptr)
					break;
			}
			if (loader_ == nullptr)
			{
				const auto load_error = GetLastError();
				SetLastError(last_error_before_load);
				status_.state = runtime_state::loader_missing;
				status_.last_error = std::format(
				    "openxr_loader.dll was not loadable beside the active overlord/game modules (Win32={})",
				    load_error);
				return false;
			}
			status_.loader_loaded = true;
			publish_progress_locked("loader", "resolve xrGetInstanceProcAddr");
			auto entry =
			    reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader_, "xrGetInstanceProcAddr"));
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
			if (!call_ok(
			        result, "xrEnumerateInstanceExtensionProperties", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			publish_progress_locked("extensions", "xrEnumerateInstanceExtensionProperties(data)");
			std::vector<XrExtensionProperties> extensions(extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
			result = dispatch_.enumerate_instance_extension_properties(
			    nullptr, extension_count, &extension_count, extensions.data());
			if (!call_ok(
			        result, "xrEnumerateInstanceExtensionProperties", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			const auto supports_d3d11 = std::ranges::any_of(
			    extensions,
			    [](const auto& item)
			    { return std::strcmp(item.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0; });
			if (!supports_d3d11)
			{
				fail(runtime_state::runtime_unavailable,
				     "XR_KHR_D3D11_enable",
				     XR_ERROR_EXTENSION_NOT_PRESENT);
				(void)teardown_preserving_error();
				return false;
			}

			publish_progress_locked("instance", "xrCreateInstance");
			constexpr auto requested_api_version = XR_MAKE_VERSION(1, 0, 0);
			cylinder_supported_ = std::ranges::any_of(
			    extensions,
			    [](const auto& e) {
				    return std::strcmp(e.extensionName, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME) ==
				           0;
			    });
			const char* enabled_extensions[]{XR_KHR_D3D11_ENABLE_EXTENSION_NAME,
			                                 XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME};
			XrInstanceCreateInfo instance_info{XR_TYPE_INSTANCE_CREATE_INFO};
			strcpy_s(instance_info.applicationInfo.applicationName, product::name);
			instance_info.applicationInfo.applicationVersion = 1;
			strcpy_s(instance_info.applicationInfo.engineName, product::name);
			instance_info.applicationInfo.engineVersion = 1;
			instance_info.applicationInfo.apiVersion = requested_api_version;
			instance_info.enabledExtensionCount = cylinder_supported_ ? 2 : 1;
			instance_info.enabledExtensionNames = enabled_extensions;
			result = dispatch_.create_instance(&instance_info, &instance_);
			if (!call_ok(result, "xrCreateInstance", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			status_.instance_created = true;
			publish_progress_locked("instance", "load instance OpenXR dispatch");
			if (!dispatch_.load_instance(instance_, error, result))
			{
				status_.state = runtime_state::runtime_unavailable;
				status_.last_error = error;
				set_result(result);
				(void)teardown_preserving_error();
				return false;
			}

			publish_progress_locked("instance", "xrGetInstanceProperties");
			XrInstanceProperties instance_properties{XR_TYPE_INSTANCE_PROPERTIES};
			result = dispatch_.get_instance_properties(instance_, &instance_properties);
			if (!call_ok(result, "xrGetInstanceProperties", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			status_.runtime_name = instance_properties.runtimeName;
			status_.last_runtime_name = status_.runtime_name;

			publish_progress_locked("system", "xrGetSystem");
			const XrSystemGetInfo system_info{
			    XR_TYPE_SYSTEM_GET_INFO, nullptr, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
			result = dispatch_.get_system(instance_, &system_info, &system_id_);
			if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
			{
				const auto unavailable_headset = std::format(
				    "xrGetSystem: {} has no available headset; check the connection and selected runtime, "
				    "then use vr_reinit after reconnecting", status_.runtime_name);
				fail(runtime_state::no_hmd, unavailable_headset.c_str(), result);
				(void)teardown_preserving_error();
				return false;
			}
			if (!call_ok(result, "xrGetSystem", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			publish_progress_locked("system", "xrGetSystemProperties");
			XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES};
			result = dispatch_.get_system_properties(instance_, system_id_, &system_properties);
			if (!call_ok(result, "xrGetSystemProperties", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			status_.system_name = system_properties.systemName;
			status_.menu_surface_mode = cylinder_supported_ ? "cylinder" : "quad";
			if (system_properties.graphicsProperties.maxLayerCount < menu_surface::surface_count + 1)
			{
				fail(runtime_state::runtime_unavailable,
				     "OpenXR menu layer capacity",
				     XR_ERROR_LAYER_LIMIT_EXCEEDED);
				(void)teardown_preserving_error();
				return false;
			}
			status_.last_system_name = status_.system_name;

			publish_progress_locked("views", "xrEnumerateViewConfigurationViews(count)");
			std::uint32_t view_count{};
			result = dispatch_.enumerate_view_configuration_views(
			    instance_, system_id_, view_type_, 0, &view_count, nullptr);
			if (!call_ok(result, "xrEnumerateViewConfigurationViews", runtime_state::runtime_unavailable) ||
			    view_count != 2)
			{
				if (!XR_FAILED(result))
					fail(runtime_state::runtime_unavailable, "stereo view count", XR_ERROR_RUNTIME_FAILURE);
				(void)teardown_preserving_error();
				return false;
			}
			publish_progress_locked("views", "xrEnumerateViewConfigurationViews(data)");
			view_configs_.assign(view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
			result = dispatch_.enumerate_view_configuration_views(
			    instance_, system_id_, view_type_, view_count, &view_count, view_configs_.data());
			if (!call_ok(result, "xrEnumerateViewConfigurationViews", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			status_.view_count = view_count;

			publish_progress_locked("views", "xrEnumerateEnvironmentBlendModes(count)");
			std::uint32_t blend_count{};
			result = dispatch_.enumerate_environment_blend_modes(
			    instance_, system_id_, view_type_, 0, &blend_count, nullptr);
			if (!call_ok(result, "xrEnumerateEnvironmentBlendModes", runtime_state::runtime_unavailable) ||
			    blend_count == 0)
			{
				(void)teardown_preserving_error();
				return false;
			}
			publish_progress_locked("views", "xrEnumerateEnvironmentBlendModes(data)");
			std::vector<XrEnvironmentBlendMode> blend_modes(blend_count);
			result = dispatch_.enumerate_environment_blend_modes(
			    instance_, system_id_, view_type_, blend_count, &blend_count, blend_modes.data());
			if (!call_ok(result, "xrEnumerateEnvironmentBlendModes", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			blend_mode_ = blend_modes.front();
			status_.blend_mode = blend_mode_ == XR_ENVIRONMENT_BLEND_MODE_OPAQUE ? "opaque" : "other";

			publish_progress_locked("graphics_requirements", "xrGetD3D11GraphicsRequirementsKHR");
			XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
			result = dispatch_.get_d3d11_graphics_requirements(instance_, system_id_, &requirements);
			if (!call_ok(result, "xrGetD3D11GraphicsRequirementsKHR", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			if (!graphics_requirements_match(requirements, graphics, error))
			{
				status_.state = runtime_state::graphics_mismatch;
				status_.last_error = error;
				set_result(XR_ERROR_GRAPHICS_DEVICE_INVALID);
				(void)teardown_preserving_error();
				return false;
			}

			publish_progress_locked("graphics_requirements", "bind the game D3D11 device");
			publish_progress_locked("session", "xrCreateSession");
			const XrGraphicsBindingD3D11KHR binding{
			    XR_TYPE_GRAPHICS_BINDING_D3D11_KHR, nullptr, graphics.device.Get()};
			const XrSessionCreateInfo session_info{XR_TYPE_SESSION_CREATE_INFO, &binding, 0, system_id_};
			result = dispatch_.create_session(instance_, &session_info, &session_);
			if (!call_ok(result, "xrCreateSession", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			status_.session_created = true;

			publish_progress_locked("spaces", "xrCreateReferenceSpace(local)");
			XrReferenceSpaceCreateInfo space_info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
			space_info.poseInReferenceSpace.orientation.w = 1.0f;
			space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
			result = dispatch_.create_reference_space(session_, &space_info, &local_space_);
			if (!call_ok(result, "xrCreateReferenceSpace(local)", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			publish_progress_locked("spaces", "xrCreateReferenceSpace(view)");
			space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
			result = dispatch_.create_reference_space(session_, &space_info, &view_space_);
			if (!call_ok(result, "xrCreateReferenceSpace(view)", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}

			publish_progress_locked("swapchains", "xrEnumerateSwapchainFormats(count)");
			std::uint32_t format_count{};
			result = dispatch_.enumerate_swapchain_formats(session_, 0, &format_count, nullptr);
			if (!call_ok(result, "xrEnumerateSwapchainFormats", runtime_state::runtime_unavailable) ||
			    format_count == 0)
			{
				(void)teardown_preserving_error();
				return false;
			}
			publish_progress_locked("swapchains", "xrEnumerateSwapchainFormats(data)");
			std::vector<std::int64_t> formats(format_count);
			result =
			    dispatch_.enumerate_swapchain_formats(session_, format_count, &format_count, formats.data());
			if (!call_ok(result, "xrEnumerateSwapchainFormats", runtime_state::runtime_unavailable))
			{
				(void)teardown_preserving_error();
				return false;
			}
			constexpr std::array preferred{std::int64_t(DXGI_FORMAT_R16G16B16A16_FLOAT),
			                               std::int64_t(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),
			                               std::int64_t(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB),
			                               std::int64_t(DXGI_FORMAT_R8G8B8A8_UNORM)};
			color_format_ = 0;
			for (auto candidate : preferred)
				if (std::ranges::find(formats, candidate) != formats.end())
				{
					color_format_ = candidate;
					break;
				}
			if (!color_format_)
			{
				fail(runtime_state::runtime_unavailable,
				     "OpenXR color format",
				     XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED);
				(void)teardown_preserving_error();
				return false;
			}
			menu_format_ = DXGI_FORMAT(color_format_);
			for (auto candidate : {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB})
				if (std::ranges::find(formats, std::int64_t(candidate)) != formats.end())
				{
					menu_format_ = candidate;
					break;
				}
			unsigned width = 0, height = 0;
			for (const auto& view : view_configs_)
			{
				width = (std::max)(width, view.recommendedImageRectWidth);
				height = (std::max)(height, view.recommendedImageRectHeight);
			}
			for (auto& view : view_configs_)
			{
				if (width > view.maxImageRectWidth || height > view.maxImageRectHeight)
				{
					fail(runtime_state::runtime_unavailable,
					     "common OpenXR eye extent",
					     XR_ERROR_LIMIT_REACHED);
					(void)teardown_preserving_error();
					return false;
				}
				view.recommendedImageRectWidth = width;
				view.recommendedImageRectHeight = height;
			}
			status_.recommended_eye_width = width;
			status_.recommended_eye_height = height;
			if (status_.requested_scene_mode == scene_mode::engine_stereo &&
			    !engine_scene_resolution::request({width, height}, error))
			{
				status_.native_renderer_error = error;
				fail(runtime_state::fatal_for_vr, error.c_str(), XR_ERROR_RUNTIME_FAILURE);
				(void)teardown_preserving_error();
				return false;
			}
			status_.color_format = color_format_;
			for (std::size_t index = 0; index < eyes_.size(); ++index)
			{
				publish_progress_locked("swapchains", std::format("create eye swapchain {}", index));
				if (!create_eye_swapchain(dispatch_,
				                          session_,
				                          graphics.device.Get(),
				                          color_format_,
				                          view_configs_[index],
				                          eyes_[index],
				                          error,
				                          result))
				{
					status_.state = runtime_state::runtime_unavailable;
					status_.last_error = error;
					set_result(result);
					(void)teardown_preserving_error();
					return false;
				}
				status_.eyes[index].width = eyes_[index].width;
				status_.eyes[index].height = eyes_[index].height;
			}

			graphics_ = graphics;
			status_.device_generation = graphics.generation;
			++status_.session_generation;
			publish_progress_locked("input", "create and attach OpenXR action sets");
				if (!inputs_.initialize(dispatch_, instance_, session_, result, error))
				{
					inputs_.invalidate(controller_input::input_reason::initialization_failed,result);
				fail(runtime_state::runtime_unavailable, error.c_str(), result);
				(void)teardown_preserving_error();
				return false;
			}
			apply_controller_reference(std::move(controller_reference));
			// A new LOCAL space cannot reuse a reference from an older session.
			head_pose_bridge::request_recenter();
			status_.controller_input_ready = true;
			native_menu::set_requested(true);
			status_.submission_on_game_device = true;
			status_.graphics_transport = "h2_device_openxr";
			status_.native_renderer_ready = false;
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
			status_.last_error = std::format("{} failed during teardown (XrResult={} {})",
			                                 operation,
			                                 static_cast<std::int64_t>(result),
			                                 result_name(result));
			set_result(result);
			status_.applied_enabled = false;
			publish_status_locked();
			return false;
		}

		bool settle_eye(eye_swapchain& eye, XrResult& result) noexcept
		{
			if (!eye.acquired)
				return true;
			if (!eye.waited)
			{
				const XrSwapchainImageWaitInfo wait_info{
				    XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, 10'000'000};
				result = dispatch_.wait_swapchain_image(eye.handle, &wait_info);
				if (XR_FAILED(result) || result == XR_TIMEOUT_EXPIRED)
					return false;
				eye.waited = true;
			}
			return release_acquired_image(dispatch_, eye, result);
		}

		bool teardown_locked(const bool final_shutdown) noexcept
		{
			inputs_.invalidate();
			status_.controller_input_ready = false;
			native_menu::set_requested(false);
			pause_dim_ = 0;
			head_pose_bridge::invalidate_pose();
			if (prediction_.sdk_frame_open)
			{
				prediction_.menu_layers_ready = false;
				(void)finish_prediction(world_submission::omit);
			}
			if (!has_objects())
			{
				session_running_ = false;
				status_.applied_enabled = false;
				status_.session_running = false;
				status_.state =
				    final_shutdown || !status_.desired_enabled ? runtime_state::disabled : status_.state;
				return true;
			}
			++status_.cleanup_count;
			engine_stereo_bridge::invalidate_views();
			compositor_.invalidate();
			capture_.invalidate(status_.device_generation);
			status_.applied_enabled = false;
			if (graphics_ && !openxr::wait_for_gpu_idle(
			                     graphics_.device.Get(), graphics_.context.Get(), status_.last_error))
				return teardown_failure("D3D11 GPU idle wait", XR_ERROR_RUNTIME_FAILURE);
			XrResult result{XR_SUCCESS};
			const auto menu_cleanup = menus_.destroy(dispatch_);
			if (!menu_cleanup)
				return teardown_failure(menu_cleanup.operation, menu_cleanup.code);
			const auto action_cleanup = inputs_.destroy(dispatch_);
			if (!action_cleanup)
				return teardown_failure(action_cleanup.operation, action_cleanup.code);
			for (auto& eye : eyes_)
			{
				if (eye.acquired && !settle_eye(eye, result))
					return teardown_failure(eye.waited ? "xrReleaseSwapchainImage" : "xrWaitSwapchainImage",
					                        result);
			}
			// xrEndSession is only legal after STOPPING. Explicit teardown already
			// owns the session mutex and retires GPU use; xrDestroySession is legal
			// from any state once no thread can use the handle.
			if (session_running_ && session_state_ == XR_SESSION_STATE_STOPPING &&
			    dispatch_.end_session != nullptr)
			{
				result = dispatch_.end_session(session_);
				// EndSession always transitions to not-running, including errors.
				session_running_ = false;
				status_.session_running = false;
				if (XR_FAILED(result) && result != XR_ERROR_SESSION_NOT_RUNNING)
					return teardown_failure("xrEndSession", result);
				++status_.session_end_count;
			}

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
					if (dispatch_.destroy_space == nullptr)
						return teardown_failure("xrDestroySpace", XR_ERROR_FUNCTION_UNSUPPORTED);
					result = dispatch_.destroy_space(*space);
					if (XR_FAILED(result))
						return teardown_failure("xrDestroySpace", result);
					*space = XR_NULL_HANDLE;
				}
			}
			if (session_ != XR_NULL_HANDLE)
			{
				if (dispatch_.destroy_session == nullptr)
					return teardown_failure("xrDestroySession", XR_ERROR_FUNCTION_UNSUPPORTED);
				result = dispatch_.destroy_session(session_);
				if (XR_FAILED(result))
					return teardown_failure("xrDestroySession", result);
				session_ = XR_NULL_HANDLE;
				session_running_ = false;
				status_.session_running = false;
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
				if (XR_FAILED(result))
					return teardown_failure("xrDestroyInstance", result);
				instance_ = XR_NULL_HANDLE;
				status_.instance_created = false;
			}
			dispatch_.reset();
			if (loader_ != nullptr)
			{
				FreeLibrary(loader_);
				loader_ = nullptr;
			}
			status_.loader_loaded = false;
			status_.session_created = false;
			status_.instance_created = false;
			status_.session_running = false;
			status_.view_count = 0;
			status_.color_format = 0;
			session_state_ = XR_SESSION_STATE_UNKNOWN;
			status_.session_state = static_cast<std::int32_t>(session_state_);
			status_.session_state_name = session_state_name(XR_SESSION_STATE_UNKNOWN);
			status_.eyes = {};
			status_.runtime_name.clear();
			status_.system_name.clear();
			if (status_.native_renderer_ready)
				native_render_session::active().invalidate(status_.device_generation);
			blit_.reset();
			status_.native_renderer_ready = false;
			status_.submission_on_game_device = false;
			graphics_ = {};
			system_id_ = XR_NULL_SYSTEM_ID;
			status_.state = final_shutdown || !status_.desired_enabled ? runtime_state::disabled
			                                                           : runtime_state::waiting_for_graphics;
			return true;
		}

		bool poll_events()
		{
			for (;;)
			{
				XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
				const auto result = dispatch_.poll_event(instance_, &event);
				if (result == XR_EVENT_UNAVAILABLE)
					return true;
				if (XR_FAILED(result))
					return fail(runtime_state::recoverable_error, "xrPollEvent", result);
				if (event.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED)
				{
					const auto& changed = reinterpret_cast<const XrEventDataInteractionProfileChanged&>(event);
					if (changed.session == session_)
						inputs_.profile_changed();
					continue;
				}
				if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
				{
					const auto& changed =
					    reinterpret_cast<const XrEventDataReferenceSpaceChangePending&>(event);
					if (changed.session == session_ &&
					    changed.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL)
						reference_change_ = changed.changeTime;
					continue;
				}
				if (event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
					continue;
				const auto& changed = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
				if (changed.session != session_)
					continue;
				session_state_ = changed.state;
				status_.session_state = static_cast<std::int32_t>(session_state_);
				status_.session_state_name = session_state_name(changed.state);
				if (changed.state == XR_SESSION_STATE_READY && !session_running_)
				{
					++status_.session_ready_count;
					const XrSessionBeginInfo begin_info{XR_TYPE_SESSION_BEGIN_INFO, nullptr, view_type_};
					const auto begin_result = dispatch_.begin_session(session_, &begin_info);
					if (XR_FAILED(begin_result))
						return fail(runtime_state::recoverable_error, "xrBeginSession", begin_result);
					session_running_ = true;
					status_.session_running = true;
					++status_.session_begin_count;
					status_.state = runtime_state::running;
				}
				else if (changed.state == XR_SESSION_STATE_STOPPING)
				{
					++status_.session_stopping_count;
					if (prediction_.sdk_frame_open)
					{
						prediction_.menu_layers_ready = false;
						(void)finish_prediction(world_submission::omit);
					}
						inputs_.invalidate(controller_input::input_reason::session_inactive);
						head_pose_bridge::invalidate_pose();
						if (session_running_)
					{
						const auto end_result = dispatch_.end_session(session_);
						session_running_ = false;
						status_.session_running = false;
						if (XR_FAILED(end_result))
							return fail(runtime_state::recoverable_error, "xrEndSession", end_result);
						++status_.session_end_count;
					}
					status_.state = runtime_state::session_idle;
				}
				else if (changed.state == XR_SESSION_STATE_EXITING ||
				         changed.state == XR_SESSION_STATE_LOSS_PENDING)
				{
						status_.reinitialize_pending = true;
						inputs_.invalidate(controller_input::input_reason::session_inactive);
					head_pose_bridge::invalidate_pose();
					engine_stereo_bridge::invalidate_views();
					return false;
				}
			}
		}

		bool copy_pose(const XrPosef& pose, head_pose_bridge::tracking_pose& output)
		{
			const auto& q = pose.orientation;
			const auto& p = pose.position;
			const float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
			if (!std::isfinite(norm) || std::abs(norm - 1.f) > .01f)
				return false;
			output = {{p.x, p.y, p.z}, pose_filter::rotation({q.x, q.y, q.z, q.w})};
			return pose_filter::valid({output.position_meters, output.orientation});
		}
		bool retire_native_prediction()
		{
			if (!prediction_.native_pair_admitted)
				return true;
			const auto id = prediction_.pair;
			bool retired = native_render_session::active().pair_published(id)
			                   ? native_render_session::active().release_pair(id)
			                   : native_render_session::active().discard_unpublished_pair(id);
			if (!retired)
				native_render_session::active().quarantine_pair(id);
			prediction_.native_pair_admitted = false;
			return retired;
		}
		bool finish_prediction(world_submission world,
		                       publication_policy publication = publication_policy::invalidate)
		{
			if (!prediction_.sdk_frame_open)
				return true;
			std::array<XrCompositionLayerProjectionView, 2> views{};
			for (unsigned eye = 0; eye < 2; ++eye)
			{
				views[eye] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
				views[eye].pose = prediction_.views[eye].pose;
				views[eye].fov = prediction_.views[eye].fov;
				views[eye].subImage = {
				    eyes_[eye].handle, {{0, 0}, {int(eyes_[eye].width), int(eyes_[eye].height)}}, 0};
			}
			const XrCompositionLayerProjection projection{
			    XR_TYPE_COMPOSITION_LAYER_PROJECTION, nullptr, 0, local_space_, 2, views.data()};
			std::array<const XrCompositionLayerBaseHeader*, menu_surface::surface_count + 1> layers{};
			unsigned count = 0;
			if (world == world_submission::include)
				layers[count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
			if (prediction_.menu_layers_ready)
				for (const auto* layer : menus_.layers())
					layers[count++] = layer;
			const XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO,
			                         nullptr,
			                         prediction_.state.predictedDisplayTime,
			                         blend_mode_,
			                         count,
			                         layers.data()};
			const auto result = dispatch_.end_frame(session_, &end);
			prediction_.sdk_frame_open = false;
			const bool retired = retire_native_prediction();
			prediction_ = {};
			if (publication == publication_policy::invalidate)
				engine_stereo_bridge::invalidate_views();
			if (XR_FAILED(result))
			{
				status_.reinitialize_pending = true;
				return fail(runtime_state::recoverable_error, "xrEndFrame", result);
			}
			if (!retired)
				return fail(
				    runtime_state::fatal_for_vr, "native OpenXR pair retirement", XR_ERROR_RUNTIME_FAILURE);
			if (count)
				++status_.submitted_frames;
			status_.state = runtime_state::running;
			return true;
		}
		void apply_controller_reference(controller_pose_reference::configuration reference)
		{
			if (!reference.expected_runtime.empty() && reference.expected_runtime != status_.runtime_name)
				reference = {};
			if (status_.runtime_name == "VirtualDesktopXR")
				reference = controller_pose_reference::touch_legacy_reference();
			// SteamVR metadata was copied before loading OpenXR. No OpenVR SDK connection
			// overlaps this instance/session or the Present-owned frame loop.
			status_.controller_pose_reference = reference.name;
			status_.controller_pose_reference_error = reference.error;
			for (unsigned hand = 0; hand < reference.hands.size(); ++hand)
				status_.controller_reference_ids[hand] = reference.hands[hand].reference_id;
			inputs_.set_grip_reference(std::move(reference));
		}

		bool try_arm_native()
		{
			if (status_.requested_scene_mode != scene_mode::engine_stereo)
				return true;
			const auto proof = native_stereo_source::current();
			if (proof.state == native_stereo_source::phase::waiting)
			{
				status_.native_renderer_ready = false;
				status_.native_renderer_error = "awaiting coordinated native stereo content proof";
				return true;
			}
			if (proof.state == native_stereo_source::phase::failed)
			{
				return fail(runtime_state::fatal_for_vr, proof.error.c_str(), XR_ERROR_RUNTIME_FAILURE);
			}
			if (proof.generation != graphics_.generation ||
			    proof.context != reinterpret_cast<std::uintptr_t>(graphics_.context.Get()) ||
			    proof.owner_thread != GetCurrentThreadId() || proof.source.Width != eyes_[0].width ||
			    proof.source.Height != eyes_[0].height)
			{
				return fail(runtime_state::fatal_for_vr,
				            "native OpenXR content proof device/owner mismatch",
				            XR_ERROR_GRAPHICS_DEVICE_INVALID);
			}
			const auto ring = native_render_session::active().get_status();
			if (!ring.available || ring.device_generation != graphics_.generation ||
			    ring.width != proof.source.Width || ring.height != proof.source.Height ||
			    ring.format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
			    ring.source_format != std::uint32_t(proof.source.Format))
			{
				std::string error;
				if ((ring.available && !native_render_session::active().suspend_acquisition()) ||
				    !native_render_session::active().ensure_copy_ring(
				        graphics_, proof.source, DXGI_FORMAT_R16G16B16A16_FLOAT, error))
				{
					status_.native_renderer_error = error;
					return fail(
					    runtime_state::fatal_for_vr, "native OpenXR ring creation", XR_ERROR_RUNTIME_FAILURE);
				}
			}
			status_.native_renderer_ready = true;
			status_.native_renderer_error.clear();
			return true;
		}
		bool begin_prediction(std::uint64_t pair)
		{
			if (prediction_.sdk_frame_open || !session_running_)
				return false;
			if (!try_arm_native())
				return false;
			prediction_ = {};
			prediction_.state = {XR_TYPE_FRAME_STATE};
			prediction_.pair = pair;
			const XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
				auto result = dispatch_.wait_frame(session_, &wait, &prediction_.state);
				if (XR_FAILED(result))
				{
					inputs_.invalidate(controller_input::input_reason::wait_frame_failed,result);
				head_pose_bridge::invalidate_pose();
				engine_stereo_bridge::invalidate_views();
				return fail(runtime_state::recoverable_error, "xrWaitFrame", result);
			}
			const XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
				result = dispatch_.begin_frame(session_, &begin);
				if (XR_FAILED(result))
				{
					inputs_.invalidate(controller_input::input_reason::begin_frame_failed,result);
				head_pose_bridge::invalidate_pose();
				engine_stereo_bridge::invalidate_views();
				status_.reinitialize_pending = true;
				return fail(runtime_state::recoverable_error, "xrBeginFrame", result);
			}
			prediction_.sdk_frame_open = true;
				if (!prediction_.state.shouldRender)
				{
					inputs_.invalidate(controller_input::input_reason::frame_not_rendered);
				head_pose_bridge::invalidate_pose();
				(void)finish_prediction(world_submission::omit);
				return false;
			}
			if (reference_change_ && prediction_.state.predictedDisplayTime >= reference_change_)
			{
					head_pose_bridge::request_recenter();
					inputs_.invalidate(controller_input::input_reason::reference_changed);
				reference_change_ = 0;
			}
			prediction_.views = {{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
			XrViewState state{XR_TYPE_VIEW_STATE};
			std::uint32_t count{};
			const XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO,
			                              nullptr,
			                              view_type_,
			                              prediction_.state.predictedDisplayTime,
			                              local_space_};
			result = dispatch_.locate_views(session_, &locate, &state, 2, &count, prediction_.views.data());
			if (XR_FAILED(result) || count != 2)
			{
				(void)finish_prediction(world_submission::omit);
				return fail(runtime_state::recoverable_error,
				            "xrLocateViews",
				            XR_FAILED(result) ? result : XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED);
			}
			XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
			result = dispatch_.locate_space(
			    view_space_, local_space_, prediction_.state.predictedDisplayTime, &head);
			const auto flags = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
			const auto space_flags =
			    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
				if (XR_FAILED(result) || (state.viewStateFlags & flags) != flags ||
				    (head.locationFlags & space_flags) != space_flags || !copy_pose(head.pose, prediction_.head))
				{
					inputs_.invalidate(XR_FAILED(result) ? controller_input::input_reason::tracking_failed :
						controller_input::input_reason::hmd_pose_invalid,result);
				head_pose_bridge::invalidate_pose();
				(void)finish_prediction(world_submission::omit);
				return false;
			}
			std::array<head_pose_bridge::tracking_pose, 2> eye_poses{};
			std::array<pose_filter::quat, 2> eye_rotations{};
			for (unsigned eye = 0; eye < 2; ++eye)
			{
				if (!copy_pose(prediction_.views[eye].pose, eye_poses[eye]))
				{
					inputs_.invalidate(controller_input::input_reason::pose_invalid);
					(void)finish_prediction(world_submission::omit);
					return false;
				}
				const auto& q = prediction_.views[eye].pose.orientation;
				eye_rotations[eye] = pose_filter::normalize({q.x, q.y, q.z, q.w});
				const auto& f = prediction_.views[eye].fov;
				projections_[eye] = {std::tan(f.angleLeft),
				                     std::tan(f.angleRight),
				                     std::tan(f.angleDown),
				                     std::tan(f.angleUp)};
			}
			if (status_.requested_scene_mode == scene_mode::engine_stereo)
			{
				// Only relative eye rotation determines whether H2's shared camera can
				// represent this family. Separate locate calls may update prediction even
				// at the same display time; VIEW-space disagreement is not optical cant.
				const auto alignment = std::abs(pose_filter::dot(eye_rotations[0], eye_rotations[1]));
				constexpr float minimum_eye_alignment = .99999f;
				if (alignment < minimum_eye_alignment)
				{
					// Capture the rejected sample before finish_prediction clears it. This
					// is an adapter rejection, not an error returned by xrLocateViews.
					constexpr float radians_to_degrees = 57.29577951308232f;
					const auto& left = prediction_.views[0].pose.orientation;
					const auto& right = prediction_.views[1].pose.orientation;
					const auto& view_head = head.pose.orientation;
					const auto error = std::format(
					    "OpenXR native bridge cannot represent canted eye orientations: "
					    "reason=stereo_eye_orientation_mismatch source=adapter_guard "
					    "(left/right rotation exceeds the native shared-camera limit); "
					    "eye_angle_deg={:.6f} max_eye_angle_deg={:.6f} "
					    "eye_alignment={:.8f} min_eye_alignment={:.8f} "
					    "pair={} predicted_display_time={} observed_tick_ms={} session_generation={} "
					    "view_flags=0x{:X} head_flags=0x{:X} "
					    "raw_left_q_xyzw=[{:.9f},{:.9f},{:.9f},{:.9f}] "
					    "raw_right_q_xyzw=[{:.9f},{:.9f},{:.9f},{:.9f}] "
					    "raw_head_q_xyzw=[{:.9f},{:.9f},{:.9f},{:.9f}]",
					    pose_filter::angle(eye_rotations[0], eye_rotations[1]) * radians_to_degrees,
					    2 * std::acos(minimum_eye_alignment) * radians_to_degrees,
					    alignment, minimum_eye_alignment,
					    prediction_.pair, prediction_.state.predictedDisplayTime, GetTickCount64(),
					    status_.session_generation, state.viewStateFlags, head.locationFlags,
					    left.x, left.y, left.z, left.w, right.x, right.y, right.z, right.w,
					    view_head.x, view_head.y, view_head.z, view_head.w);
					inputs_.invalidate(controller_input::input_reason::view_configuration_unsupported,
					                   XR_ERROR_FEATURE_UNSUPPORTED);
					head_pose_bridge::invalidate_pose();
					(void)finish_prediction(world_submission::omit);
					return fail(runtime_state::fatal_for_vr, error.c_str(), XR_ERROR_FEATURE_UNSUPPORTED);
				}
				// Render head, offsets and submitted views must describe one located
				// sample. Keep the SDK's original eye poses for projection submission.
				prediction_.head = {
				    pose_filter::scale(pose_filter::add(eye_poses[0].position_meters,
				                                       eye_poses[1].position_meters), .5f),
				    pose_filter::rotation(pose_filter::slerp(eye_rotations[0], eye_rotations[1], .5f))};
			}
			head_pose_bridge::publish_tracking_pose(
			    prediction_.head, pair, std::chrono::steady_clock::now(),
			    head_reference_policy(session_state_, state.viewStateFlags, head.locationFlags));
			std::string operation;
			XrResult input_result{};
			if (!inputs_.sample(dispatch_,
			                    session_,
			                    local_space_,
			                    prediction_.state.predictedDisplayTime,
			                    session_state_ == XR_SESSION_STATE_FOCUSED,
			                    input_result,
			                    operation))
			{
				(void)finish_prediction(world_submission::omit);
				return fail(runtime_state::recoverable_error, operation.c_str(), input_result);
			}
			status_.controller_pose_reference_error = inputs_.grip_reference().error;
			menus_.observe_backdrop(prediction_.head, graphics_.generation);
			++status_.tracking_pose_sample_count;
			status_.tracking_last_present_frame = pair;
			const auto mode = native_menu::current_presentation();
			const auto ui = native_menu::current();
			const auto now = GetTickCount64();
			const bool fresh = ui.enabled && now >= ui.timestamp && now - ui.timestamp <= 250;
			const bool movie = menu_surface::movie_theater(mode.enabled,
			                                               mode.video,
			                                               mode.frontend,
			                                               mode.scene,
			                                               fresh,
			                                               ui.count,
			                                               ui.briefing,
			                                               mode.fullscreen_video);
			prediction_.ui_only = mode.enabled && (mode.frontend || movie);
			pause_dim_ = fresh && !ui.frontend ? ui.scene_dim : 0;

			if (status_.requested_scene_mode == scene_mode::engine_stereo && !prediction_.ui_only)
			{
				if (status_.native_renderer_ready)
				{
					if (!native_render_session::active().admit_pair(pair))
					{
						(void)finish_prediction(world_submission::omit);
						return fail(runtime_state::fatal_for_vr,
						            "native OpenXR prediction admission",
						            XR_ERROR_RUNTIME_FAILURE);
					}
					prediction_.native_pair_admitted = true;
				}
				const auto inverse = pose_filter::transpose(prediction_.head.orientation);
				std::array<std::array<float, 3>, 2> eye_offsets;
				for (unsigned eye = 0; eye < 2; ++eye)
				{
					eye_offsets[eye] = pose_filter::rotate(
					    inverse, pose_filter::sub(eye_poses[eye].position_meters, prediction_.head.position_meters));
				}
				if (!engine_stereo_bridge::publish_view_family(
				        pair, eye_offsets[0], eye_offsets[1], projections_))
				{
					(void)finish_prediction(world_submission::omit);
					return fail(
					    runtime_state::fatal_for_vr, "OpenXR predicted view family", XR_ERROR_POSE_INVALID);
				}
				// Bootstrap observations need poses but cannot retain an XR frame for
				// a source whose game-device content proof is not ready yet.
				if (!status_.native_renderer_ready)
				{
					(void)finish_prediction(world_submission::omit, publication_policy::retain_bootstrap);
					return false;
				}
			}
			else
				engine_stereo_bridge::invalidate_views();
			prediction_.views_valid = true;
			status_.frame_context_id = pair;
			++status_.frame_context_prepare_count;
			return true;
		}
		bool render_eye(eye_swapchain& eye,
		                unsigned index,
		                ID3D11Texture2D* source,
		                eye_content content,
		                XrResult& result,
		                std::string& error)
		{
			const XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
			result = dispatch_.acquire_swapchain_image(eye.handle, &acquire, &eye.acquired_index);
			if (XR_FAILED(result))
			{
				error = "xrAcquireSwapchainImage";
				return false;
			}
			eye.acquired = true;
			++status_.eyes[index].acquired;
			const XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, 10'000'000};
			result = dispatch_.wait_swapchain_image(eye.handle, &wait);
			if (XR_FAILED(result) || result == XR_TIMEOUT_EXPIRED)
			{
				error = "xrWaitSwapchainImage";
				return false;
			}
			eye.waited = true;
			if (eye.acquired_index >= eye.render_targets.size())
			{
				result = XR_ERROR_RUNTIME_FAILURE;
				return false;
			}
			if (content == eye_content::diagnostic_color)
			{
				const float pulse = float((status_.submitted_frames % 253) + 1) / 255.f;
				const float color[]{index == 0 ? 1.f : 0.f, index == 0 ? 0.f : 1.f, 1.f, pulse};
				const auto queue = d3d11::acquire_gpu_queue_interop();
				graphics_.context->ClearRenderTargetView(eye.render_targets[eye.acquired_index].Get(), color);
			}
			else
			{
				const texture_blit::renderer::draw_request transfer{
				    .graphics = graphics_,
				    .source = source,
				    .destination = eye.render_targets[eye.acquired_index].Get(),
				    .width = eye.width,
				    .height = eye.height,
				    .source_encoding = texture_blit::encoding::linear,
				    .dim = pause_dim_};
				if (!blit_.draw(transfer, error))
				{
					result = XR_ERROR_RUNTIME_FAILURE;
					return false;
				}
			}
			{
				const auto queue = d3d11::acquire_gpu_queue_interop();
				graphics_.context->Flush();
			}
			const XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
			result = dispatch_.release_swapchain_image(eye.handle, &release);
			if (XR_FAILED(result))
			{
				error = "xrReleaseSwapchainImage";
				return false;
			}
			eye.acquired = eye.waited = false;
			++status_.eyes[index].released;
			++status_.compositor_render_count;
			return true;
		}
		bool frame_failure(const char* operation, XrResult result)
		{
			for (auto& eye : eyes_)
			{
				XrResult ignored{};
				(void)settle_eye(eye, ignored);
			}
				prediction_.menu_layers_ready = false;
				(void)finish_prediction(world_submission::omit);
				inputs_.invalidate(controller_input::input_reason::frame_submission_failed,result);
			head_pose_bridge::invalidate_pose();
			fail(runtime_state::recoverable_error, operation, result);
			(void)teardown_preserving_error();
			return false;
		}
		bool render_synthetic()
		{
			if (!prediction_.sdk_frame_open || !prediction_.views_valid)
				return false;
			XrResult result{};
			std::string error;
			for (unsigned eye = 0; eye < 2; ++eye)
				if (!render_eye(eyes_[eye], eye, nullptr, eye_content::diagnostic_color, result, error))
					return frame_failure(error.empty() ? "OpenXR eye render" : error.c_str(), result);
			return finish_prediction(world_submission::include);
		}
		bool complete_native(IDXGISwapChain* chain)
		{
			if (!prediction_.views_valid)
				return finish_prediction(world_submission::omit);
			bool world = false;
			if (!prediction_.ui_only)
			{
				if (native_render_session::active().pair_deferred(prediction_.pair))
				{
					return finish_prediction(world_submission::omit);
				}
				if (native_render_session::active().pair_failed(prediction_.pair))
				{
					return frame_failure("H2 predicted stereo pair failed", XR_ERROR_RUNTIME_FAILURE);
				}
				if (!native_render_session::active().pair_published(prediction_.pair))
				{
					status_.last_compositor_error =
					    "waiting for the exact predicted native OpenXR stereo pair";
					if (++prediction_.pending_presents >= 4)
						return finish_prediction(world_submission::omit);
					return true;
				}
				std::array<native_render_session::eye_target, 2> source;
				if (!native_render_session::active().acquire_published_pair(prediction_.pair, source))
				{
					return frame_failure("native OpenXR pair acquire", XR_ERROR_RUNTIME_FAILURE);
				}
				XrResult result{};
				std::string error;
				++status_.compositor_prepare_count;
				for (unsigned eye = 0; eye < 2; ++eye)
					if (!render_eye(eyes_[eye],
					                eye,
					                source[eye].color.Get(),
					                eye_content::native_texture,
					                result,
					                error))
						return frame_failure(error.empty() ? "OpenXR eye transfer" : error.c_str(), result);
				status_.last_compositor_error.clear();
				world = true;
			}
			const menu_layers::presentation_input menu_input{.session = session_,
			                                                 .space = local_space_,
			                                                 .graphics = graphics_,
			                                                 .swapchain = chain,
			                                                 .head = prediction_.head,
			                                                 .cylinder_supported = cylinder_supported_,
			                                                 .format = menu_format_};
			const auto menu_result = menus_.prepare(dispatch_, menu_input);
			if (!menu_result)
			{
				const auto detail = std::string(menu_result.call.operation) +
				                    (menu_result.detail.empty() ? "" : ": " + menu_result.detail);
				return frame_failure(detail.c_str(), menu_result.call.code);
			}
			prediction_.menu_layers_ready = true;
			return finish_prediction(world ? world_submission::include : world_submission::omit);
		}
		bool maintain_session(const d3d11::device_snapshot& graphics)
		{
			const auto resized = resize_generation_.exchange(0, std::memory_order_acq_rel);
			if (status_.desired_enabled && graphics && resized == graphics.generation)
				status_.reinitialize_pending = true;
			if (!status_.desired_enabled)
			{
				(void)teardown_locked(true);
				return false;
			}
			if (status_.reinitialize_pending ||
			    (status_.applied_enabled && status_.device_generation != graphics.generation))
			{
				if (!teardown_locked(false))
					return false;
				status_.reinitialize_pending = false;
				auto_initialize_allowed_ = true;
			}
			if (!status_.applied_enabled)
			{
				if (!graphics)
				{
					status_.state = runtime_state::waiting_for_graphics;
					return false;
				}
				if (!auto_initialize_allowed_ && initialization_generation_ == graphics.generation)
					return false;
				initialization_generation_ = graphics.generation;
				auto_initialize_allowed_ = false;
				if (!initialize_locked(graphics))
					return false;
			}
			if (status_.state == runtime_state::fatal_for_vr)
				return false;
			owner_generation_ = graphics.generation;
			owner_thread_ = GetCurrentThreadId();
			status_.direct_present_owner_thread_id = owner_thread_;
			return poll_events() && session_running_;
		}

		mutable std::mutex mutex_;
		runtime_status status_{runtime_state::disabled, true};
		HMODULE loader_{};
		dispatch_table dispatch_{};
		XrInstance instance_{XR_NULL_HANDLE};
		XrSystemId system_id_{XR_NULL_SYSTEM_ID};
		XrSession session_{XR_NULL_HANDLE};
		// SDK control state is authoritative; runtime_status is its copied report.
		XrSessionState session_state_{XR_SESSION_STATE_UNKNOWN};
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
		std::uint64_t initialization_generation_{};
		std::atomic_uint64_t resize_generation_{};
		mutable std::mutex status_mutex_;
		runtime_status status_snapshot_{runtime_state::disabled, true};
	};
#else
	class runtime_backend::implementation final
	{
	  public:
		explicit implementation(startup_query)
		{
			status_.state = runtime_state::sdk_headers_unavailable;
		}
		void set_desired_enabled(bool value)
		{
			status_.desired_enabled = value;
		}
		void set_scene_mode(scene_mode mode)
		{
			status_.requested_scene_mode = mode;
		}
		void request_reinitialize()
		{
			status_.reinitialize_pending = true;
		}
		void prepare_frame(const d3d11::device_snapshot&, std::uint64_t)
		{
		}
		bool initialize(const d3d11::device_snapshot&)
		{
			status_.state = runtime_state::sdk_headers_unavailable;
			return false;
		}
		void on_present(const d3d11::device_snapshot&, std::uint64_t)
		{
		}
		void on_present(const d3d11::present_event&)
		{
		}
		void on_present_post(const d3d11::present_event&, HRESULT)
		{
		}
		void capture_present(const d3d11::present_event&)
		{
		}
		bool capture_engine_texture(const d3d11::device_snapshot&, ID3D11Texture2D*, capture_frame_tag)
		{
			return false;
		}
		void poll_capture(const d3d11::device_snapshot&)
		{
		}
		void on_resize_before(const d3d11::resize_event&) noexcept
		{
		}
		void on_device_destroying(const d3d11::device_snapshot&) noexcept
		{
		}
		void shutdown() noexcept
		{
			status_.state = runtime_state::disabled;
			status_.applied_enabled = false;
		}
		bool shutdown_complete() const noexcept
		{
			return true;
		}
		bool requested_enabled() const
		{
			return status_.desired_enabled;
		}
		bool applied_enabled() const
		{
			return false;
		}
		runtime_status get_status() const
		{
			return status_;
		}

	  private:
		runtime_status status_{};
	};
#endif

	runtime_backend::runtime_backend(startup_query query)
	    : implementation_(std::make_unique<implementation>(query))
	{
	}
	runtime_backend::~runtime_backend()
	{
		if (implementation_ != nullptr)
		{
			implementation_->shutdown();
		}
	}
	void runtime_backend::set_desired_enabled(const bool enabled)
	{
		implementation_->set_desired_enabled(enabled);
	}
	void runtime_backend::set_scene_mode(const scene_mode mode)
	{
		implementation_->set_scene_mode(mode);
	}
	void runtime_backend::request_reinitialize()
	{
		implementation_->request_reinitialize();
	}
	void runtime_backend::prepare_frame(const d3d11::device_snapshot& graphics,
	                                    const std::uint64_t frame_index)
	{
		implementation_->prepare_frame(graphics, frame_index);
	}
	bool runtime_backend::initialize(const d3d11::device_snapshot& graphics)
	{
		return implementation_->initialize(graphics);
	}
	void runtime_backend::on_present(const d3d11::device_snapshot& graphics, const std::uint64_t frame)
	{
		implementation_->on_present(graphics, frame);
	}
	void runtime_backend::on_present(const d3d11::present_event& event)
	{
		implementation_->on_present(event);
	}
	void runtime_backend::on_present_post(const d3d11::present_event& event, const HRESULT result)
	{
		implementation_->on_present_post(event, result);
	}
	void runtime_backend::capture_present(const d3d11::present_event& event)
	{
		implementation_->capture_present(event);
	}
	bool runtime_backend::capture_engine_texture(const d3d11::device_snapshot& graphics,
	                                             ID3D11Texture2D* const source,
	                                             const capture_frame_tag tag)
	{
		return implementation_->capture_engine_texture(graphics, source, tag);
	}
	void runtime_backend::poll_capture(const d3d11::device_snapshot& graphics)
	{
		implementation_->poll_capture(graphics);
	}
	void runtime_backend::on_resize_before(const d3d11::resize_event& event) noexcept
	{
		implementation_->on_resize_before(event);
	}
	void runtime_backend::on_device_destroying(const d3d11::device_snapshot& graphics) noexcept
	{
		implementation_->on_device_destroying(graphics);
	}
	void runtime_backend::shutdown() noexcept
	{
		implementation_->shutdown();
	}
	bool runtime_backend::shutdown_complete() const noexcept
	{
		return implementation_->shutdown_complete();
	}
	bool runtime_backend::requested_enabled() const
	{
		return implementation_->requested_enabled();
	}
	bool runtime_backend::applied_enabled() const
	{
		return implementation_->applied_enabled();
	}
	runtime_status runtime_backend::get_status() const
	{
		return implementation_->get_status();
	}
}
