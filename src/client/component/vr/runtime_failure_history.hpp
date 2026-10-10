#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace vr
{
	struct runtime_failure_event
	{
		std::uint64_t tick{},session{},frame{},repetitions{1};
		std::int64_t code{};
		std::string operation,stage,detail,runtime,system;
	};
	struct runtime_failure_history
	{
		std::uint64_t total{};
		std::size_t retained{},next{};
		runtime_failure_event first;
		std::array<runtime_failure_event,8> recent;
		void record(std::uint64_t tick,std::uint64_t session,std::uint64_t frame,std::int64_t code,
			std::string_view operation,std::string_view stage,std::string_view detail,
			std::string_view runtime,std::string_view system)
		{
			runtime_failure_event event{tick,session,frame,1,code,std::string(operation.substr(0,256)),
				std::string(stage.substr(0,128)),std::string(detail.substr(0,512)),std::string(runtime.substr(0,256)),std::string(system.substr(0,256))};
			if(!total)first=event;
			++total;
			if(retained)
			{
				auto& prior=recent[(next+recent.size()-1)%recent.size()];
				if(prior.session==session&&prior.code==code&&prior.operation==event.operation&&prior.detail==event.detail)
				{event.repetitions=prior.repetitions+1;prior=std::move(event);return;}
			}
			recent[next]=std::move(event);next=(next+1)%recent.size();
			if(retained<recent.size())++retained;
		}
	};
}
