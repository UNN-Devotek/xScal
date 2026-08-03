#pragma once

#include "config/config.h"
#include "hook/movie_root_hook.hpp"

#include <cstdint>
#include <string_view>

namespace sf {

struct RuntimeProcessPlatform final {
    using ReadRuntimeVersionFn = bool(*)(
        const wchar_t* executable_path,
        config::RuntimeVersion& version) noexcept;

    decltype(&::GetModuleFileNameW) get_module_file_name{};
    decltype(&::GetModuleHandleW) get_module_handle{};
    ReadRuntimeVersionFn read_runtime_version{};
    decltype(&::MessageBoxW) message_box{};
};
[[nodiscard]] RuntimeProcessPlatform SystemRuntimeProcessPlatform() noexcept;

class BridgeRuntime final {
public:
    explicit BridgeRuntime(
        CallbackRegistry& callback_registry,
        VtableHookPlatform platform = SystemVtableHookPlatform()) noexcept;

    BridgeRuntime(const BridgeRuntime&) = delete;
    BridgeRuntime& operator=(const BridgeRuntime&) = delete;

    [[nodiscard]] VtableHookStatus Initialize(
        std::wstring_view executable_name,
        std::uintptr_t module_base,
        const config::RuntimeVersion& runtime_version) noexcept;

    [[nodiscard]] VtableHookStatus Shutdown() noexcept;
    [[nodiscard]] bool IsActive() const noexcept;

private:
    MovieRootHookController controller_;
};

[[nodiscard]] VtableHookStatus InitializeCurrentProcess(
    BridgeRuntime& runtime,
    RuntimeProcessPlatform platform = SystemRuntimeProcessPlatform()) noexcept;

}