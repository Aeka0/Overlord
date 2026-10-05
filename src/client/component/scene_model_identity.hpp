#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace scene_models::identity
{
	struct pool
	{
		std::uintptr_t first{};
		std::size_t count{}, stride{};
		bool operator==(const pool&) const = default;
		bool valid() const noexcept
		{
			return first && count && count<=65536 && stride &&
				count<=(std::numeric_limits<std::uintptr_t>::max()-first)/stride;
		}
		std::optional<std::size_t> index(std::uintptr_t model) const noexcept
		{
			if (!valid() || model<first) return {};
			const auto offset=model-first;
			if (offset%stride || offset/stride>=count) return {};
			return offset/stride;
		}
		bool contains(std::uintptr_t address) const noexcept
		{
			return valid() && address>=first && address-first<count*stride;
		}
	};
	struct alias { std::uintptr_t descriptor{}, source{}; };
	// Single main-owner writer; append-only immutable batches, lock-free readers.
	// Source is the actual loaded asset whose vertex/material resources a subset
	// borrows. It is NOT an arbitrary valid index or a separately owned DB asset.
	// Keep descriptors/resources alive until all native queued work is retired.
	template<std::size_t Capacity> class registry
	{
		struct entry { alias model{}; pool native{}; };
		std::array<entry,Capacity> entries_{};
		std::atomic_size_t count_{};
	public:
		bool publish(pool native,std::span<const alias> batch) noexcept
		{
			const auto used=count_.load(std::memory_order_relaxed);
			if (!native.valid() || batch.empty() || batch.size()>Capacity-used) return false;
			for (std::size_t i=0;i<batch.size();++i)
			{
				const auto item=batch[i];
				if (!item.descriptor || native.contains(item.descriptor) || !native.index(item.source)) return false;
				for (std::size_t n=0;n<used;++n)
					if (entries_[n].model.descriptor==item.descriptor) return false;
				for (std::size_t n=0;n<i;++n) if (batch[n].descriptor==item.descriptor) return false;
			}
			for (std::size_t n=0;n<batch.size();++n) entries_[used+n]={batch[n],native};
			count_.store(used+batch.size(),std::memory_order_release);
			return true;
		}
		std::optional<std::uintptr_t> source(pool native,std::uintptr_t model) const noexcept
		{
			if (native.index(model)) return model; // Ordinary native assets: O(1).
			const auto used=count_.load(std::memory_order_acquire);
			for (std::size_t n=0;n<used;++n)
			{
				const auto& e=entries_[n];
				if (e.model.descriptor==model)
					return native==e.native && native.index(e.model.source) ?
						std::optional<std::uintptr_t>(e.model.source) : std::nullopt;
			}
			return {};
		}
		std::size_t size() const noexcept { return count_.load(std::memory_order_acquire); }
		// Native asset-unload barrier only: all readers and queued submissions
		// must be drained, and producers must retire their published descriptors.
		// Never call this for weapon switches, checkpoint time or cache pressure.
		void reset_after_drain() noexcept
		{
			count_.store(0,std::memory_order_release);
			entries_={};
		}
	};
}
