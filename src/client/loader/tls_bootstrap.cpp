#include "tls_bootstrap.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace tls
{
	namespace
	{
		bool contains(const image_view& image, const std::uintptr_t address, const std::size_t count)
		{
			return image.base && image.size <= (std::numeric_limits<std::uintptr_t>::max)() - image.base &&
				address >= image.base && address - image.base <= image.size && count <= image.size - (address - image.base);
		}

		bool accessible(const std::uintptr_t address, const std::size_t count, const bool write = false)
		{
			if (count > (std::numeric_limits<std::uintptr_t>::max)() - address) return false;
			for (auto cursor = address; cursor < address + count;)
			{
				MEMORY_BASIC_INFORMATION region{};
				if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) ||
					region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
				const auto protection = region.Protect & 0xff;
				const bool writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
					protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
				if (write ? !writable : !(writable || protection == PAGE_READONLY || protection == PAGE_EXECUTE_READ)) return false;
				const auto available = region.RegionSize - (cursor - reinterpret_cast<std::uintptr_t>(region.BaseAddress));
				if (!available) return false;
				cursor += (std::min)(available, address + count - cursor);
			}
			return true;
		}

		void require_range(const image_view& image, const std::uintptr_t address, const std::size_t count)
		{
			if (!contains(image, address, count) || !accessible(address, count))
				throw std::runtime_error("Invalid TLS address range in the loaded executable");
		}

		struct layout
		{
			IMAGE_TLS_DIRECTORY64 directory{};
			std::uintptr_t address{};
			std::size_t initialized_size{};
			std::size_t total_size{};
		};

		layout read_layout(const image_view& image)
		{
			if (!image.directory.VirtualAddress || image.directory.Size < sizeof(IMAGE_TLS_DIRECTORY64) ||
				image.directory.VirtualAddress > image.size || image.directory.Size > image.size - image.directory.VirtualAddress ||
				image.base > (std::numeric_limits<std::uintptr_t>::max)() - image.directory.VirtualAddress)
				throw std::runtime_error("Invalid TLS directory in the loaded executable");
			layout result;
			result.address = image.base + image.directory.VirtualAddress;
			require_range(image, result.address, sizeof(result.directory));
			std::memcpy(&result.directory, reinterpret_cast<const void*>(result.address), sizeof(result.directory));
			const auto& directory = result.directory;
			if (directory.EndAddressOfRawData < directory.StartAddressOfRawData)
				throw std::runtime_error("Invalid TLS template size in the loaded executable");
			result.initialized_size = directory.EndAddressOfRawData - directory.StartAddressOfRawData;
			if (result.initialized_size > (std::numeric_limits<std::size_t>::max)() - directory.SizeOfZeroFill)
				throw std::runtime_error("TLS template size overflows");
			result.total_size = result.initialized_size + directory.SizeOfZeroFill;
			if (result.initialized_size) require_range(image, directory.StartAddressOfRawData, result.initialized_size);
			require_range(image, directory.AddressOfIndex, sizeof(DWORD));
			if (directory.AddressOfIndex % alignof(DWORD)) throw std::runtime_error("Unaligned TLS index address");
			if (directory.AddressOfCallBacks) require_range(image, directory.AddressOfCallBacks, sizeof(std::uintptr_t));
			return result;
		}

		// Preserve each region separately: a TLS template may span pages with
		// different protections. All permission changes finish before any write.
		class write_access final
		{
		public:
			write_access(const std::uintptr_t address, const std::size_t count)
			{
				try
				{
					for (auto cursor = address; cursor < address + count;)
					{
						MEMORY_BASIC_INFORMATION info{};
						if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)))
							throw std::runtime_error("Could not query TLS memory protection");
						const auto available = info.RegionSize - (cursor - reinterpret_cast<std::uintptr_t>(info.BaseAddress));
						if (!available) throw std::runtime_error("Invalid TLS memory region");
						const auto size = (std::min)(available, address + count - cursor);
						regions_.push_back({reinterpret_cast<void*>(cursor), size, 0});
						if (!VirtualProtect(regions_.back().address, size, PAGE_READWRITE, &regions_.back().protection))
						{
							regions_.pop_back();
							throw std::runtime_error("Could not make TLS memory writable");
						}
						cursor += size;
					}
				}
				catch (...) { restore(); throw; }
			}

			~write_access() { restore(); }
			bool restore() noexcept
			{
				bool complete = true;
				for (auto it = regions_.rbegin(); it != regions_.rend(); ++it)
				{
					if (!it->protection) continue;
					DWORD ignored{};
					if (VirtualProtect(it->address, it->size, it->protection, &ignored)) it->protection = 0;
					else complete = false;
				}
				return complete;
			}

		private:
			struct region { void* address; std::size_t size; DWORD protection; };
			std::vector<region> regions_;
		};
	}

	bool has_template(const image_view& image)
	{
		if (!image.directory.VirtualAddress && !image.directory.Size) return false;
		(void)read_layout(image);
		return true;
	}

	void initialize_game_tls(const image_view& game, const image_view& helper, void* thread_slots)
	{
		const auto source = read_layout(game);
		const auto destination = read_layout(helper);
		if (source.total_size > destination.total_size)
			throw std::runtime_error("Game TLS template exceeds the embedded helper's allocation");
		if (source.total_size) require_range(helper, destination.directory.StartAddressOfRawData, source.total_size);

		DWORD index{};
		std::memcpy(&index, reinterpret_cast<const void*>(destination.directory.AddressOfIndex), sizeof(index));
		const auto slots = reinterpret_cast<std::uintptr_t>(thread_slots);
		const auto offset = static_cast<std::uintptr_t>(index) * sizeof(void*);
		if (!slots || offset > (std::numeric_limits<std::uintptr_t>::max)() - slots ||
			!accessible(slots + offset, sizeof(void*)))
			throw std::runtime_error("Invalid TLS helper slot");
		void* thread_data{};
		std::memcpy(&thread_data, reinterpret_cast<const void*>(slots + offset), sizeof(thread_data));
		if (!thread_data || !accessible(reinterpret_cast<std::uintptr_t>(thread_data), source.total_size, true))
			throw std::runtime_error("Invalid current-thread TLS allocation");

		write_access index_access(source.directory.AddressOfIndex, sizeof(index));
		write_access template_access(destination.directory.StartAddressOfRawData, source.total_size);
		write_access directory_access(destination.address, sizeof(source.directory));
		std::memcpy(reinterpret_cast<void*>(source.directory.AddressOfIndex), &index, sizeof(index));
		if (source.initialized_size)
		{
			std::memmove(thread_data, reinterpret_cast<const void*>(source.directory.StartAddressOfRawData), source.initialized_size);
			std::memmove(reinterpret_cast<void*>(destination.directory.StartAddressOfRawData),
				reinterpret_cast<const void*>(source.directory.StartAddressOfRawData), source.initialized_size);
		}
		if (source.directory.SizeOfZeroFill)
		{
			std::memset(static_cast<char*>(thread_data) + source.initialized_size, 0, source.directory.SizeOfZeroFill);
			std::memset(reinterpret_cast<void*>(destination.directory.StartAddressOfRawData + source.initialized_size),
				0, source.directory.SizeOfZeroFill);
		}
		std::memcpy(reinterpret_cast<void*>(destination.address), &source.directory, sizeof(source.directory));
		const bool directory_restored = directory_access.restore();
		const bool template_restored = template_access.restore();
		const bool index_restored = index_access.restore();
		if (!directory_restored || !template_restored || !index_restored)
			throw std::runtime_error("Could not restore TLS memory protection");
	}
}
