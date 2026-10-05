#include <std_include.hpp>

#include "runtime_backend.hpp"

#include "openvr_runtime.hpp"
#include "steamvr_runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>

namespace vr
{
	namespace
	{
		std::string lower_ascii(std::string value)
		{
			std::ranges::transform(value, value.begin(), [](const unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
			return value;
		}

		std::string environment_value(const char* const name)
		{
			const auto length = GetEnvironmentVariableA(name, nullptr, 0);
			if (length == 0) return {};
			std::string value(length, '\0');
			const auto copied = GetEnvironmentVariableA(name, value.data(), length);
			if (copied == 0 || copied >= length) return {};
			value.resize(copied);
			return value;
		}

		struct backend_choice
		{
			bool valid{true};
			std::string reason{"strict native path selects SteamVR/OpenVR"};
		};

		backend_choice choose_backend()
		{
			const auto requested = lower_ascii(environment_value("H2V_VR_BACKEND"));
			if (requested == "openvr" || requested == "steamvr")
			{
				return {true, "H2V_VR_BACKEND explicitly selected SteamVR/OpenVR"};
			}
			if (!requested.empty())
			{
				return {false, "strict native path rejects unsupported H2V_VR_BACKEND=" + requested};
			}

			const auto override_manifest = environment_value("XR_RUNTIME_JSON");
			if (!override_manifest.empty())
			{
				const auto normalized = lower_ascii(override_manifest);
				if (normalized.find("steamxr") != std::string::npos ||
					normalized.find("steamvr") != std::string::npos)
				{
					return {true, "XR_RUNTIME_JSON names SteamVR; selecting its OpenVR interface"};
				}
				return {false, "strict native path rejects a non-SteamVR XR_RUNTIME_JSON"};
			}

			const auto steamvr_runtime = steamvr::locate_active_runtime();
			if (steamvr_runtime.steamvr_manifest)
			{
				return {true,
					"the active runtime manifest is SteamVR; using its declared library through OpenVR"};
			}

			return {};
		}
	}

	class runtime_backend::implementation final
	{
	public:
		implementation() : choice_(choose_backend()) {}

		void set_desired_enabled(const bool enabled)
		{
			const std::lock_guard lock(mutex_);
			openvr_.set_desired_enabled(enabled);
		}
		void set_scene_mode(const scene_mode mode)
		{
			const std::lock_guard lock(mutex_);
			openvr_.set_scene_mode(mode);
		}
		void request_reinitialize()
		{
			const std::lock_guard lock(mutex_);
			openvr_.request_reinitialize();
		}
		void prepare_frame(const d3d11::device_snapshot& graphics, const std::uint64_t frame_index)
		{
			// Backend selection is immutable after construction. Renderer observation
			// is independent from the Present-owner OpenVR transaction.
			if (choice_.valid) openvr_.prepare_frame(graphics, frame_index);
		}
		bool initialize(const d3d11::device_snapshot& graphics)
		{
			const std::lock_guard lock(mutex_);
			return choice_.valid && openvr_.initialize(graphics);
		}
		void on_present(const d3d11::device_snapshot& graphics, const std::uint64_t frame)
		{
			const std::lock_guard lock(mutex_);
			if (choice_.valid) openvr_.on_present(graphics, frame);
		}
		void on_present(const d3d11::present_event& event)
		{
			const std::lock_guard lock(mutex_);
			if (choice_.valid) openvr_.on_present(event);
		}
		void on_present_post(const d3d11::present_event& event, const HRESULT result)
		{
			const std::lock_guard lock(mutex_);
			if (choice_.valid) openvr_.on_present_post(event, result);
		}
		void capture_present(const d3d11::present_event& event)
		{
			const std::lock_guard lock(mutex_);
			if (choice_.valid) openvr_.capture_present(event);
		}
		bool capture_engine_texture(const d3d11::device_snapshot& graphics,
			ID3D11Texture2D* const source, const capture_frame_tag tag)
		{
			const std::lock_guard lock(mutex_);
			return choice_.valid && openvr_.capture_engine_texture(graphics, source, tag);
		}
		void poll_capture(const d3d11::device_snapshot& graphics)
		{
			const std::lock_guard lock(mutex_);
			if (choice_.valid) openvr_.poll_capture(graphics);
		}
		void on_resize_before(const d3d11::resize_event& event) noexcept
		{
			const std::lock_guard lock(mutex_);
			if (choice_.valid) openvr_.on_resize_before(event);
		}
		void on_device_destroying(const d3d11::device_snapshot& graphics) noexcept
		{
			const std::lock_guard lock(mutex_);
			if (choice_.valid) openvr_.on_device_destroying(graphics);
		}
		void shutdown() noexcept
		{
			const std::lock_guard lock(mutex_);
			openvr_.shutdown();
		}
		bool shutdown_complete() const noexcept
		{
			const std::lock_guard lock(mutex_);
			return openvr_.shutdown_complete();
		}
		bool requested_enabled() const
		{
			const std::lock_guard lock(mutex_);
			return openvr_.requested_enabled();
		}
		bool applied_enabled() const
		{
			const std::lock_guard lock(mutex_);
			return choice_.valid && openvr_.applied_enabled();
		}
		bool requires_present_owner_execution() const noexcept
		{
			return true;
		}

		runtime_status get_status() const
		{
			auto status = openvr_.get_status();
			status.backend_name = "openvr";
			status.backend_selection_reason = choice_.reason;
			if (!choice_.valid)
			{
				status.state = runtime_state::runtime_unavailable;
				status.applied_enabled = false;
				status.last_error = choice_.reason;
			}
			return status;
		}

	private:
		backend_choice choice_;
		openvr::runtime_backend openvr_;
		mutable std::mutex mutex_;
	};

	runtime_backend::runtime_backend() : implementation_(std::make_unique<implementation>()) {}
	runtime_backend::~runtime_backend() = default;
	void runtime_backend::set_desired_enabled(const bool value)
	{
		const std::lock_guard lock(mutex_);
		implementation_->set_desired_enabled(value);
	}
	void runtime_backend::set_scene_mode(const scene_mode value)
	{
		const std::lock_guard lock(mutex_);
		implementation_->set_scene_mode(value);
	}
	void runtime_backend::request_reinitialize()
	{
		const std::lock_guard lock(mutex_);
		implementation_->request_reinitialize();
	}
	void runtime_backend::prepare_frame(const d3d11::device_snapshot& graphics,
		const std::uint64_t frame_index)
	{
		// prepare_frame only records renderer ownership for OpenVR. Runtime queue
		// calls are confined to the real DXGI Present pre/post transaction.
		implementation_->prepare_frame(graphics, frame_index);
	}
	bool runtime_backend::initialize(const d3d11::device_snapshot& value)
	{
		const std::lock_guard lock(mutex_);
		return implementation_->initialize(value);
	}
	void runtime_backend::on_present(const d3d11::device_snapshot& value, const std::uint64_t frame)
	{
		const std::lock_guard lock(mutex_);
		implementation_->on_present(value, frame);
	}
	void runtime_backend::on_present(const d3d11::present_event& value)
	{
		const std::lock_guard lock(mutex_);
		implementation_->on_present(value);
	}
	void runtime_backend::on_present_post(const d3d11::present_event& value, const HRESULT result)
	{
		const std::lock_guard lock(mutex_);
		implementation_->on_present_post(value, result);
	}
	void runtime_backend::capture_present(const d3d11::present_event& value)
	{
		const std::lock_guard lock(mutex_);
		implementation_->capture_present(value);
	}
	bool runtime_backend::capture_engine_texture(const d3d11::device_snapshot& graphics,
		ID3D11Texture2D* const source, const capture_frame_tag tag)
	{
		const std::lock_guard lock(mutex_);
		return implementation_->capture_engine_texture(graphics, source, tag);
	}
	void runtime_backend::poll_capture(const d3d11::device_snapshot& value)
	{
		const std::lock_guard lock(mutex_);
		implementation_->poll_capture(value);
	}
	void runtime_backend::on_resize_before(const d3d11::resize_event& value) noexcept
	{
		const std::lock_guard lock(mutex_);
		implementation_->on_resize_before(value);
	}
	void runtime_backend::on_device_destroying(const d3d11::device_snapshot& value) noexcept
	{
		const std::lock_guard lock(mutex_);
		implementation_->on_device_destroying(value);
	}
	void runtime_backend::shutdown() noexcept
	{
		const std::lock_guard lock(mutex_);
		implementation_->shutdown();
	}
	bool runtime_backend::shutdown_complete() const noexcept
	{
		const std::lock_guard lock(mutex_);
		return implementation_->shutdown_complete();
	}
	bool runtime_backend::requested_enabled() const
	{
		const std::lock_guard lock(mutex_);
		return implementation_->requested_enabled();
	}
	bool runtime_backend::applied_enabled() const
	{
		const std::lock_guard lock(mutex_);
		return implementation_->applied_enabled();
	}
	bool runtime_backend::requires_present_owner_execution() const noexcept
	{
		return implementation_->requires_present_owner_execution();
	}
	runtime_status runtime_backend::get_status() const
	{
		// Status is an immutable-backend observation.  Do not make diagnostics
		// wait behind an in-flight Submit/pose pacing call held by the facade.
		return implementation_->get_status();
	}
}
