#include <std_include.hpp>
#include "engine_scene_job_capture.hpp"
#include "debug_options.hpp"
#include "loader/target_identity.hpp"
#include "component/console.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::engine_scene_job_capture
{
	namespace
	{
		constexpr std::uintptr_t wait_function = 0x140793DE0;
		constexpr std::uintptr_t producer_function = 0x14077FB10;
		constexpr std::uintptr_t backend_frontend = 0x151A7F3B0;
		constexpr std::uintptr_t producer_frontend = 0x150F91188;
		using wait_fn = std::uintptr_t(*)(void*, std::uint32_t);
		using producer_fn = std::uintptr_t(*)(void*, void*, void*, void*, void*);

		template<region_capture::phase Point>
		std::uintptr_t wait_stub(void* predicate, std::uint32_t process_jobs)
		{
			// Preserve the native predicate and job-pumping wait in its original
			// position. In particular, do not move later waits before GPU setup.
			if (!region_capture::enabled())
				return reinterpret_cast<wait_fn>(wait_function)(predicate, process_jobs);
			const auto frontend = *reinterpret_cast<const std::uintptr_t*>(backend_frontend);
			region_capture::phase_scope timing(Point, predicate, frontend);
			checkpoint(nullptr, frontend, Point);
			const auto result = reinterpret_cast<wait_fn>(wait_function)(predicate, process_jobs);
			timing.finish(result);
			checkpoint(nullptr, frontend, Point);
			return result;
		}

		std::uintptr_t producer_stub(void* record, void* scene, void* camera,
			void* descriptors, void* headers)
		{
			if (!region_capture::enabled())
				return reinterpret_cast<producer_fn>(producer_function)(record, scene, camera, descriptors, headers);
			const auto frontend = *reinterpret_cast<const std::uintptr_t*>(producer_frontend);
			region_capture::phase_scope timing(region_capture::phase::surface_producer, record, frontend);
			checkpoint(record, frontend, region_capture::phase::surface_producer);
			// Five pointer arguments recovered from 0x140778CF2, including the
			// caller's fifth stack argument. Preserve RAX even though caller ignores it.
			const auto result = reinterpret_cast<producer_fn>(producer_function)(record, scene, camera, descriptors, headers);
			timing.finish(result);
			checkpoint(record, frontend, region_capture::phase::surface_producer);
			return result;
		}
	}

	void checkpoint(const void* record, std::uintptr_t frontend,
		region_capture::phase point, std::uint64_t family) noexcept
	{
		if (!region_capture::enabled()) return;
		region_capture::row value{0, 0, static_cast<std::uint64_t>(region_capture::kind::job_state),
			reinterpret_cast<std::uintptr_t>(record), static_cast<std::uint64_t>(point), frontend};
		if (frontend)
		{
			const auto* flags = reinterpret_cast<const std::uint8_t*>(frontend + 0x541BE8);
			value.b = region_capture::read<std::uint32_t>(flags) |
				(std::uint64_t{region_capture::read<std::uint32_t>(flags + 4)} << 32);
			value.c = region_capture::read<std::uint32_t>(flags + 8) |
				(std::uint64_t{region_capture::read<std::uint32_t>(flags + 12)} << 32);
		}
		if (record)
		{
			const auto* header = static_cast<const std::uint8_t*>(record) + 0x2FC8 + 24 * 0x130 + 0x100;
			value.d = region_capture::read<std::uint32_t>(header);
			value.e = region_capture::read<std::uint64_t>(header + 8);
		}
		value.f = family;
		region_capture::emit(value);
	}

	void install()
	{
		if (!debug_options::enabled(debug_options::probe::perf)) return;
		if (!target_identity::get().compatibility_probe_passed)
			throw std::runtime_error("scene job capture requires the verified H2 image");
		const std::array<std::uintptr_t, 4> sites{0x1407A7E14, 0x1407A8D05, 0x1407A7FEC, 0x140778CF2};
		const std::array<std::uintptr_t, 4> targets{wait_function, wait_function, wait_function, producer_function};
		const std::array<void*, 4> replacements{
			reinterpret_cast<void*>(wait_stub<region_capture::phase::wait_initial>),
			reinterpret_cast<void*>(wait_stub<region_capture::phase::wait_surfaces>),
			reinterpret_cast<void*>(wait_stub<region_capture::phase::wait_fx>),
			reinterpret_cast<void*>(producer_stub)};
		// Validate all original call destinations before the first write. No
		// detours of entire job dispatcher functions, no runtime hook reloading.
		for (std::size_t i{}; i < sites.size(); ++i)
		{
			std::array<std::uint8_t, 5> bytes{0xE8}, mask{0xFF,0xFF,0xFF,0xFF,0xFF};
			const auto displacement = static_cast<std::int32_t>(targets[i] - (sites[i] + 5));
			std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
			if (!utils::hook_validation::validate_executable_target(reinterpret_cast<void*>(targets[i])) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(sites[i]),
					{bytes.data(), mask.data(), bytes.size()}) ||
				utils::hook::is_relatively_far(reinterpret_cast<void*>(sites[i]), replacements[i]))
				throw std::runtime_error("scene job capture call-site validation failed");
		}
		for (std::size_t i{}; i < sites.size(); ++i) utils::hook::call(sites[i], replacements[i]);
		console::info("[VR] scene job capture boundaries installed; recording OFF until vr_perfStart\n");
	}
}
