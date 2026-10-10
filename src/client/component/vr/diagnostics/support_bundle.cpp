#include <std_include.hpp>
#include "support_bundle.hpp"
#include "support_archive.hpp"
#include "report_paths.hpp"
#include "../diagnostics.hpp"
#include "../vr_runtime.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "component/game_data.hpp"
#include "launcher/vr_settings.hpp"
#include "launcher/risk_settings_config.hpp"
#include "launcher/preflight.hpp"
#include <utils/latest_task_worker.hpp>
#include <condition_variable>
#include <fstream>
#include <shlobj.h>

namespace vr::diagnostics::support
{
	namespace
	{
		utils::latest_task_worker worker;
		std::function<bool()> enabled;
		std::atomic_bool stopping{true},manual_pending{},checkpoint_pending{};
		std::atomic_uint64_t serial{},saved_signature{},retry_after{};
		std::mutex wait_mutex,admission_mutex;
		std::condition_variable wake;
		bool shortcut_held{};
		std::uint64_t next_poll{};

		void reveal(const std::filesystem::path& path)
		{
			const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
			const auto cleanup=gsl::finally([&]{if(SUCCEEDED(initialized))CoUninitialize();});
			if(auto* item=ILCreateFromPathW(path.c_str()))
			{
				const auto result=SHOpenFolderAndSelectItems(item,0,nullptr,0);ILFree(item);
				if(SUCCEEDED(result))return;
			}
			ShellExecuteW(nullptr,L"open",path.parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
		}
		std::unique_ptr<archive> reserve_archive()
		{
			SYSTEMTIME utc{};GetSystemTime(&utc);
			const auto id=std::format("overlord-diagnostics-{:04}{:02}{:02}-{:02}{:02}{:02}-p{}-{}-{}",
				utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond,
				GetCurrentProcessId(),GetTickCount64(),++serial);
			for(const auto& root:report_directories())try{return std::make_unique<archive>(root,id);}catch(const std::filesystem::filesystem_error&){}
			throw std::runtime_error("No writable diagnostic folder");
		}
		void add_existing(archive& result)
		{
			unsigned source{};
			for(const auto& root:report_directories())result.include(root/"overlord-status-latest.txt",std::format("previous-status-{}.txt",++source));
			result.include("minidumps/overlord-status-latest.txt","legacy-status.txt");
			// Only files produced by Overlord's diagnostic paths. Existing files
			// remain labelled historical; a fresh process never relabels an old dump.
			std::vector<std::pair<std::filesystem::file_time_type,std::filesystem::path>> candidates;
			std::error_code error;std::size_t visited{};
			for(std::filesystem::directory_iterator it("minidumps",error),end;!error&&it!=end;it.increment(error))
			{
				if(++visited>2048){result.note("existing-artifacts","directory_scan_capped");break;}
				const auto& path=it->path();const auto name=path.filename().string();
				if(!(name.starts_with("overlord-crash-")||name.starts_with("overlord-emergency-")||
					name.starts_with("overlord-scene-stall-")||name.starts_with("overlord-backend-stall-")||
					name.starts_with("overlord-interop-stall-")||name.starts_with("overlord-soft-freeze-")))continue;
				if(path.extension()!=L".txt"&&path.extension()!=L".dmp")continue;
				std::error_code metadata_error;
				const auto stamp=it->last_write_time(metadata_error);
				if(!metadata_error)candidates.emplace_back(stamp,path);
			}
			std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first>b.first;});
			unsigned texts{},dumps{};
			for(const auto& [stamp,path]:candidates)
			{
				auto& count=path.extension()==L".dmp"?dumps:texts;
				if(count>=2)continue;++count;
				result.include(path,"existing-"+path.filename().string());
			}
		}
		void add_settings(archive& result)
		{
			try
			{
				const auto profile=launcher_vr_settings::read_profile();
				const auto risks=launcher_vr_settings::read_risk_settings(profile);
				nlohmann::json saved{{"profile_path",game_data::get_config_source_path()},
					{"launcher_resolved_values",launcher_vr_settings::read_values(profile)}};
				for(std::size_t i{};i<risks.size();++i)saved["saved_risk_settings"][launcher_vr_settings::risk_settings[i].name]=
					risks[i]?nlohmann::json(*risks[i]):nlohmann::json(nullptr);
				result.add("settings-saved.json",saved.dump(2,' ',false,nlohmann::json::error_handler_t::replace));
				result.add("startup-preflight.json",launcher_preflight::check().dump(2));
			}
			catch(const std::exception& error){result.note("saved-settings",error.what());}
		}
		void collect()
		{
			const auto complete=gsl::finally([]{manual_pending=false;});
			if(stopping)return;
			auto result=reserve_archive();
			try
			{
				// Save each snapshot immediately, so ZIP failure leaves usable text.
				result->add("README.txt","Attach this ZIP to your Overlord issue. No console copying is needed.\n"
					"This is a local diagnostic capture; nothing was uploaded automatically.\n"
					"status-1/2 are fresh snapshots from this request, about five seconds apart.\n"
					"The process/build/time and before_first_present labels identify their scope.\n"
					"previous-status and existing-* are historical files; their association with this run is not assumed.\n"
					"manifest.json lists unavailable/omitted files and size limits. Large dumps are not silently truncated.\n"
					"A brief description of what you saw is still useful; the package cannot observe the physical HMD display.\n");
				result->add("core-1.txt",collect_core_status_text(enabled()));
				result->add("status-1.txt",collect_status_text(enabled()));
				result->add("trace-1.txt",crash_trace());
				{
					std::unique_lock lock(wait_mutex);wake.wait_for(lock,5s,[]{return stopping.load();});
				}
				if(!stopping)
				{
					result->add("core-2.txt",collect_core_status_text(enabled()));
					result->add("status-2.txt",collect_status_text(enabled()));
					result->add("trace-2.txt",crash_trace());
					add_settings(*result);
				}
				else result->note("status-2.txt","cancelled_on_shutdown");
				result->add("console-recent.txt",console::diagnostic_history());
				// Fresh evidence has priority over potentially large historical dumps.
				add_existing(*result);
				const auto path=result->finish();
				console::info("[VR] Diagnostic ZIP ready: %s\n[VR] Upload this ZIP with your issue. No text copying is needed.\n",report_path_text(path).c_str());
				if(!stopping)reveal(path);
			}
			catch(const std::exception& error)
			{
				console::error("[VR] Diagnostic collection incomplete: %s\n[VR] Captured files: %s\n",error.what(),report_path_text(result->staging()).c_str());
				if(!stopping)reveal(result->staging()/"status-1.txt");
			}
		}
		void poll()
		{
			if(stopping)return;
			const bool keys=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_SHIFT)&0x8000)&&(GetAsyncKeyState(VK_F8)&0x8000);
			if(keys&&!shortcut_held)
			{
				DWORD foreground_process{};const auto foreground=GetForegroundWindow();
				GetWindowThreadProcessId(foreground,&foreground_process);
				if(foreground_process==GetCurrentProcessId()||foreground==GetConsoleWindow())request();
			}
			shortcut_held=keys;
			const auto now=GetTickCount64();if(now<next_poll)return;next_poll=now+1000;
			if(manual_pending||checkpoint_pending||now<retry_after)return;
			const auto state=runtime::get().get_status();const auto graphics=d3d11::get_graphics_status();
			if(!graphics.present_count&&!state.failures.total)return;
			const auto signature=(state.session_generation<<32)^(state.failures.retained<<8)^
				(static_cast<std::uint64_t>(state.state)<<2)^(state.native_renderer_ready?2ull:0ull)^1ull;
			if(saved_signature==signature)return;
			// A manual request may arrive while the read-only snapshots above run.
			// Never replace its pending task with an automatic checkpoint.
			const std::lock_guard admission(admission_mutex);
			if(stopping||manual_pending||checkpoint_pending)return;
			checkpoint_pending=true;
			if(!worker.replace([signature]
			{
				const auto done=gsl::finally([]{checkpoint_pending=false;});
				if(stopping)return;
				if(write_status_snapshot(enabled()))saved_signature=signature;
				else retry_after=GetTickCount64()+30000;
			},[](std::exception_ptr){checkpoint_pending=false;retry_after=GetTickCount64()+30000;}))checkpoint_pending=false;
		}
	}
	void start(std::function<bool()> provider)
	{
		enabled=std::move(provider);stopping=false;
		scheduler::loop(poll,scheduler::pipeline::async,10ms);
		console::info("[VR] Need help? Press Ctrl+Shift+F8 in the game, or run vr_diagnose. A diagnostic ZIP will be created and selected for upload.\n");
	}
	void request()
	{
		const std::lock_guard admission(admission_mutex);
		if(stopping)return;
		if(manual_pending.exchange(true)){console::info("[VR] Diagnostic collection is already running; please wait for the ZIP folder.\n");return;}
		checkpoint_pending=false;
		if(!worker.replace(collect,[](std::exception_ptr error)
		{
			manual_pending=false;
			try{if(error)std::rethrow_exception(error);}catch(const std::exception& e){console::error("[VR] Diagnostic collection failed: %s\n",e.what());}
		}))manual_pending=false;
		else console::info("[VR] Collecting diagnostics for five seconds. Keep the issue visible; the ZIP folder will open automatically.\n");
	}
	void stop()
	{
		{const std::lock_guard admission(admission_mutex);stopping=true;}
		wake.notify_all();worker.stop();
	}
}
