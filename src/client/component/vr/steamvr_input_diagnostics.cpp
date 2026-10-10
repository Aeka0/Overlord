#include <std_include.hpp>
#include "steamvr_input.hpp"
#include "diagnostics/input_status.hpp"
#include "diagnostics/input_manifest.hpp"
#include <sstream>

namespace vr::steamvr_input
{
	void actions::capture_setup() noexcept
	{
		try
		{
			using diagnostics::input_quoted;
			std::ostringstream out;
			out << "  openvr_input_setup: initialization=" << diagnostics_.initializations
			    << " process_tick_ms=" << GetTickCount64() << " interface=" << IVRInput_Version
			    << " manifest_result=" << manifest_code_ << " set_result=" << set_code_ << " set_handle=" << set_
			    << " error=" << input_quoted(error_) << '\n';
			const auto path = diagnostics::input_path(manifest_path_);
			const auto manifest = diagnostics::read_input_manifest(out, path);
			const auto sets = manifest.find("action_sets");
			unsigned matching_sets{};
			if (sets != manifest.end() && sets->is_array() && sets->size() <= 512)
				for (const auto& item : *sets)
					if (item.is_object() && item.contains("name") && item["name"] == "/actions/gameplay") ++matching_sets;
			out << "    action_set_declarations=" << matching_sets << " expected=1\n";
			const auto declared = manifest.find("actions");
			const bool actions_array = declared != manifest.end() && declared->is_array() && declared->size() <= 512;
			for (std::size_t i = 0; i < setup_action_count_; ++i)
			{
				const auto& action = setup_actions_[i];
				unsigned matches{}, correct_types{};
				if (actions_array) for (const auto& item : *declared)
				{
					if (!item.is_object()) continue;
					const auto name = item.find("name"), type = item.find("type");
					if (name != item.end() && *name == action.name)
					{
						++matches;
						correct_types += type != item.end() && *type == action.type;
					}
				}
				out << "    action_setup: name=" << action.name << " expected_type=" << action.type
				    << " get_result=" << action.code << " result_name="
				    << diagnostics::input_error_name(controller_input::input_backend::openvr,
				        {controller_input::input_reason::initialization_failed, action.code})
				    << " handle=" << action.handle << " declaration_checked=" << actions_array
				    << " declarations=" << matches << " matching_types=" << correct_types << '\n';
			}
			const auto bindings = manifest.find("default_bindings");
			if (bindings != manifest.end() && bindings->is_array())
			{
				out << "    default_bindings: count=" << bindings->size() << " inspected_limit=16\n";
				for (std::size_t i = 0; i < std::min<std::size_t>(bindings->size(), 16); ++i)
				{
					const auto& binding = (*bindings)[i];
					if (!binding.is_object()) { out << "    binding: malformed_entry\n"; continue; }
					const auto url = binding.find("binding_url"), type = binding.find("controller_type");
					out << "    binding: controller=" << input_quoted(type != binding.end() && type->is_string()
					    ? type->get<std::string>() : "missing_or_wrong_type");
					if (url == binding.end() || !url->is_string()) { out << " state=missing_binding_url\n"; continue; }
					const auto relative = diagnostics::input_path(url->get<std::string>());
					if (!diagnostics::local_input_binding(relative)) { out << " state=nonlocal_path_not_inspected\n"; continue; }
					std::error_code error;
					const auto base = std::filesystem::weakly_canonical(path.parent_path(), error);
					if (error) { out << " state=unresolved_directory\n"; continue; }
					const auto target = std::filesystem::weakly_canonical(base / relative, error);
					if (error || !diagnostics::local_input_binding(target.lexically_relative(base)))
					{ out << " state=unresolved_or_external_path\n"; continue; }
					out << '\n';
					(void)diagnostics::read_input_manifest(out, target);
				}
			}
			else out << "    default_bindings: missing_or_wrong_type\n";
			diagnostics_.latest_setup = std::make_shared<const std::string>(out.str());
			if (!diagnostics_.first_setup) diagnostics_.first_setup = diagnostics_.latest_setup;
			if (!error_.empty() && !diagnostics_.first_failed_setup) diagnostics_.first_failed_setup = diagnostics_.latest_setup;
		}
		catch (...) { ++diagnostics_.collection_failures; }
	}

	void actions::capture_probe(bool input_failed) noexcept
	{
		probe_pending_ = false;
		// Device/event churn cannot consume the last slot before an API failure.
		if (probe_count_ >= (input_failed ? 4u : 3u)) { ++diagnostics_.probes_skipped; return; }
		++probe_count_;
		++diagnostics_.probes;
		try
		{
			using diagnostics::input_quoted;
			std::ostringstream out;
			out << "  openvr_input_probe: initialization=" << diagnostics_.initializations
			    << " sequence=" << sequence_ << " process_tick_ms=" << GetTickCount64()
			    << " probe=" << probe_count_ << " input_failed=" << input_failed << " per_initialization_limit=4\n";
			if (auto* applications = VRApplications())
			{
				std::array<char, 1024> key{}, manifest{};
				const auto key_result = applications->GetApplicationKeyByProcessId(GetCurrentProcessId(), key.data(), unsigned(key.size()));
				key.back() = 0;
				out << "    application_identity: result=" << key_result << " key=" << input_quoted(key.data());
				if (key_result == VRApplicationError_None)
				{
					EVRApplicationError result{};
					const auto needed = applications->GetApplicationPropertyString(key.data(),
					    VRApplicationProperty_ActionManifestURL_String, manifest.data(), unsigned(manifest.size()), &result);
					manifest.back() = 0;
					out << " manifest_result=" << result << " required_bytes=" << needed
					    << " registered_manifest=" << input_quoted(manifest.data());
				}
				out << '\n';
			}
			else out << "    application_identity: interface_unavailable\n";
			if (system_) for (unsigned role = 0; role < 3; ++role)
			{
				const auto device = role == 0 ? k_unTrackedDeviceIndex_Hmd : system_->GetTrackedDeviceIndexForControllerRole(
				    role == 1 ? TrackedControllerRole_LeftHand : TrackedControllerRole_RightHand);
				out << "    input_device: role=" << (role == 0 ? "hmd" : role == 1 ? "left" : "right") << " index=" << device;
				if (device == k_unTrackedDeviceIndexInvalid) { out << " assigned=0\n"; continue; }
				out << " connected=" << system_->IsTrackedDeviceConnected(device) << '\n';
				for (const auto property : {Prop_ModelNumber_String, Prop_TrackingSystemName_String, Prop_ControllerType_String,
				    Prop_InputProfilePath_String, Prop_DriverVersion_String})
				{
					std::array<char, 512> value{};
					ETrackedPropertyError result{};
					const auto needed = system_->GetStringTrackedDeviceProperty(device, property, value.data(), unsigned(value.size()), &result);
					value.back() = 0;
					out << "      device_property=" << property << " result=" << result << " required_bytes=" << needed
					    << " value=" << input_quoted(value.data()) << '\n';
				}
			}
			for (std::size_t i = 0; input_ && i < setup_action_count_; ++i)
			{
				const auto& action = setup_actions_[i];
				if (action.code != VRInputError_None || !action.handle) continue;
				std::array<InputBindingInfo_t, 2> bindings{};
				std::uint32_t count{};
				const auto result = input_->GetActionBindingInfo(action.handle, bindings.data(), sizeof(InputBindingInfo_t), unsigned(bindings.size()), &count);
				out << "    runtime_binding: action=" << action.name << " handle=" << action.handle
				    << " result=" << result << " result_name=" << diagnostics::input_error_name(controller_input::input_backend::openvr,
				        {controller_input::input_reason::action_query_failed, result})
				    << " count=" << count << " capacity=" << bindings.size() << '\n';
				if (result != VRInputError_None) continue;
				for (unsigned n = 0; n < std::min<unsigned>(count, unsigned(bindings.size())); ++n)
				{
					auto& binding = bindings[n];
					binding.rchDevicePathName[127] = binding.rchInputPathName[127] = binding.rchModeName[127] = binding.rchSlotName[127] = 0;
					out << "      binding_source: device=" << input_quoted(binding.rchDevicePathName)
					    << " input=" << input_quoted(binding.rchInputPathName) << " mode=" << input_quoted(binding.rchModeName)
					    << " slot=" << input_quoted(binding.rchSlotName) << '\n';
				}
			}
			diagnostics_.latest_probe = std::make_shared<const std::string>(out.str());
			if (!diagnostics_.first_probe) diagnostics_.first_probe = diagnostics_.latest_probe;
			// Introspection can fail independently (for example a small binding
			// buffer). Reserve this evidence for an actual producer input failure.
			if (input_failed && !diagnostics_.first_failed_probe) diagnostics_.first_failed_probe = diagnostics_.latest_probe;
		}
		catch (...) { ++diagnostics_.collection_failures; }
	}

	void actions::observe_event(const VREvent_t& event) noexcept
	{
		switch (event.eventType)
		{
		case VREvent_TrackedDeviceActivated:
		case VREvent_TrackedDeviceDeactivated:
		case VREvent_TrackedDeviceUpdated:
		case VREvent_TrackedDeviceRoleChanged:
		case VREvent_Input_BindingLoadFailed:
		case VREvent_Input_BindingLoadSuccessful:
		case VREvent_Input_ActionManifestReloaded:
		case VREvent_Input_ActionManifestLoadFailed:
		case VREvent_Input_BindingsUpdated:
		case VREvent_Input_BindingSubscriptionChanged:
			break;
		default: return;
		}
		pose_adapter_.reset();
		input_event record{controller_input::clock::now(), diagnostics_.initializations, event.eventType, event.trackedDeviceIndex};
		if (event.eventType == VREvent_Input_BindingLoadFailed || event.eventType == VREvent_Input_BindingLoadSuccessful)
		{
			const auto& data = event.data.inputBinding;
			record.details = {data.ulAppContainer, data.pathMessage, data.pathUrl, data.pathControllerType};
		}
		else if (event.eventType == VREvent_Input_ActionManifestLoadFailed)
		{
			const auto& data = event.data.actionManifest;
			record.details = {data.pathAppKey, data.pathMessage, data.pathMessageParam, data.pathManifestPath};
		}
		if (!diagnostics_.first_load_failure.type && (event.eventType == VREvent_Input_BindingLoadFailed ||
		    event.eventType == VREvent_Input_ActionManifestLoadFailed)) diagnostics_.first_load_failure = record;
		diagnostics_.events[diagnostics_.event_next] = record;
		diagnostics_.event_next = (diagnostics_.event_next + 1) % diagnostics_.events.size();
		if (diagnostics_.event_count < diagnostics_.events.size()) ++diagnostics_.event_count;
		else ++diagnostics_.events_discarded;
		controller_type_retry_.reset();
		probe_pending_ = true; // Coalesced and capped; event handling performs no extra API calls.
	}
}
