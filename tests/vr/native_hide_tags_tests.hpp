#pragma once
#include "component/vr/gameplay/native_hide_tags.hpp"

namespace native_hide_tags_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons;
		// Captured m4m203_eotech: receiver-local sight off/on at 18/19,
		// script-string tokens 6506/5004; native global on=87, mask[2]=0x100.
		for (unsigned hands : {68u,75u,96u})
		{
			std::array<std::uint32_t,254> bones{};
			const auto count=hands+34;
			for (unsigned i=0;i<count;++i) bones[i]=i+100;
			bones[hands+18]=6506;bones[hands+19]=5004;
			const std::array<std::uint32_t,32> optic{5004},iron{6506};
			part_mask hidden{};
			check(bind_native_hide_tags({bones.data(),count},optic,hidden), "bind native optic hide tags to assembled M4");
			const auto hidden_at=[&](unsigned i){return bool(hidden[i/32]&(0x80000000u>>(i%32)));};
			check(hidden_at(hands+19) && !hidden_at(hands+18) && !hidden_at(hands+24) && !hidden_at(13),
				"optic hides upright sight while folded sight, optic attachment and arms remain visible");
			if (hands==68) check(hidden[2]==0x100, "reproduces live native M4 mask exactly");
			check(bind_native_hide_tags({bones.data(),count},iron,hidden) && hidden_at(hands+18) && !hidden_at(hands+19),
				"shared receiver switched to bare irons replaces old optic mask");
			part_mask other{};
			check(bind_native_hide_tags({bones.data(),count},optic,other) && other!=hidden,
				"two held M4 variants retain independent visibility");
			const std::array<std::uint32_t,2> absent{0,99999};
			check(bind_native_hide_tags({bones.data(),count},absent,hidden) && hidden==part_mask{},
				"unused and absent optional hide tags cannot hide unrelated geometry");
			check(bind_native_hide_tags({bones.data(),count},{},hidden), "weapon without hide tags is valid");
		}
		std::array<std::uint32_t,255> too_many{};too_many.fill(1);
		std::array<std::uint32_t,33> too_many_tags{};
		part_mask out{};out[0]=1;const auto unchanged=out;
		check(!bind_native_hide_tags(too_many,{},out) && !bind_native_hide_tags({too_many.data(),254},too_many_tags,out) &&
			!bind_native_hide_tags({}, {},out) && out==unchanged, "invalid object bounds do not partially publish a mask");
		const std::array<std::uint32_t,3> missing_name{1,0,2};
		check(!bind_native_hide_tags(missing_name,{},out), "missing bone identity rejects visibility binding");
		const std::array<std::uint32_t,3> aliases{1,2,2};const std::array<std::uint32_t,1> tag{2};
		check(bind_native_hide_tags(aliases,tag,out) && out[0]==0x60000000,
			"duplicate receiver/attachment tag aliases share native hide selection");
	}
}
