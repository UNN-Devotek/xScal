#include "platform/windows_memory.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace sf::platform {

bool IsReadableProtection(DWORD protection) noexcept {
    if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    switch (protection & 0xFFU) {
    case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY: return true;
    default: return false;
    }
}

bool IsExecutableProtection(DWORD protection) noexcept {
    if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    switch (protection & 0xFFU) {
    case PAGE_EXECUTE: case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY: return true;
    default: return false;
    }
}

bool IsExecutableAddress(VirtualQueryFn virtual_query, const void* address) noexcept {
    if (virtual_query == nullptr || address == nullptr) return false;
    MEMORY_BASIC_INFORMATION region{};
    return virtual_query(address, &region, sizeof(region)) != 0 &&
        region.State == MEM_COMMIT && IsExecutableProtection(region.Protect) &&
        RegionContains(region, address, 1);
}

bool RegionContains(const MEMORY_BASIC_INFORMATION& region, const void* address, std::size_t size) noexcept {
    if (address == nullptr || size == 0 || region.RegionSize < size) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return value >= base && value - base <= region.RegionSize - size;
}

bool IsReadableRange(VirtualQueryFn virtual_query, const void* address, std::size_t size) noexcept {
    if (virtual_query == nullptr || address == nullptr || size == 0) return false;
    auto current = reinterpret_cast<std::uintptr_t>(address);
    if (size > (std::numeric_limits<std::uintptr_t>::max)() - current) return false;
    const auto end = current + size;
    while (current < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (virtual_query(reinterpret_cast<const void*>(current), &region, sizeof(region)) == 0 ||
            region.State != MEM_COMMIT || !IsReadableProtection(region.Protect) ||
            !RegionContains(region, reinterpret_cast<const void*>(current), 1)) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - base) return false;
        const auto region_end = base + region.RegionSize;
        if (region_end <= current) return false;
        current = std::min(region_end, end);
    }
    return true;
}

bool GetReadableSpanEnd(VirtualQueryFn virtual_query, const char* current, std::size_t remaining, const char*& span_end) noexcept {
    span_end = nullptr;
    if (virtual_query == nullptr || current == nullptr || remaining == 0) return false;
    MEMORY_BASIC_INFORMATION region{};
    if (virtual_query(current, &region, sizeof(region)) == 0 || region.State != MEM_COMMIT ||
        !IsReadableProtection(region.Protect) || !RegionContains(region, current, 1)) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(current);
    const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    if (region.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - base ||
        remaining > (std::numeric_limits<std::uintptr_t>::max)() - start) return false;
    const auto region_end = base + region.RegionSize;
    const auto requested_end = start + remaining;
    span_end = reinterpret_cast<const char*>(std::min(region_end, requested_end));
    return span_end > current;
}

}