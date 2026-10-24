#include <std_include.hpp>
#include "sound_output_diagnostics.hpp"
#include "loader/component_loader.hpp"
#include "console.hpp"
#include "scheduler.hpp"
#include "vr/debug_options.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <mutex>
#include <sstream>

namespace sound::output_diagnostics
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		utils::hook::detour initialize_hook, start_hook;
		std::mutex mutex;
		std::string latest{"audio_output: no native device lifecycle observed\n"};
		std::uint64_t revision{};
		std::string utf8(const wchar_t* text)
		{
			if (!text) return {};
			const auto count = static_cast<int>(wcsnlen(text, 2048));
			const auto bytes = WideCharToMultiByte(CP_UTF8, 0, text, count, nullptr, 0, nullptr, nullptr);
			std::string result(bytes, '\0');
			if (bytes) WideCharToMultiByte(CP_UTF8, 0, text, count, result.data(), bytes, nullptr, nullptr);
			return result;
		}
		void capture(const char* stage, int started)
		{
			// H2 SND_InitDriver / WASAPI Start retain these objects. Observe only
			// after their original calls, on the same native owner thread. Never
			// cache COM pointers or query them from vr_status / the async writer.
			auto* device = *reinterpret_cast<IMMDevice* const*>(0x14BDF0658);
			auto* client = *reinterpret_cast<IAudioClient* const*>(0x14BDF0620);
			std::ostringstream out;
			out << "audio_output: stage=" << stage << " start_result=" << started
				<< " pid=" << GetCurrentProcessId() << " tick=" << GetTickCount64()
				<< " device=" << (device != nullptr) << " client=" << (client != nullptr) << '\n';
			if (device)
			{
				DWORD state{}; LPWSTR id{};
				const auto state_hr = device->GetState(&state), id_hr = device->GetId(&id);
				out << "  endpoint_state=" << state << " state_hresult=" << std::hex << state_hr
					<< " id_hresult=" << id_hr << std::dec << " id=" << utf8(id) << '\n';
				CoTaskMemFree(id);
			}
			if (client)
			{
				UINT32 padding{}, frames{};
				const auto padding_hr = client->GetCurrentPadding(&padding), size_hr = client->GetBufferSize(&frames);
				out << "  padding=" << padding << " frames=" << frames << " padding_hresult=" << std::hex
					<< padding_hr << " size_hresult=" << size_hr << std::dec << '\n';
				ComPtr<ISimpleAudioVolume> volume;
				const auto volume_hr = client->GetService(IID_PPV_ARGS(&volume));
				out << "  volume_service_hresult=" << std::hex << volume_hr << std::dec;
				if (volume)
				{
					float level{}; BOOL muted{};
					const auto level_hr = volume->GetMasterVolume(&level), mute_hr = volume->GetMute(&muted);
					out << " volume=" << level << " muted=" << muted << " level_hresult=" << std::hex
						<< level_hr << " mute_hresult=" << mute_hr << std::dec;
				}
				out << '\n';
				ComPtr<IAudioSessionControl> control;
				const auto control_hr = client->GetService(IID_PPV_ARGS(&control));
				out << "  session_service_hresult=" << std::hex << control_hr << std::dec;
				if (control)
				{
					AudioSessionState state{}; const auto hr = control->GetState(&state);
					out << " state=" << int(state) << " state_hresult=" << std::hex << hr << std::dec;
					ComPtr<IAudioSessionControl2> control2;
					if (SUCCEEDED(control.As(&control2)))
					{
						DWORD pid{}; LPWSTR id{};
						const auto pid_hr = control2->GetProcessId(&pid), id_hr = control2->GetSessionIdentifier(&id);
						out << " pid=" << pid << " pid_hresult=" << std::hex << pid_hr
							<< " id_hresult=" << id_hr << std::dec << " id=" << utf8(id);
						CoTaskMemFree(id);
					}
				}
				out << '\n';
			}
			const auto text = out.str(); std::uint64_t version{};
			{ const std::lock_guard lock(mutex); latest = text; version = ++revision; }
			console::print_text(console::con_type_info, text);
			scheduler::once([text, version] {
				{ const std::lock_guard lock(mutex); if (version != revision) return; }
				utils::io::write_file("minidumps/overlord-audio.txt", text);
			}, scheduler::pipeline::async);
		}
		void initialize(int value)
		{
			initialize_hook.invoke<void>(value);
			try { capture("initialize", -1); }
			catch (const std::exception& e) { console::warn("[audio diagnostics] %s\n", e.what()); }
		}
		bool start()
		{
			const auto result = start_hook.invoke<bool>();
			try { capture("start", result ? 1 : 0); }
			catch (const std::exception& e) { console::warn("[audio diagnostics] %s\n", e.what()); }
			return result;
		}
	}
	std::string status()
	{
		if (!vr::debug_options::enabled(vr::debug_options::probe::audio_output))
			return "audio_output: disabled (vr_debugAudioOutput; requires game restart)\n";
		const std::lock_guard lock(mutex); return latest;
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			if (!vr::debug_options::enabled(vr::debug_options::probe::audio_output)) return;
			constexpr std::array<std::uint8_t, 11> bytes{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x7c,0x24,0x10,0x55};
			std::array<std::uint8_t, bytes.size()> mask; mask.fill(255);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1405D9E00), {bytes.data(),mask.data(),bytes.size()}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1405DC3E0), {bytes.data(),mask.data(),bytes.size()}))
			{
				const std::lock_guard lock(mutex); latest = "audio_output: native lifecycle contract rejected\n"; return;
			}
			initialize_hook.create(0x1405D9E00, initialize);
			start_hook.create(0x1405DC3E0, start);
		}
	};
}
REGISTER_COMPONENT(sound::output_diagnostics::component)
