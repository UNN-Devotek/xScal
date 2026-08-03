#include <Windows.h>

#include <array>
#include <cstddef>

namespace {

INIT_ONCE system_dxgi_once = INIT_ONCE_STATIC_INIT;
HMODULE system_dxgi = nullptr;

BOOL CALLBACK LoadSystemDxgi(PINIT_ONCE, PVOID, PVOID*) noexcept {
    std::array<wchar_t, MAX_PATH> path{};
    const UINT length = ::GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
    constexpr wchar_t suffix[] = L"\\dxgi.dll";
    constexpr std::size_t suffix_length = (sizeof(suffix) / sizeof(suffix[0])) - 1;
    if (length == 0 || length >= path.size() ||
        suffix_length >= path.size() - length) {
        return FALSE;
    }

    for (std::size_t index = 0; index <= suffix_length; ++index) {
        path[length + index] = suffix[index];
    }
    system_dxgi = ::LoadLibraryW(path.data());
    return system_dxgi != nullptr;
}

}

extern "C" FARPROC __fastcall ResolveSystemDxgiExport(const char* name) noexcept {
    if (name == nullptr ||
        ::InitOnceExecuteOnce(&system_dxgi_once, &LoadSystemDxgi, nullptr, nullptr) == FALSE ||
        system_dxgi == nullptr) {
        return nullptr;
    }
    return ::GetProcAddress(system_dxgi, name);
}