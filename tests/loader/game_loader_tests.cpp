#include "loader/binary_cache.hpp"
#include "loader/tls_bootstrap.hpp"
#include "component/game_data.hpp"
#include <utils/io.hpp>

#include <cstdio>
#include <cstring>
#include <array>
#include <limits>
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
		headers.OptionalHeader.ImageBase = game_image::base_address;
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

	struct tls_image
	{
		void* memory = VirtualAlloc(nullptr, 0x5000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		tls::image_view view{reinterpret_cast<std::uintptr_t>(memory), 0x5000, {0x100, sizeof(IMAGE_TLS_DIRECTORY64)}};
		IMAGE_TLS_DIRECTORY64& directory() { return *reinterpret_cast<IMAGE_TLS_DIRECTORY64*>(view.base + 0x100); }
		DWORD& index() { return *reinterpret_cast<DWORD*>(view.base + 0x200); }
		char* data() { return reinterpret_cast<char*>(view.base + 0x1000); }
		tls_image()
		{
			if (!memory) throw std::runtime_error("TLS fixture allocation failed");
			directory().StartAddressOfRawData = view.base + 0x1000;
			directory().EndAddressOfRawData = view.base + 0x1100;
			directory().AddressOfIndex = view.base + 0x200;
		}
		~tls_image() { VirtualFree(memory, 0, MEM_RELEASE); }
	};

	void tls_checks()
	{
		require(game_image::supported_mapping(0x140000000, 0x140000000), "supported fixed image rejected");
		require(!game_image::supported_mapping(0x083b0000, 0x140000000), "issue 111 relocated image accepted");
		require(!game_image::supported_mapping(0x0ae90000, 0x140000000), "issue 85 relocated image accepted");
		require(!game_image::supported_mapping(0x140000000, 0x180000000), "unsupported preferred image base accepted");

		tls_image game, helper;
		std::array<char, 0x4000> thread_data;
		thread_data.fill('x');
		std::array<void*, 32> slots{};
		slots[15] = thread_data.data();
		helper.index() = 15;
		game.index() = 99;
		std::memset(game.data(), 'g', 0x100);
		const auto baseline = game.directory();
		const auto helper_baseline = helper.directory();
		const auto check_rejected = [&](const char* message)
		{
			rejects([&] { tls::initialize_game_tls(game.view, helper.view, slots.data()); }, message);
			require(game.index() == 99 && thread_data[0] == 'x' &&
				std::memcmp(&helper.directory(), &helper_baseline, sizeof(helper_baseline)) == 0,
				"rejected TLS layout performed a partial write");
		};

		game.directory().AddressOfIndex = 0x151bf0bf8;
		check_rejected("out-of-image TLS index accepted");
		game.directory() = baseline;
		game.directory().EndAddressOfRawData = baseline.StartAddressOfRawData - 1;
		check_rejected("reversed TLS template accepted");
		game.directory() = baseline;
		game.directory().SizeOfZeroFill = 1;
		check_rejected("TLS template exceeding helper capacity accepted");
		game.directory() = baseline;
		game.directory().StartAddressOfRawData = (std::numeric_limits<std::uintptr_t>::max)() - 0x10;
		game.directory().EndAddressOfRawData = (std::numeric_limits<std::uintptr_t>::max)();
		check_rejected("overflowing template address accepted");
		game.directory() = baseline;
		game.view.directory.Size = 1;
		check_rejected("truncated TLS directory accepted");
		game.view.directory.Size = sizeof(IMAGE_TLS_DIRECTORY64);
		game.view.directory.VirtualAddress = 0x4fff;
		check_rejected("out-of-image TLS directory accepted");
		game.view.directory.VirtualAddress = 0x100;
		slots[15] = nullptr;
		check_rejected("missing thread TLS allocation accepted");
		slots[15] = thread_data.data();

		DWORD old_protection{};
		require(VirtualProtect(game.data(), 0x1000, PAGE_NOACCESS, &old_protection) != 0, "cannot protect TLS fixture");
		check_rejected("unreadable source TLS data accepted");
		require(VirtualProtect(game.data(), 0x1000, old_protection, &old_protection) != 0, "cannot restore TLS fixture");

		// A read-only section view is readable but Windows refuses to grant it
		// write access. Exercise a real VirtualProtect failure after the source
		// index's permission change has already succeeded.
		const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 0x5000, nullptr);
		require(mapping != nullptr, "cannot create read-only-view fixture");
		const auto read_view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
		const auto write_view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0);
		require(read_view && write_view, "cannot map TLS fixture views");
		const auto read_base = reinterpret_cast<std::uintptr_t>(read_view);
		auto read_directory = helper_baseline;
		read_directory.StartAddressOfRawData = read_base + 0x1000;
		read_directory.EndAddressOfRawData = read_base + 0x1100;
		read_directory.AddressOfIndex = read_base + 0x200;
		std::memcpy(static_cast<char*>(write_view) + 0x100, &read_directory, sizeof(read_directory));
		const DWORD read_index = 15;
		std::memcpy(static_cast<char*>(write_view) + 0x200, &read_index, sizeof(read_index));
		const tls::image_view read_helper{read_base, 0x5000, helper.view.directory};
		require(tls::has_template(read_helper), "read-only helper layout is invalid");
		require(VirtualProtect(game.memory, 0x1000, PAGE_READONLY, &old_protection) != 0, "cannot arrange permission rollback");
		rejects([&] { tls::initialize_game_tls(game.view, read_helper, slots.data()); }, "failed write permission silently accepted");
		MEMORY_BASIC_INFORMATION rolled_back{};
		require(VirtualQuery(game.memory, &rolled_back, sizeof(rolled_back)) && rolled_back.Protect == PAGE_READONLY &&
			game.index() == 99 && thread_data[0] == 'x', "failed permission acquisition did not roll back before writes");
		require(VirtualProtect(game.memory, 0x1000, PAGE_READWRITE, &old_protection) != 0, "cannot restore rollback fixture");
		UnmapViewOfFile(write_view);
		UnmapViewOfFile(read_view);
		CloseHandle(mapping);

		// Exercise a template spanning differently protected pages, with zero fill.
		game.directory().EndAddressOfRawData = game.directory().StartAddressOfRawData + 0x2100;
		game.directory().SizeOfZeroFill = 16;
		helper.directory().EndAddressOfRawData = helper.directory().StartAddressOfRawData + 0x3000;
		std::memset(game.data(), 'g', 0x2100);
		require(VirtualProtect(helper.data(), 0x1000, PAGE_READONLY, &old_protection) != 0, "cannot set first helper page");
		require(VirtualProtect(helper.data() + 0x1000, 0x1000, PAGE_EXECUTE_READ, &old_protection) != 0, "cannot set second helper page");
		require(VirtualProtect(game.memory, 0x1000, PAGE_READONLY, &old_protection) != 0, "cannot set source index page");
		require(VirtualProtect(helper.memory, 0x1000, PAGE_READONLY, &old_protection) != 0, "cannot set helper directory page");
		tls::initialize_game_tls(game.view, helper.view, slots.data());
		require(game.index() == 15, "TLS helper index not installed");
		require(thread_data[0] == 'g' && thread_data[0x20ff] == 'g' && thread_data[0x2100] == 0 &&
			thread_data[0x210f] == 0 && thread_data[0x2110] == 'x', "thread TLS template or zero fill is wrong");
		require(std::memcmp(thread_data.data(), helper.data(), 0x2110) == 0, "helper template differs from thread template");
		require(std::memcmp(&helper.directory(), &game.directory(), sizeof(baseline)) == 0, "TLS callback/template metadata changed");
		for (const auto& item : std::array<std::pair<void*, DWORD>, 4>{{
			{helper.data(), PAGE_READONLY}, {helper.data() + 0x1000, PAGE_EXECUTE_READ},
			{game.memory, PAGE_READONLY}, {helper.memory, PAGE_READONLY}}})
		{
			MEMORY_BASIC_INFORMATION region{};
			require(VirtualQuery(item.first, &region, sizeof(region)) && region.Protect == item.second,
				"TLS initialization did not restore page protection");
		}
		require(!tls::has_template({game.view.base, game.view.size, {}}), "empty TLS directory treated as a template");
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
		tls_checks();
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
