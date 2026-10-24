#include "binary_cache.hpp"
#include "component/game_data.hpp"

#include <utils/io.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace binary_cache
{
	namespace
	{
		using file_handle = std::unique_ptr<void, close_handle>;

		file_handle open_executable(const std::filesystem::path& path)
		{
			const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file != INVALID_HANDLE_VALUE) return file_handle(file);
			const auto error = GetLastError();
			if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
			throw std::runtime_error("Could not read the game executable (Windows error " +
				std::to_string(error) + "). Close other game instances and try again.");
		}

		bool matches(HANDLE file, const std::string& expected)
		{
			LARGE_INTEGER size{};
			if (!file || !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
				static_cast<unsigned long long>(size.QuadPart) != expected.size()) return false;

			// Bound memory use even if an old or damaged cache has an enormous size.
			std::array<char, 64 * 1024> buffer{};
			for (std::size_t offset{}; offset < expected.size();)
			{
				const auto count = static_cast<DWORD>((std::min)(buffer.size(), expected.size() - offset));
				DWORD received{};
				if (!ReadFile(file, buffer.data(), count, &received, nullptr) || received != count ||
					std::memcmp(buffer.data(), expected.data() + offset, count) != 0) return false;
				offset += count;
			}
			return true;
		}
	}

	void close_handle::operator()(void* handle) const noexcept
	{
		CloseHandle(handle);
	}

	lease::lease(std::filesystem::path path, file_handle file)
		: path_(std::move(path)), file_(std::move(file))
	{
	}

	const std::filesystem::path& lease::path() const noexcept
	{
		return path_;
	}

	std::string read_original(const std::filesystem::path& path)
	{
		const auto file = open_executable(path);
		if (!file) throw std::runtime_error("The selected game executable is missing: " + path.generic_string());
		LARGE_INTEGER size{};
		if (!GetFileSizeEx(file.get(), &size) || size.QuadPart != game_data::supported_binary_size)
			throw std::runtime_error("File size mismatch, bad game files");
		std::string data(static_cast<std::size_t>(game_data::supported_binary_size), '\0');
		DWORD received{};
		if (!ReadFile(file.get(), data.data(), static_cast<DWORD>(data.size()), &received, nullptr) || received != data.size())
			throw std::runtime_error("Could not completely read the selected game executable");
		return data;
	}

	void patch_aslr(std::string& data)
	{
		if (data.size() != game_data::supported_binary_size)
			throw std::runtime_error("File size mismatch, bad game files");

		IMAGE_DOS_HEADER dos{};
		std::memcpy(&dos, data.data(), sizeof(dos));
		if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
			static_cast<std::size_t>(dos.e_lfanew) > data.size() - sizeof(IMAGE_NT_HEADERS64))
			throw std::runtime_error("Invalid game executable PE headers");

		IMAGE_NT_HEADERS64 headers{};
		std::memcpy(&headers, data.data() + dos.e_lfanew, sizeof(headers));
		if (headers.Signature != IMAGE_NT_SIGNATURE || headers.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
			headers.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
			headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
			headers.OptionalHeader.ImageBase != game_image_base)
			throw std::runtime_error("Unsupported game executable PE layout");

		headers.OptionalHeader.DllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;
		const auto offset = static_cast<std::size_t>(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
			offsetof(IMAGE_OPTIONAL_HEADER64, DllCharacteristics);
		std::memcpy(data.data() + offset, &headers.OptionalHeader.DllCharacteristics, sizeof(WORD));
	}

	lease prepare(const std::filesystem::path& path, const std::string& expected)
	{
		if (expected.empty()) throw std::runtime_error("Cannot cache an empty game executable");
		if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());

		// Serialize only writers to this cache, without waiting on another game.
		// This also protects the shared atomic writer's sibling .tmp file.
		auto lock_path = path;
		lock_path += L".lock";
		const auto raw_lock = CreateFileW(lock_path.c_str(), GENERIC_WRITE, 0, nullptr,
			OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (raw_lock == INVALID_HANDLE_VALUE)
			throw std::runtime_error("Could not lock the game executable cache. Another launch may be preparing it; try again.");
		const file_handle cache_lock(raw_lock);

		auto file = open_executable(path);
		if (matches(file.get(), expected)) return lease(path, std::move(file));
		file.reset();

		if (!utils::io::write_file_atomic(path, expected))
			throw std::runtime_error("Could not refresh the cached game executable. Close other game instances and try again.");

		file = open_executable(path);
		if (!matches(file.get(), expected))
			throw std::runtime_error("The cached game executable does not match the selected game. Startup stopped before loading it.");
		return lease(path, std::move(file));
	}
}
