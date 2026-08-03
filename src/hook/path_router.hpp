#pragma once

#include <string_view>

namespace sf::routing {

[[nodiscard]] bool IsObjectAlias(std::string_view path) noexcept;
[[nodiscard]] bool IsDirectCallAlias(std::string_view path) noexcept;
[[nodiscard]] bool ShouldEnsureRootBridge(std::string_view path) noexcept;

}