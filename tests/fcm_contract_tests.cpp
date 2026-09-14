#include "fcm/fcm_contract.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace sf::fcm;
    Check(kRequiredChatMethods[0] == "connect", "required method order");
    Check(kInputCallbacks[1] == "Input.IsKeyPressed", "input callback spelling");

    InputContract input;
    Check(!input.RegisterKey(0), "reject VK zero");
    Check(input.RegisterKey(0x2D), "register Insert");
    Check(!input.RegisterKey(0x2D), "duplicate registration is explicit false");
    Check(input.SetKeyPressed(0x2D, true), "set registered key");
    Check(input.IsKeyPressed(0x2D), "read pressed key");
    Check(input.UnregisterKey(0x2D), "unregister Insert");
    Check(!input.IsKeyPressed(0x2D), "unregister clears pressed state");

    DeterministicChatContract chat;
    Check(chat.Send("global", "blocked").empty(), "send requires authentication");
    Check(chat.Connect() && chat.IsAuthenticated(), "connect authenticates fixture");
    chat.Queue({"m-1", "global", "VaultTester", "https://example.com", ""});
    chat.Queue({"m-2", "events", "EventBot", "https://discord.com/events/123/456", ""});
    const auto first = chat.Poll(0, 1);
    Check(first.success && first.cursor == 1 && first.events.size() == 1, "bounded first poll");
    const auto second = chat.Poll(first.cursor, 10);
    Check(second.cursor == 2 && second.events.size() == 1, "cursor advances deterministically");
    Check(chat.Send("global", "hello") == "sim-send-1", "deterministic send id");
    chat.Disconnect();
    Check(!chat.Poll(second.cursor, 10).success, "disconnected poll fails closed");
    std::cout << "xScal FCM contract tests passed\n";
}
