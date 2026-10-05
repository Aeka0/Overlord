#pragma once
#include <atomic>
#include <stdexcept>
#include <utility>

namespace vr
{
	// Exclusive ownership of a callback slot. Registration happens on the loader
	// thread; dispatch reads the atomic slot without taking a lock. Reset prevents
	// future dispatch, but does not drain an already borrowed callback: the owner
	// retains its existing alive/device/lifetime guards and shutdown boundary.
	template <class Callback> class callback_registration
	{
		std::atomic<Callback>* slot_{};
		Callback callback_{};

	  public:
		callback_registration() noexcept = default;
		callback_registration(std::atomic<Callback>& slot, Callback callback)
		{
			if (!callback)
				throw std::invalid_argument("Cannot register an empty VR callback");
			Callback expected{};
			if (!slot.compare_exchange_strong(expected, callback))
				throw std::logic_error("VR callback slot already has an owner");
			slot_ = &slot;
			callback_ = callback;
		}
		callback_registration(const callback_registration&) = delete;
		callback_registration& operator=(const callback_registration&) = delete;
		callback_registration(callback_registration&& other) noexcept
		    : slot_(std::exchange(other.slot_, nullptr)), callback_(std::exchange(other.callback_, nullptr))
		{
		}
		callback_registration& operator=(callback_registration&& other) noexcept
		{
			if (this != &other)
			{
				reset();
				slot_ = std::exchange(other.slot_, nullptr);
				callback_ = std::exchange(other.callback_, nullptr);
			}
			return *this;
		}
		~callback_registration()
		{
			reset();
		}
		void reset() noexcept
		{
			if (const auto slot = std::exchange(slot_, nullptr))
			{
				auto expected = std::exchange(callback_, nullptr);
				slot->compare_exchange_strong(expected, nullptr);
			}
		}
		explicit operator bool() const noexcept
		{
			return slot_ != nullptr;
		}
	};
}
