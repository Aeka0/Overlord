#pragma once

#include <iosfwd>

namespace d3d11 { struct graphics_status; }
namespace vr { struct runtime_status; }
namespace vr::head_pose_bridge { struct status; }
namespace vr::engine_stereo_bridge { struct status; }
namespace vr::engine_view_probe { struct status; }
namespace vr::engine_backend_probe { struct status; }
namespace vr::engine_stereo_binding { struct status; }
namespace vr::engine_stereo_backend_view { struct status; }
namespace vr::engine_stereo_backend_target { struct status; }
namespace vr::engine_stereo_backend_target { struct frame_status; }
namespace vr::engine_stereo_output_merger { struct status; }
namespace vr::engine_stereo_draw_indexed { struct status; }
namespace vr::engine_stereo_execution { struct status; }
namespace vr::engine_stereo_owner_pass { struct report; }
namespace vr::engine_stereo_renderer { struct culling_union_status; }
namespace vr::engine_stereo_scene_batch_probe { struct report; }
namespace vr::engine_stereo_ssr_history_probe { struct report; }
namespace vr::engine_stereo_ssr_consumer_probe { struct report; }
namespace vr::engine_stereo_eye_resources { struct status; }
namespace vr::engine_stereo_effect_timeline { struct report; }
namespace vr::engine_stereo_material_buffer_probe { struct report; }
namespace vr::engine_stereo_particle_buffer_probe { struct report; }
namespace vr::engine_stereo_gpu_census { struct report; }
namespace vr::engine_stereo_constant_buffer_probe { struct report; }

namespace vr::diagnostics::detail
{

	// Append to the same stream in report order. Inputs are the caller's existing
	// snapshots: these sections do not own runtime state or resample those inputs.
	void append_runtime_status(std::ostringstream& output,
		const vr::runtime_status& runtime_status,
		const head_pose_bridge::status& head_status);

	void append_frontend_status(std::ostringstream& output,
		const engine_stereo_bridge::status& bridge_status,
		const engine_view_probe::status& view_probe_status,
		const engine_backend_probe::status& backend_probe_status,
		const engine_stereo_binding::status& stereo_binding_status,
		const engine_stereo_backend_view::status& backend_view_status,
		const engine_stereo_backend_target::status& backend_target_status,
		const engine_stereo_backend_target::frame_status& backend_target_frame_status,
		const engine_stereo_renderer::culling_union_status& culling_status);

	void append_execution_status(std::ostringstream& output,
		const engine_stereo_output_merger::status& output_merger_status,
		const engine_stereo_draw_indexed::status& draw_indexed_status,
		const engine_stereo_execution::status& execution_status,
		const engine_stereo_owner_pass::report& owner_pass_status);

	void append_scene_status(std::ostringstream& output,
		const engine_stereo_owner_pass::report& owner_pass_status,
		const engine_stereo_scene_batch_probe::report& scene_batch_status);

	void append_history_status(std::ostringstream& output,
		const engine_stereo_ssr_history_probe::report& ssr_history_status,
		const engine_stereo_ssr_consumer_probe::report& ssr_consumer_status,
		const engine_stereo_eye_resources::status& eye_resource_status);

	void append_effect_status(std::ostringstream& output,
		const engine_stereo_owner_pass::report& owner_pass_status,
		const engine_stereo_effect_timeline::report& effect_timeline_status);

	void append_owner_status(std::ostringstream& output,
		const engine_stereo_owner_pass::report& owner_pass_status,
		const engine_stereo_material_buffer_probe::report& material_buffer_status,
		const engine_stereo_particle_buffer_probe::report& particle_buffer_status);

	void append_census_status(std::ostringstream& output,
		const engine_stereo_gpu_census::report& gpu_census_status);

	void append_resource_status(std::ostringstream& output,
		const engine_stereo_gpu_census::report& gpu_census_status,
		const engine_stereo_constant_buffer_probe::report& constant_buffer_status);
}
