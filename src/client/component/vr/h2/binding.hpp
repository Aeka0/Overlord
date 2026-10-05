#pragma once
#include <cstdint>

namespace vr::h2
{
	// A canonical native signature, not a signature inferred from each call's
	// argument types. The owning adapter still validates executable identity,
	// call-site bytes, thread ownership and current object lifetime.
	template <class Signature> class function;
	template <class Result, class... Arguments> class function<Result(Arguments...)>
	{
	  public:
		using signature = Result(Arguments...);
		using pointer = signature*;
		explicit constexpr function(std::uintptr_t address) noexcept : address_(address)
		{
		}
		constexpr std::uintptr_t address() const noexcept
		{
			return address_;
		}
		pointer get() const noexcept
		{
			return reinterpret_cast<pointer>(address_);
		}
		Result operator()(Arguments... arguments) const
		{
			return get()(arguments...);
		}

	  private:
		std::uintptr_t address_;
	};

	// Only the address is immutable. Read the live native value at its original
	// use boundary; a readable pointer does not establish readiness or ownership.
	template <class T> class read_only_global
	{
	  public:
		using value_type = T;
		explicit constexpr read_only_global(std::uintptr_t address) noexcept : address_(address)
		{
		}
		constexpr std::uintptr_t address() const noexcept
		{
			return address_;
		}
		const T* get() const noexcept
		{
			return reinterpret_cast<const T*>(address_);
		}
		T read() const
		{
			return *get();
		}

	  private:
		std::uintptr_t address_;
	};
}
