#include <std_include.hpp>
#include "target_identity.hpp"

#include <utils/cryptography.hpp>
#include <utils/io.hpp>

namespace target_identity
{
	namespace
	{
		struct pe_identity
		{
			std::uint32_t timestamp{};
			std::uint32_t image_size{};
			std::uint32_t checksum{};
			bool valid{};
		};

		std::mutex identity_mutex;
		snapshot identity;

		template <typename T>
		bool read_at(const std::string& data, const size_t offset, T& value)
		{
			if (offset > data.size() || sizeof(T) > data.size() - offset)
			{
				return false;
			}

			std::memcpy(&value, data.data() + offset, sizeof(value));
			return true;
		}

		pe_identity read_pe_identity(const std::string& data)
		{
			pe_identity result;
			IMAGE_DOS_HEADER dos_header{};
			if (!read_at(data, 0, dos_header) || dos_header.e_magic != IMAGE_DOS_SIGNATURE || dos_header.e_lfanew < 0)
			{
				return result;
			}

			IMAGE_NT_HEADERS64 nt_headers{};
			if (!read_at(data, static_cast<size_t>(dos_header.e_lfanew), nt_headers) ||
				nt_headers.Signature != IMAGE_NT_SIGNATURE ||
				nt_headers.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
				nt_headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
			{
				return result;
			}

			result.timestamp = nt_headers.FileHeader.TimeDateStamp;
			result.image_size = nt_headers.OptionalHeader.SizeOfImage;
			result.checksum = nt_headers.OptionalHeader.CheckSum;
			result.valid = true;
			return result;
		}

		std::string absolute_path_or_input(const std::string& path)
		{
			std::error_code path_error;
			const auto absolute_path = std::filesystem::absolute(path, path_error);
			return path_error ? path : absolute_path.generic_string();
		}
	}

	void capture_original(const std::string& path, const std::string& data)
	{
		const auto pe = read_pe_identity(data);
		snapshot captured;
		captured.original_path = absolute_path_or_input(path);
		captured.original_sha256 = utils::cryptography::sha256::compute(data, true);
		captured.original_file_size = static_cast<std::uint64_t>(data.size());
		captured.original_pe_timestamp = pe.timestamp;
		captured.original_image_size = pe.image_size;
		captured.original_pe_checksum = pe.checksum;
		captured.original_valid_pe = pe.valid;

		std::lock_guard lock(identity_mutex);
		identity = std::move(captured);
	}

	void capture_loaded(const std::string& path, const std::string& expected_data)
	{
		const auto loaded_data = utils::io::read_file(path);
		const auto pe = read_pe_identity(loaded_data);
		const auto expected_hash = utils::cryptography::sha256::compute(expected_data, true);
		const auto loaded_hash = loaded_data.empty()
			? std::string{}
			: utils::cryptography::sha256::compute(loaded_data, true);

		std::lock_guard lock(identity_mutex);
		identity.loaded_path = absolute_path_or_input(path);
		identity.expected_loaded_sha256 = expected_hash;
		identity.loaded_sha256 = loaded_hash;
		identity.loaded_file_size = static_cast<std::uint64_t>(loaded_data.size());
		identity.loaded_pe_timestamp = pe.timestamp;
		identity.loaded_image_size = pe.image_size;
		identity.loaded_pe_checksum = pe.checksum;
		identity.loaded_valid_pe = pe.valid;
		identity.cache_matches_expected = !loaded_hash.empty() && loaded_hash == expected_hash;
	}

	void mark_compatibility_probe_passed()
	{
		std::lock_guard lock(identity_mutex);
		identity.compatibility_probe_passed = true;
	}

	snapshot get()
	{
		std::lock_guard lock(identity_mutex);
		return identity;
	}
}
