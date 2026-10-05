#pragma once
#include "scene_model_record.hpp"
#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <type_traits>

namespace scene_models
{
	// Immutable queued submissions and stable native lighting storage. The age
	// limit is CPU freshness admission; the reuse guard preserves the existing
	// bounded queue policy. Neither is proof of a GPU or native asset drain.
	// clear_after_drain must run only at the drained native asset barrier.
	template<class Payload,size_t Capacity,size_t Parts=1>
	class submission_pool
	{
		static_assert(Capacity>0 && Parts>0);
		static_assert(std::is_trivially_copyable_v<Payload>);
	public:
		using clock=std::chrono::steady_clock;
		inline static constexpr auto max_age=std::chrono::milliseconds(150);
		inline static constexpr auto reuse_guard=std::chrono::milliseconds(150);
		struct lease {Payload payload{};clock::time_point at{};size_t part{};bool live{};};
		std::optional<size_t> acquire(const Payload& payload,clock::time_point now) noexcept
		{
			const std::lock_guard lock(mutex_);const auto index=cursor_++%Capacity;
			if(records_[index].live && now>=records_[index].at && now-records_[index].at<=reuse_guard)return {};
			records_[index]={payload,now,0,true};return index;
		}
		unsigned short* handle(size_t index,size_t part=0) noexcept
		{return index<Capacity && part<Parts?&lighting_[index*Parts+part]:nullptr;}
		std::optional<lease> lookup(const void* entry) noexcept
		{
			const auto index=native_entry::lighting_index(entry,lighting_);
			if(!index)return {};
			const std::lock_guard lock(mutex_);auto value=records_[*index/Parts];value.part=*index%Parts;return value;
		}
		static bool fresh(const lease& value,clock::time_point now) noexcept
		{return value.live && now>=value.at && now-value.at<=max_age;}
		void clear_after_drain() noexcept
		{const std::lock_guard lock(mutex_);records_={};cursor_=0;}
	private:
		std::mutex mutex_;
		std::array<lease,Capacity> records_{};
		std::array<unsigned short,Capacity*Parts> lighting_{};
		size_t cursor_{};
	};
}
