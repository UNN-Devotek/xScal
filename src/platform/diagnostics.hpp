#pragma once

#include "hook/vtable_hook.hpp"

#include <Windows.h>

namespace sf {

void SetDiagnosticModule(HMODULE module) noexcept;
void ClearDiagnosticLog() noexcept;
void DiagnosticLog(const char* message) noexcept;
void DiagnosticLogFormat(const char* format, ...) noexcept;
[[nodiscard]] const char* VtableHookStatusName(VtableHookStatus status) noexcept;

}