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

// All offsets are relative to the main executable module base. The only
// installable hook target is the primary MovieRoot GetVariable slot. Its
// initializer deliberately preserves the recovered `vtable base + 0x190`
// expression. Secondary subobject tables are documented in RE material, not
// exposed to hook consumers.
struct TargetProfile final {
    TargetKind kind;
    std::wstring_view executable_name;
    config::RuntimeVersion expected_runtime_version;
    std::uintptr_t primary_get_variable_slot_offset;
    std::uintptr_t get_member_offset;
    std::uintptr_t set_member_offset;
    std::uintptr_t release_value_offset;
};

[[nodiscard]] const TargetProfile* SelectTargetProfile(
    std::wstring_view executable_name) noexcept;

}
