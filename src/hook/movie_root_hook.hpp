#pragma once

#include "api/callback_registry.hpp"
#include "scaleform/bridge.hpp"
#include "hook/vtable_hook.hpp"

#include <cstdint>
#include <memory>
#include <mutex>

namespace sf {

class MovieRootRouteState;

class MovieRootHookController final {
public:
    explicit MovieRootHookController(
        CallbackRegistry& callbackRegistry,
        VtableHookPlatform platform = SystemVtableHookPlatform()) noexcept;
    ~MovieRootHookController();

    MovieRootHookController(const MovieRootHookController&) = delete;
    MovieRootHookController& operator=(const MovieRootHookController&) = delete;

    [[nodiscard]] VtableHookStatus Install(
        const TargetProfile& profile,
        std::uintptr_t moduleBase) noexcept;
    [[nodiscard]] VtableHookStatus Restore() noexcept;
    [[nodiscard]] bool IsActive() const noexcept;

private:
    CallbackRegistry& callbackRegistry_;
    VtableHookPlatform platform_;
    VtableHook vtableHook_;
    std::shared_ptr<NativeFunctionHandler> functionHandler_;
    mutable std::mutex lifecycleMutex_;
    std::shared_ptr<MovieRootRouteState> routeState_;
};

[[nodiscard]] bool __fastcall HookedMovieRootGetVariable(
    void* movieRoot,
    ScaleformValue* outValue,
    const char* path,
    unsigned int callerR9Scratch = 0U) noexcept;

}
