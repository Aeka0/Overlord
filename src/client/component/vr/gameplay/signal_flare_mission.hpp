#pragma once
#include <cstdint>
#include <string>
namespace vr::gameplay::equipment::special::flare::mission
{
    bool active() noexcept;
    bool authorized() noexcept; // Server only; native waiter and story readiness.
    bool consumed() noexcept;
    bool ignite() noexcept;
    std::uint64_t generation() noexcept;
    std::string status();
}
