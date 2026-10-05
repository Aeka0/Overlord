#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace vr::gameplay
{
    // Registration tables use one runtime canonical spelling; function lookup
    // also exposes token aliases. Resolve by address, not by comparing names.
    template<class Entries>
    std::optional<std::pair<std::uintptr_t,std::uintptr_t>> script_function_extent(
        const Entries& entries,std::uintptr_t entry) noexcept
    {
        if(!entry)return {};
        bool found=false;auto end=std::numeric_limits<std::uintptr_t>::max();
        for(const auto& item:entries)
        {
            const auto address=reinterpret_cast<std::uintptr_t>(item.second);
            found|=address==entry;
            if(address>entry && address<end)end=address;
        }
        if(!found || end==std::numeric_limits<std::uintptr_t>::max() || end-entry>65536)return {};
        return std::pair{entry,end};
    }
}
