#pragma once
#include "component/vr/gameplay/campaign/cliffhanger/snowmobile_boarding.hpp"

template<class Check> void snowmobile_boarding_tests(Check& check)
{
	namespace b = vr::gameplay::cliffhanger::boarding;
	using namespace std::chrono_literals;
	const auto start = std::chrono::steady_clock::time_point{} + 1s;
	b::evidence e{true, false, false, true, {20, 1}, {21, 1}, {100, 200, 0}, {}, 40, 40};
	b::gate gate;
	const auto dwell = [&](b::evidence value)
	{
		bool accepted{};
		for (int i = 0; i <= 5; ++i) accepted = gate.update(value, start + i * 50ms);
		return accepted;
	};
	for (unsigned blocked = 0; blocked < 8; ++blocked)
	{
		auto rejected = e;
		if (blocked == 0) rejected.available = false;
		if (blocked == 1) rejected.riding = true;
		if (blocked == 2) rejected.trip_started = true;
		if (blocked == 3) rejected.player_ready = false;
		if (blocked == 4) rejected.trigger = {};
		if (blocked == 5) rejected.vehicle = {};
		if (blocked == 6) rejected.velocity = {2, 0, 0};
		if (blocked == 7) rejected.distance = b::reach_meters * rejected.units + .1f;
		gate.reset();
		check(!dwell(rejected), "spawn, wrong story phase, invalid player, missing target, movement and distance cannot board");
	}
	gate.reset();
	check(!gate.update(e, start) && !gate.update(e, start + 100ms) &&
	      !gate.update(e, start + 200ms) && gate.update(e, start + 250ms),
	      "parked native boarding target requires a continuous quarter second of proximity");
	gate.consumed(e.trigger);
	check(!dwell(e), "one native trigger cannot receive repeated automatic boarding events");
	gate.reset();
	check(dwell(e), "checkpoint load can board the restored native target without a remembered spawn event");
	gate.reset();
	gate.update(e, start); gate.update(e, start + 100ms);
	e.vehicle_origin[0] += .6f;
	check(!gate.update(e, start + 200ms) && !gate.update(e, start + 300ms),
	      "scripted vehicle displacement restarts dwell even with a zero physics velocity");
	gate.reset(); gate.update(e, start); gate.update(e, start + 100ms);
	check(!gate.update(e, start + 1s), "missing observations cannot count paused time as stillness");
	check(!gate.update(e, start), "rewound time starts a new observation");
	gate.reset(); gate.update(e, start); gate.update(e, start + 100ms);
	++e.reference;
	check(!gate.update(e, start + 200ms) && !gate.update(e, start + 300ms),
	      "recentered tracking cannot inherit the previous proximity dwell");
	++e.trigger.generation;
	check(!gate.update(e, start + 350ms), "a reused native entity slot cannot inherit an old trigger dwell");
	e.velocity[0] = std::numeric_limits<float>::quiet_NaN();
	check(!gate.update(e, start + 50ms), "invalid velocity fails closed");
	e.velocity = {}; e.distance = std::numeric_limits<float>::infinity();
	check(!gate.update(e, start + 100ms), "invalid tracking geometry fails closed");
}
