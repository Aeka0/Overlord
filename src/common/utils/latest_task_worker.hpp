#pragma once

#include <condition_variable>
#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace utils
{
	// One running task and at most one pending task. A newer intent replaces the
	// pending task; the owner supplies cooperative cancellation for running work.
	class latest_task_worker final
	{
	public:
		using task = std::function<void()>;
		using failure_handler = std::function<void(std::exception_ptr)>;

		~latest_task_worker() { stop(); }
		latest_task_worker() = default;
		latest_task_worker(const latest_task_worker&) = delete;
		latest_task_worker& operator=(const latest_task_worker&) = delete;

		bool replace(task work, failure_handler failed)
		{
			if (!work || !failed) return false;
			const std::lock_guard lock(mutex_);
			if (stopping_) return false;
			if (!thread_.joinable())
			{
				thread_ = std::thread([this] { run(); });
				worker_id_ = thread_.get_id();
			}
			pending_ = {std::move(work), std::move(failed)};
			ready_.notify_one();
			return true;
		}

		void cancel_pending()
		{
			const std::lock_guard lock(mutex_);
			pending_ = {};
		}
		bool stopping() const noexcept { return stopping_.load(std::memory_order_acquire); }

		// The owner cancels running I/O before joining. Stop discards pending work
		// and permanently closes admission; it never detaches a borrowed owner.
		// A callback may close admission; the external owner still performs the join.
		void stop()
		{
			bool on_worker{};
			{
				const std::lock_guard lock(mutex_);
				stopping_ = true;
				pending_ = {};
				on_worker = worker_id_ == std::this_thread::get_id();
			}
			ready_.notify_one();
			if (on_worker) return;
			const std::lock_guard stop_lock(stop_mutex_);
			if (thread_.joinable()) thread_.join();
		}

	private:
		struct entry { task work; failure_handler failed; };
		std::mutex mutex_, stop_mutex_;
		std::condition_variable ready_;
		std::thread thread_;
		std::thread::id worker_id_;
		entry pending_;
		std::atomic_bool stopping_{};

		void run() noexcept
		{
			for (;;)
			{
				entry next;
				{
					std::unique_lock lock(mutex_);
					ready_.wait(lock, [this] { return stopping_ || bool(pending_.work); });
					if (stopping_) return;
					next = std::exchange(pending_, {});
				}
				try { next.work(); }
				catch (...)
				{
					// Failure belongs to this task. It must not kill the worker or
					// prevent the newest queued request from starting.
					try { next.failed(std::current_exception()); }
					catch (...) {}
				}
			}
		}
	};
}
