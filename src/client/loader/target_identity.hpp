#pragma once

#include <cstdint>
#include <string>

namespace target_identity
{
	struct snapshot
	{
		std::string original_path;
		std::string loaded_path;
		std::string original_sha256;
		std::string expected_loaded_sha256;
		std::string loaded_sha256;
		std::uint64_t original_file_size{};
		std::uint64_t loaded_file_size{};
		std::uint32_t original_pe_timestamp{};
		std::uint32_t loaded_pe_timestamp{};
		std::uint32_t original_image_size{};
		std::uint32_t loaded_image_size{};
		std::uint32_t original_pe_checksum{};
		std::uint32_t loaded_pe_checksum{};
		bool original_valid_pe{};
		bool loaded_valid_pe{};
		bool cache_matches_expected{};
		bool compatibility_probe_passed{};
	};

	void capture_original(const std::string& path, const std::string& data);
	void capture_loaded(const std::string& path, const std::string& expected_data);
	void mark_compatibility_probe_passed();

	[[nodiscard]] snapshot get();
}
