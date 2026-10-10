#include <std_include.hpp>

#include "status_sections.hpp"
#include "format_helpers.hpp"
#include "build_config.hpp"
#include "product.hpp"
#include "game/game.hpp"
#include "component/game_module.hpp"
#include "../settings.hpp"
#include "launcher/risk_settings_config.hpp"
#include <utils/native_memory.hpp>
#include <utils/nt.hpp>
#include <version.h>
#include <iomanip>
#include <sstream>
#include <vector>

namespace vr::diagnostics::detail
{
	namespace
	{
		std::string module_path(HMODULE module)
		{
			if (!module) return {};
			std::vector<wchar_t> path(32768);
			const auto size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
			if (!size || size >= path.size()) return {};
			const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
				path.data(), static_cast<int>(size), nullptr, 0, nullptr, nullptr);
			if (!bytes) return {};
			std::string utf8(static_cast<std::size_t>(bytes), '\0');
			if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(),
				static_cast<int>(size), utf8.data(), bytes, nullptr, nullptr)) return {};
			return utf8;
		}

		void append_image_identity(std::ostringstream& out, const utils::nt::library& module)
		{
			IMAGE_DOS_HEADER dos{};
			IMAGE_NT_HEADERS64 nt{};
			const auto* base = module.get_ptr();
			if (!utils::native_memory::read_bytes(&dos, base, sizeof(dos)) ||
				dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || dos.e_lfanew > 0x100000 ||
				!utils::native_memory::read_at(base, static_cast<std::size_t>(dos.e_lfanew), nt) ||
				nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
			{out << "  client_image: unavailable\n"; return;}
			out << "  client_image: pe_timestamp=0x" << std::hex << nt.FileHeader.TimeDateStamp
				<< " image_size=0x" << nt.OptionalHeader.SizeOfImage << std::dec << '\n';
			// Read the loaded client's RSDS identity, not a replaceable file on disk
			// or the manually mapped game image. GUID/age identify the matching PDB.
			if (nt.OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_DEBUG)
			{
				const auto& directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
				const auto inside = [&](std::size_t offset, std::size_t bytes)
				{return offset < nt.OptionalHeader.SizeOfImage && bytes <= nt.OptionalHeader.SizeOfImage-offset;};
				if (directory.VirtualAddress && inside(directory.VirtualAddress, directory.Size) &&
					directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY) <= 64)
					for (std::size_t i{}; i < directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i)
					{
						IMAGE_DEBUG_DIRECTORY debug{};
						struct codeview {std::uint32_t signature; GUID guid; std::uint32_t age;} cv{};
						if (!utils::native_memory::read_at(base, directory.VirtualAddress+i*sizeof(debug), debug) ||
							debug.Type != IMAGE_DEBUG_TYPE_CODEVIEW || debug.SizeOfData < sizeof(cv) ||
							!inside(debug.AddressOfRawData, sizeof(cv)) ||
							!utils::native_memory::read_at(base, debug.AddressOfRawData, cv) || cv.signature != 0x53445352) continue;
						out << "  client_pdb: guid=" << std::hex << std::setfill('0')
							<< std::setw(8) << cv.guid.Data1 << '-' << std::setw(4) << cv.guid.Data2
							<< '-' << std::setw(4) << cv.guid.Data3 << '-';
						for (unsigned b{}; b < 8; ++b)
						{if (b == 2) out << '-'; out << std::setw(2) << unsigned(cv.guid.Data4[b]);}
						out << std::setfill(' ') << std::dec << " age=" << cv.age << '\n';
						return;
					}
			}
			out << "  client_pdb: unavailable\n";
		}

		// Keep native calls in destructor-free leaves. A crash/stall report must
		// tolerate unavailable engine globals without walking the VM or scheduler.
		bool read_scene_initialized(bool& value) noexcept
		{
			__try {value = game::CL_IsCgameInitialized(); return true;}
			__except (EXCEPTION_EXECUTE_HANDLER) {return false;}
		}
		bool read_dvar(const char* name, game::dvar_t& value) noexcept
		{
			__try {return utils::native_memory::read_bytes(&value, game::Dvar_FindVar(name), sizeof(value));}
			__except (EXCEPTION_EXECUTE_HANDLER) {return false;}
		}
		std::string dvar_text(const game::dvar_t& value)
		{
			if (value.type != game::dvar_type::string || !value.current.string) return "unavailable";
			std::string text;
			for (std::size_t i{}; i < 256; ++i)
			{
				char c{};
				if (!utils::native_memory::read_at(value.current.string, i, c)) return "unreadable";
				if (!c) return quoted_text(text);
				text += c;
			}
			return quoted_text(text+"[truncated]");
		}
		void append_setting(std::ostringstream& out, const char* name)
		{
			game::dvar_t value{};
			out << "  " << name << '=';
			if (!read_dvar(name,value)) {out << "unavailable\n";return;}
			if (value.type==game::dvar_type::string) out<<dvar_text(value);
			else if (value.type==game::dvar_type::boolean) out<<yes_no(value.current.enabled);
			else if (value.type==game::dvar_type::value) out<<value.current.value;
			else if (value.type==game::dvar_type::integer||value.type==game::dvar_type::enumeration)
			{
				out<<value.current.integer;
				if(value.type==game::dvar_type::enumeration&&value.current.integer>=0&&
					value.current.integer<value.domain.enumeration.stringCount&&value.domain.enumeration.stringCount<=256)
				{
					game::dvar_t text{};text.type=game::dvar_type::string;
					if(utils::native_memory::read_at(value.domain.enumeration.strings,
						static_cast<std::size_t>(value.current.integer)*sizeof(const char*),text.current.string))out<<" label="<<dvar_text(text);
				}
			}
			else out<<"unsupported_dvar_type";
			out<<'\n';
		}
	}

	void append_session_status(std::ostringstream& out, const bool target_ready)
	{
		SYSTEMTIME utc{}; GetSystemTime(&utc);
		out << "incident context:\n  report_utc=" << std::setfill('0')
			<< std::setw(4) << utc.wYear << '-' << std::setw(2) << utc.wMonth << '-' << std::setw(2) << utc.wDay
			<< 'T' << std::setw(2) << utc.wHour << ':' << std::setw(2) << utc.wMinute << ':' << std::setw(2) << utc.wSecond
			<< '.' << std::setw(3) << utc.wMilliseconds << "Z" << std::setfill(' ')
			<< " pid=" << GetCurrentProcessId() << " tick_ms=" << GetTickCount64() << '\n';
		out << "  client: product=" << quoted_text(product::name) << " version=" << quoted_text(VERSION)
			<< " configuration=" << build_config::name << " git_hash=" << GIT_HASH
			<< " git_describe=" << quoted_text(GIT_DESCRIBE) << " git_branch=" << quoted_text(GIT_BRANCH)
			<< " git_dirty=" << GIT_DIRTY << " compiled=" << quoted_text(__DATE__ " " __TIME__) << '\n';
		const auto module = game_module::get_host_module();
		const auto path = module_path(module.get_handle());
		out << "  client_path=" << (path.empty() ? "unavailable" : quoted_text(path, 32768)) << '\n';
		append_image_identity(out, module);
		std::error_code cwd_error;
		const auto cwd=std::filesystem::current_path(cwd_error).u8string();
		out<<"  working_directory="<<(cwd_error?"unavailable":quoted_text(
			std::string_view(reinterpret_cast<const char*>(cwd.data()),cwd.size()),32768))<<'\n';
		out << "game context:\n  target_ready=" << yes_no(target_ready);
		if (!target_ready) {out << " native_state=unavailable\n"; return;}
		try {out << " environment=" << quoted_text(game::environment::get_string());}
		catch (...) {out << " environment=unavailable";}
		bool scene{};
		out << " scene_initialized=" << (read_scene_initialized(scene) ? yes_no(scene) : "unavailable");
		std::uint8_t frontend{};
		out << " frontend=" << (utils::native_memory::read_bytes(&frontend,
			reinterpret_cast<const void*>(0x140BEBCCC), sizeof(frontend)) ? yes_no(frontend != 0) : "unavailable") << '\n';
		for (const auto* name : {"mapname", "cl_ingame", "sv_running", "cl_paused", "cg_cinematicFullscreen"})
			append_setting(out,name);
		out<<"effective settings: source=native_current_values\n";
		for(const auto& setting:settings::choices)append_setting(out,setting.name);
		for(const auto& setting:settings::toggles)append_setting(out,setting.name);
		for(const auto& setting:settings::numbers)append_setting(out,setting.name);
		for(const auto& setting:launcher_vr_settings::risk_settings)append_setting(out,setting.name);
		for(const auto* name:{"r_postAA","r_displayMode","r_mode","r_vsync","com_maxfps","vr_engineProbe"})append_setting(out,name);
	}
}
