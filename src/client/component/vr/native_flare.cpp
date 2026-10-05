#include <std_include.hpp>
#include "native_flare.hpp"
#include "native_flare_geometry.hpp"
#include "native_flare_projection.hpp"
#include "diagnostics/native_flare_probe.hpp"
#include "engine_stereo_owner_pass.hpp"
#include "settings.hpp"
#include "game/dvars.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::native_flare
{
	namespace
	{
		constexpr std::uintptr_t emit_site = 0x14042E816, emit_target = 0x140432510;
		constexpr std::uintptr_t draw_site = 0x140295118, draw_target = 0x140294E90;
		using namespace native_flare_geometry;
		bool ready{};
		game::dvar_t* disable_lens_flare{};
		std::atomic_uint64_t disabled_draws{};
		std::atomic_uint64_t emitted{}, allocation_rejected{}, geometry_rejected{}, draw_rejected{}, flat_draws{};
		std::array<std::atomic_uint64_t, 2> eye_draws{};

		bool copy(void* destination, const void* source, std::size_t size) noexcept
		{
			const auto from = reinterpret_cast<std::uintptr_t>(source), to = reinterpret_cast<std::uintptr_t>(destination);
			if (from < 0x10000 || to < 0x10000 || !size || size > 4096 ||
				from > UINTPTR_MAX - size || to > UINTPTR_MAX - size) return false;
			__try { std::memcpy(destination, source, size); return true; }
			__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
				GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
			{ return false; }
		}
		template<class T> bool read(T& output, std::uintptr_t address) noexcept
		{ return copy(&output, reinterpret_cast<const void*>(address), sizeof(output)); }

		void emit_stub(void* context, const float* center, const float* a, const float* b)
		{
			const auto address = reinterpret_cast<std::uintptr_t>(context);
			std::uintptr_t allocator{}, mapped{}, camera_pointer{};
			std::uint32_t before{}, after{}, stride{};
			std::array<float, 44> native_camera{};
			vector light{};
			const bool valid = read(allocator, address + 0x150) && read(mapped, allocator + 0x10) &&
				mapped >= 0x10000 && mapped <= UINTPTR_MAX - 0xC0000 &&
				read(stride, allocator + 0x24) && stride == sizeof(vertex) &&
				read(before, allocator + 0x28) && before <= 0x4000 - 4 &&
				read(camera_pointer, address + 0x58) && read(native_camera, camera_pointer) && read(light, address + 0xA0);
			diagnostics::native_flare_probe::emit(
				reinterpret_cast<diagnostics::native_flare_probe::emit_fn>(emit_target), context, center, a, b);
			if (!valid || !read(after, allocator + 0x28) || after != before + 4)
			{ ++allocation_rejected; return; }
			quad vertices{};
			const auto target = mapped + std::uintptr_t(before)*sizeof(vertex);
			if (!read(vertices, target)) { ++allocation_rejected; return; }
			camera c{};
			std::copy_n(native_camera.begin(), 3, c.origin.begin());
			for (unsigned i = 0; i < 3; ++i) std::copy_n(native_camera.begin() + 28 + 3*i, 3, c.axis[i].begin());
			c.tan_x = native_camera[42]; c.tan_y = native_camera[43];
			if (!to_world(c, light, vertices))
			{
				++geometry_rejected;
				// Invalid authored data must not turn into a giant/NaN world quad.
				// Collapse only this already-allocated flare; keep native bookkeeping.
				for (auto& v : vertices) v.position = {};
			}
			if (copy(reinterpret_cast<void*>(target), vertices.data(), sizeof(vertices))) ++emitted;
			else ++allocation_rejected;
		}

		void draw_stub(void* context, void* record, unsigned technique)
		{
			engine_stereo_view::eye_slot eye{};
			const bool stereo = engine_stereo_owner_pass::snapshot_current_eye(record, eye);
			// Suppress only the VR draw. Keep native emission/bookkeeping intact so
			// the same arena remains valid for desktop and session-loss rendering.
			if (stereo && disable_lens_flare && disable_lens_flare->current.enabled)
			{
				++disabled_draws;
				return;
			}
			// Geometry is world-space for its whole native lifetime, including a
			// desktop/session-loss path. Never reinterpret an in-flight arena by a
			// live toggle. Flat rendering uses this exact record's own view instead.
			if (!stereo && !copy(eye.bytes.data(), record, eye.bytes.size())) { ++draw_rejected; return; }
			std::uintptr_t source_address{};
			std::uint32_t mode{};
			std::array<float, 16> world{};
			std::array<float, 4> eye_offset{};
			std::array<float, 64> relative{}, projected{};
			vector origin{};
			std::memcpy(relative.data(), eye.bytes.data(), sizeof(relative));
			std::memcpy(origin.data(), eye.bytes.data() + 0x100, sizeof(origin));
			if (technique != 8 || !read(source_address, reinterpret_cast<std::uintptr_t>(context)) ||
				!read(mode, source_address + 0x3298) || mode != 4 ||
				!read(world, source_address + 0x900) || !read(eye_offset, source_address + 0x16F0) ||
				!absolute_matrices(relative, origin, projected)) { ++draw_rejected; return; }
			for (unsigned i = 0; i < 16; ++i)
				if (world[i] != (i % 5 == 0 ? 1.f : 0.f)) { ++draw_rejected; return; }
			for (unsigned i = 0; i < 3; ++i)
				if (eye_offset[i] != 0.f) { ++draw_rejected; return; }
			std::array<float,64> saved{};
			std::uint8_t flags{};
			if (!read(saved, source_address + 0x2BF0) || !read(flags, source_address + 0x3340))
			{ ++draw_rejected; return; }
			// Source belongs to the current native flare call; only four matrices
			// and its projection-input versions are scoped. No scene/rebase/history,
			// shared dynamic buffer, device resource or upload lifecycle is altered.
			const projection_scope scope(reinterpret_cast<std::byte*>(source_address), saved, projected, flags);
			diagnostics::native_flare_probe::draw(
				reinterpret_cast<diagnostics::native_flare_probe::draw_fn>(draw_target), context, record, technique);
			if (stereo) ++eye_draws[eye.output_eye]; else ++flat_draws;
		}
		template<std::size_t N> bool verify(std::uintptr_t address, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t, N> mask{}; mask.fill(0xff);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address), {bytes, mask.data(), N}));
		}
	}
	bool installed() noexcept { return ready; }
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			disable_lens_flare = dvars::register_bool(settings::disable_lens_flare, false, game::DVAR_FLAG_SAVED,
				"Disable native lens flare draws in VR eyes; preserve non-VR rendering");
			constexpr std::uint8_t emit[]{0xE8,0xF5,0x3C,0x00,0x00};
			constexpr std::uint8_t draw[]{0xE8,0x73,0xFD,0xFF,0xFF};
			constexpr std::uint8_t matrix[]{0x0F,0x10,0x81,0x70,0x2C,0x00,0x00};
			constexpr std::uint8_t allocator[]{0x48,0x8B,0x43,0x30,0x33,0xD2};
			if (verify(emit_site, emit) && verify(draw_site, draw) &&
				verify(0x140786FA8, matrix) && verify(0x1402AB485, allocator))
			{
				utils::hook::call(emit_site, emit_stub);
				utils::hook::call(draw_site, draw_stub);
				ready = true;
			}
			else console::error("[VR flare] native signatures rejected; world-quad path not installed\n");
			command::add("vr_flare_status", [] {
				console::info("[VR flare] installed=%d world_quads=%llu allocation_rejected=%llu geometry_rejected=%llu "
					"eyes=%llu/%llu flat=%llu draw_rejected=%llu disabled_draws=%llu\n", ready, emitted.load(), allocation_rejected.load(),
					geometry_rejected.load(), eye_draws[0].load(), eye_draws[1].load(), flat_draws.load(), draw_rejected.load(),
					disabled_draws.load());
			});
		}
	};
}
REGISTER_COMPONENT(vr::native_flare::component)
