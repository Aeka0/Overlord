#include <std_include.hpp>

#include "component/vr/openxr_runtime.hpp"
#include "component/vr/openxr_layer_policy.hpp"
#include "component/vr/openvr_runtime.hpp"
#include "component/vr/runtime_backend.hpp"
#include "component/vr/frame_capture.hpp"
#include "component/vr/engine_stereo_dynamic_upload.hpp"
#include "component/vr/native_render_session.hpp"
#include "component/vr/scene_compositor.hpp"
#include "test_support.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <format>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace vr::tests
{
	void reset_gpu_queue_gate_probe();
	void wait_for_gpu_queue_gate_attempts(std::uint64_t expected);
	std::uint64_t gpu_queue_gate_acquisitions();
}

namespace
{
	void expect_dynamic_upload_advances_once()
	{
		using namespace vr::engine_stereo_dynamic_upload;
		using vr::tests::require;
		constexpr std::uint32_t owner = 7;
		for (const auto data : std::array<std::uintptr_t, 4>{0x2000, 0x3000, 0x2000, 0x3000})
		{
			cycle upload{data, owner};
			require(upload.boundary(0, data, owner) == action::defer &&
				upload.finish_eye(0), "left must retain the current allocation");
			require(upload.boundary(1, data, owner) == action::advance,
				"right must open exactly one next allocation");
			upload.returned();
			require(upload.finish_eye(1) && !upload.take_deferred(owner),
				"successful exit must not remap");
			require(upload.boundary(1, data, owner) == action::reject &&
				upload.error == failure::order && !upload.take_deferred(owner),
				"duplicate boundary must not issue Map twice");
		}
		cycle aborted{0x2000, owner};
		require(aborted.boundary(0, aborted.data, owner) == action::defer &&
			aborted.take_deferred(owner), "early exit lost the original H2 Map");
		aborted.returned();
		require(aborted.recovered && !aborted.take_deferred(owner) &&
			!aborted.finish_eye(1), "recovered partial family was accepted as stereo");
		cycle interrupted{0x2000, owner};
		(void)interrupted.boundary(0, interrupted.data, owner);
		(void)interrupted.boundary(1, interrupted.data, owner);
		require(!interrupted.take_deferred(owner) &&
			interrupted.error == failure::original_did_not_return,
			"a partially executed Map-all must never be retried");
		cycle foreign{0x2000, owner};
		require(foreign.boundary(0, 0x3000, owner) == action::reject &&
			foreign.error == failure::identity && !foreign.take_deferred(owner),
			"foreign arena acquired an upload obligation");
		cycle wrong_thread{0x2000, owner};
		(void)wrong_thread.boundary(0, wrong_thread.data, owner);
		require(wrong_thread.boundary(1, wrong_thread.data, owner + 1) == action::reject &&
			!wrong_thread.take_deferred(owner + 1) && wrong_thread.take_deferred(owner),
			"deferred upload moved to a foreign thread or lost its cleanup");
		wrong_thread.returned();
		require(!wrong_thread.finish_eye(1), "thread violation was cleared by cleanup");
		cycle missing{0x2000, owner};
		require(missing.boundary(1, missing.data, owner) == action::reject &&
			!missing.take_deferred(owner), "right-before-left issued an extra Map");
		cycle empty{};
		require(!empty.finish_eye(0) && !empty.take_deferred(owner),
			"an unentered owner must not synthesize a Map call");
	}

	void expect_gpu_queue_gate_serializes_threads()
	{
		vr::tests::reset_gpu_queue_gate_probe();
		std::mutex phase_mutex;
		std::condition_variable phase_cv;
		bool first_has_gate{};
		bool release_first{};
		std::atomic_uint32_t active_owners{};
		std::atomic_bool overlap_detected{};

		auto enter_gate = [&]
		{
			auto gate = d3d11::acquire_gpu_queue_interop();
			if (active_owners.fetch_add(1, std::memory_order_acq_rel) != 0)
			{
				overlap_detected.store(true, std::memory_order_release);
			}
			return gate;
		};

		std::thread present_thread([&]
		{
			auto present_gate = enter_gate();
			{
				const std::lock_guard phase_lock(phase_mutex);
				first_has_gate = true;
			}
			phase_cv.notify_all();
			{
				std::unique_lock phase_lock(phase_mutex);
				phase_cv.wait(phase_lock, [&] { return release_first; });
			}
			active_owners.fetch_sub(1, std::memory_order_acq_rel);
		});

		{
			std::unique_lock phase_lock(phase_mutex);
			phase_cv.wait(phase_lock, [&] { return first_has_gate; });
		}
		std::thread renderer_thread([&]
		{
			auto runtime_gate = enter_gate();
			active_owners.fetch_sub(1, std::memory_order_acq_rel);
		});

		vr::tests::wait_for_gpu_queue_gate_attempts(2);
		vr::tests::require(vr::tests::gpu_queue_gate_acquisitions() == 1,
			"renderer acquired the D3D11 GPU gate while Present still owned it");
		{
			const std::lock_guard phase_lock(phase_mutex);
			release_first = true;
		}
		phase_cv.notify_all();
		present_thread.join();
		renderer_thread.join();

		vr::tests::require(vr::tests::gpu_queue_gate_acquisitions() == 2 &&
			active_owners.load(std::memory_order_acquire) == 0 &&
			!overlap_detected.load(std::memory_order_acquire),
			"D3D11 GPU gate did not serialize Present and renderer runtime ownership");
	}

	void expect_openvr_present_transaction_contract()
	{
		constexpr std::uint64_t generation = 17;
		constexpr std::uint64_t frame = 41;
		constexpr std::uint32_t owner_thread = 73;
		d3d11::present_event event{};
		event.graphics.generation = generation;
		event.frame_index = frame;
		using vr::openvr::present_post_validation;
		vr::openvr::present_transaction_key transaction{true, frame, generation, owner_thread};

		vr::tests::require(vr::openvr::validate_present_post(transaction, event,
			owner_thread, S_OK) == present_post_validation::matched,
			"matching DXGI Present pre/post transaction was rejected");
		transaction.active = false;
		vr::tests::require(vr::openvr::validate_present_post(transaction, event,
			owner_thread, S_OK) == present_post_validation::missing_pre,
			"Present-post without a pre transaction was accepted");
		transaction.active = true;
		++event.frame_index;
		vr::tests::require(vr::openvr::validate_present_post(transaction, event,
			owner_thread, S_OK) == present_post_validation::frame_mismatch,
			"a different Present frame completed the OpenVR transaction");
		event.frame_index = frame;
		++event.graphics.generation;
		vr::tests::require(vr::openvr::validate_present_post(transaction, event,
			owner_thread, S_OK) == present_post_validation::generation_mismatch,
			"a different D3D11 generation completed the OpenVR transaction");
		event.graphics.generation = generation;
		vr::tests::require(vr::openvr::validate_present_post(transaction, event,
			owner_thread + 1, S_OK) == present_post_validation::thread_mismatch,
			"a different thread completed the OpenVR transaction");
		vr::tests::require(vr::openvr::validate_present_post(transaction, event,
			owner_thread, DXGI_ERROR_DEVICE_REMOVED) == present_post_validation::present_failed,
			"a failed DXGI Present completed the OpenVR transaction");
		vr::tests::require(vr::openvr::present_owner_change_is_violation(true, false,
			generation, owner_thread, generation, owner_thread + 1),
			"an unrequested same-generation Present owner change was accepted");
		vr::tests::require(!vr::openvr::present_owner_change_is_violation(true, true,
			generation, owner_thread, generation, owner_thread + 1),
			"reinitialize did not permit the next Present to rebind its owner");
		vr::tests::require(!vr::openvr::present_owner_change_is_violation(true, false,
			generation, owner_thread, generation + 1, owner_thread + 1),
			"a new D3D11 generation was bound to the stale Present owner");
	}

	void expect_openvr_gpu_transport_requires_exact_arming()
	{
		vr::runtime_status status{};
		status.applied_enabled = true;
		status.graphics_transport = "h2_device_direct";
		vr::tests::require(!vr::openvr::gpu_frame_transport_is_armed(status),
			"OpenVR GPU transport armed without an H2-device submission target");
		status.submission_on_game_device = true;
		vr::tests::require(!vr::openvr::gpu_frame_transport_is_armed(status),
			"OpenVR GPU transport armed without an exact native stereo reservation");
		status.native_renderer_ready = true;
		vr::tests::require(vr::openvr::gpu_frame_transport_is_armed(status),
			"a complete exact H2-device stereo transport was not recognized as armed");
		status.graphics_transport = "gpu_transport_unarmed";
		vr::tests::require(!vr::openvr::gpu_frame_transport_is_armed(status),
			"the explicit unarmed transport state admitted compositor GPU work");
	}

	void expect_openvr_raw_projection_preserves_vertical_edges()
	{
		const auto left = vr::openvr::projection_tangents_from_raw(
			-1.376f, 0.839f, -1.428f, 0.966f);
		vr::tests::require(left.left == -1.376f && left.right == 0.839f &&
			left.down == -1.428f && left.up == 0.966f,
			"OpenVR's historically inverted top/bottom names mirrored the vertical frustum");
	}

	void expect_openvr_shutdown_waits_for_matching_post()
	{
		{
			vr::openvr::runtime_backend idle_runtime;
			idle_runtime.shutdown();
			vr::tests::require(idle_runtime.shutdown_complete(),
				"idle OpenVR shutdown did not complete immediately");
		}

		constexpr std::uint64_t generation = 29;
		constexpr std::uint64_t frame = 61;
		d3d11::present_event event{};
		event.graphics.generation = generation;
		event.frame_index = frame;

		vr::openvr::runtime_backend runtime;
		runtime.on_present(event);
		auto status = runtime.get_status();
		vr::tests::require(status.present_owner_transaction_active,
			"shutdown smoke did not establish a Present-pre transaction");
		const auto pre_count = status.present_owner_pre_count;

		runtime.shutdown();
		status = runtime.get_status();
		vr::tests::require(!runtime.shutdown_complete() &&
			status.present_owner_transaction_active,
			"terminal shutdown destroyed an active Present transaction");

		runtime.on_present(event);
		status = runtime.get_status();
		vr::tests::require(status.present_owner_pre_count == pre_count &&
			status.present_owner_transaction_active,
			"terminal shutdown admitted a new Present-pre transaction");

		runtime.on_present_post(event, S_OK);
		status = runtime.get_status();
		vr::tests::require(runtime.shutdown_complete() &&
			!status.present_owner_transaction_active && !status.desired_enabled &&
			!status.applied_enabled && status.state == vr::runtime_state::disabled,
			"matching Present-post did not complete deferred terminal shutdown");
	}

	void expect_openvr_present_status_observability()
	{
		constexpr std::uint64_t generation = 23;
		constexpr std::uint64_t frame = 57;
		d3d11::present_event event{};
		event.graphics.generation = generation;
		event.frame_index = frame;

		vr::openvr::runtime_backend runtime;
		runtime.on_present(event);
		auto status = runtime.get_status();
		vr::tests::require(status.present_owner_transaction_active &&
			status.present_owner_transaction_frame == frame &&
			status.present_owner_transaction_generation == generation &&
			status.present_owner_transaction_thread_id == GetCurrentThreadId() &&
			status.present_owner_pre_count == 1 && status.present_owner_post_count == 0,
			"OpenVR Present-pre transaction identity was not observable");

		runtime.on_present_post(event, S_OK);
		status = runtime.get_status();
		vr::tests::require(!status.present_owner_transaction_active &&
			status.present_owner_transaction_thread_id == 0 &&
			status.present_owner_pre_count == 1 && status.present_owner_post_count == 1 &&
			status.present_owner_last_completed_frame == frame &&
			status.present_owner_last_post_hresult == S_OK &&
			status.direct_present_owner_contract_valid &&
			status.direct_present_owner_contract_violations == 0,
			"matching OpenVR Present completion was not persisted in status");

		++event.frame_index;
		runtime.on_present(event);
		runtime.on_present_post(event, DXGI_ERROR_DEVICE_REMOVED);
		status = runtime.get_status();
		vr::tests::require(!status.present_owner_transaction_active &&
			status.present_owner_transaction_thread_id == 0 &&
			status.present_owner_pre_count == 2 && status.present_owner_post_count == 2 &&
			status.present_owner_last_completed_frame == frame &&
			status.present_owner_last_post_hresult == DXGI_ERROR_DEVICE_REMOVED &&
			!status.direct_present_owner_contract_valid &&
			status.direct_present_owner_contract_violations == 1,
			"failed OpenVR Present completion overwrote or hid transaction evidence");
	}

	void expect_backend_override()
	{
		constexpr char environment[] = "H2V_VR_BACKEND";
		const auto existing_size = GetEnvironmentVariableA(environment, nullptr, 0);
		std::string existing(existing_size, '\0');
		if (existing_size != 0)
		{
			const auto written = GetEnvironmentVariableA(environment, existing.data(), existing_size);
			existing.resize(written);
		}

		SetEnvironmentVariableA(environment, "openvr");
		{
			vr::runtime_backend runtime;
			const auto status = runtime.get_status();
			vr::tests::require(status.backend_name == "openvr",
				"the explicit OpenVR backend override was not selected");
			vr::tests::require(runtime.requires_present_owner_execution(),
				"OpenVR was not bound to the real DXGI Present owner execution domain");
		}
		SetEnvironmentVariableA(environment, "openxr");
		{
			vr::runtime_backend runtime;
			const auto status = runtime.get_status();
			vr::tests::require(status.backend_name == "openvr" &&
				status.state == vr::runtime_state::runtime_unavailable &&
				status.last_error.find("rejects unsupported H2V_VR_BACKEND=openxr") !=
				std::string::npos,
				"the strict native runtime did not reject the OpenXR override");
			vr::tests::require(runtime.requires_present_owner_execution(),
				"the strict SteamVR/OpenVR runtime left the Present owner domain");
		}
		SetEnvironmentVariableA(environment, existing_size == 0 ? nullptr : existing.c_str());
	}

	void expect_process_runtime_preference()
	{
		constexpr wchar_t runtime_environment[] = L"XR_RUNTIME_JSON";
		const auto existing_size = GetEnvironmentVariableW(runtime_environment, nullptr, 0);
		std::wstring existing(existing_size, L'\0');
		if (existing_size != 0)
		{
			const auto written = GetEnvironmentVariableW(runtime_environment, existing.data(), existing_size);
			existing.resize(written);
		}
		SetEnvironmentVariableW(runtime_environment, nullptr);

		const auto base = std::filesystem::temp_directory_path() /
			std::format("h2v-openxr-runtime-policy-{}-{}", GetCurrentProcessId(), GetTickCount64());
		const auto manifest = base.string() + ".json";
		const auto library = base.string() + ".dll";
		{
			std::ofstream stream(library, std::ios::binary);
			stream << "runtime-probe";
		}
		{
			std::ofstream stream(manifest, std::ios::binary);
			stream << std::format(R"({{"file_format_version":"1.0.0","runtime":{{"library_path":"{}"}}}})",
				std::filesystem::path(library).filename().string());
		}

		const auto result = vr::openxr::apply_runtime_preference(manifest);
		const auto selected_size = GetEnvironmentVariableW(runtime_environment, nullptr, 0);
		std::wstring selected(selected_size, L'\0');
		if (selected_size != 0)
		{
			const auto written = GetEnvironmentVariableW(runtime_environment, selected.data(), selected_size);
			selected.resize(written);
		}
		SetEnvironmentVariableW(runtime_environment, existing_size == 0 ? nullptr : existing.c_str());
		std::error_code remove_error;
		std::filesystem::remove(manifest, remove_error);
		std::filesystem::remove(library, remove_error);

		vr::tests::require(result.applied && result.override_active && result.override_set_by_policy &&
			result.source == "automatic_virtual_desktop_vdxr" && result.blocking_error.empty(),
			"process-local OpenXR runtime preference was not applied");
		vr::tests::require(selected_size != 0 &&
			std::filesystem::path(selected) == std::filesystem::absolute(manifest),
			"process-local OpenXR runtime preference selected the wrong manifest");
	}

	void expect_incompatible_layer_isolated()
	{
		constexpr wchar_t disable_environment[] = L"H2V_TEST_DISABLE_VIRTUAL_DESKTOP_LAYER";
		constexpr wchar_t unrelated_environment[] = L"H2V_TEST_DISABLE_UNRELATED_LAYER";
		SetEnvironmentVariableW(disable_environment, nullptr);
		SetEnvironmentVariableW(unrelated_environment, nullptr);
		const auto path = std::filesystem::temp_directory_path() /
			std::format("h2v-openxr-layer-policy-{}-{}.json", GetCurrentProcessId(), GetTickCount64());
		const auto unrelated_path = std::filesystem::temp_directory_path() /
			std::format("h2v-openxr-layer-policy-unrelated-{}-{}.json", GetCurrentProcessId(), GetTickCount64());
		{
			std::ofstream stream(path, std::ios::binary);
			stream << R"({"file_format_version":"1.0.0","api_layer":{"name":"XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility","library_path":"unused.dll","api_version":"1.0","implementation_version":"1","description":"Compatibility layer for OculusXR Plugin","disable_environment":"H2V_TEST_DISABLE_VIRTUAL_DESKTOP_LAYER"}})";
		}
		{
			std::ofstream stream(unrelated_path, std::ios::binary);
			stream << R"({"file_format_version":"1.0.0","api_layer":{"name":"XR_APILAYER_TEST_unrelated","library_path":"unused.dll","api_version":"1.0","implementation_version":"1","description":"Unrelated tool layer","disable_environment":"H2V_TEST_DISABLE_UNRELATED_LAYER"}})";
		}

		const std::array manifests{path, unrelated_path};
		const auto result = vr::openxr::apply_native_implicit_layer_policy(manifests);
		std::error_code remove_error;
		std::filesystem::remove(path, remove_error);
		std::filesystem::remove(unrelated_path, remove_error);
		wchar_t value[8]{};
		const auto length = GetEnvironmentVariableW(disable_environment, value, static_cast<DWORD>(std::size(value)));
		wchar_t unrelated_value[8]{};
		const auto unrelated_length = GetEnvironmentVariableW(unrelated_environment, unrelated_value,
			static_cast<DWORD>(std::size(unrelated_value)));
		SetEnvironmentVariableW(disable_environment, nullptr);
		SetEnvironmentVariableW(unrelated_environment, nullptr);

		vr::tests::require(result.applied && result.manifest_count == 2 &&
			result.manifest_paths.size() == 2 && result.disabled_layers.size() == 1 &&
			result.disabled_environment_variables.size() == 1 && result.blocking_error.empty(),
			"native OpenXR layer policy did not isolate the incompatible compatibility shim");
		vr::tests::require(length == 1 && value[0] == L'1',
			"native OpenXR layer policy did not set the manifest-provided disable environment");
		vr::tests::require(unrelated_length == 0,
			"native OpenXR layer policy disabled an unrelated tool layer");
	}

	void expect_loader_missing(vr::openxr::runtime_backend& runtime,
		const d3d11::device_snapshot& graphics, const std::uint64_t expected_attempts)
	{
		vr::tests::require(!runtime.initialize(graphics),
			"initialization unexpectedly succeeded without a loader");
		const auto status = runtime.get_status();
		vr::tests::require(status.sdk_headers_available,
			"OpenXR SDK headers were not compiled into the smoke test");
		vr::tests::require(status.state == vr::runtime_state::loader_missing,
			"expected loader_missing state");
		vr::tests::require(status.desired_enabled,
			"desired VR state was not retained");
		vr::tests::require(!status.applied_enabled,
			"VR was applied without a loader");
		vr::tests::require(!status.loader_loaded,
			"loader remained loaded after failed initialization");
		vr::tests::require(status.initialization_attempt_count == expected_attempts,
			"initialization attempt count did not advance exactly once");
		vr::tests::require(status.last_initialization_stage == "loader",
			"failure was not reported at loader stage");
	}

	class compositor_fixture final
	{
	public:
		explicit compositor_fixture(const d3d11::device_snapshot& graphics) : graphics_(graphics)
		{
			const auto instance = GetModuleHandleW(nullptr);
			constexpr wchar_t class_name[] = L"h2v_vr_compositor_smoke_window";
			WNDCLASSW window_class{};
			window_class.lpfnWndProc = DefWindowProcW;
			window_class.hInstance = instance;
			window_class.lpszClassName = class_name;
			RegisterClassW(&window_class);
			window_class_registered_ = true;
			window_ = CreateWindowExW(0, class_name, class_name, WS_OVERLAPPEDWINDOW,
				0, 0, 4, 4, nullptr, nullptr, instance, nullptr);
			vr::tests::require(window_ != nullptr,
				"failed to create compositor smoke window");

			Microsoft::WRL::ComPtr<IDXGIFactory> factory;
			vr::tests::require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),
				"CreateDXGIFactory1 failed");
			DXGI_SWAP_CHAIN_DESC swap_chain_description{};
			swap_chain_description.BufferDesc.Width = 4;
			swap_chain_description.BufferDesc.Height = 4;
			swap_chain_description.BufferDesc.RefreshRate = {60, 1};
			swap_chain_description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			swap_chain_description.SampleDesc = {1, 0};
			swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swap_chain_description.BufferCount = 2;
			swap_chain_description.OutputWindow = window_;
			swap_chain_description.Windowed = TRUE;
			swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
			vr::tests::require(SUCCEEDED(factory->CreateSwapChain(graphics_.device.Get(),
				&swap_chain_description, &swap_chain_)), "CreateSwapChain failed");

			Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
			vr::tests::require(SUCCEEDED(swap_chain_->GetBuffer(0,
				IID_PPV_ARGS(&backbuffer))), "GetBuffer for compositor source failed");
			vr::tests::require(SUCCEEDED(graphics_.device->CreateRenderTargetView(
				backbuffer.Get(), nullptr, &source_view_)), "Create source RTV failed");
			const float source_color[4]{0.10f, 0.20f, 0.30f, 1.0f};
			graphics_.context->OMSetRenderTargets(1, source_view_.GetAddressOf(), nullptr);
			graphics_.context->ClearRenderTargetView(source_view_.Get(), source_color);
			graphics_.context->Flush();

			for (std::size_t index = 0; index < eye_textures_.size(); ++index)
			{
				const D3D11_TEXTURE2D_DESC eye_description{
					4, 4, 1, 1, index == 0 ? DXGI_FORMAT_R8G8B8A8_UNORM :
						DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, {1, 0},
					D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
				vr::tests::require(SUCCEEDED(graphics_.device->CreateTexture2D(
					&eye_description, nullptr, &eye_textures_[index])),
					"Create eye texture failed");
				vr::tests::require(SUCCEEDED(graphics_.device->CreateRenderTargetView(
					eye_textures_[index].Get(), nullptr, &eye_views_[index])),
					"Create eye RTV failed");
			}

			const D3D11_TEXTURE2D_DESC sentinel_description{
				4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
				D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
			Microsoft::WRL::ComPtr<ID3D11Texture2D> sentinel_texture;
			vr::tests::require(SUCCEEDED(graphics_.device->CreateTexture2D(
				&sentinel_description, nullptr, &sentinel_texture)),
				"Create sentinel texture failed");
			vr::tests::require(SUCCEEDED(graphics_.device->CreateRenderTargetView(
				sentinel_texture.Get(), nullptr, &sentinel_view_)),
				"Create sentinel RTV failed");
		}

		~compositor_fixture()
		{
			if (window_ != nullptr)
			{
				DestroyWindow(window_);
			}
			if (window_class_registered_)
			{
				UnregisterClassW(L"h2v_vr_compositor_smoke_window",
					GetModuleHandleW(nullptr));
			}
		}

		compositor_fixture(const compositor_fixture&) = delete;
		compositor_fixture& operator=(const compositor_fixture&) = delete;

		void require_sentinel() const
		{
			ID3D11RenderTargetView* observed{};
			graphics_.context->OMGetRenderTargets(1, &observed, nullptr);
			vr::tests::require(observed == sentinel_view_.Get(),
				"compositor did not restore the sentinel RTV");
			if (observed != nullptr)
			{
				observed->Release();
			}
		}

		std::array<std::uint8_t, 4> read_eye(const std::size_t index) const
		{
			D3D11_TEXTURE2D_DESC description{};
			eye_textures_[index]->GetDesc(&description);
			description.Usage = D3D11_USAGE_STAGING;
			description.BindFlags = 0;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			description.MiscFlags = 0;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
			vr::tests::require(SUCCEEDED(graphics_.device->CreateTexture2D(
				&description, nullptr, &staging)), "Create eye staging texture failed");
			graphics_.context->CopyResource(staging.Get(), eye_textures_[index].Get());
			graphics_.context->Flush();
			D3D11_MAPPED_SUBRESOURCE mapped{};
			vr::tests::require(SUCCEEDED(graphics_.context->Map(staging.Get(), 0,
				D3D11_MAP_READ, 0, &mapped)), "Map eye staging texture failed");
			std::array<std::uint8_t, 4> pixel{};
			std::memcpy(pixel.data(), mapped.pData, pixel.size());
			graphics_.context->Unmap(staging.Get(), 0);
			return pixel;
		}

		const d3d11::device_snapshot& graphics() const noexcept
		{
			return graphics_;
		}

		IDXGISwapChain* swap_chain() const noexcept
		{
			return swap_chain_.Get();
		}

		ID3D11RenderTargetView* eye_view(const std::size_t index) const noexcept
		{
			return eye_views_[index].Get();
		}

		void clear_source(const std::array<float, 4>& color) const
		{
			auto* const source_view = source_view_.Get();
			graphics_.context->OMSetRenderTargets(1, &source_view, nullptr);
			graphics_.context->ClearRenderTargetView(source_view, color.data());
			graphics_.context->Flush();
		}

		Microsoft::WRL::ComPtr<ID3D11Texture2D> source_texture() const
		{
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			source_view_->GetResource(&resource);
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			vr::tests::require(SUCCEEDED(resource.As(&texture)),
				"Compositor source is not a D3D11 texture");
			return texture;
		}

		void bind_sentinel() const
		{
			auto* const sentinel = sentinel_view_.Get();
			graphics_.context->OMSetRenderTargets(1, &sentinel, nullptr);
		}

	private:
		d3d11::device_snapshot graphics_;
		HWND window_{};
		bool window_class_registered_{};
		Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> source_view_;
		std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2> eye_textures_;
		std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, 2> eye_views_;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> sentinel_view_;
	};

	void expect_backbuffer_compositor(const d3d11::device_snapshot& graphics)
	{
		compositor_fixture fixture(graphics);
		vr::scene_compositor compositor;
		std::string error;

		fixture.bind_sentinel();
		vr::tests::require(compositor.prepare(graphics, fixture.swap_chain(), error),
			std::format("backbuffer compositor prepare failed: {}", error));
		fixture.require_sentinel();
		const vr::scene_compositor_target target{fixture.eye_view(0), 4, 4};
		vr::tests::require(compositor.render_eye(graphics, target, error),
			std::format("backbuffer compositor render failed: {}", error));
		fixture.require_sentinel();
		const auto pixel = fixture.read_eye(0);
		vr::tests::require(std::abs(static_cast<int>(pixel[0]) - 26) <= 2 &&
			std::abs(static_cast<int>(pixel[1]) - 51) <= 2 &&
			std::abs(static_cast<int>(pixel[2]) - 77) <= 2,
			"backbuffer compositor did not retain source pixels");
	}

	void expect_cross_device_backbuffer_pipeline(const d3d11::device_snapshot& game_graphics)
	{
		compositor_fixture game(game_graphics);
		const auto compositor_graphics = vr::tests::create_graphics_on_same_adapter(
			game_graphics, game_graphics.generation);
		compositor_fixture headset(compositor_graphics);
		vr::frame_capture capture;
		capture.produce({game.swap_chain(), game_graphics, 1});
		game_graphics.context->Flush();

		vr::captured_frame frame;
		std::string error;
		for (std::size_t attempt = 0; attempt < 10'000 && !frame; ++attempt)
		{
			capture.poll(game_graphics);
			if (capture.acquire(compositor_graphics.device.Get(), game_graphics.generation,
				frame, error))
			{
				break;
			}
			SwitchToThread();
		}
		vr::tests::require(static_cast<bool>(frame), std::format(
			"cross-device game capture never became available: {}", error));

		vr::scene_compositor compositor;
		const auto prepared = frame.texture != nullptr
			? compositor.prepare_capture(compositor_graphics, frame.texture.Get(),
				frame.description, frame.frame_id, frame.device_generation, error)
			: compositor.prepare_cpu_capture(compositor_graphics, frame.cpu_pixels,
				frame.row_pitch, frame.description, frame.frame_id, frame.device_generation, error);
		capture.release(frame);
		vr::tests::require(prepared, std::format(
			"cross-device captured backbuffer prepare failed: {}", error));

		for (std::size_t eye = 0; eye < 2; ++eye)
		{
			const vr::scene_compositor_target target{headset.eye_view(eye), 4, 4};
			vr::tests::require(compositor.render_eye(compositor_graphics, target, error, 0),
				std::format("monoscopic game capture did not render to eye {}: {}", eye, error));
			const auto pixel = headset.read_eye(eye);
			vr::tests::require(std::abs(static_cast<int>(pixel[0]) - 26) <= 2 &&
				std::abs(static_cast<int>(pixel[1]) - 51) <= 2 &&
				std::abs(static_cast<int>(pixel[2]) - 77) <= 2,
				std::format("eye {} did not receive the captured game backbuffer", eye));
		}

		const auto status = capture.get_status();
		vr::tests::require(status.produced == 1 && status.ready == 1 &&
			status.acquired == 1 && status.released == 1,
			"cross-device capture lifecycle counters are inconsistent");
		vr::tests::require(status.shared_opened == 1 && status.shared_failed == 0,
			"same-adapter game capture did not use direct D3D11 shared-texture interop");
	}

	void expect_explicit_engine_texture_capture(const d3d11::device_snapshot& graphics)
	{
		compositor_fixture fixture(graphics);
		vr::frame_capture capture;
		const auto source = fixture.source_texture();
		vr::tests::require(capture.produce_texture(graphics, source.Get(), {true, 19, 1}),
			"explicit engine texture capture was rejected");
		graphics.context->Flush();

		vr::captured_frame frame;
		std::string error;
		for (std::size_t attempt = 0; attempt < 10'000 && !frame; ++attempt)
		{
			capture.poll(graphics);
			if (capture.acquire(graphics.device.Get(), graphics.generation, frame, error))
			{
				break;
			}
			SwitchToThread();
		}
		vr::tests::require(static_cast<bool>(frame), std::format(
			"explicit engine texture capture never became available: {}", error));
		vr::tests::require(frame.tag.stereo && frame.tag.pair_id == 19 && frame.tag.eye_index == 1,
			"explicit engine texture capture lost its immutable eye tag");
		capture.release(frame);
		const auto status = capture.get_status();
		vr::tests::require(status.produced == 1 && status.acquired == 1 && status.released == 1,
			"explicit engine texture capture lifecycle counters are inconsistent");
	}

	void expect_atomic_native_stereo_capture(const d3d11::device_snapshot& graphics)
	{
		vr::frame_capture capture;
		const D3D11_TEXTURE2D_DESC source_description{
			4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
			D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 0};
		std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2> sources{};
		for (auto& source : sources)
		{
			vr::tests::require(SUCCEEDED(graphics.device->CreateTexture2D(
				&source_description, nullptr, &source)),
				"native non-shared source creation failed");
		}
		vr::tests::require(capture.produce_texture(graphics, sources[0].Get(),
			{true, 73, 0, true}), "native left eye was not retained");
		vr::tests::require(capture.produce_texture(graphics, sources[1].Get(),
			{true, 73, 1, true}), "native right eye was not retained");

		std::array<vr::captured_frame, 2> pair{};
		std::string error;
		const auto acquire_result = capture.acquire_stereo_pair(graphics.device.Get(),
			graphics.generation, 73, pair, error);
		vr::tests::require(acquire_result == vr::stereo_pair_acquire_result::ready,
			std::format("native same-device capture was not immediately available: {}", error));
		vr::tests::require(pair[0] && pair[1], std::format(
			"native stereo capture never produced an atomic pair: {}", error));
		vr::tests::require(pair[0].tag.native && pair[1].tag.native &&
			pair[0].tag.pair_id == 73 && pair[1].tag.pair_id == 73 &&
			pair[0].tag.eye_index == 0 && pair[1].tag.eye_index == 1,
			"native stereo capture paired mismatched eye metadata");
		vr::tests::require(pair[0].texture && pair[1].texture && !pair[0].shared &&
			!pair[1].shared && pair[0].texture.Get() == sources[0].Get() &&
			pair[1].texture.Get() == sources[1].Get() &&
			pair[0].producer_texture.Get() == sources[0].Get() &&
			pair[1].producer_texture.Get() == sources[1].Get(),
			"native stereo capture did not retain the exact H2-device textures");
		capture.release(pair[0]);
		capture.release(pair[1]);
		const auto status = capture.get_status();
		vr::tests::require(status.produced == 2 && status.ready == 2 &&
			status.acquired == 2 && status.released == 2 &&
			status.native_direct_acquired == 2 && status.shared_opened == 0 &&
			status.shared_failed == 0 && status.cpu_fallback_ready == 0,
			"atomic native capture lifecycle counters are inconsistent");
	}

	void expect_native_direct_submission_ring(const d3d11::device_snapshot& graphics)
	{
		auto& session = vr::native_render_session::active();
		session.invalidate();
		const auto baseline = session.get_status();
		std::string error;
		vr::tests::require(session.ensure(graphics, 256, 256, error),
			std::format("native direct ring creation failed: {}", error));

		auto publish_pair = [&](const std::uint64_t pair_id)
		{
			std::array<vr::native_render_session::eye_target, 2> rendered{};
			for (std::uint32_t eye{}; eye < rendered.size(); ++eye)
			{
				vr::tests::require(session.acquire_target(pair_id, eye, rendered[eye]),
					std::format("pair {} eye {} could not lease its target", pair_id, eye));
				vr::tests::require(session.complete_rendered_eye(
					pair_id, eye, rendered[eye].color.Get()),
					std::format("pair {} eye {} failed atomic completion", pair_id, eye));
			}
			std::array<vr::native_render_session::eye_target, 2> published{};
			vr::tests::require(session.acquire_published_pair(pair_id, published),
				std::format("pair {} was not atomically published", pair_id));
			return published;
		};

		const auto first = publish_pair(101);
		const auto second = publish_pair(102);
		vr::tests::require(first[0].color.Get() != second[0].color.Get() &&
			first[1].color.Get() != second[1].color.Get(),
			"native direct ring aliased its two ownership slots");
		for (const auto& pair : {first, second})
		{
			for (const auto& eye : pair)
			{
				D3D11_TEXTURE2D_DESC description{};
				eye.color->GetDesc(&description);
				Microsoft::WRL::ComPtr<ID3D11Device> owner;
				eye.color->GetDevice(&owner);
				vr::tests::require(owner.Get() == graphics.device.Get() &&
					description.MiscFlags == 0,
					"native submission target is shared or belongs to another device");
			}
		}

		vr::native_render_session::eye_target exhausted;
		vr::tests::require(!session.acquire_target(103, 0, exhausted),
			"native ring reused a target before its submitted owner retired");
		vr::tests::require(session.release_pair(101), "published pair 101 did not release");
		const auto third = publish_pair(104);
		vr::tests::require(third[0].color.Get() == first[0].color.Get() &&
			third[1].color.Get() == first[1].color.Get(),
			"retiring a pair did not make that exact ring slot reusable");
		vr::tests::require(session.release_pair(102), "published pair 102 did not release");
		vr::tests::require(session.release_pair(104), "published pair 104 did not release");

		const auto status = session.get_status();
		vr::tests::require(status.pair_acquires == baseline.pair_acquires + 3 &&
			status.pair_releases == baseline.pair_releases + 3 &&
			status.pair_exhaustions == baseline.pair_exhaustions + 1,
			"native direct ring ownership counters are inconsistent");
		session.invalidate(graphics.generation);

		d3d11::device_snapshot warp_graphics;
		constexpr D3D_FEATURE_LEVEL warp_levels[]{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};
		const auto warp_result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP,
			nullptr, 0, warp_levels, static_cast<UINT>(std::size(warp_levels)),
			D3D11_SDK_VERSION, &warp_graphics.device, &warp_graphics.feature_level,
			&warp_graphics.context);
		vr::tests::require(SUCCEEDED(warp_result) && warp_graphics.device &&
			warp_graphics.context, std::format(
				"D3D11 WARP format probe creation failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(warp_result)));
		warp_graphics.generation = 91;

		D3D11_TEXTURE2D_DESC hdr_source_description{};
		hdr_source_description.Width = 256;
		hdr_source_description.Height = 256;
		hdr_source_description.MipLevels = 1;
		hdr_source_description.ArraySize = 1;
		hdr_source_description.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		hdr_source_description.SampleDesc = {1, 0};
		hdr_source_description.Usage = D3D11_USAGE_DEFAULT;
		hdr_source_description.BindFlags = D3D11_BIND_SHADER_RESOURCE |
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
		// Bootstrap sizes the ring from raw scene target 4; its actual production
		// input is now H2's post-PostFX target 5. The captured resource descriptor
		// includes UAV capability despite having no registered UAV view.
		const auto display_source_description = hdr_source_description;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> hdr_source;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> hdr_source_view;
		vr::tests::require(SUCCEEDED(warp_graphics.device->CreateTexture2D(
			&display_source_description, nullptr, &hdr_source)) &&
			SUCCEEDED(warp_graphics.device->CreateRenderTargetView(
				hdr_source.Get(), nullptr, &hdr_source_view)),
			"WARP H2 PostFX target-5 fixture creation failed");

		D3D11_TEXTURE2D_DESC sentinel_description{};
		sentinel_description.Width = 16;
		sentinel_description.Height = 16;
		sentinel_description.MipLevels = 1;
		sentinel_description.ArraySize = 1;
		sentinel_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sentinel_description.SampleDesc = {1, 0};
		sentinel_description.Usage = D3D11_USAGE_DEFAULT;
		sentinel_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> sentinel_texture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sentinel_view;
		vr::tests::require(SUCCEEDED(warp_graphics.device->CreateTexture2D(
			&sentinel_description, nullptr, &sentinel_texture)) &&
			SUCCEEDED(warp_graphics.device->CreateShaderResourceView(
				sentinel_texture.Get(), nullptr, &sentinel_view)),
			"WARP state-restoration sentinel creation failed");

		const D3D11_VIEWPORT sentinel_viewport{7.0f, 11.0f, 113.0f, 127.0f,
			0.125f, 0.875f};
		const D3D11_RECT sentinel_scissor{3, 5, 97, 101};
		auto bind_sentinel_state = [&]
		{
			ID3D11RenderTargetView* output = hdr_source_view.Get();
			ID3D11ShaderResourceView* input = sentinel_view.Get();
			warp_graphics.context->OMSetRenderTargets(1, &output, nullptr);
			warp_graphics.context->RSSetViewports(1, &sentinel_viewport);
			warp_graphics.context->RSSetScissorRects(1, &sentinel_scissor);
			warp_graphics.context->IASetPrimitiveTopology(
				D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
			warp_graphics.context->PSSetShaderResources(0, 1, &input);
			warp_graphics.context->CSSetShaderResources(0, 1, &input);
		};
		auto require_sentinel_state = [&]
		{
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pixel_input;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> compute_input;
			Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;
			D3D11_VIEWPORT viewport{};
			D3D11_RECT scissor{};
			UINT viewport_count{1};
			UINT scissor_count{1};
			D3D11_PRIMITIVE_TOPOLOGY topology{};
			warp_graphics.context->OMGetRenderTargets(1, &output, nullptr);
			warp_graphics.context->RSGetViewports(&viewport_count, &viewport);
			warp_graphics.context->RSGetScissorRects(&scissor_count, &scissor);
			warp_graphics.context->RSGetState(&rasterizer);
			warp_graphics.context->IAGetPrimitiveTopology(&topology);
			warp_graphics.context->VSGetShader(&vertex_shader, nullptr, nullptr);
			warp_graphics.context->PSGetShader(&pixel_shader, nullptr, nullptr);
			warp_graphics.context->PSGetShaderResources(0, 1, &pixel_input);
			warp_graphics.context->CSGetShaderResources(0, 1, &compute_input);
			vr::tests::require(output.Get() == hdr_source_view.Get() &&
				pixel_input.Get() == sentinel_view.Get() &&
				compute_input.Get() == sentinel_view.Get() &&
				viewport_count == 1 && std::memcmp(&viewport, &sentinel_viewport,
					sizeof(viewport)) == 0 && scissor_count == 1 &&
				std::memcmp(&scissor, &sentinel_scissor, sizeof(scissor)) == 0 &&
				topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP &&
				vertex_shader == nullptr && pixel_shader == nullptr && rasterizer == nullptr,
				"ExecuteCommandList(TRUE) did not restore the complete H2 sentinel state");
		};

		auto texture_hash = [&](ID3D11Texture2D* const texture,
			const DXGI_FORMAT format, const float* const encoded_color)
		{
			D3D11_TEXTURE2D_DESC description{};
			texture->GetDesc(&description);
			description.Usage = D3D11_USAGE_STAGING;
			description.BindFlags = 0;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			description.MiscFlags = 0;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
			vr::tests::require(SUCCEEDED(warp_graphics.device->CreateTexture2D(
				&description, nullptr, &staging)),
				"WARP converted staging texture creation failed");
			warp_graphics.context->CopyResource(staging.Get(), texture);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			vr::tests::require(SUCCEEDED(warp_graphics.context->Map(staging.Get(), 0,
				D3D11_MAP_READ, 0, &mapped)), "WARP converted texture Map failed");
			const auto unmap = gsl::finally([&]
			{
				warp_graphics.context->Unmap(staging.Get(), 0);
			});
			// Verify actual production shader pixels, not just two different hashes.
			// These source values are exact in R11G11B10 and exercise the toe,
			// midtones, black and white. Format equality must not bypass decoding.
			const auto unpack_float = [](const std::uint32_t bits, const unsigned mantissa_bits)
			{
				const auto exponent = bits >> mantissa_bits;
				const auto fraction = float(bits & ((1u << mantissa_bits) - 1)) /
					float(1u << mantissa_bits);
				return exponent ? std::ldexp(1.0f + fraction, int(exponent) - 15) :
					std::ldexp(fraction, -14);
			};
			std::array<float, 3> pixel{};
			std::uint32_t packed{};
			std::memcpy(&packed, mapped.pData, sizeof(packed));
			if (format == DXGI_FORMAT_R16G16B16A16_FLOAT)
			{
				std::array<std::uint16_t, 4> halves{};
				std::memcpy(halves.data(), mapped.pData, sizeof(halves));
				for (std::size_t channel{}; channel < 3; ++channel)
					pixel[channel] = unpack_float(halves[channel], 10);
			}
			else if (format == DXGI_FORMAT_R11G11B10_FLOAT)
			{
				pixel = {unpack_float(packed & 0x7ff, 6),
					unpack_float((packed >> 11) & 0x7ff, 6), unpack_float(packed >> 22, 5)};
			}
			else
			{
				const unsigned bits = format == DXGI_FORMAT_R10G10B10A2_UNORM ? 10 : 8;
				const auto mask = (1u << bits) - 1;
				for (unsigned channel{}; channel < 3; ++channel)
					pixel[channel] = float((packed >> (channel * bits)) & mask) / float(mask);
			}
			for (std::size_t channel{}; channel < 3; ++channel)
			{
				const auto encoded = encoded_color[channel];
				const auto expected = encoded <= 0.04045f ? encoded / 12.92f :
					std::pow((encoded + 0.055f) / 1.055f, 2.4f);
				vr::tests::require(std::abs(pixel[channel] - expected) < 0.007f,
					"native display RGB was not decoded once to linear; gray washout regression");
			}
			const std::size_t bytes_per_pixel =
				format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
			std::uint64_t hash{1469598103934665603ull};
			for (UINT row{}; row < description.Height; ++row)
			{
				const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData) +
					static_cast<std::size_t>(row) * mapped.RowPitch;
				for (std::size_t index{};
					index < static_cast<std::size_t>(description.Width) * bytes_per_pixel;
					++index)
				{
					hash = (hash ^ bytes[index]) * 1099511628211ull;
				}
			}
			return hash;
		};

		constexpr std::array destination_formats{
			DXGI_FORMAT_R11G11B10_FLOAT,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			DXGI_FORMAT_R8G8B8A8_UNORM,
		};
		const auto conversion_baseline = session.get_status();
		static unsigned composition_calls{}, composition_errors{}, composition_eye_mask{};
		composition_calls = composition_errors = composition_eye_mask = 0;
		vr::eye_composition::set_consumer([](const vr::eye_composition::event& event,
			ID3D11DeviceContext* context, ID3D11ShaderResourceView* source,
			ID3D11RenderTargetView* destination) noexcept {
			++composition_calls;
			if (!context || !source || !destination || event.eye > 1 ||
				event.views.eyes[event.eye].pair_id != event.pair_id) { ++composition_errors; return; }
			if (!event.model_origins.valid || event.model_origins.eyes[event.eye][0] != 10.f + event.eye)
				++composition_errors;
			if (event.model_origins.placement != std::array<float,3>{40,50,60}) ++composition_errors;
			Microsoft::WRL::ComPtr<ID3D11Resource> input, output;
			source->GetResource(&input); destination->GetResource(&output);
			if (input.Get() == output.Get()) ++composition_errors;
			composition_eye_mask |= 1u << event.eye;
		});
		const auto reset_composition = gsl::finally([] { vr::eye_composition::set_consumer(nullptr); });
		for (std::size_t format_index{}; format_index < destination_formats.size();
			++format_index)
		{
			const auto destination_format = destination_formats[format_index];
			vr::tests::require(session.ensure_copy_ring(warp_graphics,
				hdr_source_description, destination_format, error), std::format(
					"native WARP format ring {} creation failed: {}",
					static_cast<unsigned int>(destination_format), error));
			const auto suspended_status = session.get_status();
			vr::tests::require(!suspended_status.accepting_pairs &&
				suspended_status.deferred_conversion,
				"rebuilt native format ring admitted a producer before resume");
			vr::tests::require(!session.copy_eye(5000 + format_index, 0,
				hdr_source.Get(), 5, warp_graphics.context.Get()),
				"suspended native format ring accepted a new GPU copy");
			const auto pair_id = 6000 + format_index;
			vr::tests::require(session.admit_pair(pair_id) &&
				session.accepts_pair(pair_id) && !session.accepts_pair(pair_id + 100),
				"native format ring did not admit exactly one prepared pair");
			vr::tests::require(!session.copy_eye(pair_id + 100, 0,
				hdr_source.Get(), 5, warp_graphics.context.Get()) &&
				session.accepts_pair(pair_id),
				"wrong-pair rejection closed or consumed the exact prepared admission");
			const auto admission_failure = session.get_status().last_copy_failure;
			vr::tests::require(admission_failure.stage ==
				vr::native_render_session::copy_failure::admission &&
				admission_failure.pair_id == pair_id + 100 &&
				admission_failure.expected_pair_id == pair_id &&
				!admission_failure.source_descriptor_read &&
				admission_failure.result == E_ACCESSDENIED,
				"copy admission rejection lost its exact prediction or invented a descriptor");
			if (format_index == 0)
			{
				vr::tests::require(!session.ensure_copy_ring(warp_graphics,
					hdr_source_description, destination_formats[1], error),
					"native format ring rebuilt while acquisition was active");
			}

			bind_sentinel_state();
			vr::engine_stereo_view::slot_pair composition_views{};
			auto composition_records = std::make_unique<vr::engine_stereo_view::scene_record_pair>();
			composition_records->pair_id = pair_id; composition_records->publication = 1;
			for (unsigned eye = 0; eye < 2; ++eye)
			{
				composition_views.eyes[eye].pair_id = pair_id;
				composition_views.eyes[eye].publication = 1;
				composition_views.eyes[eye].output_eye = eye;
				const std::array<float,3> origin{10.f+eye,20,30};
				auto& record = eye ? composition_records->right : composition_records->left;
				std::memcpy(record.data()+vr::engine_stereo_view::h2_view_origin_offset, origin.data(), sizeof(origin));
				const std::array<float,3> placement{40,50,60};
				std::memcpy(record.data()+vr::engine_stereo_view::h2_current_model_placement_origin_offset, placement.data(), sizeof(placement));
				// Distinct history values must NOT reach current-eye composition.
				const std::array<float,3> history{400,500,600};
				std::memcpy(record.data()+0x19C, history.data(), sizeof(history));
				std::memcpy(record.data()+0x2DC0, history.data(), sizeof(history));
			}
			vr::eye_composition::event composition{composition_views, pair_id, warp_graphics.generation,
				0, hdr_source_description.Width, hdr_source_description.Height,
				vr::eye_composition::model_origins_for(composition_views, *composition_records)};
			composition_records.reset(); // Callback owns the small origin snapshot.
			composition_eye_mask = 0;
			const float left_color[4]{0.03125f, 0.25f, 0.5f, 1.0f};
			warp_graphics.context->ClearRenderTargetView(hdr_source_view.Get(), left_color);
			vr::tests::require(session.copy_eye(pair_id, 0, hdr_source.Get(), 5,
				warp_graphics.context.Get(), &composition), "native format ring left conversion failed");
			vr::tests::require(composition_eye_mask == 1 && !session.pair_published(pair_id),
				"left composition did not occur before pair publication");
			require_sentinel_state();
			const float right_color[4]{0.0f, 1.0f, 0.75f, 1.0f};
			warp_graphics.context->ClearRenderTargetView(hdr_source_view.Get(), right_color);
			composition.eye = 1;
			vr::tests::require(session.copy_eye(pair_id, 1, hdr_source.Get(), 5,
				warp_graphics.context.Get(), &composition), "native format ring right conversion failed");
			vr::tests::require(composition_eye_mask == 3 && composition_errors == 0,
				"composition did not receive distinct source/destination for both eyes");
			vr::tests::require(session.get_status().last_copy_failure.pair_id == pair_id + 100,
				"successful copy erased the previous rejected-copy evidence");
			require_sentinel_state();
			vr::tests::require(!session.accepts_pair(pair_id) &&
				!session.get_status().accepting_pairs &&
				session.get_status().expected_pair_id == 0,
				"completed prepared pair did not close native producer admission");
			vr::tests::require(session.suspend_acquisition(),
				"native format ring did not suspend after publishing a pair");
			vr::tests::require(!session.copy_eye(pair_id, 0, hdr_source.Get(), 5,
				warp_graphics.context.Get(), &composition) && session.pair_published(pair_id),
				"suspended copy mutated an already-published pair");
			if (format_index + 1 < destination_formats.size())
			{
				vr::tests::require(!session.ensure_copy_ring(warp_graphics,
					hdr_source_description, destination_formats[format_index + 1], error),
					"native format ring rebuilt over a live published pair");
			}

			std::array<vr::native_render_session::eye_target, 2> copied{};
			vr::tests::require(session.acquire_published_pair(pair_id, copied),
				"native format ring did not expose its completed pair");
			std::array<std::uint64_t, 2> hashes{};
			for (std::size_t eye_index{}; eye_index < copied.size(); ++eye_index)
			{
				D3D11_TEXTURE2D_DESC description{};
				copied[eye_index].color->GetDesc(&description);
				vr::tests::require(copied[eye_index].color.Get() != hdr_source.Get() &&
					description.Format == destination_format &&
					description.BindFlags == (D3D11_BIND_SHADER_RESOURCE |
						D3D11_BIND_RENDER_TARGET) && description.CPUAccessFlags == 0 &&
					description.MiscFlags == 0 && copied[eye_index].depth == nullptr &&
					copied[eye_index].depth_view == nullptr,
					"native converted destination violated its same-device contract");
				hashes[eye_index] = texture_hash(copied[eye_index].color.Get(),
					destination_format, eye_index == 0 ? left_color : right_color);
			}
			vr::tests::require(hashes[0] != hashes[1],
				"native format conversion lost the distinct left/right source contents");
			vr::tests::require(session.release_pair(pair_id),
				"native converted pair did not retire");
		}

		const auto copy_status = session.get_status();
		vr::tests::require(composition_calls == destination_formats.size()*2,
			"composition ran on a rejected/published eye or missed an accepted eye");
		vr::tests::require(copy_status.available && copy_status.copy_ring &&
			!copy_status.accepting_pairs && copy_status.deferred_conversion &&
			copy_status.source_format == DXGI_FORMAT_R11G11B10_FLOAT &&
			copy_status.format == DXGI_FORMAT_R8G8B8A8_UNORM &&
			copy_status.deferred_context_create_result == S_OK &&
			copy_status.last_command_list_result == S_OK &&
			copy_status.conversion_pipeline_result == S_OK &&
			copy_status.last_conversion_result == E_ACCESSDENIED &&
			copy_status.conversion_attempts ==
				conversion_baseline.conversion_attempts + destination_formats.size() * 2 &&
			copy_status.conversion_completions ==
				conversion_baseline.conversion_completions + destination_formats.size() * 2 &&
			copy_status.conversion_failures == conversion_baseline.conversion_failures &&
			copy_status.source_view_cached &&
			copy_status.source_view_creations ==
				conversion_baseline.source_view_creations + destination_formats.size() &&
			copy_status.source_identity_matches ==
				conversion_baseline.source_identity_matches + destination_formats.size() &&
			copy_status.source_identity_mismatches ==
				conversion_baseline.source_identity_mismatches &&
			copy_status.command_list_builds ==
				conversion_baseline.command_list_builds + destination_formats.size() * 2 &&
			copy_status.command_list_build_failures ==
				conversion_baseline.command_list_build_failures &&
			copy_status.command_list_executions ==
				conversion_baseline.command_list_executions + destination_formats.size() * 2 &&
			copy_status.last_source_texture ==
				reinterpret_cast<std::uintptr_t>(hdr_source.Get()) &&
			copy_status.last_source_view != 0 &&
			copy_status.last_source_view_create_result == S_OK &&
			copy_status.acquisition_rejections >=
				conversion_baseline.acquisition_rejections + destination_formats.size() * 3,
			"native format conversion status lost its isolation or admission evidence");

		Microsoft::WRL::ComPtr<ID3D11Texture2D> replacement_hdr_source;
		vr::tests::require(SUCCEEDED(warp_graphics.device->CreateTexture2D(
			&display_source_description, nullptr, &replacement_hdr_source)),
			"replacement H2 HDR source fixture creation failed");
		constexpr std::uint64_t replacement_pair_id = 7000;
		vr::tests::require(session.admit_pair(replacement_pair_id) &&
			session.copy_eye(replacement_pair_id, 0, hdr_source.Get(), 5,
				warp_graphics.context.Get()) &&
			!session.copy_eye(replacement_pair_id, 1, replacement_hdr_source.Get(), 5,
				warp_graphics.context.Get()) && session.pair_failed(replacement_pair_id),
			"native source identity replacement did not hard-fail its exact pair");
		const auto replacement_status = session.get_status();
		const auto& replacement_failure = replacement_status.last_copy_failure;
		vr::tests::require(replacement_failure.stage ==
			vr::native_render_session::copy_failure::source_identity &&
			replacement_failure.pair_id == replacement_pair_id && replacement_failure.eye == 1 &&
			replacement_failure.source == reinterpret_cast<std::uintptr_t>(replacement_hdr_source.Get()) &&
			replacement_failure.cached_source == reinterpret_cast<std::uintptr_t>(hdr_source.Get()) &&
			replacement_failure.source_descriptor_read &&
			replacement_failure.device_generation == warp_graphics.generation,
			"source identity rejection lost the original copy failure snapshot");
		vr::tests::require(!session.copy_eye(replacement_pair_id, 1, hdr_source.Get(), 5,
			warp_graphics.context.Get()) && session.get_status().last_copy_failure.stage ==
			vr::native_render_session::copy_failure::source_identity &&
			session.get_status().last_copy_failure.source == replacement_failure.source,
			"subsequent quarantined-ring rejection overwrote the first copy failure");
		vr::tests::require(replacement_status.source_view_cached &&
			replacement_status.source_view_creations == copy_status.source_view_creations &&
			replacement_status.source_identity_matches ==
				copy_status.source_identity_matches + 1 &&
			replacement_status.source_identity_mismatches ==
				copy_status.source_identity_mismatches + 1 &&
			replacement_status.last_source_texture == copy_status.last_source_texture &&
			replacement_status.last_source_view == copy_status.last_source_view &&
			replacement_status.last_conversion_result == DXGI_ERROR_INVALID_CALL,
			"native source identity rejection replaced or recreated the retained SRV");
		ID3D11ShaderResourceView* null_view{};
		warp_graphics.context->OMSetRenderTargets(0, nullptr, nullptr);
		warp_graphics.context->PSSetShaderResources(0, 1, &null_view);
		warp_graphics.context->CSSetShaderResources(0, 1, &null_view);
		warp_graphics.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
		session.invalidate(warp_graphics.generation);
		vr::tests::require(session.get_status().last_copy_failure.pair_id == replacement_pair_id &&
			session.get_status().last_copy_failure.stage ==
				vr::native_render_session::copy_failure::source_identity,
			"session cleanup erased the rejected copy evidence");

		// Exercise real pixels across both native target identities and all ring
		// eye caches. Switching every two pairs revisits each ring slot in both
		// modes; the eyes also deliberately choose different modes in one pair.
		vr::tests::require(session.ensure_copy_ring(warp_graphics,
			hdr_source_description, DXGI_FORMAT_R8G8B8A8_UNORM, error),
			"thermal transition ring creation failed");
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> thermal_view;
		vr::tests::require(SUCCEEDED(warp_graphics.device->CreateRenderTargetView(
			replacement_hdr_source.Get(), nullptr, &thermal_view)), "thermal source RTV failed");
		const float normal_color[4]{0.03125f, 0.25f, 0.5f, 1.0f};
		const float thermal_color[4]{0.75f, 1.0f, 0.0f, 1.0f};
		warp_graphics.context->ClearRenderTargetView(hdr_source_view.Get(), normal_color);
		warp_graphics.context->ClearRenderTargetView(thermal_view.Get(), thermal_color);
		const auto transition_baseline = session.get_status();
		for (std::uint32_t iteration = 0; iteration < 12; ++iteration)
		{
			const auto pair = 7100 + iteration;
			vr::tests::require(session.admit_pair(pair), "thermal pair admission failed");
			for (std::uint32_t eye = 0; eye < 2; ++eye)
			{
				const bool thermal = ((iteration / 2 + eye) % 2) != 0;
				bind_sentinel_state();
				vr::tests::require(session.copy_eye(pair, eye,
					thermal ? replacement_hdr_source.Get() : hdr_source.Get(), thermal ? 4 : 5,
					warp_graphics.context.Get()), "thermal target switch rejected a stable identity");
				require_sentinel_state();
			}
			std::array<vr::native_render_session::eye_target, 2> converted{};
			vr::tests::require(session.acquire_published_pair(pair, converted),
				"thermal pair was not published");
			for (std::uint32_t eye = 0; eye < 2; ++eye)
				(void)texture_hash(converted[eye].color.Get(), DXGI_FORMAT_R8G8B8A8_UNORM,
					((iteration / 2 + eye) % 2) ? thermal_color : normal_color);
			vr::tests::require(session.release_pair(pair), "thermal pair retirement failed");
		}
		const auto transition_status = session.get_status();
		vr::tests::require(transition_status.source_view_creations == transition_baseline.source_view_creations + 2 &&
			transition_status.command_list_builds == transition_baseline.command_list_builds + 8 &&
			transition_status.command_list_executions == transition_baseline.command_list_executions + 24 &&
			transition_status.conversion_failures == transition_baseline.conversion_failures,
			"thermal switching rebuilt stable GPU caches or lost an eye");
		vr::tests::require(session.admit_pair(7199) &&
			!session.copy_eye(7199, 0, hdr_source.Get(), 304, warp_graphics.context.Get()) &&
			session.get_status().last_copy_failure.stage == vr::native_render_session::copy_failure::arguments &&
			session.discard_unpublished_pair(7199), "unknown native target was accepted");
		// A valid ID cannot authorize a replacement within its own cache slot.
		vr::tests::require(session.admit_pair(7200) &&
			!session.copy_eye(7200, 0, hdr_source.Get(), 4, warp_graphics.context.Get()) &&
			session.get_status().last_copy_failure.stage == vr::native_render_session::copy_failure::source_identity,
			"thermal cache identity replacement was accepted");
		warp_graphics.context->OMSetRenderTargets(0, nullptr, nullptr);
		session.invalidate(warp_graphics.generation);

		// Transaction-level validation happens after the second eye has made a
		// pair visible to the Present owner. Rebuild an isolated ring so this
		// terminal quarantine check cannot contaminate the conversion cases above.
		vr::tests::require(session.ensure_copy_ring(warp_graphics,
			hdr_source_description, DXGI_FORMAT_R11G11B10_FLOAT, error),
			"published-pair revoke ring rebuild failed");
		auto resized_description = display_source_description;
		resized_description.Width += 16;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> resized_source;
		vr::tests::require(SUCCEEDED(warp_graphics.device->CreateTexture2D(
			&resized_description, nullptr, &resized_source)), "resized source fixture failed");
		constexpr std::uint64_t resized_pair_id = 7995;
		const auto before_resized = session.get_status();
		vr::tests::require(session.admit_pair(resized_pair_id) &&
			!session.copy_eye(resized_pair_id, 0, resized_source.Get(), 5, warp_graphics.context.Get()),
			"source extent mismatch was not rejected");
		const auto resized_status = session.get_status();
		const auto& resized_failure = resized_status.last_copy_failure;
		vr::tests::require(resized_failure.stage == vr::native_render_session::copy_failure::source_extent &&
			resized_failure.pair_id == resized_pair_id && resized_failure.eye == 0 &&
			resized_failure.source_descriptor_read &&
			resized_failure.source_descriptor.Width == resized_description.Width &&
			resized_failure.expected_width == hdr_source_description.Width &&
			resized_failure.expected_context == reinterpret_cast<std::uintptr_t>(warp_graphics.context.Get()) &&
			resized_status.conversion_attempts == before_resized.conversion_attempts &&
			resized_status.pair_acquires == before_resized.pair_acquires &&
			session.discard_unpublished_pair(resized_pair_id),
			"pre-GPU rejection lost its source/expected contract or allocated a pair");
		const auto discard_quarantine_baseline =
			session.get_status().pair_quarantines;
		constexpr std::uint64_t unallocated_pair_id = 7998;
		vr::tests::require(session.admit_pair(unallocated_pair_id) &&
			session.discard_unpublished_pair(unallocated_pair_id) &&
			session.pair_failed(unallocated_pair_id) &&
			!session.accepts_pair(unallocated_pair_id) &&
			!session.pair_published(unallocated_pair_id) &&
			session.get_status().pair_quarantines == discard_quarantine_baseline,
			"discarding an unallocated predicted family quarantined ring storage");
		const auto deferral_baseline = session.get_status().pair_deferrals;
		constexpr std::uint64_t deferred_pair_id = 7997;
		constexpr std::uint64_t post_deferral_pair_id = 7996;
		vr::tests::require(session.admit_pair(deferred_pair_id) &&
			session.defer_unpublished_pair(deferred_pair_id) &&
			session.pair_deferred(deferred_pair_id) &&
			!session.pair_failed(deferred_pair_id) &&
			!session.accepts_pair(deferred_pair_id) &&
			!session.pair_published(deferred_pair_id) &&
			session.get_status().pair_deferrals == deferral_baseline + 1 &&
			session.admit_pair(post_deferral_pair_id) &&
			session.discard_unpublished_pair(post_deferral_pair_id),
			"deferring a cache-warmup prediction did not admit a fresh pose family");
		constexpr std::uint64_t partial_pair_id = 7999;
		vr::tests::require(session.admit_pair(partial_pair_id) &&
			session.copy_eye(partial_pair_id, 0, hdr_source.Get(), 5,
				warp_graphics.context.Get()) &&
			session.discard_unpublished_pair(partial_pair_id) &&
			session.pair_failed(partial_pair_id) &&
			!session.pair_published(partial_pair_id) &&
			session.get_status().pair_quarantines == discard_quarantine_baseline,
			"discarding a partial unpublished pair did not return its ring storage");
		constexpr std::uint64_t revoked_pair_id = 8000;
		const auto quarantine_baseline = session.get_status().pair_quarantines;
		vr::tests::require(session.admit_pair(revoked_pair_id) &&
			session.copy_eye(revoked_pair_id, 0, hdr_source.Get(), 5,
				warp_graphics.context.Get()) &&
			session.copy_eye(revoked_pair_id, 1, hdr_source.Get(), 5,
				warp_graphics.context.Get()) &&
			session.pair_published(revoked_pair_id),
			"published-pair revoke fixture did not reach the handoff state");
		session.quarantine_pair(revoked_pair_id);
		std::array<vr::native_render_session::eye_target, 2> revoked{};
		const auto revoked_status = session.get_status();
		vr::tests::require(!session.pair_published(revoked_pair_id) &&
			!session.acquire_published_pair(revoked_pair_id, revoked) &&
			session.pair_failed(revoked_pair_id) &&
			revoked_status.pair_quarantines == quarantine_baseline + 1,
			"quarantine left a transaction-failed published pair acquirable");
		session.invalidate(warp_graphics.generation);
	}

	void expect_same_family_stereo_compositor(const d3d11::device_snapshot& graphics)
	{
		compositor_fixture left_fixture(graphics);
		compositor_fixture right_fixture(graphics);
		vr::scene_compositor compositor;
		std::string error;
		const auto left_source = left_fixture.source_texture();
		const auto right_source = right_fixture.source_texture();
		D3D11_TEXTURE2D_DESC left_description{};
		D3D11_TEXTURE2D_DESC right_description{};
		left_source->GetDesc(&left_description);
		right_source->GetDesc(&right_description);

		left_fixture.clear_source({0.10f, 0.20f, 0.30f, 1.0f});
		right_fixture.clear_source({0.70f, 0.10f, 0.20f, 1.0f});
		const std::array mismatched{
			vr::stereo_capture_source{left_source.Get(), left_description,
				1, graphics.generation, 1, 0},
			vr::stereo_capture_source{right_source.Get(), right_description,
				2, graphics.generation, 1, 1},
		};
		vr::tests::require(!compositor.prepare_stereo_pair(graphics, mismatched, error) &&
			!compositor.stereo_source_available(graphics.generation),
			"opposite eye from a different engine frame was accepted as stereo");
		const std::array matched{
			vr::stereo_capture_source{left_source.Get(), left_description,
				1, graphics.generation, 1, 0},
			vr::stereo_capture_source{right_source.Get(), right_description,
				1, graphics.generation, 1, 1},
		};
		vr::tests::require(compositor.prepare_stereo_pair(graphics, matched, error) &&
			compositor.stereo_source_available(graphics.generation),
			std::format("same-family opposite eye did not complete the stereo pair: {}", error));

		const std::array<std::array<std::uint8_t, 4>, 2> expected{{
			{26, 51, 77, 255},
			{179, 26, 51, 255},
		}};
		for (std::size_t eye = 0; eye < 2; ++eye)
		{
			vr::tests::require(compositor.render_eye(graphics,
				{left_fixture.eye_view(eye), 4, 4}, error, static_cast<std::uint32_t>(2 + eye)),
				std::format("temporal stereo eye {} render failed: {}", eye, error));
			const auto rendered = left_fixture.read_eye(eye);
			for (std::size_t channel = 0; channel < expected[eye].size(); ++channel)
			{
				vr::tests::require(std::abs(static_cast<int>(rendered[channel]) -
					static_cast<int>(expected[eye][channel])) <= 2,
					std::format("temporal eye {} channel {} was not retained", eye, channel));
			}
		}

		compositor.revoke_sources();
		vr::tests::require(!compositor.render_eye(graphics,
			{left_fixture.eye_view(0), 4, 4}, error, 2),
			"revoked temporal compositor unexpectedly rendered");
		vr::tests::require(!compositor.render_eye({graphics.device, graphics.context,
			graphics.feature_level, graphics.generation + 1},
			{left_fixture.eye_view(0), 4, 4}, error, 2),
			"stale-generation temporal source was accepted for rendering");
	}
}

int main()
{
	try
	{
		expect_dynamic_upload_advances_once();
		expect_gpu_queue_gate_serializes_threads();
		expect_openvr_present_transaction_contract();
		expect_openvr_gpu_transport_requires_exact_arming();
		expect_openvr_raw_projection_preserves_vertical_edges();
		expect_openvr_shutdown_waits_for_matching_post();
		expect_openvr_present_status_observability();
		expect_backend_override();
		expect_process_runtime_preference();
		expect_incompatible_layer_isolated();
		const auto graphics = vr::tests::create_graphics(1);
		expect_backbuffer_compositor(graphics);
		expect_cross_device_backbuffer_pipeline(graphics);
		expect_explicit_engine_texture_capture(graphics);
		expect_atomic_native_stereo_capture(graphics);
		expect_native_direct_submission_ring(graphics);
		expect_same_family_stereo_compositor(graphics);
		vr::openxr::runtime_backend runtime;

		vr::tests::require(runtime.get_status().sdk_headers_available,
			"sdk_headers_available must be true for the formal VR smoke test");
		runtime.set_desired_enabled(true);
		expect_loader_missing(runtime, graphics, 1);

		runtime.request_reinitialize();
		expect_loader_missing(runtime, graphics, 2);

		runtime.request_reinitialize();
		expect_loader_missing(runtime, graphics, 3);

		runtime.set_desired_enabled(false);
		runtime.on_present(graphics, 1);
		auto status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::disabled,
			"disable did not produce disabled state");
		vr::tests::require(!status.desired_enabled && !status.applied_enabled,
			"disable left requested or applied VR enabled");

		runtime.set_desired_enabled(true);
		expect_loader_missing(runtime, graphics, 4);
		runtime.set_desired_enabled(false);
		vr::tests::require(!runtime.initialize(graphics),
			"disabled initialization unexpectedly succeeded");
		status = runtime.get_status();
		vr::tests::require(status.state == vr::runtime_state::disabled,
			"repeated disable did not remain disabled");

		runtime.shutdown();
		std::cout << "vr-runtime-no-loader-smoke: PASS\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "vr-runtime-no-loader-smoke: FAIL: " << error.what() << '\n';
		return 1;
	}
}
