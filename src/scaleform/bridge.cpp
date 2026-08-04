#include "scaleform/bridge.hpp"
#include "platform/diagnostics.hpp"
#include "scaleform/scaleform_layout.hpp"

#include <atomic>
#include <cstddef>
#include <limits>
#include <utility>

namespace sf {
namespace {

std::atomic_size_t attachedRootCount{};

using CreateObjectRoutine = void(__fastcall*)(
    void*, ScaleformValue*, void*, void*, unsigned int);
using CreateFunctionRoutine = void(__fastcall*)(
    void*, ScaleformValue*, void*, void*);
[[nodiscard]] bool IsCreatedObjectValue(const ScaleformValue& value) noexcept {
    const auto info = InspectScaleformValue(value);
    return (info.rawType & layout::kTypeMask) == layout::kObjectType &&
        info.objectInterface != nullptr && info.data != nullptr;
}


[[nodiscard]] CreateObjectRoutine GetCreateObjectRoutine(void* movieRoot) noexcept {
    if (movieRoot == nullptr) {
        return nullptr;
    }
    void* const vtable = *reinterpret_cast<void**>(movieRoot);
    if (vtable == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<CreateObjectRoutine*>(
        static_cast<std::byte*>(vtable) + layout::kCreateObjectVtableOffset);
}

[[nodiscard]] CreateFunctionRoutine GetCreateFunctionRoutine(void* movieRoot) noexcept {
    if (movieRoot == nullptr) {
        return nullptr;
    }
    void* const vtable = *reinterpret_cast<void**>(movieRoot);
    if (vtable == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<CreateFunctionRoutine*>(
        static_cast<std::byte*>(vtable) + layout::kCreateFunctionVtableOffset);
}

[[nodiscard]] bool AddModuleOffset(
    std::uintptr_t moduleBase,
    std::uintptr_t offset,
    std::uintptr_t& result) noexcept {
    if (offset > (std::numeric_limits<std::uintptr_t>::max)() - moduleBase) {
        return false;
    }
    result = moduleBase + offset;
    return result != 0;
}

}

ResolveScaleformApiStatus ResolveScaleformApi(
    const TargetProfile& profile,
    std::uintptr_t moduleBase,
    platform::VirtualQueryFn virtualQuery,
    ResolvedScaleformApi& resolved) noexcept {
    resolved = {};
    std::uintptr_t getMember{};
    std::uintptr_t setMember{};
    std::uintptr_t releaseValue{};
    if (!AddModuleOffset(moduleBase, profile.getMemberOffset, getMember) ||
        !AddModuleOffset(moduleBase, profile.setMemberOffset, setMember) ||
        !AddModuleOffset(moduleBase, profile.releaseValueOffset, releaseValue)) {
        return ResolveScaleformApiStatus::AddressOverflow;
    }
    if (!platform::IsExecutableAddress(virtualQuery, reinterpret_cast<void*>(getMember)) ||
        !platform::IsExecutableAddress(virtualQuery, reinterpret_cast<void*>(setMember)) ||
        !platform::IsExecutableAddress(virtualQuery, reinterpret_cast<void*>(releaseValue))) {
        return ResolveScaleformApiStatus::TargetNotExecutable;
    }
    resolved.getMember = reinterpret_cast<ScaleformGetMemberRoutine>(getMember);
    resolved.setMember = reinterpret_cast<ScaleformSetMemberRoutine>(setMember);
    resolved.releaseValue = reinterpret_cast<ScaleformReleaseValueRoutine>(releaseValue);
    return ResolveScaleformApiStatus::Resolved;
}

bool GetScaleformMember(
    const MovieRootContext& context,
    ScaleformValue& object,
    const char* memberName,
    ScaleformValue& outValue) noexcept {
    if (context.api.getMember == nullptr || memberName == nullptr) {
        return false;
    }

    const auto owner = InspectScaleformValue(object);
    if (owner.objectInterface == nullptr || owner.data == nullptr) {
        return false;
    }


    return context.api.getMember(
        owner.objectInterface,
        owner.data,
        memberName,
        &outValue,
        (owner.rawType & layout::kTypeMask) == layout::kDisplayObjectType);
}

bool SetScaleformMember(
    const MovieRootContext& context,
    ScaleformValue& object,
    const char* memberName,
    ScaleformValue& memberValue) noexcept {
    if (context.api.setMember == nullptr || memberName == nullptr) {
        return false;
    }

    const auto owner = InspectScaleformValue(object);
    if (owner.objectInterface == nullptr || owner.data == nullptr) {
        return false;
    }

    const bool isDisplayObject = (owner.rawType & layout::kTypeMask) == layout::kDisplayObjectType;

    if (context.api.setMember(
            owner.objectInterface,
            owner.data,
            memberName,
            &memberValue,
            isDisplayObject)) {
        return true;
    }
    if (!isDisplayObject) {
        return false;
    }

    return context.api.setMember(
        owner.objectInterface,
        owner.data,
        memberName,
        &memberValue,
        false);
}

void ReleaseValue(
    const MovieRootContext& context,
    ScaleformValue& value) noexcept {
    if (context.api.releaseValue == nullptr) {
        return;
    }

    const auto info = InspectScaleformValue(value);
    if ((info.rawType & layout::kOwnedValueFlag) == 0 ||
        info.objectInterface == nullptr || info.data == nullptr) {
        return;
    }

    context.api.releaseValue(info.objectInterface, &value, info.data);
}

bool AttachSFCodeObjectToComponent(
    MovieRootContext& context,
    ScaleformValue& component,
    ScaleformValue& outBridge) noexcept {
    if (context.movieRoot == nullptr || context.api.getMember == nullptr ||
        context.api.setMember == nullptr || context.api.releaseValue == nullptr ||
        context.functionHandler == nullptr) {
        return false;
    }

    const auto createObject = GetCreateObjectRoutine(context.movieRoot);
    const auto createFunction = GetCreateFunctionRoutine(context.movieRoot);
    if (createObject == nullptr || createFunction == nullptr) {
        return false;
    }

    createObject(context.movieRoot, &outBridge, nullptr, nullptr, 0);
    if (!IsCreatedObjectValue(outBridge)) {
        ReleaseValue(context, outBridge);
        return false;
    }
    ScopedScaleformValue releaseBridgeOnFailure{context, outBridge};

    ScaleformValue call{};
    createFunction(
        context.movieRoot,
        &call,
        context.functionHandler->AsScaleformHandler(),
        nullptr);
    if (!IsCreatedObjectValue(call)) {
        ReleaseValue(context, call);
        return false;
    }
    ScopedScaleformValue releaseCall{context, call};

    if (!SetScaleformMember(context, outBridge, "call", call)) {
        return false;
    }

    ScaleformValue callReadback{};
    const bool callReadable = GetScaleformMember(context, outBridge, "call", callReadback);
    const bool callObjectLike =
        callReadable && IsObjectLikeScaleformValue(callReadback);
    ScopedScaleformValue releaseCallReadback{context, callReadback};
    if (!callObjectLike) {
        return false;
    }
    DiagnosticLog("xScal: attached call");

    if (!SetScaleformMember(context, component, "__SFCodeObj", outBridge)) {
        return false;
    }

    ScaleformValue bridgeReadback{};
    const bool bridgeReadable =
        GetScaleformMember(context, component, "__SFCodeObj", bridgeReadback);
    const bool bridgeObjectLike =
        bridgeReadable && IsObjectLikeScaleformValue(bridgeReadback);
    ScopedScaleformValue releaseBridgeReadback{context, bridgeReadback};
    if (!bridgeObjectLike) {
        return false;
    }
    const std::size_t rootNumber =
        attachedRootCount.fetch_add(1, std::memory_order_relaxed) + 1;
    DiagnosticLogFormat(
        "xScal: attached root object #%zu to movieRoot. Thanks Todd!",
        rootNumber);

    releaseBridgeOnFailure.Dismiss();
    return true;
}

bool AttachSFCodeObjectToRoot(
    MovieRootContext& context,
    ScaleformValue& outBridge) noexcept {
    if (context.movieRoot == nullptr || context.originalGetVariable == nullptr) {
        return false;
    }

    ScaleformValue root{};
    if (!context.originalGetVariable(context.movieRoot, &root, "root1", 0U)) {
        ReleaseValue(context, root);
        return false;
    }
    ScopedScaleformValue releaseRoot{context, root};
    return AttachSFCodeObjectToComponent(context, root, outBridge);
}
bool CreateNativeCallFunction(
    MovieRootContext& context,
    ScaleformValue& outFunction) noexcept {
    if (context.movieRoot == nullptr || context.functionHandler == nullptr) {
        return false;
    }
    const auto createFunction = GetCreateFunctionRoutine(context.movieRoot);
    if (createFunction == nullptr) {
        return false;
    }
    createFunction(
        context.movieRoot,
        &outFunction,
        context.functionHandler->AsScaleformHandler(),
        nullptr);
    if (!IsCreatedObjectValue(outFunction)) {
        ReleaseValue(context, outFunction);
        outFunction = {};
        return false;
    }
    return true;
}




ScopedScaleformValue::ScopedScaleformValue(
    const MovieRootContext& context,
    ScaleformValue& value) noexcept
    : context_{&context}, value_{&value} {}

ScopedScaleformValue::~ScopedScaleformValue() {
    if (context_ != nullptr && value_ != nullptr) {
        ReleaseValue(*context_, *value_);
    }
}

ScopedScaleformValue::ScopedScaleformValue(ScopedScaleformValue&& other) noexcept
    : context_{std::exchange(other.context_, nullptr)},
      value_{std::exchange(other.value_, nullptr)} {}

void ScopedScaleformValue::Dismiss() noexcept {
    context_ = nullptr;
    value_ = nullptr;
}}
