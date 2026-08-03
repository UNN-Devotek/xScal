#include "hook/path_router.hpp"

namespace sf::routing {
namespace {

[[nodiscard]] bool HasRootPrefix(std::string_view path) noexcept {
    return path.starts_with("root1.") || path.starts_with("root.") || path.starts_with("_root.");
}

}

bool IsObjectAlias(std::string_view path) noexcept {
    constexpr std::string_view suffix = ".__SFCodeObj";
    if (path == "__SFCodeObj" ||
        path == "root1.__SFCodeObj" ||
        path == "root1.FilterHolder_mc.Menu_mc.__SFCodeObj" ||
        path == "root1.Menu_mc.__SFCodeObj") {
        return true;
    }
    return HasRootPrefix(path) && path.size() > suffix.size() && path.ends_with(suffix);
}

bool IsDirectCallAlias(std::string_view path) noexcept {
    constexpr std::string_view call_suffix = ".call";
    return path.size() > call_suffix.size() && path.ends_with(call_suffix) &&
        IsObjectAlias(path.substr(0, path.size() - call_suffix.size()));
}

bool ShouldEnsureRootBridge(std::string_view path) noexcept {
    const bool root_qualified = path == "root1" || path.starts_with("root1.");
    return root_qualified && path.find("__SFCodeObj") == std::string_view::npos;
}

}