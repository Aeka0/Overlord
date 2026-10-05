#pragma once
#include <atomic>
#include <cstdint>
#include "pose_filter.hpp"

namespace vr::stabilization
{
	struct control {bool enabled{};float strength{};float amount() const noexcept {return enabled?strength:0.f;}};
	struct configuration {control desktop{false,50},head{false,30},hand{false,40};};
	using provider=configuration(*)() noexcept;
	inline std::atomic<provider> settings_provider{};
	inline std::atomic_bool gameplay{false};
	inline std::atomic_uint64_t epoch{1};
	inline configuration settings() noexcept {const auto p=settings_provider.load();return p?p():configuration{};}
	inline void set_gameplay(bool active) noexcept {if(gameplay.exchange(active)!=active)++epoch;}
	inline void invalidate() noexcept {++epoch;}
}
