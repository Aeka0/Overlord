#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace vr::region_capture
{
	// CPU-only telemetry. No D3D calls, allocation, file I/O or waiting in hooks.
	enum class kind : std::uint64_t
	{
		trace = 1, frontend_begin, frontend_end, owner_begin, owner_end,
		list, camera, failure, mark, heartbeat, stopped,
		phase_begin, phase_end, job_state,
		rigid_model, rigid_context, rigid_placement,
		surface_budget,
		surface_layout,
	};
	enum class phase : std::uint64_t
	{
		backend = 1, claim, clone, admission, scene_completion, scene_publication,
		wait_initial = 10, wait_surfaces, wait_fx, surface_producer = 20,
	};
	struct row
	{
		std::uint64_t tick{}, thread{}, type{}, pair{}, eye{}, a{}, b{}, c{}, d{}, e{}, f{};
	};
	static_assert(sizeof(row) == 88);
	using sink_fn = void(*)(row) noexcept;
	inline std::atomic<sink_fn> sink{};
	inline bool enabled() noexcept { return sink.load(std::memory_order_relaxed) != nullptr; }
	inline void emit(row value) noexcept
	{
		if (const auto target = sink.load(std::memory_order_acquire)) target(value);
	}
	inline void event(kind type, std::uint64_t pair = 0, std::uint64_t eye = 0,
		std::uint64_t a = 0, std::uint64_t b = 0) noexcept
	{
		emit({0, 0, static_cast<std::uint64_t>(type), pair, eye, a, b});
	}
	// Phase rows use pair=record identity, eye=phase, a=frontend identity,
	// b=end result (claim publication / clone or admission boolean). They are not
	// stereo pair rows. Explicit finish bounds nested work without extra scopes.
	class phase_scope
	{
		bool active_;
		row value_;
	public:
		phase_scope(phase stage, const void* record = nullptr, std::uintptr_t frontend = 0) noexcept
			: active_(enabled()), value_{0, 0, static_cast<std::uint64_t>(kind::phase_begin),
				reinterpret_cast<std::uintptr_t>(record), static_cast<std::uint64_t>(stage), frontend}
		{ if (active_) emit(value_); }
		phase_scope(const phase_scope&) = delete;
		phase_scope& operator=(const phase_scope&) = delete;
		void finish(std::uint64_t result = 0) noexcept
		{
			if (!active_) return;
			active_ = false;
			value_.type = static_cast<std::uint64_t>(kind::phase_end);
			value_.b = result;
			emit(value_);
		}
		~phase_scope() { finish(); }
	};
	template<class T> T read(const std::uint8_t* p) noexcept
	{
		T value{};
		std::memcpy(&value, p, sizeof(value));
		return value;
	}
	inline std::uint64_t hash(const std::uint8_t* p, std::size_t size) noexcept
	{
		std::uint64_t value = 14695981039346656037ull;
		for (std::size_t i{}; i < size; ++i) value = (value ^ p[i]) * 1099511628211ull;
		return value;
	}
	// Only the owned, fixed-size H2 scene record is read. Never dereference its
	// surface/material pointers. Stage: 0=clone, 1=before owner, 2=after owner.
	inline void snapshot(std::uint64_t pair, std::uint64_t eye, std::uint64_t stage,
		const void* record) noexcept
	{
		if (!enabled() || !record) return;
		const auto* bytes = static_cast<const std::uint8_t*>(record);
		row camera{0, 0, static_cast<std::uint64_t>(kind::camera), pair, eye, stage};
		camera.b = read<std::uint32_t>(bytes + 0x100);
		camera.c = read<std::uint32_t>(bytes + 0x104);
		camera.d = read<std::uint32_t>(bytes + 0x108);
		camera.e = reinterpret_cast<std::uintptr_t>(record);
		emit(camera);
		for (std::size_t index{}; index < 26; ++index)
		{
			const auto* list = bytes + 0x2FC8 + index * 0x130;
			const auto pointer = read<std::uint64_t>(list + 0x90);
			const auto count = read<std::uint32_t>(list + 0x98);
			// Known failing lists always recorded; any other null/nonempty surface
			// range is also retained. This is observation, never an admission gate.
			if (index != 18 && index != 24 && (pointer != 0 || count == 0)) continue;
			emit({0, 0, static_cast<std::uint64_t>(kind::list), pair, eye,
				(stage << 32) | index, pointer, count,
				read<std::uint32_t>(list + 0x100), hash(list, 0x130),
				read<std::uint64_t>(list + 0x108)});
		}
	}
	struct draw_counts { bool active{}; std::uint64_t draws{}, indices{}; };
	inline thread_local draw_counts counts;
	inline void draw_indexed(std::uint32_t indices) noexcept
	{
		if (counts.active) { ++counts.draws; counts.indices += indices; }
	}
	inline void begin_owner(std::uint64_t pair, std::uint64_t eye, const void* record) noexcept
	{
		counts = {enabled(), 0, 0};
		if (!counts.active) return;
		snapshot(pair, eye, 1, record);
		event(kind::owner_begin, pair, eye);
	}
	inline void end_owner(std::uint64_t pair, std::uint64_t eye, const void* record) noexcept
	{
		if (!counts.active) return;
		counts.active = false;
		event(kind::owner_end, pair, eye, counts.draws, counts.indices);
		snapshot(pair, eye, 2, record);
	}
}
