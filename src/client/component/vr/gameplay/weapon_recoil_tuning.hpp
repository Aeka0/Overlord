#pragma once
#include "native_weapon_family.hpp"
#include <array>

namespace vr::gameplay::weapons::recoil
{
	struct weapon_tuning
	{
		std::string_view family;
		float scale;
		int native_class{-1}; // Rifle guard excludes similarly named alternate feeds.
		bool short_weapon{}; // Overrides native SMG classification for the long-only penalty.
	};
	// Relative to the existing standing 3x VR baseline. Native family matching includes
	// attachment/skin/akimbo suffixes, with the same name boundaries as reload.
	inline constexpr std::array weapon_tunings{
		weapon_tuning{"deserteagle",5.f}, weapon_tuning{"coltanaconda",5.f},
		weapon_tuning{"usp",.6f},
		weapon_tuning{"m4",.5f,0}, weapon_tuning{"m4m203",.5f,0},
		weapon_tuning{"masada",.5f,0}, // ACR's native stem.
		weapon_tuning{"ak47",1.5f,0},
		weapon_tuning{"m14",.5f}, weapon_tuning{"m21",.5f}, weapon_tuning{"m14ebr",.5f},
		weapon_tuning{"tmp",.3f,-1,true}, weapon_tuning{"pp2000",.4f,-1,true},
		weapon_tuning{"uzi",1.f,-1,true}, // Mini Uzi; preserve the existing base recoil.
		weapon_tuning{"p90",.5f},
		weapon_tuning{"mp5",.25f}, // H2 MP5K uses the native mp5 stem.
		weapon_tuning{"kriss",.25f}, // Vector's native stem.
		weapon_tuning{"fal",2.f,0}, weapon_tuning{"scar_h",1.2f,0},
		weapon_tuning{"fn2000",1.5f,0}, weapon_tuning{"famas",1.2f,0},
		weapon_tuning{"tavor",.75f,0}, // TAR-21's native stem.
		weapon_tuning{"ump45",1.2f}, weapon_tuning{"beretta393",.5f},
		weapon_tuning{"aa12",3.f,4}};

	inline const weapon_tuning* tuning_for(std::string_view name,int native_class) noexcept
	{
		for (const auto& tuning:weapon_tunings)
			if ((tuning.native_class<0 || tuning.native_class==native_class) && native_weapon_family(name,tuning.family))
				return &tuning;
		return nullptr;
	}
	inline float weapon_scale(std::string_view name,int native_class) noexcept
	{
		const auto* tuning=tuning_for(name,native_class);
		return tuning ? tuning->scale : 1.f;
	}
}
