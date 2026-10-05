#pragma once
#include "game/game.hpp"
#include <utils/native_memory.hpp>
#include <array>
#include <algorithm>
#include <string_view>

namespace vr::gameplay::weapons::native_weapon_read
{
    // The token encoding is a search ceiling, not evidence that every slot is a
    // registered definition. Missing/transitioning entries must never be dereferenced.
    inline constexpr unsigned token_limit=512;
    struct entry
    {
        const game::WeaponDef* definition{};
        std::array<char,128> name{};
        explicit operator bool() const noexcept {return definition && name[0];}
    };
    inline entry get(unsigned token) noexcept
    {
        entry out;const char* name{};
        if(!token || token>=token_limit ||
            !utils::native_memory::read_at(&game::weapon_defs[0],token*sizeof(void*),out.definition) || !out.definition ||
            !utils::native_memory::read_at(out.definition,0,name) || !name ||
            !utils::native_memory::read_bytes(out.name.data(),name,out.name.size()))return {};
        const auto end=std::find(out.name.begin(),out.name.end(),'\0');
        if(end==out.name.begin() || end==out.name.end())return {};
        for(auto p=out.name.begin();p!=end;++p)
            if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='_' || *p=='-'))return {};
        return out;
    }
    inline unsigned find(std::string_view name) noexcept
    {
        for(unsigned i=1;i<token_limit;++i)
            if(const auto value=get(i);value && std::string_view(value.name.data())==name)return i;
        return 0;
    }
}
