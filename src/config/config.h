#pragma once

#include <cstdint>
#include <string_view>

namespace sf::config {

inline constexpr std::string_view kXScalVersion{"0.1.1"};

struct RuntimeVersion final {
    std::uint16_t major;
    std::uint16_t minor;
    std::uint16_t build;
    std::uint16_t revision;

    [[nodiscard]] constexpr bool operator==(const RuntimeVersion&) const noexcept = default;
};

struct ExecutableConfig final {
    RuntimeVersion runtimeVersion;
    std::uintptr_t primaryGetVariableSlot;
    std::uintptr_t getMember;
    std::uintptr_t setMember;
    std::uintptr_t releaseValue;
};

}

#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif
