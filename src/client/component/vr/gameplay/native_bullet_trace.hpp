#pragma once
#include "../spatial_math.hpp"
#include <cstddef>
#include <cstdint>

namespace game
{
	struct gentity_s;
}

namespace vr::gameplay::weapons::native_bullet_trace
{
	// H2-owned trace packet. Share this ABI across shield and shot observers.
	struct parameters
	{
		std::uint16_t shooter, ignore;
		float scale;
		int means;
		bool flag;
		std::byte pad[3];
		spatial_math::vec original, start, end, direction;
	};
	struct result
	{
		float fraction;
		spatial_math::vec normal;
		int surface, contents, type;
		std::uint16_t entity;
		std::byte pad[2];
		unsigned part;
		std::uint16_t model, location;
		std::byte tail[8];
		void* target;
		spatial_math::vec position;
		bool ignored;
		std::byte reserved[3];
		int surface_type;
	};
	static_assert(offsetof(parameters, start) == 0x1c && offsetof(parameters, direction) == 0x34);
	static_assert(offsetof(result, target) == 0x30 && offsetof(result, position) == 0x38 &&
	              sizeof(result) == 0x50);
	using next = bool (*)(parameters*, unsigned, bool, game::gentity_s*, result*, int);
	using interceptor = bool (*)(next, parameters*, unsigned, bool, game::gentity_s*, result*, int);
	using observer =
	    void (*)(const parameters&, unsigned, bool, const game::gentity_s*, const result&) noexcept;
	bool initialize();
	// Startup registration only. One physical collision modifier; observers see
	// its final result, never the uncorrected native shield contact.
	bool set_interceptor(interceptor);
	bool add_observer(observer);
}
