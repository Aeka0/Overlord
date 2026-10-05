#include <std_include.hpp>

#include "component/vr/engine_backend_probe.hpp"
#include "component/vr/debug_options.hpp"
#include "component/vr/native_render_contract.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
	namespace probe = vr::engine_backend_probe;
	namespace contract = vr::native_render_contract;

	constexpr std::int32_t s_ok = 0;
	constexpr std::int32_t s_false = 1;
	constexpr std::int32_t e_fail = static_cast<std::int32_t>(0x80004005u);
	constexpr std::uint32_t invalid_target_id = contract::target_registry_capacity;
	constexpr std::uintptr_t default_frontend = 0x11110000;
	constexpr std::uintptr_t default_commands = 0x22220000;

	void require(const bool condition, const char* const message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	class fake_record
	{
	public:
		explicit fake_record(const std::uintptr_t frontend = default_frontend,
			const std::uintptr_t commands = default_commands,
			const std::uint32_t type = contract::expected_world_record_type)
		{
			write(contract::record_command_stream_offset, commands);
			write(contract::record_frontend_offset, frontend);
			write(contract::record_type_offset, type);
			write(contract::record_target_0_offset, std::uint32_t{7});
			write(contract::record_target_1_offset, std::uint32_t{8});
			write(contract::record_target_2_offset, std::uint32_t{9});
			write(contract::record_target_selector_offset, std::uint32_t{1});
		}

		[[nodiscard]] std::uintptr_t address() noexcept
		{
			return reinterpret_cast<std::uintptr_t>(bytes_.data());
		}

	private:
		template <typename Value>
		void write(const std::uintptr_t offset, const Value value) noexcept
		{
			std::memcpy(bytes_.data() + static_cast<std::size_t>(offset), &value,
				sizeof(value));
		}

		static constexpr std::size_t byte_count =
			contract::record_target_selector_offset + sizeof(std::uint32_t);
		std::array<std::byte, byte_count> bytes_{};
	};

	void enable_clean_probe()
	{
		probe::set_enabled(false);
		probe::set_device_generation(0);
		require(probe::reset(), "clean probe reset did not quiesce");
		probe::set_enabled(true);
	}

	[[nodiscard]] probe::frontend_record_observation observation(
		fake_record& record, const std::uintptr_t frontend = default_frontend,
		const std::uint64_t epoch = 1, const std::uint64_t transaction = 1,
		const std::uint32_t index = 0)
	{
		return {
			frontend,
			record.address(),
			index,
			contract::expected_world_record_type,
			epoch,
			transaction,
		};
	}

	void publish_exact(fake_record& record, const std::uint64_t epoch = 1,
		const std::uint64_t transaction = 1, const std::uint32_t index = 0)
	{
		require(probe::publish_frontend_record(
			observation(record, default_frontend, epoch, transaction, index)),
			"exact frontend publication was rejected");
	}

	void prepare_query_stream(const std::uint64_t device_generation,
		const std::int32_t present_result = s_ok)
	{
		enable_clean_probe();
		probe::set_device_generation(device_generation);
		probe::record_present_result(device_generation, 100, present_result);
	}

	void publish_query(const std::uintptr_t query,
		const std::uint64_t generation_before,
		const std::uint64_t generation_after,
		const std::uint64_t frontend_epoch = 1)
	{
		require(probe::record_query_publish(query, generation_before,
			generation_after, frontend_epoch),
			"valid query publication was rejected");
	}

	void expect_disabled_does_not_record()
	{
		fake_record record;
		probe::set_enabled(false);
		require(probe::reset(), "disabled probe reset did not quiesce");

		auto empty = probe::backend_token{};
		require(!probe::publish_frontend_record(observation(record)),
			"disabled probe accepted a frontend publication");
		require(!probe::begin_backend(record.address()),
			"disabled probe began a backend transaction");
		probe::record_post_bind(empty, record.address(), invalid_target_id);
		probe::record_dispatch(empty, default_commands,
			probe::observation_stage::before_call);
		probe::end_backend(empty, probe::backend_completion::incomplete,
			default_commands);
		probe::record_present_result(7, 1, s_ok);
		require(!probe::record_query_publish(0x1000, 0, 1, 1),
			"disabled probe accepted a query publication");
		require(probe::record_query_result(0x1000, 0, s_ok, true) ==
			probe::query_result_class::identity_mismatch,
			"disabled probe returned a terminal query result");

		const auto state = probe::get_status();
		require(!state.enabled && state.newest_trace_sequence == 0 &&
			state.frontend_publications == 0 && state.backend_transactions == 0 &&
			state.backend_orphan_dispatches == 0 && state.query_publications == 0 &&
			state.query_results == 0 && state.present_results == 0,
			"disabled probe changed observation counters or trace state");
	}

	void expect_default_trace_off_preserves_backend()
	{
		require(!vr::debug_options::enabled(vr::debug_options::probe::view),
			"detailed backend traces must default off");
		enable_clean_probe();
		fake_record record;
		publish_exact(record, 9, 90, 1);
		auto token = probe::begin_backend(record.address());
		require(token && token.frontend_epoch == 9 && token.frontend_transaction_id == 90 &&
			probe::get_backend_watchdog_status().depth == 1,
			"trace-off backend lost its production token or watchdog");
		probe::record_dispatch(token, default_commands, probe::observation_stage::before_call);
		probe::record_dispatch(token, default_commands, probe::observation_stage::after_call);
		probe::end_backend(token, probe::backend_completion::cpu_dispatch_return, default_commands);
		probe::set_device_generation(1);
		probe::record_present_result(1, 1, s_ok);
		publish_query(0x10000, 0, 1);
		require(probe::record_query_result(0x10000, 0, s_ok, true) ==
			probe::query_result_class::complete,
			"trace-off backend lost exact query completion observation");
		const auto state = probe::get_status();
		std::array<probe::trace_record, 1> records{};
		require(!token && state.backend_transactions == 1 && state.backend_active == 0 &&
			state.backend_cpu_dispatch_returns == 1 && state.query_complete_results == 1 &&
			probe::get_backend_watchdog_status().depth == 0 && state.newest_trace_sequence == 0 &&
			probe::read_recent(records.data(), records.size()) == 0,
			"trace-off backend changed lifecycle behavior or published detailed events");
	}

	void expect_reset_refuses_active_backend_transaction()
	{
		fake_record record;
		enable_clean_probe();
		publish_exact(record, 70, 700, 0);
		auto token = probe::begin_backend(record.address());
		require(token && probe::get_status().backend_active == 1,
			"reset concurrency setup did not retain an active backend transaction");

		const auto reset_succeeded = probe::reset();
		require(!reset_succeeded && !probe::is_enabled(),
			"reset cleared storage while a backend transaction was still active");
		auto state = probe::get_status();
		require(state.backend_active == 1 && state.backend_transactions == 1 &&
			state.frontend_publications == 1,
			"failed reset discarded active backend evidence");

		probe::end_backend(token, probe::backend_completion::incomplete,
			default_commands);
		require(!token && probe::get_status().backend_active == 0,
			"disabled observer did not drain the pre-reset backend token");
		require(probe::reset(),
			"reset did not succeed after the active backend transaction drained");
		state = probe::get_status();
		require(!state.enabled && state.backend_transactions == 0 &&
			state.frontend_publications == 0 && state.newest_trace_sequence == 0,
			"quiescent reset did not clear prior backend evidence");
	}

	void expect_reset_quiesces_concurrent_publications()
	{
		fake_record record;
		enable_clean_probe();
		std::atomic_bool stop{};
		std::atomic_uint64_t frontend_iterations{};
		std::atomic_uint64_t query_iterations{};

		std::thread frontend_writer([&]
		{
			while (!stop.load(std::memory_order_acquire))
			{
				if (!probe::is_enabled())
				{
					std::this_thread::yield();
					continue;
				}
				const auto iteration = frontend_iterations.fetch_add(1,
					std::memory_order_relaxed) + 1;
				(void)probe::publish_frontend_record(observation(record,
					default_frontend, iteration, iteration, 0));
			}
		});
		std::thread query_writer([&]
		{
			std::uint64_t generation{};
			while (!stop.load(std::memory_order_acquire))
			{
				if (!probe::is_enabled())
				{
					std::this_thread::yield();
					continue;
				}
				probe::set_device_generation(90);
				probe::record_present_result(90, generation + 1, s_ok);
				const auto query = 0x90000 +
					static_cast<std::uintptr_t>((generation & 63) * 0x10);
				if (probe::record_query_publish(query, generation, generation + 1,
					generation + 1))
				{
					(void)probe::record_query_result(query, generation, s_ok, true);
				}
				++generation;
				query_iterations.store(generation, std::memory_order_release);
			}
		});

		while (frontend_iterations.load(std::memory_order_acquire) < 100 ||
			query_iterations.load(std::memory_order_acquire) < 100)
		{
			std::this_thread::yield();
		}
		for (std::uint32_t iteration{}; iteration < 32; ++iteration)
		{
			require(probe::reset(),
				"reset could not quiesce concurrent frontend/query observers");
			probe::set_enabled(true);
		}
		probe::set_enabled(false);
		stop.store(true, std::memory_order_release);
		frontend_writer.join();
		query_writer.join();
		require(probe::reset(),
			"final reset did not quiesce concurrent frontend/query observers");
		const auto state = probe::get_status();
		require(!state.enabled && state.newest_trace_sequence == 0 &&
			state.frontend_publications == 0 && state.query_publications == 0,
			"concurrent reset left partially cleared observer storage");
	}

	void expect_frontend_exact_mapping_and_oldest_claim()
	{
		fake_record record;
		enable_clean_probe();
		require(probe::publish_frontend_record(
			observation(record, 0x33330000, 10, 100, 3)),
			"nonmatching frontend publication was rejected");
		require(probe::publish_frontend_record(
			observation(record, default_frontend, 11, 101, 1)),
			"first exact frontend publication was rejected");
		require(probe::publish_frontend_record(
			observation(record, default_frontend, 12, 102, 2)),
			"second exact frontend publication was rejected");

		auto first = probe::begin_backend(record.address());
		require(first && first.frontend_epoch == 11 &&
			first.frontend_transaction_id == 101 && first.record_index == 1 &&
			first.publication_sequence == 2 &&
			!probe::has_fault(first.faults, probe::backend_fault::mapping_miss),
			"backend did not claim the oldest exact record/frontend publication");
		probe::end_backend(first, probe::backend_completion::incomplete);

		auto second = probe::begin_backend(record.address());
		require(second && second.frontend_epoch == 12 &&
			second.frontend_transaction_id == 102 && second.record_index == 2 &&
			second.publication_sequence == 3,
			"record-address reuse did not advance to the next exact publication");
		probe::end_backend(second, probe::backend_completion::incomplete);

		auto miss = probe::begin_backend(record.address());
		require(miss && miss.frontend_epoch == 0 &&
			probe::has_fault(miss.faults, probe::backend_fault::mapping_miss),
			"frontend identity mismatch was incorrectly claimed by record address alone");
		probe::end_backend(miss, probe::backend_completion::incomplete);

		const auto state = probe::get_status();
		require(state.frontend_publications == 3 && state.frontend_claims == 2 &&
			state.frontend_mapping_misses == 1,
			"frontend exact-match accounting is incorrect");
	}

	void expect_backend_order_and_cpu_return_only()
	{
		fake_record record;
		enable_clean_probe();
		publish_exact(record, 21, 201, 2);

		auto token = probe::begin_backend(record.address());
		require(static_cast<bool>(token), "backend transaction did not begin");
		auto watchdog = probe::get_backend_watchdog_status();
		require(watchdog.depth == 1 && watchdog.record == record.address() &&
			watchdog.phase == probe::backend_watchdog_phase::backend_entered,
			"backend begin was not visible to the watchdog");

		// target_registry_capacity is deliberately outside the H2 registry. The
		// probe may inspect the fake record, but must return before the fixed registry.
		probe::record_post_bind(token, record.address(), invalid_target_id);
		watchdog = probe::get_backend_watchdog_status();
		require(watchdog.depth == 1 &&
			watchdog.phase == probe::backend_watchdog_phase::post_bind,
			"post-bind did not advance the watchdog");

		probe::record_dispatch(token, default_commands,
			probe::observation_stage::before_call);
		require(probe::get_status().backend_cpu_dispatch_returns == 0,
			"dispatch entry was misclassified as a CPU return");
		probe::record_dispatch(token, default_commands,
			probe::observation_stage::after_call);
		require(probe::get_status().backend_cpu_dispatch_returns == 1,
			"dispatch return was not recorded at the CPU call boundary");
		probe::end_backend(token, probe::backend_completion::cpu_dispatch_return,
			default_commands);
		require(!token, "end_backend did not consume its token");

		const auto state = probe::get_status();
		require(state.backend_transactions == 1 && state.backend_post_binds == 1 &&
			state.backend_dispatch_enters == 1 &&
			state.backend_cpu_dispatch_returns == 1 && state.backend_active == 0 &&
			state.backend_target_invalid == 1,
			"backend CPU-return-only accounting is incorrect");
		watchdog = probe::get_backend_watchdog_status();
		require(watchdog.depth == 0 &&
			watchdog.phase == probe::backend_watchdog_phase::idle,
			"backend end left the watchdog armed");

		std::array<probe::trace_record, 6> records{};
		const auto count = probe::read_recent(records.data(), records.size());
		require(count == records.size(), "backend transaction emitted the wrong event count");
		const std::array expected_kinds{
			probe::event_kind::backend_begin,
			probe::event_kind::backend_post_bind,
			probe::event_kind::backend_target,
			probe::event_kind::backend_dispatch,
			probe::event_kind::backend_dispatch,
			probe::event_kind::backend_end,
		};
		for (std::size_t index{}; index < records.size(); ++index)
		{
			require(records[index].kind == expected_kinds[index],
				"backend trace order changed");
			require(records[index].trace_id == records.front().trace_id,
				"backend trace correlation id changed inside one transaction");
		}
		require(records[3].stage == probe::observation_stage::before_call &&
			records[4].stage == probe::observation_stage::after_call &&
			records[5].stage == probe::observation_stage::leave &&
			records[4].values[6] == static_cast<std::uint64_t>(
				probe::backend_completion::cpu_dispatch_return) &&
			records[5].values[6] == static_cast<std::uint64_t>(
				probe::backend_completion::cpu_dispatch_return),
			"backend dispatch/end stages lost CPU-return semantics");
		require(probe::has_fault(static_cast<probe::backend_faults>(records[1].values[7]),
			probe::backend_fault::target_invalid) && records[1].values[2] == 0,
			"sentinel target did not safely skip the fixed H2 registry");
	}

	void expect_null_incomplete_and_mismatch_paths()
	{
		{
			fake_record record(default_frontend, 0);
			enable_clean_probe();
			publish_exact(record);
			auto token = probe::begin_backend(record.address());
			require(token && token.command_stream == 0,
				"null-command backend transaction did not begin");
			probe::end_backend(token, probe::backend_completion::dispatch_skipped_null);
			const auto state = probe::get_status();
			require(state.backend_dispatch_skipped_null == 1 &&
				state.backend_dispatch_enters == 0 &&
				state.backend_cpu_dispatch_returns == 0 && state.backend_active == 0,
				"null command was not classified as a skipped CPU dispatch");
		}

		{
			fake_record record;
			enable_clean_probe();
			publish_exact(record);
			auto token = probe::begin_backend(record.address());
			probe::record_dispatch(token, default_commands,
				probe::observation_stage::before_call);
			probe::end_backend(token, probe::backend_completion::incomplete,
				default_commands);
			const auto state = probe::get_status();
			require(state.backend_incomplete == 1 &&
				state.backend_dispatch_enters == 1 &&
				state.backend_cpu_dispatch_returns == 0 && state.backend_active == 0,
				"missing dispatch return was not preserved as incomplete");
		}

		{
			fake_record record;
			enable_clean_probe();
			publish_exact(record);
			auto token = probe::begin_backend(record.address());
			probe::record_post_bind(token, record.address() + 8, invalid_target_id);
			probe::record_dispatch(token, default_commands + 1,
				probe::observation_stage::before_call);
			probe::end_backend(token, probe::backend_completion::incomplete,
				default_commands + 2);
			const auto state = probe::get_status();
			require(state.backend_record_mismatches == 1 &&
				state.backend_command_mismatches == 1 && state.backend_incomplete == 1,
				"record/command mismatches were not retained");

			std::array<probe::trace_record, 5> records{};
			require(probe::read_recent(records.data(), records.size()) == records.size(),
				"mismatch transaction emitted the wrong event count");
			const auto faults = static_cast<probe::backend_faults>(records.back().values[7]);
			require(probe::has_fault(faults, probe::backend_fault::record_mismatch) &&
				probe::has_fault(faults, probe::backend_fault::command_mismatch) &&
				probe::has_fault(faults, probe::backend_fault::target_invalid),
				"backend end did not retain accumulated mismatch faults");
		}
	}

	void expect_query_exact_identity_and_pointer_reuse()
	{
		prepare_query_stream(10);
		constexpr std::uintptr_t query = 0x10000;
		publish_query(query, 40, 41, 400);
		require(probe::get_status().latest_query_generation == 40,
			"query identity used generation_after instead of consumer generation_before");
		require(probe::record_query_result(query + 8, 40, s_ok, true) ==
			probe::query_result_class::identity_mismatch,
			"different query pointer completed a publication");
		require(probe::record_query_result(query, 41, s_ok, true) ==
			probe::query_result_class::identity_mismatch,
			"generation_after incorrectly completed a query publication");
		require(probe::record_query_result(query, 40, s_ok, true) ==
			probe::query_result_class::complete,
			"exact pointer/generation_before query result did not complete");
		auto state = probe::get_status();
		require(state.query_complete_results == 1 &&
			state.query_unbound_completions == 1 &&
			state.query_identity_mismatches == 2 &&
			state.query_generation_mismatches == 1 &&
			state.backend_transactions == 0 && state.backend_active == 0,
			"exact query completion was not retained solely as unbound evidence");

		prepare_query_stream(11);
		publish_query(query, 80, 81, 800);
		publish_query(query, 81, 82, 801);
		require(probe::record_query_result(query, 80, s_ok, true) ==
			probe::query_result_class::complete,
			"reused pointer did not match its older exact generation");
		require(probe::record_query_result(query, 81, s_ok, true) ==
			probe::query_result_class::complete,
			"reused pointer did not match its newer exact generation");
		state = probe::get_status();
		require(state.query_complete_results == 2 &&
			state.query_unbound_completions == 2 &&
			state.query_identity_mismatches == 0,
			"pointer reuse was not disambiguated by exact generation_before");
	}

	void expect_query_nonterminal_and_failed_results()
	{
		prepare_query_stream(20);
		constexpr std::uintptr_t query = 0x20000;
		publish_query(query, 5, 6, 500);
		require(probe::record_query_result(query, 5, s_false, true) ==
			probe::query_result_class::pending,
			"S_FALSE was promoted to query completion");
		require(probe::record_query_result(query, 5, s_ok, false) ==
			probe::query_result_class::pending,
			"S_OK/FALSE was promoted to query completion");
		require(probe::record_query_result(query, 5, e_fail, true) ==
			probe::query_result_class::failed,
			"FAILED query result was not classified as failed");
		require(probe::record_query_result(query, 5, s_ok, true) ==
			probe::query_result_class::identity_mismatch,
			"resolved FAILED publication was later completed");

		const auto state = probe::get_status();
		require(state.query_pending_results == 2 && state.query_failed_results == 1 &&
			state.query_complete_results == 0 && state.query_unbound_completions == 0,
			"nonterminal/failed query results produced a false completion");
	}

	void expect_query_device_present_and_staleness_gates()
	{
		constexpr std::uintptr_t query = 0x30000;

		enable_clean_probe();
		publish_query(query, 0, 1);
		require(probe::record_query_result(query, 0, s_ok, true) ==
			probe::query_result_class::identity_mismatch &&
			probe::get_status().query_complete_results == 0,
			"zero device generation allowed a false query completion");

		enable_clean_probe();
		probe::set_device_generation(30);
		publish_query(query, 0, 1);
		require(probe::record_query_result(query, 0, s_ok, true) ==
			probe::query_result_class::identity_mismatch,
			"missing Present prerequisite allowed query completion");
		auto state = probe::get_status();
		require(state.query_present_prerequisite_misses == 1 &&
			state.query_complete_results == 0,
			"missing Present prerequisite was not diagnosed");

		prepare_query_stream(31);
		publish_query(query, 0, 1);
		probe::set_device_generation(32);
		probe::record_present_result(32, 101, s_ok);
		require(probe::record_query_result(query, 0, s_ok, true) ==
			probe::query_result_class::identity_mismatch,
			"query from an old device generation completed on a new device");
		state = probe::get_status();
		require(state.query_device_mismatches == 1 &&
			state.query_complete_results == 0,
			"device-generation mismatch was not diagnosed");

		prepare_query_stream(40, e_fail);
		publish_query(query, 0, 1);
		require(probe::record_query_result(query, 0, s_ok, true) ==
			probe::query_result_class::identity_mismatch,
			"failed Present allowed query completion");
		state = probe::get_status();
		require(state.query_present_prerequisite_misses == 1 &&
			state.query_complete_results == 0 &&
			state.query_unbound_completions == 0,
			"failed Present produced a false completion");

		prepare_query_stream(50);
		constexpr std::uintptr_t stale_query = 0x40000;
		publish_query(stale_query, 0, 1);
		for (std::uint64_t generation = 1; generation <= 9; ++generation)
		{
			publish_query(0x41000 + static_cast<std::uintptr_t>(generation * 0x10),
				generation, generation + 1, generation + 1);
		}
		require(probe::get_status().latest_query_generation == 9,
			"query generation setup did not exceed the eight-generation window");
		require(probe::record_query_result(stale_query, 0, s_ok, true) ==
			probe::query_result_class::identity_mismatch,
			"query older than eight generations completed");
		state = probe::get_status();
		require(state.query_stale_generations == 1 &&
			state.query_complete_results == 0 &&
			state.query_unbound_completions == 0,
			"stale query generation produced a false completion");
	}

	void expect_trace_ring_wrap()
	{
		enable_clean_probe();
		constexpr std::size_t extra_records = 7;
		for (std::size_t index{}; index < probe::trace_capacity + extra_records; ++index)
		{
			probe::record_present_result(60, index + 1, s_ok);
		}

		const auto state = probe::get_status();
		require(state.newest_trace_sequence == probe::trace_capacity + extra_records &&
			state.trace_overwrite_count == extra_records && state.trace_drop_count == 0,
			"backend/query trace ring wrap accounting is incorrect");
		std::array<probe::trace_record, 4> records{};
		require(probe::read_recent(records.data(), records.size()) == records.size(),
			"wrapped trace did not return the requested newest records");
		require(records.front().sequence == state.newest_trace_sequence - 3 &&
			records.back().sequence == state.newest_trace_sequence,
			"wrapped trace returned stale or reordered records");
		for (const auto& record : records)
		{
			require(record.kind == probe::event_kind::present_result,
				"wrapped trace returned an unexpected event kind");
		}

		probe::set_enabled(false);
		probe::record_present_result(60, state.newest_trace_sequence + 1, s_ok);
		require(probe::get_status().newest_trace_sequence == state.newest_trace_sequence,
			"disabled wrapped trace accepted another record");
	}

	void expect_target_prepare_evidence()
	{
		enable_clean_probe();
		probe::record_target_prepare({
			0x50000000, 0x50001000, 1, 4,
			31, 0x200, 0x2C98, 0x17,
			{1, 4, 5}, {1, 5, 4}, 0, 1, true, 0x1407A9000,
		});
		probe::record_target_prepare({
			0, 0xDEAD, contract::frontend_record_capacity, 0,
			0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0,
			{}, {}, 0, 0, false,
		});
		const auto state = probe::get_status();
		require(state.target_prepare_calls == 2 &&
			state.target_prepare_valid_records == 1 &&
			state.target_prepare_invalid_records == 1 &&
			state.target_prepare_changed_records == 1 &&
			state.target_prepare_target_changes == 1,
			"target-prepare accounting changed");
		const auto formatted = probe::format_recent(4);
		require(formatted.find("domain=target_prepare") != std::string::npos &&
			formatted.find("target_prepare/after_call") != std::string::npos &&
			formatted.find("caller=0x1407a9000") != std::string::npos &&
			formatted.find("changed_bytes=31") != std::string::npos &&
			formatted.find("target1=4->5") != std::string::npos &&
			formatted.find("target2=5->4") != std::string::npos &&
			formatted.find("selector=0->1") != std::string::npos,
			"target-prepare evidence was not formatted");
	}
}

int main()
{
	try
	{
		expect_default_trace_off_preserves_backend();
		vr::debug_options::selection diagnostics{};
		diagnostics[static_cast<std::size_t>(vr::debug_options::probe::view)] = true;
		vr::debug_options::initialize(diagnostics);
		expect_disabled_does_not_record();
		expect_frontend_exact_mapping_and_oldest_claim();
		expect_backend_order_and_cpu_return_only();
		expect_null_incomplete_and_mismatch_paths();
		expect_query_exact_identity_and_pointer_reuse();
		expect_query_nonterminal_and_failed_results();
		expect_query_device_present_and_staleness_gates();
		expect_target_prepare_evidence();
		expect_trace_ring_wrap();
		expect_reset_refuses_active_backend_transaction();
		expect_reset_quiesces_concurrent_publications();
		std::cout << "vr-engine-backend-probe-smoke: PASS\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "vr-engine-backend-probe-smoke: FAIL; " << error.what() << '\n';
		return 1;
	}
}
