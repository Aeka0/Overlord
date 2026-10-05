#pragma once
#include "component/console_format.hpp"
#include "component/console_output_queue.hpp"
#include <future>
#include <thread>
#include <atomic>

namespace console_format_tests
{
	inline std::string format(const char* pattern,...)
	{
		va_list arguments;va_start(arguments,pattern);
		auto result=console::detail::format_message(pattern,arguments);
		va_end(arguments);return result;
	}
	inline int run()
	{
		int failures{};
		const auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<"console FAIL: "<<message<<'\n';}};
		for (const auto size:{0u,4095u,4096u,8192u,65536u})
		{
			const std::string text(size,'x');
			check(format("%s",text.c_str())==text,"complete status survives the former 4 KiB assertion boundary");
			check(format("[%u] %s %% %d",size,text.c_str(),-73)=="["+std::to_string(size)+"] "+text+" % -73",
				"retry preserves mixed varargs and literal percent signs");
		}
		const std::string utf8="\xe6\xad\xa6\xe5\x99\xa8\xe7\x8a\xb6\xe6\x80\x81";
		std::string text;for(int n=0;n<1000;++n)text+=utf8;
		check(format("%s",text.c_str())==text,"long UTF-8 diagnostics remain byte-exact");
		check(format("%*s",static_cast<int>(console::detail::maximum_message_size+1),"").starts_with("Console message exceeds"),
			"pathological formatted width has a bounded allocation");
		std::atomic_bool intact{true};
		const auto worker=[&](char value){const std::string own(16000,value);for(int n=0;n<50;++n)if(format("%s",own.c_str())!=own)intact=false;};
		std::thread a(worker,'a'),b(worker,'b');a.join();b.join();
		check(intact,"simultaneous diagnostic writers cannot overwrite another message");
		{
			console::detail::output_queue queue;
			queue.push(3,"warning");queue.push(3,"warning");queue.push(7,utf8);
			auto batch=queue.take();
			check(batch.messages.size()==2 && batch.messages[0].repetitions==2 && batch.messages[1].text==utf8,
				"ordered logs coalesce repeated warnings and retain UTF-8");
			queue.push(7,"before blocked sink");
			std::promise<void> blocked,release;auto gate=release.get_future();
			std::thread sink([&]{auto owned=queue.take();blocked.set_value();gate.wait();});
			blocked.get_future().wait();
			auto producer=std::async(std::launch::async,[&]{for(int i=0;i<100;++i)queue.push(3,std::to_string(i));});
			const bool independent=producer.wait_for(std::chrono::seconds(1))==std::future_status::ready;
			release.set_value();sink.join();producer.get();
			check(independent && queue.take().messages.size()==100,"a blocked terminal sink cannot hold a rendering producer's queue lock");
			queue.push(7,std::string(console::detail::output_queue::byte_limit,'x'));
			check(!queue.push(3,"overflow") && !queue.push(7,std::string(console::detail::output_queue::byte_limit+1,'y')),
				"blocked output has a fixed byte budget, including oversized single messages");
			batch=queue.take();check(batch.messages.size()==1 && batch.dropped==2,"overflow is counted rather than silently lost");
			for(unsigned i=0;i<console::detail::output_queue::message_limit+10;++i)queue.push(3,std::to_string(i));
			batch=queue.take();check(batch.messages.size()==console::detail::output_queue::message_limit && batch.dropped==10,
				"tiny warning storms also have a bounded message count");
		}
		return failures;
	}
}
