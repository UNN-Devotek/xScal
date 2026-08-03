#include "api/callback_registry.hpp"

#include <mutex>

namespace sf {

bool CallbackRegistry::Register(
    std::string_view name,
    ScaleformCallback callback,
    void* user_data) noexcept {
    if (name.empty() || callback == nullptr) {
        return false;
    }

    try {
        std::unique_lock lock(mutex_);
        entries_.insert_or_assign(std::string{name}, Entry{callback, user_data});
        return true;
    } catch (...) {
        return false;
    }
}

bool CallbackRegistry::Unregister(std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }

    try {
        std::unique_lock lock(mutex_);
        return entries_.erase(std::string{name}) != 0;
    } catch (...) {
        return false;
    }
}

bool CallbackRegistry::Dispatch(std::string_view name, const ScaleformCall& call) const noexcept {
    if (name.empty()) {
        return false;
    }

    try {
        Entry entry;
        {
            std::shared_lock lock(mutex_);
            const auto it = entries_.find(std::string{name});
            if (it == entries_.end()) {
                return false;
            }
            entry = it->second;
        }
        return entry.callback(&call, entry.user_data);
    } catch (...) {
        return false;
    }
}

CallbackRegistry& GlobalCallbackRegistry() noexcept {
    static CallbackRegistry registry;
    return registry;
}

}

