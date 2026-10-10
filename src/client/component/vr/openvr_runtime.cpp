#include <std_include.hpp>

#include "openvr_runtime.hpp"

#include "engine_stereo_bridge.hpp"
#include "engine_stereo_owner_pass.hpp"
#include "engine_scene_resolution.hpp"
#include "diagnostics.hpp"
#include "head_pose_bridge.hpp"
#include "native_render_session.hpp"
#include "steamvr_runtime.hpp"
#include "steamvr_input.hpp"
#include "controller_input.hpp"
#include "menu_overlay.hpp"
#include "movie_presentation.hpp"

#include <openvr.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <optional>

namespace vr::openvr
{
	namespace
	{
		const char* compositor_error_name(const EVRCompositorError error) noexcept
		{
			switch (error)
			{
			case VRCompositorError_None: return "VRCompositorError_None";
			case VRCompositorError_RequestFailed: return "VRCompositorError_RequestFailed";
			case VRCompositorError_IncompatibleVersion: return "VRCompositorError_IncompatibleVersion";
			case VRCompositorError_DoNotHaveFocus: return "VRCompositorError_DoNotHaveFocus";
			case VRCompositorError_InvalidTexture: return "VRCompositorError_InvalidTexture";
			case VRCompositorError_IsNotSceneApplication: return "VRCompositorError_IsNotSceneApplication";
			case VRCompositorError_TextureIsOnWrongDevice: return "VRCompositorError_TextureIsOnWrongDevice";
			case VRCompositorError_TextureUsesUnsupportedFormat: return "VRCompositorError_TextureUsesUnsupportedFormat";
			case VRCompositorError_SharedTexturesNotSupported: return "VRCompositorError_SharedTexturesNotSupported";
			case VRCompositorError_IndexOutOfRange: return "VRCompositorError_IndexOutOfRange";
			case VRCompositorError_AlreadySubmitted: return "VRCompositorError_AlreadySubmitted";
			case VRCompositorError_InvalidBounds: return "VRCompositorError_InvalidBounds";
			case VRCompositorError_AlreadySet: return "VRCompositorError_AlreadySet";
			default: return "VRCompositorError_Unknown";
			}
		}

		std::string tracked_string(IVRSystem* const system, const ETrackedDeviceProperty property)
		{
			ETrackedPropertyError error{TrackedProp_Success};
			const auto required = system->GetStringTrackedDeviceProperty(k_unTrackedDeviceIndex_Hmd,
				property, nullptr, 0, &error);
			if (required <= 1 || (error != TrackedProp_Success && error != TrackedProp_BufferTooSmall)) return {};
			std::string value(required, '\0');
			error = TrackedProp_Success;
			system->GetStringTrackedDeviceProperty(k_unTrackedDeviceIndex_Hmd, property,
				value.data(), required, &error);
			if (error != TrackedProp_Success) return {};
			value.resize(std::strlen(value.c_str()));
			return value;
		}

		bool texture_uses_device(ID3D11Texture2D* const texture,
			ID3D11Device* const expected) noexcept
		{
			if (texture == nullptr || expected == nullptr) return false;
			Microsoft::WRL::ComPtr<ID3D11Device> owner;
			texture->GetDevice(&owner);
			if (!owner) return false;
			Microsoft::WRL::ComPtr<IUnknown> owner_identity;
			Microsoft::WRL::ComPtr<IUnknown> expected_identity;
			return SUCCEEDED(owner.As(&owner_identity)) &&
				SUCCEEDED(expected->QueryInterface(IID_PPV_ARGS(&expected_identity))) &&
				owner_identity.Get() == expected_identity.Get();
		}

		struct prepared_frame
		{
			bool valid{};
			std::uint64_t frame_id{};
			std::uint64_t device_generation{};
			std::array<engine_stereo_bridge::eye_projection, 2> projections{};
		};

		enum class scene_prepare_result
		{
			pending,
			deferred,
			ready,
			fatal,
		};

		enum class frame_phase
		{
			idle_unarmed,
			need_pose,
			collecting,
			submitted_waiting_present,
			fatal_holding_pair,
		};

		enum class submitted_pair_retirement
		{
			none,
			confirm_lossless_format,
			advance_format_probe,
			finish_unqualified_format_probe,
			resume_after_focus,
			fatal_after_wait_get_poses,
		};

		constexpr std::array submit_format_candidates{
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			DXGI_FORMAT_R8G8B8A8_UNORM,
		};

		const char* frame_phase_name(const frame_phase phase) noexcept
		{
			switch (phase)
			{
			case frame_phase::idle_unarmed: return "idle_unarmed";
			case frame_phase::need_pose: return "need_pose";
			case frame_phase::collecting: return "collecting";
			case frame_phase::submitted_waiting_present: return "submitted_waiting_present";
			case frame_phase::fatal_holding_pair: return "fatal_holding_pair";
			default: return "unknown";
			}
		}
	}

	present_post_validation validate_present_post(const present_transaction_key& transaction,
		const d3d11::present_event& event, const std::uint32_t thread_id,
		const HRESULT result) noexcept
	{
		return present_transaction::validate(transaction, event, thread_id, result);
	}

	bool present_owner_change_is_violation(const bool applied_enabled,
		const bool reinitialize_pending, const std::uint64_t owner_generation,
		const std::uint32_t owner_thread_id, const std::uint64_t event_generation,
		const std::uint32_t event_thread_id) noexcept
	{
		return present_transaction::owner_changed(applied_enabled, reinitialize_pending,
			owner_generation, owner_thread_id, event_generation, event_thread_id);
	}

	bool gpu_frame_transport_is_armed(const runtime_status& status) noexcept
	{
		return status.applied_enabled && status.native_renderer_ready &&
			status.submission_on_game_device &&
			status.graphics_transport == "h2_device_direct";
	}

	projection_tangents projection_tangents_from_raw(const float left,
		const float right, const float top, const float bottom) noexcept
	{
		// OpenVR's historical parameter names are inverted vertically: `top` is
		// the negative tangent of the bottom (-Y) edge and `bottom` is the
		// positive tangent of the top (+Y) edge. They are already H2's down/up
		// tangents and must not be negated or exchanged again.
		return {left, right, top, bottom};
	}

	class runtime_backend::implementation final
	{
		friend struct runtime_test_access;
	public:
		implementation() = default;
		~implementation() { shutdown(); }

		void set_desired_enabled(const bool enabled)
		{
			const std::lock_guard lock(mutex_);
			if (shutdown_requested_) return;
			if (enabled) final_shutdown_ = false;
			if (enabled && !status_.desired_enabled) auto_initialize_allowed_ = true;
			status_.desired_enabled = enabled;
			publish_status_locked();
		}

		void set_scene_mode(const scene_mode mode)
		{
			const std::lock_guard lock(mutex_);
			status_.requested_scene_mode = mode;
			publish_status_locked();
		}

		void request_reinitialize()
		{
			const std::lock_guard lock(mutex_);
			if (shutdown_requested_ || final_shutdown_) return;
			auto_initialize_allowed_ = true;
			status_.reinitialize_pending = true;
			publish_status_locked();
		}

		void prepare_frame(const d3d11::device_snapshot& graphics, const std::uint64_t frame_index)
		{
			const std::lock_guard lock(mutex_);
			if (shutdown_requested_ || !status_.desired_enabled || !status_.applied_enabled)
			{
				return;
			}
			const auto renderer_thread_id = GetCurrentThreadId();
			const auto renderer_changed = status_.direct_renderer_thread_id != renderer_thread_id;
			status_.direct_renderer_thread_id = renderer_thread_id;
			if (renderer_changed)
			{
				diagnostics::record_trace(diagnostics::trace_event::runtime_prepare_frame,
					frame_index, graphics.generation);
				diagnostics::record_trace(diagnostics::trace_event::runtime_direct_thread_contract,
					renderer_thread_id, status_.direct_present_owner_thread_id);
			}
			if (status_.state == runtime_state::fatal_for_vr)
			{
				return;
			}
			if (!gpu_frame_transport_is_armed(status_))
			{
				// Renderer ownership is observed here, but no OpenVR GPU frame may be
				// opened until a lower H2 renderer boundary reserves two view slots
				// from one immutable world snapshot. Natural scene-hook entries are
				// not such a reservation and may straddle frontend frames.
				publish_status_locked();
				return;
			}
			if (!graphics || status_.device_generation != graphics.generation)
			{
				++status_.frame_context_miss_count;
				diagnostics::record_trace(diagnostics::trace_event::runtime_prepare_frame_result,
					frame_index, 0);
				publish_status_locked();
				return;
			}
			const auto native_status = native_render_session::active().get_status();
			if (!native_status.available ||
				native_status.device_generation != graphics.generation ||
				native_status.width != status_.eyes[0].width ||
				native_status.height != status_.eyes[0].height ||
				native_status.format != static_cast<std::uint32_t>(status_.color_format) ||
				!native_status.copy_ring)
			{
				status_.native_renderer_ready = false;
				status_.native_renderer_error =
					"native H2 eye ring changed outside the Present-owner transaction";
				++status_.native_renderer_failure_count;
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error = status_.native_renderer_error;
				frame_phase_ = frame_phase::fatal_holding_pair;
				engine_stereo_bridge::invalidate_views();
				diagnostics::record_trace(diagnostics::trace_event::runtime_prepare_frame_result,
					frame_index, 0);
				publish_status_locked();
				return;
			}
			status_.native_renderer_ready = true;
			status_.native_renderer_error.clear();
			if (renderer_changed)
			{
				diagnostics::record_trace(diagnostics::trace_event::runtime_prepare_frame_result,
					frame_index, 1);
			}
			publish_status_locked();
		}

		void capture_present(const d3d11::present_event& event)
		{
			// Gameplay production accepts only native engine stereo. Explicit
			// native 2D movies use a separate curved overlay at the Present owner.
			(void)event;
		}
		bool capture_engine_texture(const d3d11::device_snapshot& graphics,
			ID3D11Texture2D* const source, const capture_frame_tag tag)
		{
			const std::lock_guard lock(mutex_);
			if (shutdown_requested_ || !graphics || graphics.generation != status_.device_generation ||
				!direct_graphics_ || direct_graphics_.generation != graphics.generation ||
				direct_graphics_.device.Get() != graphics.device.Get() || !tag.native ||
				!tag.stereo || tag.pair_id == 0 || tag.eye_index >= 2)
			{
				return false;
			}
			return native_render_session::active().complete_rendered_eye(
				tag.pair_id, tag.eye_index, source);
		}
		void poll_capture(const d3d11::device_snapshot& graphics) { (void)graphics; }

		bool initialize(const d3d11::device_snapshot& graphics)
		{
			(void)graphics;
			const std::lock_guard lock(mutex_);
			status_.state = runtime_state::fatal_for_vr;
			status_.last_error =
				"OpenVR initialization is restricted to the real DXGI Present-pre owner";
			publish_status_locked();
			return false;
		}

		void on_present(const d3d11::present_event& event)
		{
			const std::lock_guard lock(mutex_);
			if (shutdown_requested_ || final_shutdown_) return;
			const auto thread_id = GetCurrentThreadId();
			if (present_transaction_.active)
			{
				++status_.direct_present_owner_contract_violations;
				status_.direct_present_owner_contract_valid = false;
				frame_phase_ = frame_phase::fatal_holding_pair;
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error = "DXGI Present-pre re-entered an unfinished OpenVR transaction";
				status_.last_compositor_error = status_.last_error;
				engine_stereo_bridge::invalidate_views();
				publish_status_locked();
				return;
			}
			const auto resized_generation=resize_pending_generation_.exchange(0,std::memory_order_acq_rel);
			if (resized_generation==event.graphics.generation && resized_generation!=0 && status_.desired_enabled)
			{
				// H2 recreated its scene/display targets without replacing the device.
				// Retire stale pose families and cached conversion sources through the
				// normal detach path here, outside DXGI's resize callback/GPU scope.
				auto_initialize_allowed_=true;
				status_.reinitialize_pending=true;
				engine_stereo_owner_pass::request_temporal_history_reset();
			}
			present_transaction_ = {true, event.frame_index,
				event.graphics.generation, thread_id};
			++status_.present_owner_pre_count;
			status_.direct_present_owner_contract_valid = false;
			if (d3d11::is_inside_present_gpu_scope())
			{
				++status_.direct_present_owner_contract_violations;
				frame_phase_ = frame_phase::fatal_holding_pair;
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error =
					"OpenVR Present-pre was invoked while the DXGI GPU scope was held";
				status_.last_compositor_error = status_.last_error;
				present_transaction_traced_ = true;
				publish_status_locked();
				return;
			}
			const bool owner_violation = present_owner_change_is_violation(
				status_.applied_enabled, status_.reinitialize_pending,
				present_owner_generation_, present_owner_thread_id_,
				event.graphics.generation, thread_id);
			if (owner_violation)
			{
				++status_.direct_present_owner_contract_violations;
				frame_phase_ = frame_phase::fatal_holding_pair;
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error = std::format(
					"DXGI Present owner changed within D3D11 generation {} ({} -> {})",
					event.graphics.generation, present_owner_thread_id_, thread_id);
				status_.last_compositor_error = status_.last_error;
				engine_stereo_bridge::invalidate_views();
				present_transaction_traced_ = true;
				diagnostics::record_trace(diagnostics::trace_event::runtime_direct_thread_contract,
					present_owner_thread_id_, thread_id);
				publish_status_locked();
				return;
			}
			present_transaction_traced_ = status_.desired_enabled &&
				(!status_.applied_enabled || status_.reinitialize_pending ||
					frame_phase_ == frame_phase::need_pose ||
					(frame_phase_ == frame_phase::collecting && prepared_frame_.valid &&
						(native_render_session::active().pair_published(prepared_frame_.frame_id) ||
							native_render_session::active().pair_deferred(prepared_frame_.frame_id) ||
							native_render_session::active().pair_failed(prepared_frame_.frame_id))));
			if (present_transaction_traced_)
			{
				diagnostics::record_trace(diagnostics::trace_event::present_pre,
					event.frame_index, event.graphics.generation);
			}
			try
			{
				const auto present_ready = on_present_locked(event.graphics, event.swap_chain);
				if (present_ready && present_owner_generation_ != status_.device_generation)
				{
					present_owner_generation_ = status_.device_generation;
					present_owner_thread_id_ = thread_id;
					status_.direct_present_owner_thread_id = thread_id;
					diagnostics::record_trace(diagnostics::trace_event::runtime_direct_thread_contract,
						status_.direct_renderer_thread_id, thread_id);
				}
				const auto menu=native_menu::current();
				const auto presentation=native_menu::current_presentation();
				update_movie_mode(presentation,menu);
				if (present_ready && !presentation.frontend && !movie_mode_ && frame_phase_ == frame_phase::collecting && prepared_frame_.valid)
				{
					advance_prepared_frame();
				}
				set_pause_dim(present_ready&&ui_head_valid_&&menu.enabled&&!menu.frontend&&
					GetTickCount64()>=menu.timestamp&&GetTickCount64()-menu.timestamp<=250?menu.scene_dim:0);
				if(present_ready&&ui_head_valid_&&presentation.enabled&&(presentation.frontend||movie_mode_))set_theater(true);
				Microsoft::WRL::ComPtr<ID3D11Texture2D> movie_source;
				if(present_ready&&movie_mode_&&event.swap_chain)(void)event.swap_chain->GetBuffer(0,IID_PPV_ARGS(&movie_source));
				(void)menu_overlay_.capture_movie(event.graphics.context.Get(),movie_source.Get(),status_.device_generation,present_ready&&movie_mode_);
				if(present_ready)menu_overlay_.update(ui_head_,status_.device_generation,
					ui_head_valid_);
			}
			catch (const std::exception& error)
			{
				frame_phase_ = frame_phase::fatal_holding_pair;
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error = std::format("OpenVR Present-pre exception: {}", error.what());
				status_.last_compositor_error = status_.last_error;
				engine_stereo_bridge::invalidate_views();
			}
			catch (...)
			{
				frame_phase_ = frame_phase::fatal_holding_pair;
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error = "unknown OpenVR Present-pre exception";
				status_.last_compositor_error = status_.last_error;
				engine_stereo_bridge::invalidate_views();
			}
			if (status_.state == runtime_state::fatal_for_vr && !present_transaction_traced_)
			{
				present_transaction_traced_ = true;
				diagnostics::record_trace(diagnostics::trace_event::present_pre,
					event.frame_index, event.graphics.generation);
			}
			publish_status_locked();
		}

		void on_present(const d3d11::device_snapshot& graphics, const std::uint64_t frame)
		{
			(void)graphics;
			(void)frame;
			const std::lock_guard lock(mutex_);
			status_.state = runtime_state::fatal_for_vr;
			status_.last_error =
				"synthetic Present completion is forbidden for the OpenVR production path";
			publish_status_locked();
		}

		void on_present_post(const d3d11::present_event& event, const HRESULT result)
		{
			const std::lock_guard lock(mutex_);
			if (final_shutdown_ || (!present_transaction_.active &&
				!status_.applied_enabled && status_.device_generation == 0))
			{
				return;
			}
			const auto terminal_shutdown = gsl::finally([this]
			{
				if (shutdown_requested_ && !present_transaction_.active)
				{
					complete_terminal_shutdown_locked();
				}
			});
			const auto thread_id = GetCurrentThreadId();
			++status_.present_owner_post_count;
			status_.present_owner_last_post_hresult = result;
			if (d3d11::is_inside_present_gpu_scope())
			{
				++status_.direct_present_owner_contract_violations;
				status_.direct_present_owner_contract_valid = false;
				frame_phase_ = frame_phase::fatal_holding_pair;
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error =
					"OpenVR Present-post was invoked while the DXGI GPU scope was held";
				status_.last_compositor_error = status_.last_error;
				publish_status_locked();
				return;
			}
			const auto transaction = present_transaction_;
			const auto transaction_traced = present_transaction_traced_;
			const auto validation = validate_present_post(transaction, event, thread_id, result);
			present_transaction_ = {};
			present_transaction_traced_ = false;
			if (transaction_traced || validation != present_post_validation::matched)
			{
				diagnostics::record_trace(diagnostics::trace_event::present_post,
					event.frame_index, static_cast<std::uint32_t>(result));
			}
			if (validation != present_post_validation::matched)
			{
				++status_.direct_present_owner_contract_violations;
				status_.direct_present_owner_contract_valid = false;
				frame_phase_ = frame_phase::fatal_holding_pair;
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error = validation != present_post_validation::present_failed
					? std::format("DXGI Present-post did not match its OpenVR pre transaction "
						"(validation={} frame={} generation={} thread={})",
						static_cast<unsigned int>(validation), event.frame_index,
						event.graphics.generation, thread_id)
					: std::format("IDXGISwapChain::Present failed inside the OpenVR transaction "
						"(HRESULT=0x{:08X})", static_cast<std::uint32_t>(result));
				status_.last_compositor_error = status_.last_error;
				engine_stereo_bridge::invalidate_views();
				diagnostics::record_trace(diagnostics::trace_event::runtime_direct_thread_contract,
					transaction.thread_id, thread_id);
				// A failed or unpaired DXGI transaction leaves compositor ownership
				// unknowable. Detach OpenVR before releasing any native target, then
				// preserve the fatal evidence for vr_status.
				teardown_preserving_error();
				frame_phase_ = frame_phase::fatal_holding_pair;
				publish_status_locked();
				return;
			}
			status_.direct_present_owner_contract_valid = true;
			status_.present_owner_last_completed_frame = event.frame_index;
			if (status_.state == runtime_state::fatal_for_vr ||
				frame_phase_ == frame_phase::fatal_holding_pair)
			{
				teardown_preserving_error();
				frame_phase_ = frame_phase::fatal_holding_pair;
				publish_status_locked();
				return;
			}
			if (!status_.desired_enabled || !status_.applied_enabled)
			{
				publish_status_locked();
				return;
			}
			const auto gpu_transport_armed = gpu_frame_transport_is_armed(status_);
			if (gpu_transport_armed && frame_phase_ == frame_phase::submitted_waiting_present)
			{
				frame_phase_ = frame_phase::need_pose;
				++status_.completed_present_handoff_count;
				const auto present = d3d11::get_graphics_status();
				if (present.last_present_post != std::chrono::steady_clock::time_point{})
				{
					status_.present_to_wait_get_poses_us = static_cast<std::uint64_t>(
						std::chrono::duration_cast<std::chrono::microseconds>(
							std::chrono::steady_clock::now() - present.last_present_post).count());
				}
			}
			const auto ui=native_menu::current();
			const auto presentation=native_menu::current_presentation();
			update_movie_mode(presentation,ui);
			const bool ui_only=presentation.enabled&&(presentation.frontend||movie_mode_);
			if(ui_only)
			{
				discard_prepared_native_pair(false);prepared_frame_={};
				(void)native_render_session::active().suspend_acquisition();
			}
			const bool pose_tick=!gpu_transport_armed || frame_phase_ == frame_phase::need_pose || ui_only;
			if (pose_tick)
			{
				try
				{
					const auto prepared = sample_frame_views_locked(event.frame_index,
						gpu_transport_armed&&!ui_only,ui_only);
					if(ui_only&&prepared)frame_phase_=gpu_transport_armed?frame_phase::need_pose:frame_phase::idle_unarmed;
					else if (gpu_transport_armed && prepared)
					{
						frame_phase_ = frame_phase::collecting;
					}
				}
				catch (const std::exception& error)
				{
					++status_.tracking_pose_failure_count;
					frame_phase_ = frame_phase::fatal_holding_pair;
					status_.state = runtime_state::fatal_for_vr;
					status_.last_error = std::format("OpenVR Present-post exception: {}", error.what());
					status_.last_compositor_error = status_.last_error;
				}
				catch (...)
				{
					++status_.tracking_pose_failure_count;
					frame_phase_ = frame_phase::fatal_holding_pair;
					status_.state = runtime_state::fatal_for_vr;
					status_.last_error = "unknown OpenVR Present-post exception";
					status_.last_compositor_error = status_.last_error;
				}
				if (status_.state == runtime_state::fatal_for_vr ||
					frame_phase_ == frame_phase::fatal_holding_pair)
				{
					teardown_preserving_error();
					frame_phase_ = frame_phase::fatal_holding_pair;
				}
			}
			if(!pose_tick&&ui.interactive()&&system_)
			{
				// A native pause can stop scene production. Keep UI sampling on this
				// same Present owner without changing an in-flight stereo view family.
				TrackedDevicePose_t pose{};
				system_->GetDeviceToAbsoluteTrackingPose(TrackingUniverseStanding,0,&pose,1);
				ui_head_valid_=pose.bDeviceIsConnected&&pose.bPoseIsValid;
				if(ui_head_valid_)for(unsigned r=0;r<3;++r)
				{
					ui_head_.position_meters[r]=pose.mDeviceToAbsoluteTracking.m[r][3];
					for(unsigned c=0;c<3;++c)ui_head_.orientation[r][c]=pose.mDeviceToAbsoluteTracking.m[r][c];
				}
					input_actions_.sample(ui_head_valid_&&system_->IsInputAvailable(),ui_head_valid_ ? controller_input::input_reason::input_unavailable :
						!pose.bDeviceIsConnected ? controller_input::input_reason::hmd_disconnected : controller_input::input_reason::hmd_pose_invalid);
			}
			publish_status_locked();
		}

		void on_resize_before(const d3d11::resize_event& event) noexcept
		{
			native_menu::invalidate_images();
			// Publish only a request. Submitted textures stay alive until the next
			// Present owner detaches OpenVR; no runtime lock or GPU work in resize.
			resize_pending_generation_.store(event.graphics.generation,std::memory_order_release);
		}

		void on_device_destroying(const d3d11::device_snapshot& graphics) noexcept
		{
			const std::lock_guard lock(mutex_);
			if (status_.device_generation == graphics.generation)
			{
				present_transaction_ = {};
				present_transaction_traced_ = false;
				status_.reinitialize_pending = true;
				teardown_locked(false);
			}
			publish_status_locked();
		}

		void shutdown() noexcept
		{
			const std::lock_guard lock(mutex_);
			if (final_shutdown_) return;
			shutdown_requested_ = true;
			auto_initialize_allowed_ = false;
			if (!present_transaction_.active)
			{
				complete_terminal_shutdown_locked();
				return;
			}
			publish_status_locked();
		}

		bool shutdown_complete() const noexcept
		{
			const std::lock_guard lock(mutex_);
			return final_shutdown_;
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
			runtime_status result;
			{
				const std::lock_guard lock(status_mutex_);
				result = status_snapshot_;
			}
			return result;
		}

	private:
		using pacing_clock = std::chrono::steady_clock;

		static std::uint64_t elapsed_microseconds(
			const pacing_clock::time_point started) noexcept
		{
			if (started == pacing_clock::time_point{}) return 0;
			const auto elapsed = pacing_clock::now() - started;
			return static_cast<std::uint64_t>((std::max)(
				std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(),
				std::int64_t{}));
		}

		static void record_duration(const std::uint64_t elapsed,
			std::uint64_t& count, std::uint64_t& last, std::uint64_t& maximum,
			std::uint64_t& total) noexcept
		{
			++count;
			last = elapsed;
			maximum = (std::max)(maximum, elapsed);
			total += elapsed;
		}

		void clear_prepared_pair_timing() noexcept
		{
			prepared_pair_started_at_ = {};
			prepared_pair_pending_presents_ = 0;
		}

		void clear_submitted_pair_timing() noexcept
		{
			submitted_pair_started_at_ = {};
		}

		void clear_pacing_transients() noexcept
		{
			clear_prepared_pair_timing();
			clear_submitted_pair_timing();
		}

		void record_prepared_pair_ready() noexcept
		{
			if (prepared_pair_started_at_ == pacing_clock::time_point{}) return;
			const auto elapsed = elapsed_microseconds(prepared_pair_started_at_);
			record_duration(elapsed, status_.prepared_pair_ready_sample_count,
				status_.prepared_pair_ready_last_us,
				status_.prepared_pair_ready_max_us,
				status_.prepared_pair_ready_total_us);
			status_.prepared_pair_pending_presents_last =
				prepared_pair_pending_presents_;
			status_.prepared_pair_pending_presents_max = (std::max)(
				status_.prepared_pair_pending_presents_max,
				prepared_pair_pending_presents_);
			status_.prepared_pair_pending_presents_total +=
				prepared_pair_pending_presents_;
			clear_prepared_pair_timing();
		}

		void begin_submitted_pair_timing() noexcept
		{
			submitted_pair_started_at_ = pacing_clock::now();
		}

		void record_submitted_pair_retirement() noexcept
		{
			if (submitted_pair_started_at_ == pacing_clock::time_point{}) return;
			const auto elapsed = elapsed_microseconds(submitted_pair_started_at_);
			record_duration(elapsed, status_.submit_to_retirement_count,
				status_.submit_to_retirement_last_us,
				status_.submit_to_retirement_max_us,
				status_.submit_to_retirement_total_us);
			clear_submitted_pair_timing();
		}

		void sample_compositor_frame_timing() noexcept
		{
			Compositor_FrameTiming timing{};
			timing.m_nSize = sizeof(timing);
			++status_.compositor_frame_timing_query_count;
			status_.compositor_frame_timing_valid =
				compositor_api_ != nullptr && compositor_api_->GetFrameTiming(&timing);
			if (!status_.compositor_frame_timing_valid)
			{
				status_.compositor_frame_timing_frame_index = 0;
				status_.compositor_frame_timing_dropped_frames = 0;
				status_.compositor_frame_timing_mispresented = 0;
				status_.compositor_frame_timing_client_interval_ms = 0.0f;
				status_.compositor_frame_timing_present_cpu_ms = 0.0f;
				status_.compositor_frame_timing_wait_for_present_cpu_ms = 0.0f;
				status_.compositor_frame_timing_submit_frame_ms = 0.0f;
				status_.compositor_frame_timing_pre_submit_gpu_ms = 0.0f;
				status_.compositor_frame_timing_post_submit_gpu_ms = 0.0f;
				status_.compositor_frame_timing_total_render_gpu_ms = 0.0f;
				return;
			}

			status_.compositor_frame_timing_frame_index = timing.m_nFrameIndex;
			status_.compositor_frame_timing_dropped_frames = timing.m_nNumDroppedFrames;
			status_.compositor_frame_timing_mispresented = timing.m_nNumMisPresented;
			status_.compositor_frame_timing_client_interval_ms = timing.m_flClientFrameIntervalMs;
			status_.compositor_frame_timing_present_cpu_ms = timing.m_flPresentCallCpuMs;
			status_.compositor_frame_timing_wait_for_present_cpu_ms = timing.m_flWaitForPresentCpuMs;
			status_.compositor_frame_timing_submit_frame_ms = timing.m_flSubmitFrameMs;
			status_.compositor_frame_timing_pre_submit_gpu_ms = timing.m_flPreSubmitGpuMs;
			status_.compositor_frame_timing_post_submit_gpu_ms = timing.m_flPostSubmitGpuMs;
			status_.compositor_frame_timing_total_render_gpu_ms = timing.m_flTotalRenderGpuMs;
		}

		void complete_terminal_shutdown_locked() noexcept
		{
			if (present_transaction_.active) return;
			status_.desired_enabled = false;
			status_.reinitialize_pending = false;
			final_shutdown_ = true;
			shutdown_requested_ = false;
			present_transaction_traced_ = false;
			teardown_locked(true);
			publish_status_locked();
		}

		void publish_status_locked() noexcept
		{
			status_.openvr_input_diagnostics = input_actions_.diagnostics();
			status_.frame_phase = frame_phase_name(frame_phase_);
			status_.present_handoff_pending =
				frame_phase_ == frame_phase::submitted_waiting_present;
			status_.present_owner_transaction_active = present_transaction_.active;
			status_.present_owner_transaction_frame = present_transaction_.frame;
			status_.present_owner_transaction_generation = present_transaction_.generation;
			status_.present_owner_transaction_thread_id = present_transaction_.thread_id;
			const std::lock_guard lock(status_mutex_);
			status_snapshot_ = status_;
		}

		bool fail(const runtime_state state, const std::string& operation, const std::int64_t result,
			const std::string& result_name)
		{
			diagnostics::record_trace(diagnostics::trace_event::runtime_state_change,
				static_cast<std::uint64_t>(state), static_cast<std::uint64_t>(result));
			status_.state = state;
			status_.last_xr_result = result;
			status_.last_xr_result_name = result_name;
			status_.last_error = std::format("{} failed (OpenVR={} {})", operation, result, result_name);
			status_.failures.record(GetTickCount64(),status_.session_generation,status_.frame_context_id,result,
				operation,status_.last_initialization_stage,status_.last_error,status_.runtime_name,status_.system_name);
			status_.worker_last_completed_operation = operation + " (failed)";
			status_.worker_current_operation = "idle";
			status_.worker_last_status_update = GetTickCount64();
			return false;
		}

		void progress(const char* const stage, const char* const operation)
		{
			status_.last_initialization_stage = stage;
			status_.worker_current_operation = operation;
			status_.worker_last_status_update = GetTickCount64();
			publish_status_locked();
		}

		bool configure_eye_dimensions()
		{
			std::uint32_t recommended_width{};
			std::uint32_t recommended_height{};
			system_->GetRecommendedRenderTargetSize(&recommended_width, &recommended_height);
			if (!engine_scene_resolution::valid({recommended_width, recommended_height}))
			{
				return fail(runtime_state::runtime_unavailable, "GetRecommendedRenderTargetSize", -1,
					"recommended eye dimensions are outside the H2/D3D11 limits; no clamping is permitted");
			}
			const auto width = recommended_width;
			const auto height = recommended_height;
			for (auto& eye : status_.eyes)
			{
				eye.width = width;
				eye.height = height;
			}
			status_.recommended_eye_width = width;
			status_.recommended_eye_height = height;
			std::string error;
			if (!engine_scene_resolution::request({width, height}, error))
				return fail(runtime_state::fatal_for_vr, "native H2 scene resolution", -1, error);
			status_.color_format = DXGI_FORMAT_R8G8B8A8_UNORM;
			return true;
		}

		bool configure_direct_submission(const d3d11::device_snapshot& game_graphics,
			const std::uint32_t scene_owner_thread_id, std::string& error)
		{
			direct_graphics_ = {};
			status_.submission_on_game_device = false;
			status_.direct_present_owner_contract_valid = false;
			++status_.adapter_preflight_attempts;
			if ((game_graphics.creation.creation_flags & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
			{
				error = "H2 created D3D11 with SINGLETHREADED; native OpenVR cannot share its immediate context";
				++status_.adapter_preflight_failures;
				return false;
			}
			if (scene_owner_thread_id == 0 ||
				scene_owner_thread_id != GetCurrentThreadId())
			{
				error = "proven H2 scene owner is not the DXGI Present/OpenVR queue owner";
				++status_.adapter_preflight_failures;
				return false;
			}
			if (!steamvr::validate_compositor_adapter(system_, game_graphics.device.Get(), error))
			{
				++status_.adapter_preflight_failures;
				return false;
			}
			direct_graphics_ = game_graphics;
			status_.submission_on_game_device = true;
			status_.graphics_transport = "h2_device_direct";
			status_.direct_scene_owner_thread_id = scene_owner_thread_id;
			++status_.adapter_preflight_successes;
			error.clear();
			return true;
		}

		void initialize_submit_format_probe(const D3D11_TEXTURE2D_DESC& source) noexcept
		{
			format_probe_source_ = source;
			format_probe_index_ = 0;
			pending_pair_retirement_ = submitted_pair_retirement::none;
			probe_retirement_candidate_.reset();
			pending_retirement_error_.clear();
			status_.submit_format_probe = {};
			status_.submit_format_probe.state = "probing";
			status_.submit_format_probe.active = true;
			status_.submit_format_probe.current_candidate = 0;
			for (std::size_t index{}; index < submit_format_candidates.size(); ++index)
			{
				status_.submit_format_probe.candidates[index].format =
					static_cast<std::uint32_t>(submit_format_candidates[index]);
			}
		}

		bool activate_submit_format_candidate(const std::size_t index, std::string& error)
		{
			if (!direct_graphics_ || index >= submit_format_candidates.size())
			{
				error = "submit format probe candidate is outside its bounded matrix";
				return false;
			}
			format_probe_index_ = index;
			auto& probe = status_.submit_format_probe;
			probe.current_candidate = static_cast<std::uint32_t>(index);
			probe.state = "creating_candidate";
			auto& candidate = probe.candidates[index];
			candidate.creation_error.clear();
			UINT support{};
			const auto support_result = direct_graphics_.device->CheckFormatSupport(
				submit_format_candidates[index], &support);
			candidate.check_format_result = static_cast<std::int32_t>(support_result);
			candidate.format_support = support;
			if (FAILED(support_result))
			{
				error = std::format("CheckFormatSupport(format={}) failed (HRESULT=0x{:08X})",
					static_cast<std::uint32_t>(submit_format_candidates[index]),
					static_cast<std::uint32_t>(support_result));
				candidate.creation_error = error;
				return false;
			}
			if (!engine_scene_resolution::accepts_source({format_probe_source_.Width, format_probe_source_.Height}) ||
				format_probe_source_.Width != status_.recommended_eye_width ||
				format_probe_source_.Height != status_.recommended_eye_height)
			{
				error = "native submit source is not the exact SteamVR-recommended eye size";
				candidate.creation_error = error;
				return false;
			}
			if (!native_render_session::active().ensure_copy_ring(direct_graphics_,
				format_probe_source_, submit_format_candidates[index], error))
			{
				candidate.creation_error = error;
				return false;
			}
			candidate.ring_ready = true;
			status_.color_format = submit_format_candidates[index];
			status_.native_renderer_ready = true;
			status_.native_renderer_error.clear();
			status_.state = runtime_state::running;
			status_.session_state_name = std::format(
				"SteamVR connected; probing direct stereo format {}",
				static_cast<std::uint32_t>(submit_format_candidates[index]));
			status_.last_error.clear();
			status_.last_compositor_error.clear();
			pending_retirement_error_.clear();
			probe.state = "probing";
			return true;
		}

		bool require_suspended_native_acquisition(std::string& error) noexcept
		{
			const auto before = native_render_session::active().get_status();
			if (before.available &&
				!native_render_session::active().suspend_acquisition())
			{
				error = "native ownership ring refused acquisition suspension";
				return false;
			}
			const auto after = native_render_session::active().get_status();
			if (after.accepting_pairs)
			{
				error = "native ownership ring remained open after acquisition suspension";
				return false;
			}
			return true;
		}

		bool activate_next_submit_format_candidate(const std::size_t first,
			std::string& error)
		{
			error.clear();
			for (auto index = first; index < submit_format_candidates.size(); ++index)
			{
				std::string candidate_error;
				if (activate_submit_format_candidate(index, candidate_error)) return true;
				++status_.submit_format_probe.candidate_rejections;
				if (!error.empty()) error += "; ";
				error += std::format("candidate[{}] format {}: {}", index,
					static_cast<std::uint32_t>(submit_format_candidates[index]),
					candidate_error.empty() ? "creation failed without detail" : candidate_error);
			}
			return false;
		}

		void finish_submit_format_probe_without_lossless(std::string reason)
		{
			native_render_session::active().invalidate(status_.device_generation);
			direct_graphics_ = {};
			status_.native_renderer_ready = false;
			status_.submission_on_game_device = false;
			status_.graphics_transport = "gpu_transport_unarmed";
			status_.color_format = 0;
			status_.submit_format_probe.active = false;
			status_.submit_format_probe.complete = true;
			status_.submit_format_probe.state = "no_lossless_format";
			status_.state = runtime_state::session_idle;
			status_.session_state_name =
				"SteamVR connected; format probe complete; lossless GPU transport unarmed";
			if (reason.empty())
			{
				reason =
					"SteamVR accepted no lossless direct stereo format; lossy probe results are diagnostic only";
			}
			status_.last_error = std::move(reason);
			status_.last_compositor_error = status_.last_error;
			prepared_frame_ = {};
			clear_prepared_pair_timing();
			frame_phase_ = frame_phase::idle_unarmed;
			engine_stereo_bridge::invalidate_views();
			pending_retirement_error_.clear();
		}

		bool try_arm_gpu_transport(const d3d11::device_snapshot& graphics)
		{
			if (gpu_frame_transport_is_armed(status_)) return true;
			if (!engine_scene_resolution::ready())
			{
				const auto resolution = engine_scene_resolution::get_report();
				if (resolution.state == engine_scene_resolution::phase::failed)
					return fail(runtime_state::fatal_for_vr, "native H2 scene resolution", -1, resolution.error);
				status_.native_renderer_error = "awaiting H2 coordinated native eye resolution rebuild";
				return true;
			}
			if (status_.submit_format_probe.complete &&
				status_.submit_format_probe.selected_format == 0)
			{
				return true;
			}
			const auto proof = engine_stereo_owner_pass::get_report();
			if (proof.state != engine_stereo_owner_pass::gate_state::complete) return true;
			if (proof.error != engine_stereo_owner_pass::failure::none ||
				proof.completed_eye_mask != 0x3 || !proof.eyes_distinct ||
				proof.nonzero_bytes[0] == 0 || proof.nonzero_bytes[1] == 0 ||
				proof.device_removed_reason != S_OK ||
				proof.context != reinterpret_cast<std::uintptr_t>(graphics.context.Get()) ||
				proof.device_generation != graphics.generation ||
				proof.owner_thread_id != GetCurrentThreadId())
			{
				return fail(runtime_state::fatal_for_vr,
					"native stereo proof admission", -1,
					"completed proof does not belong to the current Present-owner device context");
			}

			D3D11_TEXTURE2D_DESC source{};
			source.Width = proof.width;
			source.Height = proof.height;
			source.MipLevels = proof.mip_levels;
			source.ArraySize = proof.array_size;
			source.Format = static_cast<DXGI_FORMAT>(proof.format);
			source.SampleDesc = {proof.sample_count, proof.sample_quality};
			source.Usage = static_cast<D3D11_USAGE>(proof.usage);
			source.BindFlags = proof.bind_flags;
			source.CPUAccessFlags = proof.cpu_access_flags;
			source.MiscFlags = proof.misc_flags;
			if (!engine_stereo_owner_pass::supports_readback_source(source) ||
				!engine_scene_resolution::accepts_source({source.Width, source.Height}) ||
				source.Width != status_.recommended_eye_width || source.Height != status_.recommended_eye_height)
			{
				return fail(runtime_state::fatal_for_vr,
					"native stereo target-4 contract", -1,
					"proof source descriptor is not the exact H2 HDR scene target");
			}

			std::string error;
			if (!configure_direct_submission(graphics, proof.owner_thread_id, error))
			{
				// The device, owner-thread, and adapter identities cannot change inside
				// this OpenVR/device generation. Preserve one exact failure instead of
				// retrying the same rejected preflight on every Present.
				return fail(runtime_state::fatal_for_vr,
					"native same-device OpenVR preflight", -1, error);
			}
			initialize_submit_format_probe(source);
			if (!require_suspended_native_acquisition(error))
			{
				direct_graphics_ = {};
				status_.submission_on_game_device = false;
				status_.graphics_transport = "gpu_transport_unarmed";
				return fail(runtime_state::fatal_for_vr,
					"native HDR eye ownership suspension", -1, error);
			}
			if (!activate_next_submit_format_candidate(0, error))
			{
				finish_submit_format_probe_without_lossless(std::format(
					"no submit-format candidate could be created: {}", error));
				return true;
			}

			status_.native_renderer_ready = true;
			status_.native_renderer_error.clear();
			status_.state = runtime_state::running;
			status_.session_state_name =
				"SteamVR connected; native stereo submit-format probe armed";
			status_.last_error.clear();
			status_.last_compositor_error.clear();
			prepared_frame_ = {};
			clear_pacing_transients();
			frame_phase_ = frame_phase::need_pose;
			engine_stereo_bridge::invalidate_views();
			return true;
		}

		bool complete_pending_pair_retirement()
		{
			if (probe_retirement_candidate_)
			{
				status_.submit_format_probe.candidates[*probe_retirement_candidate_].retired = true;
				probe_retirement_candidate_.reset();
			}
			auto action = pending_pair_retirement_;
			pending_pair_retirement_ = submitted_pair_retirement::none;
			if (action == submitted_pair_retirement::none) return true;
			if (!direct_graphics_ ||
				FAILED(direct_graphics_.device->GetDeviceRemovedReason()))
			{
				pending_retirement_error_ =
					"D3D11 device became invalid before the format-probe retirement boundary";
				action = submitted_pair_retirement::fatal_after_wait_get_poses;
			}

			if (action == submitted_pair_retirement::confirm_lossless_format)
			{
				status_.submit_format_probe.active = false;
				status_.submit_format_probe.complete = true;
				status_.submit_format_probe.state = "lossless_format_selected";
				status_.submit_format_probe.selected_format =
					submit_format_candidates[format_probe_index_];
				status_.session_state_name =
					"SteamVR connected; lossless native stereo transport running";
				status_.state = runtime_state::running;
				status_.last_error.clear();
				status_.last_compositor_error.clear();
				pending_retirement_error_.clear();
				return true;
			}

			if (action == submitted_pair_retirement::resume_after_focus)
			{
				// Focus loss says nothing about format support. Retry the same ring
				// and candidate only after the compositor has retired both eye calls.
				if (status_.submit_format_probe.active)
					status_.submit_format_probe.state = "probing";
				status_.state = runtime_state::running;
				status_.last_error.clear();
				status_.last_compositor_error.clear();
				pending_retirement_error_.clear();
				return true;
			}

			if (action == submitted_pair_retirement::advance_format_probe)
			{
				std::string error;
				const auto first_candidate = format_probe_index_ + 1;
				if (activate_next_submit_format_candidate(first_candidate, error)) return true;
				pending_retirement_error_ = std::format(
					"submit format probe exhausted candidates [{}..{}]: {}",
					first_candidate, submit_format_candidates.size() - 1, error);
				action = submitted_pair_retirement::finish_unqualified_format_probe;
			}

			if (action == submitted_pair_retirement::finish_unqualified_format_probe)
			{
				finish_submit_format_probe_without_lossless(
					std::move(pending_retirement_error_));
				return false;
			}

			frame_phase_ = frame_phase::fatal_holding_pair;
			engine_stereo_bridge::invalidate_views();
			status_.state = runtime_state::fatal_for_vr;
			status_.submit_format_probe.active = false;
			status_.submit_format_probe.state = "failed";
			status_.last_error = pending_retirement_error_.empty()
				? "direct stereo submission failed after safe WaitGetPoses retirement"
				: pending_retirement_error_;
			status_.last_compositor_error = status_.last_error;
			pending_retirement_error_.clear();
			return false;
		}

		bool sample_frame_views_locked(const std::uint64_t frame_index,
			const bool prepare_gpu_frame,const bool ui_only=false)
		{
			const auto gpu_transport_armed = gpu_frame_transport_is_armed(status_);
			if (!ui_only && prepare_gpu_frame != gpu_transport_armed)
			{
				return fail(runtime_state::fatal_for_vr,
					"OpenVR pose sampling contract", -1,
					prepare_gpu_frame ? "GPU transport is not armed" :
						"tracking-only sampling was requested after GPU transport was armed");
			}
			if (!prepare_gpu_frame && !ui_only && (prepared_frame_.valid || prepared_native_pair_id_ != 0 ||
				submitted_native_pair_id_ != 0))
			{
				return fail(runtime_state::fatal_for_vr,
					"OpenVR tracking-only ownership gate", -1,
					"GPU frame ownership exists while transport is unarmed");
			}
			if (prepare_gpu_frame)
			{
				diagnostics::record_trace(
					diagnostics::trace_event::runtime_direct_submission_frame,
					frame_index, status_.device_generation);
				progress("views", "IVRCompositor::WaitGetPoses (DXGI Present-post boundary)");
			}
			else
			{
				status_.worker_current_operation =
					"IVRCompositor::WaitGetPoses (tracking-only Present-post)";
				status_.worker_last_status_update = GetTickCount64();
				publish_status_locked();
			}
			diagnostics::record_trace(diagnostics::trace_event::runtime_wait_get_poses,
				frame_index, status_.device_generation);
			std::array<TrackedDevicePose_t, k_unMaxTrackedDeviceCount> poses{};
			EVRCompositorError pose_result{};
			std::uint64_t wait_get_poses_us{};
			{
				diagnostics::gpu_interop_entered(0, 0);
				const auto interop_exit = gsl::finally([]
				{
					diagnostics::gpu_interop_exited();
				});
				const auto wait_get_poses_started = pacing_clock::now();
				pose_result = compositor_api_->WaitGetPoses(poses.data(),
					static_cast<std::uint32_t>(poses.size()), nullptr, 0);
				wait_get_poses_us = elapsed_microseconds(wait_get_poses_started);
			}
			record_duration(wait_get_poses_us, status_.wait_get_poses_call_count,
				status_.wait_get_poses_last_us, status_.wait_get_poses_max_us,
				status_.wait_get_poses_total_us);
			if (wait_get_poses_us >= 2'000) ++status_.wait_get_poses_at_least_2ms_count;
			if (wait_get_poses_us >= 11'000) ++status_.wait_get_poses_at_least_11ms_count;
			if (wait_get_poses_us >= 50'000) ++status_.wait_get_poses_at_least_50ms_count;
			if (wait_get_poses_us >= 100'000) ++status_.wait_get_poses_at_least_100ms_count;
			diagnostics::record_trace(diagnostics::trace_event::runtime_wait_get_poses_result,
				frame_index, static_cast<std::uint64_t>(pose_result));
			if (pose_result != VRCompositorError_None)
			{
				ui_head_valid_=false;
					if(pose_result==VRCompositorError_DoNotHaveFocus){set_pause_dim(0);set_theater(false);}
					controller_input::invalidate(pose_result==VRCompositorError_DoNotHaveFocus ?
						controller_input::input_reason::compositor_focus_lost : controller_input::input_reason::tracking_failed,
						controller_input::input_backend::openvr,pose_result);
				head_pose_bridge::invalidate_pose();
				++status_.tracking_pose_failure_count;
				++status_.frame_context_miss_count;
				prepared_frame_ = {};
				clear_prepared_pair_timing();
				if (submitted_native_pair_id_ != 0 || pose_result == VRCompositorError_DoNotHaveFocus)
				{
					// Focus can be unavailable before the first submission, too. A
					// failed boundary provides no retirement proof. Keep every submitted
					// resource and retry WaitGetPoses on a later matched Present instead of
					// detaching OpenVR or releasing compositor-visible memory.
					frame_phase_ = frame_phase::need_pose;
					engine_stereo_bridge::invalidate_views();
					status_.state = runtime_state::recoverable_error;
					status_.last_xr_result = pose_result;
					status_.last_xr_result_name = compositor_error_name(pose_result);
					status_.last_error = std::format(
						"IVRCompositor::WaitGetPoses deferred (OpenVR={} {})",
						static_cast<std::int32_t>(pose_result), compositor_error_name(pose_result));
					status_.last_compositor_error = status_.last_error;
					return false;
				}
				frame_phase_ = frame_phase::fatal_holding_pair;
				engine_stereo_bridge::invalidate_views();
				return fail(runtime_state::fatal_for_vr, "IVRCompositor::WaitGetPoses",
					pose_result, compositor_error_name(pose_result));
			}
			if (submitted_native_pair_id_ != 0)
			{
				// WaitGetPoses is the first documented next-frame boundary after the
				// complete stereo Submit. Only now may H2 render into this pair again.
				diagnostics::record_trace(
					diagnostics::trace_event::runtime_submission_retire,
					submitted_native_pair_id_, status_.submitted_frames);
				if (!native_render_session::active().release_pair(submitted_native_pair_id_))
				{
					prepared_frame_ = {};
					frame_phase_ = frame_phase::fatal_holding_pair;
					engine_stereo_bridge::invalidate_views();
					return fail(runtime_state::fatal_for_vr,
						"native direct submission pair retirement", -1,
						"submitted pair was not published in the ownership ring");
				}
				record_submitted_pair_retirement();
				submitted_native_pair_id_ = 0;
				// GetFrameTiming is compositor IPC and framesAgo=0 describes the latest
				// available history entry, not necessarily the pair just submitted. Sample
				// it sparsely at a proven next-frame retirement boundary so diagnostics do
				// not perturb the Submit hot path we are measuring.
				if (status_.compositor_frame_timing_query_count == 0 ||
					status_.direct_pairs_retired % 64 == 0)
				{
					sample_compositor_frame_timing();
				}
				++status_.direct_pairs_retired;
				if (!complete_pending_pair_retirement()) return false;
			}

			const auto& hmd_pose = poses[k_unTrackedDeviceIndex_Hmd];
				if (!hmd_pose.bDeviceIsConnected || !hmd_pose.bPoseIsValid)
				{
					ui_head_valid_=false;
					controller_input::invalidate(!hmd_pose.bDeviceIsConnected ? controller_input::input_reason::hmd_disconnected :
						controller_input::input_reason::hmd_pose_invalid,controller_input::input_backend::openvr);
				diagnostics::record_trace(diagnostics::trace_event::runtime_state_change,
					static_cast<std::uint64_t>(runtime_state::no_hmd),
					(hmd_pose.bDeviceIsConnected ? 2ull : 0ull) |
						(hmd_pose.bPoseIsValid ? 1ull : 0ull));
				diagnostics::record_trace(diagnostics::trace_event::runtime_wait_get_poses_result,
					frame_index, (hmd_pose.bDeviceIsConnected ? 2ull : 0ull) |
						(hmd_pose.bPoseIsValid ? 1ull : 0ull));
				++status_.frame_context_miss_count;
				++status_.tracking_pose_failure_count;
				head_pose_bridge::invalidate_pose();
				engine_stereo_bridge::invalidate_views();
				status_.state = runtime_state::no_hmd;
				status_.last_error = "SteamVR has no valid HMD render pose";
				prepared_frame_ = {};
				clear_prepared_pair_timing();
				return false;
			}

			const auto& hmd_transform = hmd_pose.mDeviceToAbsoluteTracking;
			ui_head_.position_meters={hmd_transform.m[0][3],hmd_transform.m[1][3],hmd_transform.m[2][3]};
			for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)ui_head_.orientation[row][col]=hmd_transform.m[row][col];
			ui_head_valid_=true;
			head_pose_bridge::publish_tracking_pose({
				{hmd_transform.m[0][3], hmd_transform.m[1][3], hmd_transform.m[2][3]},
				{{
					{hmd_transform.m[0][0], hmd_transform.m[0][1], hmd_transform.m[0][2]},
					{hmd_transform.m[1][0], hmd_transform.m[1][1], hmd_transform.m[1][2]},
					{hmd_transform.m[2][0], hmd_transform.m[2][1], hmd_transform.m[2][2]},
				}},
			},frame_index,std::chrono::steady_clock::now());

			input_actions_.sample(system_->IsInputAvailable());
			if(ui_only)
			{
				engine_stereo_bridge::invalidate_views();++status_.tracking_pose_sample_count;
				status_.tracking_last_present_frame=frame_index;status_.state=runtime_state::session_idle;
				status_.session_state_name=movie_mode_?"native fullscreen video; curved video overlay":"native 2D frontend; spatial menu overlays";status_.last_error.clear();
				return true;
			}

			std::array<std::array<float, 3>, 2> eye_positions{};
			for (std::size_t index{}; index < eye_positions.size(); ++index)
			{
				const auto eye_transform = system_->GetEyeToHeadTransform(index == 0 ? Eye_Left : Eye_Right);
				eye_positions[index] = {eye_transform.m[0][3], eye_transform.m[1][3], eye_transform.m[2][3]};
			}

			std::array<engine_stereo_bridge::eye_projection, 2> projections{};
			for (std::size_t index{}; index < projections.size(); ++index)
			{
				float left{};
				float right{};
				float top{};
				float bottom{};
				system_->GetProjectionRaw(index == 0 ? Eye_Left : Eye_Right,
					&left, &right, &top, &bottom);
				const auto tangents = projection_tangents_from_raw(
					left, right, top, bottom);
				projections[index] = {tangents.left, tangents.right,
					tangents.down, tangents.up};
			}

			if (prepare_gpu_frame &&
				!native_render_session::active().admit_pair(frame_index))
			{
				++status_.frame_context_miss_count;
				prepared_frame_ = {};
				clear_prepared_pair_timing();
				frame_phase_ = frame_phase::fatal_holding_pair;
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error = std::format(
					"native ownership ring rejected exact prepared pair {}", frame_index);
				status_.last_compositor_error = status_.last_error;
				engine_stereo_bridge::invalidate_views();
				return false;
			}

			if (!engine_stereo_bridge::publish_view_family(frame_index, eye_positions[0],
				eye_positions[1], projections))
			{
				if (prepare_gpu_frame)
				{
					(void)native_render_session::active().suspend_acquisition();
				}
				++status_.tracking_pose_failure_count;
				prepared_frame_ = {};
				clear_prepared_pair_timing();
				frame_phase_ = frame_phase::fatal_holding_pair;
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error = "engine stereo bridge rejected the predicted OpenVR view family";
				status_.last_compositor_error = status_.last_error;
				++status_.frame_context_miss_count;
				return false;
			}
			++status_.tracking_pose_sample_count;
			status_.tracking_last_present_frame = frame_index;
			status_.last_xr_result = VRCompositorError_None;
			status_.last_xr_result_name = compositor_error_name(VRCompositorError_None);
			status_.last_compositor_error.clear();
			if (!prepare_gpu_frame)
			{
				status_.state = runtime_state::session_idle;
				status_.session_state_name =
					"SteamVR connected; tracking active; GPU frame transport unarmed";
				status_.last_error = status_.submit_format_probe.complete &&
					status_.submit_format_probe.selected_format == 0
					? "OpenVR GPU transport remains unarmed because the format probe found no lossless submission format"
					: "OpenVR GPU transport is unarmed until H2 scene input isolation and backend eye targets are proven";
				status_.worker_last_completed_operation =
					"IVRCompositor::WaitGetPoses (tracking-only Present-post)";
				status_.worker_current_operation = "idle";
				status_.worker_last_status_update = GetTickCount64();
				return true;
			}

			prepared_frame_ = {true, frame_index, status_.device_generation, projections};
			prepared_pair_started_at_ = pacing_clock::now();
			prepared_pair_pending_presents_ = 0;
			status_.frame_context_id = frame_index;
			++status_.frame_context_prepare_count;
			status_.state = runtime_state::running;
			status_.last_error.clear();
			return true;
		}

		bool initialize_locked(const d3d11::device_snapshot& graphics)
		{
			clear_pacing_transients();
			status_.compositor_frame_timing_valid = false;
			++status_.initialization_attempt_count;
			status_.sdk_headers_available = true;
			status_.last_error.clear();
			status_.submit_format_probe = {};
			status_.application_registered = false;
			status_.application_registration_error.clear();
			format_probe_source_ = {};
			format_probe_index_ = 0;
			pending_pair_retirement_ = submitted_pair_retirement::none;
			probe_retirement_candidate_.reset();
			pending_retirement_error_.clear();
			progress("graphics", "validate graphics and prior SteamVR session");
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

			// Native engine-stereo is the only supported source. Reject the
			// configuration before opening SteamVR when the exact H2 target or the
			// native scene hook is not verified; otherwise the compositor can remain
			// in its indefinite "waiting" state until a level begins rendering.
			const auto bridge = engine_stereo_bridge::get_status();
			if (!bridge.target_matched)
			{
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error = "native engine-stereo target identity is not verified";
				return false;
			}
			if (!bridge.render_hook_installed)
			{
				status_.state = runtime_state::fatal_for_vr;
				status_.last_error = "native engine-stereo scene hook is not installed";
				return false;
			}
			if (system_ != nullptr) teardown_locked(false);

			// OpenVR owns its runtime discovery through openvrpaths.vrpath. Do not
			// force the client DLL from the active OpenXR manifest: OpenXR runtime
			// selection and OpenVR client discovery are separate contracts, and the
			// manifest may name a different architecture or vendor runtime.
			progress("runtime_registry", "resolve OpenVR path registry");
			std::string path_registry_diagnostic;
			steamvr::path_registry_scope path_registry(path_registry_diagnostic);
			if (const auto ipc_diagnostic = steamvr::diagnose_ipc_environment(); !ipc_diagnostic.empty())
			{
				return fail(runtime_state::runtime_unavailable, "SteamVR_Namespace", ERROR_ACCESS_DENIED,
					ipc_diagnostic);
			}
			char runtime_path[2048]{};
			std::uint32_t required_runtime_path_size{};
			if (!VR_GetRuntimePath(runtime_path, sizeof(runtime_path),
				&required_runtime_path_size))
			{
				const auto detail = path_registry_diagnostic.empty()
					? "OpenVR path registry did not resolve a runtime" : path_registry_diagnostic;
				return fail(runtime_state::runtime_unavailable, "VR_GetRuntimePath", -1, detail);
			}
			status_.runtime_library = runtime_path;
			status_.loader_loaded = true;
			progress("instance", "VR_Init");
			EVRInitError initialize_error{VRInitError_None};
			system_ = VR_Init(&initialize_error, VRApplication_Scene);
			if (initialize_error != VRInitError_None || system_ == nullptr)
			{
				system_ = nullptr;
				return fail(runtime_state::runtime_unavailable, "VR_Init", initialize_error,
					VR_GetVRInitErrorAsSymbol(initialize_error));
			}
			status_.instance_created = true;
			progress("application", "register SteamVR application identity and cover");
			status_.application_registration_error = steamvr::register_application();
			status_.application_registered = status_.application_registration_error.empty();
			// Valve requires manifest registration before the first PollNextEvent.
			// Input failure is reported separately; it must not destroy GPU ownership.
			status_.controller_input_ready = input_actions_.initialize(system_);
			status_.controller_input_error = input_actions_.error();
			status_.runtime_name = tracked_string(system_, Prop_TrackingSystemName_String);
			if (status_.runtime_name.empty()) status_.runtime_name = "SteamVR";
			status_.system_name = tracked_string(system_, Prop_ModelNumber_String);
			status_.last_runtime_name = status_.runtime_name;
			status_.last_system_name = status_.system_name;

			progress("compositor", "VRCompositor");
			compositor_api_ = VRCompositor();
			if (compositor_api_ == nullptr)
			{
				const auto result = fail(runtime_state::runtime_unavailable, "VRCompositor", -1,
					"IVRCompositor unavailable");
				teardown_preserving_error();
				return result;
			}
			compositor_api_->SetTrackingSpace(TrackingUniverseStanding);
			menu_overlay_.initialize(VROverlay());
			status_.session_created = true;

			progress("swapchains", "query native OpenVR eye dimensions");
			if (!configure_eye_dimensions())
			{
				teardown_preserving_error();
				return false;
			}
			// Keep tracking live while the process-local isolated owner proof validates
			// both target-4 images. The next Present-pre arms the only production path
			// after that proof reaches complete; no desktop or shared-texture source exists.
			status_.device_generation = graphics.generation;
			status_.view_count = 2;
			status_.effective_scene_mode = scene_mode::engine_stereo;
			status_.session_running = true;
			status_.applied_enabled = true;
			status_.reinitialize_pending = false;
			status_.state = runtime_state::session_idle;
			status_.native_renderer_ready = false;
			status_.submission_on_game_device = false;
			status_.graphics_transport = "gpu_transport_unarmed";
			status_.native_renderer_error =
				"awaiting isolated outer-owner target-4 GPU content proof";
			status_.last_error =
				"OpenVR GPU transport is unarmed until the process-local target-4 proof completes";
			status_.session_state_name = "SteamVR connected; GPU frame transport unarmed";
			status_.last_xr_result = 0;
			status_.last_xr_result_name = "VRInitError_None";
			engine_stereo_bridge::invalidate_views();
			frame_phase_ = frame_phase::idle_unarmed;
			++status_.session_generation;
			++status_.session_begin_count;
			// Connection recreation starts a new tracking-origin lifetime.
			head_pose_bridge::request_recenter();
			status_.worker_last_completed_operation = "backend.initialize";
			status_.worker_current_operation = "idle";
			status_.last_initialization_stage = "complete";
			return true;
		}

		void teardown_preserving_error() noexcept
		{
			const auto state = status_.state;
			const auto error = status_.last_error;
			const auto result = status_.last_xr_result;
			const auto result_name = status_.last_xr_result_name;
			const auto stage = status_.last_initialization_stage;
			teardown_locked(false);
			status_.state = state;
			status_.last_error = error;
			status_.last_xr_result = result;
			status_.last_xr_result_name = result_name;
			status_.last_initialization_stage = stage;
		}

		void discard_prepared_native_pair(const bool quarantine) noexcept
		{
			const auto pair_id = prepared_native_pair_id_;
			prepared_native_pair_ = {};
			prepared_native_pair_id_ = 0;
			if (pair_id == 0) return;
			if (quarantine)
			{
				native_render_session::active().quarantine_pair(pair_id);
			}
			else
			{
				(void)native_render_session::active().release_pair(pair_id);
			}
		}

		void update_movie_mode(const native_menu::presentation& mode,const native_menu::state& ui) noexcept
		{
			const auto now=GetTickCount64();
			const bool fresh=ui.enabled&&now>=ui.timestamp&&now-ui.timestamp<=250;
			movie_mode_=menu_surface::movie_theater(mode.enabled,mode.video,mode.frontend,mode.scene,
				fresh,ui.count,ui.briefing,mode.fullscreen_video);
			// A fresh briefing or the native fullscreen draw owns a movie over an
			// initialized map. World-material videos cannot inherit that ownership.
		}
		void apply_scene_fade() noexcept
		{
			const float alpha=theater_active_?1.f:pause_dim_;
			if(compositor_api_&&alpha!=scene_fade_)
			{compositor_api_->FadeToColor(0,0,0,0,alpha,false);scene_fade_=alpha;}
		}
		void set_pause_dim(float alpha) noexcept
		{pause_dim_=std::isfinite(alpha)?std::clamp(alpha,0.f,1.f):0;apply_scene_fade();}
		void set_theater(bool enabled) noexcept
		{
			if(!compositor_api_)return;
			// Valve's loading-overlay pattern explicitly drops to the compositor
			// before stopping scene submission. A black foreground fade hides its
			// grid, while application overlays remain visible above that backdrop.
			if(theater_active_!=enabled)compositor_api_->FadeGrid(0,enabled);
			theater_active_=enabled;apply_scene_fade();
		}

		void teardown_locked(const bool final_shutdown) noexcept
		{
			movie_mode_=false;
			set_pause_dim(0);
			set_theater(false);
			menu_overlay_.shutdown();ui_head_valid_=false;
			input_actions_.reset();
			status_.controller_input_ready = false;
			status_.application_registered = false;
			diagnostics::record_trace(diagnostics::trace_event::runtime_teardown_begin,
				status_.device_generation, (final_shutdown ? 1ull << 63 : 0) |
					submitted_native_pair_id_);
			clear_pacing_transients();
			status_.compositor_frame_timing_valid = false;
			if (system_ != nullptr || compositor_api_ != nullptr) ++status_.cleanup_count;
			engine_stereo_bridge::invalidate_views();
			head_pose_bridge::invalidate_pose();
			(void)native_render_session::active().suspend_acquisition();
			// native_render_session owns every submitted/quarantined target until
			// OpenVR has completely detached from the application.
			if (system_ != nullptr)
			{
				diagnostics::record_trace(diagnostics::trace_event::runtime_teardown_stage,
					1, submitted_native_pair_id_);
				VR_Shutdown();
				diagnostics::record_trace(diagnostics::trace_event::runtime_teardown_stage,
					2, submitted_native_pair_id_);
				system_ = nullptr;
			}
			menu_overlay_.runtime_detached();
			compositor_api_ = nullptr;
			discard_prepared_native_pair(false);
			if (submitted_native_pair_id_ != 0)
			{
				(void)native_render_session::active().release_pair(submitted_native_pair_id_);
				submitted_native_pair_id_ = 0;
			}
			direct_graphics_ = {};
			diagnostics::record_trace(diagnostics::trace_event::runtime_teardown_stage,
				3, status_.device_generation);
			native_render_session::active().invalidate(status_.device_generation);
			diagnostics::record_trace(diagnostics::trace_event::runtime_teardown_stage,
				4, status_.device_generation);
			format_probe_source_ = {};
			format_probe_index_ = 0;
			pending_pair_retirement_ = submitted_pair_retirement::none;
			probe_retirement_candidate_.reset();
			pending_retirement_error_.clear();
			prepared_frame_ = {};
			frame_phase_ = frame_phase::need_pose;
			if (status_.session_running) ++status_.session_end_count;
			status_.applied_enabled = false;
			status_.loader_loaded = false;
			status_.instance_created = false;
			status_.session_created = false;
			status_.session_running = false;
			status_.view_count = 0;
			status_.color_format = 0;
			status_.recommended_eye_width = 0;
			status_.recommended_eye_height = 0;
			status_.eyes = {};
			status_.runtime_name.clear();
			status_.system_name.clear();
			status_.device_generation = 0;
			status_.native_renderer_ready = false;
			status_.submission_on_game_device = false;
			status_.direct_present_owner_contract_valid = false;
			status_.direct_scene_owner_thread_id = 0;
			present_owner_generation_ = 0;
			present_owner_thread_id_ = 0;
			status_.native_renderer_error.clear();
			status_.state = final_shutdown || !status_.desired_enabled
				? runtime_state::disabled : runtime_state::waiting_for_graphics;
		}

		scene_prepare_result fail_native_scene_locked(std::string error,
			std::uint64_t pair_id = 0) noexcept
		{
			if (prepared_native_pair_id_ != 0)
			{
				const auto prepared_pair_id = prepared_native_pair_id_;
				discard_prepared_native_pair(true);
				if (pair_id == prepared_pair_id) pair_id = 0;
			}
			if (pair_id != 0)
			{
				native_render_session::active().quarantine_pair(pair_id);
			}
			prepared_frame_ = {};
			clear_prepared_pair_timing();
			frame_phase_ = frame_phase::fatal_holding_pair;
			engine_stereo_bridge::invalidate_views();
			status_.last_compositor_error = std::move(error);
			status_.last_error = status_.last_compositor_error;
			status_.state = runtime_state::fatal_for_vr;
			++status_.compositor_source_miss_count;
			diagnostics::record_trace(diagnostics::trace_event::runtime_source_pair_result,
				status_.submitted_frames, 2);
			diagnostics::record_trace(diagnostics::trace_event::runtime_prepare_scene_result,
				static_cast<std::uint64_t>(scene_mode::engine_stereo), 2);
			return scene_prepare_result::fatal;
		}

		scene_prepare_result prepare_scene()
		{
			diagnostics::record_trace(diagnostics::trace_event::runtime_prepare_scene,
				static_cast<std::uint64_t>(status_.requested_scene_mode), status_.submitted_frames);
			status_.effective_scene_mode = scene_mode::engine_stereo;
			status_.last_compositor_error.clear();
			if (status_.requested_scene_mode != scene_mode::engine_stereo)
			{
				return fail_native_scene_locked("strict VR requires scene_mode engine_stereo");
			}
			if (prepared_native_pair_id_ != 0)
			{
				return fail_native_scene_locked(
					"a native pair remained prepared across renderer submission boundaries");
			}
			++status_.compositor_prepare_count;
			diagnostics::record_trace(diagnostics::trace_event::runtime_source_pair,
				status_.submitted_frames, status_.device_generation);
			if (native_render_session::active().pair_deferred(prepared_frame_.frame_id))
			{
				// The renderer learned an exact H2 arena identity after this pose family
				// had already started. It freed the unpublished native slot; now discard only
				// the matching pose and acquire a new coherent family at Present-post.
				prepared_frame_ = {};
				clear_prepared_pair_timing();
				frame_phase_ = frame_phase::need_pose;
				engine_stereo_bridge::invalidate_views();
				++status_.direct_pairs_deferred;
				status_.state = runtime_state::running;
				status_.last_error.clear();
				status_.last_compositor_error.clear();
				diagnostics::record_trace(diagnostics::trace_event::runtime_source_pair_result,
					status_.submitted_frames, 3);
				diagnostics::record_trace(diagnostics::trace_event::runtime_prepare_scene_result,
					static_cast<std::uint64_t>(scene_mode::engine_stereo), 3);
				return scene_prepare_result::deferred;
			}
			if (native_render_session::active().pair_failed(prepared_frame_.frame_id))
			{
				// Preserve the producer's exact rejection instead of replacing it with
				// the generic family error. Only use evidence for this pair/generation.
				const auto rejection = native_render_session::active().get_status().last_copy_failure;
				if (rejection.stage != native_render_session::copy_failure::none &&
					rejection.pair_id == prepared_frame_.frame_id &&
					rejection.device_generation == status_.device_generation)
				{
					return fail_native_scene_locked(std::format(
						"H2 native copy rejected: stage={} pair={} eye={} HRESULT=0x{:08X}; "
						"see native_copy_failure in vr_status",
						native_render_session::to_string(rejection.stage), rejection.pair_id,
						rejection.eye, static_cast<std::uint32_t>(rejection.result)),
						prepared_frame_.frame_id);
				}
				return fail_native_scene_locked(
					"H2 rejected the predicted native stereo family before direct submission",
					prepared_frame_.frame_id);
			}
			if (!native_render_session::active().pair_published(prepared_frame_.frame_id))
			{
				status_.last_compositor_error =
					"waiting for H2 to complete the predicted native stereo scene passes";
				diagnostics::record_trace(diagnostics::trace_event::runtime_source_pair_result,
					status_.submitted_frames, 0);
				diagnostics::record_trace(diagnostics::trace_event::runtime_prepare_scene_result,
					static_cast<std::uint64_t>(scene_mode::engine_stereo), 0);
				return scene_prepare_result::pending;
			}

			std::array<native_render_session::eye_target, 2> pair{};
			if (!native_render_session::active().acquire_published_pair(
				prepared_frame_.frame_id, pair))
			{
				return fail_native_scene_locked(
					"native stereo pair could not be acquired from its ownership ring",
					prepared_frame_.frame_id);
			}

			bool contract_valid = direct_graphics_ &&
				direct_graphics_.generation == status_.device_generation;
			for (std::size_t index{}; index < pair.size(); ++index)
			{
				D3D11_TEXTURE2D_DESC description{};
				if (pair[index].color) pair[index].color->GetDesc(&description);
				contract_valid = contract_valid && pair[index].color != nullptr &&
					texture_uses_device(pair[index].color.Get(), direct_graphics_.device.Get()) &&
					pair[index].width == status_.eyes[index].width &&
					pair[index].height == status_.eyes[index].height &&
					description.Width == status_.eyes[index].width &&
					description.Height == status_.eyes[index].height &&
					description.MipLevels == 1 && description.ArraySize == 1 &&
					description.Format == static_cast<DXGI_FORMAT>(status_.color_format) &&
					description.SampleDesc.Count == 1 &&
					description.Usage == D3D11_USAGE_DEFAULT &&
					(description.BindFlags & (D3D11_BIND_RENDER_TARGET |
						D3D11_BIND_SHADER_RESOURCE)) ==
						(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE) &&
					description.CPUAccessFlags == 0 && description.MiscFlags == 0;
			}
			if (!contract_valid)
			{
				return fail_native_scene_locked(
					"native stereo pair violated the exact same-device non-shared Submit contract",
					prepared_frame_.frame_id);
			}
			prepared_native_pair_ = std::move(pair);
			prepared_native_pair_id_ = prepared_frame_.frame_id;
			++status_.direct_pairs_acquired;
			diagnostics::record_trace(diagnostics::trace_event::runtime_source_pair_result,
				status_.submitted_frames, 1);
			diagnostics::record_trace(diagnostics::trace_event::runtime_prepare_scene_result,
				static_cast<std::uint64_t>(scene_mode::engine_stereo), 1);
			return scene_prepare_result::ready;
		}

		bool poll_events()
		{
			VREvent_t event{};
			while (system_->PollNextEvent(&event, sizeof(event)))
			{
				input_actions_.observe_event(event);
				if (event.eventType == VREvent_Quit)
				{
					diagnostics::record_trace(diagnostics::trace_event::runtime_event,
						event.eventType, event.trackedDeviceIndex);
					system_->AcknowledgeQuit_Exiting();
					status_.reinitialize_pending = true;
					status_.last_error = "SteamVR requested application shutdown";
					return false;
				}
			}
			return true;
		}

		bool render_eye(const std::size_t index)
		{
			diagnostics::record_trace(diagnostics::trace_event::runtime_render_eye,
				index, static_cast<std::uint64_t>(status_.effective_scene_mode));
			if (index >= prepared_native_pair_.size() || prepared_native_pair_id_ == 0 ||
				prepared_native_pair_[index].color == nullptr)
			{
				status_.last_compositor_error =
					"direct H2-device SteamVR submission eye is not ready";
				status_.state = runtime_state::recoverable_error;
				diagnostics::record_trace(diagnostics::trace_event::runtime_render_eye_result,
					index, 0);
				return false;
			}
			++status_.eyes[index].acquired;
			diagnostics::record_trace(diagnostics::trace_event::runtime_render_eye_result,
				index, 1);
			return true;
		}

		EVRCompositorError submit_eye(const std::size_t index)
		{
			if (!gpu_frame_transport_is_armed(status_))
			{
				return VRCompositorError_RequestFailed;
			}
			diagnostics::record_trace(diagnostics::trace_event::runtime_submit, index,
				status_.submitted_frames);
			++status_.direct_submit_attempts;
			// Standard OpenVR D3D11 path: submit the ordinary same-device ring texture
			// filled by the proven, immediate-context-ordered target-4 copy. No shared
			// handle, cross-device import, CPU readback, compatibility path, or Flush exists.
			Texture_t texture{prepared_native_pair_[index].color.Get(),
				TextureType_DirectX, ColorSpace_Linear};
			const VRTextureBounds_t bounds{0.0f, 0.0f, 1.0f, 1.0f};
			diagnostics::gpu_interop_entered(
				reinterpret_cast<std::uintptr_t>(prepared_native_pair_[index].color.Get()), index);
			const auto submit_exit = gsl::finally([]()
			{
				diagnostics::gpu_interop_exited();
			});
			const auto submit_started = pacing_clock::now();
			const auto result = compositor_api_->Submit(index == 0 ? Eye_Left : Eye_Right,
				&texture, &bounds, Submit_Default);
			const auto submit_us = elapsed_microseconds(submit_started);
			record_duration(submit_us, status_.submit_call_count,
				status_.submit_last_us, status_.submit_max_us, status_.submit_total_us);
			if (result == VRCompositorError_None)
			{
				++status_.eyes[index].released;
				diagnostics::record_trace(diagnostics::trace_event::runtime_submit_result,
					index, static_cast<std::uint64_t>(result));
				return result;
			}
			++status_.direct_submit_failures;
			diagnostics::record_trace(diagnostics::trace_event::runtime_submit_result,
				index, static_cast<std::uint64_t>(result));
			return result;
		}

		void retain_prepared_pair_until_wait_get_poses(
			const submitted_pair_retirement retirement, std::string error)
		{
			submitted_native_pair_id_ = prepared_native_pair_id_;
			prepared_native_pair_ = {};
			prepared_native_pair_id_ = 0;
			prepared_frame_ = {};
			pending_pair_retirement_ = retirement;
			pending_retirement_error_ = std::move(error);
			frame_phase_ = frame_phase::submitted_waiting_present;
			engine_stereo_bridge::invalidate_views();
			status_.state = runtime_state::recoverable_error;
			status_.last_error = pending_retirement_error_;
			status_.last_compositor_error = status_.last_error;
		}

		bool submit_prepared_pair()
		{
			if (!gpu_frame_transport_is_armed(status_) || prepared_native_pair_id_ == 0 ||
				submitted_native_pair_id_ != 0)
			{
				(void)fail_native_scene_locked(
					"direct OpenVR submission ownership state is inconsistent",
					prepared_native_pair_id_);
				return false;
			}
			const auto probing = status_.submit_format_probe.active;
			if (probing) ++status_.submit_format_probe.candidate_attempts;
			std::array<EVRCompositorError, 2> results{
				VRCompositorError_RequestFailed, VRCompositorError_RequestFailed};
			for (std::size_t index{}; index < prepared_native_pair_.size(); ++index)
			{
				if (!render_eye(index))
				{
					(void)fail_native_scene_locked(
						"OpenVR direct stereo pair was incomplete before Submit",
						prepared_native_pair_id_);
					return false;
				}
			}
			for (std::size_t index{}; index < prepared_native_pair_.size(); ++index)
			{
				results[index] = submit_eye(index);
			}
			begin_submitted_pair_timing();

			const auto accepted_eye_mask =
				(results[0] == VRCompositorError_None ? 0x1u : 0u) |
				(results[1] == VRCompositorError_None ? 0x2u : 0u);
			if (probing)
			{
				auto& candidate = status_.submit_format_probe.candidates[format_probe_index_];
				candidate.retired = false;
				candidate.left_submit_result = results[0];
				candidate.right_submit_result = results[1];
				candidate.accepted_eye_mask = accepted_eye_mask;
				probe_retirement_candidate_ = format_probe_index_;
			}

			if (accepted_eye_mask != 0x3)
			{
				const auto error = std::format(
					"direct stereo Submit results were L={} ({}) R={} ({})",
					static_cast<std::int32_t>(results[0]), compositor_error_name(results[0]),
					static_cast<std::int32_t>(results[1]), compositor_error_name(results[1]));
				const auto failed_result = results[0] != VRCompositorError_None
					? results[0] : results[1];
				status_.last_xr_result = failed_result;
				status_.last_xr_result_name = compositor_error_name(failed_result);
				compositor_api_->ClearLastSubmittedFrame();
				if (probing) ++status_.submit_format_probe.clear_last_frame_calls;
				std::string suspension_error;
				if (!require_suspended_native_acquisition(suspension_error))
				{
					if (probing)
					{
						status_.submit_format_probe.state =
							"retiring_ownership_suspension_failure";
					}
					retain_prepared_pair_until_wait_get_poses(
						submitted_pair_retirement::fatal_after_wait_get_poses,
						std::format("{}; {}", error, suspension_error));
					return false;
				}
				// A partial Submit may have reached the compositor before focus moved.
				// Keep the whole pair until a successful WaitGetPoses, just as for a
				// complete submission. A hard error in either eye still wins over focus.
				const auto focus_only = std::ranges::all_of(results, [](const auto result)
				{
					return result == VRCompositorError_None ||
						result == VRCompositorError_DoNotHaveFocus;
				});
					if (focus_only)
					{
						controller_input::invalidate(controller_input::input_reason::compositor_focus_lost,
							controller_input::input_backend::openvr,VRCompositorError_DoNotHaveFocus);
					head_pose_bridge::invalidate_pose();
					if (probing) status_.submit_format_probe.state = "retiring_focus_loss";
					retain_prepared_pair_until_wait_get_poses(
						submitted_pair_retirement::resume_after_focus, error);
					return false;
				}
				const auto both_format_rejected = probing &&
					results[0] == VRCompositorError_TextureUsesUnsupportedFormat &&
					results[1] == VRCompositorError_TextureUsesUnsupportedFormat;
				if (probing) ++status_.submit_format_probe.candidate_rejections;
				if (both_format_rejected && format_probe_index_ + 1 <
					submit_format_candidates.size())
				{
					status_.submit_format_probe.state = "retiring_rejected_candidate";
					retain_prepared_pair_until_wait_get_poses(
						submitted_pair_retirement::advance_format_probe, error);
					return false;
				}
				if (both_format_rejected)
				{
					status_.submit_format_probe.state = "retiring_exhausted_probe";
					retain_prepared_pair_until_wait_get_poses(
						submitted_pair_retirement::finish_unqualified_format_probe, error);
					return false;
				}
				status_.submit_format_probe.state = "retiring_nonformat_failure";
				retain_prepared_pair_until_wait_get_poses(
					submitted_pair_retirement::fatal_after_wait_get_poses, error);
				return false;
			}

			if (probing && format_probe_index_ != 0)
			{
				// UNORM acceptance is useful diagnostic evidence, but quantizing the H2
				// HDR scene is not an eligible production fallback. Retire this one-shot
				// frame at the documented boundary and return to tracking-only operation.
				compositor_api_->ClearLastSubmittedFrame();
				++status_.submit_format_probe.clear_last_frame_calls;
				++status_.submitted_frames;
				++status_.compositor_render_count;
				std::string suspension_error;
				if (!require_suspended_native_acquisition(suspension_error))
				{
					status_.submit_format_probe.state =
						"retiring_ownership_suspension_failure";
					retain_prepared_pair_until_wait_get_poses(
						submitted_pair_retirement::fatal_after_wait_get_poses,
						suspension_error);
					return false;
				}
				status_.submit_format_probe.state = "retiring_lossy_acceptance";
				retain_prepared_pair_until_wait_get_poses(
					submitted_pair_retirement::finish_unqualified_format_probe,
					std::format("diagnostic format {} was accepted but is not a lossless H2 HDR transport",
						static_cast<std::uint32_t>(submit_format_candidates[format_probe_index_])));
				return false;
			}

			if (probing)
			{
				status_.submit_format_probe.state = "retiring_lossless_acceptance";
				pending_pair_retirement_ = submitted_pair_retirement::confirm_lossless_format;
			}

			submitted_native_pair_id_ = prepared_native_pair_id_;
			prepared_native_pair_ = {};
			prepared_native_pair_id_ = 0;
			prepared_frame_ = {};
			frame_phase_ = frame_phase::submitted_waiting_present;
			engine_stereo_bridge::invalidate_views();
			++status_.submitted_frames;
			++status_.compositor_render_count;
			status_.state = runtime_state::running;
			status_.last_error.clear();
			status_.last_xr_result = 0;
			status_.last_xr_result_name = "VRCompositorError_None";
			if(!native_menu::current_presentation().frontend&&!movie_mode_)set_theater(false);
			return true;
		}

		void advance_prepared_frame()
		{
			if (!prepared_frame_.valid || prepared_frame_.device_generation != status_.device_generation)
			{
				(void)fail_native_scene_locked("no renderer-prepared SteamVR pose frame",
					prepared_frame_.frame_id);
				return;
			}
			const auto scene_result = prepare_scene();
			if (scene_result == scene_prepare_result::pending)
			{
				++prepared_pair_pending_presents_;
				return;
			}
			if (scene_result == scene_prepare_result::ready)
			{
				record_prepared_pair_ready();
				(void)submit_prepared_pair();
				return;
			}
			if (scene_result == scene_prepare_result::deferred)
			{
				return;
			}
			clear_prepared_pair_timing();
		}

		bool on_present_locked(const d3d11::device_snapshot& graphics, IDXGISwapChain* const swap_chain)
		{
			if (!status_.desired_enabled)
			{
				teardown_locked(true);
				return false;
			}
			if (status_.reinitialize_pending || (status_.applied_enabled &&
				status_.device_generation != graphics.generation))
			{
				teardown_locked(false);
				status_.reinitialize_pending = false;
			}
			if (status_.state == runtime_state::fatal_for_vr && status_.applied_enabled)
			{
				teardown_preserving_error();
				frame_phase_ = frame_phase::fatal_holding_pair;
				return false;
			}
			if (!status_.applied_enabled)
			{
				// Missing graphics must not consume the attempt. Retain the attempted
				// generation across teardown: Present failure may clear the active
				// generation before the device-destroy notification reaches us.
				if (!graphics)
				{
					if (status_.state != runtime_state::fatal_for_vr)
						status_.state = runtime_state::waiting_for_graphics;
					return false;
				}
				if (!auto_initialize_allowed_ &&
					initialization_device_generation_ == graphics.generation) return false;
				auto_initialize_allowed_ = false;
				initialization_device_generation_ = graphics.generation;
				if (!initialize_locked(graphics)) return false;
			}
			if (status_.state == runtime_state::fatal_for_vr) return false;
			if (!poll_events()) return false;
			if (!try_arm_gpu_transport(graphics)) return false;
			(void)swap_chain;
			return true;
		}

		mutable std::mutex mutex_;
		// All OpenVR queue-facing work runs under this lock on the real DXGI
		// Present owner. The renderer thread only publishes completed eye targets.
		mutable std::mutex status_mutex_;
		runtime_status status_{runtime_state::disabled, true};
		runtime_status status_snapshot_{runtime_state::disabled, true};
		IVRSystem* system_{};
		steamvr_input::actions input_actions_;
		menu_overlay::presenter menu_overlay_;
		head_pose_bridge::tracking_pose ui_head_{};
		bool ui_head_valid_{};
		bool theater_active_{};
		bool movie_mode_{};
		float pause_dim_{},scene_fade_{};
		IVRCompositor* compositor_api_{};
		prepared_frame prepared_frame_{};
		std::array<native_render_session::eye_target, 2> prepared_native_pair_{};
		std::uint64_t prepared_native_pair_id_{};
		std::uint64_t submitted_native_pair_id_{};
		pacing_clock::time_point prepared_pair_started_at_{};
		std::uint64_t prepared_pair_pending_presents_{};
		pacing_clock::time_point submitted_pair_started_at_{};
		d3d11::device_snapshot direct_graphics_{};
		D3D11_TEXTURE2D_DESC format_probe_source_{};
		std::size_t format_probe_index_{};
		submitted_pair_retirement pending_pair_retirement_{submitted_pair_retirement::none};
		std::optional<std::size_t> probe_retirement_candidate_{};
		std::string pending_retirement_error_{};
		frame_phase frame_phase_{frame_phase::need_pose};
		present_transaction_key present_transaction_{};
		bool present_transaction_traced_{};
		std::uint64_t present_owner_generation_{};
		std::uint32_t present_owner_thread_id_{};
		bool final_shutdown_{};
		bool shutdown_requested_{};
		bool auto_initialize_allowed_{true};
		std::uint64_t initialization_device_generation_{};
		std::atomic_uint64_t resize_pending_generation_{};
	};

	runtime_backend::runtime_backend() : implementation_(std::make_unique<implementation>()) {}
	runtime_backend::~runtime_backend() = default;
	void runtime_backend::set_desired_enabled(const bool value) { implementation_->set_desired_enabled(value); }
	void runtime_backend::set_scene_mode(const scene_mode value) { implementation_->set_scene_mode(value); }
	void runtime_backend::request_reinitialize() { implementation_->request_reinitialize(); }
	void runtime_backend::prepare_frame(const d3d11::device_snapshot& graphics, const std::uint64_t frame_index)
	{
		implementation_->prepare_frame(graphics, frame_index);
	}
	bool runtime_backend::initialize(const d3d11::device_snapshot& value) { return implementation_->initialize(value); }
	void runtime_backend::on_present(const d3d11::device_snapshot& value, const std::uint64_t frame) { implementation_->on_present(value, frame); }
	void runtime_backend::on_present(const d3d11::present_event& value) { implementation_->on_present(value); }
	void runtime_backend::on_present_post(const d3d11::present_event& value, const HRESULT result)
	{
		implementation_->on_present_post(value, result);
	}
	void runtime_backend::capture_present(const d3d11::present_event& value) { implementation_->capture_present(value); }
	bool runtime_backend::capture_engine_texture(const d3d11::device_snapshot& graphics,
		ID3D11Texture2D* const source, const capture_frame_tag tag)
	{
		return implementation_->capture_engine_texture(graphics, source, tag);
	}
	void runtime_backend::poll_capture(const d3d11::device_snapshot& value) { implementation_->poll_capture(value); }
	void runtime_backend::on_resize_before(const d3d11::resize_event& value) noexcept { implementation_->on_resize_before(value); }
	void runtime_backend::on_device_destroying(const d3d11::device_snapshot& value) noexcept { implementation_->on_device_destroying(value); }
	void runtime_backend::shutdown() noexcept { implementation_->shutdown(); }
	bool runtime_backend::shutdown_complete() const noexcept { return implementation_->shutdown_complete(); }
	bool runtime_backend::requested_enabled() const { return implementation_->requested_enabled(); }
	bool runtime_backend::applied_enabled() const { return implementation_->applied_enabled(); }
	runtime_status runtime_backend::get_status() const { return implementation_->get_status(); }
}
