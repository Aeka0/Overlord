#include <std_include.hpp>
#include "native_launcher_fire.hpp"
#include "native_ballistics.hpp"
#include "native_weapon_read.hpp"
#include "native_ammunition.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::native_launcher_fire
{
	namespace
	{
		bool ready{};
		unsigned cached{};
		std::uint64_t epoch{};
		int checked{-1000};
		constexpr std::array names{"m203_m4", "m203_m16", "scar_h_m203", "gl_masada", "gl_ak47", "m203"};
		template <std::size_t N> bool match(std::uintptr_t at, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t, N> mask{};
			mask.fill(255);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),
			                                                        {bytes, mask.data(), N}));
		}
		bool admitted(const native_weapon_read::entry& value) noexcept
		{
			if (!value)
				return false;
			const auto* d = value.definition;
			// Native H2 class 6 is the witnessed launcher class. SDK enum labels
			// for this field are shifted and must not replace these verified values.
			return d->weapType == game::WEAPTYPE_PROJECTILE && int(d->weapClass) == 6 && d->clipSize == 1 &&
			       d->projectileModel && d->projImpactExplode && d->projectileSpeed > 0 &&
			       d->projectileSpeed <= 11000;
		}
	}
	bool initialize()
	{
		if (ready)
			return true;
		constexpr std::uint8_t entry[]{0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x50};
		constexpr std::uint8_t packet[]{0x0f, 0xb6, 0x57, 0x44, 0x8b, 0x4f, 0x3c};
		constexpr std::uint8_t call[]{0xe8, 0x9f, 0x40, 0, 0};
		return ready = match(0x14051cd40, entry) && match(0x14051cd69, packet) && match(0x140518c9c, call);
	}
	void reset() noexcept
	{
		cached = 0;
		epoch = 0;
		checked = -1000;
	}
	unsigned resident_grenade() noexcept
	{
		if (!initialize())
			return 0;
		const auto time = game::CG_GetGameTime(0);
		const auto timeline = native_ammunition::timeline();
		if (timeline != epoch || time < checked)
			reset();
		epoch = timeline;
		if (cached)
		{
			const auto value = native_weapon_read::get(cached);
			if (admitted(value) &&
			    std::find(names.begin(), names.end(), std::string_view(value.name.data())) != names.end())
				return cached;
			cached = 0;
		}
		if (std::int64_t(time) - checked < 1000)
			return 0;
		checked = time;
		std::size_t best = names.size();
		for (unsigned token = 1; token < native_weapon_read::token_limit; ++token)
		{
			const auto value = native_weapon_read::get(token);
			if (!value)
				continue;
			const auto found = std::find(names.begin(), names.end(), std::string_view(value.name.data()));
			const auto rank = std::size_t(found - names.begin());
			if (rank < best && admitted(value))
			{
				cached = token;
				best = rank;
				if (!rank)
					break;
			}
		}
		if (cached)
			return cached;
		for (const auto* name : names)
		{
			unsigned token{};
			if (game::DB_XAssetExists(game::ASSET_TYPE_WEAPON, name) &&
			    !game::DB_IsXAssetDefault(game::ASSET_TYPE_WEAPON, name))
				token = game::G_GetWeaponForName(name).data;
			const auto value = native_weapon_read::get(token);
			if (admitted(value) && std::string_view(value.name.data()) == name)
				return cached = token;
		}
		return 0; // No fabricated definition, whole-map load or silent bullet fallback.
	}
	bool prepare(unsigned token,
	             const shot_geometry& geometry,
	             native_ballistics::parameters& packet) noexcept
	{
		if (!initialize())
			return false;
		const auto value = native_weapon_read::get(token);
		if (!admitted(value))
			return false;
		packet = {};
		packet.shot = geometry;
		packet.weapon = token;
		packet.definition = const_cast<game::WeaponDef*>(value.definition);
		return true;
	}
	game::gentity_s* fire(game::gentity_s* attacker, const native_ballistics::parameters& packet)
	{
		// Exactly the native G_FireWeapon class-6 and existing VR underbarrel call.
		// Native speed, inherited movement, arming distance, collision and damage
		// remain on the real launcher definition; no player selection swap occurs.
		return utils::hook::invoke<game::gentity_s*>(0x14051cd40, attacker, packet.weapon, &packet);
	}
}
