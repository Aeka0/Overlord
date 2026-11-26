#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <new>

namespace vr
{
	// Lazy heap-allocated storage for large static diagnostic or thread-local objects.
	// Replaces multi-kilobyte/megabyte .data and .tls templates with an 8-byte pointer.
	template <typename T>
	class lazy_box
	{
	public:
		using value_type = T;
		static constexpr std::size_t storage_bytes = sizeof(T);

		[[nodiscard]] T& get() noexcept
		{
			if (!ptr_)
			{
				ptr_ = std::make_unique<T>();
			}
			return *ptr_;
		}

		[[nodiscard]] T* get_if() const noexcept
		{
			return ptr_.get();
		}

		[[nodiscard]] bool allocated() const noexcept
		{
			return ptr_ != nullptr;
		}

		void zero_if_allocated() noexcept
		{
			if (ptr_)
			{
				// Reconstruct in place instead of `*ptr_ = {}` so large T never builds a stack temporary.
				ptr_->~T();
				::new (static_cast<void*>(ptr_.get())) T{};
			}
		}

		void clear_storage() noexcept
		{
			ptr_.reset();
		}

		T* operator->() noexcept
		{
			return &get();
		}

		T& operator*() noexcept
		{
			return get();
		}

	private:
		std::unique_ptr<T> ptr_{};
	};

	// Lazy heap-allocated storage for large static diagnostic or thread-local std::array<T, N>.
	template <typename T, std::size_t N>
	class lazy_array
	{
	public:
		using array_type = std::array<T, N>;
		static constexpr std::size_t storage_bytes = sizeof(array_type);

		[[nodiscard]] static constexpr std::size_t size() noexcept
		{
			return N;
		}

		[[nodiscard]] array_type& get() noexcept
		{
			if (!ptr_)
			{
				ptr_ = std::make_unique<array_type>();
			}
			return *ptr_;
		}

		[[nodiscard]] const array_type* get_if() const noexcept
		{
			return ptr_.get();
		}

		[[nodiscard]] array_type* get_if() noexcept
		{
			return ptr_.get();
		}

		[[nodiscard]] bool allocated() const noexcept
		{
			return ptr_ != nullptr;
		}

		[[nodiscard]] T& operator[](const std::size_t index) noexcept
		{
			return get()[index];
		}

		[[nodiscard]] T* data() noexcept
		{
			return get().data();
		}

		[[nodiscard]] auto begin() noexcept
		{
			return get().begin();
		}

		[[nodiscard]] auto end() noexcept
		{
			return get().end();
		}

		void fill(const T& value) noexcept
		{
			if (ptr_)
			{
				ptr_->fill(value);
			}
		}

		void zero_if_allocated() noexcept
		{
			if (ptr_)
			{
				// Reconstruct element-by-element so neither `*ptr_ = {}` nor `T{}` puts large arrays on the stack.
				for (auto& element : *ptr_)
				{
					element.~T();
					::new (static_cast<void*>(&element)) T{};
				}
			}
		}

		void clear_storage() noexcept
		{
			ptr_.reset();
		}

	private:
		std::unique_ptr<array_type> ptr_{};
	};
}
