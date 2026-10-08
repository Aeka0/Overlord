#pragma once

#include "../steamvr_input_diagnostics.hpp"
#include <ostream>

namespace vr::diagnostics
{
	inline const char* input_event_name(std::uint32_t type) noexcept
	{
		switch (type)
		{
		case 100: return "TrackedDeviceActivated";
		case 101: return "TrackedDeviceDeactivated";
		case 102: return "TrackedDeviceUpdated";
		case 108: return "TrackedDeviceRoleChanged";
		case 1701: return "BindingLoadFailed";
		case 1702: return "BindingLoadSuccessful";
		case 1703: return "ActionManifestReloaded";
		case 1704: return "ActionManifestLoadFailed";
		case 1707: return "BindingsUpdated";
		case 1708: return "BindingSubscriptionChanged";
		default: return "unknown";
		}
	}

	inline void append_steamvr_input(std::ostream& out, const steamvr_input::diagnostic_snapshot& s,
	                                 std::chrono::steady_clock::time_point now)
	{
		if (!s.initializations && !s.event_count) return;
		out << "openvr_input_evidence: initializations=" << s.initializations << " resets=" << s.resets
		    << " probes=" << s.probes << " probes_skipped=" << s.probes_skipped
		    << " collection_failures=" << s.collection_failures
		    << " retained_events=" << s.event_count << " discarded_events=" << s.events_discarded
		    << " event_application_attribution=unverified\n";
		const auto text = [&](const char* label, const std::shared_ptr<const std::string>& value) {
			if (value) out << "  " << label << ":\n" << *value;
		};
		text("first_setup", s.first_setup);
		if (s.first_failed_setup != s.first_setup) text("first_failed_setup", s.first_failed_setup);
		if (s.latest_setup != s.first_setup && s.latest_setup != s.first_failed_setup) text("latest_setup", s.latest_setup);
		text("first_probe", s.first_probe);
		if (s.first_failed_probe != s.first_probe) text("first_failed_probe", s.first_failed_probe);
		if (s.latest_probe != s.first_probe && s.latest_probe != s.first_failed_probe) text("latest_probe", s.latest_probe);
		const auto event = [&](const char* label, const steamvr_input::input_event& e) {
			out << "  " << label << ": type=" << e.type << " name=" << input_event_name(e.type)
			    << " initialization=" << e.initialization << " device=" << e.device << " age_ms="
			    << (now >= e.at ? std::chrono::duration_cast<std::chrono::milliseconds>(now - e.at).count() : -1)
			    << " event_data=" << e.details[0] << ',' << e.details[1] << ',' << e.details[2] << ',' << e.details[3] << '\n';
		};
		if (s.first_load_failure.type) event("first_input_load_failure", s.first_load_failure);
		for (std::size_t n = 0; n < s.event_count; ++n)
			event("input_runtime_event", s.events[(s.event_next + s.events.size() - s.event_count + n) % s.events.size()]);
	}
}
