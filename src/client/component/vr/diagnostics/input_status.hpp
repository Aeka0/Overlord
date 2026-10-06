#pragma once

#include "../input_history.hpp"
#include <ostream>

namespace vr::diagnostics
{
	inline void append_input_history(std::ostream& out,
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
		out << prefix << "input_history: scope=process backend=" << to_string(s.backend)
		    << " samples=" << s.samples << " invalidations=" << s.invalidations
		    << " observed_ms=" << age(s.started_at, s.observed)
		    << " publication_age_ms=" << age(s.sampled_at, s.observed) << " sample_age_ms=" << sampled_age
		    << " sample_fresh=" << fresh << " last_sample_sequence=" << s.last_sample_sequence
		    << " reference=" << s.last_sample_reference << " runtime_focus=" << s.runtime_focus
		    << " runtime_focus_known=" << s.runtime_focus_known << " gameplay_active=" << s.gameplay_active
		    << " gate=" << to_string(s.gate.reason) << " code=" << s.gate.code << '\n';
		if (!s.samples)
			out << prefix << "input_summary: No controller action samples have been published.\n";
		else if (!fresh)
			out << prefix
			    << "input_summary: Controller action samples are stale; the states below describe the last publication.\n";
		else if (!s.channels[index(input_channel::focus)].valid_samples)
			out << prefix << "input_summary: Controller actions have never been available in this process.\n";
		else if (!s.channels[index(input_channel::focus)].valid)
			out << prefix
			    << "input_summary: Controller actions were available earlier and are unavailable now.\n";
		else
			out << prefix
			    << "input_summary: The latest action synchronization succeeded; inspect each hand and action below.\n";
		out << prefix << "last_input_api_error: backend=" << to_string(s.last_api_error_backend)
		    << " channel=" << to_string(s.last_api_error_channel)
		    << " reason=" << to_string(s.last_api_error.reason) << " code=" << s.last_api_error.code
		    << " age_ms=" << age(s.last_api_error_at, s.api_error_seen) << '\n';
		for (std::size_t i = 0; i < s.channels.size(); ++i)
		{
			const auto& h = s.channels[i];
			out << prefix << "input_channel=" << to_string(static_cast<input_channel>(i))
			    << " valid=" << h.valid << " ever_valid=" << (h.valid_samples != 0)
			    << " valid_samples=" << h.valid_samples << " activity_samples=" << h.activity_samples
			    << " last_valid_age_ms=" << age(h.last_valid_at, h.valid_samples != 0)
			    << " current=" << to_string(h.current.reason) << " code=" << h.current.code
			    << " current_ms=" << age(h.current_since, s.observed) << " losses=" << h.losses
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
			    << " reference=" << e.reference << " runtime_focus=" << e.runtime_focus
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
		else if (s.gate.reason == input_reason::action_update_failed || s.api_error_seen)
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
		    << "input_help: Close the VR dashboard, return to the game and release then operate both controllers.\n"
		    << prefix
		    << "input_help: Run vr_status once after the problem and share minidumps/h2-mod-vr-status-latest.txt.\n";
	}
}
