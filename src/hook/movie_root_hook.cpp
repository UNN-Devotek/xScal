#include "hook/movie_root_hook.hpp"
#include "platform/diagnostics.hpp"
#include "hook/path_router.hpp"
#include "platform/windows_memory.hpp"
#include "scaleform/scaleform_layout.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace sf {
namespace {

std::mutex active_route_mutex;
std::shared_ptr<MovieRootRouteState> active_route_state;
std::atomic<MovieRootGetVariable> fallback_original{};
thread_local bool inside_movie_root_hook = false;
class RecursionGuard final {
public:
    RecursionGuard() noexcept { inside_movie_root_hook = true; }
    ~RecursionGuard() { inside_movie_root_hook = false; }
};

[[nodiscard]] bool IsReadableMovieRoot(
    const VtableHookPlatform& platform,
    const void* movie_root) noexcept {
    if (movie_root == nullptr || platform.virtual_query == nullptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (platform.virtual_query(movie_root, &memory, sizeof(memory)) == 0 ||
        memory.State != MEM_COMMIT || !platform::IsReadableProtection(memory.Protect)) {
        return false;
    }
    const auto region_base = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto address = reinterpret_cast<std::uintptr_t>(movie_root);
    return address >= region_base && memory.RegionSize >= sizeof(void*) &&
        address - region_base <= memory.RegionSize - sizeof(void*);
}

[[nodiscard]] bool TryReadPath(
    const VtableHookPlatform& platform,
    const char* path,
    std::string_view& result) noexcept {
    constexpr std::size_t kMaxPathLength = 4096;
    result = {};
    if (path == nullptr || platform.virtual_query == nullptr) {
        return false;
    }

    const auto start = reinterpret_cast<std::uintptr_t>(path);
    std::uintptr_t current = start;
    std::size_t length = 0;
    while (length < kMaxPathLength) {
        MEMORY_BASIC_INFORMATION memory{};
        if (platform.virtual_query(
                reinterpret_cast<const void*>(current), &memory, sizeof(memory)) == 0 ||
            memory.State != MEM_COMMIT || !platform::IsReadableProtection(memory.Protect)) {
            return false;
        }
        const auto region_base = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - region_base) {
            return false;
        }
        const auto region_end = region_base + memory.RegionSize;
        if (current < region_base || current >= region_end) {
            return false;
        }

        const std::size_t readable = std::min<std::size_t>(
            static_cast<std::size_t>(region_end - current),
            kMaxPathLength - length);
        const auto* bytes = reinterpret_cast<const char*>(current);
        for (std::size_t index = 0; index < readable; ++index) {
            if (bytes[index] == '\0') {
                result = std::string_view{path, length + index};
                return !result.empty();
            }
        }
        current += readable;
        length += readable;
    }
    return false;
}
[[nodiscard]] bool ReadPreparedOriginal(
    const VtableHookPlatform& platform,
    const TargetProfile& profile,
    std::uintptr_t module_base,
    MovieRootGetVariable& original,
    void*& expected_vtable,
    VtableHookStatus& failure) noexcept {
    if (platform.virtual_query == nullptr) {
        failure = VtableHookStatus::InvalidPlatform;
        return false;
    }
    if (profile.primary_get_variable_slot_offset < layout::kGetVariableVtableOffset ||
        profile.primary_get_variable_slot_offset >
            (std::numeric_limits<std::uintptr_t>::max)() - module_base) {
        failure = profile.primary_get_variable_slot_offset < layout::kGetVariableVtableOffset
            ? VtableHookStatus::InvalidArgument
            : VtableHookStatus::AddressOverflow;
        return false;
    }

    const auto slot_address = module_base + profile.primary_get_variable_slot_offset;
    if ((slot_address % alignof(void*)) != 0) {
        failure = VtableHookStatus::SlotUnaligned;
        return false;
    }

    MEMORY_BASIC_INFORMATION memory{};
    if (platform.virtual_query(reinterpret_cast<const void*>(slot_address), &memory, sizeof(memory)) == 0 ||
        memory.State != MEM_COMMIT || !platform::IsReadableProtection(memory.Protect)) {
        failure = VtableHookStatus::SlotNotReadable;
        return false;
    }
    const auto region_base = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if (slot_address < region_base || memory.RegionSize < sizeof(void*) ||
        slot_address - region_base > memory.RegionSize - sizeof(void*)) {
        failure = VtableHookStatus::SlotNotReadable;
        return false;
    }

    void* raw_original{};
    std::memcpy(&raw_original, reinterpret_cast<const void*>(slot_address), sizeof(raw_original));
    if (raw_original == nullptr) {
        failure = VtableHookStatus::OriginalNotExecutable;
        return false;
    }
    original = reinterpret_cast<MovieRootGetVariable>(raw_original);
    expected_vtable = reinterpret_cast<void*>(slot_address - layout::kGetVariableVtableOffset);
    return true;
}

[[nodiscard]] bool IsObjectReadback(
    bool readable,
    const ScaleformValue& value) noexcept {
    return readable && IsObjectLikeScaleformValue(value);
}

}

class MovieRootRouteState final {
public:
    MovieRootRouteState(
        std::shared_ptr<NativeFunctionHandler> function_handler,
        VtableHookPlatform platform,
        MovieRootGetVariable original,
        void* expected_vtable,
        ResolvedScaleformApi api) noexcept
        : platform_(platform),
          function_handler_(std::move(function_handler)),
          original_(original),
          expected_vtable_(expected_vtable),
          api_(api) {}

    [[nodiscard]] bool Route(
        void* movie_root,
        ScaleformValue* out_value,
        const char* path,
        unsigned int caller_r9_scratch) noexcept;

private:
    [[nodiscard]] bool EnsureBridge(void* movie_root) noexcept;
    [[nodiscard]] bool HasBridgeForMovieRoot(void* movie_root) const noexcept;
    void RememberBridgeForMovieRoot(void* movie_root) noexcept;

    VtableHookPlatform platform_;
    std::shared_ptr<NativeFunctionHandler> function_handler_;
    std::mutex bridge_mutex_;
    MovieRootGetVariable original_{};
    void* expected_vtable_{};
    ResolvedScaleformApi api_{};
    std::vector<void*> bridged_movie_roots_;
};

MovieRootHookController::MovieRootHookController(
    CallbackRegistry& callback_registry,
    VtableHookPlatform platform) noexcept
    : callback_registry_(callback_registry),
      platform_(platform),
      vtable_hook_(platform) {
    try {
        function_handler_ = std::make_shared<NativeFunctionHandler>(callback_registry);
    } catch (...) {
    }
}

MovieRootHookController::~MovieRootHookController() {
    VtableHookStatus status = VtableHookStatus::NotActive;
    for (unsigned int attempt = 0; attempt < 3; ++attempt) {
        status = Restore();
        if (!vtable_hook_.IsActive() && status != VtableHookStatus::ProtectionRestoreFailed) {
            break;
        }
    }

    {
        std::scoped_lock active_lock{active_route_mutex};
        if (active_route_state == route_state_) {
            active_route_state.reset();
            fallback_original.store(nullptr, std::memory_order_release);
        }
    }
    route_state_.reset();
}

VtableHookStatus MovieRootHookController::Install(
    const TargetProfile& profile,
    std::uintptr_t module_base) noexcept {
    std::scoped_lock lock{lifecycle_mutex_};
    if (vtable_hook_.IsActive()) {
        return VtableHookStatus::AlreadyActive;
    }

    MovieRootGetVariable prepared_original{};
    void* prepared_vtable{};
    VtableHookStatus failure{};
    if (!ReadPreparedOriginal(
            platform_, profile, module_base, prepared_original, prepared_vtable, failure)) {
        return failure;
    }

    ResolvedScaleformApi prepared_api{};
    const auto api_status = ResolveScaleformApi(
        profile, module_base, platform_.virtual_query, prepared_api);
    if (api_status == ResolveScaleformApiStatus::AddressOverflow) {
        return VtableHookStatus::AddressOverflow;
    }
    if (api_status != ResolveScaleformApiStatus::Resolved) {
        return VtableHookStatus::TargetRoutineNotExecutable;
    }

    if (function_handler_ == nullptr) {
        return VtableHookStatus::AllocationFailed;
    }

    std::shared_ptr<MovieRootRouteState> candidate;
    try {
        candidate = std::make_shared<MovieRootRouteState>(
            function_handler_, platform_, prepared_original, prepared_vtable, prepared_api);
    } catch (...) {
        return VtableHookStatus::AllocationFailed;
    }

    {
        std::scoped_lock active_lock{active_route_mutex};
        if (active_route_state != nullptr) {
            return VtableHookStatus::AlreadyActive;
        }
        active_route_state = candidate;
        fallback_original.store(prepared_original, std::memory_order_release);
    }

    const auto status = vtable_hook_.Install(profile, module_base, &HookedMovieRootGetVariable);
    if (status != VtableHookStatus::Installed) {
        std::scoped_lock active_lock{active_route_mutex};
        if (active_route_state == candidate) {
            active_route_state.reset();
            fallback_original.store(nullptr, std::memory_order_release);
        }
        return status;
    }

    route_state_ = candidate;
    return status;
}

VtableHookStatus MovieRootHookController::Restore() noexcept {
    std::scoped_lock lock{lifecycle_mutex_};
    const auto status = vtable_hook_.Restore();
    if (!vtable_hook_.IsActive()) {
        {
            std::scoped_lock active_lock{active_route_mutex};
            if (active_route_state == route_state_) {
                active_route_state.reset();
                fallback_original.store(nullptr, std::memory_order_release);
            }
        }
        route_state_.reset();
    }
    return status;
}

bool MovieRootHookController::IsActive() const noexcept {
    return vtable_hook_.IsActive();
}

bool MovieRootRouteState::HasBridgeForMovieRoot(void* movie_root) const noexcept {
    return std::find(
        bridged_movie_roots_.begin(), bridged_movie_roots_.end(), movie_root) !=
        bridged_movie_roots_.end();
}

void MovieRootRouteState::RememberBridgeForMovieRoot(void* movie_root) noexcept {
    if (HasBridgeForMovieRoot(movie_root)) {
        return;
    }
    try {
        bridged_movie_roots_.push_back(movie_root);
    } catch (...) {
    }
}

bool MovieRootRouteState::EnsureBridge(void* movie_root) noexcept {
    std::unique_lock lock{bridge_mutex_, std::try_to_lock};
    if (!lock.owns_lock()) {
        return false;
    }
    if (HasBridgeForMovieRoot(movie_root)) {
        return true;
    }
    const auto original = original_;
    if (api_.get_member == nullptr || original == nullptr) {
        return false;
    }

    MovieRootContext context{
        movie_root,
        original,
        api_,
        function_handler_.get(),
    };
    ScaleformValue existing{};
    const bool existing_readable =
        original(movie_root, &existing, "root1.__SFCodeObj", 0U);
    const bool existing_object = IsObjectReadback(existing_readable, existing);
    if (existing_object) {
        ScaleformValue existing_call{};
        const bool existing_call_readable =
            GetScaleformMember(context, existing, "call", existing_call);
        const bool existing_call_object = IsObjectReadback(existing_call_readable,
            existing_call);
        ReleaseValue(context, existing_call);
        if (existing_call_object) {
            ReleaseValue(context, existing);
            RememberBridgeForMovieRoot(movie_root);
            return true;
        }

        ScaleformValue native_call{};
        if (!CreateNativeCallFunction(context, native_call)) {
            ReleaseValue(context, existing);
            return false;
        }
        const bool call_attached = SetScaleformMember(context, existing, "call", native_call);
        ScaleformValue attached_call{};
        const bool attached_call_readable =
            call_attached && GetScaleformMember(context, existing, "call", attached_call);
        const bool attached_call_object = IsObjectReadback(attached_call_readable,
            attached_call);
        ReleaseValue(context, attached_call);
        ReleaseValue(context, native_call);
        ReleaseValue(context, existing);
        if (attached_call_object) {
            DiagnosticLog("xScal: successfully attached call");
            RememberBridgeForMovieRoot(movie_root);
        }
        return attached_call_object;
    }
    ReleaseValue(context, existing);

    ScaleformValue bridge{};
    if (!AttachSFCodeObjectToRoot(context, bridge)) {
        return false;
    }
    ScaleformValue inserted_call{};
    const bool inserted_call_readable =
        original(movie_root, &inserted_call, "root1.__SFCodeObj.call", 0U);
    const bool inserted_call_object = IsObjectReadback(inserted_call_readable,
        inserted_call);
    ReleaseValue(context, inserted_call);
    ReleaseValue(context, bridge);
    if (inserted_call_object) {
        RememberBridgeForMovieRoot(movie_root);
    }
    return inserted_call_object;
}

bool MovieRootRouteState::Route(
    void* movie_root,
    ScaleformValue* out_value,
    const char* path,
    unsigned int caller_r9_scratch) noexcept {
    const auto original = original_;
    const auto expected_vtable = expected_vtable_;
    if (movie_root == nullptr || out_value == nullptr || path == nullptr ||
        original == nullptr || expected_vtable == nullptr) {
        return false;
    }
    if (!IsReadableMovieRoot(platform_, movie_root)) {
        return false;
    }

    void* incoming_vtable{};
    std::memcpy(&incoming_vtable, movie_root, sizeof(incoming_vtable));
    if (incoming_vtable != expected_vtable) {
        return false;
    }
    if (inside_movie_root_hook) {
        return original(movie_root, out_value, path, caller_r9_scratch);
    }

    RecursionGuard recursion_guard;
    const bool original_result =
        original(movie_root, out_value, path, caller_r9_scratch);
    std::string_view requested_path;
    if (!TryReadPath(platform_, path, requested_path)) {
        return original_result;
    }

    const bool direct_call = routing::IsDirectCallAlias(requested_path);
    const bool object_alias = routing::IsObjectAlias(requested_path);
    if (original_result && routing::ShouldEnsureRootBridge(requested_path)) {
        (void)EnsureBridge(movie_root);
    }
    if (!direct_call && !object_alias) {
        return original_result;
    }
    if (direct_call) {
        MovieRootContext context{
            movie_root,
            original,
            api_,
            function_handler_.get(),
        };
        ScaleformValue native_call{};
        if (!CreateNativeCallFunction(context, native_call)) {
            return original_result;
        }
        ReleaseValue(context, *out_value);
        *out_value = native_call;
        native_call = {};
        return true;
    }
    if (original_result) {
        return true;
    }
    if (!EnsureBridge(movie_root)) {
        return false;
    }
    if (original(movie_root, out_value, path, caller_r9_scratch)) {
        return true;
    }
    return requested_path != "root1.__SFCodeObj" &&
        original(movie_root, out_value, "root1.__SFCodeObj", 0U);
}

bool __fastcall HookedMovieRootGetVariable(
    void* movie_root,
    ScaleformValue* out_value,
    const char* path,
    unsigned int caller_r9_scratch) noexcept {
    std::shared_ptr<MovieRootRouteState> route_state;
    {
        std::unique_lock active_lock{active_route_mutex, std::try_to_lock};
        if (!active_lock.owns_lock()) {
            const auto original = fallback_original.load(std::memory_order_acquire);
            return original != nullptr &&
                original(movie_root, out_value, path, caller_r9_scratch);
        }
        route_state = active_route_state;
    }
    return route_state != nullptr &&
        route_state->Route(movie_root, out_value, path, caller_r9_scratch);
}

}
