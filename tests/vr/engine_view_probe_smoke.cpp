#include <std_include.hpp>

#include "component/console.hpp"
#include "component/vr/engine_view_probe.hpp"
#include "component/vr/debug_options.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
	void require(const bool condition, const std::string_view message)
	{
		if (!condition) throw std::runtime_error(std::string{message});
	}

	void expect_slot_math()
	{
		using namespace vr::engine_view_probe;
		constexpr std::uintptr_t frontend = 0x10000000;
		constexpr auto base = frontend + frontend_slot_base_offset;
		constexpr auto valid = derive_slot_index(frontend, base + 2 * frontend_slot_stride, 3);
		static_assert(valid && valid.index == 2);
		static_assert(!derive_slot_index(frontend, base + 1, 3));
		static_assert(!derive_slot_index(frontend, base + 3 * frontend_slot_stride, 3));
		static_assert(unpack_low_u32(pack_u32_pair(7, 11)) == 7);
		static_assert(unpack_high_u32(pack_u32_pair(7, 11)) == 11);
		static_assert(unpack_u16(pack_u16_quad(3, 5, 7, 11), 0) == 3);
		static_assert(unpack_u16(pack_u16_quad(3, 5, 7, 11), 1) == 5);
		static_assert(unpack_u16(pack_u16_quad(3, 5, 7, 11), 2) == 7);
		static_assert(unpack_u16(pack_u16_quad(3, 5, 7, 11), 3) == 11);
		static_assert(float_from_bits(float_bits(1.25f)) == 1.25f);
	}

	void expect_complete_transaction()
	{
		using namespace vr::engine_view_probe;
		set_enabled(false);
		reset();
		set_enabled(true);

		constexpr std::uintptr_t frontend = 0x10000000;
		constexpr auto slot = frontend + frontend_slot_base_offset + 2 * frontend_slot_stride;
		const auto token = begin(41, 1, {
			0x20000000, 0x12345678, 0, 2, 1.0f, -1, 0,
		});
		require(static_cast<bool>(token), "view transaction did not start");
		record_reservation(token, {frontend, frontend, 2, 3, 2,
			1, 2, 2, 3, 2, 2, 7, 7});
		callstack_observation stack{{0x1403CA243, 0x140123456, 0x7FF700001234}};
		stack.frames[8] = 0x1405A3767;
		record_callstack(token, stack);
		record_frontend(token, {frontend, 7, 2, 2, 3, 3,
			slot_base_address(frontend), 2},
			observation_stage::enter);
		record_slot(token, {frontend, slot, 2, 3, 2, 7, 2, 3, 3});
		record_generator(token, {0, 2, 0x30000000, 0x31000000, slot,
			0x32000000, -1, frontend, 3, 2});
		record_return(token, {return_source::allocator, slot, slot, 3, 2, 7, 2, 3,
			-1, frontend});
		record_return(token, {return_source::generator, slot, 0, 3, 2, 7, 2, 3,
			-1, 0x32000000});
		end(token, {2, 3, 1, 1, 2, slot, 2, 7, 7, 0xA5A5});
		record_frame_flip(41, 1, {frontend, frontend, 7, 7, 2, 3, 2, 2, 3, 3,
			3, 3, 1});

		std::array<record, 16> records{};
		const auto count = read_recent(records.data(), records.size());
		require(count == 11, "complete view transaction emitted the wrong event count");
		constexpr std::array expected{
			event_kind::begin,
			event_kind::record_reservation,
			event_kind::callstack,
			event_kind::callstack_tail,
			event_kind::frontend,
			event_kind::slot,
			event_kind::generator,
			event_kind::call_return,
			event_kind::call_return,
			event_kind::end,
			event_kind::flip,
		};
		for (std::size_t index{}; index < expected.size(); ++index)
		{
			require(records[index].kind == expected[index], "view event ordering changed");
			require(!has_flag(records[index].flags, record_flag::orphan),
				"valid view transaction was marked orphan");
		}
		const auto& reservation = records[1];
		require(reservation.values[0] == frontend && reservation.values[1] == frontend &&
			reservation.values[2] == pack_u32_pair(2, 3) &&
			reservation.values[3] == 2 &&
			reservation.values[4] == pack_u32_pair(1, 2) &&
			reservation.values[5] == pack_u32_pair(2, 3) &&
			reservation.values[6] == pack_u32_pair(2, 2) &&
			reservation.values[7] == pack_u32_pair(7, 7),
			"record reservation payload fields were reordered or dropped");
		const auto probe_status = get_status();
		require(probe_status.transaction_count == 1 && probe_status.orphan_records == 0 &&
			probe_status.duplicate_records == 0 && probe_status.invalid_slot_records == 0 &&
			probe_status.count_mismatch_records == 0,
			"valid view transaction changed an error counter");
		const auto formatted = format_recent(16);
		std::size_t negative_draw_type_count{};
		for (auto offset = formatted.find("draw_type=-1"); offset != std::string::npos;
			offset = formatted.find("draw_type=-1", offset + 1))
		{
			++negative_draw_type_count;
		}
		require(formatted.find("record_reservation/after_call") != std::string::npos &&
			formatted.find("scene_record_index=2") != std::string::npos &&
			formatted.find("global_record_count=2->3") != std::string::npos &&
			formatted.find("current_record_index=1->2") != std::string::npos &&
			negative_draw_type_count == 4 &&
			formatted.find("frame_time=") == std::string::npos,
			"record lifecycle evidence was formatted with stale or missing semantics");
	}

	void expect_invalid_record_reservation_rejected()
	{
		using namespace vr::engine_view_probe;
		set_enabled(false);
		reset();
		set_enabled(true);

		const auto token = begin(42, 1, {});
		require(static_cast<bool>(token), "invalid reservation transaction did not start");
		record_reservation(token, {
			0x10000000,
			0x10000000,
			frontend_record_capacity,
			frontend_record_capacity + 1,
			frontend_record_capacity,
			0,
			frontend_record_capacity,
			frontend_record_capacity,
			frontend_record_capacity + 1,
			1,
			1,
			0,
			0,
		});
		record_reservation(token, {
			0x10000000,
			0x10000000,
			(std::numeric_limits<std::uint32_t>::max)(),
			0,
			(std::numeric_limits<std::uint32_t>::max)(),
			3,
			(std::numeric_limits<std::uint32_t>::max)(),
			3,
			0,
			1,
			1,
			0,
			0,
		});
		record_reservation(token, {});
		record_reservation(token, {
			0x10000000,
			0x10000000,
			0,
			1,
			0,
			3,
			0,
			1,
			1,
			1,
			1,
			0,
			1,
		});

		std::array<record, 5> records{};
		const auto count = read_recent(records.data(), records.size());
		require(count == records.size(), "invalid reservation records were dropped");
		for (std::size_t index = 1; index < records.size(); ++index)
		{
			require(records[index].kind == event_kind::record_reservation &&
				has_flag(records[index].flags, record_flag::count_mismatch),
				"invalid record reservation was not rejected");
		}
		require(has_flag(records.back().flags, record_flag::selector_changed),
			"record reservation selector change was not classified");
		require(get_status().count_mismatch_records == 4,
			"invalid reservation error accounting changed");
	}

	void expect_duplicate_scene_call_rejected()
	{
		using namespace vr::engine_view_probe;
		set_enabled(false);
		reset();
		set_enabled(true);

		constexpr std::uintptr_t frontend = 0x10000000;
		constexpr auto first_slot = frontend + frontend_slot_base_offset;
		constexpr auto second_slot = first_slot + frontend_slot_stride;
		const auto token = begin(52, 1, {});
		require(static_cast<bool>(token), "duplicate-call transaction did not start");
		record_view_call(token, {0, frontend, 0, 0, 0x20000000, 0xABC},
			observation_stage::before_call);
		record_slot(token, {frontend, first_slot, 0, 1, 0, 0, 0, 1, 1});
		record_generator(token, {0, 0, 0, 0, first_slot, 0, 3, frontend, 1, 0});
		record_view_call(token, {0, frontend, 0, 1, 0x20000000, 0xABC},
			observation_stage::after_call);
		record_view_call(token, {1, frontend, 0, 1, 0x20000000, 0xABC},
			observation_stage::before_call);
		record_slot(token, {frontend, second_slot, 1, 2, 1, 0, 0, 1, 1});
		record_generator(token, {0, 0, 0, 0, second_slot, 0, 3, frontend, 2, 1});
		record_view_call(token, {1, frontend, 0, 2, 0x20000000, 0xABC},
			observation_stage::after_call);
		end(token, {0, 2, 2, 2, 4, second_slot, 1, 0, 0, 0x1234});

		std::array<record, 16> records{};
		const auto count = read_recent(records.data(), records.size());
		require(count == 10, "duplicate-call transaction emitted the wrong event count");
		const auto state = get_status();
		require(state.transaction_count == 1 && state.duplicate_records == 1 &&
			state.count_mismatch_records == 1 && state.invalid_slot_records == 0,
			"a second scene call was not rejected as a duplicate transaction");
		const auto formatted = format_recent(16);
		require(formatted.find("call=1") != std::string::npos &&
			formatted.find("expected_scene_calls=1") != std::string::npos,
			"duplicate-call evidence was not formatted");
	}

	void expect_r_end_frame_reentrancy_flag()
	{
		using namespace vr::engine_view_probe;
		set_enabled(false);
		reset();
		set_enabled(true);

		const auto reentrancy_flag = flag(record_flag::inside_r_end_frame);
		const auto token = begin(7, 1, {}, reentrancy_flag);
		require(static_cast<bool>(token), "R_EndFrame reentrant transaction did not start");
		record_callstack(token, {}, reentrancy_flag);
		end(token, {}, reentrancy_flag);

		std::array<record, 3> records{};
		const auto count = read_recent(records.data(), records.size());
		require(count == records.size(), "interval correlation emitted the wrong event count");
		for (const auto& record : records)
		{
			require(record.frontend_frame_id == 7,
				"R_EndFrame reentrant transaction lost its frontend epoch");
			require(has_flag(record.flags, record_flag::inside_r_end_frame),
				"R_EndFrame reentrant transaction lost its boundary flag");
		}
	}

	void expect_view_state_evidence()
	{
		using namespace vr::engine_view_probe;
		set_enabled(false);
		reset();
		set_enabled(true);

		const auto token = begin(61, 1, {});
		require(static_cast<bool>(token), "view-state transaction did not start");
		record_view_state(token, {
			view_state_source::outer_scene, 0, 0x20000000,
			0, 0x10, 0x20, 0x30, 0x40, 0x50,
		}, observation_stage::before_call);

		auto stable = with_relation(0, view_state_relation::slot_compared);
		stable = with_relation(stable, view_state_relation::slot_unchanged);
		record_view_state(token, {
			view_state_source::draw_surface_generator, stable, 0x30000000,
			0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666,
		}, observation_stage::after_call);

		auto changed_match = with_relation(0, view_state_relation::slot_compared);
		changed_match = with_relation(changed_match, view_state_relation::output_compared);
		changed_match = with_relation(changed_match,
			view_state_relation::output_matches_slot);
		record_view_state(token, {
			view_state_source::outer_scene, changed_match, 0x20000000,
			0x7777, 0x7777, 0x8888, 0x9999, 0xAAAA, 0xBBBB,
		}, observation_stage::after_call);

		const auto output_mismatch = with_relation(0,
			view_state_relation::output_compared);
		record_view_state(token, {
			view_state_source::outer_scene, output_mismatch, 0x20000000,
			0xCCCC, 0xDDDD, 0xEEEE, 0xFFFF, 0x1234, 0x5678,
		}, observation_stage::after_call);

		const auto state = get_status();
		require(state.view_state_snapshots == 4 &&
			state.stable_slot_comparisons == 1 &&
			state.changed_slot_comparisons == 1 &&
			state.matching_output_copies == 1 &&
			state.mismatching_output_copies == 1,
			"view-state relation accounting changed");
		const auto formatted = format_recent(8);
		require(formatted.find("view_state/before_call") != std::string::npos &&
			formatted.find("source=draw_surface_generator") != std::string::npos &&
			formatted.find("slot_hash=0x1111") != std::string::npos &&
			formatted.find("slot_unchanged=yes") != std::string::npos &&
			formatted.find("output_matches_slot=yes") != std::string::npos &&
			formatted.find("output_matches_slot=no") != std::string::npos,
			"view-state hashes or relations were not formatted");
	}

	void expect_camera_state_evidence()
	{
		using namespace vr::engine_view_probe;
		set_enabled(false);
		reset();
		set_enabled(true);

		const camera_state_observation viewpos{
			camera_state_source::set_viewpos_now, float_bits(0.0f), 0x1403ACB32,
			0x11110000, 0, 0, 0, 0xAAAA, 0xBBBB,
		};
		record_unscoped_camera_state(71, viewpos, observation_stage::enter);
		record_unscoped_camera_state(71, viewpos, observation_stage::snapshot);
		record_unscoped_camera_state(71, viewpos, observation_stage::leave);

		const auto token = begin(71, 1, {});
		require(static_cast<bool>(token), "camera-state transaction did not start");
		const camera_state_observation helper{
			camera_state_source::camera_helper, float_bits(1.25f), 0x14077BC43,
			0x22220000, 0x14EEE08B8, 0xCCCC, 0xDDDD, 0xEEEE, 0xFFFF,
		};
		record_camera_state(token, helper, observation_stage::before_call);
		record_camera_state(token, helper, observation_stage::after_call);
		end(token, {});

		const auto state = get_status();
		require(state.camera_state_snapshots == 5 &&
			state.scoped_camera_state_snapshots == 2 &&
			state.unscoped_camera_state_snapshots == 3 &&
			state.set_viewpos_calls == 1 && state.camera_helper_calls == 1,
			"camera-state accounting changed");
		require(state.orphan_records == 0,
			"unscoped camera evidence was incorrectly classified as orphaned");
		const auto formatted = format_recent(16);
		require(formatted.find("camera_state/enter") != std::string::npos &&
			formatted.find("source=set_viewpos_now") != std::string::npos &&
			formatted.find("source=camera_helper") != std::string::npos &&
			formatted.find("caller=0x14077bc43") != std::string::npos &&
			formatted.find("scalar=1.25") != std::string::npos &&
			formatted.find("output_hash=0xdddd") != std::string::npos,
			"camera-state identity or hashes were not formatted");
	}

	void expect_slot_initializer_evidence()
	{
		using namespace vr::engine_view_probe;
		set_enabled(false);
		reset();
		set_enabled(true);

		const auto token = begin(81, 1, {});
		require(static_cast<bool>(token), "slot-initializer transaction did not start");
		record_slot_initializer(token, {
			0x20000000, 0x30000000, 0x1111, 0x2222, 0x3333,
			0x4444, 0x4444, 12, 4, 15, 2,
		});
		record_slot_initializer(token, {
			0x20000000, 0, 0x1111, 0, 0,
			0x5555, 0x6666, 0, invalid_slot_index, invalid_slot_index,
			invalid_slot_index,
		});
		end(token, {});

		const auto state = get_status();
		require(state.slot_initializer_calls == 2 &&
			state.slot_initializer_slot_changes == 1 &&
			state.slot_initializer_shared_changes == 1 &&
			state.slot_initializer_invalid_slots == 1 &&
			state.invalid_slot_records == 1,
			"slot-initializer accounting changed");
		const auto formatted = format_recent(8);
		require(formatted.find("slot_initializer/after_call") != std::string::npos &&
			formatted.find("slot_hash=0x2222->0x3333") != std::string::npos &&
			formatted.find("changed_bytes=12") != std::string::npos &&
			formatted.find("changed_range=4..15") != std::string::npos &&
			formatted.find("slot_index=2") != std::string::npos &&
			formatted.find("shared_unchanged=yes") != std::string::npos &&
			formatted.find("shared_unchanged=no") != std::string::npos &&
			formatted.find("slot_index=invalid") != std::string::npos,
			"slot-initializer evidence was not formatted");
	}

	void expect_descriptor_contract_evidence()
	{
		using namespace vr::engine_view_probe;
		set_enabled(false);
		reset();
		set_enabled(true);
		const auto token = begin(82, 1, {});
		record_descriptor_contract(token, {
			0x21000000, 0xAAAA, 0xAAAA, 0x501C8, true, true,
		});
		record_descriptor_contract(token, {
			0x21000000, 0xBBBB, 0xCCCC, 0x501C8, true, true,
		});
		record_descriptor_contract(token, {
			0x21000000, 0, 0, 0x501C8, false, false,
		});
		end(token, {});

		const auto state = get_status();
		require(state.descriptor_contract_samples == 3 &&
			state.descriptor_contract_stable == 1 &&
			state.descriptor_contract_changes == 1 &&
			state.descriptor_contract_unreadable == 1,
			"descriptor-contract accounting changed");
		const auto formatted = format_recent(8);
		require(formatted.find("descriptor_contract/after_call") != std::string::npos &&
			formatted.find("size=328136") != std::string::npos &&
			formatted.find("full_hash=0xaaaa->0xaaaa") != std::string::npos &&
			formatted.find("stable=yes") != std::string::npos &&
			formatted.find("readable_before=no") != std::string::npos,
			"descriptor-contract evidence was not formatted");
	}

	void expect_ownership_boundary_evidence()
	{
		using namespace vr::engine_view_probe;
		set_enabled(false);
		reset();
		set_enabled(true);
		record_ownership_boundary(73, 1, {
			ownership_boundary_kind::frontend_handoff,
			0x14EF1EB80,
			0x14FF57E80,
			0x150FB13F0,
			1,
			1,
			0,
			1,
			1,
			0,
			0xA5A5A5A5,
		}, observation_stage::enter);

		std::array<record, 1> records{};
		require(read_recent(records.data(), records.size()) == 1,
			"ownership-boundary evidence was not published");
		const auto& record = records.front();
		require(record.kind == event_kind::ownership_boundary &&
			record.frontend_frame_id == 73 &&
			record.stage == observation_stage::enter &&
			record.values[0] == static_cast<std::uint64_t>(
				ownership_boundary_kind::frontend_handoff) &&
			record.values[1] == 0x14EF1EB80 &&
			record.values[2] == 0x14FF57E80 &&
			record.values[3] == 0x150FB13F0 &&
			record.values[4] == pack_u32_pair(1, 1) &&
			record.values[5] == pack_u32_pair(0, 1) &&
			record.values[6] == pack_u32_pair(1, 0) &&
			record.values[7] == 0xA5A5A5A5,
			"ownership-boundary payload fields were reordered or dropped");
		const auto formatted = format_recent(1);
		require(formatted.find("boundary=frontend_handoff") != std::string::npos &&
			formatted.find("backend_frontend=0x14ff57e80") != std::string::npos,
			"ownership-boundary evidence was not formatted");
		set_enabled(false);
	}

	void expect_ring_wrap()
	{
		using namespace vr::engine_view_probe;
		set_enabled(false);
		reset();
		set_enabled(true);
		for (std::size_t index{}; index < trace_capacity + 7; ++index)
		{
			record_frame_flip(index + 1, 1, {});
		}
		const auto probe_status = get_status();
		require(probe_status.newest_sequence == trace_capacity + 7 &&
			probe_status.overwrite_count == 7 && probe_status.dropped_records == 0,
			"view trace overwrite accounting is incorrect");
		std::array<record, 4> records{};
		const auto count = read_recent(records.data(), records.size());
		require(count == records.size(), "view trace did not return the newest wrapped records");
		require(records.front().sequence == probe_status.newest_sequence - records.size() + 1 &&
			records.back().sequence == probe_status.newest_sequence,
			"view trace returned stale records after wrapping");
		const auto formatted = format_recent(256);
		const auto first_formatted = probe_status.newest_sequence - 255;
		require(formatted.find("H2 CPU view-slot trace") != std::string::npos &&
			formatted.find(std::to_string(first_formatted) + " qpc=") != std::string::npos &&
			formatted.find(std::to_string(probe_status.newest_sequence) + " qpc=") !=
				std::string::npos,
			"view trace control-plane formatting failed");
		require(formatted.size() > 0x1000,
			"view trace no longer reproduces the single-console-message overflow case");
		const auto chunks = console::split_text_chunks(formatted);
		std::string reassembled;
		reassembled.reserve(formatted.size());
		for (const auto chunk : chunks)
		{
			require(!chunk.empty() && chunk.size() <= console::max_text_chunk_size,
				"view trace console chunk exceeded its bounded formatter input");
			reassembled.append(chunk.data(), chunk.size());
		}
		require(reassembled == formatted,
			"view trace console chunking dropped or reordered evidence bytes");
		set_enabled(false);
		const auto disabled_sequence = get_status().newest_sequence;
		record_frame_flip(0xFFFFFFFF, 1, {});
		require(get_status().newest_sequence == disabled_sequence,
			"disabled view trace accepted a record");
	}
}

int main()
{
	try
	{
		// The default diagnostic-off path must still issue unique production
		// correlation tokens without filling the trace ring.
		using namespace vr::engine_view_probe;
		set_enabled(true);
		const auto first = begin(1, 1, {});
		const auto second = begin(1, 1, {});
		require(first && second && first.transaction_id != second.transaction_id,
			"disabled diagnostics must preserve stereo transaction tokens");
		require(get_status().newest_sequence == 0,
			"view diagnostics must default to an empty trace");
		set_enabled(false);
		reset();
		vr::debug_options::initialize({true, false, false});
		vr::debug_options::initialize({false, true, true});
		require(vr::debug_options::enabled(vr::debug_options::probe::view) &&
			!vr::debug_options::enabled(vr::debug_options::probe::snapshots) &&
			!vr::debug_options::enabled(vr::debug_options::probe::perf),
			"startup selection must remain fixed for the process lifetime");
		expect_slot_math();
		expect_complete_transaction();
		expect_invalid_record_reservation_rejected();
		expect_duplicate_scene_call_rejected();
		expect_r_end_frame_reentrancy_flag();
		expect_view_state_evidence();
		expect_camera_state_evidence();
		expect_slot_initializer_evidence();
		expect_descriptor_contract_evidence();
		expect_ownership_boundary_evidence();
		expect_ring_wrap();
		std::cout << "vr-engine-view-probe-smoke: PASS\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "vr-engine-view-probe-smoke: FAIL; " << error.what() << '\n';
		return 1;
	}
}
