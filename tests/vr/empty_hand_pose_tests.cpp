#include "component/vr/gameplay/empty_hand_pose.hpp"
#include <iostream>
#include <limits>

namespace empty = vr::gameplay::hands::empty_hand;
namespace hi = vr::gameplay::hand_interaction;
namespace hands = vr::gameplay::hands;
namespace weapons = vr::gameplay::weapons;
using hand = vr::hand;
using frame = vr::controller_input::frame;

namespace
{
	frame input()
	{
		frame f{}; f.sequence = 1; f.reference_generation = 1; f.focused = true;
		f.sampled_at = vr::controller_input::clock::time_point{} + std::chrono::seconds(1);
		for (unsigned h = 0; h < 2; ++h)
		{
			f.grip[h].valid = true;
			f.squeeze[h].active = f.trigger[h].active = true;
		}
		return f;
	}
	void advance(frame& f, double seconds)
	{
		++f.sequence;
		f.sampled_at += std::chrono::duration_cast<vr::controller_input::clock::duration>(std::chrono::duration<double>(seconds));
	}
	float difference(vr::gameplay::hands::quat a, vr::gameplay::hands::quat b)
	{
		float d{}; for (unsigned i = 0; i < 4; ++i) d += a[i] * b[i];
		return 1 - std::abs(d);
	}
	struct fixture
	{
		vr::gameplay::hands::rig rig{};
		vr::gameplay::hands::pose_library library{};
		std::array<vr::gameplay::hands::bone, 12> bones{};
		std::array<vr::gameplay::hands::joint_pose, 8> joints{{
			{"j_thumb_le_0", {0, 0, .70710678f, .70710678f}},
			{"j_index_le_0", {0, 0, .70710678f, .70710678f}},
			{"j_index_le_1", {0, 0, .70710678f, .70710678f}},
			{"j_mid_le_0", {0, 0, .70710678f, .70710678f}},
			{"j_thumb_ri_0", {0, 0, 0, 1}},
			{"j_index_ri_0", {0, 0, 0, 1}},
			{"j_index_ri_1", {0, 0, 0, 1}},
			{"j_mid_ri_0", {0, 0, 0, 1}}}};
		vr::gameplay::hands::pose_schema profile{};
		fixture()
		{
			profile.fingers = joints; rig.count = int(bones.size()); rig.parent.fill(-1);
			rig.arms[0].wrist = 1; rig.arms[1].wrist = 6; rig.weapon_bones[11] = true;
			rig.parent[1] = rig.parent[6] = rig.parent[11] = 0;
			rig.parent[2] = rig.parent[3] = rig.parent[5] = 1; rig.parent[4] = 3;
			rig.parent[7] = rig.parent[8] = rig.parent[10] = 6; rig.parent[9] = 8;
			library.finger.fill(-1); library.opposite.fill(-1); library.valid = true;
			library.mirror_basis.fill({0, 0, 0, 1});
			const std::array<int, 8> indices{2, 3, 4, 5, 7, 8, 9, 10};
			for (unsigned i = 0; i < indices.size(); ++i)
			{
				const auto bone = indices[i]; library.finger[bone] = int(i);
				library.opposite[bone] = indices[(i + 4) % 8];
				library.rest_local[bone] = {{1, 0, 0}, {0, 0, 0, 1}};
			}
			for (int i = 0; i < rig.count; ++i)
			{
				bones[i].rotation = {0, 0, 0, 1}; bones[i].weight = 2;
				if (rig.parent[i] >= 0) bones[i].position = vr::gameplay::hands::add(bones[rig.parent[i]].position, {1, 0, 0});
			}
		}
	};
}

int main()
{
	int checks{}, failures{};
	const auto check = [&](bool passed, const char* message)
	{ ++checks; if (!passed) { ++failures; std::cerr << "FAIL: " << message << '\n'; } };
	check(empty::select(false, false) == empty::gesture::relaxed, "no input selects relaxed hand");
	check(empty::select(true, false) == empty::gesture::point, "Grip selects extended index");
	check(empty::select(false, true) == empty::gesture::pinch, "Trigger selects thumb/index pinch");
	check(empty::select(true, true) == empty::gesture::fist, "Grip and Trigger select fist");
	for(const auto actor:{hand::left,hand::right})
	{
		fixture f;const int h=int(actor),other=1-h,wrist=f.rig.arms[h].wrist;
		const auto opposite=f.bones[f.rig.arms[other].wrist];const auto weapon=f.bones[11];const auto position=f.bones[wrist].position;
		const auto tracked=vr::gameplay::hands::normalize(vr::gameplay::hands::quat{.2f,.3f,.1f,.8f}),neutral=vr::gameplay::hands::normalize(vr::gameplay::hands::quat{.1f,.6f,.2f,.7f});
		f.bones[wrist].rotation={1,0,0,0};
		check(empty::orient_wrist(f.rig,actor,{},false,tracked,neutral,f.bones),"free wrist orientation replaces unrelated weapon basis");
		check(difference(f.bones[wrist].rotation,vr::gameplay::hands::multiply(tracked,neutral))<.00001f && f.bones[wrist].position==position,
			"neutral wrist follows only its own tracking without moving the hand");
		check(f.bones[f.rig.arms[other].wrist].rotation==opposite.rotation && f.bones[11].rotation==weapon.rotation,"neutral wrist does not rotate other hand or weapon");
		const auto before=f.bones[wrist].rotation;
		check(!empty::orient_wrist(f.rig,actor,{},true,{0,0,0,1},{0,0,0,1},f.bones) && f.bones[wrist].rotation==before,"held weapon keeps its constrained wrist");
		hi::pose_plan plan;plan.driver={hi::domain::world,{53,7},0,0};
		check(!empty::orient_wrist(f.rig,actor,plan,false,{0,0,0,1},{0,0,0,1},f.bones) && f.bones[wrist].rotation==before,"active world interaction keeps wrist authority");
		plan={};plan.knife_attachment=true;
		check(!empty::orient_wrist(f.rig,actor,plan,false,{0,0,0,1},{0,0,0,1},f.bones),"knife co-grasp is not treated as an empty wrist");
	}
	for (const auto h : {hand::left, hand::right})
	{
		for (const int fps : {60, 72, 90, 120, 144})
		{
			auto f = input(); empty::controller control; (void)control.update(f, h);
			f.squeeze[unsigned(h)].down = f.trigger[unsigned(h)].down = true;
			empty::presentation p; float last{};
			for (int i = 0; i < fps; ++i)
			{
				advance(f, 1.0 / fps); p = control.update(f, h);
				check(p.enabled && p.fingers.index >= last && p.fingers.index < 1, "fist approaches target without snapping or overshoot");
				const auto repeat = control.update(f, h);
				check(repeat.fingers.index == p.fingers.index, "second eye/model cannot advance the same input twice");
				last = p.fingers.index;
				const float expected = 1 - std::exp(-.69314718056f * float(i + 1) / (float(fps) * empty::controller::half_life_seconds));
				check(std::abs(p.fingers.index - expected) < .000003f, "response time is independent of headset refresh rate");
			}
			f.trigger[unsigned(h)].down = false; advance(f, 1.0 / fps);
			p = control.update(f, h);
			check(p.requested == empty::gesture::point && p.fingers.index > 0 && p.fingers.index < last && p.fingers.remaining > .99f,
				"fist reverses smoothly to point while other fingers stay curled");
			f.squeeze[unsigned(h)].down = false; f.trigger[unsigned(h)].down = true; advance(f, 1.0 / fps);
			const auto pinch = control.update(f, h);
			check(pinch.fingers.index > p.fingers.index && pinch.fingers.remaining < p.fingers.remaining,
				"point can reverse to pinch without passing through a snapped preset");
		}
		{
			auto f = input(); empty::controller control; (void)control.update(f, h);
			f.squeeze[unsigned(h)].down = f.trigger[unsigned(h)].down = true;
			advance(f, .02); auto p = control.update(f, h);
			const auto before = p.fingers.index;
			f.grip[unsigned(h)].valid = false; advance(f, .02); p = control.update(f, h);
			check(!p.enabled && p.fingers.index == before, "tracking loss suspends output without changing articulation");
			f.grip[unsigned(h)].valid = true; advance(f, 5); p = control.update(f, h);
			check(p.enabled && p.fingers.index == before, "tracking recovery ignores time while unseen");
			advance(f, .01); p = control.update(f, h);
			check(p.fingers.index > before && p.fingers.index < .6f, "recovered tracking resumes smoothly");
			const auto prior = p.fingers.index; ++f.reference_generation; advance(f, .01);
			check(control.update(f, h).fingers.index == prior, "recenter does not snap finger articulation");
			advance(f, 2); check(control.update(f, h).fingers.index == prior, "long render stalls rebase animation time");
			f.focused = false; advance(f, .01); check(!control.update(f, h).enabled, "unfocused input cannot present a default gesture");
			f.focused = true; f.trigger[unsigned(h)].active = f.squeeze[unsigned(h)].active = false; advance(f, .01);
			check(control.update(f, h).requested == empty::gesture::relaxed, "inactive digital inputs do not invent pressed fingers");
		}
	}
	{
		fixture model;
		const auto original = model.bones;
		const empty::presentation point{true, empty::gesture::point, empty::target(empty::gesture::point)};
		check(empty::apply(model.rig, model.library, model.profile, hand::left, point, model.bones), "left default applies to real joint chain");
		check(model.bones[3].rotation == original[3].rotation && difference(model.bones[5].rotation, original[5].rotation) > .2f,
			"point extends index while curling the remaining fingers");
		check(model.bones[1].rotation == original[1].rotation && model.bones[1].position == original[1].position &&
			model.bones[11].rotation == original[11].rotation && model.bones[11].position == original[11].position,
			"default fingers never change wrist ownership or weapon bones");
		check(empty::apply(model.rig, model.library, model.profile, hand::right, point, model.bones) &&
			model.bones[10].rotation[2] < -.7f, "right hand mirrors left witnessed grasp instead of copying opposite pose");
		const auto prior = model.bones;
		auto invalid = point; invalid.fingers.thumb = std::numeric_limits<float>::quiet_NaN();
		check(!empty::apply(model.rig, model.library, model.profile, hand::left, invalid, model.bones) &&
			model.bones[2].rotation == prior[2].rotation, "invalid animation input cannot partially mutate fingers");
	}
	for (const auto provider : {hi::domain::knife, hi::domain::magazine, hi::domain::underbarrel, hi::domain::carry, hi::domain::world, hi::domain::tactical})
	{
		fixture model; auto f = input(); empty::pose_controller poses;
		hi::pose_plan plan; plan.driver = {provider, {53, 7}, 0, 0};
		const vr::gameplay::hands::quat held_rotation{0, 0, .5f, .8660254f};
		vr::gameplay::hands::bone before = model.bones[3]; model.bones[3].rotation = held_rotation;
		check(!poses.present(model.rig, model.library, model.profile, f, hand::left, plan, false, model.bones) &&
			model.bones[3].rotation == held_rotation, "entity pose owns the hand for every provider");
		advance(f, 1.0 / 90); model.bones[3] = before;
		check(poses.present(model.rig, model.library, model.profile, f, hand::left, {}, false, model.bones) &&
			difference(model.bones[3].rotation, held_rotation) < .000001f, "first empty frame starts from last visible entity fingers");
		float last = model.bones[3].rotation[2];
		for (int i = 0; i < 30; ++i)
		{
			advance(f, 1.0 / 90);
			(void)poses.present(model.rig, model.library, model.profile, f, hand::left, {}, false, model.bones);
			const float current = model.bones[3].rotation[2];
			check(current < last && current > 0, "released entity fingers approach relaxed hand continuously"); last = current;
		}
		check(last < .005f, "released entity settles into relaxed hand within a short natural transition");
		const auto prior = model.bones[3].rotation;
		check(!poses.present(model.rig, model.library, model.profile, f, hand::left, {}, true, model.bones) &&
			model.bones[3].rotation == prior, "explicit mechanical pose mask also outranks empty defaults");
	}
	{
		fixture model; auto f = input(); empty::pose_controller poses;
		const vr::gameplay::hands::quat visible{0, 0, .5f, .8660254f}; model.bones[3].rotation = visible;
		(void)poses.present(model.rig, model.library, model.profile, f, hand::left, {}, true, model.bones);
		model.bones[3].rotation = {0, 0, 0, 1};
		check(poses.present(model.rig, model.library, model.profile, f, hand::left, {}, false, model.bones) &&
			difference(model.bones[3].rotation, visible) < .000001f, "occupied-to-empty within one input snapshot retains observed pose");
		advance(f, .01); (void)poses.present(model.rig, model.library, model.profile, f, hand::left, {}, false, model.bones);
		const auto blended = model.bones[3].rotation;
		// A new receiver can change rig storage/lengths while retaining the same
		// semantic authored profile. A second draw must not capture its native
		// pose again and restart the already active hand transition.
		auto second_rig = model.rig; auto second_library = model.library; auto second_bones = model.bones;
		second_library.rest_local[3].position = {2, 0, 0}; second_bones[3].rotation = {0, 0, 0, 1};
		check(poses.present(second_rig, second_library, model.profile, f, hand::left, {}, false, second_bones) &&
			difference(second_bones[3].rotation, blended) < .000001f &&
			std::abs(vr::gameplay::hands::length(vr::gameplay::hands::sub(second_bones[3].position, second_bones[1].position)) - 2) < .000001f,
			"same-snapshot alternate rig preserves transition progress and its own bone lengths");
		const vr::gameplay::hands::quat rest_delta{0, 0, .17364818f, .98480775f};
		second_library.rest_local[3].rotation = rest_delta;
		(void)poses.present(second_rig, second_library, model.profile, f, hand::left, {}, false, second_bones);
		check(difference(second_bones[3].rotation, vr::gameplay::hands::multiply(rest_delta, blended)) < .000001f,
			"model replacement preserves articulation relative to its new bind frame");
		second_rig.parent[3] = 999;
		check(!poses.present(second_rig, second_library, model.profile, f, hand::left, {}, false, second_bones),
			"malformed rig is rejected before parent traversal");
	}
	std::cout << "empty hand pose: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
