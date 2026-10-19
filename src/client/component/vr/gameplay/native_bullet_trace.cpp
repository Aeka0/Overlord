#include <std_include.hpp>
#include "native_bullet_trace.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::native_bullet_trace
{
	namespace
	{
		utils::hook::detour hook;
		bool installed{};
		interceptor filter{};
		std::array<observer, 4> observers{};
		std::size_t count{};
		bool original(parameters* p,
		              unsigned weapon,
		              bool alternate,
		              game::gentity_s* attacker,
		              result* hit,
		              int surface)
		{
			return hook.invoke<bool>(p, weapon, alternate, attacker, hit, surface);
		}
		bool trace(parameters* p,
		           unsigned weapon,
		           bool alternate,
		           game::gentity_s* attacker,
		           result* hit,
		           int surface)
		{
			const bool value = filter ? filter(original, p, weapon, alternate, attacker, hit, surface)
			                          : original(p, weapon, alternate, attacker, hit, surface);
			if (p && hit)
				for (std::size_t i{}; i < count; ++i)
					observers[i](*p, weapon, alternate, attacker, *hit);
			return value;
		}
	}
	bool initialize()
	{
		if (installed)
			return true;
		constexpr std::uint8_t bytes[]{0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
		                               0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
		                               0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x40};
		std::array<std::uint8_t, sizeof(bytes)> mask{};
		mask.fill(255);
		if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1404ac4e0),
		                                                 {bytes, mask.data(), sizeof(bytes)}))
			return false;
		hook.create(0x1404ac4e0, trace);
		return installed = true;
	}
	bool set_interceptor(interceptor value)
	{
		if (!value || (filter && filter != value) || !initialize())
			return false;
		filter = value;
		return true;
	}
	bool add_observer(observer value)
	{
		if (!value || !initialize())
			return false;
		if (std::find(observers.begin(), observers.begin() + count, value) != observers.begin() + count)
			return true;
		if (count == observers.size())
			return false;
		observers[count++] = value;
		return true;
	}
}
