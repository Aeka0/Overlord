#pragma once

#include "../debug_options.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <ostream>
#include <windows.h>

namespace vr::diagnostics::wait_timing
{
	// These are observation sites, not new synchronization domains. Nested
	// measurements are inclusive and must never be added together as frame time.
	enum class site : std::size_t
	{
		frame_maintenance,
		facade_enabled, facade_poll, facade_pre, facade_post, facade_other,
		dispatch_enabled, dispatch_poll, dispatch_pre, dispatch_post, dispatch_other,
		xr_prepare, xr_enabled, xr_pre, xr_post, xr_other,
		vr_prepare, vr_enabled, vr_pre, vr_post, vr_other,
		hands_solver, reload_tick, reload_current, reload_publish, reload_other,
		native_copy, composition, desktop_present, xr_wait_frame, xr_wait_image, vr_wait_poses,
		count
	};
	inline constexpr std::array names{
		"renderer.maintenance",
		"runtime.facade.enabled", "runtime.facade.poll", "runtime.facade.pre", "runtime.facade.post", "runtime.facade.other",
		"runtime.dispatch.enabled", "runtime.dispatch.poll", "runtime.dispatch.pre", "runtime.dispatch.post", "runtime.dispatch.other",
		"openxr.prepare", "openxr.enabled", "openxr.pre", "openxr.post", "openxr.other",
		"openvr.prepare", "openvr.enabled", "openvr.pre", "openvr.post", "openvr.other",
		"hands.solver", "reload.tick", "reload.current", "reload.publish", "reload.other",
		"native.copy", "eye.composition", "dxgi.present", "xrWaitFrame", "xrWaitSwapchainImage", "WaitGetPoses"};
	static_assert(names.size() == static_cast<std::size_t>(site::count));

	enum class identity : std::uint8_t { none, renderer_frame, present_frame, skeleton, game_time, stereo_pair };
	inline constexpr std::array identity_names{"none", "renderer_frame", "present_frame", "skeleton", "game_time", "stereo_pair"};
	struct context
	{
		identity kind{};
		std::uint64_t id{}, object{}, detail{};
	};
	inline thread_local context current_context;
	[[nodiscard]] inline bool enabled() noexcept { return debug_options::enabled(debug_options::probe::waits); }

	class context_scope
	{
		bool active_{enabled()};
		context previous_{};
	public:
		explicit context_scope(context value) noexcept
		{
			if (active_) { previous_ = current_context; current_context = value; }
		}
		~context_scope() { if (active_) current_context = previous_; }
		context_scope(const context_scope&) = delete;
		context_scope& operator=(const context_scope&) = delete;
	};

	struct sample
	{
		std::uint64_t started_ns{}, wait_ns{}, held_ns{};
		std::uint32_t thread{};
		context owner{};
		bool locking{};
	};
	inline constexpr std::size_t recent_capacity = 8;
	struct snapshot
	{
		std::uint64_t count{}, wait_total_ns{}, held_total_ns{}, waits_100us{}, waits_1ms{}, holds_1ms{};
		sample latest{}, max_wait{}, max_hold{};
		std::array<sample, recent_capacity> recent{};
		std::size_t next{}, retained{};
	};

	// Reuse the bounded, nonblocking publication contract of screen_display.
	// No producer waits for diagnostics (including a concurrently formatted
	// report); contention drops an observation and is visible in the report.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // Intentional cache-line padding separates hot observation sites.
#endif
	class alignas(64) bucket
	{
		std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
		std::atomic_uint64_t dropped_{};
		snapshot data_;
	public:
		void record(const sample& value) noexcept
		{
			if (busy_.test_and_set(std::memory_order_acquire))
			{
				dropped_.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			++data_.count;
			data_.wait_total_ns += value.wait_ns;
			data_.held_total_ns += value.held_ns;
			data_.waits_100us += value.wait_ns >= 100'000;
			data_.waits_1ms += value.wait_ns >= 1'000'000;
			data_.holds_1ms += value.held_ns >= 1'000'000;
			data_.latest = value;
			if (data_.count == 1 || value.wait_ns > data_.max_wait.wait_ns) data_.max_wait = value;
			if (data_.count == 1 || value.held_ns > data_.max_hold.held_ns) data_.max_hold = value;
			if (value.wait_ns >= 100'000 || value.held_ns >= 1'000'000)
			{
				data_.recent[data_.next] = value;
				data_.next = (data_.next + 1) % recent_capacity;
				if (data_.retained < recent_capacity) ++data_.retained;
			}
			busy_.clear(std::memory_order_release);
		}
		[[nodiscard]] bool read(snapshot& output) noexcept
		{
			if (busy_.test_and_set(std::memory_order_acquire)) return false;
			output = data_;
			busy_.clear(std::memory_order_release);
			return true;
		}
		[[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	inline std::array<bucket, names.size()> observations;

	struct system_clock
	{
		static std::uint64_t now() noexcept
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		}
		static std::uint32_t thread() noexcept { return GetCurrentThreadId(); }
	};

	// Clock injection keeps the disabled path, exact interval boundaries and
	// publish-after-unlock behavior testable without sleeps or a live game.
	template<class Clock = system_clock>
	class measurement
	{
		site site_;
		std::optional<sample> value_;
	public:
		explicit measurement(site location, bool locking) noexcept : site_(location)
		{
			if (enabled())
			{
				value_.emplace();
				value_->thread = Clock::thread();
				value_->owner = current_context;
				value_->locking = locking;
				value_->started_ns = Clock::now();
			}
		}
		void acquired() noexcept
		{
			if (value_) value_->wait_ns = Clock::now() - value_->started_ns;
		}
		void finish() noexcept
		{
			if (value_) value_->held_ns = Clock::now() - value_->started_ns - value_->wait_ns;
		}
		void publish() noexcept
		{
			if (value_) observations[static_cast<std::size_t>(site_)].record(*value_);
		}
	};

	template<class Mutex, class Clock = system_clock>
	class scoped_lock
	{
		Mutex& mutex_;
		measurement<Clock> timing_;
	public:
		scoped_lock(Mutex& mutex, site location) : mutex_(mutex), timing_(location, true)
		{
			mutex_.lock(); // Exactly the original blocking acquisition; no try-lock/polling.
			timing_.acquired();
		}
		~scoped_lock()
		{
			timing_.finish();
			mutex_.unlock();
			timing_.publish();
		}
		scoped_lock(const scoped_lock&) = delete;
		scoped_lock& operator=(const scoped_lock&) = delete;
	};
	template<class Mutex> scoped_lock(Mutex&, site) -> scoped_lock<Mutex>;

	class scope
	{
		measurement<> timing_;
	public:
		explicit scope(site location) noexcept : timing_(location, false) {}
		~scope() { timing_.finish(); timing_.publish(); }
		scope(const scope&) = delete;
		scope& operator=(const scope&) = delete;
	};

	inline void append(std::ostream& out)
	{
		out << "CPU wait timing: enabled=" << (enabled() ? "yes" : "no") << '\n';
		if (!enabled()) return;
		out << "  units=us clock=steady wall_time_only=yes nested_intervals=inclusive_do_not_sum"
			" interval=process_lifetime completed_samples_only=yes\n"
			"  wait=lock_acquisition_wall_time hold=locked_body_or_call_wall_time"
			" recent_threshold=wait_100us_or_hold_1ms recent_capacity_per_site=" << recent_capacity
			<< " context_ids=separate_domains_not_interchangeable"
			" present_detail=sync_interval skeleton_id=timestamp pair_detail=eye\n";
		const auto append_sample = [&](const char* label, const sample& value)
		{
			out << "    " << label << " start_us=" << value.started_ns / 1000 << " tid=" << value.thread
				<< " context=" << identity_names[static_cast<std::size_t>(value.owner.kind)]
				<< " id=" << value.owner.id << " object=" << value.owner.object << " detail=" << value.owner.detail
				<< " wait_us=" << value.wait_ns / 1000 << " hold_us=" << value.held_ns / 1000 << '\n';
		};
		for (std::size_t i{}; i < observations.size(); ++i)
		{
			snapshot value;
			const auto ready = observations[i].read(value);
			out << "  " << names[i] << " snapshot_busy=" << !ready << " dropped=" << observations[i].dropped();
			if (!ready) { out << '\n'; continue; }
			out << " samples=" << value.count;
			if (!value.count) { out << '\n'; continue; }
			out << " type=" << (value.latest.locking ? "lock" : "call")
				<< " wait_us(total/avg/max)=" << value.wait_total_ns / 1000 << '/'
				<< value.wait_total_ns / value.count / 1000 << '/' << value.max_wait.wait_ns / 1000
				<< " hold_us(total/avg/max)=" << value.held_total_ns / 1000 << '/'
				<< value.held_total_ns / value.count / 1000 << '/' << value.max_hold.held_ns / 1000
				<< " wait_ge_100us=" << value.waits_100us << " wait_ge_1ms=" << value.waits_1ms
				<< " hold_ge_1ms=" << value.holds_1ms << '\n';
			append_sample("latest", value.latest);
			append_sample("max_wait", value.max_wait);
			append_sample("max_hold", value.max_hold);
			for (std::size_t n{}; n < value.retained; ++n)
				append_sample("recent", value.recent[(value.next + recent_capacity - value.retained + n) % recent_capacity]);
		}
	}
}
