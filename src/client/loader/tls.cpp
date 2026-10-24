#include <std_include.hpp>
#include "tls.hpp"

#include <utils/nt.hpp>
#include <utils/binary_resource.hpp>

namespace tls
{
	namespace
	{
		utils::binary_resource tls_dll_file(TLS_DLL, "overlord-tlsdll.dll");
	}

	utils::nt::library allocate_helper()
	{
		static auto already_allocated = false;
		if (already_allocated)
		{
			throw std::runtime_error("Currently only a single allocation is supported!");
		}

		already_allocated = true;

		const auto dll_path = tls_dll_file.get_extracted_file();
		const auto tls_dll = utils::nt::library::load(dll_path);
		if (!tls_dll)
		{
			throw std::runtime_error("Failed to load TLS DLL");
		}

		return tls_dll;
	}
}
