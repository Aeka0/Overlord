#include <std_include.hpp>
#include "native_fx_checkpoint.hpp"
#include "native_weapon_fx.hpp"
#include "native_followed_fx.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <mutex>
#include <sstream>

namespace vr::gameplay::native_fx::checkpoint
{
	namespace
	{
		constexpr std::uintptr_t collect_address = 0x140429470;
		constexpr std::uintptr_t prepare_address = 0x1404294B0;
		constexpr std::uintptr_t find_address = 0x140446C90;
		constexpr std::uintptr_t read_particles = 0x140429930;
		constexpr std::uintptr_t release_reference = 0x1404553B0, collect_garbage = 0x140457620;
		constexpr std::uintptr_t native_definitions = 0x14439C690, native_count = 0x1443A0690;
		utils::hook::detour prepare_hook;
		registry definitions;
		std::mutex mutex;
		bool ready{};
		std::atomic_uint64_t saved_variants{}, restored_variants{}, retired_tails{}, rejections{};

		void reject(const char* reason)
		{
			++rejections;
			game::Com_Error(game::ERR_DROP, "VR FX checkpoint rejected: %s", reason);
		}
		void prepare(void* archive)
		{
			// Refresh once at the save's stream-allocation boundary, before its
			// worker jobs read names/pointers. Variants can be created after level
			// entry, so the level's original dictionary alone is insufficient.
			utils::hook::invoke<void>(collect_address);
			bool appended{};
			{
				const std::lock_guard lock(mutex);
				auto& count = *reinterpret_cast<unsigned*>(native_count);
				auto size = static_cast<std::size_t>(count);
				appended = definitions.append({reinterpret_cast<game::FxEffectDef**>(native_definitions), native_capacity}, size);
				if (appended) { saved_variants += definitions.size(); count = static_cast<unsigned>(size); }
			}
			if (!appended) { reject("definition dictionary capacity"); return; }
			prepare_hook.invoke<void>(archive);
		}
		game::FxEffectDef* resolve(const char* name)
		{
			if (!name) return nullptr;
			const std::string_view value(name, strnlen_s(name, source_name_limit + 64));
			if (!value.starts_with(prefix)) return utils::hook::invoke<game::FxEffectDef*>(find_address, name);
			const auto id = parse(value);
			if (!id) { reject("invalid presentation identity"); return nullptr; }
			auto* source = utils::hook::invoke<game::FxEffectDef*>(find_address, id->source.data());
			auto* result = id->kind == variant::world ? weapons::native_weapon_fx::restore_definition(source) :
				native_followed_fx::restore_definition(source, id->attached);
			if (!result) reject("presentation definition unavailable");
			else ++restored_variants;
			return result;
		}
		template<class T> T read(const void* pointer, std::size_t offset)
		{
			T value{}; std::memcpy(&value, static_cast<const std::byte*>(pointer) + offset, sizeof(T)); return value;
		}
		void restore_particles(void* system, void* archive)
		{
			const auto first = read<unsigned>(system, 0xe8), end = read<unsigned>(system, 0xec);
			auto* handles = read<const unsigned*>(system, 0xf8);
			auto* pool = read<std::byte*>(system, 0x30);
			if (end - first > native_capacity || !handles || !pool)
			{
				reject("effect pool range"); return;
			}
			std::array<void*, native_capacity> orphans{};
			std::array<bool, native_capacity> seen{};
			std::size_t count{};
			for (auto sequence = first; sequence != end; ++sequence)
			{
				const auto handle = handles[sequence & (native_capacity - 1)];
				// Native archive buffer: +0x2000 effects, +0xa2000 trails.
				if (!valid_effect_handle(handle)) { reject("effect handle range"); return; }
				const auto slot=handle/(effect_size/handle_unit);
				if (seen[slot]) { reject("duplicate effect handle"); return; }
				seen[slot]=true;
				auto* effect = pool + std::size_t(handle) * 16;
				if (read<game::FxEffectDef*>(effect, 0)) continue;
				if (!empty_orphan_tail({effect, 0xe8}, handle))
				{
					reject("unmapped effect still owns particles or references"); return;
				}
				orphans[count++] = effect;
			}
			// Empty tails consume no particle payload. Keep the archive reader's
			// original order, then let native refcounts/free lists retire the tails.
			utils::hook::invoke<void>(read_particles, system, archive);
			for (std::size_t i = 0; i < count; ++i)
				utils::hook::invoke<void>(release_reference, system, orphans[i]);
			if (count) { utils::hook::invoke<void>(collect_garbage, system); retired_tails += count; }
		}
		template<std::size_t N> bool verify(std::uintptr_t address, const std::array<std::uint8_t, N>& bytes)
		{
			std::array<std::uint8_t, N> mask; mask.fill(255);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),
				{bytes.data(), mask.data(), N}));
		}
	}
	bool initialize()
	{
		if (ready) return true;
		// Captured H2 FX archive: 2048 name/old-pointer entries, four name
		// streams, remapping completed before the one particle-restore call.
		constexpr std::array<std::uintptr_t, 4> lookups{0x140429686, 0x1404296F5, 0x140429765, 0x1404297D5};
		if (!verify(collect_address, std::array<std::uint8_t, 13>{0x48,0x83,0xec,0x28,0x33,0xd2,0x48,0x8d,0x0d,0x13,0x32,0xf7,0x03}) ||
			!verify(prepare_address, std::array<std::uint8_t, 11>{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57}) ||
			!verify(0x14042947D, std::array<std::uint8_t, 6>{0x41,0xb8,0x08,0x40,0,0}) ||
			!verify(0x140429CA0, std::array<std::uint8_t, 25>{0x48,0x63,0x05,0xe9,0x69,0xf7,0x03,0x48,0x8d,0x15,0xe2,0x29,0xf7,0x03,0x48,0x89,0x0c,0xc2,0xff,0x05,0xd8,0x69,0xf7,0x03,0xc3}) ||
			!verify(find_address, std::array<std::uint8_t, 10>{0x80,0x39,0,0x75,3,0x33,0xc0,0xc3,0x41,0xb8}) ||
			!verify(0x1404298EC, std::array<std::uint8_t, 5>{0xe8,0x3f,0,0,0}) ||
			!verify(release_reference, std::array<std::uint8_t, 11>{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57}) ||
			!verify(0x1404553DC, std::array<std::uint8_t, 13>{0x83,0x8f,0x58,1,0,0,8,0x0f,0xba,0xe0,0x1a,0x72,0x18}) ||
			!verify(0x140457644, std::array<std::uint8_t, 7>{0x83,0xa1,0x58,1,0,0,0xf7})) return false;
		for (const auto address : lookups)
		{
			const auto displacement = static_cast<std::int32_t>(find_address - address - 5);
			std::array<std::uint8_t, 5> bytes{0xe8}; std::memcpy(bytes.data() + 1, &displacement, 4);
			if (!verify(address, bytes)) return false;
			if (utils::hook::is_relatively_far(reinterpret_cast<void*>(address),reinterpret_cast<void*>(resolve))) return false;
		}
		if (utils::hook::is_relatively_far(reinterpret_cast<void*>(0x1404298EC),reinterpret_cast<void*>(restore_particles))) return false;
		try { prepare_hook.create(prepare_address, prepare); }
		catch (...) { return false; }
		for (const auto address : lookups) utils::hook::call(address, resolve);
		utils::hook::call(0x1404298EC, restore_particles);
		ready = true;
		return true;
	}
	bool publish(std::span<game::FxEffectDef* const> values)
	{
		const std::lock_guard lock(mutex);
		return ready && definitions.publish(values);
	}
	void remove(variant kind)
	{
		const std::lock_guard lock(mutex); definitions.remove(kind);
	}
	std::string status()
	{
		const std::lock_guard lock(mutex); std::ostringstream out;
		out << "fx_checkpoint_ready=" << ready << " registered_variants=" << definitions.size()
			<< " saved_variants=" << saved_variants.load() << " restored_variants=" << restored_variants.load()
			<< " retired_empty_tails=" << retired_tails.load() << " rejections=" << rejections.load() << '\n';
		return out.str();
	}
}
