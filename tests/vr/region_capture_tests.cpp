#include "component/vr/region_capture_queue.hpp"
#include "component/vr/engine_scene_completion.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

namespace rc = vr::region_capture;
static std::vector<rc::row> observed;
static void collect(rc::row row) noexcept { observed.push_back(row); }

int main()
{
	rc::queue<2> bounded;
	assert(bounded.push({1}));
	assert(bounded.push({2}));
	assert(!bounded.push({3}));
	assert(bounded.dropped == 1);
	rc::row rows[2];
	assert(bounded.pop(rows, 2) == 2 && rows[0].tick == 1 && rows[1].tick == 2);
	assert(bounded.pop(rows, 2) == 0);
	assert(bounded.push({4}) && bounded.pop(rows, 1) == 1 && rows[0].tick == 4);

	// Concurrent producers cannot tear rows. Every event is either delivered or
	// explicitly counted as dropped; render threads never wait for the reader.
	rc::queue<256> concurrent;
	std::atomic_int finished{};
	std::vector<std::thread> writers;
	for (std::uint64_t thread = 1; thread <= 4; ++thread)
		writers.emplace_back([&, thread]
		{
			for (std::uint64_t i{}; i < 10000; ++i)
				concurrent.push({i, thread, 0, 0, 0, i ^ thread});
			++finished;
		});
	std::uint64_t received{};
	do
	{
		const auto size = concurrent.pop(rows, 2);
		for (std::size_t i{}; i < size; ++i)
		{ assert(rows[i].a == (rows[i].tick ^ rows[i].thread)); ++received; }
	} while (finished != 4);
	for (auto& writer : writers) writer.join();
	while (const auto size = concurrent.pop(rows, 2)) received += size;
	assert(received + concurrent.dropped == 40000);

	std::array<std::uint8_t, 0x8090> record{};
	const std::uint32_t count = 7;
	std::memcpy(record.data() + 0x2FC8 + 18 * 0x130 + 0x98, &count, 4);
	rc::snapshot(1, 1, 0, record.data());
	assert(observed.empty());
	rc::sink.store(collect);
	rc::snapshot(1, 1, 0, record.data());
	assert(observed.size() == 3);
	assert(observed[1].a == 18 && observed[1].b == 0 && observed[1].c == 7);
	const auto original = record;
	rc::begin_owner(1, 1, record.data());
	rc::draw_indexed(99);
	rc::draw_indexed(9);
	rc::end_owner(1, 1, record.data());
	const auto end = observed[observed.size() - 4];
	assert(end.type == static_cast<std::uint64_t>(rc::kind::owner_end));
	assert(end.a == 2 && end.b == 108);
	assert(original == record);
	rc::sink.store(nullptr);
	observed.clear();
	{ rc::phase_scope off(rc::phase::clone); }
	assert(observed.empty());
	rc::sink.store(collect);
	{
		rc::phase_scope outer(rc::phase::backend, record.data(), 123);
		{ rc::phase_scope inner(rc::phase::claim, record.data(), 123); inner.finish(42); }
		outer.finish(1);
		outer.finish(2); // No duplicate end on explicit finish or destruction.
	}
	assert(observed.size() == 4);
	assert(observed[0].type == 12 && observed[0].eye == 1 && observed[0].a == 123);
	assert(observed[2].type == 13 && observed[2].eye == 2 && observed[2].b == 42);
	assert(observed[3].type == 13 && observed[3].b == 1);
	rc::sink.store(nullptr);
	namespace completion = vr::engine_scene_completion;
	constexpr std::uintptr_t frontend = 123;
	auto identity = frontend;
	completion::flags ready{};
	std::vector<completion::stage> waits;
	const auto complete = [&](completion::stage stage)
	{
		waits.push_back(stage);
		if (stage == completion::stage::initial) { ready.initial = 1; ready.scene = 1; }
		if (stage == completion::stage::surfaces) ready.surfaces = 1;
		if (stage == completion::stage::effects) ready.effects = 1;
	};
	const auto read_flags = [&](std::uintptr_t value) { assert(value == frontend); return ready; };
	assert(completion::await_inputs(frontend, [&] { return identity; }, complete, read_flags) == completion::failure::none);
	assert(ready.complete() && waits == std::vector<completion::stage>({completion::stage::initial,
		completion::stage::surfaces, completion::stage::effects}));
	waits.clear();
	assert(completion::await_inputs(0, [&] { return identity; }, complete, read_flags) == completion::failure::identity);
	assert(waits.empty());
	ready.effects = 0;
	assert(completion::await_inputs(frontend, [&] { return identity; }, [](auto) {}, read_flags) == completion::failure::incomplete);
	assert(completion::await_inputs(frontend, [&] { return identity; }, [&](auto) { identity = frontend + 1; }, read_flags) == completion::failure::identity);
	std::cout << "region-capture-tests: PASS (queue/loss, snapshots, scopes, native completion ordering/contracts)\n";
}
