#pragma once
#include "component/vr/controller_profile_retry.hpp"

template<class Check> void controller_profile_retry_tests(Check& check)
{
	using vr::controller_profile::refresh_retry;
	const refresh_retry::clock::time_point now{};
	refresh_retry retry;
	check(retry.ready(now),"controller identity is queried immediately after initialization");
	retry.record_result(false,now);
	check(!retry.ready(now) && !retry.ready(now+std::chrono::milliseconds(249)),
		"failed controller queries do not poll on every frame");
	check(retry.ready(now+refresh_retry::retry_delay),"transient query failure retries without a device event");
	retry.record_result(true,now+refresh_retry::retry_delay);
	check(!retry.ready(now+std::chrono::hours(1)),"known controller identity stops querying, including non-Index pairs");
	retry.reset();
	check(retry.ready(now),"device/profile changes rearm controller identification immediately");
	for(unsigned attempt=0;attempt<refresh_retry::attempt_limit;++attempt)
	{
		const auto time=now+refresh_retry::retry_delay*attempt;
		check(retry.ready(time),"failed identity query retains its bounded retry budget");
		retry.record_result(false,time);
	}
	check(!retry.ready(now+std::chrono::hours(1)),"persistent runtime failure exhausts the retry budget");
	retry.reset();
	check(retry.ready(now),"a new runtime event recovers even after retry exhaustion");
}
