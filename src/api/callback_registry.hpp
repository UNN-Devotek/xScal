#pragma once

#include <cstddef>

#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sf {

enum class ScaleformResultKind {
    Undefined,
    Boolean,
    String,
};

struct ScaleformResult final {
    ScaleformResultKind kind{ScaleformResultKind::Undefined};
    bool boolean_value{};
    std::string string_value;

    void SetBoolean(bool value) noexcept {
        boolean_value = value;
        kind = ScaleformResultKind::Boolean;
    }

    [[nodiscard]] bool SetString(std::string_view value) noexcept {
        try {
            string_value.assign(value);
            kind = ScaleformResultKind::String;
            return true;
        } catch (...) {
            return false;
        }
    }
};

// Internal view of the recovered ActionScript argument array.
struct ScaleformCall final {
    const void* arguments{};
    std::size_t argument_count{};
    ScaleformResult* result{};
};

using ScaleformCallback = bool(__fastcall*)(const ScaleformCall* call, void* user_data);

class CallbackRegistry final {
public:
    [[nodiscard]] bool Register(
        std::string_view name,
        ScaleformCallback callback,
        void* user_data) noexcept;
    [[nodiscard]] bool Unregister(std::string_view name) noexcept;
    [[nodiscard]] bool Dispatch(std::string_view name, const ScaleformCall& call) const noexcept;

private:
    struct Entry final {
        ScaleformCallback callback{};
        void* user_data{};
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};

// Shared by the source-defined callbacks and the runtime MovieRoot controller.
[[nodiscard]] CallbackRegistry& GlobalCallbackRegistry() noexcept;

}
