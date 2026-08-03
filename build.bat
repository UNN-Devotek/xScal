@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

if defined VSCMD_VER if defined UniversalCRTSdkDir if defined UCRTVersion if exist "%UniversalCRTSdkDir%Lib\%UCRTVersion%\ucrt\x64\ucrt.lib" goto toolchain_ready

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto toolchain_missing

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL goto toolchain_missing

set "WINDOWS_SDK_LIB=%ProgramFiles(x86)%\Windows Kits\10\Lib"
for /f "delims=" %%I in ('dir /b /ad /o-n "%WINDOWS_SDK_LIB%\10.*" 2^>nul') do (
    if not defined WINDOWS_SDK_VERSION (
        if exist "%WINDOWS_SDK_LIB%\%%I\ucrt\x64\ucrt.lib" if exist "%WINDOWS_SDK_LIB%\%%I\um\x64\kernel32.lib" set "WINDOWS_SDK_VERSION=%%I"
    )
)
if not defined WINDOWS_SDK_VERSION goto sdk_missing

set "VSDEVCMD=%VSINSTALL%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" goto toolchain_missing
call "%VSDEVCMD%" -arch=amd64 -host_arch=amd64 -winsdk=!WINDOWS_SDK_VERSION!
if errorlevel 1 exit /b 1

:toolchain_ready
where cmake >nul 2>nul
if errorlevel 1 goto cmake_missing
where nmake >nul 2>nul
if errorlevel 1 goto toolchain_missing

cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build build
if errorlevel 1 exit /b 1

ctest --test-dir build --output-on-failure
if errorlevel 1 exit /b 1

if not exist dist mkdir dist
copy /y "build\dxgi.dll" "dist\dxgi.dll" >nul
if errorlevel 1 exit /b 1

echo.
echo Build complete:
echo   build\dxgi.dll
echo   dist\dxgi.dll
exit /b 0

:toolchain_missing
echo Error: an x64 Visual C++ toolchain was not found.
echo Install Visual Studio Build Tools with the C++ workload,
echo or run this script from an x64 Visual Studio developer prompt.
exit /b 1

:sdk_missing
echo Error: no complete Windows SDK with x64 UCRT and UM libraries was found.
exit /b 1

:cmake_missing
echo Error: CMake was not found on PATH.
exit /b 1