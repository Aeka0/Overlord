#pragma once

#include <cstdint>
#include <memory>

#include "vr_runtime.hpp"
#include "present_transaction.hpp"

namespace vr::openvr
{
	struct projection_tangents
	{
		float left{};
		float right{};
		float down{};
		float up{};
	};

	using present_transaction_key = present_transaction::key;
	using present_post_validation = present_transaction::validation;

	[[nodiscard]] present_post_validation validate_present_post(
		const present_transaction_key& transaction,
		const d3d11::present_event& event, std::uint32_t thread_id,
		HRESULT result) noexcept;
	[[nodiscard]] bool present_owner_change_is_violation(bool applied_enabled,
		bool reinitialize_pending, std::uint64_t owner_generation,
		std::uint32_t owner_thread_id, std::uint64_t event_generation,
		std::uint32_t event_thread_id) noexcept;
	[[nodiscard]] bool gpu_frame_transport_is_armed(const runtime_status& status) noexcept;
	[[nodiscard]] projection_tangents projection_tangents_from_raw(float left,
		float right, float top, float bottom) noexcept;

	class runtime_backend final
	{
	public:
		runtime_backend();
		~runtime_backend();

		void set_desired_enabled(bool enabled);
		void set_scene_mode(scene_mode mode);
		void request_reinitialize();
		void prepare_frame(const d3d11::device_snapshot& graphics, std::uint64_t frame_index);
		[[nodiscard]] bool initialize(const d3d11::device_snapshot& graphics);
		void on_present(const d3d11::device_snapshot& graphics, std::uint64_t frame_index);
		void on_present(const d3d11::present_event& event);
		void on_present_post(const d3d11::present_event& event, HRESULT result);
		void capture_present(const d3d11::present_event& event);
		[[nodiscard]] bool capture_engine_texture(const d3d11::device_snapshot& graphics,
			ID3D11Texture2D* source, capture_frame_tag tag);
		void poll_capture(const d3d11::device_snapshot& graphics);
		void on_resize_before(const d3d11::resize_event& event) noexcept;
		void on_device_destroying(const d3d11::device_snapshot& graphics) noexcept;
		void shutdown() noexcept;
		[[nodiscard]] bool shutdown_complete() const noexcept;

		[[nodiscard]] bool requested_enabled() const;
		[[nodiscard]] bool applied_enabled() const;
		[[nodiscard]] runtime_status get_status() const;

	private:
		friend struct runtime_test_access;
		class implementation;
		std::unique_ptr<implementation> implementation_;
	};
}
