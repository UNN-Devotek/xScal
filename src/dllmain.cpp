#include "api/scaleform_callbacks.hpp"
#include "api/callback_registry.hpp"
#include "platform/diagnostics.hpp"
#include "runtime/runtime.hpp"

#include <Windows.h>

#include <atomic>
#include <new>

namespace {

std::atomic<sf::BridgeRuntime*> runtime_instance{};

DWORD WINAPI InitializeBridge(LPVOID) noexcept {
    sf::ClearDiagnosticLog();

    auto& callback_registry = sf::GlobalCallbackRegistry();
    (void)sf::RegisterScaleformCallbacks(callback_registry);

    auto* candidate = new (std::nothrow) sf::BridgeRuntime(callback_registry);
    if (candidate == nullptr) {
        return 0;
    }

    sf::BridgeRuntime* expected = nullptr;
    if (!runtime_instance.compare_exchange_strong(
            expected, candidate, std::memory_order_acq_rel, std::memory_order_acquire)) {
        delete candidate;
        return 0;
    }

    const auto hookStatus = sf::InitializeCurrentProcess(*candidate);
    sf::DiagnosticLogFormat("Init: %s", sf::VtableHookStatusName(hookStatus));
    return 0;
}

}

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) noexcept {
    if (reason == DLL_PROCESS_ATTACH) {
        sf::SetDiagnosticModule(module);
        (void)::DisableThreadLibraryCalls(module);
        HANDLE thread = ::CreateThread(nullptr, 0, &InitializeBridge, nullptr, 0, nullptr);
        if (thread != nullptr) {
            (void)::CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        sf::BridgeRuntime* runtime = runtime_instance.load(std::memory_order_acquire);
        if (runtime != nullptr) {
            (void)runtime->Shutdown();
        }
    }
    return TRUE;
}