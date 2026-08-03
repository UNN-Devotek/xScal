#include "scaleform_callbacks.hpp"

#include "api/callback_registry.hpp"
#include "config/config.h"
#include "config/target_profile.hpp"
#include "scaleform/bridge.hpp"
#include "scaleform/scaleform_layout.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>



// Add new __SFCodeObj.call("name", ...) handlers in this file
//   1. Implement a function with the ScaleformCallbackHandler signature below.
//   2. Add one {"name", &Function} row to kScaleformCallbackHandlers.
// ====================================================================================

namespace sf {
namespace {

bool __fastcall HandleGetXScalRuntimeInfo(const ScaleformCall*, void*) noexcept;
bool __fastcall HandleReadIhbData(const ScaleformCall*, void*) noexcept;
bool __fastcall HandleOldMods(const ScaleformCall*, void*) noexcept;

constexpr std::size_t kMaxReturnedFileSize = 0x3FFF;
std::atomic<TargetKind> runtime_platform{TargetKind::Steam};

// mod file pathes 
constexpr wchar_t kIHBDataPath[]    = L"Data\\configuration\\ImprovedBars.ctx";
constexpr wchar_t kBuffDataPath[]         = L"Data\\BuffData.ini";
constexpr wchar_t kChallengeDataPath[]    = L"Data\\ChallengeData.ini";
constexpr wchar_t kPerksPath[]            = L"Data\\perkloadoutmanager.ini";
constexpr wchar_t kSaveEverythingPath[]   = L"Data\\saveeverything.ini";
constexpr wchar_t kItemsModPath[]         = L"Data\\itemsmod.ini";
constexpr wchar_t kLegendaryModsPath[]    = L"Data\\LegendaryMods.ini";
constexpr wchar_t kVendorLogPath[]        = L"Data\\vendorlog.txt";
constexpr wchar_t kDPSMeterPath[]         = L"Data\\DPSMeter.txt";
constexpr wchar_t kCampSearchConfigPath[] = L"Data\\CAMPSearch.ini";
constexpr wchar_t kBlockFilePath[]        = L"Data\\configuration\\blocklist.ini";
constexpr wchar_t kCampDataPath[]         = L"Data\\CampData.ini";
constexpr wchar_t kCharacterDataPath[]    = L"Data\\CharacterData.ini";

struct ScaleformCallbackDefinition final {
    const char* name;
    ScaleformCallback callback;
    void* userData;
};

constexpr std::array kScaleformCallbackHandlers{
    ScaleformCallbackDefinition{"GetZFERuntimeInfo", &HandleGetXScalRuntimeInfo},
    ScaleformCallbackDefinition{"ReadIHBData", &HandleReadIhbData},
    // lets stick to one handler
        
    //ScaleformCallbackDefinition{"ChatConfigFile",        &HandleOldMods, const_cast<wchar_t*>(kTCPath)},
    ScaleformCallbackDefinition{"WriteIHBData",        &HandleOldMods, const_cast<wchar_t*>(kIHBDataPath)},
    ScaleformCallbackDefinition{"writeBuffDataFile",        &HandleOldMods, const_cast<wchar_t*>(kBuffDataPath)},
    ScaleformCallbackDefinition{"writeChallengeDataFile",   &HandleOldMods, const_cast<wchar_t*>(kChallengeDataPath)},
    ScaleformCallbackDefinition{"writePerksFile",           &HandleOldMods, const_cast<wchar_t*>(kPerksPath)},
    ScaleformCallbackDefinition{"writeSaveEverythingFile",  &HandleOldMods, const_cast<wchar_t*>(kSaveEverythingPath)},
    ScaleformCallbackDefinition{"writeItemsModFile",        &HandleOldMods, const_cast<wchar_t*>(kItemsModPath)},
    ScaleformCallbackDefinition{"writeLegendaryModsFile",   &HandleOldMods, const_cast<wchar_t*>(kLegendaryModsPath)},
    ScaleformCallbackDefinition{"writeVendorLogFile",       &HandleOldMods, const_cast<wchar_t*>(kVendorLogPath)},
    ScaleformCallbackDefinition{"writeDPSMeterFile",        &HandleOldMods, const_cast<wchar_t*>(kDPSMeterPath)},
    ScaleformCallbackDefinition{"writeCampSearchConfigFile",&HandleOldMods, const_cast<wchar_t*>(kCampSearchConfigPath)},
    ScaleformCallbackDefinition{"writeBlockFile",           &HandleOldMods, const_cast<wchar_t*>(kBlockFilePath)},
    ScaleformCallbackDefinition{"writeCampDataFile",        &HandleOldMods, const_cast<wchar_t*>(kCampDataPath)},
    ScaleformCallbackDefinition{"writeCharacterDataFile",   &HandleOldMods, const_cast<wchar_t*>(kCharacterDataPath)},
};

// Helpers

[[nodiscard]] const char* RuntimePlatformName() noexcept {
    return runtime_platform.load(std::memory_order_acquire) == TargetKind::GamePass
        ? "gamepass"
        : "steam";
}

[[nodiscard]] bool ResolvePath(const wchar_t* relativePath, std::filesystem::path& resolvedPath) noexcept
{
    try {
        std::array<wchar_t, 32768> executablePath{};

        const DWORD length = ::GetModuleFileNameW(
            nullptr,
            executablePath.data(),
            static_cast<DWORD>(executablePath.size()));

        if (length == 0 || length >= executablePath.size()) {
            return false;
        }

        resolvedPath =
            std::filesystem::path{executablePath.data()}.parent_path() /
            relativePath;

        return true;
    } catch (...) {
        return false;
    }
}
// ======================================================
// callback implementations. Hi Todd!

bool HandleOldMods(const ScaleformCall* call, void* userData) noexcept
{
    if (call == nullptr || call->result == nullptr) return false;
    const auto* relativePath = static_cast<const wchar_t*>(userData);

    call->result->SetBoolean(false);
    if (call->arguments == nullptr || call->argument_count < 2) {
        return true;
    }

    std::string_view fileData;
    if (!TryGetScaleformStringArgument(*call, 1, fileData)) {
        return true;
    }
    
    try {
        std::filesystem::path targetPath;
        if (!ResolvePath(relativePath, targetPath)) {
            return true;
        }

        std::error_code error;
        std::filesystem::create_directories(targetPath.parent_path(), error);
        if (error) {
            return true;
        }

        std::filesystem::path temporaryPath = targetPath;
        temporaryPath += L".tmp";
        {
            std::ofstream output{
                temporaryPath,
                std::ios::binary | std::ios::trunc};
            if (!output) {
                return true;
            }
            output.write(fileData.data(), static_cast<std::streamsize>(fileData.size()));
            if (!output) {
                output.close();
                std::filesystem::remove(temporaryPath, error);
                return true;
            }
        }

        if (!::MoveFileExW(
                temporaryPath.c_str(),
                targetPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporaryPath, error);
            return true;
        }

        
         call->result->SetBoolean(true);
        return true;
    } catch(...)
    {

        return true;
    }
}


bool __fastcall HandleGetXScalRuntimeInfo(const ScaleformCall* call, void*) noexcept 
{
    
    if (call == nullptr || call->result == nullptr) return false;
    try {
        std::string runtime_info{"{\"runtime\":\"xScal\",\"version\":\""};
        runtime_info.append(config::kXScalVersion.data(), config::kXScalVersion.size());
        runtime_info += "\",\"platform\":\"";
        runtime_info += RuntimePlatformName();
        runtime_info += "\"}";
        return call->result->SetString(runtime_info);
    } catch (...) {
        return false;
    }
}
/*
bool __fastcall HandleWriteIhbData(const ScaleformCall* call, void*) noexcept 
{
    if (call == nullptr || call->result == nullptr) {
        return false;
    }

    call->result->SetBoolean(false);
    if (call->arguments == nullptr || call->argument_count < 2) {
        return true;
    }

    std::string_view data;
    if (!TryGetScaleformStringArgument(*call, 1, data)) {
        return true;
    }

    try {
        std::filesystem::path target_path;
        if (!ResolveIhbPath(target_path)) {
            return true;
        }

        std::error_code error;
        std::filesystem::create_directories(target_path.parent_path(), error);
        if (error) {
            return true;
        }

        std::filesystem::path temporary_path = target_path;
        temporary_path += L".tmp";
        {
            std::ofstream output{
                temporary_path,
                std::ios::binary | std::ios::trunc};
            if (!output) {
                return true;
            }
            output.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!output) {
                output.close();
                std::filesystem::remove(temporary_path, error);
                return true;
            }
        }

        if (!::MoveFileExW(
                temporary_path.c_str(),
                target_path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary_path, error);
            return true;
        }

        call->result->SetBoolean(true);
        return true;
    } catch (...) {
        return true;
    }
}
*/

bool __fastcall HandleReadIhbData(const ScaleformCall* call, void*) noexcept 
{
    if (call == nullptr || call->result == nullptr) {
        return false;
    }

    call->result->SetBoolean(false);
    try {
        std::filesystem::path targetPath;
        if (!ResolvePath(kIHBDataPath, targetPath)) {
            return true;
        }

        std::ifstream input{targetPath, std::ios::binary | std::ios::ate};
        if (!input) {
            return true;
        }
        const std::streampos end = input.tellg();
        if (end < 0 || static_cast<std::uintmax_t>(end) > kMaxReturnedFileSize) {
            return true;
        }

        std::string contents(static_cast<std::size_t>(end), '\0');
        input.seekg(0, std::ios::beg);
        if (!contents.empty()) {
            input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (!input) {
                return true;
            }
        }
        return call->result->SetString(contents);
    } catch (...) {
        return true;
    }
}


}

bool RegisterScaleformCallbacks(CallbackRegistry& registry) noexcept {
    bool registered_all = true;
    for (const auto& definition : kScaleformCallbackHandlers) {
        if (!registry.Register(definition.name, definition.callback, definition.userData)) {
            registered_all = false;
        }
    }
    return registered_all;
}

void SetScaleformRuntimePlatform(TargetKind kind) noexcept {
    runtime_platform.store(kind, std::memory_order_release);
}

}
