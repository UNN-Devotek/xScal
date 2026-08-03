#pragma once

#include "scaleform/scaleform_layout.hpp"

#include <cstddef>

namespace sf {

// Recovered Scaleform values are passed by address and treated as opaque data.
// Their size and alignment are part of the target's x64 ABI.
struct alignas(16) ScaleformValue final {
    std::byte storage[layout::kValueSize]{};
};

// MovieRoot::GetVariable at vtable offset 0x190 in the supported builds.
using MovieRootGetVariable = bool(__fastcall*)(
    void* movie_root,
    ScaleformValue* out_value,
    const char* path,
    unsigned int caller_r9_scratch);

static_assert(sizeof(ScaleformValue) == layout::kValueSize);
static_assert(alignof(ScaleformValue) == 16);

}
