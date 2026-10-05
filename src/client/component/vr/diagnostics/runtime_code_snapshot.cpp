#include <std_include.hpp>

#include "runtime_code_snapshot.hpp"

#include <utils/hook_validation.hpp>
#include <utils/cryptography.hpp>
#include <utils/io.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>

namespace vr::diagnostics
{
	namespace
	{
		bool is_readable_executable_protection(const std::uint32_t protection) noexcept
		{
			if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
			switch (protection & 0xFF)
			{
			case PAGE_EXECUTE_READ:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return true;
			default:
				return false;
			}
		}

		bool validate_snapshot_range(const std::uintptr_t begin,
			const std::uintptr_t end) noexcept
		{
			if (begin == 0 || end <= begin) return false;
			auto current = begin;
			while (current < end)
			{
				const auto validation = utils::hook_validation::validate_executable_target(
					reinterpret_cast<const void*>(current));
				if (!validation || !is_readable_executable_protection(validation.protection))
				{
					return false;
				}

				const auto region_begin = reinterpret_cast<std::uintptr_t>(
					validation.region_base);
				if (validation.region_size >
					(std::numeric_limits<std::uintptr_t>::max)() - region_begin)
				{
					return false;
				}
				const auto region_end = region_begin + validation.region_size;
				if (current < region_begin || current >= region_end) return false;
				current = (std::min)(end, region_end);
			}
			return true;
		}

	}

	bool write_runtime_code_snapshots() noexcept
	{
		try
		{
			struct code_region
			{
				const char* name;
				std::uintptr_t begin;
				std::uintptr_t end;
			};
			// H2's on-disk .text is encrypted. These fingerprint-specific runtime
			// bounds come from runtime disassembly/PE exception metadata and are copied
			// before this component modifies its five frontend observation callsites.
			constexpr std::array regions{
				code_region{"scene_owner_adapter", 0x140367CA0, 0x140367D59},
				code_region{"scene_owner", 0x1403CA1A0, 0x1403CA253},
				code_region{"scene_owner_parent", 0x1403AC750, 0x1403ACDB9},
				// Fixed-size clusters around local-client/view setup reached by
				// R_RenderScene. They are intentionally not labelled as function bounds.
				code_region{"scene_local_client_prepare_a_cluster", 0x1403ACDC0,
					0x1403ACE80},
				code_region{"scene_local_client_prepare_b_cluster", 0x1403AE270,
					0x1403AE370},
				code_region{"scene_owner_parent_2", 0x1403D8BE0, 0x1403D8D58},
				code_region{"scene_owner_parent_3", 0x1403D8E0D, 0x1403D8E75},
				code_region{"scene_record_reservation", 0x140779E00, 0x140779E33},
				code_region{"view_slot_allocator", 0x14076D5D0, 0x14076D5FA},
				code_region{"frontend_command_cluster", 0x14076D5D0, 0x14076D7F1},
				code_region{"frontend_end_command", 0x14076D040, 0x14076D0C5},
				code_region{"frontend_record_command_pointer_link", 0x14076D660,
					0x14076D692},
				code_region{"frontend_scene_command_emit_cluster", 0x14076D0C5,
					0x14076D1CE},
				code_region{"frontend_command_pointer_link_540fa8", 0x14076D790,
					0x14076D7AD},
				code_region{"render_scene", 0x14077B820, 0x14077BD53},
				code_region{"frontend_view_slot_initializer", 0x14077E340,
					0x14077E500},
				code_region{"frontend_scene_definition_copy", 0x140779E50,
					0x140779ECE},
				code_region{"frontend_view_matrix_finalizer", 0x140777E30,
					0x140777F30},
				code_region{"frontend_view_projection_builder", 0x14077C0A0,
					0x14077C192},
				code_region{"frontend_view_math_cluster", 0x14060EDC0,
					0x14060F400},
				code_region{"frontend_extended_scene_selective_copy", 0x14077CCF0,
					0x14077D278},
				code_region{"frontend_scene_worker_multistage_consumer", 0x140778C10,
					0x140778E51},
				code_region{"generate_draw_surfs_body_after_detour", 0x140778E65,
					0x140779689},
				code_region{"generator_per_view_output_initializer", 0x14077F4B0,
					0x14077F549},
				code_region{"client_scene_ui_tail_dispatch", 0x1403679E0,
					0x140367C89},
				code_region{"menu_find_by_name", 0x140603080, 0x1406030F2},
				code_region{"menu_context_dispatch", 0x140603480, 0x1406034AF},
				code_region{"frontend_record_initializer", 0x14077E500, 0x14077E872},
				code_region{"worker_enqueue", 0x1407928F0, 0x140792A5B},
				code_region{"worker_queue_12_or_1a_positive_predicate", 0x140792DD0,
					0x140792DF0},
				code_region{"worker_command_dispatch", 0x140793980, 0x140793D1D},
				code_region{"worker_command_dispatch_table", 0x140793D20, 0x140793DE0},
				code_region{"worker_wait_entry", 0x140793DE0, 0x140793DFE},
				code_region{"worker_wait_until_predicate", 0x140793DFE,
					0x140793FF7},
				code_region{"worker_wait_all", 0x140794200, 0x140794224},
				// The recurring worker AV is at 0x14044496F. Preserve the surrounding
				// decrypted code so a failure can be resolved without another game run.
				code_region{"worker_surface_index_cluster", 0x140443000, 0x140445000},
				code_region{"backend_record_command_pointer_callee_cluster",
					0x140799AD9, 0x140799B10},
				code_region{"backend_record_view_bind_leaf", 0x14079F6F0,
					0x14079F760},
				code_region{"backend_command_stream_dispatch", 0x14079EAE0,
					0x14079EBCB},
				code_region{"backend_view_state_commit_leaf", 0x140789AA0,
					0x140789B20},
				code_region{"backend_view_copy_owner", 0x14078A340,
					0x14078A75B},
				code_region{"backend_view_copy_leaf", 0x14078B5D0,
					0x14078B717},
				code_region{"backend_depth_hack_projection", 0x140786D30,
					0x140786DD3},
				code_region{"backend_target_prepare_owner", 0x14079A500,
					0x14079A653},
				code_region{"backend_render_target_select", 0x1407892C0,
					0x1407893C4},
				code_region{"backend_om_set_render_targets", 0x140781CA0,
					0x140781D9C},
				code_region{"backend_per_record_tail_callee", 0x1407A6BE0, 0x1407A6D28},
				code_region{"backend_record_target_index", 0x1407A7220,
					0x1407A7255},
				code_region{"backend_record_target_mode", 0x1407A73D0,
					0x1407A7410},
				code_region{"backend_current_record_target_prepare", 0x1407A7DA0,
					0x1407A8306},
				code_region{"backend_frame_cluster", 0x1407A0000, 0x1407A9000},
				code_region{"backend_record_render_loop", 0x1407A8370,
					0x1407A8608},
				code_region{"backend_record_command_scan_cluster_b", 0x1407A8E60,
					0x1407A8F6B},
				code_region{"backend_record_classifier", 0x1407B0740,
					0x1407B07F7},
				code_region{"h2_gpu_query_create_pool", 0x14074C2D0,
					0x14074C3A8},
				code_region{"h2_gpu_query_result_consumer", 0x1407506E0,
					0x14075084D},
				code_region{"h2_gpu_query_publish", 0x14074BB90,
					0x14074BBB4},
				code_region{"h2_gpu_query_issue_rotate", 0x1407A46E0,
					0x1407A47D9},
				code_region{"h2_present_and_query_publish", 0x1407A13A0,
					0x1407A1585},
			};
			constexpr auto image_base = std::uintptr_t{0x140000000};
			constexpr auto manifest_path = "minidumps/runtime-code/manifest.txt";
			const auto capture_tick = GetTickCount64();
			std::ostringstream capture_id_stream;
			capture_id_stream << std::hex << GetCurrentProcessId() << '-' << capture_tick;
			const auto capture_id = capture_id_stream.str();
			std::ostringstream in_progress;
			in_progress << "state=in_progress\r\n"
				<< "capture_id=" << capture_id << "\r\n"
				<< "capture_tick=" << std::dec << capture_tick << "\r\n"
				<< "expected_region_count=" << regions.size() << "\r\n";
			if (!utils::io::write_file_atomic(manifest_path, in_progress.str()))
			{
				return false;
			}

			std::ostringstream manifest_body;
			manifest_body << "capture_id=" << capture_id << "\r\n"
				<< "capture_tick=" << std::dec << capture_tick << "\r\n"
				<< "scope=post_existing_h2mod_hooks_pre_vr_observation_calls\r\n"
				<< "image_base=0x" << std::hex << image_base << "\r\n";

			const auto append_known_relative_patch = [&](const std::uintptr_t address,
				const char* const source, const char* const kind)
			{
				const auto valid = validate_snapshot_range(address, address + 5);
				const auto opcode = valid
					? *reinterpret_cast<const std::uint8_t*>(address) : 0;
				const auto relative = valid && (opcode == 0xE8 || opcode == 0xE9);
				std::uintptr_t target{};
				if (relative)
				{
					std::int32_t displacement{};
					std::memcpy(&displacement, reinterpret_cast<const void*>(address + 1),
						sizeof(displacement));
					target = static_cast<std::uintptr_t>(
						static_cast<std::intptr_t>(address + 5) + displacement);
				}
				manifest_body << "known_prior_modification=0x" << std::hex << address
					<< " source=" << source << " kind=" << kind
					<< " observed=" << (valid ? "yes" : "unavailable")
					<< " opcode=0x" << static_cast<unsigned int>(opcode)
					<< " rel32_decoded=" << (relative ? "yes" : "no")
					<< " target=0x" << target << "\r\n";
			};
			append_known_relative_patch(0x1405A3740, "patches.cpp", "com_frame_minhook");
			append_known_relative_patch(0x1405A38B9, "patches.cpp",
				"worker_timeout_call_hook");
			append_known_relative_patch(0x14076D7B0, "scheduler.cpp",
				"r_end_frame_minhook");
			append_known_relative_patch(0x140781090, "camera.cpp", "camera_minhook");

			constexpr auto camera_call_site = std::uintptr_t{0x1403ACB2D};
			constexpr auto menu_fps_patch = std::uintptr_t{0x1403D8E1B};
			constexpr auto generator_entry = std::uintptr_t{0x140778E60};
			const auto camera_call_valid = validate_snapshot_range(camera_call_site,
				camera_call_site + 5);
			std::uintptr_t camera_call_target{};
			std::uint8_t camera_call_opcode{};
			if (camera_call_valid)
			{
				camera_call_opcode = *reinterpret_cast<const std::uint8_t*>(camera_call_site);
			}
			const auto camera_rel32_call = camera_call_valid && camera_call_opcode == 0xE8;
			if (camera_rel32_call)
			{
				std::int32_t displacement{};
				std::memcpy(&displacement,
					reinterpret_cast<const void*>(camera_call_site + 1), sizeof(displacement));
				camera_call_target = static_cast<std::uintptr_t>(
					static_cast<std::intptr_t>(camera_call_site + 5) + displacement);
			}
			manifest_body << "known_prior_modification=0x" << std::hex << camera_call_site
				<< " source=camera.cpp kind=rel32_call_hook observed="
				<< (camera_call_valid ? "yes" : "unavailable")
				<< " opcode=0x" << static_cast<unsigned int>(camera_call_opcode)
				<< " rel32_decoded=" << (camera_rel32_call ? "yes" : "no")
				<< " target=0x" << camera_call_target << "\r\n";

			const auto menu_patch_valid = validate_snapshot_range(menu_fps_patch,
				menu_fps_patch + 1);
			const auto menu_patch_opcode = menu_patch_valid
				? *reinterpret_cast<const std::uint8_t*>(menu_fps_patch) : 0;
			manifest_body << "known_prior_modification=0x" << std::hex << menu_fps_patch
				<< " source=patches.cpp kind=menu_fps_branch observed="
				<< (menu_patch_valid ? "yes" : "unavailable")
				<< " opcode=0x" << static_cast<unsigned int>(menu_patch_opcode) << "\r\n";

			const auto generator_entry_valid = validate_snapshot_range(generator_entry,
				generator_entry + 1);
			const auto generator_entry_opcode = generator_entry_valid
				? *reinterpret_cast<const std::uint8_t*>(generator_entry) : 0;
			manifest_body << "generate_sorted_draw_surfs=capture_skipped "
				<< "reason=known_xmodel_component_ownership observed="
				<< (generator_entry_valid ? "yes" : "unavailable")
				<< " entry_opcode=0x" << std::hex
				<< static_cast<unsigned int>(generator_entry_opcode)
				<< " e9_detour=" << (generator_entry_valid && generator_entry_opcode == 0xE9
					? "yes" : "no") << "\r\n";
			bool complete = true;
			for (const auto& region : regions)
			{
				const auto size = region.end > region.begin ? region.end - region.begin : 0;
				if (size == 0 || size > 1024 * 1024 ||
					!validate_snapshot_range(region.begin, region.end))
				{
					manifest_body << region.name << " invalid begin=0x" << region.begin
						<< " end=0x" << region.end << " size=" << std::dec << size
						<< " validation=readable_executable_range_failed\r\n" << std::hex;
					complete = false;
					continue;
				}
				std::ostringstream path;
				path << "minidumps/runtime-code/" << region.name << "-h2+"
					<< std::hex << region.begin - image_base << "-h2+"
					<< region.end - image_base << ".bin";
				const std::string bytes(reinterpret_cast<const char*>(region.begin), size);
				const auto sha256 = utils::cryptography::sha256::compute(bytes, true);
				const auto written = utils::io::write_file_atomic(path.str(), bytes);
				manifest_body << region.name << " begin=0x" << std::hex << region.begin
					<< " end=0x" << region.end << " size=" << std::dec << size
					<< " file=" << path.str() << " written=" << (written ? "yes" : "no")
					<< " sha256=" << sha256
					<< "\r\n";
				complete = complete && written;
			}
			std::ostringstream final_manifest;
			final_manifest << "state=" << (complete ? "complete" : "incomplete") << "\r\n"
				<< manifest_body.str();
			return utils::io::write_file_atomic(manifest_path, final_manifest.str()) && complete;
		}
		catch (...)
		{
			return false;
		}
	}

}
