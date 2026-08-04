#pragma once

#include "config/config.h"
#include "hook/movie_root_hook.hpp"

#include <cstdint>
#include <string_view>

namespace sf {

struct RuntimeProcessPlatform final {
    using ReadRuntimeVersionFn = bool(*)(
        const wchar_t* executablePath,
        config::RuntimeVersion& version) noexcept;

    decltype(&::GetModuleFileNameW) getModuleFileName{};
    decltype(&::GetModuleHandleW) getModuleHandle{};
    ReadRuntimeVersionFn readRuntimeVersion{};
    decltype(&::MessageBoxW) messageBox{};
};
[[nodiscard]] RuntimeProcessPlatform SystemRuntimeProcessPlatform() noexcept;

class BridgeRuntime final {
public:
    explicit BridgeRuntime(
        CallbackRegistry& callbackRegistry,
        VtableHookPlatform platform = SystemVtableHookPlatform()) noexcept;

    BridgeRuntime(const BridgeRuntime&) = delete;
    BridgeRuntime& operator=(const BridgeRuntime&) = delete;

    [[nodiscard]] VtableHookStatus Initialize(
        std::wstring_view executableName,
        std::uintptr_t moduleBase,
        const config::RuntimeVersion& runtimeVersion) noexcept;

    [[nodiscard]] VtableHookStatus Shutdown() noexcept;
    [[nodiscard]] bool IsActive() const noexcept;

private:
    MovieRootHookController controller_;
};

[[nodiscard]] VtableHookStatus InitializeCurrentProcess(
    BridgeRuntime& runtime,
    RuntimeProcessPlatform platform = SystemRuntimeProcessPlatform()) noexcept;

}