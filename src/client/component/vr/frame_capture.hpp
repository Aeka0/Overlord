#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include "component/d3d11.hpp"

namespace vr
{
	struct capture_frame_tag
	{
		bool stereo{};
		std::uint64_t pair_id{};
		std::uint32_t eye_index{};
		bool native{};
	};

	struct captured_frame
	{
		// The producer object remains the authoritative H2 target for ownership
		// validation. Strict native capture requires texture to be this same object;
		// generic diagnostics may still populate it with a shared import.
		Microsoft::WRL::ComPtr<ID3D11Texture2D> producer_texture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		D3D11_TEXTURE2D_DESC description{};
		std::uint64_t frame_id{};
		std::uint64_t device_generation{};
		bool shared{};
		std::uint32_t slot_index{};
		std::uint32_t row_pitch{};
		std::vector<std::byte> cpu_pixels;
		capture_frame_tag tag{};

		explicit operator bool() const noexcept
		{
			return frame_id != 0 && device_generation != 0 &&
				(texture != nullptr || (!cpu_pixels.empty() && row_pitch != 0));
		}
	};

	enum class stereo_pair_acquire_result
	{
		ready,
		pending,
		failed,
	};

	struct frame_capture_status
	{
		bool snapshot_busy{};
		std::uint64_t produced{};
		std::uint64_t ready{};
		std::uint64_t acquired{};
		std::uint64_t released{};
		std::uint64_t dropped{};
		std::uint64_t query_pending{};
		std::uint64_t slot_exhausted{};
		std::uint64_t stale_generation{};
		std::uint64_t shared_opened{};
		std::uint64_t shared_import_reused{};
		std::uint64_t native_direct_acquired{};
		std::uint64_t shared_failed{};
		std::uint64_t cpu_fallback_requested{};
		std::uint64_t cpu_fallback_ready{};
		std::uint64_t cpu_fallback_consumed{};
		std::uint64_t native_produced{};
		std::uint64_t native_pairs_acquired{};
		std::uint64_t native_pair_misses{};
		std::uint64_t invalidated{};
	};

	class frame_capture final
	{
	public:
		frame_capture() = default;
		~frame_capture();

		frame_capture(const frame_capture&) = delete;
		frame_capture& operator=(const frame_capture&) = delete;

		// Called only from the game Present-pre callback. It never waits for the GPU.
		void produce(const d3d11::present_event& event,
			capture_frame_tag tag = {}) noexcept;
		// Called only after an engine eye pass has finished writing an explicit
		// render target. Native stereo retains the exact ordinary H2 texture;
		// diagnostic captures use the generic capture ring.
		[[nodiscard]] bool produce_texture(const d3d11::device_snapshot& graphics,
			ID3D11Texture2D* source, capture_frame_tag tag = {}) noexcept;
		// Called from the renderer boundary for generic diagnostic captures. Native
		// same-device eyes are published atomically without a query because OpenVR
		// consumes the exact texture through the standard D3D11 Submit contract.
		void poll(const d3d11::device_snapshot& graphics) noexcept;
		// Called only from the OpenXR worker. The game context is never touched here.
		[[nodiscard]] bool acquire(ID3D11Device* consumer_device, std::uint64_t generation,
			captured_frame& output, std::string& error) noexcept;
		// Atomically acquires the newest complete left/right pair.  Native stereo
		// rendering must never let the compositor observe a half-updated pair.
		[[nodiscard]] stereo_pair_acquire_result acquire_stereo_pair(ID3D11Device* consumer_device,
			std::uint64_t generation, std::uint64_t expected_pair_id,
			std::array<captured_frame, 2>& output,
			std::string& error) noexcept;
		void release(captured_frame& frame) noexcept;
		void invalidate(std::uint64_t generation) noexcept;
		void revoke_all() noexcept;
		[[nodiscard]] frame_capture_status get_status() const noexcept;
		// A busy capture producer is unavailable evidence, never zero counters.
		[[nodiscard]] bool try_get_status(frame_capture_status& output) const noexcept;

	private:
		enum class slot_state
		{
			free,
			producing,
			ready,
			acquired,
		};

		struct slot
		{
			slot_state state{slot_state::free};
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
			Microsoft::WRL::ComPtr<ID3D11Query> query;
			HANDLE shared_handle{};
			bool shared_nt_handle{};
			D3D11_TEXTURE2D_DESC description{};
			std::uint64_t frame_id{};
			std::uint64_t device_generation{};
			std::uint32_t row_pitch{};
			std::vector<std::byte> cpu_pixels;
			capture_frame_tag tag{};
		};

		struct shared_import
		{
			HANDLE handle{};
			bool nt_handle{};
			std::uint64_t device_generation{};
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		};

		void poll_queries_locked(const d3d11::device_snapshot& graphics) noexcept;
		void recycle_slot_locked(slot& value) noexcept;
		void reset_slot_locked(slot& value) noexcept;
		[[nodiscard]] bool ensure_slot_locked(const d3d11::device_snapshot& graphics,
			const D3D11_TEXTURE2D_DESC& source, slot& value,
			bool require_cpu_fallback) noexcept;
		[[nodiscard]] slot* find_free_slot_locked() noexcept;
		[[nodiscard]] slot* find_oldest_ready_slot_locked() noexcept;
		void discard_ready_except_locked(const slot* retained) noexcept;
		[[nodiscard]] bool produce_texture_locked(const d3d11::device_snapshot& graphics,
			ID3D11Texture2D* source, capture_frame_tag tag) noexcept;
		[[nodiscard]] bool open_slot_locked(ID3D11Device* consumer_device,
			std::uint64_t generation, slot& value, captured_frame& output,
			std::string& error, bool require_gpu_texture = false) noexcept;
		[[nodiscard]] HRESULT import_shared_locked(ID3D11Device* consumer_device,
			std::uint64_t generation, HANDLE handle, bool nt_handle,
			Microsoft::WRL::ComPtr<ID3D11Texture2D>& output) noexcept;
		void clear_shared_imports_locked(std::uint64_t generation = 0) noexcept;

		mutable std::mutex mutex_;
		std::array<slot, 4> slots_{};
		std::array<shared_import, 4> shared_imports_{};
		Microsoft::WRL::ComPtr<ID3D11Device> import_device_;
		std::uint64_t next_frame_id_{};
		std::uint64_t failed_native_pair_id_{};
		std::uint32_t failed_native_eye_{};
		HRESULT failed_native_result_{S_OK};
		frame_capture_status status_{};
	};
}
