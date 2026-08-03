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
    kSteam.runtime_version,
    kSteam.primary_get_variable_slot,
    kSteam.get_member,
    kSteam.set_member,
    kSteam.release_value,
};

constexpr TargetProfile kGamePassProfile{
    TargetKind::GamePass,
    L"Project76_GamePass.exe",
    kGamePass.runtime_version,
    kGamePass.primary_get_variable_slot,
    kGamePass.get_member,
    kGamePass.set_member,
    kGamePass.release_value,
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

const TargetProfile* SelectTargetProfile(std::wstring_view executable_name) noexcept {
    if (EqualsIgnoreCase(executable_name, kSteamProfile.executable_name)) {
        return &kSteamProfile;
    }
    if (EqualsIgnoreCase(executable_name, kGamePassProfile.executable_name)) {
        return &kGamePassProfile;
    }
    return nullptr;
}

}
