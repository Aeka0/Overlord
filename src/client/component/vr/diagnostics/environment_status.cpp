#include <std_include.hpp>
#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "component/d3d11.hpp"
#include <psapi.h>

namespace vr::diagnostics::detail
{
	namespace
	{
		std::string utf8(const wchar_t* text)
		{
			const auto size=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
			if(size<=1)return {};
			std::string result(static_cast<std::size_t>(size),'\0');
			if(!WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),size,nullptr,nullptr))return {};
			result.pop_back();return result;
		}
	}
	void append_environment_status(std::ostringstream& out,const d3d11::device_snapshot& device)
	{
		out<<"system environment:\n";
		OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);
		const auto query=reinterpret_cast<LONG(WINAPI*)(OSVERSIONINFOW*)>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
		if(query&&query(&version)==0)out<<"  windows="<<version.dwMajorVersion<<'.'<<version.dwMinorVersion<<'.'<<version.dwBuildNumber<<'\n';
		else out<<"  windows=unavailable\n";
		std::array<wchar_t,256> cpu{};DWORD bytes=static_cast<DWORD>(sizeof(cpu));
		const auto cpu_result=RegGetValueW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
			L"ProcessorNameString",RRF_RT_REG_SZ,nullptr,cpu.data(),&bytes);
		SYSTEM_INFO system{};GetNativeSystemInfo(&system);
		MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
		out<<"  cpu="<<(cpu_result==ERROR_SUCCESS?quoted_text(utf8(cpu.data())):"unavailable")
			<<" logical_processors="<<system.dwNumberOfProcessors;
		if(GlobalMemoryStatusEx(&memory))out<<" ram_total_mb="<<memory.ullTotalPhys/(1024*1024)<<" ram_available_mb="<<memory.ullAvailPhys/(1024*1024);
		out<<'\n';
		FILETIME created{},exited{},kernel{},user{},now{};
		if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))
		{
			GetSystemTimeAsFileTime(&now);
			const auto value=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
			out<<"  process_start_filetime="<<value(created)<<" process_uptime_ms="<<(value(now)-value(created))/10000<<'\n';
		}
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		DXGI_ADAPTER_DESC description{};
		if(device.device&&SUCCEEDED(device.device.As(&dxgi))&&SUCCEEDED(dxgi->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&description)))
		{
			out<<"  graphics_adapter: name="<<quoted_text(utf8(description.Description))
				<<" vendor=0x"<<std::hex<<description.VendorId<<" device=0x"<<description.DeviceId
				<<" luid=0x"<<static_cast<std::uint32_t>(description.AdapterLuid.HighPart)<<':'<<description.AdapterLuid.LowPart
				<<std::dec<<" dedicated_mb="<<description.DedicatedVideoMemory/(1024*1024)<<" generation="<<device.generation;
			LARGE_INTEGER driver{};
			const auto result=adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver);
			if(SUCCEEDED(result))out<<" driver="<<HIWORD(driver.HighPart)<<'.'<<LOWORD(driver.HighPart)<<'.'<<HIWORD(driver.LowPart)<<'.'<<LOWORD(driver.LowPart);
			else out<<" driver=unavailable query=0x"<<std::hex<<static_cast<std::uint32_t>(result)<<std::dec;
			out<<'\n';
		}
		else out<<"  graphics_adapter=unavailable\n";
		// In-process module names explain runtime/injector overlap without reading
		// arbitrary files, registry inventories or another application's logs.
		std::array<HMODULE,512> modules{};DWORD required{};
		if(EnumProcessModules(GetCurrentProcess(),modules.data(),static_cast<DWORD>(sizeof(modules)),&required))
		{
			out<<"  loaded_modules: truncated="<<yes_no(required>sizeof(modules))<<'\n';
			for(std::size_t i{};i<std::min<std::size_t>(required/sizeof(HMODULE),modules.size());++i)
			{
				std::array<wchar_t,512> name{};
				if(GetModuleBaseNameW(GetCurrentProcess(),modules[i],name.data(),static_cast<DWORD>(name.size())))
					out<<"    "<<quoted_text(utf8(name.data()))<<'\n';
			}
		}
		else out<<"  loaded_modules=unavailable\n";
	}
}
