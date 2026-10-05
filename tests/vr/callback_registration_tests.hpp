#pragma once
#include "component/vr/callback_registration.hpp"
#include <type_traits>

namespace callback_registration_tests
{
	inline unsigned calls{};
	inline void invoke() noexcept { ++calls; }
	template<class Check> void run(Check check)
	{
		using callback = void(*)() noexcept;
		using owner = vr::callback_registration<callback>;
		static_assert(!std::is_copy_constructible_v<owner>);
		std::atomic<callback> slot{};
		owner first(slot, invoke);
		bool rejected{};
		try { owner duplicate(slot, invoke); }
		catch (const std::logic_error&) { rejected = true; }
		check(rejected && slot.load() == invoke, "duplicate registration preserves the existing owner");
		owner transferred(std::move(first));
		first.reset();
		check(!first && transferred && slot.load() == invoke, "moved ownership cannot be cleared by the previous handle");
		calls = 0;
		if (const auto callback = slot.load()) callback();
		check(calls == 1, "registered callback remains available for dispatch");
		transferred.reset();
		owner replacement(slot, invoke);
		transferred.reset();
		check(slot.load() == invoke, "repeated reset cannot clear a replacement using the same function");
		{
			owner scoped(std::move(replacement));
			check(bool(scoped), "scope receives exclusive ownership");
		}
		check(!slot.load(), "scope destruction unregisters its callback");
		std::atomic<callback> other_slot{};
		owner destination(slot, invoke);
		owner source(other_slot, invoke);
		destination = std::move(source);
		check(!slot.load() && other_slot.load() == invoke && !source, "move assignment retires only the previous owned slot");
		destination.reset();
		rejected = false;
		try { owner empty(slot, nullptr); }
		catch (const std::invalid_argument&) { rejected = true; }
		check(rejected && !slot.load(), "empty callbacks cannot acquire a slot");
	}
}
