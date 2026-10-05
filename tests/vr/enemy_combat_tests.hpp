#pragma once
#include "component/vr/gameplay/enemy_combat_policy.hpp"

namespace enemy_combat_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::enemy_combat;
		for (unsigned mod = 0; mod < 20; ++mod)
			check(enemy_melee(true,true,true,2,1,mod) == (mod == 8 || mod == 9),
				"Only the two native melee damage kinds are scaled");
		check(!enemy_melee(false,true,true,2,1,8) && !enemy_melee(true,false,true,2,1,8) &&
			!enemy_melee(true,true,false,2,1,8) && !enemy_melee(true,true,true,2,2,8) &&
			!enemy_melee(true,true,true,0,1,8) && !enemy_melee(true,true,true,2,0,8),
			"Desktop play, outgoing hits, world damage, allies and missing teams are excluded");
		check(enemy_melee(true,true,true,2,3,8), "An attacking neutral actor also uses the melee policy");
		check(scale_damage(200,.5f) == 100 && scale_damage(100,.5f) == 50 &&
			scale_damage(100,1) == 100 && scale_damage(100,2) == 200,
			"Scale original incoming damage once, retaining native difficulty calculation downstream");
		check(scale_damage(1,.1f) == 1 && scale_damage(0,.5f) == 0 && scale_damage(-1,.5f) == -1,
			"Rounding preserves positive hits without resurrecting canceled damage");
		check(scale_damage(std::numeric_limits<int>::max(),2) == std::numeric_limits<int>::max() &&
			scale_damage(100,std::numeric_limits<float>::quiet_NaN()) == 100 &&
			scale_damage(100,std::numeric_limits<float>::infinity()) == 100 &&
			scale_damage(100,-2) == 10 && scale_damage(100,50) == 200,
			"Corrupt and extreme multipliers never overflow native damage");
		std::array<std::uint8_t,126> native{};
		constexpr std::array<std::uint8_t,18> prefix{
			0x32,0x18,0x8e,0x5b,0x33,0x51,0xdc,0x2c,0x1b,0x2f,0,0xb0,3,0,0xa0,1,0x19,0x53};
		std::copy(prefix.begin(),prefix.end(),native.begin());
		const auto branch = bite_branch(native);
		check(branch && branch->source == 1 && native[branch->target] == 0xa0 &&
			native[branch->target+1] == 1 && native[branch->target+2] == 0x19,
			"Dog decision resumes the native return-true after normal parameter validation");
		check(!bite_branch({}) && !bite_branch({native.data(),125}), "Missing or changed script layout rejects the hook");
		for (unsigned i = 0; i < prefix.size(); ++i)
		{
			native[i] ^= 1;
			check(!bite_branch(native), "Changed field, branch or opcode rejects the dog hook");
			native[i] ^= 1;
		}
	}
}
