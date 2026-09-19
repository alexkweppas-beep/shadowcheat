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

enable_language(ASM_MASM)

# Off for anything a user ever runs: development tooling writes a crash log,
# profiler reports and an event log to disk, which the shipping payload must
# never do. This is the switch Hooks.h already checks to un-stub HLog, so
# turning it on lights up the breadcrumbs that are already in the code.
#
# The implementations live under src/Dev and are excluded rather than merely
# stubbed, so that directory can be absent entirely. That is what makes the user
# tree publishable: with the switch off the build does not reference it, and the
# headers compile the entry points down to inline no-ops.
option(SHADOW_DEV
    "Build development tooling: crash log, profiler, event logger" OFF)

# The event logger alone, so a developer build can drop the heaviest and most
# invasive piece while keeping the crash log and the profiler.
option(SHADOW_EVENT_LOGGER
    "Instrument the game's V8 and write an event log" ON)

# Distribution builds are protected after link. The SDK markers alone are not
# protection: they are ordinary imports until VMProtect_Con consumes them, so
# the post-build step is fail-closed and never leaves a raw DLL under the
# shipping filename.
option(SHADOW_ENABLE_VMP
    "Protect Release shadow_internal.dll with VMProtect" ON)
option(SHADOW_KEEP_VMP_RAW
    "Keep shadow_internal.raw.dll beside the protected artifact" ON)
set(SHADOW_VMP_ROOT "" CACHE PATH
    "VMProtect directory containing VMProtect_Con.exe, Include/C and Lib/Windows")
set(SHADOW_VMP_WATERMARK "" CACHE STRING
    "Optional existing VMProtect watermark name")

if(SHADOW_ENABLE_VMP)
    set(SHADOW_VMP_ROOT "C:/Users/Vlad/Desktop/vmp 3.9.4 ultimate")
    set(SHADOW_VMP_CONSOLE
        "${SHADOW_VMP_ROOT}/VMProtect_Con.exe")
    set(SHADOW_VMP_INCLUDE
        "${SHADOW_VMP_ROOT}/Include/C")
    set(SHADOW_VMP_LIBRARY
        "${SHADOW_VMP_ROOT}/Lib/Windows/VMProtectSDK64.lib")
    foreach(_required
            "${SHADOW_VMP_CONSOLE}"
            "${SHADOW_VMP_INCLUDE}/VMProtectSDK.h"
            "${SHADOW_VMP_LIBRARY}")
        if(NOT EXISTS "${_required}")
            message(FATAL_ERROR
                "SHADOW_ENABLE_VMP=ON, but VMProtect file is missing: "
                "${_required}. Set -DSHADOW_VMP_ROOT=... or explicitly use "
                "-DSHADOW_ENABLE_VMP=OFF for an unprotected dev build.")
        endif()
    endforeach()
endif()

file(GLOB_RECURSE SHADOW_INTERNAL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
if(SHADOW_DEV)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/Dev")
        message(FATAL_ERROR
            "SHADOW_DEV is ON but src/Dev is missing. This tree is the user "
            "build; configure with -DSHADOW_DEV=OFF.")
    endif()
else()
    list(FILTER SHADOW_INTERNAL_SOURCES EXCLUDE REGEX "/src/Dev/")
endif()
list(FILTER SHADOW_INTERNAL_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")


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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Security/ida_cloak.asm"
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
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    JM_XORSTR_DISABLE_AVX_INTRINSICS)

if(SHADOW_ENABLE_VMP)
    target_include_directories(internal PRIVATE "${SHADOW_VMP_INCLUDE}")
    target_compile_definitions(internal PRIVATE
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:SHADOW_PACKER=1>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:SHADOW_HAVE_VMP_SDK=1>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:SHADOW_PROTECTED_RELEASE=1>")
    target_link_libraries(internal PRIVATE
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:${SHADOW_VMP_LIBRARY}>")
endif()

if(SHADOW_DEV)
    target_compile_definitions(internal PRIVATE RAGEMP_DIAGNOSTICS)
    if(NOT SHADOW_EVENT_LOGGER)
        target_compile_definitions(internal PRIVATE RAGEMP_EVENT_LOGGER_OFF)
    endif()
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

if(MSVC)
    target_compile_options(internal PRIVATE
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/O2>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Ob2>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/GL>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Gy>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Gw>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/GS>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/sdl>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Zc:inline>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Brepro>")
endif()

target_link_libraries(internal PRIVATE
    d3d11
    dxgi
    d3dcompiler
    psapi
    winhttp
    crypt32
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
    /SUBSYSTEM:WINDOWS
    # Never embed the absolute PDB path into the binary: it leaks the build
    # machine's layout and username. DllMain still wipes the RSDS record at
    # runtime as a second line for configs that link with /DEBUG.
    /PDBALTPATH:shadow.pdb
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/LTCG>"
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/OPT:REF>"
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/OPT:ICF>"
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/INCREMENTAL:NO>"
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/DYNAMICBASE>"
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/HIGHENTROPYVA>"
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/NXCOMPAT>"
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Brepro>"
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/RELEASE>")

if(SHADOW_ENABLE_VMP)
    set(_shadow_internal_output_name "shadow_internal.raw")
else()
    set(_shadow_internal_output_name "shadow_internal")
endif()

set_target_properties(internal PROPERTIES
    # The linker never writes the shipping filename when VMProtect is enabled.
    # A failed protection step therefore cannot leave a naked DLL that looks
    # like a valid distribution artifact.
    OUTPUT_NAME "${_shadow_internal_output_name}"
    PREFIX ""
    C_STANDARD 11
    # Pinned rather than inherited: the repository root asks for C++20 and this
    # code is written against 17, where it is known to compile.
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    # Manual mapping bypasses the Windows loader, so nothing guarantees a CRT
    # DLL is resolvable in the target process: the runtime is linked in.
    MSVC_RUNTIME_LIBRARY "MultiThreaded")

if(SHADOW_ENABLE_VMP)
    add_custom_command(TARGET internal POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=$<TARGET_FILE:internal>"
            "-DOUTPUT=$<TARGET_FILE_DIR:internal>/shadow_internal.dll"
            "-DCONFIG=$<CONFIG>"
            "-DVMP_CONSOLE=${SHADOW_VMP_CONSOLE}"
            "-DVMP_PROJECT=${CMAKE_CURRENT_SOURCE_DIR}/protection/shadow_internal.vmp"
            "-DVMP_WATERMARK=${SHADOW_VMP_WATERMARK}"
            "-DKEEP_RAW=${SHADOW_KEEP_VMP_RAW}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunVMProtect.cmake"
        COMMENT "Protecting shadow_internal.dll with VMProtect"
        VERBATIM)
endif()

# The no-driver delivery path: a usermode manual-map injector that loads
# shadow_internal.dll into GTA5.exe without touching the kernel.
add_executable(shadow_injector
    "${CMAKE_CURRENT_SOURCE_DIR}/injector/main.cpp")

target_compile_features(shadow_injector PRIVATE cxx_std_17)

target_compile_definitions(shadow_injector PRIVATE UNICODE _UNICODE)

target_link_libraries(shadow_injector PRIVATE advapi32)

set_target_properties(shadow_injector PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded")
