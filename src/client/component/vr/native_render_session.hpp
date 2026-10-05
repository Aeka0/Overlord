#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <wrl/client.h>

#include "component/d3d11.hpp"
#include "eye_composition.hpp"
#include "desktop_stabilization.hpp"

namespace vr::native_render_session
{
	class session;
	[[nodiscard]] session& active() noexcept;

	struct eye_target
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> color;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> color_view;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_view;
		std::uint32_t width{};
		std::uint32_t height{};
	};

	struct desktop_eye
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		std::uint64_t pair_id{};
		std::uint64_t generation{};
		std::uint32_t width{}, height{};
		engine_stereo_bridge::eye_projection projection{};
		desktop_mirror::crop recording_crop{};
		desktop_mirror::camera_sample camera{};
	};

	enum class copy_failure : std::uint8_t
	{
		none, admission, session, arguments, context, source_device,
		deferred_context, source_descriptor, source_extent, ring_exhausted,
		ring_state, conversion_pipeline, source_identity, source_view,
		command_list, device_removed,
	};

	[[nodiscard]] const char* to_string(copy_failure value) noexcept;

	// Value-only evidence from the first rejected copy in a pair. Cleanup and
	// successful copies must not erase it; addresses are identities, not owners.
	struct copy_failure_snapshot
	{
		copy_failure stage{copy_failure::none};
		HRESULT result{S_OK};
		std::uint64_t pair_id{};
		std::uint64_t expected_pair_id{};
		std::uint64_t device_generation{};
		std::uint64_t rebuilds{};
		std::uint64_t tick_ms{};
		std::uint32_t eye{2};
		std::uint32_t thread_id{};
		std::uintptr_t source{};
		std::uintptr_t cached_source{};
		std::uintptr_t context{};
		std::uintptr_t expected_context{};
		std::uintptr_t expected_device{};
		bool available{};
		bool accepting_pairs{};
		bool copy_ring{};
		bool source_descriptor_read{};
		D3D11_TEXTURE2D_DESC source_descriptor{};
		std::uint32_t expected_width{};
		std::uint32_t expected_height{};
		DXGI_FORMAT expected_source_format{DXGI_FORMAT_UNKNOWN};
	};

	struct status
	{
		bool available{};
		std::uint64_t device_generation{};
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t format{};
		std::uint32_t source_format{};
		bool copy_ring{};
		bool accepting_pairs{};
		std::uint64_t expected_pair_id{};
		bool deferred_conversion{};
		std::uint64_t rebuilds{};
		std::uint64_t invalidations{};
		std::uint64_t capture_requests{};
		std::uint64_t capture_failures{};
		std::uint64_t pair_acquires{};
		std::uint64_t pair_releases{};
		std::uint64_t pair_deferrals{};
		std::uint64_t pair_quarantines{};
		std::uint64_t pair_exhaustions{};
		std::uint64_t pair_rejections{};
		std::uint64_t acquisition_suspends{};
		std::uint64_t acquisition_resumes{};
		std::uint64_t acquisition_rejections{};
		std::uint64_t conversion_attempts{};
		std::uint64_t conversion_completions{};
		std::uint64_t conversion_failures{};
		copy_failure_snapshot last_copy_failure{};
		bool source_view_cached{};
		std::uint64_t source_view_creations{};
		std::uint64_t source_identity_matches{};
		std::uint64_t source_identity_mismatches{};
		std::uint64_t command_list_builds{};
		std::uint64_t command_list_build_failures{};
		std::uint64_t command_list_tag_failures{};
		std::uint64_t command_list_executions{};
		std::array<std::uint64_t, 2> copy_lock_wait_samples{};
		std::array<std::uint64_t, 2> copy_lock_wait_last_us{};
		std::array<std::uint64_t, 2> copy_lock_wait_max_us{};
		std::array<std::uint64_t, 2> copy_lock_wait_total_us{};
		std::array<std::uint64_t, 2> command_list_execute_samples{};
		std::array<std::uint64_t, 2> command_list_execute_last_us{};
		std::array<std::uint64_t, 2> command_list_execute_max_us{};
		std::array<std::uint64_t, 2> command_list_execute_total_us{};
		std::array<std::uint64_t, 2> removed_reason_samples{};
		std::array<std::uint64_t, 2> removed_reason_last_us{};
		std::array<std::uint64_t, 2> removed_reason_max_us{};
		std::array<std::uint64_t, 2> removed_reason_total_us{};
		std::uint64_t last_source_texture{};
		std::uint64_t last_source_view{};
		std::uint32_t last_source_view_create_result{};
		std::uint32_t deferred_context_create_result{};
		std::uint32_t last_command_list_result{};
		std::uint32_t conversion_pipeline_result{};
		std::uint32_t last_conversion_result{};
		std::uint32_t destination_format_support{};
		std::string capability_probe{};
	};

	class session final
	{
	public:
		[[nodiscard]] bool ensure(const d3d11::device_snapshot& graphics,
			std::uint32_t width, std::uint32_t height, std::string& error) noexcept;
		[[nodiscard]] bool ensure_copy_ring(const d3d11::device_snapshot& graphics,
			const D3D11_TEXTURE2D_DESC& source, DXGI_FORMAT destination_format,
			std::string& error) noexcept;
		void invalidate(std::uint64_t device_generation = 0) noexcept;
		[[nodiscard]] bool available(const d3d11::device_snapshot& graphics) const noexcept;
		[[nodiscard]] bool suspend_acquisition() noexcept;
		[[nodiscard]] bool admit_pair(std::uint64_t pair_id) noexcept;
		[[nodiscard]] bool accepts_pair(std::uint64_t pair_id) const noexcept;
		// The target pairs are the OpenVR submission ring. An eye texture is owned
		// by exactly one stereo family until the next successful WaitGetPoses retires
		// that submitted family.
		[[nodiscard]] bool acquire_target(std::uint64_t pair_id, std::uint32_t eye,
			eye_target& output) noexcept;
		[[nodiscard]] bool complete_rendered_eye(std::uint64_t pair_id,
			std::uint32_t eye, ID3D11Texture2D* texture) noexcept;
		[[nodiscard]] bool copy_eye(std::uint64_t pair_id, std::uint32_t eye,
			ID3D11Texture2D* source, std::uint32_t source_target, ID3D11DeviceContext* context,
			const eye_composition::event* composition = nullptr) noexcept;
		[[nodiscard]] bool acquire_published_pair(std::uint64_t pair_id,
			std::array<eye_target, 2>& output) noexcept;
		// Read-only right-eye preview on the same H2 GPU owner thread. This does
		// not lease/retire a pair, wait for SteamVR, or retain a desktop backbuffer.
		[[nodiscard]] desktop_eye right_eye_for_desktop() const noexcept;
		[[nodiscard]] bool pair_failed(std::uint64_t pair_id) const noexcept;
		[[nodiscard]] bool pair_deferred(std::uint64_t pair_id) const noexcept;
		[[nodiscard]] bool pair_published(std::uint64_t pair_id) const noexcept;
		[[nodiscard]] bool release_pair(std::uint64_t pair_id) noexcept;
		// Terminates a predicted family that never reached compositor ownership.
		// Rendering storage is returned to the ring, while published/quarantined
		// pairs retain their existing non-reusable safety semantics.
		[[nodiscard]] bool discard_unpublished_pair(std::uint64_t pair_id) noexcept;
		// Retires a deliberately unsubmitted prediction after an exact CPU-side
		// dependency was learned. Unlike discard_unpublished_pair(), this is not a
		// renderer failure and allows OpenVR to sample a fresh pose family.
		[[nodiscard]] bool defer_unpublished_pair(std::uint64_t pair_id) noexcept;
		void quarantine_pair(std::uint64_t pair_id) noexcept;
		void cancel_pair(std::uint64_t pair_id) noexcept;
		void record_capture(bool success) noexcept;
		[[nodiscard]] status get_status() const noexcept;

	private:
		mutable std::recursive_mutex mutex_;
		enum class pair_state : std::uint8_t { free, rendering, published, quarantined };
		struct target_pair
		{
			std::array<eye_target, 2> eyes{};
			std::array<std::array<Microsoft::WRL::ComPtr<ID3D11CommandList>, 2>, 2> conversion_commands{};
			std::uint64_t pair_id{};
			std::uint32_t completed_eye_mask{};
			std::uint32_t active_eye_mask{};
			pair_state state{pair_state::free};
			std::uint64_t preview_pair_id{};
			std::uint32_t preview_owner_thread{};
			engine_stereo_bridge::eye_projection preview_projection{};
			desktop_mirror::crop preview_recording_crop{};
			desktop_mirror::camera_sample preview_camera{};
		};
		void invalidate_locked() noexcept;
		[[nodiscard]] bool terminate_unpublished_pair_locked(std::uint64_t pair_id,
			bool failed) noexcept;
		static constexpr std::size_t pair_count = 2;
		std::array<target_pair, pair_count> pairs_{};
		std::size_t next_pair_index_{};
		std::uint64_t device_generation_{};
		std::uint32_t width_{};
		std::uint32_t height_{};
		DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
		DXGI_FORMAT source_format_{DXGI_FORMAT_UNKNOWN};
		bool copy_ring_{};
		bool accepting_pairs_{};
		std::uint64_t expected_pair_id_{};
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> conversion_deferred_context_;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> conversion_vertex_shader_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> conversion_pixel_shader_;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> conversion_rasterizer_state_;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> conversion_depth_state_;
		Microsoft::WRL::ComPtr<ID3D11BlendState> conversion_blend_state_;
		// H2 targets 4 and 5 exchange scene/display roles. Each identity remains
		// stable within a device generation; cache both SRVs and each ring eye's
		// commands by target, without rebuilding GPU state on thermal transitions.
		std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2> conversion_sources_{};
		std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2> conversion_source_views_{};
		std::uint64_t failed_pair_id_{};
		std::uint64_t deferred_pair_id_{};
		status status_{};
	};
}
