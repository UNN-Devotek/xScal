#include "fcm/fcm_contract.hpp"

#include <algorithm>

namespace sf::fcm {

bool InputContract::IsValidVirtualKey(int virtualKey) noexcept {
    return virtualKey >= 1 && virtualKey <= 255;
}

bool InputContract::RegisterKey(int virtualKey) {
    return IsValidVirtualKey(virtualKey) && registered_.insert(virtualKey).second;
}

bool InputContract::IsKeyPressed(int virtualKey) const {
    return registered_.contains(virtualKey) && pressed_.contains(virtualKey);
}

bool InputContract::UnregisterKey(int virtualKey) {
    pressed_.erase(virtualKey);
    return registered_.erase(virtualKey) == 1;
}

bool InputContract::SetKeyPressed(int virtualKey, bool pressed) {
    if (!registered_.contains(virtualKey)) return false;
    if (pressed) pressed_.insert(virtualKey); else pressed_.erase(virtualKey);
    return true;
}

std::size_t InputContract::RegisteredCount() const noexcept {
    return registered_.size();
}

bool DeterministicChatContract::Connect() {
    authenticated_ = true;
    return true;
}

bool DeterministicChatContract::IsAuthenticated() const noexcept {
    return authenticated_;
}

void DeterministicChatContract::Queue(ChatEvent event) {
    queue_.push_back(std::move(event));
}

PollResult DeterministicChatContract::Poll(std::uint64_t afterCursor, std::size_t limit) {
    PollResult result;
    result.success = authenticated_;
    result.cursor = cursor_;
    if (!authenticated_ || afterCursor > cursor_) return result;
    limit = std::min(limit, queue_.size());
    while (result.events.size() < limit) {
        result.events.push_back(std::move(queue_.front()));
        queue_.pop_front();
        result.cursor = ++cursor_;
    }
    return result;
}

std::string DeterministicChatContract::Send(std::string_view channel, std::string_view body) {
    if (!authenticated_ || channel.empty() || body.empty() || body.size() > 500) return {};
    return "sim-send-" + std::to_string(++sent_);
}

void DeterministicChatContract::Disconnect() noexcept {
    authenticated_ = false;
}

} // namespace sf::fcm
