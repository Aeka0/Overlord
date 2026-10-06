#pragma once

#include "component/vr/diagnostics/input_status.hpp"
#include <sstream>

template<class Check>
void input_history_tests(const Check& check)
{
	using namespace vr::controller_input;
	using namespace std::chrono_literals;
	input_history history;
	frame f;
	f.sequence=f.reference_generation=1; f.sampled_at=clock::time_point{1s};
	f.source.backend=input_backend::openvr;
	f.source.gate={input_reason::input_unavailable};
	history.observe(f);
	f.sampled_at+=10ms;++f.sequence;history.observe(f);
	auto s=history.snapshot();
	const auto focus=index(input_channel::focus), left=index(input_channel::left_grip), right=index(input_channel::right_grip);
	check(s.samples==2 && !s.channels[focus].valid_samples && !s.channels[focus].losses &&
		s.channels[focus].current_since==clock::time_point{1s},"never-available input is distinct from a loss and retains episode duration");
	std::ostringstream report;
	vr::diagnostics::append_input_history(report,s,f.sampled_at);
	check(report.str().find("never been available")!=std::string::npos &&
		report.str().find("last_valid_age_ms=-1")!=std::string::npos,"one report explains never-valid input without a fabricated timestamp");
	f.source.runtime_focus=true;f.source.runtime_focus_known=true;f.source.gate={input_reason::none};
	f.focused=f.move_active=f.turn_active=true;
	for(unsigned h=0;h<2;++h)f.grip[h].valid=f.aim[h].valid=f.trigger[h].active=true;
	f.sampled_at+=10ms;++f.sequence;history.observe(f);
	s=history.snapshot();
	check(s.channels[focus].valid_samples==1 && s.channels[focus].last_rejection.reason==input_reason::input_unavailable &&
		s.channels[index(input_channel::move)].activity_samples==0 && s.channels[index(input_channel::right_trigger)].valid_samples==1,
		"recovery preserves rejection and distinguishes an available neutral action from unavailable input");
	f.grip[0].valid=false;f.source.channels[left]={input_reason::controller_disconnected};
	f.sampled_at+=10ms;++f.sequence;history.observe(f);
	f.sampled_at+=10ms;++f.sequence;history.observe(f);
	s=history.snapshot();
	check(s.channels[left].losses==1 && !s.channels[right].losses && s.channels[right].valid &&
		s.channels[left].last_loss.reason==input_reason::controller_disconnected,"one-hand loss is counted once without attributing it to the other hand");
	f.grip[0].valid=true;f.move={0,1};f.trigger[1].down=true;
	f.sampled_at+=10ms;++f.sequence;history.observe(f);
	check(history.snapshot().channels[index(input_channel::move)].activity_samples==1 &&
		history.snapshot().channels[index(input_channel::right_trigger)].activity_samples==1,"observed use is separate from availability");
	frame failed;
	failed.source.backend=input_backend::openvr;failed.source.runtime_focus=true;
	failed.source.runtime_focus_known=true;
	failed.source.gate={input_reason::action_update_failed,17};
	failed.sequence=f.sequence+1;failed.reference_generation=9;failed.sampled_at=f.sampled_at+10ms;
	history.set_gameplay_active(true);history.observe(failed);
	const auto lost=history.snapshot();
	failed.sequence=0;failed.sampled_at+=10ms;failed.source.gate={input_reason::runtime_reset};history.observe(failed);
	f.source.backend=input_backend::openxr;f.sampled_at=failed.sampled_at+10ms;history.observe(f);
	s=history.snapshot();
	check(s.channels[focus].valid && s.channels[focus].losses==1 && s.channels[focus].last_loss.code==17 &&
		s.channels[focus].last_loss_backend==input_backend::openvr && s.last_api_error.code==17 &&
		s.last_api_error_backend==input_backend::openvr && s.invalidations==1,
		"reinitialization and backend recovery cannot erase the original loss or API error");
	check(lost.channels[focus].last_loss_runtime_focus && lost.channels[focus].last_loss_runtime_focus_known &&
		lost.channels[focus].last_loss_gameplay_active &&
		lost.channels[focus].last_loss_reference==9,"loss records distinguish runtime focus from action failure and retain command context");
	report.str("");report.clear();vr::diagnostics::append_input_history(report,s,f.sampled_at);
	check(report.str().find("last_loss=action_update_failed last_loss_code=17")!=std::string::npos &&
		report.str().find("last_input_api_error: backend=openvr")!=std::string::npos,"saved report retains exact reasons and the code's backend after recovery");
	input_history unobserved;
	input_history chronology=history;
	failed.source.gate={input_reason::input_unavailable};failed.source.runtime_focus=false;
	failed.sequence=f.sequence+1;failed.sampled_at=f.sampled_at+10ms;chronology.observe(failed);
	report.str("");report.clear();vr::diagnostics::append_input_history(report,chronology.snapshot(),failed.sampled_at);
	check(report.str().find("left_grip:controller_disconnected:0")!=std::string::npos &&
		report.str().find("gate=input_unavailable")!=std::string::npos,
		"a new dashboard focus loss cannot hide the preceding recovered controller disconnection in recent history");
	for(unsigned i=0;i<20;++i)
	{
		failed.sampled_at+=10ms;++failed.sequence;
		failed.source.gate={i%2 ? input_reason::input_unavailable : input_reason::runtime_reset};chronology.observe(failed);
	}
	check(chronology.snapshot().transition_count==input_transition_capacity && chronology.snapshot().transitions_discarded>0,
		"flapping input history is bounded and reports discarded transitions");
	report.str("");report.clear();vr::diagnostics::append_input_history(report,s,f.sampled_at+151ms);
	check(report.str().find("sample_fresh=0")!=std::string::npos && report.str().find("samples are stale")!=std::string::npos &&
		history.snapshot().channels[focus].losses==s.channels[focus].losses,"stale reporting cannot claim fresh input or invent a disconnection event");
	report.str("");report.clear();vr::diagnostics::append_input_history(report,unobserved.snapshot(),f.sampled_at);
	check(report.str().find("sample_age_ms=-1")!=std::string::npos &&
		report.str().find("No controller action samples")!=std::string::npos,"reporting before any sample is explicit and safe");
}
