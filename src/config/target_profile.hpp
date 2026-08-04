#pragma once

#include "config/config.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sf {

enum class TargetKind {
    Steam,
    GamePass,
};

[[nodiscard]] constexpr const char* TargetKindName(TargetKind kind) noexcept {
    switch (kind) {
        case TargetKind::Steam: return "steam";
        case TargetKind::GamePass: return "gamepass";
        default: return "unknown";
    }
}

// All offsets are relative to the main executable module base. The only
// installable hook target is the primary MovieRoot GetVariable slot. Its
// initializer deliberately preserves the recovered `vtable base + 0x190`
// expression. Secondary subobject tables are documented in RE material, not
// exposed to hook consumers.
struct TargetProfile final {
    TargetKind kind;
    std::wstring_view executableName;
    config::RuntimeVersion expectedRuntimeVersion;
    std::uintptr_t primaryGetVariableSlotOffset;
    std::uintptr_t getMemberOffset;
    std::uintptr_t setMemberOffset;
    std::uintptr_t releaseValueOffset;
};

[[nodiscard]] const TargetProfile* SelectTargetProfile(
    std::wstring_view executableName) noexcept;

}
