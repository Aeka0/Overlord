#include <std_include.hpp>

// Temporary CPU-only provenance probe. No exported asset, shader replacement,
// GPU queries/readback, render-state writes, or native waits. Release omits it.
#ifdef DEBUG
#include "../engine_stereo_owner_pass.hpp"
#include "../native_flare.hpp"
#include "native_flare_probe.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>
#include <atomic>
#include <cmath>
#include <memory>
#include <sstream>

namespace vr::diagnostics::native_flare_probe
{
	namespace
	{
		constexpr std::size_t samples_per_channel = 64, channels = 3;
		struct sample
		{
			std::uint64_t tick{}, pair{}, publication{};
			std::uint32_t channel{}, thread{}, validity{};
			std::uintptr_t context{}, record{}, data{}, mesh{}, vertex_buffer{}, material{};
			std::uint32_t first_vertex{}, element_count{};
			std::array<char, 128> material_name{};
			std::array<float, 3> light_position{};
			std::array<float, 44> fx_camera{}; // origin, axis at +0x70, tanHalfFov at +0xA8.
			std::array<float, 6> quad_arguments{}; // center, corner vector 1/2 (NDC).
			std::array<float, 48> vertices{}; // Four native 48-byte vertices, unmodified.
			std::array<float, 92> eye_view{}, native_view{}; // 0x170 bytes.
			std::array<float, 640> native_matrix_cache{}; // +0..+0x9FF, post native draw.
			std::array<float, 4> native_eye_constant{};
			std::array<std::uint32_t, 3> first_surface{};
		};
		struct slot { sample value; std::atomic_bool ready{}; };
		struct run
		{
			std::uint64_t begin{}, end{};
			std::array<std::atomic_uint32_t, channels> seen{};
			std::array<slot, channels * samples_per_channel> slots{};
		};
		std::atomic<std::shared_ptr<run>> active;
		std::atomic_uint64_t deadline{};
		std::atomic_bool alive{true};

		// Destructor-free bounded SEH leaf: stale optional diagnostic pointers
		// discard evidence, never interrupt the original engine call.
		bool read_bytes(void* output, std::uintptr_t source, std::size_t size) noexcept
		{
			if (!output || source < 0x10000 || !size || size > 4096 ||
				source > UINTPTR_MAX - size) return false;
			__try { std::memcpy(output, reinterpret_cast<const void*>(source), size); return true; }
			__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
				GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
			{ return false; }
		}
		template<class T> bool read(T& value, std::uintptr_t address) noexcept
		{ return read_bytes(&value, address, sizeof(value)); }

		std::shared_ptr<run> current() noexcept
		{
			if (!deadline.load(std::memory_order_relaxed)) return {};
			auto value = active.load();
			const auto now = GetTickCount64();
			if (!value || now < value->begin || now >= value->end) return {};
			return value;
		}
		slot* reserve(run& value, std::uint32_t channel) noexcept
		{
			// Separate eye budgets avoid aliasing every Nth call to only one eye.
			const auto ordinal = value.seen[channel].fetch_add(1, std::memory_order_relaxed);
			if (ordinal % 4 || ordinal / 4 >= samples_per_channel) return nullptr;
			return &value.slots[channel * samples_per_channel + ordinal / 4];
		}
		void label(sample& value) noexcept
		{
			std::uintptr_t name{};
			if (!read(name, value.material) || !read(value.material_name, name))
				value.material_name.fill(0);
			value.material_name.back() = 0;
		}
		void emit_sample(emit_fn original, void* context, const float* center, const float* corner1, const float* corner2)
		{
			auto capture = current();
			auto* destination = capture ? reserve(*capture, 0) : nullptr;
			if (!destination) { original(context, center, corner1, corner2); return; }
			auto& s = destination->value;
			s.tick = GetTickCount64(); s.thread = GetCurrentThreadId();
			s.context = reinterpret_cast<std::uintptr_t>(context);
			std::uintptr_t camera{}, allocator{}, mapped{}, descriptor{};
			std::uint32_t before_vertices{}, before_elements{}, after_vertices{}, after_elements{}, stride{};
			if (read(s.light_position, s.context + 0xA0) && read(camera, s.context + 0x58) &&
				read(s.fx_camera, camera) && read_bytes(s.quad_arguments.data(), reinterpret_cast<std::uintptr_t>(center), 8) &&
				read_bytes(s.quad_arguments.data() + 2, reinterpret_cast<std::uintptr_t>(corner1), 8) &&
				read_bytes(s.quad_arguments.data() + 4, reinterpret_cast<std::uintptr_t>(corner2), 8))
				s.validity |= 1;
			const bool allocation = read(allocator, s.context + 0x150) &&
				read(mapped, allocator + 0x10) && read(stride, allocator + 0x24) && stride == 48 &&
				read(before_vertices, allocator + 0x28) && before_vertices <= 0x4000 - 4 &&
				read(before_elements, allocator) && before_elements < 1024 &&
				read(descriptor, allocator + 0x40);
			original(context, center, corner1, corner2); // exactly once; no observer lock held.
			if (allocation && read(after_vertices, allocator + 0x28) &&
				after_vertices == before_vertices + 4 && read(after_elements, allocator) &&
				after_elements == before_elements + 1)
			{
				s.mesh = descriptor; s.first_vertex = before_vertices;
				if (descriptor >= 0x540C80) s.data = descriptor - 0x540C80;
				if (read(s.vertices, mapped + std::uintptr_t(before_vertices) * stride)) s.validity |= 2;
				if (read(s.vertex_buffer, descriptor + 0x28)) s.validity |= 4;
				// Exact native 24-byte flare list appended by the observed emission.
				const auto element = 0x151392180ull + std::uintptr_t(before_elements) * 24;
				if (read(s.material, element) && read(s.first_surface, element + 8)) s.validity |= 8;
				label(s);
			}
			destination->ready.store(true, std::memory_order_release);
		}
		void draw_sample(draw_fn original, void* context, void* record, std::uint32_t technique)
		{
			auto capture = current();
			engine_stereo_view::eye_slot eye{};
			if (!capture || !engine_stereo_owner_pass::snapshot_current_eye(record, eye))
			{ original(context, record, technique); return; }
			auto* destination = reserve(*capture, eye.output_eye + 1);
			if (!destination) { original(context, record, technique); return; }
			auto& s = destination->value;
			s.channel = eye.output_eye + 1; s.tick = GetTickCount64(); s.thread = GetCurrentThreadId();
			s.pair = eye.pair_id; s.publication = eye.publication;
			s.record = reinterpret_cast<std::uintptr_t>(record);
			std::memcpy(s.eye_view.data(), eye.bytes.data(), eye.bytes.size());
			std::uintptr_t source{};
			std::array<std::uint16_t, 2> surfaces{};
			if (read(source, reinterpret_cast<std::uintptr_t>(context)) &&
				read(s.native_view, source + 0x2BF0)) s.validity |= 1;
			s.context = source;
			// The draw's own backend arena, not a mutable frontend/latest pointer.
			if (read(s.data, 0x151A7F3B0ull) && s.data && read(surfaces, s.record + 0x2EA4) &&
				surfaces[0] < 4096 && surfaces[1] <= 4096 - surfaces[0])
			{
				s.mesh = s.data + 0x540C80; s.element_count = surfaces[1];
				if (read(s.vertex_buffer, s.mesh + 0x28)) s.validity |= 4;
				if (surfaces[1] && read(s.material, s.data + 0xF89100 + surfaces[0] * 24ull))
				{
					(void)read(s.first_surface, s.data + 0xF89108 + surfaces[0] * 24ull);
					label(s);
				}
			}
			original(context, record, technique); // shader/matrix cache updated by H2 itself.
			if (read(s.native_matrix_cache, source) && read(s.native_eye_constant, source + 0x16F0)) s.validity |= 2;
			destination->ready.store(true, std::memory_order_release);
		}
		template<class T, std::size_t N> void values(std::ostringstream& out, const char* key, const std::array<T, N>& v)
		{
			out << key << '=';
			for (const auto value : v)
			{
				if constexpr (std::is_floating_point_v<T>)
				{ if (!std::isfinite(value)) { out << " nonfinite"; continue; } }
				out << ' ' << value;
			}
			out << '\n';
		}
		void finish(const std::shared_ptr<run>& capture)
		{
			active.store({});
			// In-flight callbacks retain their run. Acquire-ready slots only; a late
			// callback is explicitly omitted, never waited on by the renderer/writer.
			std::ostringstream out;
			out.precision(9);
			out << "version=2 mode=cpu_read_only gpu_contents=not_sampled native_view_offset=0x2BF0\n"
				<< "begin=" << capture->begin << " end=" << capture->end << '\n';
			unsigned completed{};
			for (const auto& slot : capture->slots)
			{
				if (!slot.ready.load(std::memory_order_acquire)) continue;
				const auto& s = slot.value; ++completed;
				out << "sample=" << completed << " channel=" << s.channel << " tick=" << s.tick
					<< " thread=" << s.thread << " pair=" << s.pair << " publication=" << s.publication
					<< " validity=" << s.validity << '\n' << std::hex
					<< "context=" << s.context << " record=" << s.record << " data=" << s.data
					<< " mesh=" << s.mesh << " vb=" << s.vertex_buffer << " material=" << s.material
					<< std::dec << " name=" << s.material_name.data() << '\n'
					<< "first_vertex=" << s.first_vertex << " element_count=" << s.element_count << '\n';
				values(out, "surface", s.first_surface);
				if (!s.channel)
				{
					values(out, "light", s.light_position); values(out, "fx_camera", s.fx_camera);
					values(out, "quad_ndc", s.quad_arguments); values(out, "vertices", s.vertices);
				}
				else
				{
					values(out, "eye_view", s.eye_view); values(out, "native_view", s.native_view);
					values(out, "matrix_cache", s.native_matrix_cache); values(out, "eye_constant", s.native_eye_constant);
				}
			}
			out << "completed=" << completed << '\n';
			const auto path = "minidumps/overlord-flare-sample-" + std::to_string(capture->begin) + ".txt";
			const bool saved = utils::io::write_file_atomic(path, out.str());
			console::info("[VR flare] CPU sample finished: %u records, saved=%d %s\n", completed, saved, path.c_str());
			// Keep the arm gate closed until this run no longer owns publication.
			deadline.store(0, std::memory_order_release);
		}
	}
	void emit(emit_fn original, void* c, const float* p, const float* a, const float* b) { emit_sample(original,c,p,a,b); }
	void draw(draw_fn original, void* c, void* r, unsigned t) { draw_sample(original,c,r,t); }
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			command::add("vr_flareSample", [] {
				if (!native_flare::installed()) { console::error("[VR flare] native signatures rejected\n"); return; }
				if (deadline.load()) { console::info("[VR flare] sample already pending\n"); return; }
				auto capture = std::make_shared<run>();
				capture->begin = GetTickCount64() + 2000; capture->end = capture->begin + 4000;
				active.store(capture); deadline.store(capture->end);
				scheduler::once([capture] {
					if (alive.load()) finish(capture);
				}, scheduler::async, 6500ms);
				console::info("[VR flare] starts in 2s, samples 4s, maximum 192 CPU records; close console and face the light\n");
			});
		}
		void pre_destroy() override
		{
			alive.store(false); deadline.store(0);
			active.store({});
		}
	};
}
REGISTER_COMPONENT(vr::diagnostics::native_flare_probe::component)
#endif
