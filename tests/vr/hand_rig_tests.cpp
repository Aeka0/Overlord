#include "component/vr/gameplay/hands/rig_builder.hpp"
#include "hand_rig_cache_tests.hpp"
#include <iostream>
#include <limits>

int main()
{
	using namespace vr::gameplay::hands;
	const std::array<vec, 3> body_axis{{{1,0,0},{0,1,0},{0,0,1}}};
	int failures{};
	const auto check = [&](bool okay, const char* name) {
		if (!okay)
		{
			++failures;
			std::cerr << "FAIL: " << name << '\n';
		}
	};
	hand_rig_cache_tests::run(check);
	// Semantic/layout witnesses from the live objects, with synthetic poses.
	// No exported game mesh or animation is included in these fixtures.
	std::array<bone_definition, 96> bones{};
	for (int i = 0; i < 68; ++i)
		bones[i] = {"helper", i == 0 ? -1 : 0};
	bones[7] = {"tag_torso", 0};
	bones[8] = {"j_shoulder_le", 7};
	bones[9] = {"j_shoulder_ri", 7};
	bones[13] = {"tag_weapon", 7};
	bones[14] = {"j_elbow_le", 8};
	bones[15] = {"j_elbow_ri", 9};
	bones[24] = {"j_wrist_le", 14};
	bones[25] = {"j_wrist_ri", 15};
	bones[28] = {"finger", 24};
	for (int i = 68; i < 96; ++i)
		bones[i] = {"part", 68};
	bones[68] = {"j_gun", 13};
	bones[68].bind = {{0, 0, 0, 1}, {0, 0, 0}, 2};
	bones[78] = {"tag_flash", 68, {{0, 0, 0, 1}, {25, 0, 2}, 2}};
	bones[76] = {"tag_cover", 68};
	bones[79] = {"tag_foregrip", 68};
	bones[81] = {"tag_laser",68,{{0,0,0,1},{16,2,3},2}};
	bones[88] = {"tag_silencer", 68};
	bones[92] = {"tag_foregrip", 79};
	bones[93] = {"tag_cover", 76};
	bones[94] = {"tag_silencer", 88};
	bones[95] = {"tag_flash_silenced", 94};
	const std::array<model_definition, 5> m4{{{"viewhands_us_army", 0, 68},
											  {"h2_viewmodel_m4_base", 68, 24},
											  {"attach_h2_mp5k_foregrip_vm", 92, 1},
											  {"attach_h2_m4_cover_vm", 93, 1},
											  {"attach_h2_silencer_01_vm", 94, 2}}};
	auto resolved = resolve_rig(m4, bones);
	check(!resolved.rejection && resolved.contract == "single-root-forward-muzzle", "five-model M4 resolves");
	check(resolved.layout.laser==81 && resolved.layout.laser!=resolved.layout.muzzle,
		"laser uses the receiver's authored emitter rather than the muzzle");
	{
		auto repeated=bones;repeated[92]={"tag_laser",81};
		check(resolve_rig(m4,repeated).layout.laser==81,"attachment socket aliases cannot replace the receiver laser tag");
		repeated[82]={"tag_laser",68};const auto ambiguous=resolve_rig(m4,repeated);
		check(!ambiguous.rejection && ambiguous.layout.laser==-1,"ambiguous optional laser disables the beam without rejecting the weapon rig");
	}
	check(resolved.layout.count == 96 && resolved.layout.arms[0].wrist == 24 &&
			  resolved.layout.arms[1].wrist == 25,
		  "live M4 semantic indices");
	for (int i = 0; i < 96; ++i)
		check(resolved.layout.weapon_bones[i] == (i >= 68), "M4 attachment ownership");
	const std::array<model_definition, 2> m9{{{"viewhands_us_army", 0, 68}, {"wpn_h1_pst_m9_vm", 68, 9}}};
	std::array<bone_definition, 77> pistol_bones{};
	std::copy_n(bones.begin(), 77, pistol_bones.begin());
	pistol_bones[73] = {"tag_flash", 68, {{0, 0, 0, 1}, {10, 0, 1}, 2}};
	const auto pistol = resolve_rig(m9, pistol_bones);
	check(!pistol.rejection && pistol.contract == "single-root-forward-muzzle" &&
			  pistol.layout.arms[0].wrist == 24,
		  "M9 topology regression");
	std::array<bone, 96> native{};
	for (int i = 0; i < 96; ++i)
		native[i] = {{0, 0, 0, 1}, {static_cast<float>(i % 5), 0, 0}, 2};
	native[8].position = {0, 2, 0};
	native[14].position = {3, 2, -4};
	native[24].position = {6, 2, 0};
	native[9].position = {0, -2, 0};
	native[15].position = {3, -2, -4};
	native[25].position = {6, -2, 0};
	native[92] = native[79];
	native[93] = native[76];
	native[94] = native[88];
	std::array<bone, 96> solved{};
	std::array<bool, 2> limited{};
	const float h = std::sqrt(0.5f);
	const std::array<anchor, 2> targets{{{{4, 4, 1}}, {{5, -3, 1}, {0, 0, h, h}}}};
	const std::array<vec, 2> shoulders{vec{-1, 3, -2}, vec{-1, -3, -2}};
	check(solve(resolved.layout, native, targets, shoulders, body_axis, 1, solved, limited), "M4 pose solves with relocated shoulders");
	const auto close = [](vec a, vec b) { return length(sub(a, b)) < 0.002f; };
	{
		const std::span<const model_definition> hands_model(m4.data(),1);
		const std::span<const bone_definition> hand_bones(bones.data(),68);
		const auto empty=resolve_rig(hands_model,hand_bones,rig_kind::hands_only);
		check(!empty.rejection && empty.layout.gun==-1 && empty.layout.muzzle==-1 &&
			empty.layout.rear_grip_wrist==-1,"empty hands resolve without a firearm or controlling hand");
		check(resolve_rig(hands_model,hand_bones).rejection,"firearm path still requires firearm geometry");
		check(resolve_rig(m9,pistol_bones,rig_kind::hands_only).rejection,"empty presentation rejects a hidden firearm");
		std::array<bone,68> empty_source{}, empty_result{};
		std::copy_n(native.begin(),68,empty_source.begin());
		empty_source[24].rotation={h,0,0,h};
		empty_source[28].position=add(empty_source[24].position,rotate(empty_source[24].rotation,{2,0,0}));
		empty_source[28].rotation=empty_source[24].rotation;
		check(solve_arms(empty.layout,empty_source,targets,shoulders,body_axis,empty_result,limited),
			"both empty arms solve with independent anatomical wrist targets");
		for (int hand=0;hand<2;++hand)
		{
			const auto wrist=empty.layout.arms[hand].wrist;
			check(close(empty_result[wrist].position,targets[hand].position),"empty wrist reaches its own controller");
			check(close(rotate(empty_result[wrist].rotation,{0,1,0}),rotate(targets[hand].rotation,{0,1,0})),
				"empty wrist orientation does not depend on a gun basis");
		}
		check(close(sub(empty_result[28].position,empty_result[24].position),rotate(targets[0].rotation,{2,0,0})),
			"empty pose preserves this model's relaxed finger contact");
		check(empty_result[13].position==empty_source[13].position,"empty arms do not move a weapon attachment tag");
		const auto before=empty_result;
		auto bad_targets=targets; bad_targets[1].position[0]=std::numeric_limits<float>::quiet_NaN();
		check(!solve_arms(empty.layout,empty_source,bad_targets,shoulders,body_axis,empty_result,limited) &&
			empty_result[24].position==before[24].position && empty_result[25].position==before[25].position,
			"bad empty-hand tracking cannot partially commit either arm");
		auto malformed_hands=bones; malformed_hands[24].parent=15;
		check(resolve_rig(hands_model,{malformed_hands.data(),68},rig_kind::hands_only).rejection,
			"crossed empty-hand skeleton ownership is rejected");
	}
	for (int i = 68; i < 96; ++i)
		check(close(sub(solved[i].position, solved[25].position),
					rotate(targets[1].rotation, sub(native[i].position, native[25].position))),
			  "all weapon and attachment bones follow holding hand exactly once");
	check(close(solved[92].position, solved[79].position) && close(solved[93].position, solved[76].position) &&
			  close(solved[94].position, solved[88].position),
		  "native duplicate anchors stay coincident");
	auto changed = m4;
	// Structural compatibility only: these are deliberately synthetic arm names,
	// not evidence that a shipped ghillie/suit mesh has been visually accepted.
	for (auto name : {"viewhands_test_141", "viewhands_test_ghillie", "viewhands_test_suit",
					  "viewmodel_base_viewhands", "custom_hand_model"})
	{
		changed[0].name = name;
		check(!resolve_rig(changed, bones).rejection, "same semantic hand skeleton is reusable");
	}
	changed = m4;
	changed[1].name = "unverified_ak_model";
	check(!resolve_rig(changed, bones).rejection, "unnamed weapon contract resolves without a name whitelist");
	auto malformed = bones;
	malformed[94].parent = -1;
	check(resolve_rig(m4, malformed).rejection, "unattached silencer rejected");
	malformed = bones;
	malformed[95].parent = 95;
	check(resolve_rig(m4, malformed).rejection, "cyclic attachment rejected");
	malformed = bones;
	malformed[24].name = "absent";
	check(resolve_rig(m4, malformed).rejection, "missing wrist rejected");
	malformed = bones;
	malformed[20].name = "j_wrist_le";
	check(resolve_rig(m4, malformed).rejection, "ambiguous wrist rejected");
	malformed = bones;
	malformed[92].name = "j_gun";
	check(resolve_rig(m4, malformed).rejection, "unprofiled secondary gun rejected");
	changed = m4;
	changed[2].name = "wpn_h1_pst_m9_vm";
	check(!resolve_rig(changed, bones).rejection, "model labels do not override actual attachment ownership");
	changed = m4;
	changed[3].begin = 92;
	check(resolve_rig(changed, bones).rejection, "overlapping model ranges rejected");

	// A sleeve inserted between receiver and attachments must follow its arm,
	// not the gun. Appended model order is not a weapon ownership rule.
	std::array<bone_definition, 97> mixed{};
	std::copy_n(bones.begin(), 92, mixed.begin());
	mixed[92] = {"sleeve_helper", 24};
	for (int i = 92; i < 96; ++i)
	{
		mixed[i + 1] = bones[i];
		if (mixed[i + 1].parent >= 92)
			++mixed[i + 1].parent;
	}
	const std::array<model_definition, 6> mixed_models{
		{m4[0], m4[1], {"sleeve", 92, 1}, {m4[2].name, 93, 1}, {m4[3].name, 94, 1}, {m4[4].name, 95, 2}}};
	const auto with_sleeve = resolve_rig(mixed_models, mixed);
	check(!with_sleeve.rejection && !with_sleeve.layout.weapon_bones[92] && with_sleeve.layout.weapon_bones[96],
		  "interleaved sleeve does not become a weapon attachment");
	std::array<bone, 97> mixed_native{}, mixed_solved{};
	std::copy_n(native.begin(), 92, mixed_native.begin());
	mixed_native[92] = native[28];
	std::copy(native.begin() + 92, native.end(), mixed_native.begin() + 93);
	check(solve(with_sleeve.layout, mixed_native, targets, shoulders, body_axis, 1, mixed_solved, limited),
		  "mixed-owner model pose solves");
	check(close(sub(mixed_solved[92].position, mixed_solved[24].position),
				sub(mixed_native[92].position, mixed_native[24].position)),
		  "appended sleeve follows left wrist rather than right gun rotation");
	check(resolve_rig({}, {}).rejection, "empty rig rejected");

	// Real AK composition metadata, with synthetic bind/animation transforms.
	std::array<bone_definition, 99> ak{};
	std::copy_n(bones.begin(), 68, ak.begin());
	for (int i = 68; i < 99; ++i)
		ak[i] = {"part", 68};
	ak[68] = bones[68];
	ak[80] = {"tag_cover", 68};
	ak[88] = {"tag_thermal_scope", 68};
	ak[82] = {"tag_flash", 68, {{0, 0, 0, 1}, {25.383888f, -0.505405f, 3.747067f}, 2}};
	ak[93] = {"tag_cover", 80};
	ak[94] = {"tag_thermal_scope", 88};
	ak[95] = {"tag_scope_ads_off", 94};
	ak[96] = {"tag_scope_ads_on", 94};
	ak[97] = {"tag_reticle_attach", 96};
	ak[98] = {"tag_reticle_thermal_scope", 97};
	const std::array<model_definition, 4> ak_models{{{"viewhands_us_army", 0, 68},
													 {"h2_viewmodel_ak47_base", 68, 25},
													 {"attach_h2_ak47_cover_vm", 93, 1},
													 {"attach_h2_thermal_scope_2_vm", 94, 5}}};
	const auto rifle = resolve_rig(ak_models, ak);
	check(!rifle.rejection && rifle.muzzle_bone == 82 && rifle.layout.weapon_bones[98],
		  "AK thermal reticle belongs to receiver");
	std::array<bone, 99> ak_native{}, ak_solved{};
	std::copy_n(native.begin(), 68, ak_native.begin());
	for (int i = 68; i < 99; ++i)
		ak_native[i] = {{0, 0, 0, 1}, {static_cast<float>(i % 5), 0, 0}, 2};
	check(solve(rifle.layout, ak_native, targets, shoulders, body_axis, 1, ak_solved, limited), "AK uses the shared shoulder/pose solver");
	for (int i = 68; i < 99; ++i)
		check(close(sub(ak_solved[i].position, ak_solved[25].position),
					rotate(targets[1].rotation, sub(ak_native[i].position, ak_native[25].position))),
			  "AK receiver, cover, optic and reticle transform once");

	malformed = bones;
	malformed[78].name = "no_muzzle";
	check(resolve_rig(m4, malformed).rejection, "grenade/C4 with j_gun but no muzzle rejected");
	malformed = bones;
	malformed[78].bind.rotation = {0, 0, h, h};
	check(resolve_rig(m4, malformed).rejection, "shield-like 90 degree muzzle rejected even with M4 model name");
	malformed = bones;
	malformed[78].bind.rotation = {0, -0.0897522f, 0, 0.99594116f};
	check(resolve_rig(m4, malformed).rejection, "Javelin-like muzzle tilt needs dedicated adapter");
	malformed = bones;
	malformed[78].bind = {};
	check(resolve_rig(m4, malformed).rejection, "missing bind evidence rejected");
	malformed = bones;
	malformed[78].bind.position = {-25, 0, 0};
	check(resolve_rig(m4, malformed).rejection, "rear-facing muzzle placement rejected");
	malformed = bones;
	malformed[78].bind.rotation = {0, 0, std::numeric_limits<float>::quiet_NaN(), 1};
	check(resolve_rig(m4, malformed).rejection, "nonfinite muzzle rejected");
	// A muzzle can be under a moving barrel. Bind transforms are model-global;
	// do not confuse its parent-local rotation with its model-space forward axis.
	malformed = bones;
	malformed[78].parent = 74;
	check(!resolve_rig(m4, malformed).rejection, "nested barrel muzzle supported");
	const quat common_basis{0, h, 0, h};
	malformed[68].bind.rotation = common_basis;
	malformed[78].bind.rotation = common_basis;
	malformed[78].bind.position = rotate(common_basis, {25, 0, 2});
	check(!resolve_rig(m4, malformed).rejection, "bind comparison is root-relative not world-relative");
	std::cout << "vr-hand-rig-tests: " << (failures ? "FAIL" : "PASS") << '\n';
	return failures ? 1 : 0;
}
