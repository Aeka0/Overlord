#pragma once
#include <utils/native_memory.hpp>
#include <cstdint>
#include <optional>
#include <span>

namespace game {struct XModel;}
namespace scene_models::native_entry
{
	template<class T> inline bool model(const void* entry,T*& out) noexcept
	{return utils::native_memory::read_at(entry,8,out);}
	inline bool model(const void* entry,std::uintptr_t& out) noexcept
	{return utils::native_memory::read_at(entry,8,out);}
	template<class T> inline bool lighting(const void* entry,T& out) noexcept
	{static_assert(sizeof(T)==sizeof(std::uintptr_t));return utils::native_memory::read_at(entry,0x68,out);}
	inline std::optional<size_t> lighting_index(const void* entry,std::span<const unsigned short> handles) noexcept
	{
		std::uintptr_t handle{};const auto begin=reinterpret_cast<std::uintptr_t>(handles.data());
		if(!lighting(entry,handle) || handle<begin || handle-begin>=handles.size_bytes() ||
			(handle-begin)%sizeof(unsigned short))return {};
		return (handle-begin)/sizeof(unsigned short);
	}
}
