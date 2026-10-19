#pragma once
#include "hei_round_policy.hpp"
#include "weapon_identity.hpp"

namespace vr::gameplay::weapons::hei_round
{
	// Freeze delivery before ammunition debit. A requested but unavailable
	// conversion rejects the shot; zero with requested=false is ordinary fire.
	unsigned projectile_definition(unsigned source, bool& requested) noexcept;
	void projectile_fired(game::gentity_s*) noexcept;
	class shot_scope
	{
		friend void observe(const native_bullet_trace::parameters&,
		                    unsigned,
		                    bool,
		                    const game::gentity_s*,
		                    const native_bullet_trace::result&) noexcept;
		first_contact contact;
		weapon_identity weapon;
		shot_scope* previous{};
		const void* player{};
		std::uint64_t timeline{}, epoch{};
		int command_time{};
		bool admitted{};

	  public:
		explicit shot_scope(weapon_identity) noexcept;
		~shot_scope();
		shot_scope(const shot_scope&) = delete;
		shot_scope& operator=(const shot_scope&) = delete;
		void complete() noexcept;
	};
}
