#include "component/ui_script_modules.hpp"
#include "product.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace
{
	void require(bool value, const char* message)
	{
		if (!value) throw std::runtime_error(message);
	}
	void initializer(const std::filesystem::path& root, const std::string& name)
	{
		const auto folder = root / "ui_scripts" / name;
		std::filesystem::create_directories(folder);
		std::ofstream(folder / "__init__.lua") << "return true\n";
	}
	void paths(lua_State* state, const char* name, const std::vector<std::string>& values)
	{
		lua_createtable(state, static_cast<int>(values.size()), 0);
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			lua_pushlstring(state, values[i].data(), values[i].size());
			lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
		}
		lua_setglobal(state, name);
	}
}

int main(int argc, char** argv)
{
	namespace fs = std::filesystem;
	using ui_scripting::modules::discover;
	if (argc > 1 && std::string_view(argv[1]) == "--inspect")
	{
		std::vector<std::string> roots;
		for (int i = 2; i < argc; ++i) roots.emplace_back(argv[i]);
		for (const auto& selected : discover(roots)) std::cout << selected.name << " -> " << selected.initializer << '\n';
		return 0;
	}
	const auto parent = fs::absolute(".tests-temp");
	fs::create_directories(parent);
	const auto root = parent / ("ui-modules-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	require(fs::create_directory(root), "test directory must be new");
	const auto cleanup = [&]
	{
		if (fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(parent) &&
			root.filename().string().starts_with("ui-modules-")) fs::remove_all(root);
	};
	try
	{
		require(std::string(product::fixed_menu_text("@MENU_SP_CAMPAIGN")) == "Overlord", "campaign title uses current product");
		require(std::string(product::fixed_menu_text("MENU_GENERAL")) == "Overlord", "settings title uses current product");
		for (const auto* key : {"MENU_SYSINFO_CUSTOMER_SUPPORT_URL", "@MENU_SYSINFO_DONATION_URL"})
			require(product::fixed_menu_text(key) == product::repository_url, "displayed project URLs cannot retain an upstream destination");
		require(std::string(product::menu_text_alias("MENU_SYSINFO_DONATION_LINK")) == "MENU_SYSINFO_CUSTOMER_SUPPORT_LINK",
			"legacy donation action is labeled as a project link");
		require(product::menu_description("MENU_GENERAL_DESC", "Set h2-mod's settings.") == "Set Overlord's settings.", "localized product descriptions are rebranded");
		require(product::menu_description("LUA_MENU_FALLBACK_ENABLE", "使用 h2-mod 字体") == "使用 Overlord 字体", "native UTF-8 wording is retained");
		require(product::menu_description("LUA_MENU_FALLBACK_ENABLE", "Overlord / overlord") == "Overlord / overlord", "already updated branding is not duplicated");
		require(product::menu_description("MENU_GENERAL_DESC", "H2-MOD VR / h2-mod-vr") == "Overlord / Overlord", "older installed VR locale names are replaced completely");
		require(!product::fixed_menu_text("CREDIT_H2MOD_VLAD") && !product::menu_text_alias("CREDIT_H2MOD_VLAD") &&
			product::menu_description("CREDIT_H2MOD_VLAD", "H2-Mod Developers") == "H2-Mod Developers", "upstream credit identity and prose are preserved");
		require(product::menu_description("UNRELATED", "https://github.com/alicealys/h2-mod") == "https://github.com/alicealys/h2-mod", "unrelated documents and links are never globally rewritten");
		const auto cache = root / "cache", local = root / "local", localized = local / "simplified_chinese", mod = root / "mod";
		initializer(cache, "buttons"); initializer(local, "buttons");
		initializer(cache, "cache_only"); initializer(local, "vr_gameplay");
		initializer(cache, "localized"); initializer(localized, "localized");
		initializer(local, "overridden"); initializer(mod, "Overridden");
		// A higher directory without an initializer is not a module override.
		fs::create_directories(mod / "ui_scripts/cache_only");
		const std::vector<std::string> roots{cache.generic_string(), local.generic_string(), localized.generic_string(), mod.generic_string()};
		const auto selected = discover(roots);
		require(selected.size() == 5, "each logical module appears once; distinct packages survive");
		const auto find = [&](const char* name) -> const std::string&
		{
			for (const auto& item : selected) if (item.name == name) return item.initializer;
			throw std::runtime_error("missing expected module");
		};
		require(find("buttons") == (local / "ui_scripts/buttons/__init__.lua").generic_string(), "local package overrides cache");
		require(find("cache_only") == (cache / "ui_scripts/cache_only/__init__.lua").generic_string(), "unshadowed cache remains available");
		require(find("localized") == (localized / "ui_scripts/localized/__init__.lua").generic_string(), "localized overlay wins");
		require(find("overridden") == (mod / "ui_scripts/Overridden/__init__.lua").generic_string(), "active mod wins with case-insensitive identity");
		require(selected.front().name == "cache_only" && selected.back().name == "overridden", "surviving packages retain low-to-high load order");
		auto aliases = roots; aliases.push_back(local.generic_string());
		require(discover(aliases).size() == selected.size(), "repeated or aliased root cannot duplicate initialization");
		fs::remove(local / "ui_scripts/buttons/__init__.lua");
		const auto refreshed = discover(roots);
		bool used_cache = false;
		for (const auto& item : refreshed) if (item.name == "buttons")
			used_cache = item.initializer == (cache / "ui_scripts/buttons/__init__.lua").generic_string();
		require(used_cache, "each VM startup rediscovers current files without stale cross-VM selection");

		for (const auto& location : {cache, local})
			fs::copy_file("data/cdata/ui_scripts/buttons/__init__.lua", location / "ui_scripts/buttons/__init__.lua", fs::copy_options::overwrite_existing);
		std::vector<std::string> chosen;
		for (const auto& item : discover({cache.generic_string(), local.generic_string()}))
			if (item.name == "buttons") chosen.push_back(item.initializer);
		for (int vm = 0; vm < 2; ++vm)
		{
			auto* state = luaL_newstate();
			require(state != nullptr, "Lua state created");
			luaL_openlibs(state);
			paths(state, "selected_initializers", chosen);
			paths(state, "duplicate_initializers", {(cache / "ui_scripts/buttons/__init__.lua").generic_string(),
				(local / "ui_scripts/buttons/__init__.lua").generic_string()});
			const auto result = luaL_dofile(state, "tests/ui_script_modules_tests.lua");
			const std::string error = result ? lua_tostring(state, -1) : "";
			lua_close(state);
			if (result) throw std::runtime_error(error);
		}
		cleanup();
		std::cout << "ui-script-modules-tests: PASS (overlay selection, duplicate-menu reproduction, two VMs, error recovery)\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		cleanup();
		std::cerr << error.what() << '\n';
		return 1;
	}
}
