#include <std_include.hpp>
#include <optional>
#include "loader/component_loader.hpp"
#include "game/dvars.hpp"
#include "component/vr/settings.hpp"
#include "component/vr/gameplay/melee_weapon_policy.hpp"
#include "launcher/vr_settings_config.hpp"

namespace
{
	std::unique_ptr<component_interface>& owner()
	{
		static std::unique_ptr<component_interface> value;
		return value;
	}
	game::dvar_t setting{};
	bool registered{};
	void require(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}
	game::dvar_t* lookup(const char* name)
	{
		require(std::string_view(name) == vr::settings::melee_weapon.name, "Melee reads its own registered setting");
		return registered ? &setting : nullptr;
	}
}

// The production component is linked unchanged. Only the native engine and
// component host are substituted, so omitting registration fails this test.
void component_loader::register_component(std::unique_ptr<component_interface>&& component)
{
	require(!owner(), "Exactly one melee setting owner");
	owner() = std::move(component);
}

game::dvar_t* dvars::register_enum(const std::string& name, const char** values,
	int default_index, unsigned int flags, const std::string&)
{
	const auto& field = vr::settings::melee_weapon;
	require(name == field.name && default_index == field.default_index, "Native melee default matches launcher catalog");
	require((flags & game::DVAR_FLAG_SAVED) != 0, "Native melee selection must persist on game save");
	for (std::size_t i{}; i < field.values.size(); ++i)
	{
		require(bool(values[i]) == bool(field.values[i]), "Native melee enum must terminate at the catalog boundary");
		if (values[i]) require(std::string_view(values[i]) == field.values[i], "Native melee ordinals match launcher choices");
	}
	setting.type = game::dvar_type::enumeration;
	setting.current.integer = default_index;
	registered = true;
	return &setting;
}

void test_native_melee_setting()
{
	namespace melee = vr::gameplay::equipment::melee_weapons;
	game::Dvar_FindVar = decltype(game::Dvar_FindVar)(reinterpret_cast<std::size_t>(&lookup));
	require(melee::selected() == melee::choice::native, "Before registration, melee retains default policy");
	require(bool(owner()), "Production melee component must register with the loader");
	owner()->post_unpack();
	require(registered, "Production post_unpack must register the melee enum before profile execution");
	for (int index{}; index <= int(melee::choice::bloody); ++index)
	{
		setting.current.integer = index;
		require(melee::selected() == melee::choice(index), "Runtime selection consumes the registered enum ordinal");
		require(melee::cheat_tools_allowed() == (index == 0), "Only Default permits official cheat substitution");
	}
	setting.type = game::dvar_type::string;
	setting.current.string = "baton";
	require(melee::selected() == melee::choice::native, "Unexpected string dvars cannot be read as integer union members");
}
