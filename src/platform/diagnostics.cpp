#include "platform/diagnostics.hpp"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace sf {
namespace {

HMODULE diagnostic_module = nullptr;
SRWLOCK diagnostic_lock = SRWLOCK_INIT;
ULONGLONG diagnostic_baseline_tick{};
ULONGLONG diagnostic_baseline_elapsed_ms{};

bool BuildLogPath(std::array<wchar_t, 32768>& path) noexcept {
    if (diagnostic_module == nullptr) {
        return false;
    }
    const DWORD length = ::GetModuleFileNameW(
        diagnostic_module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return false;
    }
    std::size_t separator = length;
    while (separator != 0 && path[separator - 1] != L'\\' && path[separator - 1] != L'/') {
        --separator;
    }
    constexpr wchar_t filename[] = L"xscal.log";
    constexpr std::size_t filename_length = (sizeof(filename) / sizeof(filename[0])) - 1;
    if (separator + filename_length >= path.size()) {
        return false;
    }
    std::memcpy(path.data() + separator, filename, sizeof(filename));
    return true;
}

}

void SetDiagnosticModule(HMODULE module) noexcept {
    diagnostic_module = module;
    diagnostic_baseline_tick = ::GetTickCount64();
    diagnostic_baseline_elapsed_ms = 0;

    FILETIME creation_time{};
    FILETIME exit_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};
    FILETIME current_time{};
    if (::GetProcessTimes(
            ::GetCurrentProcess(),
            &creation_time,
            &exit_time,
            &kernel_time,
            &user_time) != FALSE) {
        ::GetSystemTimeAsFileTime(&current_time);
        ULARGE_INTEGER creation{};
        creation.LowPart = creation_time.dwLowDateTime;
        creation.HighPart = creation_time.dwHighDateTime;
        ULARGE_INTEGER current{};
        current.LowPart = current_time.dwLowDateTime;
        current.HighPart = current_time.dwHighDateTime;
        if (current.QuadPart >= creation.QuadPart) {
            diagnostic_baseline_elapsed_ms =
                (current.QuadPart - creation.QuadPart) / 10000ULL;
        }
    }
}

void ClearDiagnosticLog() noexcept {
    std::array<wchar_t, 32768> path{};
    if (!BuildLogPath(path)) {
        return;
    }

    if (::TryAcquireSRWLockExclusive(&diagnostic_lock) == FALSE) {
        return;
    }
    HANDLE file = ::CreateFileW(
        path.data(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        (void)::CloseHandle(file);
    }
    ::ReleaseSRWLockExclusive(&diagnostic_lock);
}

void DiagnosticLog(const char* message) noexcept {
    if (message == nullptr) {
        return;
    }

    std::array<wchar_t, 32768> path{};
    if (!BuildLogPath(path)) {
        return;
    }

    char line[1024]{};
    const ULONGLONG elapsed_ms = diagnostic_baseline_elapsed_ms +
        (::GetTickCount64() - diagnostic_baseline_tick);
    const ULONGLONG hours = elapsed_ms / 3600000ULL;
    const ULONGLONG minutes = (elapsed_ms / 60000ULL) % 60ULL;
    const ULONGLONG seconds = (elapsed_ms / 1000ULL) % 60ULL;
    const ULONGLONG milliseconds = elapsed_ms % 1000ULL;
    const int prefix_length = std::snprintf(
        line,
        sizeof(line),
        "[%02llu:%02llu:%02llu.%03llu] ",
        static_cast<unsigned long long>(hours),
        static_cast<unsigned long long>(minutes),
        static_cast<unsigned long long>(seconds),
        static_cast<unsigned long long>(milliseconds));
    if (prefix_length <= 0 || static_cast<std::size_t>(prefix_length) >= sizeof(line)) {
        return;
    }
    const std::size_t available = sizeof(line) - static_cast<std::size_t>(prefix_length) - 3;
    const std::size_t message_length = strnlen(message, available);
    std::memcpy(line + prefix_length, message, message_length);
    std::size_t line_length = static_cast<std::size_t>(prefix_length) + message_length;
    line[line_length++] = '\r';
    line[line_length++] = '\n';

    if (::TryAcquireSRWLockExclusive(&diagnostic_lock) == FALSE) {
        return;
    }
    HANDLE file = ::CreateFileW(
        path.data(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written{};
        (void)::WriteFile(file, line, static_cast<DWORD>(line_length), &written, nullptr);
        (void)::CloseHandle(file);
    }
    ::ReleaseSRWLockExclusive(&diagnostic_lock);
}

void DiagnosticLogFormat(const char* format, ...) noexcept {
    if (format == nullptr) {
        return;
    }
    char message[768]{};
    va_list arguments;
    va_start(arguments, format);
    const int result = std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (result >= 0) {
        DiagnosticLog(message);
    }
}

const char* VtableHookStatusName(VtableHookStatus status) noexcept {
    switch (status) {
    case VtableHookStatus::Installed: return "Installed";
    case VtableHookStatus::Restored: return "Restored";
    case VtableHookStatus::AlreadyActive: return "AlreadyActive";
    case VtableHookStatus::NotActive: return "NotActive";
    case VtableHookStatus::InvalidArgument: return "InvalidArgument";
    case VtableHookStatus::InvalidPlatform: return "InvalidPlatform";
    case VtableHookStatus::RuntimeVersionUnavailable: return "RuntimeVersionUnavailable";
    case VtableHookStatus::RuntimeVersionMismatch: return "RuntimeVersionMismatch";
    case VtableHookStatus::AddressOverflow: return "AddressOverflow";
    case VtableHookStatus::SlotUnaligned: return "SlotUnaligned";
    case VtableHookStatus::SlotNotReadable: return "SlotNotReadable";
    case VtableHookStatus::OriginalNotExecutable: return "OriginalNotExecutable";
    case VtableHookStatus::TargetRoutineNotExecutable: return "TargetRoutineNotExecutable";
    case VtableHookStatus::ProtectFailed: return "ProtectFailed";
    case VtableHookStatus::FlushFailed: return "FlushFailed";
    case VtableHookStatus::ProtectionRestoreFailed: return "ProtectionRestoreFailed";
    case VtableHookStatus::ReplacedByAnotherWriter: return "ReplacedByAnotherWriter";
    case VtableHookStatus::AllocationFailed: return "AllocationFailed";
    }
    return "Unknown";
}

}