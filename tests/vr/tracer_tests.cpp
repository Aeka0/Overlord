#include "component/vr/gameplay/tracer_events.hpp"
#include "component/vr/gameplay/native_tracer.hpp"
#include "component/vr/gameplay/weapon_instance_cache.hpp"
#include <iostream>
#include <limits>

int main()
{
	using namespace vr::gameplay::weapons;
	using namespace tracers;
	int failures{};
	const auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::cerr << what << '\n'; } };
	check(sanitize(-1) == style::native && sanitize(7) == style::native, "Malformed style falls back to native");
	check(!colored(style::native) && !colored(style::blaze), "Native and Blaze retain the stock warm material");
	for (int i = 2; i <= 6; ++i) check(colored(style(i)), "Only colored presets select the neutral material");
	struct definition { unsigned drawInterval{4}; float speed{7500}; float colors[5][4]{}; } original;
	for (unsigned i = 0; i < 5; ++i) for (unsigned c = 0; c < 4; ++c) original.colors[i][c] = float(i*4+c)/20;
	auto native = original; apply(native, style::native, 0);
	check(std::memcmp(&native, &original, sizeof(native)) == 0, "Native changes no definition fields");
	auto blaze = original; apply(blaze, style::blaze, 0);
	check(blaze.drawInterval == 1 && blaze.speed == original.speed &&
		std::memcmp(blaze.colors, original.colors, sizeof(blaze.colors)) == 0, "Blaze changes cadence only");
	for (int value = 2; value <= 6; ++value)
	{
		auto tinted = original; apply(tinted, style(value), 1);
		for (unsigned i = 0; i < 5; ++i)
		{
			check(tinted.colors[i][3] == original.colors[i][3], "All native alpha control points retained");
			for (unsigned c = 0; c < 3; ++c) check(tinted.colors[i][c] == color(style(value), 1)[c], "Whole shot has one RGB");
		}
	}
	instance_cache<sequence, 3> guns;
	const weapon_identity left{17, 101}, right{17, 102};
	auto* l = guns.acquire(left); auto* r = guns.acquire(right);
	for (unsigned i = 0; i < 21; ++i) check(l->take(style::independence_1776) == i%3, "1776 repeats red blue white");
	check(r->take(style::independence_1776) == 0, "Same-token second gun starts its own sequence");
	check(color(style::independence_1776, 1)[2] == 1.f && color(style::independence_1776, 1)[1] < .5f, "1776 middle color is blue, not green");
	for (unsigned i = 0; i < 14; ++i) check(l->take(style::rainbow) == i%7, "Rainbow resets on selection and cycles seven colors");
	for (unsigned i = 0; i < 7; ++i) for (unsigned j = i+1; j < 7; ++j)
		check(color(style::rainbow, i) != color(style::rainbow, j), "Seven distinct spectral colors");
	check(l->take(style::independence_1776) == 0, "Switching presets restarts the sequence");
	{
		owner_scope one(left); check(detail::owner.id == left, "Committed left owner published");
		{ owner_scope nested(right); check(detail::owner.id == right, "Nested shot cannot borrow outer gun"); }
		check(detail::owner.id == left, "Nested ownership restored");
		{ owner_scope module({23, left.generation}, left); check(detail::owner.lease == left && detail::owner.id.weapon == 23, "Module retains its host lifetime and actual firing definition"); }
	}
	check(!detail::owner.id.weapon, "No owner leaks beyond native shot");
	std::array<std::byte, 0x94> state{};
	const auto put = [&]<class T>(unsigned offset, T value) { std::memcpy(state.data()+offset, &value, sizeof(value)); };
	put(0, std::uint8_t(0x55)); put(0x80, 17u); put(0x8c, std::uint16_t(1));
	put(0x1c, 100.f); put(0x58, 10.f);
	const auto identity = key(state);
	check(valid(identity) && identity.entity == 1 && identity.weapon == 17, "Decode native temporary-event prefix");
	event_bridge<4> events;
	const auto allocation = events.allocate(1);
	const appearance first{style::independence_1776, 1};
	check(events.publish(identity, allocation, first), "Publish only at matching native allocation");
	auto wrong = identity; wrong.weapon = 18;
	check(!events.consume(wrong).matched, "Same slot with different weapon rejected");
	wrong = identity; ++wrong.geometry[0];
	check(!events.consume(wrong).matched, "Same slot and weapon with different ballistic data rejected");
	wrong = identity; ++wrong.shooter;
	check(!events.consume(wrong).matched, "NPC cannot consume player's appearance");
	wrong = identity; ++wrong.generation;
	check(!events.consume(wrong).matched, "Identical geometry from a different native entity lifetime cannot borrow color");
	const auto consumed = events.consume(identity);
	check(consumed.matched && consumed.draw && consumed.visual.shot == 1, "Client gets frozen shot appearance");
	const auto duplicate = events.consume(identity);
	check(duplicate.matched && !duplicate.draw, "Repeated event suppresses both duplicate and stock tracer");
	// Two pellets/penetration segments receive the SAME appearance, no palette advance on consumption.
	auto segment = identity; segment.entity = 2; segment.geometry[0] += 1;
	check(events.publish(segment, events.allocate(2), first), "Second segment published");
	check(events.consume(segment).visual.shot == consumed.visual.shot, "All segments retain one shot color");
	const auto reused = events.allocate(1);
	check(!events.consume(identity).matched && !events.publish(identity, allocation, first), "Entity reuse invalidates old generation");
	check(events.publish(identity, reused, {style::anime, 0}), "Reused entity accepts new shot");
	events.clear(); check(!events.consume(identity).matched, "World reset removes in-flight appearances");
	check(!events.allocate(4) && !valid(key({})), "Malformed/overflow events are bounded");
	std::cout << "tracer tests: " << (failures ? "FAIL" : "PASS") << '\n';
	return failures ? 1 : 0;
}
