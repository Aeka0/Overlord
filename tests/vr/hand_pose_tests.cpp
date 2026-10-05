#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/hands/pose_solver.hpp"
#include "component/vr/gameplay/shoulder_anchors.hpp"
#include "component/vr/gameplay/hands/position_offset.hpp"
#include "wrist_alignment_tests.hpp"
#include "forearm_twist_tests.hpp"
#include "arm_orientation_tests.hpp"
#include "body_pose_tests.hpp"
#include "horizontal_heading_tests.hpp"
#include "scripted_arms_tests.hpp"
#include "free_climb_tests.hpp"
#include "ladder_tests.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include <cstring>
#include <iostream>
#include <limits>

int main()
{
	using namespace vr::gameplay::hands;
	const std::array<vec, 3> body_axis{{{1,0,0},{0,1,0},{0,0,1}}};
	constexpr position_offsets physical_baseline{-.02f,.12f,-.05f};
	int failures{};
	const auto check = [&](bool value, const char* name) {
		if (!value)
		{
			std::cerr << "FAIL: " << name << '\n';
			++failures;
		}
	};
	const auto close = [](vec a, vec b) { return length(sub(a, b)) < 0.002f; };
	forearm_twist_tests::run(check);
	arm_orientation_tests::run(check);
	body_pose_tests::run(check);
	horizontal_heading_tests::run(check);
	scripted_arms_tests::run(check);
    free_climb_tests::run(check);
    ladder_tests::run(check);
	{
		vec left{}, right{};
		const position_offsets small{.01f,.02f,.02f};
		check(offset_wrist({1,2,3}, body_axis, 100, 0, small, left) && close(left, {-1,1,5}),
			"left wrist moves inward/back/up in grip-local coordinates");
		check(offset_wrist({1,2,3}, body_axis, 100, 1, small, right) && close(right, {-1,3,5}),
			"right wrist inward offset mirrors left");
		const std::array<vec,3> yaw{{{0,1,0},{-1,0,0},{0,0,1}}};
		check(offset_wrist({1,2,3}, yaw, 100, 0, small, left) && close(left, {2,0,5}),
			"wrist offset follows grip yaw");
		const std::array<vec,3> pitch{{{0,0,1},{0,1,0},{-1,0,0}}};
		check(offset_wrist({1,2,3}, pitch, 100, 0, small, left) && close(left, {-1,1,1}),
			"grip pitch rotates its local offset, not a world-space substitute");
		check(offset_wrist({1,2,3}, body_axis, 40, 0, small, left) && close(left, {.2f,1.6f,3.8f}),
			"wrist offsets respect current world scale");
		check(offset_wrist({1,2,3}, body_axis, 40, 0, {0,0,0}, left) && left == vec{1,2,3},
			"zero compensation restores original wrist position exactly");
		check(offset_wrist({1,2,3}, body_axis, 100, 0, {-.01f,-.02f,-.02f}, left) && close(left, {3,3,1}),
			"signed offsets allow small opposite-direction adjustment");
		const auto prior = left;
		check(!offset_wrist({1,2,3}, body_axis, 100, 2, {}, left) && left == prior,
			"invalid hand leaves destination unchanged");
		check(!offset_wrist({1,2,3}, body_axis, 0, 0, {}, left), "invalid wrist world scale rejected");
		check(offset_wrist({0,0,0}, body_axis, 100, 0, physical_baseline, left) && close(left, {-12,2,-5}),
			"physical baseline applied to left wrist");
		check(offset_wrist({0,0,0}, body_axis, 100, 1, physical_baseline, right) && close(right, {-12,-2,-5}),
			"physical baseline mirrors right wrist");
		check(offset_wrist({0,0,0}, body_axis, 100, 0, {.5f,.5f,.5f}, left) && close(left, {-50,-50,50}),
			"positive half-meter boundary accepted on all axes");
		check(offset_wrist({0,0,0}, body_axis, 100, 1, {-.5f,-.5f,-.5f}, right) && close(right, {50,-50,-50}),
			"negative half-meter boundary accepted on all axes");
		check(offset_wrist({0,0,0}, body_axis, 100, 0, {.16f,.2f,-.3f}, left) && close(left, {-20,-16,-30}),
			"values beyond former limit are actually applied");
		check(!offset_wrist({1,2,3}, body_axis, 100, 0, {.5001f,0,0}, left), "excessive inward offset rejected");
		check(!offset_wrist({1,2,3}, body_axis, 100, 0, {0,-.5001f,0}, left), "excessive back offset rejected");
		check(!offset_wrist({1,2,3}, body_axis, 100, 0, {0,0,.5001f}, left), "excessive up offset rejected");
		check(!offset_wrist({1,2,3}, body_axis, 100, 0,
			{0,std::numeric_limits<float>::quiet_NaN(),0}, left), "nonfinite wrist offset rejected");
		auto invalid_axis = body_axis; invalid_axis[2][2] = -1;
		check(!offset_wrist({1,2,3}, invalid_axis, 100, 0, {}, left), "reflected controller basis rejected");
		check(!offset_wrist({std::numeric_limits<float>::infinity(),2,3}, body_axis, 100, 0, {}, left),
			"nonfinite controller position rejected");
		// Test the runtime adapter, not just a free vector helper. Different grip
		// and aim bases model a fixed device raw-to-tip orientation relationship.
		const float q = std::sqrt(.5f);
		for (const float units : {40.f,100.f})
			for (const vec view : {vec{0,0,0},vec{1000,-2000,500}})
				for (const vec wrist : {vec{1,2,3},vec{-6,7,-8}})
					for (int h = 0; h < 2; ++h)
						for (const quat rotation : {quat{0,0,0,1},quat{q,0,0,q},quat{0,q,0,q},quat{0,0,q,q},quat{0,0,1,0}})
						{
							const vec local_meters{-.12f,h == 0 ? .02f : -.02f,-.05f};
							const auto raw = sub(add(view,wrist),rotate(rotation,scale(local_meters,units)));
							vr::head_pose_bridge::world_pose grip{raw,
								{rotate(rotation,{1,0,0}),rotate(rotation,{0,1,0}),rotate(rotation,{0,0,1})}};
							const auto tip_rotation = multiply(rotation,quat{q,0,0,q});
							vr::head_pose_bridge::world_pose aim{{999,888,777},
								{rotate(tip_rotation,{1,0,0}),rotate(tip_rotation,{0,1,0}),rotate(tip_rotation,{0,0,1})}};
							anchor result;
							check(make_wrist_target(grip,aim,grip,aim,body_axis,view,units,h,physical_baseline,physical_baseline,result) && close(result.position,wrist),
								"physical pivot survives position/scale/rebase/rotation with distinct aim origin and basis");
							check(close(rotate(result.rotation,{0,0,1}),aim.axis[2]), "aim rotates hand but never translates wrist");
						}
		vr::head_pose_bridge::world_pose valid_grip{{1,2,3},body_axis}, invalid_aim = valid_grip;
		invalid_aim.axis[0][0] = std::numeric_limits<float>::quiet_NaN();
		anchor untouched{{5,6,7},{0,0,0,1}};
		check(!make_wrist_target(valid_grip,invalid_aim,valid_grip,valid_grip,body_axis,{},40,0,{},{},untouched) && untouched.position == vec{5,6,7} &&
			untouched.rotation == quat{0,0,0,1}, "invalid aim cannot partially publish wrist target");
		check(!make_wrist_target(valid_grip,valid_grip,valid_grip,valid_grip,body_axis,{std::numeric_limits<float>::infinity(),0,0},40,0,{},{},untouched),
			"nonfinite scene rebase rejected before target publication");
		check(!make_wrist_target(valid_grip,valid_grip,invalid_aim,valid_grip,body_axis,{},40,0,{},{},untouched) &&
			untouched.position==vec{5,6,7},"invalid raw grip basis cannot partially publish a wrist target");
	}
	rig r{};
	r.parent.fill(-1);
	r.count = 13;
	r.weapon_bones[11] = r.weapon_bones[12] = true;
	r.gun = 11;
	r.weapon_tag = 10;
	r.arms = {arm{1, 2, 3}, arm{5, 6, 7}};
	r.rear_grip_wrist = r.arms[1].wrist;
	r.parent[1] = 0;
	r.parent[2] = 1;
	r.parent[3] = 2;
	r.parent[4] = 3;
	r.parent[5] = 0;
	r.parent[6] = 5;
	r.parent[7] = 6;
	r.parent[8] = 7;
	r.parent[9] = 0;
	r.parent[10] = 0;
	r.parent[12] = 11;
	std::array<bone, 13> native{};
	for (auto& b : native)
	{
		b.rotation = {0, 0, 0, 1};
		b.weight = 2;
	}
	native[1].position = {0, 2, 0};
	native[2].position = {3, 2, -4};
	native[3].position = {6, 2, 0};
	native[4].position = {7, 2, 0};
	native[5].position = {0, -2, 0};
	native[6].position = {3, -2, -4};
	native[7].position = {6, -2, 0};
	native[8].position = {7, -2, 0};
	native[9].position = {0, 0, 1};
	native[10].position = {7, -2, 1};
	native[11].position = native[10].position;
	native[12].position = {8, -2, 1};
	const auto original = native;
	std::array<vec, 2> shoulders{native[1].position, native[5].position};
	std::array<anchor, 2> target{{{native[3].position}, {native[7].position}}};
	wrist_alignment_tests(check,r,native,shoulders,target);
	std::array<bone, 13> result{};
	std::array<bool, 2> limited{};
	check(solve(r, native, target, shoulders, body_axis, 1, result, limited), "native anchors accepted");
	for (int i : {0, 1, 3, 4, 5, 7, 8, 9, 10, 11, 12})
		check(close(result[i].position, native[i].position), "rest anchors preserve non-elbow bone positions");
	const auto rest_result = result;
    for(unsigned grasp=0;grasp<2;++grasp)
    {
        bar_grip::binding bar{{0,0,0,1},{.5f,.2f,.1f},true};
        const vec contact{8,grasp?-2.f:2.f,3};
        std::array<anchor,2> contacts{};contacts[grasp]=bar_grip::on_bar(bar,contact,{1,0,0},grasp);
        for(const float angle:{0.f,.7f})
        {
            auto weapon_targets=target;weapon_targets[1-grasp].rotation={0,0,std::sin(angle),std::cos(angle)};
            std::array<bone,13> posed{};
            check(solve(r,native,weapon_targets,shoulders,body_axis,int(1-grasp),posed,limited),"single weapon hand fixture solves");
            const auto weapon_pose=posed;
            check(bar_grip::constrain(r,posed,contacts,1u<<grasp,shoulders,body_axis),"bar constraint applies after either weapon hand is posed");
            const auto& wrist=posed[r.arms[grasp].wrist];
            check(close(add(wrist.position,rotate(wrist.rotation,bar.centre_in_wrist)),contact),
                "ladder palm remains on its fixed contact as the opposite gun rotates");
            for(int i=0;i<r.count;++i)if(r.weapon_bones[i] || !descendant(i,r.arms[grasp].shoulder,r))
                check(posed[i].position==weapon_pose[i].position && posed[i].rotation==weapon_pose[i].rotation,
                    "bar constraint preserves the gun muzzle and opposite arm exactly");
        }
    }
	check(solve(r, native, target, shoulders, body_axis, 0, result, limited), "left rear-grip owner solves");
	check(close(sub(result[11].position, result[3].position), sub(native[11].position, native[7].position)),
		"left owner uses native rear grip offset, not old left foregrip offset");
	check(close(result[7].position, native[7].position), "left owner does not move right wrist");
	check(!solve(r, native, target, shoulders, body_axis, -1, result, limited), "no owner rejects gun pose");
	check(!solve(r, native, target, shoulders, body_axis, 2, result, limited), "invalid owner rejects gun pose");
	check(!limited[0] && !limited[1], "ordinary reach is not clamped");
	{
		// Physical wrist-fixed rotation: the raw tracked origin moves around the
		// wrist. A fixed raw origin with changing rotation is NOT this experiment.
		const float q = std::sqrt(.5f);
		for (int rear = 0; rear < 2; ++rear)
			for (const quat rotation : {quat{0,0,0,1}, quat{q,0,0,q}, quat{0,q,0,q}, quat{0,0,q,q}})
			{
				auto corrected = target;
				for (int h = 0; h < 2; ++h)
				{
					const vec local_meters{-.12f, h == 0 ? .02f : -.02f, -.05f};
					vr::head_pose_bridge::world_pose grip{
						sub(target[h].position, rotate(rotation, scale(local_meters, 40))),
						{rotate(rotation,{1,0,0}),rotate(rotation,{0,1,0}),rotate(rotation,{0,0,1})}};
					const auto aim = grip;
					check(make_wrist_target(grip,aim,grip,aim,body_axis,{},40,h,physical_baseline,physical_baseline,corrected[h]) &&
						close(corrected[h].position, target[h].position), "moving controller origin cancels at stationary wrist");
				}
				check(solve(r, native, corrected, shoulders, body_axis, rear, result, limited),
					"nonzero correction and rotated hands solve together");
				for (int h = 0; h < 2; ++h)
					check(close(result[r.arms[h].wrist].position, corrected[h].position),
						"physical wrist-fixed rotation keeps solved wrist fixed");
				check(close(sub(result[r.gun].position, result[r.arms[rear].wrist].position),
					rotate(rotation, sub(native[r.gun].position, native[r.rear_grip_wrist].position))),
					"gun rotates around corrected rear wrist, not raw point or gun tail");
			}
	}
	target[0].position = {4, 5, 2};
	check(solve(r, native, target, shoulders, body_axis, 1, result, limited), "left hand can move independently");
	check(close(result[3].position, target[0].position), "left wrist reaches controller");
	for (int i : {5, 6, 7, 8, 10, 11, 12})
		check(close(result[i].position, rest_result[i].position), "left hand does not drag right hand or gun");
	const float half = std::sqrt(0.5f);
	target[1] = {{4, -5, 2}, {0, 0, half, half}};
	check(solve(r, native, target, shoulders, body_axis, 1, result, limited), "both hands solve together");
	for (int hand = 0; hand < 2; ++hand)
	{
		const auto a = r.arms[hand];
		check(close(result[a.wrist].position, target[hand].position), "independent wrist target");
		check(std::abs(length(sub(result[a.elbow].position, result[a.shoulder].position)) - 5) < 0.002f,
			  "upper arm length preserved");
		check(std::abs(length(sub(result[a.wrist].position, result[a.elbow].position)) - 5) < 0.002f,
			  "forearm length preserved");
	}
	check(close(sub(result[11].position, result[7].position), {0, 1, 1}),
		  "gun offset follows holding wrist rotation");
	check(close(sub(result[12].position, result[11].position), {0, 1, 0}), "weapon part relative pose preserved");
	check(close(sub(result[8].position, result[7].position), {0, 1, 0}), "native finger pose preserved");
	check(close(rotate(result[11].rotation, {1, 0, 0}), {0, 1, 0}), "barrel follows right aim");
	check(close(result[9].position, native[9].position) && close(result[0].position, native[0].position),
		  "camera and torso tags untouched");
	check(std::memcmp(native.data(), original.data(), sizeof(native)) == 0, "source snapshot immutable");
	const std::array<vec, 3> yaw{{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}};
	check(close(rotate(from_axis(yaw), {1, 0, 0}), {0, 1, 0}), "H2 basis rows convert to active quaternion");
	for (const auto axis : std::array<vec, 3>{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}})
	{
		const quat q{axis[0], axis[1], axis[2], 0};
		const std::array<vec, 3> basis{{rotate(q, {1, 0, 0}), rotate(q, {0, 1, 0}), rotate(q, {0, 0, 1})}};
		check(close(rotate(from_axis(basis), {0.2f, 0.3f, 0.4f}), rotate(q, {0.2f, 0.3f, 0.4f})),
			  "180 degree matrix conversion");
	}
	check(close(rotate(from_to({1, 0, 0}, {-1, 0, 0}), {1, 0, 0}), {-1, 0, 0}), "antiparallel arm rotation");
	target[0].position = {50, 2, 0};
	check(solve(r, native, target, shoulders, body_axis, 1, result, limited) && limited[0], "unreachable controller clamps arm length");
	check(length(sub(result[3].position, native[1].position)) < 12.501f, "stretch stops at 125 percent of native reach");
    {
        auto anchored=result;const auto shoulder=anchored[1],elbow=anchored[2];
        vr::gameplay::hands::pose_math::move_part(r,3,target[0],anchored);
        check(close(anchored[3].position,target[0].position) && close(anchored[1].position,shoulder.position) &&
            close(anchored[2].position,elbow.position),"fixed climbing wrist survives reach limiting without moving shoulder or elbow");
    }
	for (const float distance:{9.f,10.f,10.5f,12.f,12.5f,15.f,9.f})
	{
		auto reaching=target;reaching[0].position=add(shoulders[0],vec{distance,0,0});
		check(solve(r,native,reaching,shoulders,body_axis,0,result,limited),"bounded stretch solves without accumulating state");
		const auto expected=std::min(distance,12.499f);
		check(close(result[3].position,add(shoulders[0],vec{expected,0,0})),"wrist follows controller through extra reach and clamps beyond the extension bound");
		check(std::abs(length(sub(result[2].position,result[1].position))-length(sub(result[3].position,result[2].position)))<.002f,
			"upper and lower arms stretch in the original proportion");
		check(limited[0]==(distance>12.5f),"reach limit reports the remaining error after allowed stretch");
		check(close(sub(result[11].position,result[3].position),rotate(reaching[0].rotation,sub(native[11].position,native[7].position))),
			"weapon stays attached to the stretched wrist");
		if (distance==9) check(std::abs(length(sub(result[2].position,result[1].position))-5)<.002f,"normal reach restores original limb length immediately");
	}
	target[0].position = native[1].position;
	check(solve(r, native, target, shoulders, body_axis, 1, result, limited), "controller at shoulder is finite");
	for (const auto& b : result)
		for (const auto value : b.position)
			check(std::isfinite(value), "finite folded arm");

	// Rotation/translation equivariance catches model/world space confusion.
	target = {anchor{{4, 5, 2}}, anchor{{4, -5, 2}, {0, 0, half, half}}};
	check(solve(r, native, target, shoulders, body_axis, 1, result, limited), "equivariance baseline");
	auto rotated = native;
	auto rotated_target = target;
	const quat global{half, 0, 0, half};
	const vec shift{20, -30, 15};
	const std::array<vec, 3> rotated_axis{rotate(global, body_axis[0]), rotate(global, body_axis[1]), rotate(global, body_axis[2])};
	for (auto& b : rotated)
		b = transformed(b, {}, shift, global);
	for (auto& t : rotated_target)
	{
		t.position = add(shift, rotate(global, t.position));
		t.rotation = multiply(global, t.rotation);
	}
	std::array<bone, 13> rotated_result{};
	const std::array<vec, 2> rotated_shoulders{add(shift, rotate(global, shoulders[0])), add(shift, rotate(global, shoulders[1]))};
	check(solve(r, rotated, rotated_target, rotated_shoulders, rotated_axis, 1, rotated_result, limited), "rotated skeleton accepted");
	for (int i = 0; i < r.count; ++i)
		check(close(rotated_result[i].position, add(shift, rotate(global, result[i].position))),
			  "space equivariance");

	const auto saved = result;
	const auto saved_limited = limited;
	const auto reject = [&](const rig& bad, const std::array<bone, 13>& bones,
							const std::array<anchor, 2>& anchors, const char* name) {
		check(!solve(bad, bones, anchors, shoulders, body_axis, 1, result, limited), name);
		check(std::memcmp(saved.data(), result.data(), sizeof(result)) == 0 && limited == saved_limited,
			  "rejection leaves all output unchanged");
	};
	auto bad = r;
	bad.parent[4] = 4;
	reject(bad, native, target, "cyclic parent rejected");
	bad = r;
	bad.arms[1] = bad.arms[0];
	reject(bad, native, target, "overlapping arms rejected");
	bad = r;
	bad.arms[0].wrist = 256;
	reject(bad, native, target, "out of range wrist rejected");
	auto invalid = native;
	invalid[3].rotation = {};
	reject(r, invalid, target, "zero quaternion rejected");
	invalid = native;
	invalid[6].position = invalid[5].position;
	reject(r, invalid, target, "zero limb rejected");
	target[1].position[0] = std::numeric_limits<float>::quiet_NaN();
	reject(r, native, target, "NaN second hand rejected transactionally");
	target[1].position = {10000, 0, 0};
	reject(r, native, target, "extreme pose rejected");
	check(!solve(r, native, {anchor{}, anchor{}}, shoulders, body_axis, 1, {result.data(), 2}, limited), "short output rejected");

	// Move roots while leaving reachable controllers and weapon/finger offsets fixed.
	target = {anchor{{4, 5, 2}}, anchor{{4, -5, 2}, {0, 0, half, half}}};
	shoulders = {vec{-1, 3, -2}, vec{-1, -3, -2}};
	check(solve(r, native, target, shoulders, body_axis, 1, result, limited), "relocated shoulders solve");
	for (int hand = 0; hand < 2; ++hand)
	{
		const auto a = r.arms[hand];
		check(close(result[a.shoulder].position, shoulders[hand]), "arm root reaches shoulder anchor exactly");
		check(close(result[a.wrist].position, target[hand].position), "root motion does not offset wrist target");
		check(std::abs(length(sub(result[a.elbow].position, shoulders[hand])) - 5) < 0.002f,
			"relocated upper arm retains native length");
		check(std::abs(length(sub(result[a.wrist].position, result[a.elbow].position)) - 5) < 0.002f,
			"relocated forearm retains native length");
	}
	check(close(sub(result[11].position, result[7].position), {0, 1, 1}) &&
		close(sub(result[8].position, result[7].position), {0, 1, 0}), "root relocation preserves gun and fingers");
	check(close(result[0].position, native[0].position) && close(result[9].position, native[9].position),
		"root relocation leaves torso and camera alone");
	const auto relocated = result;
	shoulders[0][0] -= 2;
	check(solve(r, native, target, shoulders, body_axis, 1, result, limited), "one shoulder moves independently");
	for (int i : {5, 6, 7, 8, 10, 11, 12})
		check(close(result[i].position, relocated[i].position), "left root cannot drag right arm or gun");
	const auto before_bad_root = result;
	const auto before_bad_limit = limited;
	shoulders[1][2] = std::numeric_limits<float>::quiet_NaN();
	check(!solve(r, native, target, shoulders, body_axis, 1, result, limited) && limited == before_bad_limit &&
		std::memcmp(result.data(), before_bad_root.data(), sizeof(result)) == 0, "bad second root is transactional");

	vr::head_pose_bridge::spatial_frame spatial{};
	spatial.units_per_meter = 40;
	spatial.head_position = {100, 200, 300};
	spatial.head_yaw_axis = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
	const vec view_offset{90, 180, 270};
	check(make_shoulders(spatial, view_offset, {}, shoulders), "HMD shoulder estimate accepted");
	check(close(shoulders[0], {6.8f, 27.2f, 22}) && close(shoulders[1], {6.8f, 12.8f, 22}),
		"shoulders flank HMD below and behind, scale and render offset applied once");
	spatial.head_yaw_axis = yaw;
	check(make_shoulders(spatial, view_offset, {}, shoulders) && close(shoulders[0], {2.8f, 16.8f, 22}) &&
		close(shoulders[1], {17.2f, 16.8f, 22}), "yaw rotates shoulder pair without a height difference");
	const auto old_shoulders = shoulders;
	spatial.head_position = add(spatial.head_position, {5, -7, -10});
	check(make_shoulders(spatial, view_offset, {}, shoulders) &&
		close(sub(shoulders[0], old_shoulders[0]), {5, -7, -10}), "roomscale movement and crouch translate roots");
	const auto good_shoulders = shoulders;
	check(!make_shoulders(spatial, view_offset, {0, 0.2f, 0.08f}, shoulders) && shoulders == good_shoulders,
		"invalid shoulder settings leave outputs unchanged");
	spatial.units_per_meter = std::numeric_limits<float>::infinity();
	check(!make_shoulders(spatial, view_offset, {}, shoulders), "invalid shoulder world scale rejected");
	spatial.units_per_meter = 40;
	spatial.head_yaw_axis[0] = {0, 0, 1};
	check(!make_shoulders(spatial, view_offset, {}, shoulders), "tilted body basis rejected");
	// Regression: native support-hand elbows point inward even though the VR
	// shoulder and controller are correctly placed. Segment lengths stay 5/5.
	auto inward_native = native;
	inward_native[2].position = {3, -2, 0};
	inward_native[6].position = {3, 2, 0};
	shoulders = {native[1].position, native[5].position};
	target = {anchor{native[3].position}, anchor{native[7].position}};
	check(solve(r, inward_native, target, shoulders, body_axis, 1, result, limited), "inward native support pose solves");
	check(result[2].position[1] > shoulders[0][1] && result[6].position[1] < shoulders[1][1] &&
		result[2].position[2] < shoulders[0][2] && result[6].position[2] < shoulders[1][2],
		"left and right elbows bend outward/down instead of inheriting inward native pose");
	const auto body_bend = result;
	check(solve(r, native, target, shoulders, body_axis, 1, result, limited) &&
		close(result[2].position, body_bend[2].position) && close(result[6].position, body_bend[6].position),
		"changing animation pole with unchanged lengths does not change elbow placement");
	target[0].rotation = {half, 0, 0, half};
	check(solve(r, native, target, shoulders, body_axis, 1, result, limited) &&
		close(result[2].position, body_bend[2].position), "wrist roll does not flip elbow");
	for (int across = -10; across <= 10; ++across)
		for (int height = -10; height <= 10; ++height)
		{
			const vec direction = unit({1, across * 0.2f, height * 0.2f});
			const auto left_pole = elbow_pole(direction, body_axis, 0);
			const auto right_pole = elbow_pole({direction[0], -direction[1], direction[2]}, body_axis, 1);
			check(std::abs(dot(left_pole, direction)) < 0.001f && std::abs(length(left_pole) - 1) < 0.001f,
				"front/raised/crossed hand pole stays perpendicular and normalized");
			check(close(right_pole, {left_pole[0], -left_pole[1], left_pole[2]}), "body bend is bilaterally mirrored");
			const auto adjacent = elbow_pole(unit({1, across * 0.2f + 0.01f, height * 0.2f}), body_axis, 0);
			check(dot(left_pole, adjacent) > 0.999f, "small cross-body motion does not flip elbow");
		}
	vec previous{};
	for (int step = -20; step <= 20; ++step)
	{
		const auto direction = unit({step * 0.001f, 0.6f, -1});
		const auto pole = elbow_pole(direction, body_axis, 0);
		if (step != -20) check(dot(previous, pole) > 0.999f, "passing through preferred bend direction stays continuous");
		previous = pole;
	}
	for (const auto direction : std::array<vec, 5>{vec{-1,0,0}, vec{0,1,0}, vec{0,-1,0}, vec{0,0,1}, vec{0,0,-1}})
	{
		const auto pole = elbow_pole(direction, body_axis, 0);
		check(std::abs(length(pole) - 1) < 0.001f && std::abs(dot(pole, direction)) < 0.001f,
			"extreme cardinal reach remains finite and perpendicular");
		target[0].position = add(shoulders[0], scale(direction, 6));
		check(solve(r, native, target, shoulders, body_axis, 1, result, limited) &&
			std::abs(length(sub(result[2].position, shoulders[0])) - 5) < 0.002f &&
			std::abs(length(sub(result[3].position, result[2].position)) - 5) < 0.002f,
			"cardinal elbow solve preserves both segment lengths");
	}
	auto malformed_axis = body_axis;
	malformed_axis[1] = body_axis[0];
	const auto before_bad_axis = result;
	check(!solve(r, native, target, shoulders, malformed_axis, 1, result, limited) &&
		std::memcmp(result.data(), before_bad_axis.data(), sizeof(result)) == 0,
		"invalid body frame rejects transactionally");
	std::cout << "vr-hand-pose-tests: " << (failures ? "FAIL" : "PASS") << '\n';
	return failures ? 1 : 0;
}
