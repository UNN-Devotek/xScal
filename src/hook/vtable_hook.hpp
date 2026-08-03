#pragma once

#include "scaleform/abi.hpp"
#include "config/target_profile.hpp"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace sf {

// All platform calls are explicit so lifecycle tests can exercise the patcher
// against a fake address space. Production uses SystemVtableHookPlatform().
struct VtableHookPlatform final {
    using VirtualQueryFn = SIZE_T(WINAPI*)(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T);
    using VirtualProtectFn = BOOL(WINAPI*)(LPVOID, SIZE_T, DWORD, PDWORD);
    using ExchangePointerFn = PVOID(WINAPI*)(PVOID volatile*, PVOID);
    using CompareExchangePointerFn = PVOID(WINAPI*)(PVOID volatile*, PVOID, PVOID);
    using FlushInstructionCacheFn = BOOL(WINAPI*)(HANDLE, LPCVOID, SIZE_T);
    using GetCurrentProcessFn = HANDLE(WINAPI*)();

    VirtualQueryFn virtual_query{};
    VirtualProtectFn virtual_protect{};
    ExchangePointerFn exchange_pointer{};
    CompareExchangePointerFn compare_exchange_pointer{};
    FlushInstructionCacheFn flush_instruction_cache{};
    GetCurrentProcessFn get_current_process{};
};

[[nodiscard]] VtableHookPlatform SystemVtableHookPlatform() noexcept;

enum class VtableHookStatus {
    Installed,
    Restored,
    AlreadyActive,
    NotActive,
    InvalidArgument,
    InvalidPlatform,
    RuntimeVersionUnavailable,
    RuntimeVersionMismatch,
    AddressOverflow,
    SlotUnaligned,
    SlotNotReadable,
    OriginalNotExecutable,
    TargetRoutineNotExecutable,
    ProtectFailed,
    FlushFailed,
    ProtectionRestoreFailed,
    ReplacedByAnotherWriter,
    AllocationFailed,
};

class VtableHook final {
public:
    explicit VtableHook(VtableHookPlatform platform = SystemVtableHookPlatform()) noexcept;

    VtableHook(const VtableHook&) = delete;
    VtableHook& operator=(const VtableHook&) = delete;

    [[nodiscard]] VtableHookStatus Install(
        const TargetProfile& profile,
        std::uintptr_t module_base,
        MovieRootGetVariable hook) noexcept;
    [[nodiscard]] VtableHookStatus Restore() noexcept;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] void** SlotAddress() const noexcept;
    [[nodiscard]] MovieRootGetVariable Original() const noexcept;

private:
    enum class State {
        Inactive,
        Active,
        ProtectionRecovery,
    };

    [[nodiscard]] bool HasCompletePlatform() const noexcept;
    [[nodiscard]] bool IsReadableSlot(void* slot) const noexcept;
    [[nodiscard]] bool IsExecutableTarget(void* target) const noexcept;
    [[nodiscard]] bool RestoreProtection(void* slot, DWORD old_protection) const noexcept;
    void BeginProtectionRecovery(
        void** slot, DWORD old_protection, VtableHookStatus terminal_result) noexcept;
    void Clear() noexcept;

    VtableHookPlatform platform_;
    mutable std::mutex mutex_;
    State state_{State::Inactive};
    void** slot_{};
    MovieRootGetVariable original_{};
    void* hook_{};
    DWORD recovery_old_protection_{};
    VtableHookStatus recovery_result_{VtableHookStatus::NotActive};
};

}
