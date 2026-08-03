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
        CallbackRegistry& callback_registry,
        VtableHookPlatform platform = SystemVtableHookPlatform()) noexcept;
    ~MovieRootHookController();

    MovieRootHookController(const MovieRootHookController&) = delete;
    MovieRootHookController& operator=(const MovieRootHookController&) = delete;

    [[nodiscard]] VtableHookStatus Install(
        const TargetProfile& profile,
        std::uintptr_t module_base) noexcept;
    [[nodiscard]] VtableHookStatus Restore() noexcept;
    [[nodiscard]] bool IsActive() const noexcept;

private:
    CallbackRegistry& callback_registry_;
    VtableHookPlatform platform_;
    VtableHook vtable_hook_;
    std::shared_ptr<NativeFunctionHandler> function_handler_;
    mutable std::mutex lifecycle_mutex_;
    std::shared_ptr<MovieRootRouteState> route_state_;
};

[[nodiscard]] bool __fastcall HookedMovieRootGetVariable(
    void* movie_root,
    ScaleformValue* out_value,
    const char* path,
    unsigned int caller_r9_scratch = 0U) noexcept;

}
