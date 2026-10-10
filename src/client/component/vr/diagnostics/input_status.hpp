#pragma once

#include "../input_history.hpp"
#include <ostream>

namespace vr::diagnostics
{
	inline const char* input_error_name(controller_input::input_backend backend,
	                                    controller_input::input_condition condition) noexcept
	{
		using namespace controller_input;
		// Codes from compositor/tracking APIs use a different enumeration.
		if (backend != input_backend::openvr ||
		    (condition.reason != input_reason::action_query_failed &&
		     condition.reason != input_reason::action_update_failed &&
		     condition.reason != input_reason::initialization_failed)) return "not_applicable";
		constexpr const char* names[]{"None", "NameNotFound", "WrongType", "InvalidHandle", "InvalidParam",
		    "NoSteam", "MaxCapacityReached", "IPCError", "NoActiveActionSet", "InvalidDevice", "InvalidSkeleton",
		    "InvalidBoneCount", "InvalidCompressedData", "NoData", "BufferTooSmall", "MismatchedActionManifest",
		    "MissingSkeletonData", "InvalidBoneIndex", "InvalidPriority", "PermissionDenied", "InvalidRenderModel"};
		return condition.code >= 0 && condition.code < static_cast<std::int64_t>(std::size(names))
		    ? names[condition.code] : "unknown";
	}

	inline void append_input_failure(std::ostream& out, const char* label, const controller_input::input_failure& f,
	                                 controller_input::clock::time_point now, const char* prefix)
	{
		using namespace controller_input;
		if (!f.seen) return;
		out << prefix << label << ": backend=" << to_string(f.backend)
		    << " channel=" << to_string(f.channel) << " reason=" << to_string(f.condition.reason)
		    << " code=" << f.condition.code << " name=" << input_error_name(f.backend, f.condition)
		    << " handle=" << f.condition.handle << " age_ms="
		    << (now >= f.at ? std::chrono::duration_cast<std::chrono::milliseconds>(now - f.at).count() : -1)
		    << " sequence=" << f.sequence << " reference=" << f.reference << " initialization=" << f.initialization
		    << " runtime_focus=" << f.runtime_focus << " runtime_focus_known=" << f.runtime_focus_known
		    << " gameplay_active=" << f.gameplay_active << '\n';
	}

	inline void append_input_overview(std::ostream& out,
	                                 const controller_input::input_history_snapshot& s,
	                                 controller_input::clock::time_point now,
	                                 const char* prefix = "  ")
	{
		using namespace controller_input;
		const auto age = [now](clock::time_point at, bool seen) -> std::int64_t {
			return seen && now >= at ? std::chrono::duration_cast<std::chrono::milliseconds>(now - at).count()
			                         : -1;
		};
		const auto sampled_age = age(s.last_sample_at, s.samples != 0);
		const bool fresh = s.samples && sampled_age >= 0 && sampled_age <= 150;
		std::size_t available{}, ever_available{};
		for (std::size_t i = 1; i < s.channels.size(); ++i)
		{
			available += s.channels[i].valid;
			ever_available += s.channels[i].valid_samples != 0;
		}
		out << prefix << "input_history: scope=process backend=" << to_string(s.backend)
		    << " samples=" << s.samples << " invalidations=" << s.invalidations
		    << " observed_ms=" << age(s.started_at, s.observed)
		    << " publication_age_ms=" << age(s.sampled_at, s.observed) << " sample_age_ms=" << sampled_age
		    << " sample_fresh=" << fresh << " last_sample_sequence=" << s.last_sample_sequence
		    << " reference=" << s.last_sample_reference << " initialization=" << s.initialization << " runtime_focus=" << s.runtime_focus
		    << " runtime_focus_known=" << s.runtime_focus_known << " gameplay_active=" << s.gameplay_active
		    << " gate=" << to_string(s.gate.reason) << " code=" << s.gate.code << '\n';
		if (!s.samples)
			out << prefix << "input_summary: No controller action samples have been published.\n";
		else if (!fresh)
			out << prefix
			    << "input_summary: Controller action samples are stale; the states below describe the last publication.\n";
		else if (!ever_available)
			out << prefix << "input_summary: Controller actions have never been available in this process; focus alone does not establish working controllers.\n";
		else if (!available)
			out << prefix
			    << "input_summary: Some controller actions were available earlier; none are available in the latest sample.\n";
		else
			out << prefix
			    << "input_summary: " << available << '/' << (input_channel_count - 1)
			    << " controller action channels are available in the latest sample.\n";
		out << prefix << "hand_pose_summary: left="
		    << (s.channels[index(input_channel::left_grip)].valid && s.channels[index(input_channel::left_aim)].valid)
		    << " right=" << (s.channels[index(input_channel::right_grip)].valid && s.channels[index(input_channel::right_aim)].valid)
		    << " sample_fresh=" << fresh << " (each hand requires both grip and aim; this is not a rendering verdict)\n";
		append_input_failure(out,"first_input_api_error",s.first_api_failure,now,prefix);
		append_input_failure(out,"last_input_api_error",s.last_api_failure,now,prefix);
	}

	inline void append_input_history(std::ostream& out,
	                                 const controller_input::input_history_snapshot& s,
	                                 controller_input::clock::time_point now,
	                                 const char* prefix = "  ")
	{
		using namespace controller_input;
		append_input_overview(out,s,now,prefix);
		const auto age = [now](clock::time_point at, bool seen) -> std::int64_t {
			return seen && now >= at ? std::chrono::duration_cast<std::chrono::milliseconds>(now - at).count() : -1;
		};
		for (std::size_t i = 0; i < s.channels.size(); ++i)
		{
			const auto& h = s.channels[i];
			out << prefix << "input_channel=" << to_string(static_cast<input_channel>(i))
			    << " valid=" << h.valid << " ever_valid=" << (h.valid_samples != 0)
			    << " valid_samples=" << h.valid_samples << " activity_samples=" << h.activity_samples
			    << " last_valid_age_ms=" << age(h.last_valid_at, h.valid_samples != 0)
			    << " current=" << to_string(h.current.reason) << " code=" << h.current.code
			    << " name=" << input_error_name(s.backend,h.current) << " handle=" << h.current.handle
			    << " current_ms=" << age(h.current_since, s.observed) << " losses=" << h.losses
			    << " recoveries=" << h.recoveries << " api_failure_samples=" << h.api_failure_samples
			    << " last_loss=" << to_string(h.last_loss.reason) << " last_loss_code=" << h.last_loss.code
			    << " last_loss_backend=" << to_string(h.last_loss_backend)
			    << " last_loss_age_ms=" << age(h.last_loss_at, h.losses != 0)
			    << " last_loss_sequence=" << h.last_loss_sequence
			    << " last_loss_reference=" << h.last_loss_reference
			    << " last_loss_runtime_focus=" << h.last_loss_runtime_focus
			    << " last_loss_runtime_focus_known=" << h.last_loss_runtime_focus_known
			    << " last_loss_gameplay_active=" << h.last_loss_gameplay_active
			    << " last_rejection=" << to_string(h.last_rejection.reason)
			    << " last_rejection_code=" << h.last_rejection.code
			    << " last_rejection_backend=" << to_string(h.last_rejection_backend)
			    << " last_rejection_age_ms=" << age(h.last_rejection_at, h.rejected) << '\n';
			append_input_failure(out,"first_action_rejection",h.first_action_rejection,now,prefix);
			append_input_failure(out,"last_action_rejection",h.last_action_rejection,now,prefix);
		}
		// Keep recent causes visible after recovery and a new report-time focus loss.
		out << prefix << "input_transitions: retained=" << s.transition_count
		    << " discarded=" << s.transitions_discarded << " order=oldest_to_newest\n";
		for (std::size_t n = 0; n < s.transition_count; ++n)
		{
			const auto slot = (s.transition_next + input_transition_capacity - s.transition_count + n) %
			                  input_transition_capacity;
			const auto& e = s.transitions[slot];
			out << prefix << "input_transition: age_ms=" << age(e.at, true)
			    << " backend=" << to_string(e.backend) << " sequence=" << e.sequence
			    << " reference=" << e.reference << " initialization=" << e.initialization << " runtime_focus=" << e.runtime_focus
			    << " runtime_focus_known=" << e.runtime_focus_known
			    << " gameplay_active=" << e.gameplay_active << " gate=" << to_string(e.gate.reason)
			    << " code=" << e.gate.code << " lost=";
			bool any_loss{};
			for (std::size_t i = 0; i < input_channel_count; ++i)
				if (e.loss_mask & (1u << i))
				{
					if (any_loss)
						out << ',';
					out << to_string(static_cast<input_channel>(i));
					any_loss = true;
				}
			if (!any_loss)
				out << "none";
			out << " unavailable=";
			bool any_invalid{};
			for (std::size_t i = 0; i < input_channel_count; ++i)
				if (!(e.valid_mask & (1u << i)))
				{
					if (any_invalid)
						out << ',';
					out << to_string(static_cast<input_channel>(i)) << ':' << to_string(e.channels[i].reason)
					    << ':' << e.channels[i].code;
					any_invalid = true;
				}
			if (!any_invalid)
				out << "none";
			out << '\n';
		}
		if (s.gate.reason == input_reason::not_initialized ||
		    s.gate.reason == input_reason::initialization_failed)
			out << prefix
			    << "input_hint: Input setup failed. Check that the complete VR package is installed and share this report.\n";
		else if (s.last_api_failure.seen && s.last_api_failure.backend == input_backend::openvr &&
		         s.last_api_failure.condition.reason == input_reason::action_query_failed &&
		         s.last_api_failure.condition.code == 3)
			out << prefix
			    << "input_hint: OpenVR rejected an action handle (InvalidHandle). Retained setup, bindings and API history are needed; focus loss alone does not explain this error.\n";
		else if (s.gate.reason == input_reason::action_update_failed || s.last_api_failure.seen)
			out << prefix
			    << "input_hint: A runtime API error was observed. Its last reason and code remain above even after recovery.\n";
		else if (s.channels[index(input_channel::focus)].valid &&
		         !s.channels[index(input_channel::left_grip)].valid &&
		         !s.channels[index(input_channel::right_grip)].valid)
			out << prefix
			    << "input_hint: Neither hand has a valid grip pose. Check controller power, tracking and bindings.\n";
		out << prefix
		    << "input_help: active/valid means available, not pressed; activity_samples counts held buttons or nonzero sticks.\n"
		    << prefix
		    << "input_help: losses are availability transitions, not proof of a physical device disconnect; age_ms=-1 means unobserved.\n"
		    << prefix
		    << "input_help: Run vr_diagnose or press Ctrl+Shift+F8 for one diagnostic ZIP; its folder opens automatically.\n";
	}
}
