#include "loader/binary_cache.hpp"
#include "component/game_data.hpp"
#include <utils/io.hpp>

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace
{
	unsigned checks{};
	void require(const bool value, const char* message)
	{
		++checks;
		if (!value) throw std::runtime_error(message);
	}

	template <typename F> void rejects(F&& operation, const char* message)
	{
		bool rejected{};
		try { operation(); }
		catch (const std::runtime_error&) { rejected = true; }
		require(rejected, message);
	}

	std::string executable()
	{
		std::string bytes(static_cast<std::size_t>(game_data::supported_binary_size), '\0');
		IMAGE_DOS_HEADER dos{};
		dos.e_magic = IMAGE_DOS_SIGNATURE;
		dos.e_lfanew = 0x80;
		IMAGE_NT_HEADERS64 headers{};
		headers.Signature = IMAGE_NT_SIGNATURE;
		headers.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
		headers.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
		headers.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
		headers.OptionalHeader.ImageBase = binary_cache::game_image_base;
		headers.OptionalHeader.DllCharacteristics = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT;
		std::memcpy(bytes.data(), &dos, sizeof(dos));
		std::memcpy(bytes.data() + dos.e_lfanew, &headers, sizeof(headers));
		bytes.back() = 'X';
		return bytes;
	}

	void pe_checks()
	{
		const auto original = executable();
		auto patched = original;
		binary_cache::patch_aslr(patched);
		IMAGE_NT_HEADERS64 headers{};
		std::memcpy(&headers, patched.data() + 0x80, sizeof(headers));
		require(!(headers.OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE), "ASLR flag remains enabled");
		require(headers.OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_NX_COMPAT, "unrelated PE flag changed");
		headers.OptionalHeader.DllCharacteristics |= IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;
		std::memcpy(patched.data() + 0x80, &headers, sizeof(headers));
		require(patched == original, "ASLR patch changed unrelated executable bytes");

		for (const LONG offset : {-1L, 0L, 0x7fffffffL, static_cast<LONG>(original.size() - 1)})
		{
			auto invalid = original;
			std::memcpy(invalid.data() + offsetof(IMAGE_DOS_HEADER, e_lfanew), &offset, sizeof(offset));
			rejects([&] { binary_cache::patch_aslr(invalid); }, "invalid NT header offset accepted");
		}
		auto invalid = original;
		invalid[0] = 0;
		rejects([&] { binary_cache::patch_aslr(invalid); }, "invalid DOS signature accepted");
		invalid = original;
		invalid[0x80] = 0;
		rejects([&] { binary_cache::patch_aslr(invalid); }, "invalid NT signature accepted");
		invalid = original;
		headers.OptionalHeader.ImageBase = 0x180000000;
		std::memcpy(invalid.data() + 0x80, &headers, sizeof(headers));
		rejects([&] { binary_cache::patch_aslr(invalid); }, "unsupported preferred base accepted");
		invalid.resize(16);
		rejects([&] { binary_cache::patch_aslr(invalid); }, "truncated executable accepted");
	}

	void cache_checks(const std::filesystem::path& directory)
	{
		const auto path = directory / L"cache" / L"h2_sp64_bnet_ship.exe";
		const std::string original(150000, 'a');
		{
			const auto first = binary_cache::prepare(path, original);
			require(utils::io::read_file(path.string()) == original, "initial cache is incomplete");
			const auto second = binary_cache::prepare(path, original);
			require(second.path() == path, "matching cache not reused");
			const auto writer = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
			if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
			require(writer == INVALID_HANDLE_VALUE, "cache lease permits replacement before mapping");
			require(!DeleteFileW(path.c_str()), "cache lease permits deletion before mapping");
			rejects([&] { (void)binary_cache::prepare(path, "changed"); }, "in-use stale cache silently accepted");
			require(utils::io::read_file(path.string()) == original, "failed refresh damaged an in-use cache");
		}
		auto updated = original;
		updated[80000] = 'b';
		{
			const auto refreshed = binary_cache::prepare(path, updated);
			require(utils::io::read_file(refreshed.path().string()) == updated, "same-size stale cache not refreshed");
		}
		{
			const auto shortened = binary_cache::prepare(path, "new");
			require(utils::io::read_file(shortened.path().string()) == "new", "different-size stale cache not refreshed");
		}
		rejects([&] { (void)binary_cache::read_original(path); }, "wrong-size original executable accepted");
		const auto original_path = directory / L"original.exe";
		const auto original_image = executable();
		require(utils::io::write_file_atomic(original_path, original_image), "cannot create original fixture");
		require(binary_cache::read_original(original_path) == original_image, "bounded original read changed bytes");

		auto lock_path = path;
		lock_path += L".lock";
		const auto writer_lock = CreateFileW(lock_path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		require(writer_lock != INVALID_HANDLE_VALUE, "cannot arrange competing writer");
		rejects([&] { (void)binary_cache::prepare(path, updated); }, "competing cache writer not rejected");
		CloseHandle(writer_lock);
		require(utils::io::read_file(path.string()) == "new", "competing writer damaged the old cache");
		rejects([&] { (void)binary_cache::prepare(path, {}); }, "empty cache accepted");
	}
}

int main()
{
	wchar_t temporary[MAX_PATH]{};
	if (!GetTempPathW(MAX_PATH, temporary)) return 2;
	const auto directory = std::filesystem::path(temporary) / (L"overlord-loader-tests-" + std::to_wstring(GetCurrentProcessId()));
	int result{};
	try
	{
		pe_checks();
		cache_checks(directory);
		std::printf("PASS: game loader checks=%u\n", checks);
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	std::error_code cleanup_error;
	std::filesystem::remove_all(directory, cleanup_error);
	return result;
}
