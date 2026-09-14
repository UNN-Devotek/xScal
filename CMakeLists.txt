cmake_minimum_required(VERSION 3.20)

project(xscal LANGUAGES CXX)

option(XSCAL_BUILD_FCM_CONTRACT_TESTS "Build portable FCM chat/input contract tests" ON)
option(XSCAL_BUILD_NATIVE "Build the Windows x64 dxgi proxy" ${WIN32})

if(XSCAL_BUILD_FCM_CONTRACT_TESTS)
    enable_testing()
    add_library(xscal_fcm_contract STATIC src/fcm/fcm_contract.cpp)
    target_compile_features(xscal_fcm_contract PUBLIC cxx_std_20)
    target_include_directories(xscal_fcm_contract PUBLIC src)
    add_executable(xscal_fcm_contract_tests tests/fcm_contract_tests.cpp)
    target_link_libraries(xscal_fcm_contract_tests PRIVATE xscal_fcm_contract)
    add_test(NAME xscal_fcm_contract_tests COMMAND xscal_fcm_contract_tests)
endif()

if(NOT XSCAL_BUILD_NATIVE)
    return()
endif()

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "xscal native runtime requires a Windows x64 toolchain")
endif()

enable_language(ASM_MASM)

set(XSCAL_CORE_SOURCES
    src/api/scaleform_callbacks.cpp
    src/api/callback_registry.cpp
    src/platform/diagnostics.cpp
    src/hook/path_router.cpp
    src/hook/movie_root_hook.cpp
    src/platform/windows_memory.cpp
    src/runtime/runtime.cpp
    src/scaleform/bridge.cpp
    src/scaleform/native_handler.cpp
    src/config/target_profile.cpp
    src/hook/vtable_hook.cpp
)

add_library(dxgi SHARED
    ${XSCAL_CORE_SOURCES}
    src/dllmain.cpp
    src/dxgi_proxy.cpp
    src/dxgi_proxy.asm
    src/dxgi_proxy.def
)

target_compile_features(dxgi PRIVATE cxx_std_20)
target_compile_definitions(dxgi PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_include_directories(dxgi PRIVATE src)
target_link_libraries(dxgi PRIVATE version)
set_target_properties(dxgi PROPERTIES PREFIX "" OUTPUT_NAME "dxgi")

set(XSCAL_BUILD_TESTS_DEFAULT OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_main.cpp")
    set(XSCAL_BUILD_TESTS_DEFAULT ON)
endif()
option(XSCAL_BUILD_TESTS "Build the local xScal test suite" ${XSCAL_BUILD_TESTS_DEFAULT})

if(XSCAL_BUILD_TESTS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_main.cpp")
        message(FATAL_ERROR "XSCAL_BUILD_TESTS requires the local tests directory")
    endif()

    add_executable(xscal_tests
        ${XSCAL_CORE_SOURCES}
        tests/movie_root_hook_tests.cpp
        tests/runtime_tests.cpp
        tests/scaleform_bridge_tests.cpp
        tests/vtable_hook_tests.cpp
        tests/test_main.cpp
    )

    target_compile_features(xscal_tests PRIVATE cxx_std_20)
    target_compile_definitions(xscal_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(xscal_tests PRIVATE src)
    target_link_libraries(xscal_tests PRIVATE version)

    add_test(NAME xscal_tests COMMAND xscal_tests)
endif()
