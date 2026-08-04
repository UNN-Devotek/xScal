#include "scaleform_callbacks.hpp"

#include "api/callback_registry.hpp"
#include "config/config.h"
#include "scaleform/bridge.hpp"
#include "scaleform/scaleform_layout.hpp"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace sf {
    namespace {

        bool __fastcall HandleGetXScalRuntimeInfo(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteIhbData(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleReadIhbData(const ScaleformCall*, void*) noexcept;
        //bool __fastcall HandleWriteChatConfigFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteBuffDataFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteChallengeDataFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWritePerksFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteSaveEverythingFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteItemsModFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteLegendaryModsFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteVendorLogFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteDPSMeterFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteCampSearchConfigFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteBlockFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteCampDataFile(const ScaleformCall*, void*) noexcept;
        bool __fastcall HandleWriteCharacterDataFile(const ScaleformCall*, void*) noexcept;

        struct ScaleformCallbackDefinition final {
            const char* name;
            ScaleformCallback callback;
        };

        constexpr std::array kScaleformCallbackHandlers{
            ScaleformCallbackDefinition{"GetXSRuntimeInfo",          &HandleGetXScalRuntimeInfo}, // It was named GetZFERuntimeInfo because ImprovedBars mod used that internally. 
            ScaleformCallbackDefinition{"ReadIHBData",                &HandleReadIhbData},
            ScaleformCallbackDefinition{"WriteIHBData",               &HandleWriteIhbData},

            //ScaleformCallbackDefinition{"writeChatConfigFile",        &HandleWriteChatConfigFile},
            ScaleformCallbackDefinition{"writeBuffDataFile",          &HandleWriteBuffDataFile},
            ScaleformCallbackDefinition{"writeChallengeDataFile",     &HandleWriteChallengeDataFile},
            ScaleformCallbackDefinition{"writePerksFile",             &HandleWritePerksFile},
            ScaleformCallbackDefinition{"writeSaveEverythingFile",    &HandleWriteSaveEverythingFile},
            ScaleformCallbackDefinition{"writeItemsModFile",          &HandleWriteItemsModFile},
            ScaleformCallbackDefinition{"writeLegendaryModsFile",     &HandleWriteLegendaryModsFile},
            ScaleformCallbackDefinition{"writeVendorLogFile",         &HandleWriteVendorLogFile},
            ScaleformCallbackDefinition{"writeDPSMeterFile",          &HandleWriteDPSMeterFile},
            ScaleformCallbackDefinition{"writeCampSearchConfigFile",  &HandleWriteCampSearchConfigFile},
            ScaleformCallbackDefinition{"writeBlockFile",             &HandleWriteBlockFile},
            ScaleformCallbackDefinition{"writeCampDataFile",          &HandleWriteCampDataFile},
            ScaleformCallbackDefinition{"writeCharacterDataFile",     &HandleWriteCharacterDataFile},
        };
        constexpr std::size_t kMaxReturnedFileSize = 0x3FFF;
        std::atomic<TargetKind> runtime_platform{ TargetKind::Steam };



        // Fiile paths
        constexpr wchar_t kIhbRelativePath[] = L"Data\\configuration\\ImprovedBars.ctx";
        //constexpr wchar_t kChatConfigPath[]       = L"Data\\configuration\\chatmod.ini";
        constexpr wchar_t kBuffDataPath[] = L"Data\\BuffData.ini";
        constexpr wchar_t kChallengeDataPath[] = L"Data\\ChallengeData.ini";
        constexpr wchar_t kPerksPath[] = L"Data\\perkloadoutmanager.ini";
        constexpr wchar_t kSaveEverythingPath[] = L"Data\\saveeverything.ini";
        constexpr wchar_t kItemsModPath[] = L"Data\\itemsmod.ini";
        constexpr wchar_t kLegendaryModsPath[] = L"Data\\LegendaryMods.ini";
        constexpr wchar_t kVendorLogPath[] = L"Data\\vendorlog.txt";
        constexpr wchar_t kDPSMeterPath[] = L"Data\\DPSMeter.txt";
        constexpr wchar_t kCampSearchConfigPath[] = L"Data\\CAMPSearch.ini";
        constexpr wchar_t kBlockFilePath[] = L"Data\\configuration\\blocklist.ini";
        constexpr wchar_t kCampDataPath[] = L"Data\\CampData.ini";
        constexpr wchar_t kCharacterDataPath[] = L"Data\\CharacterData.ini";

        // HELPER FUNCTIONS

        [[nodiscard]] const char* RuntimePlatformName() noexcept 
        {
            return TargetKindName(runtime_platform.load(std::memory_order_acquire));
        }
        
        [[nodiscard]] bool PrevalidateInputContent(std::string_view payloadContent) noexcept
        {
            constexpr std::size_t kMaxPayloadSize = 200'000; // 200 KB for now. sShould put it elsewhere.

            if (payloadContent.empty() || payloadContent.size() > kMaxPayloadSize) return false;
            for (unsigned char c : payloadContent) {
                if (c == '\0')
                    return false;

                 // CR/LF/TAB are ok
                if (c < 0x20 && c != '\r' && c != '\n' && c != '\t')
                    return false;
            }

            return true;
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
                    std::filesystem::path{ executablePath.data() }.parent_path() /
                    relativePath;

                return true;
            }
            catch (...) {
                return false;
            }
        }

        [[nodiscard]] bool WriteFile(const wchar_t* relativePath, std::string_view data) noexcept
        {
            try {
                std::filesystem::path targetPath;
                if (!ResolvePath(relativePath, targetPath)) {
                    return false;
                }

                if(!PrevalidateInputContent(data)) {
                    return false;
                }

                std::error_code error;
                std::filesystem::create_directories(targetPath.parent_path(), error);
                if (error) {
                    return false;
                }

                std::filesystem::path temporaryPath = targetPath;
                temporaryPath += L".tmp";

                {
                    std::ofstream output{
                        temporaryPath,
                        std::ios::binary | std::ios::trunc };

                    if (!output) {
                        return false;
                    }

                    output.write(
                        data.data(),
                        static_cast<std::streamsize>(data.size()));

                    if (!output) {
                        output.close();
                        std::filesystem::remove(temporaryPath, error);
                        return false;
                    }
                }

                if (!::MoveFileExW(
                    temporaryPath.c_str(),
                    targetPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    std::filesystem::remove(temporaryPath, error);
                    return false;
                }

                return true;
            }
            catch (...) {
                return false;
            }
        }

        [[nodiscard]] bool HandleOldModWriteRequest(const ScaleformCall* call, const wchar_t* relativePath) noexcept
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

            call->result->SetBoolean(WriteFile(relativePath, data));
            return true;
        }

        // Add new __SFCodeObj.call("name", ...) handlers in this file:
        //   1. Implement a function with the ScaleformCallbackHandler signature below.
        //   2. Add one {"name", &Function} row to kScaleformCallbackHandlers.
        // ===========================================================================
        // CALLBACK HANDLERS

        /*
        bool __fastcall HandleWriteChatConfigFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kChatConfigPath);
        }
        */

        bool __fastcall HandleWriteBuffDataFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kBuffDataPath);
        }

        bool __fastcall HandleWriteChallengeDataFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kChallengeDataPath);
        }

        bool __fastcall HandleWritePerksFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kPerksPath);
        }

        bool __fastcall HandleWriteSaveEverythingFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kSaveEverythingPath);
        }

        bool __fastcall HandleWriteItemsModFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kItemsModPath);
        }

        bool __fastcall HandleWriteLegendaryModsFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kLegendaryModsPath);
        }

        bool __fastcall HandleWriteVendorLogFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kVendorLogPath);
        }

        bool __fastcall HandleWriteDPSMeterFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kDPSMeterPath);
        }

        bool __fastcall HandleWriteCampSearchConfigFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kCampSearchConfigPath);
        }

        bool __fastcall HandleWriteBlockFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kBlockFilePath);
        }

        bool __fastcall HandleWriteCampDataFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kCampDataPath);
        }

        bool __fastcall HandleWriteCharacterDataFile(
            const ScaleformCall* call,
            void*) noexcept
        {
            return HandleOldModWriteRequest(call, kCharacterDataPath);
        }

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

            call->result->SetBoolean(
                WriteFile(kIhbRelativePath, data));

            return true;
        }

        bool __fastcall HandleGetXScalRuntimeInfo(const ScaleformCall* call, void*) noexcept {
            if (call == nullptr || call->result == nullptr) return false;
            try {
                std::string runtime_info{ "{\"runtime\":\"xScal\",\"version\":\"" };
                runtime_info.append(config::kXScalVersion.data(), config::kXScalVersion.size());
                runtime_info += "\",\"platform\":\"";
                runtime_info += RuntimePlatformName();
                runtime_info += "\"}";
                return call->result->SetString(runtime_info);
            }
            catch (...) {
                return false;
            }
        }

        bool __fastcall HandleReadIhbData(const ScaleformCall* call, void*) noexcept {
            if (call == nullptr || call->result == nullptr) {
                return false;
            }

            call->result->SetBoolean(false);
            try {
                std::filesystem::path targetPath;
                if (!ResolvePath(kIhbRelativePath, targetPath)) {
                    return true;
                }

                std::ifstream input{ targetPath, std::ios::binary | std::ios::ate };
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
            }
            catch (...) {
                return true;
            }
        }


    }


    // REGISTRATION
    bool RegisterScaleformCallbacks(CallbackRegistry& registry) noexcept {
        bool registered_all = true;
        for (const auto& definition : kScaleformCallbackHandlers) {
            if (!registry.Register(definition.name, definition.callback, nullptr)) {
                registered_all = false;
            }
        }
        return registered_all;
    }
    void SetScaleformRuntimePlatform(TargetKind kind) noexcept {
        runtime_platform.store(kind, std::memory_order_release);
    }

}
