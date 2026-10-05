#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/aim_assist_geometry.hpp"
#include <iostream>
#include <limits>

int main()
{
	using namespace vr::gameplay;
	using vr::gameplay::hands::vec;
	int failures{};
	const auto check = [&](bool condition, const char* label) {
		if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
	};
	const auto close = [](const vec& a, const vec& b) { return vr::gameplay::hands::length(vr::gameplay::hands::sub(a, b)) < .0001f; };
	const weapons::shot_geometry shot{{1,0,0}, {0,-1,0}, {0,0,1}, {0,0,0}};
	const auto direction = [](float degrees, float distance = 100.f) -> vec {
		const auto radians = degrees * .017453292519943295f;
		return {std::cos(radians) * distance, std::sin(radians) * distance, 0};
	};
	int traces{};
	const auto visible = [&](unsigned, const vec&) { ++traces; return true; };
	for (const float strength : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()})
	{
		aim_assist::selection selected(shot, strength);
		const auto before = traces;
		check(!selected.consider(1, {100,0,0}, visible), "disabled or invalid strength rejects candidates");
		auto result = shot;
		check(!selected.apply(result) && result.forward == shot.forward && result.right == shot.right &&
			result.up == shot.up && result.origin == shot.origin, "off preserves the complete original shot");
		check(traces == before, "off performs no visibility work");
	}
	for (float strength : {.01f, 1.f, 50.f, 100.f, 200.f})
	{
		const auto degrees = vr::settings::aim_assist_degrees(strength);
		aim_assist::selection selected(shot, strength);
		check(!selected.consider(1, direction(degrees + .01f), visible), "outside selected cone rejected");
		check(selected.consider(1, direction(degrees * .9f), visible), "inside selected cone accepted");
	}
	{
		aim_assist::selection selected(shot, 50);
		check(!selected.consider(1, {100,7,7}, visible), "combined yaw and pitch outside cone rejected");
		check(selected.consider(2, {100,0,8}, visible), "vertical aim is included in the 3D cone");
		check(!selected.consider(3, {-100,0,0}, visible), "targets behind barrel rejected");
		check(!selected.consider(3, {0,0,0}, visible), "zero-length target direction rejected");
		check(!selected.consider(3, {1301,0,0}, visible), "native distance ceiling enforced");
		check(!selected.consider(3, {std::numeric_limits<float>::infinity(),0,0}, visible), "infinite position rejected");
		check(!selected.consider(3, {std::numeric_limits<float>::quiet_NaN(),0,0}, visible), "NaN position rejected");
		check(!selected.consider(4000, {100,0,0}, visible), "invalid H2 entity rejected");
		check(selected.consider(3, {1300,0,0}, visible), "native distance boundary accepted");
	}
	{
		aim_assist::selection selected(shot,100);
		check(selected.consider(3000,direction(7),visible),"valid H2 enemies above entity 2047 remain eligible");
		check(selected.entity()==3000 && std::abs(selected.correction_degrees()-7)<.001f,
			"diagnostics identify the actual target and barrel correction angle");
	}
	{
		aim_assist::selection selected(shot, 100);
		check(!selected.consider(1, direction(1), [](unsigned, const vec&) { return false; }), "occluded target rejected");
		check(selected.consider(2, direction(3), visible), "visible target behind occluded candidate can win");
		const auto before = traces;
		check(!selected.consider(3, direction(4, 10), visible) && traces == before, "closer but less aligned target does not win or trace");
		check(selected.consider(4, direction(2, 1200), visible), "angular alignment outranks range");
		auto result = shot;
		check(selected.apply(result) && close(result.forward, vr::gameplay::hands::unit(direction(2))), "best target selected");
		check(selected.consider(5, direction(-9), visible) == false, "candidate cannot widen cone using previous correction");
		check(!selected.consider(6, direction(11), visible), "rotation never accumulates beyond ten degrees");
	}
	{
		// World rotation, translation and barrel roll must not alter acceptance.
		for (const vr::gameplay::hands::quat rotation : {vr::gameplay::hands::quat{0,0,0,1}, vr::gameplay::hands::quat{0,.70710678f,0,.70710678f},
			vr::gameplay::hands::quat{.70710678f,0,0,.70710678f}})
		{
			const vec origin{8000,-9000,500};
			const weapons::shot_geometry transformed{vr::gameplay::hands::rotate(rotation, shot.forward),
				vr::gameplay::hands::rotate(rotation, shot.right), vr::gameplay::hands::rotate(rotation, shot.up), origin};
			aim_assist::selection selected(transformed, 100);
			const auto point = vr::gameplay::hands::add(origin, vr::gameplay::hands::rotate(rotation, direction(9)));
			check(selected.consider(1, point, visible), "target cone follows translated, rotated barrel");
			auto result = transformed;
			check(selected.apply(result) && result.origin == origin, "correction never moves muzzle");
			check(close(result.forward, vr::gameplay::hands::unit(vr::gameplay::hands::sub(point, origin))), "corrected ray reaches target");
			check(std::abs(vr::gameplay::hands::dot(result.forward, result.right)) < .0001f &&
				std::abs(vr::gameplay::hands::dot(result.forward, result.up)) < .0001f &&
				close(vr::gameplay::hands::cross(result.forward, result.right), vr::gameplay::hands::scale(result.up,-1)),
				"native shot vectors retain orthogonality and handedness");
		}
	}
	std::cout << "aim assist failures=" << failures << '\n';
	return failures ? 1 : 0;
}
