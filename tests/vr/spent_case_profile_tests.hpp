#pragma once
#include "component/vr/gameplay/spent_case_profiles.hpp"

template <class Check> void spent_case_profile_tests(const Check& check)
{
	using namespace vr::gameplay::weapons::spent_case;
	struct selection
	{
		std::string_view name;
		unsigned variant;
	};
	const selection cases[]{
	    {"cheytac", 1},
	    {"cheytac_silencer_desert", 2},
	    {"barrett", 3},
	    {"barrett_thermal", 3},
	    {"deserteagle", 4},
	    {"deserteagle_gold", 5},
	    {"wa2000", 6},
	    {"wa2000_thermal", 6},
	    {"m14", 7},
	    {"m14_scoped", 7},
	    {"m21_soap", 7},
	    {"m21_scoped_cloth_silenced", 7},
	    {"m14ebr_thermal", 7},
	    {"m21_scoped_arctic_silenced", 8},
	    {"m14_arctic", 8},
	};
	for (const auto& item : cases)
		check(variant_for(item.name) == item.variant,
		      "native weapon family and camouflage select the matching saved case identity");
	for (const auto name : {"m14ebr",
	                        "m14ebr_thermal_extra",
	                        "deserteagle_fake",
	                        "m210",
	                        "barretts",
	                        "m82_bipod_stand_thermal",
	                        "m4"})
		check(!variant_for(name),
		      "nearby names, mounted helpers and unrelated weapons cannot borrow a case override");
	check(variant_for("m21_soap", sources[7].source) == 8 &&
	          variant_for("m21_scoped_arctic_silenced", sources[6].source) == 7 &&
	          variant_for("deserteagle", sources[4].source) == 5,
	      "an explicit loaded receiver takes priority over name-derived camouflage");
	check(!variant_for("m14", sources[3].source) && !variant_for("wa2000", "unreviewed_receiver"),
	      "an explicit incompatible receiver cannot silently fall back to a guessed case");
	check(std::string_view(sources[0].source) == "h2_viewmodel_cheytac_base" &&
	          std::string_view(sources[1].source) == "h2_viewmodel_cheytac_base_desert" &&
	          std::string_view(sources[2].source) == "h2_viewmodel_m82_base",
	      "extending the case catalog preserves the already-deployed checkpoint identities");
}
