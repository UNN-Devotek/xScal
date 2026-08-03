#include "hook/vtable_hook.hpp"

#include "platform/windows_memory.hpp"

#include <limits>

namespace sf {
namespace {

HANDLE WINAPI CurrentProcess() noexcept {
    return ::GetCurrentProcess();
}

PVOID WINAPI ExchangePointer(PVOID volatile* target, PVOID value) noexcept {
    return ::InterlockedExchangePointer(target, value);
}

PVOID WINAPI CompareExchangePointer(
    PVOID volatile* target, PVOID exchange, PVOID comparand) noexcept {
    return ::InterlockedCompareExchangePointer(target, exchange, comparand);
}

}

VtableHookPlatform SystemVtableHookPlatform() noexcept {
    return {
        &::VirtualQuery,
        &::VirtualProtect,
        &ExchangePointer,
        &CompareExchangePointer,
        &::FlushInstructionCache,
        &CurrentProcess,
    };
}

VtableHook::VtableHook(VtableHookPlatform platform) noexcept : platform_(platform) {}

bool VtableHook::HasCompletePlatform() const noexcept {
    return platform_.virtual_query != nullptr
        && platform_.virtual_protect != nullptr
        && platform_.exchange_pointer != nullptr
        && platform_.compare_exchange_pointer != nullptr
        && platform_.flush_instruction_cache != nullptr
        && platform_.get_current_process != nullptr;
}

bool VtableHook::IsReadableSlot(void* slot) const noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    return platform_.virtual_query(slot, &memory, sizeof(memory)) != 0
        && memory.State == MEM_COMMIT
        && platform::RegionContains(memory, slot, sizeof(void*))
        && platform::IsReadableProtection(memory.Protect);
}

bool VtableHook::IsExecutableTarget(void* target) const noexcept {
    if (target == nullptr) {
        return false;
    }

    MEMORY_BASIC_INFORMATION memory{};
    return platform_.virtual_query(target, &memory, sizeof(memory)) != 0
        && memory.State == MEM_COMMIT
        && platform::IsExecutableProtection(memory.Protect);
}

bool VtableHook::RestoreProtection(void* slot, DWORD old_protection) const noexcept {
    DWORD ignored{};
    return platform_.virtual_protect(slot, sizeof(void*), old_protection, &ignored) != FALSE;
}

void VtableHook::BeginProtectionRecovery(
    void** slot, DWORD old_protection, VtableHookStatus terminal_result) noexcept {
    state_ = State::ProtectionRecovery;
    slot_ = slot;
    original_ = nullptr;
    hook_ = nullptr;
    recovery_old_protection_ = old_protection;
    recovery_result_ = terminal_result;
}

void VtableHook::Clear() noexcept {
    state_ = State::Inactive;
    slot_ = nullptr;
    original_ = nullptr;
    hook_ = nullptr;
    recovery_old_protection_ = 0;
    recovery_result_ = VtableHookStatus::NotActive;
}

VtableHookStatus VtableHook::Install(
    const TargetProfile& profile,
    std::uintptr_t module_base,
    MovieRootGetVariable hook) noexcept {
    std::scoped_lock lock{mutex_};
    if (state_ != State::Inactive) {
        return VtableHookStatus::AlreadyActive;
    }
    if (!HasCompletePlatform() || hook == nullptr) {
        return HasCompletePlatform() ? VtableHookStatus::InvalidArgument
                                     : VtableHookStatus::InvalidPlatform;
    }
    if (profile.primary_get_variable_slot_offset
        > (std::numeric_limits<std::uintptr_t>::max)() - module_base) {
        return VtableHookStatus::AddressOverflow;
    }

    const auto slot_value = module_base + profile.primary_get_variable_slot_offset;
    if ((slot_value % alignof(void*)) != 0) {
        return VtableHookStatus::SlotUnaligned;
    }
    auto* slot = reinterpret_cast<void**>(slot_value);
    if (!IsReadableSlot(slot)) {
        return VtableHookStatus::SlotNotReadable;
    }

    const auto raw_hook = reinterpret_cast<void*>(hook);
    void* observed_original = *reinterpret_cast<void* volatile*>(slot);
    if (observed_original == raw_hook) {
        return VtableHookStatus::AlreadyActive;
    }
    if (!IsExecutableTarget(observed_original)) {
        return VtableHookStatus::OriginalNotExecutable;
    }

    DWORD old_protection{};
    if (platform_.virtual_protect(slot, sizeof(void*), PAGE_READWRITE, &old_protection) == FALSE) {
        return VtableHookStatus::ProtectFailed;
    }

    observed_original = platform_.exchange_pointer(
        reinterpret_cast<PVOID volatile*>(slot), raw_hook);
    if (observed_original == raw_hook) {
        if (!RestoreProtection(slot, old_protection)) {
            BeginProtectionRecovery(slot, old_protection, VtableHookStatus::AlreadyActive);
            return VtableHookStatus::ProtectionRestoreFailed;
        }
        return VtableHookStatus::AlreadyActive;
    }
    if (!IsExecutableTarget(observed_original)) {
        platform_.compare_exchange_pointer(
            reinterpret_cast<PVOID volatile*>(slot), observed_original, raw_hook);
        if (!RestoreProtection(slot, old_protection)) {
            BeginProtectionRecovery(
                slot, old_protection, VtableHookStatus::OriginalNotExecutable);
            return VtableHookStatus::ProtectionRestoreFailed;
        }
        return VtableHookStatus::OriginalNotExecutable;
    }

    if (platform_.flush_instruction_cache(
            platform_.get_current_process(), slot, sizeof(void*)) == FALSE) {
        platform_.compare_exchange_pointer(
            reinterpret_cast<PVOID volatile*>(slot), observed_original, raw_hook);
        if (!RestoreProtection(slot, old_protection)) {
            BeginProtectionRecovery(slot, old_protection, VtableHookStatus::FlushFailed);
            return VtableHookStatus::ProtectionRestoreFailed;
        }
        return VtableHookStatus::FlushFailed;
    }

    if (!RestoreProtection(slot, old_protection)) {
        platform_.compare_exchange_pointer(
            reinterpret_cast<PVOID volatile*>(slot), observed_original, raw_hook);
        (void)platform_.flush_instruction_cache(
            platform_.get_current_process(), slot, sizeof(void*));
        BeginProtectionRecovery(
            slot, old_protection, VtableHookStatus::ProtectionRestoreFailed);
        return VtableHookStatus::ProtectionRestoreFailed;
    }

    state_ = State::Active;
    slot_ = slot;
    original_ = reinterpret_cast<MovieRootGetVariable>(observed_original);
    hook_ = raw_hook;
    return VtableHookStatus::Installed;
}

VtableHookStatus VtableHook::Restore() noexcept {
    std::scoped_lock lock{mutex_};
    if (state_ == State::Inactive) {
        return VtableHookStatus::NotActive;
    }
    if (!HasCompletePlatform()) {
        return VtableHookStatus::InvalidPlatform;
    }
    if (state_ == State::ProtectionRecovery) {
        if (!RestoreProtection(slot_, recovery_old_protection_)) {
            return VtableHookStatus::ProtectionRestoreFailed;
        }
        const auto terminal_result = recovery_result_;
        Clear();
        return terminal_result;
    }
    if (!IsReadableSlot(slot_)) {
        return VtableHookStatus::SlotNotReadable;
    }

    DWORD old_protection{};
    if (platform_.virtual_protect(slot_, sizeof(void*), PAGE_READWRITE, &old_protection) == FALSE) {
        return VtableHookStatus::ProtectFailed;
    }

    const auto observed = platform_.compare_exchange_pointer(
        reinterpret_cast<PVOID volatile*>(slot_), reinterpret_cast<void*>(original_), hook_);
    const bool changed_by_another_writer = observed != hook_;
    const bool flushed = changed_by_another_writer || platform_.flush_instruction_cache(
        platform_.get_current_process(), slot_, sizeof(void*)) != FALSE;
    const auto terminal_result = changed_by_another_writer
        ? VtableHookStatus::ReplacedByAnotherWriter
        : (flushed ? VtableHookStatus::Restored : VtableHookStatus::FlushFailed);
    if (!RestoreProtection(slot_, old_protection)) {
        BeginProtectionRecovery(slot_, old_protection, terminal_result);
        return VtableHookStatus::ProtectionRestoreFailed;
    }

    Clear();
    return terminal_result;
}

bool VtableHook::IsActive() const noexcept {
    std::scoped_lock lock{mutex_};
    return state_ == State::Active;
}

void** VtableHook::SlotAddress() const noexcept {
    std::scoped_lock lock{mutex_};
    return state_ == State::Active ? slot_ : nullptr;
}

MovieRootGetVariable VtableHook::Original() const noexcept {
    std::scoped_lock lock{mutex_};
    return state_ == State::Active ? original_ : nullptr;
}

}
