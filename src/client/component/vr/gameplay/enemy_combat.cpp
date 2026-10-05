#include <std_include.hpp>
#include "enemy_combat_policy.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/notifies.hpp"
#include "component/scripting.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook_validation.hpp>

namespace vr::gameplay::enemy_combat
{
	namespace
	{
		game::dvar_t* melee_scale{};
		game::dvar_t* disable_pounce{};
		const char* dog_site{};
		std::atomic_bool dog_bound{}, damage_bound{};
		std::atomic_uint64_t scaled_hits{}, bite_selections{};
		std::atomic_int last_input{}, last_scaled{};

		template<class T> T read(const void* base, std::size_t offset) noexcept
		{
			T value{};
			std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
			return value;
		}
		bool vr_enabled()
		{
			const auto* enabled = game::Dvar_FindVar("vr_enable");
			return enabled && enabled->current.enabled;
		}
		int filter_damage(const game::gentity_s* target, const game::gentity_s* attacker,
			int damage, unsigned means_of_death)
		{
			if (damage <= 0 || (means_of_death != 8 && means_of_death != 9) ||
				!target || !target->client || !attacker || attacker == target || attacker->client ||
				!read<void*>(attacker, 0x120) || !vr_enabled()) return damage;
			const auto* player_sentient = read<void*>(target, 0x128);
			const auto* attacker_sentient = read<void*>(attacker, 0x128);
			if (!player_sentient || !attacker_sentient || !enemy_melee(true, true, true,
				read<unsigned>(player_sentient, 0x10), read<unsigned>(attacker_sentient, 0x10), means_of_death)) return damage;
			const auto scaled = scale_damage(damage, melee_scale->current.value);
			last_input = damage;
			last_scaled = scaled;
			++scaled_hits;
			return scaled;
		}
		bool choose_bite()
		{
			if (!disable_pounce->current.enabled || !vr_enabled()) return false;
			++bite_selections;
			return true;
		}
		void clear_dog_hook()
		{
			if (dog_site) notifies::clear_hook(dog_site);
			dog_site = nullptr;
			dog_bound = false;
		}
		void bind_dog_hook()
		{
			clear_dog_hook();
			const auto file = scripting::script_function_table_sort.find("animscripts/dog/dog_combat");
			if (file == scripting::script_function_table_sort.end()) return;
			const char* begin{}, *end{};
			for (const auto& [name, pos] : file->second)
				if (name == "dog_cant_kill_in_one_hit") begin = pos;
			if (begin) for (const auto& [name, pos] : file->second)
				if (pos > begin && (!end || pos < end)) end = pos;
			if (!begin || !end) return;
			const auto branch = bite_branch({reinterpret_cast<const std::uint8_t*>(begin), std::size_t(end - begin)});
			if (!branch)
			{
				console::error("[VR combat] dog branch signature rejected; pounce suppression unavailable\n");
				return;
			}
			dog_site = begin + branch->source;
			notifies::set_gsc_hook(dog_site, begin + branch->target, choose_bite);
			dog_bound = true;
		}
		bool verify_damage_layout()
		{
			// G_Damage compares sentient teams at +0x10, using gentity +0x128.
			// Its actor gate is gentity +0x120. Keep layout evidence local here;
			// the shared notifies hook remains the sole owner of G_Damage.
			constexpr std::array<std::uint8_t,23> teams{
				0x48,0x8b,0x84,0x08,0x28,0x01,0,0,0x48,0x8b,0x97,0x28,0x01,0,0,
				0x8b,0x48,0x10,0x39,0x4a,0x10,0x75,0x21};
			constexpr std::array<std::uint8_t,9> actor{0x49,0x83,0xbd,0x20,0x01,0,0,0,0x74};
			std::array<std::uint8_t,teams.size()> team_mask{}; team_mask.fill(255);
			std::array<std::uint8_t,actor.size()> actor_mask{}; actor_mask.fill(255);
			return utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1404bd658),
				{teams.data(),team_mask.data(),teams.size()}) &&
				utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1404bd74a),
				{actor.data(),actor_mask.data(),actor.size()});
		}
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			const auto& scale = settings::enemy_melee_damage;
			melee_scale = dvars::register_float(scale.name, scale.default_value, scale.min, scale.max,
				game::DVAR_FLAG_SAVED, "Scale enemy AI melee damage received by the VR player");
			disable_pounce = dvars::register_bool(settings::disable_dog_pounce.name,
				settings::disable_dog_pounce.default_value, game::DVAR_FLAG_SAVED,
				"Use native dog bites instead of player knockdown attacks in VR");
			if (verify_damage_layout())
			{
				notifies::add_entity_damage_filter(filter_damage);
				damage_bound = true;
			}
			else console::error("[VR combat] damage layout rejected; melee scaling unavailable\n");
			scripting::on_level_start(bind_dog_hook);
			// Retained scripts may resume during checkpoint load, before on_level_start.
			scripting::on_shutdown([](bool free_scripts, bool after) {if (free_scripts && !after) clear_dog_hook();});
			command::add("vr_enemy_combat_status", [] {
				const auto report = std::format("[VR combat] vr={} damage_bound={} melee_scale={} scaled_hits={} last_raw={}->{} dog_bound={} disable_pounce={} bite_selections={}\n",
					vr_enabled(),damage_bound.load(),melee_scale->current.value,scaled_hits.load(),last_input.load(),last_scaled.load(),
					dog_bound.load(),disable_pounce->current.enabled,bite_selections.load());
				console::info("%s",report.c_str());
			});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::enemy_combat::component)
