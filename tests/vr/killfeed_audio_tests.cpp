#include <iostream>
#include <fstream>
#include <iterator>
#include <xsk/gsc/engine/h2.hpp>
extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

int main()
{
	try
	{
		xsk::gsc::h2::context context(xsk::gsc::instance::server);
		context.init(xsk::gsc::build::prod,
			[](const xsk::gsc::context*, const std::string& name)
				-> std::pair<xsk::gsc::buffer, std::vector<std::uint8_t>>
			{ throw std::runtime_error("Unexpected GSC include: " + name); });
		std::ifstream source("data/cdata/scripts/killfeed_ducking.gsc", std::ios::binary);
		if (!source) throw std::runtime_error("Ducking script is missing");
		std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(source), {}};
		const auto assembly = context.compiler().compile("scripts/killfeed_ducking", data);
		const auto& [script, stack, devmap] = context.assembler().assemble(*assembly);
		if (!script.size || !stack.size) throw std::runtime_error("Empty ducking script bytecode");
		std::cout << "Ducking GSC compiles and assembles for H2\n";
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
	auto* state = luaL_newstate();
	if (!state) return 1;
	luaL_openlibs(state);
	const auto result = luaL_dofile(state, "tests/vr/killfeed_audio_tests.lua");
	if (result) std::cerr << lua_tostring(state, -1) << '\n';
	lua_close(state);
	return result ? 1 : 0;
}
