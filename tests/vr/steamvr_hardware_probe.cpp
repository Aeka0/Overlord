#include <std_include.hpp>

#include "component/vr/vr_runtime.hpp"
#include "test_support.hpp"

#include <chrono>
#include <thread>

namespace
{
	constexpr auto probe_duration = std::chrono::seconds(8);
	constexpr std::uint64_t minimum_submitted_frames = 90;

	class environment_override final
	{
	public:
		environment_override(const char* const name, const char* const value) : name_(name)
		{
			const auto required = GetEnvironmentVariableA(name, nullptr, 0);
			if (required != 0)
			{
				previous_.resize(required);
				const auto copied = GetEnvironmentVariableA(name, previous_.data(), required);
				if (copied != 0 && copied < required)
				{
					previous_.resize(copied);
					had_previous_ = true;
				}
			}
			vr::tests::require(SetEnvironmentVariableA(name, value) != FALSE,
				"failed to select the OpenVR backend for the hardware probe");
		}

		~environment_override()
		{
			SetEnvironmentVariableA(name_, had_previous_ ? previous_.c_str() : nullptr);
		}

		environment_override(const environment_override&) = delete;
		environment_override& operator=(const environment_override&) = delete;

	private:
		const char* name_;
		std::string previous_;
		bool had_previous_{};
	};

	bool terminal_failure(const vr::runtime_status& status)
	{
		return status.state == vr::runtime_state::runtime_unavailable ||
			status.state == vr::runtime_state::no_hmd ||
			status.state == vr::runtime_state::graphics_mismatch ||
			status.state == vr::runtime_state::recoverable_error ||
			status.state == vr::runtime_state::fatal_for_vr;
	}

	void print_status(const vr::runtime_status& status)
	{
		// Use stderr for the diagnostic snapshot so it is emitted before the
		// cleanup path changes the backend state to `disabled`.
		std::cerr << "backend=" << status.backend_name
			<< " reason=" << status.backend_selection_reason
			<< " state=" << vr::to_string(status.state)
			<< " stage=" << status.last_initialization_stage
			<< " runtime=" << (status.runtime_name.empty() ? "unavailable" : status.runtime_name)
			<< " hmd=" << (status.system_name.empty() ? "unavailable" : status.system_name)
			<< " submitted_frames=" << status.submitted_frames
			<< " left_released=" << status.eyes[0].released
			<< " right_released=" << status.eyes[1].released
			<< " worker_frames=" << status.worker_present_count
			<< " error=" << (status.last_error.empty() ? "none" : status.last_error)
			<< '\n' << std::flush;
	}
}

int main()
{
	try
	{
		environment_override backend{"H2V_VR_BACKEND", "openvr"};
		const auto graphics = vr::tests::create_hardware_graphics(1);
		auto& runtime = vr::runtime::get();
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		runtime.set_desired_enabled(true);
		runtime.request_reinitialize();

		const auto deadline = std::chrono::steady_clock::now() + probe_duration;
		std::uint64_t frame{};
		vr::runtime_status status;
		while (std::chrono::steady_clock::now() < deadline)
		{
			runtime.on_present(graphics, ++frame);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			status = runtime.get_status();
			if (terminal_failure(status))
			{
				break;
			}
		}

		status = runtime.get_status();
		print_status(status);
		const bool accepted = status.backend_name == "openvr" &&
			status.state == vr::runtime_state::running &&
			status.submitted_frames >= minimum_submitted_frames &&
			status.eyes[0].released == status.submitted_frames &&
			status.eyes[1].released == status.submitted_frames;
		runtime.set_desired_enabled(false);
		runtime.shutdown();

		if (!accepted)
		{
			std::cerr << "vr-steamvr-hardware-probe: FAIL; SteamVR did not receive "
				<< minimum_submitted_frames << " complete stereo frames\n";
			return 2;
		}

		std::cerr << "vr-steamvr-hardware-probe: PASS; verify the HMD showed a pulsing "
			"magenta left eye and cyan right eye\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "vr-steamvr-hardware-probe: FAIL: " << error.what() << '\n';
		return 1;
	}
}
