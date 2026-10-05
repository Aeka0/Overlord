#pragma once

#include <atomic>

namespace gui
{
	// Native menus share the UI catcher. The overlay borrows it on input-side
	// open/close transitions; rendering must never clear or reassert that bit.
	class input_capture
	{
	public:
		[[nodiscard]] bool is_open() const noexcept { return open_.load(std::memory_order_relaxed); }

		// A native menu can acquire/release this same bit after the overlay opens.
		// Once the engine explicitly writes it, the overlay no longer owns it.
		void native_catcher_write() noexcept { added_catcher_ = false; }

		void set_open(int& catchers, const bool open) noexcept
		{
			if (open == is_open()) return;
			if (open)
			{
				added_catcher_ = (catchers & ui_catcher) == 0;
				catchers |= ui_catcher;
			}
			else
			{
				if (added_catcher_) catchers &= ~ui_catcher;
				added_catcher_ = false;
			}
			open_.store(open, std::memory_order_relaxed);
		}

	private:
		static constexpr int ui_catcher = 0x10;
		std::atomic_bool open_{};
		bool added_catcher_{}; // Only the input/lifecycle owner changes capture.
	};
}
