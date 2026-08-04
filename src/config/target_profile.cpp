#include "config/target_profile.hpp"

#include "config/config.h"

#include <cwctype>

namespace sf {
namespace {

using config::kGamePass;
using config::kSteam;

constexpr TargetProfile kSteamProfile{
    TargetKind::Steam,
    L"Fallout76.exe",
    kSteam.runtimeVersion,
    kSteam.primaryGetVariableSlot,
    kSteam.getMember,
    kSteam.setMember,
    kSteam.releaseValue,
};

constexpr TargetProfile kGamePassProfile{
    TargetKind::GamePass,
    L"Project76_GamePass.exe",
    kGamePass.runtimeVersion,
    kGamePass.primaryGetVariableSlot,
    kGamePass.getMember,
    kGamePass.setMember,
    kGamePass.releaseValue,
};

bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::towlower(left[index]) != std::towlower(right[index])) {
            return false;
        }
    }

    return true;
}

}

const TargetProfile* SelectTargetProfile(std::wstring_view executableName) noexcept {
    if (EqualsIgnoreCase(executableName, kSteamProfile.executableName)) {
        return &kSteamProfile;
    }
    if (EqualsIgnoreCase(executableName, kGamePassProfile.executableName)) {
        return &kGamePassProfile;
    }
    return nullptr;
}

}
