#pragma once

#include "api/callback_registry.hpp"
#include "scaleform/abi.hpp"
#include "config/target_profile.hpp"
#include "platform/windows_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sf {

// The native FunctionHandler at 0x180150790 reads its parameter pointer from
// RDX, an argument-array pointer from +0x20, and a 32-bit count from +0x28.
// Arguments are 48-byte records; their string representation is decoded only
// by TryDecodeScaleformString(), from the observed 0x180145E30 layout.
struct FunctionParams final {
    void* result{};
    void* movie{};
    ScaleformValue* thisObject{};
    void* unknown18{};
    const std::byte* arguments{};
    std::uint32_t argumentCount{};
};

static_assert(offsetof(FunctionParams, result) == 0x00);
static_assert(offsetof(FunctionParams, arguments) == 0x20);
static_assert(offsetof(FunctionParams, argumentCount) == 0x28);

class NativeFunctionHandler final {
public:
    explicit NativeFunctionHandler(CallbackRegistry& callbackRegistry) noexcept;

    [[nodiscard]] void* AsScaleformHandler() noexcept { return this; }

private:
    using HandlerVtableEntry = void(__fastcall*)();

    [[nodiscard]] static void* __fastcall Identity(void* handler) noexcept;
    static void __fastcall Invoke(void* handler, const FunctionParams* params) noexcept;

    const HandlerVtableEntry* vtable_;
    // GFxFunctionHandler derives from GRefCountBase. Scaleform owns this
    // prefix and performs 32-bit interlocked ref-count operations at +0x08.
    volatile std::int32_t referenceCount_;
    std::uint32_t referenceCountPadding_;

    // The recovered game and reference handlers are exactly the 0x10-byte
    // GRefCountBase prefix. Keep bridge state out of the ABI object.
    static CallbackRegistry* callbackRegistry_;
};

// Parses one 48-byte FunctionParams argument using the two candidates
// recovered in sub_180145E30. The returned name aliases game-owned storage.
[[nodiscard]] bool TryDecodeScaleformString(
    const std::byte* argumentRecord,
    std::string_view& decodedString) noexcept;

// Safely addresses and decodes one argument from a callback invocation.
[[nodiscard]] bool TryGetScaleformStringArgument(
    const ScaleformCall& call,
    std::size_t argumentIndex,
    std::string_view& decodedString) noexcept;

struct ScaleformValueInfo final {
    std::uint8_t rawType{};
    void* objectInterface{};
    void* data{};
};

[[nodiscard]] ScaleformValueInfo InspectScaleformValue(
    const ScaleformValue& value) noexcept;
[[nodiscard]] bool IsObjectLikeScaleformValue(
    const ScaleformValue& value) noexcept;

using ScaleformGetMemberRoutine = bool(__fastcall*)(
    void*, void*, const char*, ScaleformValue*, bool);
using ScaleformSetMemberRoutine = bool(__fastcall*)(
    void*, void*, const char*, ScaleformValue*, bool);
using ScaleformReleaseValueRoutine = void(__fastcall*)(
    void*, ScaleformValue*, void*);

struct ResolvedScaleformApi final {
    ScaleformGetMemberRoutine getMember{};
    ScaleformSetMemberRoutine setMember{};
    ScaleformReleaseValueRoutine releaseValue{};
};

enum class ResolveScaleformApiStatus {
    Resolved,
    AddressOverflow,
    TargetNotExecutable,
};

[[nodiscard]] ResolveScaleformApiStatus ResolveScaleformApi(
    const TargetProfile& profile,
    std::uintptr_t moduleBase,
    platform::VirtualQueryFn virtualQuery,
    ResolvedScaleformApi& resolved) noexcept;

struct MovieRootContext final {
    void* movieRoot{};
    MovieRootGetVariable originalGetVariable{};
    ResolvedScaleformApi api{};
    NativeFunctionHandler* functionHandler{};
};

// Creates __SFCodeObj.call and attaches it directly to an already-resolved UI
// component. On success outBridge owns the newly-created object and the
// caller must eventually ReleaseValue().
[[nodiscard]] bool AttachSFCodeObjectToComponent(
    MovieRootContext& context,
    ScaleformValue& component,
    ScaleformValue& outBridge) noexcept;

// Resolves root1, then delegates to AttachSFCodeObjectToComponent().
[[nodiscard]] bool AttachSFCodeObjectToRoot(
    MovieRootContext& context,
    ScaleformValue& outBridge) noexcept;

// Creates only the native `call` function through MovieRoot's recovered
// +0x180 factory. The caller owns the resulting value.
[[nodiscard]] bool CreateNativeCallFunction(
    MovieRootContext& context,
    ScaleformValue& outFunction) noexcept;

[[nodiscard]] bool GetScaleformMember(
    const MovieRootContext& context,
    ScaleformValue& object,
    const char* memberName,
    ScaleformValue& outValue) noexcept;

[[nodiscard]] bool SetScaleformMember(
    const MovieRootContext& context,
    ScaleformValue& object,
    const char* memberName,
    ScaleformValue& memberValue) noexcept;

void ReleaseValue(
    const MovieRootContext& context,
    ScaleformValue& value) noexcept;

// Releases an owned Scaleform value on every exit path unless ownership is transferred.
class ScopedScaleformValue final {
public:
    ScopedScaleformValue(const MovieRootContext& context, ScaleformValue& value) noexcept;
    ~ScopedScaleformValue();

    ScopedScaleformValue(const ScopedScaleformValue&) = delete;
    ScopedScaleformValue& operator=(const ScopedScaleformValue&) = delete;
    ScopedScaleformValue(ScopedScaleformValue&& other) noexcept;
    ScopedScaleformValue& operator=(ScopedScaleformValue&&) = delete;

    void Dismiss() noexcept;

private:
    const MovieRootContext* context_{};
    ScaleformValue* value_{};
};

}
