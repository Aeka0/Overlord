#pragma once

#include <utils/nt.hpp>
#include <cstdint>
#include <memory>

namespace binary_cache
{
	inline constexpr std::uintptr_t game_image_base = 0x140000000;

	struct close_handle
	{
		void operator()(void* handle) const noexcept;
	};

	// Deny writes/deletes from the identity check until LoadLibrary has mapped
	// the file. A matching cache must not change between those operations.
	class lease final
	{
	public:
		lease(std::filesystem::path path, std::unique_ptr<void, close_handle> file);
		[[nodiscard]] const std::filesystem::path& path() const noexcept;

	private:
		std::filesystem::path path_;
		std::unique_ptr<void, close_handle> file_;
	};

	[[nodiscard]] std::string read_original(const std::filesystem::path& path);
	void patch_aslr(std::string& data);
	[[nodiscard]] lease prepare(const std::filesystem::path& path, const std::string& expected);
}
