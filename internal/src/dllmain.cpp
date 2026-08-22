/*
 * RAGEMP.DLL - Internal cheat with ImGui menu.
 *
 * CRITICAL DESIGN CONSTRAINT:
 *   CreateThread / NtCreateThreadEx is NOT safe in this target — the
 *   EAC EOS kernel minifilter kills newly-created threads at the
 *   LdrInitializeThunk DLL_THREAD_ATTACH stage. We measured this:
 *   CreateThread returned a valid handle, but the worker function's
 *   first instruction never executed.
 *
 *   Therefore we initialize the D3D11 Present VTable hook SYNCHRONOUSLY
 *   inside DllMain. DllMain runs on the hijacked GTA5 thread (which is
 *   spinning in the syscall-return path of NtWaitForSingleObject), so
 *   we install the hook, return, and let the thread continue its normal
 *   execution. On the next frame, the render thread calls Present()
 *   and our hook fires.
 *
 * CRT handling: This DLL is manual-mapped (no LoadLibrary), so startup
 *   code must avoid relying on C++ dynamic initializers. Lumin singletons
 *   and embedded fonts are lazy/POD-backed, and ImGui uses a private heap
 *   via SetAllocatorFunctions.
 *
 *   The image is entered through _DllMainCRTStartup rather than here, so the
 *   static CRT does initialise; see the entry point comment in CMakeLists.txt
 *   for what skipping it cost. The heap overrides below are kept anyway: they
 *   are correct either way and route every allocation through the process heap.
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <malloc.h>
#include <new>

#include "Main/Diagnostics.h"
#include "Main/Hooks.h"
#include "Features/CloudStorage.h"
#include "Features/ModelPreview.h"
#include "imgui.h"

// Module handle of this DLL (set in DllMain).
HMODULE g_ThisModule = nullptr;

// SizeOfImage captured before the header wipe below. FeatureBinds::StableId
// uses it to range-check pointers without reading the (zeroed) header.
uint32_t g_ThisModuleSize = 0;

// ===================================================================
// Heap strategy for C++ allocations.
//
// We override operator new/delete to route through GetProcessHeap()
// via HeapAlloc/HeapFree. This is required because the DLL is manual-
// mapped: _DllMainCRTStartup never runs, so UCRT's _crtheap / _malloc_base
// are not initialized — resolving malloc/free resolves to UCRT stubs
// that dereference an uninitialized _crtheap (NULL) and crash.
//
// Routing through GetProcessHeap() satisfies CRT heap validation:
// ImGui's own MallocWrapper (patched in imgui.cpp) also uses
// GetProcessHeap(), and UCRT's metadata checks pass because every
// block carries real HeapAlloc headers. This eliminates the
// __fastfail(0xc0000409) heap-corruption abort that killed ragemp_v.exe
// after ~100 min in the previous build.
//
// ImGui's per-frame allocations are also routed through GetProcessHeap()
// (we do NOT set a private allocator) — UI frame volume is small enough
// that heap-lock contention is negligible.
// ===================================================================
#pragma region Heap Overrides (GetProcessHeap - required for manual-mapped DLL)

void* operator new(size_t sz) {
    return HeapAlloc(GetProcessHeap(), 0, sz ? sz : 1);
}
void* operator new[](size_t sz) {
    return HeapAlloc(GetProcessHeap(), 0, sz ? sz : 1);
}
void operator delete(void* ptr) noexcept {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}
void operator delete[](void* ptr) noexcept {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}
void operator delete(void* ptr, size_t) noexcept {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}
void operator delete[](void* ptr, size_t) noexcept {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

#pragma endregion

HANDLE g_Heap = nullptr;
HANDLE g_PrivateHeap = nullptr;
static PRUNTIME_FUNCTION g_RuntimeFunctionTable = nullptr;

static bool RegisterMappedExceptionTable(HMODULE module)
{
    if (!module)
        return false;

    const auto* base = reinterpret_cast<const BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew <= 0) {
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const auto& exceptionDirectory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!exceptionDirectory.VirtualAddress ||
        exceptionDirectory.Size < sizeof(RUNTIME_FUNCTION) ||
        static_cast<ULONGLONG>(exceptionDirectory.VirtualAddress) +
                exceptionDirectory.Size >
            nt->OptionalHeader.SizeOfImage ||
        exceptionDirectory.Size % sizeof(RUNTIME_FUNCTION) != 0) {
        return false;
    }

    g_RuntimeFunctionTable = reinterpret_cast<PRUNTIME_FUNCTION>(
        const_cast<BYTE*>(
            base + exceptionDirectory.VirtualAddress));
    return RtlAddFunctionTable(
               g_RuntimeFunctionTable,
               exceptionDirectory.Size / sizeof(RUNTIME_FUNCTION),
               reinterpret_cast<DWORD64>(module)) != FALSE;
}

// ImGui allocator functions exposed to hooks namespace.
namespace hooks {
    void* ImHeapAllocFn(size_t sz, void*) {
        return HeapAlloc(GetProcessHeap(), 0, sz);
    }
    void ImHeapFreeFn(void* ptr, void*) {
        if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
    }
}

// There is no log sink in this build, by construction rather than by a
// disabled flag: the payload must leave nothing on the user's disk, so the
// file writer does not exist here at all and hooks::HLog has nowhere to go.
// Diagnostics stay in the development tree.

// Manual mapping bypasses the loader, so nothing stops the same payload being
// mapped into one process twice: injecting again while the game is running
// leaves both copies live, each with its own Present hook, its own ESP pass and
// its own independent feature state. The visible result is two overlays drawing
// the same peds and disagreeing about them, which reads as flicker rather than
// as the obvious duplicate it is. Later arrivals bail out of DllMain instead.
//
// The name carries the process id because the object namespace is per session,
// not per process: without it, a second game would refuse to be injected at all.
// The handle is deliberately leaked, since the claim has to outlive DllMain and
// the image is never unmapped.
static HANDLE g_InstanceClaim = nullptr;

static bool ClaimSoleInstance() noexcept
{
    wchar_t name[64]{};
    const unsigned long pid = GetCurrentProcessId();
    wsprintfW(name, L"Local\\shadow_internal_%lu", pid);
    const HANDLE claim = CreateMutexW(nullptr, FALSE, name);
    if (!claim)
        return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(claim);
        return false;
    }
    g_InstanceClaim = claim;
    return true;
}

// Called from the unload path. Once the hooks are off the image is dormant and
// owns nothing, so holding the claim would only stop a fresh copy from being
// injected in its place, which is exactly what the unload key exists to allow.
void ReleaseSoleInstanceClaim() noexcept
{
    if (!g_InstanceClaim)
        return;
    CloseHandle(g_InstanceClaim);
    g_InstanceClaim = nullptr;
}

// -------------------------------------------------------------------
// Entry point — called by manual-mapper thunk SYNCHRONOUSLY on the
// hijacked GTA5 thread.
//   We install the D3D11 Present VTable hook here. The hook itself
//   runs on the next frame, on the game's render thread.
// -------------------------------------------------------------------
extern "C" BOOL WINAPI DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        if (!ClaimSoleInstance())
            return FALSE;

        // Save module handle for later use (pattern scanning, etc.).
        g_ThisModule = hModule;

        hooks::g_LogFn = nullptr;

        // Consume cloud.session BEFORE NotifyInjectReady — InjectCore proceeds
        // to forensic wipe right after the ready event and would delete the
        // token file if we wait until later in this function.
        g_Heap = GetProcessHeap();
        g_PrivateHeap = g_Heap;
        CloudStorage::Initialize();

        hooks::NotifyInjectReady();

        // Manual mapping bypasses the Windows loader, so x64 unwind metadata
        // is not registered automatically. Register .pdata before any helper
        // uses __try/__except; otherwise even a guarded access violation is
        // process-fatal because the dispatcher cannot unwind this image.
        RegisterMappedExceptionTable(hModule);

        // After the unwind table, because the recorder reads this image's
        // header under __try, and before anything that touches game memory, so
        // the first fault is already being recorded. Compiled out entirely
        // unless the build defines RAGEMP_DIAGNOSTICS.
        Diagnostics::Initialize();

        // Capture SizeOfImage/SizeOfHeaders while the header is still intact;
        // both are zeroed below at the end of DllMain.
        uint32_t sizeOfHeaders = 0;
        {
            const auto* base = reinterpret_cast<const BYTE*>(hModule);
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0) {
                const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                    base + dos->e_lfanew);
                if (nt->Signature == IMAGE_NT_SIGNATURE) {
                    g_ThisModuleSize = nt->OptionalHeader.SizeOfImage;
                    sizeOfHeaders = nt->OptionalHeader.SizeOfHeaders;
                }
            }
        }

        // The kernel worker waits for all imported graphics/UI modules before
        // invoking DllMain. Keep a few short retries for the narrow window
        // where D3D11 is loaded but its swapchain/factory is still settling.
        // This is bounded to 400 ms instead of the old 15-second stall.
        bool hookInstalled = false;
        for (int attempt = 0; attempt < 5 && !hookInstalled; ++attempt) {
            hookInstalled = hooks::Initialize();
            if (!hookInstalled && attempt < 4)
                Sleep(100);
        }
        // Unblock loader as soon as the Present hook is in place.
        if (hookInstalled)
            hooks::NotifyInjectReady();

        // Prime the embedded-mesh resource cache: FindResourceW parses the PE
        // header, which is about to stop existing.
        ModelPreview::CacheEmbeddedMeshResource();

        // Wipe the PE header: a valid MZ/PE signature at the mapping base is
        // the loudest in-memory marker of an unlisted module. Everything that
        // needed the header (exception table registration, resource cache,
        // SizeOfImage) has already consumed it.
        if (sizeOfHeaders) {
            SecureZeroMemory(
                reinterpret_cast<void*>(hModule), sizeOfHeaders);
        }
    }
    return TRUE;
}
