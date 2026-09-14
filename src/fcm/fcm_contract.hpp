#pragma once

#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sf::fcm {

inline constexpr std::string_view kRuntimeName{"xScal Chat"};
inline constexpr std::string_view kCapability{"xscal-chat-interface"};
inline constexpr std::string_view kGenericRuntimeCallback{"GetXSRuntimeInfo"};

inline constexpr std::string_view kRequiredChatMethods[]{
    "connect", "pollEvents", "sendMessage"
};

inline constexpr std::string_view kOptionalChatMethods[]{
    "getRuntimeInfo", "getAuthState", "getConnectionState", "reportMessage",
    "disconnect", "clearChatAuth"
};

inline constexpr std::string_view kInputCallbacks[]{
    "Input.RegisterKey", "Input.IsKeyPressed", "Input.UnregisterKey"
};

struct ChatEvent final {
    std::string messageId;
    std::string channel;
    std::string displayName;
    std::string body;
    std::string targetUserId;
};

struct PollResult final {
    bool success{true};
    std::uint64_t cursor{};
    std::vector<ChatEvent> events;
};

class InputContract final {
public:
    [[nodiscard]] bool RegisterKey(int virtualKey);
    [[nodiscard]] bool IsKeyPressed(int virtualKey) const;
    [[nodiscard]] bool UnregisterKey(int virtualKey);
    [[nodiscard]] bool SetKeyPressed(int virtualKey, bool pressed);
    [[nodiscard]] std::size_t RegisteredCount() const noexcept;

private:
    static bool IsValidVirtualKey(int virtualKey) noexcept;
    std::set<int> registered_;
    std::set<int> pressed_;
};

class DeterministicChatContract final {
public:
    [[nodiscard]] bool Connect();
    [[nodiscard]] bool IsAuthenticated() const noexcept;
    void Queue(ChatEvent event);
    [[nodiscard]] PollResult Poll(std::uint64_t afterCursor, std::size_t limit);
    [[nodiscard]] std::string Send(std::string_view channel, std::string_view body);
    void Disconnect() noexcept;

private:
    bool authenticated_{};
    std::uint64_t cursor_{};
    std::uint64_t sent_{};
    std::deque<ChatEvent> queue_;
};

} // namespace sf::fcm
