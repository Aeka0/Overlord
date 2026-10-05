#include <std_include.hpp>

#include "component/vr/engine_stereo_dxbc_declarations.hpp"
#include "component/vr/engine_stereo_ssr_consumer_window.hpp"

#include <d3dcompiler.h>

#include <cstdlib>
#include <iostream>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	using namespace vr::engine_stereo_ssr_consumer_window;

	void require(const bool value, const char* const message)
	{
		if (value) return;
		std::cerr << "vr-engine-ssr-consumer-window-probe: FAIL; " << message << '\n';
		std::exit(1);
	}

	void complete_pair(state& value, const std::uint64_t pair_id,
		const std::uintptr_t left_shader, const std::uintptr_t right_shader,
		const bool successful = true)
	{
		require(begin_pair(value, pair_id, false), "pair did not begin");
		require(begin_eye(value, pair_id, 0), "left eye did not begin");
		if (left_shader != 0) require(note_consumer(value, pair_id, 0, left_shader),
			"left consumer was rejected");
		require(end_eye(value, pair_id, 0), "left eye did not end");
		require(begin_eye(value, pair_id, 1), "right eye did not begin");
		if (right_shader != 0) require(note_consumer(value, pair_id, 1, right_shader),
			"right consumer was rejected");
		require(end_eye(value, pair_id, 1), "right eye did not end");
		const auto ended = end_pair(value, pair_id, successful);
		if (successful && !terminal(value)) require(ended, "successful pair did not end");
	}

	void expect_seed_skip_and_two_pair_completion()
	{
		state value{};
		require(!begin_pair(value, 10, true), "seed pair was admitted");
		require(value.skipped_seed_pairs == 1 && value.current == phase::waiting,
			"seed skip state is wrong");
		complete_pair(value, 11, 0x111, 0x111);
		require(value.current == phase::sampling && value.sampled_pairs == 1,
			"first steady pair completed too early");
		complete_pair(value, 12, 0x111, 0x111);
		require(value.current == phase::complete && value.sampled_pairs == 2 &&
			value.sampled_eye_pairs[0] == 2 && value.sampled_eye_pairs[1] == 2,
			"two-eye steady evidence did not complete");
		require(value.candidate_shader == 0x111,
			"completed evidence did not retain one shader identity");
	}

	void expect_same_id_retry_after_unsuccessful_pair()
	{
		state value{};
		complete_pair(value, 40, 0x222, 0x222, false);
		require(value.current == phase::sampling && value.unsuccessful_pairs == 1 &&
			!value.last_pair_successful, "failed attempt was not retryable");
		complete_pair(value, 40, 0x222, 0x222);
		require(value.successful_pairs == 1 && value.sampled_pairs == 1 &&
			value.last_pair_successful, "same-id retry was not accepted");
		require(!begin_pair(value, 40, false) && value.current == phase::failed &&
			value.error == failure::pair_order,
			"same-id retry after success was not rejected");
	}

	void expect_same_id_retry_after_seed_skip()
	{
		state value{};
		require(!begin_pair(value, 50, true), "seed pair was admitted");
		complete_pair(value, 50, 0x333, 0x333);
		require(value.current == phase::sampling && value.successful_pairs == 1,
			"seed-skipped pair id was not reusable");
	}

	void expect_four_sample_limit()
	{
		state value{};
		complete_pair(value, 60, 0x444, 0);
		complete_pair(value, 61, 0x444, 0);
		complete_pair(value, 62, 0x444, 0);
		complete_pair(value, 63, 0x444, 0);
		require(value.current == phase::failed &&
			value.error == failure::insufficient_evidence &&
			value.successful_pairs == maximum_sampled_pairs,
			"four-pair incomplete evidence did not fail closed");
	}

	void expect_permutation_intersection()
	{
		state value{};
		require(begin_pair(value, 65, false), "permutation pair did not begin");
		require(begin_eye(value, 65, 0), "permutation left eye did not begin");
		require(note_consumer(value, 65, 0, 0x555),
			"first left permutation was rejected");
		require(note_consumer(value, 65, 0, 0x556),
			"shared left permutation was rejected");
		require(end_eye(value, 65, 0), "permutation left eye did not end");
		require(begin_eye(value, 65, 1), "permutation right eye did not begin");
		require(note_consumer(value, 65, 1, 0x557),
			"first right permutation was rejected");
		require(note_consumer(value, 65, 1, 0x556),
			"shared right permutation was rejected");
		require(end_eye(value, 65, 1), "permutation right eye did not end");
		require(end_pair(value, 65, true), "permutation pair did not end");
		require(value.sampled_pairs == 1 && value.candidate_shader == 0 &&
			value.cross_pair_candidate_count == 1 &&
			value.cross_pair_candidates[0] == 0x556,
			"first pair was selected before cross-pair evidence existed");

		require(begin_pair(value, 66, false), "second permutation pair did not begin");
		require(begin_eye(value, 66, 0), "second left eye did not begin");
		require(note_consumer(value, 66, 0, 0x558) &&
			note_consumer(value, 66, 0, 0x556),
			"second left candidate set failed");
		require(end_eye(value, 66, 0), "second left eye did not end");
		require(begin_eye(value, 66, 1), "second right eye did not begin");
		require(note_consumer(value, 66, 1, 0x556) &&
			note_consumer(value, 66, 1, 0x559),
			"second right candidate set failed");
		require(end_eye(value, 66, 1), "second right eye did not end");
		require(end_pair(value, 66, true), "second permutation pair did not end");
		require(value.current == phase::complete && value.sampled_pairs == 2 &&
			value.candidate_shader == 0x556,
			"stable candidate did not complete across permutations");
	}

	void expect_cross_pair_set_intersection_before_selection()
	{
		state value{};
		require(begin_pair(value, 67, false), "multi-candidate pair did not begin");
		for (std::uint32_t eye{}; eye < 2; ++eye)
		{
			require(begin_eye(value, 67, eye), "multi-candidate eye did not begin");
			require(note_consumer(value, 67, eye, 0x670) &&
				note_consumer(value, 67, eye, 0x671),
				"multi-candidate eye set was rejected");
			require(end_eye(value, 67, eye), "multi-candidate eye did not end");
		}
		require(end_pair(value, 67, true) && value.candidate_shader == 0 &&
			value.cross_pair_candidate_count == 2,
			"first pair prematurely locked one of two candidates");

		complete_pair(value, 68, 0x671, 0x671);
		require(value.current == phase::complete &&
			value.candidate_shader == 0x671,
			"cross-pair intersection did not retain the stable candidate");
	}

	void expect_later_repeated_candidate_is_not_blocked_by_first_pair()
	{
		state value{};
		complete_pair(value, 74, 0x740, 0x740);
		complete_pair(value, 75, 0x741, 0x741);
		require(value.current == phase::sampling && value.sampled_pairs == 1 &&
			value.candidate_shader == 0,
			"two different one-pair candidates produced a false closure");
		complete_pair(value, 76, 0x741, 0x741);
		require(value.current == phase::complete && value.sampled_pairs == 2 &&
			value.candidate_shader == 0x741,
			"first-pair candidate blocked a later repeated candidate");
	}

	void expect_cross_pair_candidate_absence_fails_closed()
	{
		state value{};
		complete_pair(value, 66, 0x666, 0x666);
		complete_pair(value, 67, 0x667, 0x667);
		complete_pair(value, 68, 0x668, 0x668);
		complete_pair(value, 69, 0x669, 0x669);
		require(value.current == phase::failed &&
			value.error == failure::insufficient_evidence && value.sampled_pairs == 1 &&
			value.candidate_shader == 0,
			"candidate absence did not fail closed without a false conflict");
	}

	void expect_sample_overflow_preserves_candidate_closure()
	{
		require(select_observation_path(false, false) == observation_path::ignore &&
			select_observation_path(false, true) == observation_path::detailed_sample,
			"non-candidate sample policy is wrong");
		require(select_observation_path(true, false) ==
			observation_path::candidate_only,
			"sample exhaustion suppressed candidate-only observation");

		state value{};
		for (std::uint64_t pair_id = 72; pair_id < 74; ++pair_id)
		{
			require(begin_pair(value, pair_id, false),
				"overflow candidate pair did not begin");
			for (std::uint32_t eye{}; eye < 2; ++eye)
			{
				require(begin_eye(value, pair_id, eye),
					"overflow candidate eye did not begin");
				const auto path = select_observation_path(true, false);
				require(path == observation_path::candidate_only &&
					note_consumer(value, pair_id, eye, 0x72A),
					"candidate-only observation did not reach the eye set");
				require(end_eye(value, pair_id, eye),
					"overflow candidate eye did not end");
			}
			const auto ended = end_pair(value, pair_id, true);
			if (!terminal(value)) require(ended,
				"overflow candidate pair did not end");
		}
		require(value.current == phase::complete && value.sampled_pairs == 2 &&
			value.candidate_shader == 0x72A,
			"sample exhaustion prevented the two-pair candidate closure");
	}

	void expect_eye_order_failure()
	{
		state value{};
		require(begin_pair(value, 70, false), "order test pair did not begin");
		require(!begin_eye(value, 70, 1) && value.current == phase::failed &&
			value.error == failure::eye_order, "right-first eye order was accepted");
	}

	void expect_successful_pair_requires_both_eyes_ended()
	{
		state value{};
		require(begin_pair(value, 71, false), "incomplete-eye pair did not begin");
		require(begin_eye(value, 71, 0), "incomplete-eye left did not begin");
		require(note_consumer(value, 71, 0, 0x711),
			"incomplete-eye left consumer was rejected");
		require(end_eye(value, 71, 0), "incomplete-eye left did not end");
		require(begin_eye(value, 71, 1), "incomplete-eye right did not begin");
		require(note_consumer(value, 71, 1, 0x711),
			"incomplete-eye right consumer was rejected");
		require(!end_pair(value, 71, true) && value.current == phase::failed &&
			value.error == failure::eye_order && value.sampled_pairs == 0,
			"successful pair accepted an eye that had not ended");
	}

	void expect_focused_pair_retains_two_eye_content_completeness()
	{
		focused_state value{};
		require(arm_focused(value, 0xABC), "focused candidate did not arm");
		require(!begin_focused_pair(value, 90, true) &&
			value.current == focused_phase::pending &&
			value.skipped_seed_pairs == 1,
			"focused seed pair was not skipped without consuming an attempt");
		require(begin_focused_pair(value, 90, false),
			"focused pair did not begin after seed skip");
		require(begin_focused_eye(value, 90, 0) &&
			note_focused_sample(value, 90, 0, 0xABC, true) &&
			end_focused_eye(value, 90, 0),
			"focused left sample failed");
		require(begin_focused_eye(value, 90, 1) &&
			note_focused_sample(value, 90, 1, 0xABC, false) &&
			end_focused_eye(value, 90, 1),
			"focused right sample failed");
		require(end_focused_pair(value, 90, true) &&
			value.current == focused_phase::complete &&
			value.sample_eye_mask == 0x3 &&
			value.content_complete_eye_mask == 0x1,
			"focused capture hid incomplete right-eye CB content");
	}

	void expect_focused_complete_is_sticky_after_late_pair_begin()
	{
		focused_state value{};
		require(arm_focused(value, 0xACD), "sticky focused candidate did not arm");
		require(begin_focused_pair(value, 91, false),
			"sticky focused pair did not begin");
		for (std::uint32_t eye{}; eye < 2; ++eye)
		{
			require(begin_focused_eye(value, 91, eye) &&
				note_focused_sample(value, 91, eye, 0xACD, eye == 0) &&
				end_focused_eye(value, 91, eye),
				"sticky focused eye lifecycle failed");
		}
		require(end_focused_pair(value, 91, true),
			"sticky focused pair did not complete");

		const auto error = value.error;
		const auto completed_eye_mask = value.completed_eye_mask;
		const auto sample_eye_mask = value.sample_eye_mask;
		const auto content_complete_eye_mask = value.content_complete_eye_mask;
		require(!begin_focused_pair(value, 92, false) &&
			value.current == focused_phase::complete && value.error == error &&
			value.completed_eye_mask == completed_eye_mask &&
			value.sample_eye_mask == sample_eye_mask &&
			value.content_complete_eye_mask == content_complete_eye_mask,
			"late pair begin overwrote a terminal focused capture");
	}

	void expect_focused_failure_is_sticky_after_late_pair_begin()
	{
		focused_state value{};
		require(!arm_focused(value, 0) &&
			value.current == focused_phase::failed &&
			value.error == focused_failure::invalid_candidate,
			"focused failure fixture did not fail");
		const auto before = value;
		require(!begin_focused_pair(value, 93, false) &&
			value.current == before.current && value.error == before.error &&
			value.completed_eye_mask == before.completed_eye_mask &&
			value.sample_eye_mask == before.sample_eye_mask &&
			value.content_complete_eye_mask == before.content_complete_eye_mask,
			"late pair begin overwrote a terminal focused failure");
	}

	void expect_resource_write_window_retains_latest_draw_predecessor()
	{
		resource_write_window<3> value{};
		resource_write_metadata write{};
		write.destination = 0x100;
		write.caller = 0xA1;
		write.operation = resource_write_operation::clear_render_target;
		require(note_resource_write(value, write), "first resource write was rejected");
		write.destination = 0x200;
		write.caller = 0xB1;
		write.operation = resource_write_operation::copy_resource;
		require(note_resource_write(value, write), "second resource write was rejected");
		write.destination = 0x100;
		write.caller = 0xA2;
		write.eye = 1;
		write.output_target = 84;
		write.operation = resource_write_operation::draw_render_target;
		require(note_resource_write(value, write), "latest resource write was rejected");

		resource_write_metadata found{};
		require(find_last_resource_write(value, 0x100, found) &&
			found.sequence == 3 && found.caller == 0xA2 && found.eye == 1 &&
			found.output_target == 84 &&
			found.operation == resource_write_operation::draw_render_target,
			"resource lookup did not return the latest pre-draw write");
		const auto selected_snapshot = found;

		write.destination = 0x300;
		write.caller = 0xC1;
		write.operation = resource_write_operation::update_subresource;
		require(note_resource_write(value, write) && value.overflows == 1,
			"fixed write window did not report ring reuse");
		require(selected_snapshot.sequence == 3 && selected_snapshot.caller == 0xA2,
			"ring reuse mutated an already selected SRV write snapshot");
		require(!find_last_resource_write(value, 0x400, found) && !found,
			"unknown resource reported stale write metadata");

		write = {};
		write.destination = 0x500;
		require(!note_resource_write(value, write),
			"unknown write operation was accepted");
	}

	void expect_copy_source_lineage_selects_prior_source_writer()
	{
		resource_write_window<4> value{};
		resource_write_metadata producer{};
		producer.pair_id = 41;
		producer.eye = 0;
		producer.destination = 0xABC;
		producer.output_target = 91;
		producer.caller = 0x1407B2DE4ull;
		producer.operation = resource_write_operation::draw_unordered_access;
		require(note_resource_write(value, producer),
			"copy-source producer was rejected");

		resource_write_metadata copy{};
		copy.pair_id = 41;
		copy.eye = 1;
		copy.destination = 0xDEF;
		copy.source = 0xABC;
		copy.caller = 0x1403591BDull;
		copy.operation = resource_write_operation::copy_resource;
		resource_copy_source_lineage lineage{};
		require(capture_copy_source_lineage(value, copy, lineage) &&
			lineage.source_last_write_known &&
			lineage.copy.source == copy.source &&
			lineage.source_last_write.destination == copy.source &&
			lineage.source_last_write.pair_id == producer.pair_id &&
			lineage.source_last_write.eye == producer.eye &&
			lineage.source_last_write.output_target == producer.output_target &&
			lineage.source_last_write.caller == producer.caller,
			"copy-source lineage did not retain the latest source writer");

		copy.source = 0x999;
		require(capture_copy_source_lineage(value, copy, lineage) &&
			!lineage.source_last_write_known && !lineage.source_last_write,
			"unknown copy source reported stale producer metadata");
		copy.source = 0;
		require(!capture_copy_source_lineage(value, copy, lineage),
			"null copy source was accepted");
	}

	void expect_focused_absence_retries_then_fails_closed()
	{
		focused_state value{};
		require(arm_focused(value, 0xDEF), "focused retry candidate did not arm");
		for (std::uint32_t attempt{}; attempt < maximum_focused_pairs; ++attempt)
		{
			const auto pair_id = 100ull + attempt;
			require(begin_focused_pair(value, pair_id, false),
				"focused retry pair did not begin");
			for (std::uint32_t eye{}; eye < 2; ++eye)
			{
				require(begin_focused_eye(value, pair_id, eye) &&
					end_focused_eye(value, pair_id, eye),
					"focused retry eye lifecycle failed");
			}
			const auto ended = end_focused_pair(value, pair_id, true);
			if (attempt + 1 < maximum_focused_pairs)
			{
				require(ended && value.current == focused_phase::pending,
					"focused absence did not remain retryable");
			}
			else
			{
				require(!ended && value.current == focused_phase::failed &&
					value.error == focused_failure::sample_timeout,
					"focused absence did not fail closed at its bound");
			}
		}
	}

	void expect_stripped_sm50_declarations()
	{
		constexpr char disassembly[] =
			"// stripped reflection fixture\n"
			"ps_5_0\n"
			"dcl_resource_texture2d (float,float,float,float) t1\n"
			"dcl_resource_raw t10\n"
			"dcl_resource_structured t31, 16\n"
			"dcl_resource_texture2d (float,float,float,float) t100\n"
			"dcl_constantbuffer CB0[10], immediateIndexed\n"
			"dcl_constantbuffer CB3[19], immediateIndexed\n";
		vr::engine_stereo_dxbc_declarations::declarations<32, 14> parsed{};
		require(vr::engine_stereo_dxbc_declarations::parse(disassembly,
			sizeof(disassembly), parsed), "stripped ps_5_0 did not parse");
		require(parsed.profile == vr::engine_stereo_dxbc_declarations::
			shader_profile::ps_5_0, "ps_5_0 profile was not retained");
		require(parsed.shader_resources[1] && parsed.shader_resources[10] &&
			parsed.shader_resources[31], "SM5 resource declarations were lost");
		require(parsed.constant_buffers[0] && parsed.constant_buffers[3] &&
			!parsed.constant_buffers[10],
			"constant-buffer element count was mistaken for a slot");
		require(parsed.resource_declarations == 4 &&
			parsed.constant_buffer_declarations == 2 &&
			parsed.out_of_range_declarations == 1 &&
			parsed.malformed_declarations == 0,
			"declaration accounting is wrong");

		constexpr char sm51[] =
			"ps_5_1\n"
			"dcl_resource_texture2d (float,float,float,float) t1[10:*], space=0\n";
		parsed = {};
		require(!vr::engine_stereo_dxbc_declarations::parse(sm51,
			sizeof(sm51), parsed) && parsed.profile ==
			vr::engine_stereo_dxbc_declarations::shader_profile::unsupported,
			"SM5.1 range declarations were not rejected closed");

		constexpr char embedded_null[] = "ps_5_0\n\0dcl_resource_raw t10\n";
		parsed = {};
		require(!vr::engine_stereo_dxbc_declarations::parse(embedded_null,
			sizeof(embedded_null), parsed),
			"embedded NUL was accepted as executable declaration text");
	}

	void expect_real_stripped_disassembly_declarations(const char* const profile,
		const vr::engine_stereo_dxbc_declarations::shader_profile expected_profile)
	{
		constexpr char source[] =
			"cbuffer PerFrame : register(b3) { float4 PreviousValue; };\n"
			"Texture2D<float4> SceneMip : register(t10);\n"
			"float4 main() : SV_Target {\n"
			"  return SceneMip.Load(int3(0, 0, 0)) + PreviousValue;\n"
			"}\n";
		Microsoft::WRL::ComPtr<ID3DBlob> compiled;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		auto result = D3DCompile(source, sizeof(source) - 1,
			"ssr_consumer_fixture", nullptr, nullptr, "main", profile, 0, 0,
			&compiled, &errors);
		require(SUCCEEDED(result) && compiled != nullptr,
			"real SSR declaration fixture did not compile");

		Microsoft::WRL::ComPtr<ID3DBlob> stripped;
		result = D3DStripShader(compiled->GetBufferPointer(),
			compiled->GetBufferSize(), D3DCOMPILER_STRIP_REFLECTION_DATA,
			&stripped);
		require(SUCCEEDED(result) && stripped != nullptr,
			"real SSR declaration fixture did not strip reflection");

		Microsoft::WRL::ComPtr<ID3DBlob> disassembly;
		result = D3DDisassemble(stripped->GetBufferPointer(),
			stripped->GetBufferSize(), 0, nullptr, &disassembly);
		require(SUCCEEDED(result) && disassembly != nullptr,
			"real stripped SSR declaration fixture did not disassemble");

		vr::engine_stereo_dxbc_declarations::declarations<32, 14> parsed{};
		require(vr::engine_stereo_dxbc_declarations::parse(
			static_cast<const char*>(disassembly->GetBufferPointer()),
			disassembly->GetBufferSize(), parsed),
			"real stripped disassembly did not parse");
		require(parsed.profile == expected_profile &&
			parsed.shader_resources[10] && parsed.constant_buffers[3],
			"real stripped disassembly lost executable t10 or CB3");
	}

	void expect_real_d3dcompiler_declaration_pipeline()
	{
		expect_real_stripped_disassembly_declarations("ps_4_0",
			vr::engine_stereo_dxbc_declarations::shader_profile::ps_4_0);
		expect_real_stripped_disassembly_declarations("ps_4_1",
			vr::engine_stereo_dxbc_declarations::shader_profile::ps_4_1);
		expect_real_stripped_disassembly_declarations("ps_5_0",
			vr::engine_stereo_dxbc_declarations::shader_profile::ps_5_0);
	}
}

int main()
{
	expect_seed_skip_and_two_pair_completion();
	expect_same_id_retry_after_unsuccessful_pair();
	expect_same_id_retry_after_seed_skip();
	expect_four_sample_limit();
	expect_permutation_intersection();
	expect_cross_pair_set_intersection_before_selection();
	expect_later_repeated_candidate_is_not_blocked_by_first_pair();
	expect_cross_pair_candidate_absence_fails_closed();
	expect_sample_overflow_preserves_candidate_closure();
	expect_eye_order_failure();
	expect_successful_pair_requires_both_eyes_ended();
	expect_focused_pair_retains_two_eye_content_completeness();
	expect_focused_complete_is_sticky_after_late_pair_begin();
	expect_focused_failure_is_sticky_after_late_pair_begin();
	expect_resource_write_window_retains_latest_draw_predecessor();
	expect_copy_source_lineage_selects_prior_source_writer();
	expect_focused_absence_retries_then_fails_closed();
	expect_stripped_sm50_declarations();
	expect_real_d3dcompiler_declaration_pipeline();
	std::cout << "vr-engine-ssr-consumer-window-probe: PASS\n";
	return 0;
}
