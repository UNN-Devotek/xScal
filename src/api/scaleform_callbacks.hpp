#pragma once

namespace sf {

class CallbackRegistry;
enum class TargetKind;

// Registers the callbacks implemented by this DLL. Keep callback definitions
// and the registration table together in scaleform_callbacks.cpp.
[[nodiscard]] bool RegisterScaleformCallbacks(CallbackRegistry& registry) noexcept;
void SetScaleformRuntimePlatform(TargetKind kind) noexcept;

}