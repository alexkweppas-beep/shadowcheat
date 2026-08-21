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

# DllMain runs on the thread the mapper hijacks, so it is the entry point
# directly: there is no CRT startup stub in a manually mapped image.
target_link_options(internal PRIVATE
    /ENTRY:DllMain
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
