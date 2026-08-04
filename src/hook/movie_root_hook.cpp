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

namespace sf
{
    namespace
    {

        std::mutex activeRouteMutex;
        std::shared_ptr<MovieRootRouteState> activeRouteState;
        std::atomic<MovieRootGetVariable> fallbackOriginal{};
        thread_local bool insideMovieRootHook = false;
        class RecursionGuard final
        {
        public:
            RecursionGuard() noexcept { insideMovieRootHook = true; }
            ~RecursionGuard() { insideMovieRootHook = false; }
        };

        [[nodiscard]] bool IsReadableMovieRoot(
            const VtableHookPlatform &platform,
            const void *movieRoot) noexcept
        {
            if (movieRoot == nullptr || platform.virtualQuery == nullptr)
            {
                return false;
            }
            MEMORY_BASIC_INFORMATION memory{};
            if (platform.virtualQuery(movieRoot, &memory, sizeof(memory)) == 0 ||
                memory.State != MEM_COMMIT || !platform::IsReadableProtection(memory.Protect))
            {
                return false;
            }
            const auto regionBase = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
            const auto address = reinterpret_cast<std::uintptr_t>(movieRoot);
            return address >= regionBase && memory.RegionSize >= sizeof(void *) &&
                   address - regionBase <= memory.RegionSize - sizeof(void *);
        }

        [[nodiscard]] bool TryReadPath(
            const VtableHookPlatform &platform,
            const char *path,
            std::string_view &result) noexcept
        {
            constexpr std::size_t kMaxPathLength = 4096;
            result = {};
            if (path == nullptr || platform.virtualQuery == nullptr)
            {
                return false;
            }

            const auto start = reinterpret_cast<std::uintptr_t>(path);
            std::uintptr_t current = start;
            std::size_t length = 0;
            while (length < kMaxPathLength)
            {
                MEMORY_BASIC_INFORMATION memory{};
                if (platform.virtualQuery(
                        reinterpret_cast<const void *>(current), &memory, sizeof(memory)) == 0 ||
                    memory.State != MEM_COMMIT || !platform::IsReadableProtection(memory.Protect))
                {
                    return false;
                }
                const auto regionBase = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
                if (memory.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - regionBase)
                {
                    return false;
                }
                const auto regionEnd = regionBase + memory.RegionSize;
                if (current < regionBase || current >= regionEnd)
                {
                    return false;
                }

                const std::size_t readable = std::min<std::size_t>(
                    static_cast<std::size_t>(regionEnd - current),
                    kMaxPathLength - length);
                const auto *bytes = reinterpret_cast<const char *>(current);
                for (std::size_t index = 0; index < readable; ++index)
                {
                    if (bytes[index] == '\0')
                    {
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
            const VtableHookPlatform &platform,
            const TargetProfile &profile,
            std::uintptr_t moduleBase,
            MovieRootGetVariable &original,
            void *&expectedVtable,
            VtableHookStatus &failure) noexcept
        {
            if (platform.virtualQuery == nullptr)
            {
                failure = VtableHookStatus::InvalidPlatform;
                return false;
            }
            if (profile.primaryGetVariableSlotOffset < layout::kGetVariableVtableOffset ||
                profile.primaryGetVariableSlotOffset >
                    (std::numeric_limits<std::uintptr_t>::max)() - moduleBase)
            {
                failure = profile.primaryGetVariableSlotOffset < layout::kGetVariableVtableOffset
                              ? VtableHookStatus::InvalidArgument
                              : VtableHookStatus::AddressOverflow;
                return false;
            }

            const auto slotAddress = moduleBase + profile.primaryGetVariableSlotOffset;
            if ((slotAddress % alignof(void *)) != 0)
            {
                failure = VtableHookStatus::SlotUnaligned;
                return false;
            }

            MEMORY_BASIC_INFORMATION memory{};
            if (platform.virtualQuery(reinterpret_cast<const void *>(slotAddress), &memory, sizeof(memory)) == 0 ||
                memory.State != MEM_COMMIT || !platform::IsReadableProtection(memory.Protect))
            {
                failure = VtableHookStatus::SlotNotReadable;
                return false;
            }
            const auto regionBase = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
            if (slotAddress < regionBase || memory.RegionSize < sizeof(void *) ||
                slotAddress - regionBase > memory.RegionSize - sizeof(void *))
            {
                failure = VtableHookStatus::SlotNotReadable;
                return false;
            }

            void *rawOriginal{};
            std::memcpy(&rawOriginal, reinterpret_cast<const void *>(slotAddress), sizeof(rawOriginal));
            if (rawOriginal == nullptr)
            {
                failure = VtableHookStatus::OriginalNotExecutable;
                return false;
            }
            original = reinterpret_cast<MovieRootGetVariable>(rawOriginal);
            expectedVtable = reinterpret_cast<void *>(slotAddress - layout::kGetVariableVtableOffset);
            return true;
        }

        [[nodiscard]] bool IsObjectReadback(
            bool readable,
            const ScaleformValue &value) noexcept
        {
            return readable && IsObjectLikeScaleformValue(value);
        }

        // fix for .call members not being present in some of the swf's.
        [[nodiscard]] bool EnsureNativeCallMember(
            MovieRootContext &context,
            ScaleformValue &bridge) noexcept
        {
            ScaleformValue existingCall{};

            const bool existingCallReadable =
                GetScaleformMember(
                    context,
                    bridge,
                    "call",
                    existingCall);

            const bool existingCallUsable =
                IsObjectReadback(
                    existingCallReadable,
                    existingCall);

            ReleaseValue(context, existingCall);

            if (existingCallUsable)
            {
                return true;
            }

            ScaleformValue nativeCall{};

            if (!CreateNativeCallFunction(context, nativeCall))
            {
                return false;
            }

            const bool callAttached =
                SetScaleformMember(
                    context,
                    bridge,
                    "call",
                    nativeCall);

            ScaleformValue attachedCall{};

            const bool attachedCallReadable =
                callAttached &&
                GetScaleformMember(
                    context,
                    bridge,
                    "call",
                    attachedCall);

            const bool attachedCallUsable =
                IsObjectReadback(
                    attachedCallReadable,
                    attachedCall);

            ReleaseValue(context, attachedCall);
            ReleaseValue(context, nativeCall);

            return attachedCallUsable;
        }
    }

    class MovieRootRouteState final
    {
    public:
        MovieRootRouteState(
            std::shared_ptr<NativeFunctionHandler> functionHandler,
            VtableHookPlatform platform,
            MovieRootGetVariable original,
            void *expectedVtable,
            ResolvedScaleformApi api) noexcept
            : platform_(platform),
              functionHandler_(std::move(functionHandler)),
              original_(original),
              expectedVtable_(expectedVtable),
              api_(api) {}

        [[nodiscard]] bool Route(
            void *movieRoot,
            ScaleformValue *outValue,
            const char *path,
            unsigned int callerR9Scratch) noexcept;

    private:
        [[nodiscard]] bool EnsureBridge(void *movieRoot) noexcept;
        [[nodiscard]] bool HasBridgeForMovieRoot(void *movieRoot) const noexcept;
        [[nodiscard]] bool RepairComponent(void *movieRoot, ScaleformValue &component) noexcept;
        void RememberBridgeForMovieRoot(void *movieRoot) noexcept;

        VtableHookPlatform platform_;
        std::shared_ptr<NativeFunctionHandler> functionHandler_;
        std::mutex bridgeMutex_;
        MovieRootGetVariable original_{};
        void *expectedVtable_{};
        ResolvedScaleformApi api_{};
        std::vector<void *> bridgedMovieRoots_;
    };

    MovieRootHookController::MovieRootHookController(
        CallbackRegistry &callbackRegistry,
        VtableHookPlatform platform) noexcept
        : callbackRegistry_(callbackRegistry),
          platform_(platform),
          vtableHook_(platform)
    {
        try
        {
            functionHandler_ = std::make_shared<NativeFunctionHandler>(callbackRegistry);
        }
        catch (...)
        {
        }
    }

    MovieRootHookController::~MovieRootHookController()
    {
        VtableHookStatus status = VtableHookStatus::NotActive;
        for (unsigned int attempt = 0; attempt < 3; ++attempt)
        {
            status = Restore();
            if (!vtableHook_.IsActive() && status != VtableHookStatus::ProtectionRestoreFailed)
            {
                break;
            }
        }

        {
            std::scoped_lock activeLock{activeRouteMutex};
            if (activeRouteState == routeState_)
            {
                activeRouteState.reset();
                fallbackOriginal.store(nullptr, std::memory_order_release);
            }
        }
        routeState_.reset();
    }

    VtableHookStatus MovieRootHookController::Install(
        const TargetProfile &profile,
        std::uintptr_t moduleBase) noexcept
    {
        std::scoped_lock lock{lifecycleMutex_};
        if (vtableHook_.IsActive())
        {
            return VtableHookStatus::AlreadyActive;
        }

        MovieRootGetVariable preparedOriginal{};
        void *preparedVtable{};
        VtableHookStatus failure{};
        if (!ReadPreparedOriginal(
                platform_, profile, moduleBase, preparedOriginal, preparedVtable, failure))
        {
            return failure;
        }

        ResolvedScaleformApi preparedApi{};
        const auto apiStatus = ResolveScaleformApi(
            profile, moduleBase, platform_.virtualQuery, preparedApi);
        if (apiStatus == ResolveScaleformApiStatus::AddressOverflow)
        {
            return VtableHookStatus::AddressOverflow;
        }
        if (apiStatus != ResolveScaleformApiStatus::Resolved)
        {
            return VtableHookStatus::TargetRoutineNotExecutable;
        }

        if (functionHandler_ == nullptr)
        {
            return VtableHookStatus::AllocationFailed;
        }

        std::shared_ptr<MovieRootRouteState> candidate;
        try
        {
            candidate = std::make_shared<MovieRootRouteState>(
                functionHandler_, platform_, preparedOriginal, preparedVtable, preparedApi);
        }
        catch (...)
        {
            return VtableHookStatus::AllocationFailed;
        }

        {
            std::scoped_lock activeLock{activeRouteMutex};
            if (activeRouteState != nullptr)
            {
                return VtableHookStatus::AlreadyActive;
            }
            activeRouteState = candidate;
            fallbackOriginal.store(preparedOriginal, std::memory_order_release);
        }

        const auto status = vtableHook_.Install(profile, moduleBase, &HookedMovieRootGetVariable);
        if (status != VtableHookStatus::Installed)
        {
            std::scoped_lock activeLock{activeRouteMutex};
            if (activeRouteState == candidate)
            {
                activeRouteState.reset();
                fallbackOriginal.store(nullptr, std::memory_order_release);
            }
            return status;
        }

        routeState_ = candidate;
        return status;
    }

    VtableHookStatus MovieRootHookController::Restore() noexcept
    {
        std::scoped_lock lock{lifecycleMutex_};
        const auto status = vtableHook_.Restore();
        if (!vtableHook_.IsActive())
        {
            {
                std::scoped_lock activeLock{activeRouteMutex};
                if (activeRouteState == routeState_)
                {
                    activeRouteState.reset();
                    fallbackOriginal.store(nullptr, std::memory_order_release);
                }
            }
            routeState_.reset();
        }
        return status;
    }

    bool MovieRootHookController::IsActive() const noexcept
    {
        return vtableHook_.IsActive();
    }

    bool MovieRootRouteState::HasBridgeForMovieRoot(void *movieRoot) const noexcept
    {
        return std::find(
                   bridgedMovieRoots_.begin(), bridgedMovieRoots_.end(), movieRoot) !=
               bridgedMovieRoots_.end();
    }

    void MovieRootRouteState::RememberBridgeForMovieRoot(void *movieRoot) noexcept
    {
        /* cache must be revalidated
        if (HasBridgeForMovieRoot(movieRoot))
        {
            return;
        }
        */
        try
        {
            bridgedMovieRoots_.push_back(movieRoot);
        }
        catch (...)
        {
        }
    }

    bool MovieRootRouteState::EnsureBridge(
        void *movieRoot) noexcept
    {
        std::unique_lock lock{
            bridgeMutex_,
            std::try_to_lock,
        };

        if (!lock.owns_lock())
        {
            return false;
        }

        const auto original = original_;

        if (movieRoot == nullptr ||
            api_.getMember == nullptr ||
            original == nullptr)
        {
            return false;
        }

        MovieRootContext context{
            movieRoot,
            original,
            api_,
            functionHandler_.get(),
        };

        // MovieRoot addresses can be reused. Do not trust the pointer cache
        // without confirming that this root still has a usable call member.
        if (HasBridgeForMovieRoot(movieRoot))
        {
            ScaleformValue cachedCall{};

            const bool cachedCallReadable = original(
                movieRoot,
                &cachedCall,
                "root1.__SFCodeObj.call",
                0U);

            const bool cachedCallUsable =
                IsObjectReadback(
                    cachedCallReadable,
                    cachedCall);

            ReleaseValue(context, cachedCall);

            if (cachedCallUsable)
            {
                return true;
            }

            DiagnosticLogFormat(
                "xScal: no idea how, but cached MovieRoot %p lost __SFCodeObj.call; repairing",
                movieRoot);
        }

        // The object may already exist while call is missing.
        ScaleformValue existingBridge{};

        const bool existingBridgeReadable = original(
            movieRoot,
            &existingBridge,
            "root1.__SFCodeObj",
            0U);

        const bool existingBridgeUsable =
            IsObjectReadback(
                existingBridgeReadable,
                existingBridge);

        if (existingBridgeUsable)
        {
            const bool callReady =
                EnsureNativeCallMember(
                    context,
                    existingBridge);

            ReleaseValue(context, existingBridge);

            if (callReady)
            {
                RememberBridgeForMovieRoot(movieRoot);

                DiagnosticLogFormat(
                    "xScal: verified __SFCodeObj.call on MovieRoot %p",
                    movieRoot);
            }

            return callReady;
        }

        ReleaseValue(context, existingBridge);

        // no existing bridge, make and attach a copmlete object.
        ScaleformValue newBridge{};

        if (!AttachSFCodeObjectToRoot(
                context,
                newBridge))
        {
            return false;
        }

        // check thru the MovieRoot lookup path.
        ScaleformValue insertedCall{};

        const bool insertedCallReadable = original(
            movieRoot,
            &insertedCall,
            "root1.__SFCodeObj.call",
            0U);

        const bool insertedCallUsable =
            IsObjectReadback(
                insertedCallReadable,
                insertedCall);

        ReleaseValue(context, insertedCall);
        ReleaseValue(context, newBridge);

        if (insertedCallUsable)
        {
            RememberBridgeForMovieRoot(movieRoot);
        }

        return insertedCallUsable;
    }

      bool MovieRootRouteState::RepairComponent(
        void* movieRoot,
        ScaleformValue& component) noexcept {
        std::unique_lock lock{
            bridgeMutex_,
            std::try_to_lock,
        };

        if (!lock.owns_lock()) {
            return false;
        }

        const auto original = original_;

        if (movieRoot == nullptr ||
            original == nullptr ||
            api_.getMember == nullptr) {
            return false;
        }

        MovieRootContext context{
            movieRoot,
            original,
            api_,
            functionHandler_.get(),
        };

        ScaleformValue existingBridge{};

        const bool bridgeReadable =
            GetScaleformMember(
                context,
                component,
                "__SFCodeObj",
                existingBridge);

        const bool bridgeUsable =
            IsObjectReadback(
                bridgeReadable,
                existingBridge);

        if (!bridgeUsable) {
            ReleaseValue(context, existingBridge);
            return false;
        }

        // Keep the existing object and add only the missing call member.
        const bool callReady =
            EnsureNativeCallMember(
                context,
                existingBridge);

        ReleaseValue(context, existingBridge);

        if (callReady) {
            DiagnosticLogFormat(
                "xScal: repaired component __SFCodeObj.call on MovieRoot %p",
                movieRoot);
        }

        return callReady;
    }

    bool MovieRootRouteState::Route(
        void *movieRoot,
        ScaleformValue *outValue,
        const char *path,
        unsigned int callerR9Scratch) noexcept
    {
        const auto original = original_;
        const auto expectedVtable = expectedVtable_;
        if (movieRoot == nullptr || outValue == nullptr || path == nullptr ||
            original == nullptr || expectedVtable == nullptr)
        {
            return false;
        }
        if (!IsReadableMovieRoot(platform_, movieRoot))
        {
            return false;
        }

        void *incomingVtable{};
        std::memcpy(&incomingVtable, movieRoot, sizeof(incomingVtable));
        if (incomingVtable != expectedVtable)
        {
            return false;
        }
        if (insideMovieRootHook)
        {
            return original(movieRoot, outValue, path, callerR9Scratch);
        }

        RecursionGuard recursionGuard;
        const bool originalResult =
            original(movieRoot, outValue, path, callerR9Scratch);
        std::string_view requestedPath;
        if (!TryReadPath(platform_, path, requestedPath))
        {
            return originalResult;
        }

        const bool directCall = routing::IsDirectCallAlias(requestedPath);
        const bool objectAlias = routing::IsObjectAlias(requestedPath);
        if (originalResult &&
            routing::ShouldEnsureRootBridge(requestedPath)) {
            (void)EnsureBridge(movieRoot);

            if (requestedPath != "root1") {
                (void)RepairComponent(
                    movieRoot,
                    *outValue);
            }
        }
        if (!directCall && !objectAlias)
        {
            return originalResult;
        }
        if (directCall)
        {
            MovieRootContext context{
                movieRoot,
                original,
                api_,
                functionHandler_.get(),
            };
            ScaleformValue nativeCall{};
            if (!CreateNativeCallFunction(context, nativeCall))
            {
                return originalResult;
            }
            ReleaseValue(context, *outValue);
            *outValue = nativeCall;
            nativeCall = {};
            return true;
        }
        if (originalResult) {
            std::unique_lock lock{
                bridgeMutex_,
                std::try_to_lock,
            };

            if (lock.owns_lock()) {
                MovieRootContext context{
                    movieRoot,
                    original,
                    api_,
                    functionHandler_.get(),
                };

                if (EnsureNativeCallMember(
                        context,
                        *outValue)) {
                    DiagnosticLogFormat(
                        "xScal: new __SFCodeObj.call on MovieRoot %p",
                        movieRoot);
                }
            }

            return true;
        }
        if (!EnsureBridge(movieRoot))
        {
            return false;
        }
        if (original(movieRoot, outValue, path, callerR9Scratch))
        {
            return true;
        }
        return requestedPath != "root1.__SFCodeObj" &&
               original(movieRoot, outValue, "root1.__SFCodeObj", 0U);
    }

    bool __fastcall HookedMovieRootGetVariable(
        void *movieRoot,
        ScaleformValue *outValue,
        const char *path,
        unsigned int callerR9Scratch) noexcept
    {
        std::shared_ptr<MovieRootRouteState> routeState;
        {
            std::unique_lock activeLock{activeRouteMutex, std::try_to_lock};
            if (!activeLock.owns_lock())
            {
                const auto original = fallbackOriginal.load(std::memory_order_acquire);
                return original != nullptr &&
                       original(movieRoot, outValue, path, callerR9Scratch);
            }
            routeState = activeRouteState;
        }
        return routeState != nullptr &&
               routeState->Route(movieRoot, outValue, path, callerR9Scratch);
    }

}
