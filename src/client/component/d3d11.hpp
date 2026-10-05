#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace d3d11
{
	constexpr GUID guid_shader_bytecode = {0x8B07816E, 0x844C, 0x4F5C, {0xA8, 0x83, 0x1C, 0x35, 0x56, 0xDC, 0x54, 0x20}};

	using listener_token = std::uint64_t;

	enum class gpu_queue_client : std::uint8_t
	{
		unknown,
		present,
		resize_buffers,
		openvr,
	};

	struct gpu_queue_status
	{
		std::uint64_t acquire_count{};
		std::uint64_t contention_count{};
		std::uint64_t present_acquire_count{};
		std::uint64_t resize_acquire_count{};
		std::uint64_t openvr_acquire_count{};
		std::uint64_t total_wait_us{};
		std::uint64_t maximum_wait_us{};
		std::uint64_t last_wait_us{};
		DWORD last_thread_id{};
		gpu_queue_client last_client{gpu_queue_client::unknown};
		bool waiter_active{};
		DWORD waiter_thread_id{};
		gpu_queue_client waiter_client{gpu_queue_client::unknown};
		std::uint64_t waiter_started_tick{};
	};

	struct device_creation_status
	{
		bool available{};
		std::uint64_t d3d11_create_index{};
		std::uint64_t factory1_count_before_create{};
		HRESULT factory1_result{S_OK};
		HRESULT factory1_query_result{S_OK};
		HRESULT factory1_parent_result{S_OK};
		bool adapter_explicit{};
		bool factory_identity_match{};
		UINT requested_flags{};
		UINT creation_flags{};
		HRESULT multithread_query_result{E_NOINTERFACE};
		bool multithread_protected{};
		DWORD creation_thread_id{};
		std::uintptr_t device_address{};
		std::uintptr_t context_address{};
	};

	struct debug_message_sample
	{
		std::uint64_t count{};
		std::uint32_t severity{};
		std::uint32_t id{};
		DWORD thread_id{};
		std::string description;
		std::uint64_t first_tick_ms{};
		std::uint64_t first_present{};
		std::uint64_t device_generation{};
		std::uint16_t stack_size{};
		std::array<void*, 24> stack{};
	};

	struct device_snapshot
	{
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		D3D_FEATURE_LEVEL feature_level{D3D_FEATURE_LEVEL_11_0};
		std::uint64_t generation{};
		device_creation_status creation;

		explicit operator bool() const
		{
			return device && context && generation != 0;
		}
	};

	struct present_event
	{
		IDXGISwapChain* swap_chain{};
		device_snapshot graphics;
		std::uint64_t frame_index{};
		std::chrono::steady_clock::time_point timestamp{};
		UINT sync_interval{};
		UINT flags{};
	};

	struct resize_event
	{
		IDXGISwapChain* swap_chain{};
		device_snapshot graphics;
		UINT buffer_count{};
		UINT width{};
		UINT height{};
		DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
		UINT flags{};
		std::uint64_t resize_index{};
		std::chrono::steady_clock::time_point timestamp{};
	};

	struct graphics_status
	{
		std::uint64_t dxgi_factory1_create_count{};
		std::uint64_t d3d11_create_count{};
		device_creation_status active_device_creation;
		gpu_queue_status gpu_queue;
		bool present_hook_installed{};
		bool resize_hook_installed{};
		bool hook_targets_valid{};
		std::uint64_t generation{};
		HWND active_output_window{};
		UINT active_width{};
		UINT active_height{};
		DXGI_FORMAT active_format{DXGI_FORMAT_UNKNOWN};
		UINT active_buffer_count{};
		std::uint64_t present_count{};
		std::uint64_t successful_present_count{};
		std::uint64_t failed_present_count{};
		std::uint64_t resize_count{};
		HRESULT last_present_result{S_OK};
		std::chrono::steady_clock::time_point last_present_pre{};
		std::chrono::steady_clock::time_point last_present_post{};
		DWORD last_present_thread_id{};
		std::uint64_t last_present_frame_index{};
		bool present_active{};
		DWORD present_thread_id{};
		std::chrono::steady_clock::time_point present_started{};
		bool present_gpu_scope_active{};
		DWORD present_gpu_scope_thread_id{};
		std::uint64_t present_gpu_scope_count{};
		std::chrono::steady_clock::time_point present_gpu_scope_started{};
		std::chrono::steady_clock::time_point last_resize_before{};
		std::chrono::steady_clock::time_point last_resize_completed{};
		std::chrono::steady_clock::duration last_present_duration{};
		std::chrono::steady_clock::duration last_resize_duration{};
		HRESULT last_resize_result{S_OK};
		HRESULT device_removed_reason{S_OK};
		bool debug_info_hook_installed{};
		std::uint64_t debug_message_count{};
		std::uint64_t debug_corruption_count{};
		std::uint64_t debug_error_count{};
		std::uint64_t debug_warning_count{};
		std::uint32_t last_debug_severity{};
		std::uint32_t last_debug_id{};
		DWORD last_debug_thread_id{};
		std::string last_debug_message;
		std::uint32_t debug_sample_count{};
		std::uint64_t debug_unretained_count{};
		std::array<debug_message_sample, 12> debug_samples{};
		std::string last_error;
	};

	using device_listener = std::function<void(const device_snapshot&)>;
	using present_listener = std::function<void(const present_event&)>;
	using post_present_listener = std::function<void(const present_event&, HRESULT)>;
	using resize_listener = std::function<void(const resize_event&)>;

	// Runs on the D3D11CreateDevice caller after the candidate generation and
	// private-data identity have been published, but before the hooked call
	// returns the device to H2. This is intentionally distinct from
	// subscribe_device_created(), which observes later swap-chain promotion.
	listener_token subscribe_device_created_early(device_listener listener);
	listener_token subscribe_device_created(device_listener listener);
	listener_token subscribe_device_destroying(device_listener listener);
	listener_token subscribe_present_pre(present_listener listener);
	listener_token subscribe_present_post(post_present_listener listener);
	listener_token subscribe_resize_before(resize_listener listener);

	void unsubscribe(listener_token token) noexcept;

	device_snapshot get_device_snapshot();
	graphics_status get_graphics_status();
	// Serialize short explicit H2-device context operations against its
	// split-threaded Present/ResizeBuffers calls. Blocking OpenVR calls must never
	// hold this gate.
	[[nodiscard]] std::unique_lock<std::recursive_mutex> acquire_gpu_queue_interop(
		gpu_queue_client client = gpu_queue_client::unknown);
	[[nodiscard]] bool is_inside_present_gpu_scope() noexcept;
	[[nodiscard]] bool enable_graphics_hooks() noexcept;
	void shutdown_graphics_hooks() noexcept;

	namespace detail
	{
		[[nodiscard]] device_snapshot inspect_candidate_device(ID3D11Device* device) noexcept;
		[[nodiscard]] device_snapshot promote_candidate_device(ID3D11Device* device) noexcept;
		void process_deferred_graphics_shutdown() noexcept;
		void notify_present_pre(const present_event& event) noexcept;
		void notify_present_post(const present_event& event, HRESULT result) noexcept;
		void notify_resize_before(const resize_event& event) noexcept;
		void set_graphics_hook_status(bool present_installed, bool resize_installed,
			bool targets_valid, std::string error = {});
		void set_active_swap_chain(const DXGI_SWAP_CHAIN_DESC& description, std::uint64_t generation);
		void clear_active_swap_chain(std::uint64_t generation);
		void record_present_result(std::uint64_t generation, HRESULT result,
			std::chrono::steady_clock::time_point started,
			std::chrono::steady_clock::time_point completed);
		void record_present_begin(std::uint64_t generation, std::uint64_t frame_index,
			std::chrono::steady_clock::time_point started, DWORD thread_id);
		void record_present_gpu_scope_begin(std::uint64_t generation, DWORD thread_id);
		void record_present_gpu_scope_end(std::uint64_t generation, DWORD thread_id);
		void record_resize_result(std::uint64_t generation, HRESULT result,
			std::chrono::steady_clock::time_point started,
			std::chrono::steady_clock::time_point completed);
		void record_device_removed_reason(std::uint64_t generation, HRESULT reason);
	}
}
