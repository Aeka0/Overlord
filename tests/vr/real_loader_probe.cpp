#include <std_include.hpp>

#include "component/vr/openxr_layer_policy.hpp"
#include "component/vr/openxr_runtime.hpp"
#include "test_support.hpp"

int main()
{
	try
	{
		const auto layer_policy = vr::openxr::apply_native_implicit_layer_policy();
		std::cout << "probe_revision=implicit-layer-policy-v2"
			<< " policy_applied=" << (layer_policy.applied ? "yes" : "no")
			<< " manifests=" << layer_policy.manifest_count
			<< " layers_disabled=" << layer_policy.disabled_layers.size() << std::endl;
		for (const auto& path : layer_policy.manifest_paths)
		{
			std::cout << "policy_manifest=" << path << std::endl;
		}
		for (std::size_t index = 0; index < layer_policy.disabled_layers.size(); ++index)
		{
			std::cout << "policy_disabled=" << layer_policy.disabled_layers[index]
				<< " environment=" << layer_policy.disabled_environment_variables[index] << std::endl;
		}
		for (const auto& warning : layer_policy.warnings)
		{
			std::cout << "policy_warning=" << warning << std::endl;
		}
		if (!layer_policy.blocking_error.empty())
		{
			std::cerr << "policy_error=" << layer_policy.blocking_error << std::endl;
			return 1;
		}
		const auto runtime_preference = vr::openxr::apply_virtual_desktop_runtime_preference();
		std::cout << "runtime_policy_applied=" << (runtime_preference.applied ? "yes" : "no")
			<< " override_active=" << (runtime_preference.override_active ? "yes" : "no")
			<< " automatic=" << (runtime_preference.override_set_by_policy ? "yes" : "no")
			<< " source=" << (runtime_preference.source.empty() ? "none" : runtime_preference.source)
			<< " manifest=" << (runtime_preference.manifest_path.empty() ? "none" : runtime_preference.manifest_path)
			<< std::endl;
		if (!runtime_preference.warning.empty())
		{
			std::cout << "runtime_policy_warning=" << runtime_preference.warning << std::endl;
		}
		if (!runtime_preference.blocking_error.empty())
		{
			std::cerr << "runtime_policy_error=" << runtime_preference.blocking_error << std::endl;
			return 1;
		}

		const auto graphics = vr::tests::create_graphics(1);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		const auto initialized = runtime.initialize(graphics);

		const auto status = runtime.get_status();
		std::cout << "initialized=" << (initialized ? "yes" : "no")
			<< " state=" << vr::to_string(status.state)
			<< " stage=" << status.last_initialization_stage
			<< " xr=" << status.last_xr_result_name
			<< " runtime=" << (status.last_runtime_name.empty() ? "unavailable" : status.last_runtime_name)
			<< " system=" << (status.last_system_name.empty() ? "unavailable" : status.last_system_name)
			<< " layers_disabled=" << status.implicit_layers_disabled
			<< " disabled_layers=" << (status.disabled_implicit_layers.empty() ? "none" : status.disabled_implicit_layers)
			<< " layer_warning=" << (status.implicit_layer_policy_warning.empty() ? "none" : status.implicit_layer_policy_warning)
			<< " error=" << (status.last_error.empty() ? "none" : status.last_error) << '\n';

		const bool real_loader_reached_runtime = status.last_initialization_stage != "loader" &&
			status.last_initialization_stage != "global_dispatch";
		const bool safe_state = real_loader_reached_runtime && (status.state == vr::runtime_state::runtime_unavailable ||
			status.state == vr::runtime_state::no_hmd ||
			status.state == vr::runtime_state::graphics_mismatch ||
			status.state == vr::runtime_state::session_idle ||
			status.state == vr::runtime_state::running);
		runtime.shutdown();
		vr::tests::require(safe_state, "official loader probe reached an unexpected state");
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "vr-runtime-real-loader-probe: FAIL: " << error.what() << '\n';
		return 1;
	}
}
