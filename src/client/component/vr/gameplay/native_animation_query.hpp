#pragma once
#include <array>
#include <span>
#include <string_view>

namespace vr::gameplay::native_animation
{
    struct clip {std::string_view name;float time{},weight{};};
    struct snapshot
    {
        std::array<clip,128> clips{};
        std::size_t count{};
        bool valid{},time_valid{};
        std::span<const clip> leaves() const noexcept {return {clips.data(),count};}
    };
    bool initialize();
    // The caller holds this DObj's native lock. Names are borrowed only for
    // this invocation, never retained across zone lifetimes or publications.
    snapshot sample(const void* tree) noexcept;
}
