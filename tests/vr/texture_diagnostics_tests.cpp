#include "component/d3d11/texture_diagnostics.hpp"
#include <exception/minidump.hpp>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace td = d3d11::texture_diagnostics;
namespace
{
	unsigned checks{};
	std::atomic<unsigned> calls{};
	std::atomic<HRESULT> result{S_OK};
	std::atomic<bool> blocking{};
	HANDLE entered{}, leave{};
	const D3D11_TEXTURE2D_DESC* expected_descriptor{};
	const D3D11_SUBRESOURCE_DATA* expected_initial{};
	std::atomic<bool> arguments_match{true};

	void require(const bool value, const char* message)
	{
		++checks;
		if (!value) throw std::runtime_error(message);
	}

	HRESULT STDMETHODCALLTYPE fake_create(ID3D11Device*, const D3D11_TEXTURE2D_DESC* descriptor,
		const D3D11_SUBRESOURCE_DATA* initial, ID3D11Texture2D** output)
	{
		++calls;
		if (descriptor != expected_descriptor || initial != expected_initial) arguments_match = false;
		if (blocking.load())
		{
			SetEvent(entered);
			WaitForSingleObject(leave, 5000);
		}
		const auto status = result.load();
		if (SUCCEEDED(status) && output) *output = reinterpret_cast<ID3D11Texture2D*>(0x123400);
		SetLastError(0x4321);
		return status;
	}

	std::array<void*, 6> fake_table{};
	struct fake_device { void** table{fake_table.data()}; } fake;
	ID3D11Device* device() { return reinterpret_cast<ID3D11Device*>(&fake); }

	D3D11_TEXTURE2D_DESC description()
	{
		D3D11_TEXTURE2D_DESC value{};
		value.Width = 1024; value.Height = 512; value.MipLevels = 1; value.ArraySize = 1;
		value.Format = DXGI_FORMAT_R8G8B8A8_UNORM; value.SampleDesc.Count = 1;
		value.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		return value;
	}

	void mock_checks(D3D11_TEXTURE2D_DESC& descriptor)
	{
		fake_table[5] = reinterpret_cast<void*>(fake_create);
		expected_descriptor = &descriptor;
		require(td::install(device(), 7), "mock texture observer not installed");
		require(td::install(device(), 7), "reinstall did not preserve original method");
		ID3D11Texture2D* texture{};
		require(device()->CreateTexture2D(&descriptor, nullptr, &texture) == S_OK &&
			texture == reinterpret_cast<ID3D11Texture2D*>(0x123400), "texture output or success result changed");
		require(GetLastError() == 0x4321 && calls.load() == 1 && arguments_match, "observer changed LastError or call arguments/count");
		td::snapshot before_unrelated;
		td::capture(before_unrelated);
		fake_device unrelated;
		require(reinterpret_cast<ID3D11Device*>(&unrelated)->CreateTexture2D(&descriptor, nullptr, &texture) == S_OK,
			"unregistered device sharing the method lost its original API call");
		td::snapshot after_unrelated;
		td::capture(after_unrelated);
		require(after_unrelated.texture_calls == before_unrelated.texture_calls && after_unrelated.newest == before_unrelated.newest,
			"unregistered device contaminated game incident records");
		result = E_INVALIDARG;
		require(device()->CreateTexture2D(&descriptor, nullptr, reinterpret_cast<ID3D11Texture2D**>(1)) == E_INVALIDARG,
			"argument-validation HRESULT changed");
		result = DXGI_ERROR_DEVICE_REMOVED;
		require(device()->CreateTexture2D(&descriptor, nullptr, reinterpret_cast<ID3D11Texture2D**>(1)) == DXGI_ERROR_DEVICE_REMOVED,
			"failure result changed or failure output was dereferenced");
		td::snapshot snapshot;
		td::capture(snapshot);
		require(snapshot.first_failure_present && snapshot.first_failure.result == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_REMOVED) &&
			snapshot.first_failure.generation == 7 && snapshot.first_failure.width == 1024 && snapshot.first_failure.height == 512 &&
			snapshot.first_failure.descriptor_valid && snapshot.first_failure.caller && !snapshot.first_failure.texture,
			"first failure lost descriptor/caller/generation identity");
		result = S_OK;
		for (unsigned index{}; index < 180; ++index) device()->CreateTexture2D(&descriptor, nullptr, &texture);
		td::capture(snapshot);
		require(snapshot.recent_count <= td::capacity && snapshot.first_failure_present &&
			snapshot.first_failure.result == static_cast<std::uint32_t>(DXGI_ERROR_DEVICE_REMOVED), "ring wrap discarded first failure");
	}

	void warp_checks()
	{
		Microsoft::WRL::ComPtr<ID3D11Device> warp;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
		D3D_FEATURE_LEVEL level{};
		require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, &warp, &level, &context)), "WARP device unavailable");
		auto descriptor = description();
		descriptor.Width = 64; descriptor.Height = 48;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> baseline, observed;
		const auto before = warp->CreateTexture2D(&descriptor, nullptr, &baseline);
		require(td::install(warp.Get(), 24), "real WARP observer not installed");
		const auto after = warp->CreateTexture2D(&descriptor, nullptr, &observed);
		require(before == after && SUCCEEDED(after) && observed, "WARP API result changed");
		D3D11_TEXTURE2D_DESC actual{};
		observed->GetDesc(&actual);
		require(actual.Width == 64 && actual.Height == 48 && actual.Format == descriptor.Format, "WARP resource descriptor changed");
		td::snapshot snapshot;
		td::capture(snapshot);
		const auto& last = snapshot.recent[snapshot.recent_count - 1];
		require(last.device == reinterpret_cast<std::uintptr_t>(warp.Get()) && last.generation == 24 &&
			last.texture == reinterpret_cast<std::uintptr_t>(observed.Get()) && last.width == 64,
			"real WARP creation was not recorded");
	}

	void dump_checks(D3D11_TEXTURE2D_DESC& descriptor, const std::filesystem::path& path)
	{
		entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		leave = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		require(entered && leave, "cannot create blocked-call fixture");
		blocking = true;
		std::atomic<DWORD> worker_id{};
		std::thread worker([&]
		{
			worker_id = GetCurrentThreadId();
			ID3D11Texture2D* texture{};
			device()->CreateTexture2D(&descriptor, nullptr, &texture);
		});
		require(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0, "original call did not enter");
		td::snapshot snapshot;
		td::capture(snapshot);
		require(snapshot.active_count == 1 && snapshot.active[0].width == 1024 && snapshot.active[0].thread == worker_id,
			"blocked texture call is not retained independently of the recent ring");
		const bool written = exception::write_process_minidump(path.string(), worker_id);
		SetEvent(leave);
		worker.join();
		blocking = false;
		CloseHandle(entered); CloseHandle(leave);
		require(written, "supplement dump failed");
		std::ifstream input(path, std::ios::binary);
		const std::vector<char> bytes(std::istreambuf_iterator<char>(input), {});
		MINIDUMP_DIRECTORY* directory{}; void* data{}; ULONG size{};
		require(MiniDumpReadDumpStream(const_cast<char*>(bytes.data()), td::stream_type, &directory, &data, &size) && size == sizeof(td::snapshot),
			"texture evidence absent from direct/emergency dump writer");
		const auto& saved = *static_cast<const td::snapshot*>(data);
		require(saved.active_count == 1 && saved.first_failure_present && saved.first_failure.width == 1024,
			"dump supplement lost in-flight/first-failure state");
		td::capture(snapshot);
		require(!snapshot.active_count, "completed texture call left a stale active slot");
	}

	void concurrent_checks(D3D11_TEXTURE2D_DESC& descriptor)
	{
		std::atomic<unsigned> finished{};
		std::vector<std::thread> writers;
		for (unsigned thread{}; thread < 4; ++thread) writers.emplace_back([&]
		{
			for (unsigned index{}; index < 300; ++index)
			{
				ID3D11Texture2D* texture{};
				device()->CreateTexture2D(&descriptor, nullptr, &texture);
			}
			++finished;
		});
		bool consistent = true;
		td::snapshot snapshot;
		while (finished.load() < 4)
		{
			td::capture(snapshot);
			for (std::size_t index{}; index < snapshot.recent_count; ++index)
			{
				const auto& value = snapshot.recent[index];
				if (value.descriptor_valid && value.device == reinterpret_cast<std::uintptr_t>(device()))
					consistent &= value.width == 1024 && value.height == 512 && value.generation == 7;
				if (index) consistent &= snapshot.recent[index - 1].sequence < value.sequence;
			}
		}
		for (auto& writer : writers) writer.join();
		require(consistent, "concurrent capture observed mixed record fields");
		{
			td::cpu_scope scope(td::phase::execute_begin, td::phase::execute_end, 7, 123, 1, nullptr, nullptr, nullptr);
			td::capture(snapshot);
			require(snapshot.active_count == 1 && snapshot.active[0].pair == 123, "CPU submission not retained as active");
			scope.complete();
		}
		td::capture(snapshot);
		require(!snapshot.active_count && snapshot.first_failure_present, "CPU submission failed to retire its diagnostic slot");
		{
			std::array<std::optional<td::cpu_scope>, td::active_capacity + 1> scopes;
			for (auto& scope : scopes) scope.emplace(td::phase::execute_begin, td::phase::execute_end,
				7, 124, 0, nullptr, nullptr, nullptr);
			td::capture(snapshot);
			require(snapshot.active_count == td::active_capacity && snapshot.active_overflow,
				"active record capacity was exceeded without reporting overflow");
		}
		td::capture(snapshot);
		require(!snapshot.active_count, "abandoned diagnostic scopes left active entries");
		require(td::format_recent(4).find("gpu_retire_proof=none") != std::string::npos, "CPU evidence mislabeled as GPU completion");
	}
}

int wmain(int argc, wchar_t** argv)
{
	try
	{
		auto descriptor = description();
		mock_checks(descriptor);
		warp_checks();
		wchar_t temporary[MAX_PATH]{};
		if (!GetTempPathW(MAX_PATH, temporary)) return 2;
		const auto path = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(temporary) /
			(L"overlord-texture-evidence-" + std::to_wstring(GetCurrentProcessId()) + L".dmp");
		dump_checks(descriptor, path);
		concurrent_checks(descriptor);
		if (argc == 1) std::filesystem::remove(path);
		std::printf("PASS: texture diagnostic checks=%u\n", checks);
		return 0;
	}
	catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}
