# Internal cheat for RAGE Multiplayer: a DLL that is manually mapped into the
# game and draws its overlay from a hooked D3D11 Present.
#
# This is the user flavour. It writes no log and produces no dump; see
# src/SDK/RageBuildFlavor.h for what that means and why the flags are pinned.

cmake_minimum_required(VERSION 3.21)

if(NOT WIN32)
    message(FATAL_ERROR "The internal cheat is Windows only.")
endif()

if(NOT PROJECT_NAME)
    project(shadow_internal LANGUAGES CXX C)
endif()

file(GLOB_RECURSE SHADOW_INTERNAL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# Off for anything a user ever runs: it writes a crash log to disk, which the
# shipping payload must never do. This is the switch Hooks.h already checks to
# un-stub HLog, so turning it on lights up the breadcrumbs that are already in
# the code and points them at src/Main/Diagnostics.cpp. Deliberately not
# RAGEMP_DEV_BUILD, which gates development-tree code that does not exist here.
option(SHADOW_INTERNAL_DIAGNOSTICS
    "Write a crash log and record exception addresses" OFF)


# MinHook is vendored: it is three C files plus the length disassembler, and
# fetching it at configure time would make an offline build impossible.
set(SHADOW_MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook")
set(SHADOW_MINHOOK_SOURCES
    "${SHADOW_MINHOOK_DIR}/src/buffer.c"
    "${SHADOW_MINHOOK_DIR}/src/hook.c"
    "${SHADOW_MINHOOK_DIR}/src/trampoline.c"
    "${SHADOW_MINHOOK_DIR}/src/hde/hde64.c")

add_library(internal SHARED
    ${SHADOW_INTERNAL_SOURCES}
    ${SHADOW_MINHOOK_SOURCES}
    # The preview mesh is several megabytes; carried as RCDATA because a
    # constexpr array that size runs the compiler out of memory.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Features/ModelPreviewMesh.rc")

target_compile_features(internal PRIVATE cxx_std_17)

target_include_directories(internal PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/thirdparty"
    "${SHADOW_MINHOOK_DIR}/include"
    "${SHADOW_MINHOOK_DIR}/src")

target_compile_definitions(internal PRIVATE
    _WINDOWS
    _USRLIB
    NDEBUG
    # The code calls the wide Win32 entry points directly and passes macros
    # such as RT_RCDATA and IDC_ARROW straight through, so the wide mapping has
    # to be on or those macros resolve to their ANSI form and fail to compile.
    UNICODE
    _UNICODE)

if(SHADOW_INTERNAL_DIAGNOSTICS)
    target_compile_definitions(internal PRIVATE RAGEMP_DIAGNOSTICS)
endif()


target_compile_options(internal PRIVATE
    /utf-8
    /W3
    # Structured exception handling is used to survive faults inside the
    # game's own call stack, which needs asynchronous unwinding.
    /EHa
    /wd4996
    /wd26454
    /wd26439
    /wd26495
    /wd26800)

target_link_libraries(internal PRIVATE
    d3d11
    dxgi
    d3dcompiler
    psapi
    winhttp
    windowscodecs
    # Named explicitly rather than left to the default library directives: the
    # static CRT is split across three libraries and the C runtime half is not
    # pulled in on its own, which shows up as unresolved strtoul and
    # __stdio_common_vsprintf_s.
    libcmt
    libucrt
    libvcruntime
    legacy_stdio_definitions)

# Entry point: the CRT's own startup stub, which initialises the static CRT and
# then calls DllMain with the same three arguments.
#
# This used to be /ENTRY:DllMain, on the reasoning that a manually mapped image
# has no CRT startup stub. The static CRT does contain one, and skipping it left
# the UCRT uninitialised, which was not a theoretical problem: measured in game,
# the render thread took an access violation inside ntdll writing to 0x24 with
# internal_get_ptd_head_slow and __acrt_lock_table on its stack. That is the CRT
# trying to build per-thread data through a lock that was never constructed, and
# it killed the game a couple of seconds later every single time RAGE's V8
# runtime came up. Entering here fixed that and, with it, RP name and identity
# resolution, which had been reporting zeroes.
target_link_options(internal PRIVATE
    /ENTRY:_DllMainCRTStartup
    /SUBSYSTEM:WINDOWS)

set_target_properties(internal PROPERTIES
    OUTPUT_NAME "shadow_internal"
    PREFIX ""
    C_STANDARD 11
    # Pinned rather than inherited: the repository root asks for C++20 and this
    # code is written against 17, where it is known to compile.
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    # Manual mapping bypasses the Windows loader, so nothing guarantees a CRT
    # DLL is resolvable in the target process: the runtime is linked in.
    MSVC_RUNTIME_LIBRARY "MultiThreaded")

# The no-driver delivery path: a usermode manual-map injector that loads
# shadow_internal.dll into GTA5.exe without touching the kernel.
add_executable(shadow_injector
    "${CMAKE_CURRENT_SOURCE_DIR}/injector/main.cpp")

target_compile_features(shadow_injector PRIVATE cxx_std_17)

target_compile_definitions(shadow_injector PRIVATE UNICODE _UNICODE)

target_link_libraries(shadow_injector PRIVATE advapi32)

set_target_properties(shadow_injector PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded")
