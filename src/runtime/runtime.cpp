#include "runtime/runtime.hpp"

#include "platform/diagnostics.hpp"
#include "api/scaleform_callbacks.hpp"

#include <array>
#include <cstddef>
#include <cwchar>
#include <tuple>
#include <vector>

namespace sf {
namespace {

[[nodiscard]] const char* PlatformName(TargetKind kind) noexcept {
    return kind == TargetKind::Steam ? "steam" : "gamepass";
}

[[nodiscard]] bool ReadSystemRuntimeVersion(const wchar_t* path, config::RuntimeVersion& version) noexcept {
    if (path == nullptr || path[0] == L'\0') return false;
    try {
        DWORD ignored{};
        const DWORD size = ::GetFileVersionInfoSizeW(path, &ignored);
        if (size == 0) return false;
        std::vector<std::byte> info(size);
        if (::GetFileVersionInfoW(path, 0, size, info.data()) == FALSE) return false;
        void* raw{};
        UINT raw_size{};
        if (::VerQueryValueW(info.data(), L"\\", &raw, &raw_size) == FALSE ||
            raw == nullptr || raw_size < sizeof(VS_FIXEDFILEINFO)) return false;
        const auto* fixed = static_cast<const VS_FIXEDFILEINFO*>(raw);
        version = {HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
                   HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS)};
        return true;
    } catch (...) { return false; }
}

void LogRuntimeConfiguration(const TargetProfile& profile, std::uintptr_t base,
                             const config::RuntimeVersion* version) noexcept {
    DiagnosticLogFormat(
        "xScal v%.*s minimal init by DCHoaxer",
        static_cast<int>(config::kXScalVersion.size()),
        config::kXScalVersion.data());
    DiagnosticLogFormat("imagebase = %p", reinterpret_cast<void*>(base));
    if (version != nullptr) {
        DiagnosticLogFormat("runtime version: %hu.%hu.%hu.%hu", version->major,
                            version->minor, version->build, version->revision);
    } else {
        DiagnosticLog("runtime version: unknown");
    }
    DiagnosticLogFormat("platform: %s", PlatformName(profile.kind));
    DiagnosticLogFormat("RVA setMember=0x%llX",
                        static_cast<unsigned long long>(profile.set_member_offset));
}

[[nodiscard]] bool IsNewer(const config::RuntimeVersion& found,
                           const config::RuntimeVersion& expected) noexcept {
    return std::tie(found.major, found.minor, found.build, found.revision) >
           std::tie(expected.major, expected.minor, expected.build, expected.revision);
}

void ShowRuntimeVersionError(const RuntimeProcessPlatform& platform,
                             const config::RuntimeVersion* found,
                             const config::RuntimeVersion& expected) noexcept {
    std::array<wchar_t, 1024> message{};
    if (found == nullptr) {
        std::swprintf(message.data(), message.size(),
            L"xScal could not read the Fallout 76 runtime version.\n\n"
            L"Required runtime: %hu.%hu.%hu.%hu\n\n"
            L"xScal features will be unavailable for this session.",
            expected.major, expected.minor, expected.build, expected.revision);
    } else if (IsNewer(*found, expected)) {
        std::swprintf(message.data(), message.size(),
            L"Damn, hold on! are using a newer version of Fallout 76 than this version of xScal supports.\n"
            L"If this version just came out, please be patient while xScal is updated.\n\n"
            L"Runtime: %hu.%hu.%hu.%hu\nSupported: %hu.%hu.%hu.%hu\n\n"
            L"xScal features will be unavailable for this session.",
            found->major, found->minor, found->build, found->revision,
            expected.major, expected.minor, expected.build, expected.revision);
    } else {
        std::swprintf(message.data(), message.size(),
            L"You are using Fallout 76 version %hu.%hu.%hu.%hu, which is out of date "
            L"and incompatible with this version of xScal.\n\n"
            L"Required runtime: %hu.%hu.%hu.%hu\n\n"
            L"xScal features will be unavailable for this session.",
            found->major, found->minor, found->build, found->revision,
            expected.major, expected.minor, expected.build, expected.revision);
    }
    platform.message_box(nullptr, message.data(), L"xScal", MB_OK | MB_ICONEXCLAMATION);
}
}


RuntimeProcessPlatform SystemRuntimeProcessPlatform() noexcept {
    return {
        &::GetModuleFileNameW, &::GetModuleHandleW,
        &ReadSystemRuntimeVersion, &::MessageBoxW,
    };
}

BridgeRuntime::BridgeRuntime(
    CallbackRegistry& callback_registry,
    VtableHookPlatform platform) noexcept
    : controller_(callback_registry, platform) {}

VtableHookStatus BridgeRuntime::Initialize(
    std::wstring_view executable_name,
    std::uintptr_t module_base,
    const config::RuntimeVersion& runtime_version) noexcept {
    if (module_base == 0) return VtableHookStatus::InvalidArgument;
    const TargetProfile* profile = SelectTargetProfile(executable_name);
    if (profile == nullptr) return VtableHookStatus::InvalidArgument;
    if (runtime_version != profile->expected_runtime_version) {
        return VtableHookStatus::RuntimeVersionMismatch;
    }
    SetScaleformRuntimePlatform(profile->kind);
    return controller_.Install(*profile, module_base);
}
VtableHookStatus BridgeRuntime::Shutdown() noexcept {
    return controller_.Restore();
}

bool BridgeRuntime::IsActive() const noexcept {
    return controller_.IsActive();
}

VtableHookStatus InitializeCurrentProcess(
    BridgeRuntime& runtime,
    RuntimeProcessPlatform platform) noexcept {
    if (platform.get_module_file_name == nullptr || platform.get_module_handle == nullptr ||
        platform.read_runtime_version == nullptr || platform.message_box == nullptr) {
        return VtableHookStatus::InvalidPlatform;
    }
    std::array<wchar_t, 32768> path{};
    const DWORD length = platform.get_module_file_name(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return VtableHookStatus::InvalidArgument;

    const std::wstring_view full_path{path.data(), length};
    const std::size_t separator = full_path.find_last_of(L"\\/");
    const std::wstring_view name = separator == std::wstring_view::npos
        ? full_path : full_path.substr(separator + 1);
    const TargetProfile* profile = SelectTargetProfile(name);
    if (profile == nullptr) return VtableHookStatus::InvalidArgument;

    const HMODULE module = platform.get_module_handle(nullptr);
    if (module == nullptr) return VtableHookStatus::InvalidArgument;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    config::RuntimeVersion found{};
    if (!platform.read_runtime_version(path.data(), found)) {
        LogRuntimeConfiguration(*profile, base, nullptr);
        ShowRuntimeVersionError(platform, nullptr, profile->expected_runtime_version);
        return VtableHookStatus::RuntimeVersionUnavailable;
    }
    LogRuntimeConfiguration(*profile, base, &found);
    if (found != profile->expected_runtime_version) {
        ShowRuntimeVersionError(platform, &found, profile->expected_runtime_version);
        return VtableHookStatus::RuntimeVersionMismatch;
    }
    return runtime.Initialize(name, base, found);
}
}