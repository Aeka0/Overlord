#include <std_include.hpp>
#include "component/vr/native_render_contract.hpp"
#include "engine_scene_resolution.hpp"
#include "engine_backend_probe.hpp"
#include "component/console.hpp"
#include "loader/target_identity.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

#include <atomic>
#include <mutex>
#include <wrl/client.h>

namespace vr::engine_scene_resolution
{
	namespace
	{
		constexpr std::uintptr_t calculate_scene = 0x14074F670;
		constexpr std::uintptr_t check_video_changes = 0x1403D3C10;
		constexpr std::uintptr_t request_soft_resize = 0x1403D6B50;
		constexpr std::uintptr_t video_config_address = 0x14EEE0CF0;
		std::atomic<phase> state{phase::awaiting_runtime};
		std::atomic_uint64_t requested{};
		std::mutex status_mutex;
		report status;

		constexpr std::uint64_t pack(const extent value) noexcept
		{
			return (std::uint64_t{value.width} << 32) | value.height;
		}
		constexpr extent unpack(const std::uint64_t value) noexcept
		{
			return {static_cast<std::uint32_t>(value >> 32), static_cast<std::uint32_t>(value)};
		}

		void fail(const char* const error)
		{
			{
				const std::lock_guard lock(status_mutex);
				status.error = error;
				state.store(phase::failed, std::memory_order_release);
			}
			console::error("[VR] native scene resolution rejected: %s\n", error);
		}

		void calculate_scene_stub(window_parameters* const parameters)
		{
			reinterpret_cast<void(*)(window_parameters*)>(calculate_scene)(parameters);
			const auto desired = unpack(requested.load(std::memory_order_acquire));
			if (!valid(desired)) return; // No runtime yet; original H2 state only, never submitted.
			if (!apply_scene_extent(*parameters, desired))
			{
				{
					const std::lock_guard lock(status_mutex);
					status.supersample_count = parameters->supersample_count;
				}
				fail("H2 scene dimensions rejected; native VR requires a valid display and H2 supersample_count=1");
			}
		}

		bool describe_target(const std::uint32_t id, const std::size_t view_offset,
			extent& size, std::uint32_t& samples)
		{
			ID3D11View* view{};
			std::memcpy(&view, reinterpret_cast<const void*>(
				native_render_contract::target_registry_base +
				id * native_render_contract::target_registry_stride + view_offset), sizeof(view));
			if (!view) return false;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			view->GetResource(&resource);
			if (!resource || FAILED(resource.As(&texture))) return false;
			D3D11_TEXTURE2D_DESC description{};
			texture->GetDesc(&description);
			size = {description.Width, description.Height};
			samples = description.SampleDesc.Count;
			return valid(size);
		}

		void check_video_changes_stub()
		{
			// This is the original H2 main-loop call, not a renderer callback or a
			// command executed inside Present. H2 drains the render thread and takes
			// its GPU mutex before destroying/recreating all dependent targets.
			auto expected = phase::queued;
			const bool applying = state.compare_exchange_strong(expected, phase::rebuilding,
				std::memory_order_acq_rel);
			if (applying) reinterpret_cast<void(*)()>(request_soft_resize)();
			reinterpret_cast<void(*)()>(check_video_changes)();
			if (!applying || state.load(std::memory_order_acquire) == phase::failed) return;

			report observed;
			observed.requested = unpack(requested.load(std::memory_order_acquire));
			observed.apply_thread = GetCurrentThreadId();
			std::memcpy(&observed.config, reinterpret_cast<const void*>(video_config_address),
				sizeof(observed.config));
			std::uint32_t desktop_samples{};
			const bool targets = describe_target(4, 8, observed.color, observed.color_samples) &&
				describe_target(4, 0x10, observed.depth, observed.depth_samples) &&
				describe_target(1, 8, observed.desktop, desktop_samples);
			const bool matched = targets && matches_scene(observed.config, observed.requested) &&
				observed.color == observed.requested && observed.depth == observed.requested &&
				observed.desktop == observed.config.display &&
				observed.color_samples == 1 && observed.depth_samples == 1;
			{
				const std::lock_guard lock(status_mutex);
				observed.hooks_installed = status.hooks_installed;
				observed.supersample_count = 1;
				observed.rebuilds = status.rebuilds + 1;
				status = observed;
			}
			if (!matched)
			{
				fail("H2 coordinated rebuild did not produce matching native scene/color/depth and independent desktop targets");
				return;
			}
			state.store(phase::ready, std::memory_order_release);
			console::info("[VR] native eye scene ready: scene=%ux%u color=%ux%u depth=%ux%u desktop=%ux%u; no submit upscaling\n",
				observed.config.scene.width, observed.config.scene.height,
				observed.color.width, observed.color.height, observed.depth.width, observed.depth.height,
				observed.desktop.width, observed.desktop.height);
		}

		template<std::size_t N>
		void require_bytes(const std::uintptr_t address, const std::array<std::uint8_t, N>& bytes)
		{
			std::array<std::uint8_t, N> mask;
			mask.fill(0xFF);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<const void*>(address),
				{bytes.data(), mask.data(), N}) ||
				!utils::hook_validation::validate_executable_target(reinterpret_cast<const void*>(address)))
				throw std::runtime_error("native scene resolution H2 ABI fingerprint mismatch");
		}
	}

	void install()
	{
		if (!target_identity::get().compatibility_probe_passed)
			throw std::runtime_error("native scene resolution requires the verified H2 image");
		// All three natural calculation callers, plus the main-loop update call.
		const std::array<std::uintptr_t, 4> sites{0x14074C998, 0x14074DA24, 0x14074F928, 0x1405A3B02};
		const std::array<std::uintptr_t, 4> targets{calculate_scene, calculate_scene, calculate_scene, check_video_changes};
		const std::array<void*, 4> replacements{reinterpret_cast<void*>(calculate_scene_stub),
			reinterpret_cast<void*>(calculate_scene_stub), reinterpret_cast<void*>(calculate_scene_stub),
			reinterpret_cast<void*>(check_video_changes_stub)};
		// Validate the entire transaction before writing any code. These sites are
		// installed once before H2 starts threads; no live-code reload path exists.
		for (std::size_t index{}; index < sites.size(); ++index)
		{
			std::array<std::uint8_t, 5> bytes{0xE8};
			const auto displacement = static_cast<std::int32_t>(targets[index] - (sites[index] + 5));
			std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
			require_bytes(sites[index], bytes);
			if (utils::hook::is_relatively_far(reinterpret_cast<void*>(sites[index]), replacements[index]))
				throw std::runtime_error("native scene resolution hook outside rel32 range");
		}
		require_bytes(calculate_scene, std::array<std::uint8_t, 6>{0x40,0x53,0x48,0x83,0xEC,0x30});
		require_bytes(request_soft_resize, std::array<std::uint8_t, 11>{0xC7,0x05,0xCA,0x16,0x92,0x0C,0x01,0,0,0,0xC3});
		require_bytes(0x1407502D0, std::array<std::uint8_t, 13>{0x48,0x83,0xEC,0x28,0x8B,0x41,0x28,0x89,0x05,0x13,0x0A,0x79,0x0E});
		for (std::size_t index{}; index < sites.size(); ++index)
			utils::hook::call(sites[index], replacements[index]);
		const std::lock_guard lock(status_mutex);
		status.hooks_installed = true;
	}

	bool request(const extent desired, std::string& error)
	{
		const std::lock_guard lock(status_mutex);
		if (!status.hooks_installed || !valid(desired))
		{
			error = "native H2 resolution hooks unavailable or SteamVR eye extent outside the engine/D3D11 limit";
			return false;
		}
		const auto previous = requested.load(std::memory_order_acquire);
		if (previous != 0 && previous != pack(desired))
		{
			// Never resize an in-flight ownership ring during runtime reconnection.
			// A different recommendation requires a fresh process/resource lifetime.
			error = "SteamVR eye resolution changed; restart H2 to create a new native target lifetime";
			return false;
		}
		if (state.load(std::memory_order_acquire) == phase::failed)
		{
			error = status.error;
			return false;
		}
		if (previous == 0)
		{
			status.requested = desired;
			requested.store(pack(desired), std::memory_order_release);
			state.store(phase::queued, std::memory_order_release);
		}
		return true;
	}

	bool ready() noexcept { return state.load(std::memory_order_acquire) == phase::ready; }
	bool accepts_source(const extent actual) noexcept
	{
		return ready() && pack(actual) == requested.load(std::memory_order_acquire);
	}
	report get_report()
	{
		const std::lock_guard lock(status_mutex);
		auto result = status;
		result.state = state.load(std::memory_order_acquire);
		return result;
	}
	const char* to_string(const phase value) noexcept
	{
		switch (value)
		{
		case phase::awaiting_runtime: return "awaiting_runtime";
		case phase::queued: return "queued";
		case phase::rebuilding: return "rebuilding";
		case phase::ready: return "ready";
		case phase::failed: return "failed";
		default: return "unknown";
		}
	}
}
