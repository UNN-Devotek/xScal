#include "scaleform/bridge.hpp"
#include "platform/diagnostics.hpp"
#include "scaleform/scaleform_layout.hpp"

#include <atomic>
#include <cstddef>
#include <limits>
#include <utility>

namespace sf {
namespace {

std::atomic_size_t attached_root_count{};

using CreateObjectRoutine = void(__fastcall*)(
    void*, ScaleformValue*, void*, void*, unsigned int);
using CreateFunctionRoutine = void(__fastcall*)(
    void*, ScaleformValue*, void*, void*);
[[nodiscard]] bool IsCreatedObjectValue(const ScaleformValue& value) noexcept {
    const auto info = InspectScaleformValue(value);
    return (info.raw_type & layout::kTypeMask) == layout::kObjectType &&
        info.object_interface != nullptr && info.data != nullptr;
}


[[nodiscard]] CreateObjectRoutine GetCreateObjectRoutine(void* movie_root) noexcept {
    if (movie_root == nullptr) {
        return nullptr;
    }
    void* const vtable = *reinterpret_cast<void**>(movie_root);
    if (vtable == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<CreateObjectRoutine*>(
        static_cast<std::byte*>(vtable) + layout::kCreateObjectVtableOffset);
}

[[nodiscard]] CreateFunctionRoutine GetCreateFunctionRoutine(void* movie_root) noexcept {
    if (movie_root == nullptr) {
        return nullptr;
    }
    void* const vtable = *reinterpret_cast<void**>(movie_root);
    if (vtable == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<CreateFunctionRoutine*>(
        static_cast<std::byte*>(vtable) + layout::kCreateFunctionVtableOffset);
}

[[nodiscard]] bool AddModuleOffset(
    std::uintptr_t module_base,
    std::uintptr_t offset,
    std::uintptr_t& result) noexcept {
    if (offset > (std::numeric_limits<std::uintptr_t>::max)() - module_base) {
        return false;
    }
    result = module_base + offset;
    return result != 0;
}

}

ResolveScaleformApiStatus ResolveScaleformApi(
    const TargetProfile& profile,
    std::uintptr_t module_base,
    platform::VirtualQueryFn virtual_query,
    ResolvedScaleformApi& resolved) noexcept {
    resolved = {};
    std::uintptr_t get_member{};
    std::uintptr_t set_member{};
    std::uintptr_t release_value{};
    if (!AddModuleOffset(module_base, profile.get_member_offset, get_member) ||
        !AddModuleOffset(module_base, profile.set_member_offset, set_member) ||
        !AddModuleOffset(module_base, profile.release_value_offset, release_value)) {
        return ResolveScaleformApiStatus::AddressOverflow;
    }
    if (!platform::IsExecutableAddress(virtual_query, reinterpret_cast<void*>(get_member)) ||
        !platform::IsExecutableAddress(virtual_query, reinterpret_cast<void*>(set_member)) ||
        !platform::IsExecutableAddress(virtual_query, reinterpret_cast<void*>(release_value))) {
        return ResolveScaleformApiStatus::TargetNotExecutable;
    }
    resolved.get_member = reinterpret_cast<ScaleformGetMemberRoutine>(get_member);
    resolved.set_member = reinterpret_cast<ScaleformSetMemberRoutine>(set_member);
    resolved.release_value = reinterpret_cast<ScaleformReleaseValueRoutine>(release_value);
    return ResolveScaleformApiStatus::Resolved;
}

bool GetScaleformMember(
    const MovieRootContext& context,
    ScaleformValue& object,
    const char* member_name,
    ScaleformValue& out_value) noexcept {
    if (context.api.get_member == nullptr || member_name == nullptr) {
        return false;
    }

    const auto owner = InspectScaleformValue(object);
    if (owner.object_interface == nullptr || owner.data == nullptr) {
        return false;
    }


    return context.api.get_member(
        owner.object_interface,
        owner.data,
        member_name,
        &out_value,
        (owner.raw_type & layout::kTypeMask) == layout::kDisplayObjectType);
}

bool SetScaleformMember(
    const MovieRootContext& context,
    ScaleformValue& object,
    const char* member_name,
    ScaleformValue& member_value) noexcept {
    if (context.api.set_member == nullptr || member_name == nullptr) {
        return false;
    }

    const auto owner = InspectScaleformValue(object);
    if (owner.object_interface == nullptr || owner.data == nullptr) {
        return false;
    }

    const bool is_display_object = (owner.raw_type & layout::kTypeMask) == layout::kDisplayObjectType;

    if (context.api.set_member(
            owner.object_interface,
            owner.data,
            member_name,
            &member_value,
            is_display_object)) {
        return true;
    }
    if (!is_display_object) {
        return false;
    }

    return context.api.set_member(
        owner.object_interface,
        owner.data,
        member_name,
        &member_value,
        false);
}

void ReleaseValue(
    const MovieRootContext& context,
    ScaleformValue& value) noexcept {
    if (context.api.release_value == nullptr) {
        return;
    }

    const auto info = InspectScaleformValue(value);
    if ((info.raw_type & layout::kOwnedValueFlag) == 0 ||
        info.object_interface == nullptr || info.data == nullptr) {
        return;
    }

    context.api.release_value(info.object_interface, &value, info.data);
}

bool AttachSFCodeObjectToComponent(
    MovieRootContext& context,
    ScaleformValue& component,
    ScaleformValue& out_bridge) noexcept {
    if (context.movie_root == nullptr || context.api.get_member == nullptr ||
        context.api.set_member == nullptr || context.api.release_value == nullptr ||
        context.function_handler == nullptr) {
        return false;
    }

    const auto create_object = GetCreateObjectRoutine(context.movie_root);
    const auto create_function = GetCreateFunctionRoutine(context.movie_root);
    if (create_object == nullptr || create_function == nullptr) {
        return false;
    }

    create_object(context.movie_root, &out_bridge, nullptr, nullptr, 0);
    if (!IsCreatedObjectValue(out_bridge)) {
        ReleaseValue(context, out_bridge);
        return false;
    }
    ScopedScaleformValue release_bridge_on_failure{context, out_bridge};

    ScaleformValue call{};
    create_function(
        context.movie_root,
        &call,
        context.function_handler->AsScaleformHandler(),
        nullptr);
    if (!IsCreatedObjectValue(call)) {
        ReleaseValue(context, call);
        return false;
    }
    ScopedScaleformValue release_call{context, call};

    if (!SetScaleformMember(context, out_bridge, "call", call)) {
        return false;
    }

    ScaleformValue call_readback{};
    const bool call_readable = GetScaleformMember(context, out_bridge, "call", call_readback);
    const bool call_object_like =
        call_readable && IsObjectLikeScaleformValue(call_readback);
    ScopedScaleformValue release_call_readback{context, call_readback};
    if (!call_object_like) {
        return false;
    }
    DiagnosticLog("xScal: successfully attached call");

    if (!SetScaleformMember(context, component, "__SFCodeObj", out_bridge)) {
        return false;
    }

    ScaleformValue bridge_readback{};
    const bool bridge_readable =
        GetScaleformMember(context, component, "__SFCodeObj", bridge_readback);
    const bool bridge_object_like =
        bridge_readable && IsObjectLikeScaleformValue(bridge_readback);
    ScopedScaleformValue release_bridge_readback{context, bridge_readback};
    if (!bridge_object_like) {
        return false;
    }
    const std::size_t root_number =
        attached_root_count.fetch_add(1, std::memory_order_relaxed) + 1;
    DiagnosticLogFormat(
        "xScal: successfully attached root object #%zu. Thank Todd!",
        root_number);

    release_bridge_on_failure.Dismiss();
    return true;
}

bool AttachSFCodeObjectToRoot(
    MovieRootContext& context,
    ScaleformValue& out_bridge) noexcept {
    if (context.movie_root == nullptr || context.original_get_variable == nullptr) {
        return false;
    }

    ScaleformValue root{};
    if (!context.original_get_variable(context.movie_root, &root, "root1", 0U)) {
        ReleaseValue(context, root);
        return false;
    }
    ScopedScaleformValue release_root{context, root};
    return AttachSFCodeObjectToComponent(context, root, out_bridge);
}
bool CreateNativeCallFunction(
    MovieRootContext& context,
    ScaleformValue& out_function) noexcept {
    if (context.movie_root == nullptr || context.function_handler == nullptr) {
        return false;
    }
    const auto create_function = GetCreateFunctionRoutine(context.movie_root);
    if (create_function == nullptr) {
        return false;
    }
    create_function(
        context.movie_root,
        &out_function,
        context.function_handler->AsScaleformHandler(),
        nullptr);
    if (!IsCreatedObjectValue(out_function)) {
        ReleaseValue(context, out_function);
        out_function = {};
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
