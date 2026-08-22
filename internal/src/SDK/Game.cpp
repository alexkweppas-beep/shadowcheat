#include "Game.h"
#include <cstdint>
#include "RageBuildFlavor.h"
#include "../imgui/imgui.h"
#include "../Main/Hooks.h"
#include "../Features/RagePlayerNames.h"
#include "../Features/RuntimeProfiler.h"
#include "../Features/VisualPolicy.h"
#include "../Features/Visibility.h"

#include <TlHelp32.h>
#include <cmath>
#include <immintrin.h>

namespace {

// ---------------------------------------------------------------------------
// RP name resolver debug (C:\RAGEMP\cheat_log.txt via hooks::HLog)
// ---------------------------------------------------------------------------
struct NameResolveDbg {
    DWORD lastReportTick = 0;
    bool  modulesLogged  = false;
    bool  mpBaseLogged   = false;

    // ResolveRageMpNamesDirect counters (per reporting interval)
    uint32_t directRuns       = 0;
    uint32_t failNoMpDll      = 0;
    uint32_t failPoolArray    = 0;
    uint32_t failPoolCount    = 0;
    uint32_t entriesScanned   = 0;
    uint32_t skipNullEntry    = 0;
    uint32_t skipNullEntity   = 0;
    uint32_t skipNullPed      = 0;
    uint32_t skipBadName      = 0;
    uint32_t skipPedFilter    = 0;
    uint32_t namesCached      = 0;

    // Lookup counters
    uint32_t lookupMiss       = 0;
    uint32_t lookupHit        = 0;

    char lastFailStage[64]     = {};
    char lastRawName[40]       = {};
    char lastCachedName[40]    = {};
    uintptr_t lastEntry        = 0;
    uintptr_t lastEntity       = 0;
    uintptr_t lastPed          = 0;
    uintptr_t lastMpBase       = 0;
    uintptr_t lastPoolArray    = 0;
    uint32_t  lastPoolCount    = 0;
};

static NameResolveDbg s_nameDbg;

static void NameDbgLog(const char* msg)
{
    hooks::HLog(msg);
}

static void NameDbgSetFail(const char* stage)
{
    if (!stage)
        return;
    for (int i = 0; i < 63 && stage[i]; ++i)
        s_nameDbg.lastFailStage[i] = stage[i];
    s_nameDbg.lastFailStage[63] = 0;
}

static void NameDbgLogModulesOnce()
{
    if (s_nameDbg.modulesLogged)
        return;
    s_nameDbg.modulesLogged = true;

    NameDbgLog("Names: multiplayer.dll NOT found — listing loaded modules:");
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) {
        NameDbgLog("Names: CreateToolhelp32Snapshot FAILED");
        return;
    }

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    int listed = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            const wchar_t* n = me.szModule;
            if (wcsstr(n, L"multi") || wcsstr(n, L"rage") || wcsstr(n, L"RAGE") ||
                wcsstr(n, L"player") || wcsstr(n, L"MP")) {
                char buf[160];
                wsprintfA(buf, "Names: module %ls base=%p size=%lu",
                          n, me.modBaseAddr, me.modBaseSize);
                NameDbgLog(buf);
                ++listed;
                if (listed >= 24)
                    break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    if (listed == 0)
        NameDbgLog("Names: no multiplayer/rage-like modules in first scan");
}

static void NameDbgPreviewRawName(uintptr_t nameAddr)
{
    s_nameDbg.lastRawName[0] = 0;
    if (!nameAddr || !IsValidPtr(reinterpret_cast<const void*>(nameAddr)))
        return;
    __try {
        const char* s = reinterpret_cast<const char*>(nameAddr);
        for (int i = 0; i < 39; ++i) {
            char c = s[i];
            if (c == 0)
                break;
            if (c < 32 || c > 126)
                c = '?';
            s_nameDbg.lastRawName[i] = c;
            s_nameDbg.lastRawName[i + 1] = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        lstrcpynA(s_nameDbg.lastRawName, "<read fault>", 40);
    }
}

static void NameDbgReport(bool force = false); // defined after cache globals

static bool SafeReadByte(uintptr_t addr, uint8_t* out)
{
    __try {
        *out = *reinterpret_cast<const uint8_t*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadQword(uintptr_t addr, uintptr_t* out)
{
    __try {
        *out = *reinterpret_cast<const uintptr_t*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeResolveRagePool(uintptr_t* outPool)
{
    if (outPool)
        *outPool = 0;
    if (!outPool || !Offsets::RagePlayerPoolPtr || !IsValidPtr(Offsets::RagePlayerPoolPtr))
        return false;

    __try {
        const uint8_t* poolCell = *Offsets::RagePlayerPoolPtr;
        if (!IsValidPtr(poolCell))
            return false;
        *outPool = reinterpret_cast<uintptr_t>(poolCell);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outPool = 0;
        return false;
    }
}

static uint32_t SafeReadPoolMaxSlots(uintptr_t pool)
{
    uint32_t maxSlots = 226;
    __try {
        maxSlots = *reinterpret_cast<const uint32_t*>(pool + Offsets::RagePlayerPool::MaxCount);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        maxSlots = 226;
    }
    if (maxSlots == 0 || maxSlots > 256)
        maxSlots = 226;
    return maxSlots;
}

static CPlayerInfo* SafeGetPlayerInfo(const CPed* ped)
{
    if (!IsValidPtr(ped))
        return nullptr;

    __try {
        CPlayerInfo* pi = *(CPlayerInfo**)(reinterpret_cast<const uint8_t*>(ped) + Offsets::Ped::PlayerInfo);
        return IsValidPtr(pi) ? pi : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static CPed* SafeGetLocalPed()
{
    __try {
        if (!Offsets::WorldPtr || !IsValidPtr(Offsets::WorldPtr))
            return nullptr;
        const uint8_t* world = *Offsets::WorldPtr;
        if (!IsValidPtr(world))
            return nullptr;
        CPed* ped = *reinterpret_cast<CPed* const*>(world + 0x8);
        return IsValidPtr(ped) ? ped : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool IsLikelyPedPtr(uintptr_t p)
{
    return IsValidPtr(reinterpret_cast<const void*>(p));
}

// Pool slots sometimes hold pool/backref pointers (~0x05xxxxxx), not CPed*.
static bool IsPoolPlayerPed(uintptr_t p)
{
    if (!IsLikelyPedPtr(p))
        return false;
    if (p >= 0x04000000 && p < 0x08000000)
        return false;
    return true;
}

static bool IsScannableHeapRegion(const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    const DWORD prot = mbi.Protect & 0xFF;
    if (prot != PAGE_READWRITE && prot != PAGE_WRITECOPY &&
        prot != PAGE_EXECUTE_READWRITE && prot != PAGE_EXECUTE_WRITECOPY)
        return false;
    if (mbi.Type == MEM_IMAGE)
        return false;
    if (mbi.RegionSize == 0 || mbi.RegionSize > 64ull * 1024 * 1024)
        return false;
    return true;
}

static bool IsEntityBandAddress(uintptr_t a)
{
    // CE-verified RP entity cells live in low private heap (0x04..0x0A range).
    return a >= 0x04000000 && a < 0x0A000000;
}

static bool SafeProbeRead(uintptr_t addr, size_t size)
{
    if (addr < 0x10000)
        return false;
    __try {
        volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(addr);
        uint8_t v = p[size - 1];
        (void)v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool IsLikelyRpName(const char* s)
{
    if (!s || !s[0])
        return false;

    int len = 0;
    bool hasUnderscore = false;
    int digit = 0;
    for (int i = 0; i < Offsets::RagePlayerPool::NameMax; ++i) {
        char c = 0;
        __try {
            c = s[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        if (c == 0)
            break;
        if (c == '_') {
            hasUnderscore = true;
            ++len;
            continue;
        }
        if (c >= '0' && c <= '9') {
            ++digit;
            ++len;
            continue;
        }
        if (c < 32 || c > 126)
            return false;
        ++len;
    }

    // RAGE MP RP names follow the "FirstName_Surname" format — require
    // underscore to reject false positives (random printable ASCII bytes
    // elsewhere in heap that pass length/printable checks).
    if (len < 3 || len > 31)
        return false;
    if (!hasUnderscore)
        return false;
    return true;
}

static bool SafeReadRpName(uintptr_t nameAddr, char* out, int outSize)
{
    out[0] = 0;
    if (!nameAddr || outSize < 2 || !IsValidPtr(reinterpret_cast<const void*>(nameAddr)))
        return false;

    __try {
        const char* src = reinterpret_cast<const char*>(nameAddr);
        if (!IsLikelyRpName(src))
            return false;

        int n = 0;
        for (int i = 0; i < Offsets::RagePlayerPool::NameMax && n < outSize - 1; ++i) {
            const char c = src[i];
            if (c == 0)
                break;
            char ch = (c == '_') ? ' ' : c;
            out[n++] = ch;
        }
        out[n] = 0;
        return out[0] != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
        return false;
    }
}

static bool SafeReadRpNameField(uintptr_t fieldAddr, char* out, int outSize)
{
    if (!IsValidPtr(reinterpret_cast<const void*>(fieldAddr)))
        return false;

    uintptr_t namePtr = 0;
    if (SafeReadQword(fieldAddr, &namePtr) &&
        IsValidPtr(reinterpret_cast<const void*>(namePtr)) &&
        SafeReadRpName(namePtr, out, outSize)) {
        return true;
    }

    return SafeReadRpName(fieldAddr, out, outSize);
}

static bool SafeReadPoolSlotPed(uintptr_t slot, uintptr_t* outPed)
{
    if (outPed)
        *outPed = 0;
    return outPed && SafeReadQword(slot + Offsets::RagePlayerPool::SlotPed, outPed) &&
           IsPoolPlayerPed(*outPed);
}

struct PedNameCacheEntry {
    uintptr_t ped;
    char      name[48];
};

static PedNameCacheEntry s_pedNameCache[256];
static int   s_pedNameCacheCount = 0;

struct PedVisualCacheEntry {
    uintptr_t ped{};
    RagePlayerNames::SnapshotEntry snapshot{};
};

static PedVisualCacheEntry s_pedVisualCache[256]{};
static int s_pedVisualCacheCount = 0;
static int s_pedVisualHash[512]{};
struct DirectBoneCacheEntry final {
    uintptr_t ped{};
    LONG epoch{};
    DWORD tick{};
    std::uint32_t boneMask{};
    Vector3 rootPosition{};
    Vector3 bones[Game::kVisualBoneCount]{};
};
// The smoothing needs sub-frame resolution. GetTickCount64 moves in ~15.6 ms
// steps, so at 100 FPS several frames share a timestamp: the delta collapsed to
// nothing on those frames and then jumped, which the eye reads as stepping.
static double NowSeconds() noexcept
{
    static LARGE_INTEGER frequency{};
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return frequency.QuadPart
        ? static_cast<double>(counter.QuadPart) /
              static_cast<double>(frequency.QuadPart)
        : 0.0;
}

static DirectBoneCacheEntry s_directBoneCache[256]{};
static int s_directBoneCacheCount = 0;
static int s_directBoneCacheWrite = 0;
// Open-address index into the bone cache. The frame builder used to scan all
// 256 slots twice per ped, which is O(n * 256) once a crowd shows up.
static int s_directBoneHash[512]{};
static volatile LONG s_sessionCacheEpoch = 0;

struct WorldSmoothEntry final {
    uintptr_t ped{};
    LONG epoch{};
    std::uint32_t rageHandle{};
    std::uint16_t remoteId{};
    ULONGLONG updatedAt{};
    Vector3 root{};
    Vector3 bones[Game::kVisualBoneCount]{};
    std::uint32_t boneMask{};
    // When a crowd overflows the refresh budget the rig is not re-read, so the
    // pose is the same one translated to the new root. Splitting the pose off
    // the root makes a stale pose detectable — the offsets stop changing — and
    // lets it be carried forward along its own velocity instead of freezing.
    // Offsets are relative to the root, in metres.
    Vector3 poseOffset[Game::kVisualBoneCount]{};
    Vector3 poseVelocity[Game::kVisualBoneCount]{};
    // The live root that bones[] is currently expressed against. Every frame
    // the rig is rigidly translated by the change in this value before any
    // filtering runs, so the smoothing only ever has to close a *pose* error.
    // Filtering the world position instead put the whole skeleton a time
    // constant behind the body, which is what made it trail a sprinting ped.
    Vector3 poseAnchor{};
    // Seconds from the performance counter, not milliseconds from the tick
    // count: everything here is sub-frame timing.
    double updatedAtSec{};
    double poseAtSec{};
};

static WorldSmoothEntry s_worldSmooth[Game::kMaxFramePeds]{};
static int s_worldSmoothHash[512]{};

// Ingest scratch for picking the nearest peds. File scope on purpose: a static
// local with member initialisers would need a CRT thread guard, which a
// manually mapped image cannot rely on, and it would also block SEH here.
struct IngestRecord final
{
    CPed* ped;
    Vector3 position;
    float health;
    float distanceSquared;
};

static IngestRecord s_ingestHeap[Game::kMaxFramePeds]{};
static Game::FramePedContext s_frameContext{};
static std::uint32_t s_boneRoundRobin = 0;

struct ProjectionCache final {
    std::uint64_t frameId{};
    bool valid{};
    float view[16]{};
    float projection[16]{};
    float width{};
    float height{};
};

static ProjectionCache s_projectionCache{};

static void ClearPedVisualHash() noexcept
{
    for (int& value : s_pedVisualHash)
        value = 0;
}

static std::size_t PointerHashSlot(uintptr_t ped) noexcept
{
    return static_cast<std::size_t>(
        ((ped >> 4u) ^ (ped >> 17u)) & 511u);
}

static void ClearDirectBoneHash() noexcept
{
    for (int& value : s_directBoneHash)
        value = 0;
}

static void InsertDirectBoneHash(
    uintptr_t ped,
    int cacheIndex) noexcept
{
    std::size_t slot = PointerHashSlot(ped);
    for (int probe = 0; probe < 512; ++probe)
    {
        int& value = s_directBoneHash[slot];
        if (!value || value == cacheIndex + 1)
        {
            value = cacheIndex + 1;
            return;
        }
        slot = (slot + 1u) & 511u;
    }
}

// A recycled slot invalidates the old key, and open addressing cannot delete in
// place, so the index is rebuilt. Eviction only happens past 256 distinct peds.
static void RebuildDirectBoneHash() noexcept
{
    ClearDirectBoneHash();
    for (int index = 0;
         index < s_directBoneCacheCount;
         ++index)
    {
        if (!s_directBoneCache[index].ped)
            continue;
        InsertDirectBoneHash(
            s_directBoneCache[index].ped,
            index);
    }
}

static void ClearSmoothHash() noexcept
{
    for (int& value : s_worldSmoothHash)
        value = 0;
}

static void InsertSmoothHash(
    uintptr_t ped,
    int index) noexcept
{
    std::size_t slot = PointerHashSlot(ped);
    for (int probe = 0; probe < 512; ++probe)
    {
        int& value = s_worldSmoothHash[slot];
        if (!value || value == index + 1)
        {
            value = index + 1;
            return;
        }
        slot = (slot + 1u) & 511u;
    }
}

static void RebuildSmoothHash() noexcept
{
    ClearSmoothHash();
    for (int index = 0;
         index < Game::kMaxFramePeds;
         ++index)
    {
        if (!s_worldSmooth[index].ped)
            continue;
        InsertSmoothHash(
            s_worldSmooth[index].ped,
            index);
    }
}

static int LookupSmoothHash(uintptr_t ped) noexcept
{
    std::size_t slot = PointerHashSlot(ped);
    for (int probe = 0; probe < 512; ++probe)
    {
        const int value = s_worldSmoothHash[slot];
        if (!value)
            return -1;
        const int index = value - 1;
        if (index >= 0 &&
            index < Game::kMaxFramePeds &&
            s_worldSmooth[index].ped == ped)
            return index;
        slot = (slot + 1u) & 511u;
    }
    return -1;
}

static int LookupDirectBoneCache(uintptr_t ped) noexcept
{
    std::size_t slot = PointerHashSlot(ped);
    for (int probe = 0; probe < 512; ++probe)
    {
        const int value = s_directBoneHash[slot];
        if (!value)
            return -1;
        const int index = value - 1;
        if (index >= 0 &&
            index < s_directBoneCacheCount &&
            s_directBoneCache[index].ped == ped)
            return index;
        slot = (slot + 1u) & 511u;
    }
    return -1;
}

static std::size_t PedVisualHashSlot(
    uintptr_t ped) noexcept
{
    return static_cast<std::size_t>(
        ((ped >> 4u) ^ (ped >> 17u)) & 511u);
}

static void InsertPedVisualHash(
    uintptr_t ped,
    int cacheIndex) noexcept
{
    std::size_t slot = PedVisualHashSlot(ped);
    for (int probe = 0; probe < 512; ++probe)
    {
        int& value = s_pedVisualHash[slot];
        if (!value)
        {
            value = cacheIndex + 1;
            return;
        }
        slot = (slot + 1u) & 511u;
    }
}

static int LookupPedVisualHash(
    uintptr_t ped) noexcept
{
    std::size_t slot = PedVisualHashSlot(ped);
    for (int probe = 0; probe < 512; ++probe)
    {
        const int value = s_pedVisualHash[slot];
        if (!value)
            return -1;
        const int index = value - 1;
        if (index >= 0 &&
            index < s_pedVisualCacheCount &&
            s_pedVisualCache[index].ped == ped)
            return index;
        slot = (slot + 1u) & 511u;
    }
    return -1;
}

static uintptr_t s_playerPeds[256];
static int       s_playerPedCount = 0;

static void NameDbgReport(bool force)
{
    const DWORD now = GetTickCount();
    if (!force && now - s_nameDbg.lastReportTick < 5000)
        return;
    s_nameDbg.lastReportTick = now;

    char buf[256];
    wsprintfA(buf,
        "Names: REPORT directRuns=%u cached=%u poolPeds=%d "
        "nameCache=%d apiNames=%d "
        "scan(entry=%u nullEnt=%u nullPed=%u badName=%u pedFilter=%u) "
        "lookup(hit=%u miss=%u)",
        s_nameDbg.directRuns, s_nameDbg.namesCached,
        s_playerPedCount, s_pedNameCacheCount,
        RagePlayerNames::Count(),
        s_nameDbg.entriesScanned, s_nameDbg.skipNullEntity, s_nameDbg.skipNullPed,
        s_nameDbg.skipBadName, s_nameDbg.skipPedFilter,
        s_nameDbg.lookupHit, s_nameDbg.lookupMiss);
    NameDbgLog(buf);
    const auto identity =
        RagePlayerNames::GetIdentityStatus();
    wsprintfA(
        buf,
        "Names: identity ready=%u canonical=%d fallback=%d "
        "factions=%d family(cache=%d pending=%d canary=%u disabled=%u)",
        identity.helperReady ? 1u : 0u,
        identity.canonicalNames,
        identity.fallbackNames,
        identity.stringFactions,
        identity.familyCacheCount,
        identity.familyPendingCount,
        identity.familyCanary ? 1u : 0u,
        identity.familyRpcDisabled ? 1u : 0u);
    NameDbgLog(buf);

    if (s_nameDbg.lastMpBase) {
        wsprintfA(buf,
            "Names: mpBase=%p poolArray@%p=%p count@%u=%u",
            reinterpret_cast<void*>(s_nameDbg.lastMpBase),
            reinterpret_cast<void*>(s_nameDbg.lastMpBase + Offsets::RageMpDll::PedPoolPtrArray),
            reinterpret_cast<void*>(s_nameDbg.lastPoolArray),
            Offsets::RageMpDll::PedPoolCount, s_nameDbg.lastPoolCount);
        NameDbgLog(buf);
    }

    if (s_nameDbg.lastFailStage[0]) {
        wsprintfA(buf, "Names: lastFailStage=%s", s_nameDbg.lastFailStage);
        NameDbgLog(buf);
    }

    if (s_nameDbg.lastCachedName[0]) {
        wsprintfA(buf, "Names: lastCached ped=%p name='%s'",
                  reinterpret_cast<void*>(s_nameDbg.lastPed), s_nameDbg.lastCachedName);
        NameDbgLog(buf);
    } else if (s_nameDbg.lastPed) {
        wsprintfA(buf,
            "Names: lastChain entry=%p entity=%p ped=%p rawName='%s'",
            reinterpret_cast<void*>(s_nameDbg.lastEntry),
            reinterpret_cast<void*>(s_nameDbg.lastEntity),
            reinterpret_cast<void*>(s_nameDbg.lastPed),
            s_nameDbg.lastRawName[0] ? s_nameDbg.lastRawName : "<empty>");
        NameDbgLog(buf);
    }

    if (s_nameDbg.failNoMpDll)
        NameDbgLog("Names: FAIL multiplayer.dll base not resolved");
    if (s_nameDbg.failPoolArray)
        NameDbgLog("Names: FAIL PedPoolPtrArray read/invalid");
    if (s_nameDbg.failPoolCount)
        NameDbgLog("Names: FAIL PedPoolCount out of range (0 or >256)");

    s_nameDbg.directRuns = 0;
    s_nameDbg.failNoMpDll = 0;
    s_nameDbg.failPoolArray = 0;
    s_nameDbg.failPoolCount = 0;
    s_nameDbg.entriesScanned = 0;
    s_nameDbg.skipNullEntry = 0;
    s_nameDbg.skipNullEntity = 0;
    s_nameDbg.skipNullPed = 0;
    s_nameDbg.skipBadName = 0;
    s_nameDbg.skipPedFilter = 0;
    s_nameDbg.namesCached = 0;
    s_nameDbg.lookupMiss = 0;
    s_nameDbg.lookupHit = 0;
}

static DWORD s_cacheTick      = 0;
static DWORD s_cacheFrameTick = 0;
static DWORD s_nameScanTick = 0;

// --- Incremental heap scan state ---
// Instead of scanning 448MB every 1.5s (causes lag), we scan ONE region per
// frame. The cursor remembers position between calls. Full cycle ~5s, names
// cached permanently. New players appear in next cycle (~5s max delay).
static uintptr_t s_scanCursor = 0;
// ESP peds live up to ~0x3DFxxxxx (verified in lookup-miss logs), so the
// scan band must reach there to catch RP entity cells that reference them.
static uintptr_t s_scanBandStart = 0x04000000;
static uintptr_t s_scanBandEnd   = 0x40000000;

// Single-threaded (all on render thread): no locks needed.
static void EnsureNameCs() {}

static bool IsInWorldImpl()
{
    if (!Offsets::ViewportPtr || !IsValidPtr(Offsets::ViewportPtr))
        return false;
    __try {
        uint8_t* vp = *Offsets::ViewportPtr;
        if (!IsValidPtr(vp))
            return false;
        const float* V = reinterpret_cast<const float*>(vp + Offsets::Viewport::ViewMatrix);
        if (V[12] == 0.0f && V[13] == 0.0f && V[14] == 0.0f)
            return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void AddPlayerPed(uintptr_t ped)
{
    if (!IsPoolPlayerPed(ped))
        return;
    if (s_playerPedCount < (int)(sizeof(s_playerPeds) / sizeof(s_playerPeds[0]))) {
        bool exists = false;
        for (int i = 0; i < s_playerPedCount; ++i) {
            if (s_playerPeds[i] == ped) { exists = true; break; }
        }
        if (!exists)
            s_playerPeds[s_playerPedCount++] = ped;
    }
}

static void AddPlayerPedLocked(uintptr_t ped)
{
    AddPlayerPed(ped);
}

// --- Entity cell cache ---
// После первого scan, мы кэшируем адреса entity cells. Каждый кадр
// читаем имена из этих адресов (polling) — без повторного scan heap.
struct EntityCellEntry {
    uintptr_t cellAddr;   // адрес entity cell в heap
    uintptr_t ped;        // CPed* (может быть 0)
    uint32_t  lastSeenTick; // когда последний раз обновлён
};
static EntityCellEntry s_entityCells[64];
static int             s_entityCellCount = 0;
static DWORD           s_lastFullScanTick = 0;

static void AddEntityCell(uintptr_t cellAddr, uintptr_t ped)
{
    // Проверим — уже есть?
    for (int i = 0; i < s_entityCellCount; ++i) {
        if (s_entityCells[i].cellAddr == cellAddr) {
            s_entityCells[i].ped = ped;
            s_entityCells[i].lastSeenTick = GetTickCount();
            return;
        }
    }
    // Добавим новый
    if (s_entityCellCount < 64) {
        s_entityCells[s_entityCellCount].cellAddr = cellAddr;
        s_entityCells[s_entityCellCount].ped = ped;
        s_entityCells[s_entityCellCount].lastSeenTick = GetTickCount();
        ++s_entityCellCount;
    }
}

// Forward declare — CachePedName is defined below but used by PollCachedEntityCells.
static void CachePedName(uintptr_t ped, const char* name);

// Polling: читаем имена из кэшированных entity cells каждый кадр.
// Это быстро — ~64 адреса, не сканирует heap.
static void PollCachedEntityCells()
{
    const uint32_t nameOff = Offsets::RageEntity::RpName;   // 0x98
    const uint32_t pedOff  = Offsets::RageEntity::PedPtr;   // 0x90

    int validCount = 0;
    for (int i = 0; i < s_entityCellCount; ++i) {
        uintptr_t cell = s_entityCells[i].cellAddr;
        if (!cell) continue;

        // Проверим — cell всё ещё валиден (vtable на месте)?
        uintptr_t vtable = 0;
        if (!SafeReadQword(cell, &vtable)) {
            s_entityCells[i].cellAddr = 0; // invalidate
            continue;
        }

        // Проверим vtable (может быть освобождён)
        HMODULE hMp = GetModuleHandleW(L"multiplayer.dll");
        if (!hMp) continue;
        uintptr_t expectedVtable = (uintptr_t)hMp + Offsets::RageEntity::EntityVtableRVA;
        if (vtable != expectedVtable) {
            s_entityCells[i].cellAddr = 0; // не entity cell больше
            continue;
        }

        // Читаем имя
        char name[48];
        name[0] = 0;
        if (SafeReadRpName(cell + nameOff, name, (int)sizeof(name))) {
            // Читаем ped
            uintptr_t ped = 0;
            SafeReadQword(cell + pedOff, &ped);

            if (ped) {
                CachePedName(ped, name);
            }
            // Кэшируем также по cellAddr
            CachePedName(cell, name);
        }

        s_entityCells[validCount] = s_entityCells[i];
        ++validCount;
    }
    s_entityCellCount = validCount;
}

static bool IsRealPersonNameCStr(const char* str)
{
    if (!str || !str[0]) return false;
    if (lstrcmpiA(str, "Player") == 0 ||
        lstrcmpiA(str, "Unknown") == 0 ||
        lstrcmpiA(str, "Citizen") == 0 ||
        lstrcmpiA(str, "NPC") == 0 ||
        lstrcmpiA(str, "Animal") == 0) return false;
    if (strncmp(str, "\xD0\x93\xD1\x80\xD0\xB0\xD0\xB6\xD0\xB4\xD0\xB0\xD0\xBD", 12) == 0) return false;
    if (strncmp(str, "\xD0\x9D\xD0\xB5\xD0\xB7\xD0\xBD\xD0\xB0\xD0\xBA", 10) == 0) return false;
    return true;
}

static void CachePedName(uintptr_t ped, const char* name)
{
    if (!IsPoolPlayerPed(ped) || !name || !name[0])
        return;

    AddPlayerPedLocked(ped);

    for (int i = 0; i < s_pedNameCacheCount; ++i) {
        if (s_pedNameCache[i].ped == ped) {
            if (IsRealPersonNameCStr(s_pedNameCache[i].name) && !IsRealPersonNameCStr(name))
                return;
            for (int j = 0; j < (int)sizeof(s_pedNameCache[i].name); ++j) {
                s_pedNameCache[i].name[j] = name[j];
                if (!name[j]) break;
            }
            return;
        }
    }

    if (s_pedNameCacheCount >= (int)(sizeof(s_pedNameCache) / sizeof(s_pedNameCache[0])))
        return;

    s_pedNameCache[s_pedNameCacheCount].ped = ped;
    for (int j = 0; j < (int)sizeof(s_pedNameCache[0].name); ++j) {
        s_pedNameCache[s_pedNameCacheCount].name[j] = name[j];
        if (!name[j]) break;
    }
    ++s_pedNameCacheCount;
}

static bool IsTrackedPoolPedLocked(uintptr_t ped)
{
    if (!IsPoolPlayerPed(ped))
        return false;
    for (int i = 0; i < s_playerPedCount; ++i) {
        if (s_playerPeds[i] == ped)
            return true;
    }
    return false;
}

static void CopyOutName(const char* src, char* out, int outSize)
{
    if (!out || outSize < 1)
        return;
    for (int j = 0; j < outSize - 1; ++j) {
        out[j] = src[j];
        if (!src[j])
            return;
    }
    out[outSize - 1] = 0;
}

// Walk heap looking for entity cells by vtable pointer match.
// Entity cell layout (CE verified 30.06.2026):
//   [+0x00] vtable ptr (multiplayer.dll + EntityVtableRVA)
//   [+0x90] CPed* (may be NULL for non-streamed)
//   [+0x98] inline ASCII "FirstName_Surname"
static bool WalkHeapForEntityNames(uintptr_t onlyPed, char* out, int outSize)
{
    // Resolve entity vtable address from multiplayer.dll base.
    HMODULE hMp = GetModuleHandleW(L"multiplayer.dll");
    if (!hMp) return false;
    uintptr_t mpBase = (uintptr_t)hMp;
    uintptr_t entityVtable = mpBase + Offsets::RageEntity::EntityVtableRVA;

    const uint32_t nameOff = Offsets::RageEntity::RpName;    // 0x98
    const uint32_t pedOff  = Offsets::RageEntity::PedPtr;     // 0x90
    const uintptr_t bandStart = 0x04000000;
    const uintptr_t bandEnd   = 0x40000000;

    MEMORY_BASIC_INFORMATION mbi{};
    for (uintptr_t addr = bandStart; addr < bandEnd; ) {
        if (VirtualQuery(reinterpret_cast<const void*>(addr), &mbi, sizeof(mbi)) == 0)
            break;

        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        size_t regionSize = mbi.RegionSize;
        if (regionSize == 0)
            break;

        uintptr_t nextAddr = regionBase + regionSize;
        if (nextAddr <= addr)
            break;
        addr = nextAddr;

        if (regionBase >= bandEnd)
            break;
        if (!IsScannableHeapRegion(mbi))
            continue;

        uintptr_t scanStart = regionBase;
        if (scanStart < bandStart) scanStart = bandStart;
        uintptr_t regionEnd = regionBase + regionSize;
        if (regionEnd > bandEnd) regionEnd = bandEnd;
        if (scanStart >= regionEnd)
            continue;

        __try {
            for (uintptr_t cell = scanStart; cell + 0xA8 <= regionEnd; cell += sizeof(uintptr_t)) {
                uintptr_t val = 0;
                if (!SafeReadQword(cell, &val))
                    continue;
                // Check if this qword matches the entity vtable pointer.
                if (val != entityVtable)
                    continue;

                // Found an entity cell! Read the name at +0x98.
                char name[48];
                name[0] = 0;
                if (!SafeReadRpName(cell + nameOff, name, static_cast<int>(sizeof(name))))
                    continue;

                // Read the ped pointer at +0x90 (may be NULL).
                uintptr_t ped = 0;
                SafeReadQword(cell + pedOff, &ped);

                if (onlyPed && ped == onlyPed) {
                    CopyOutName(name, out, outSize);
                    CachePedName(onlyPed, name);
                    return out && out[0] != 0;
                }

                if (!onlyPed && ped) {
                    CachePedName(ped, name);
                }

                // Also cache by entity cell address — fallback for ESP.
                if (name[0]) {
                    CachePedName(cell, name);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    return false;
}

static bool TryReadEntityNameForPed(uintptr_t ped, char* out, int outSize)
{
    if (!out || outSize < 2)
        return false;
    out[0] = 0;
    return WalkHeapForEntityNames(ped, out, outSize);
}

// Incremental vtable-based scan: processes ONE region per call.
// After full scan cycle, switches to fast polling mode (PollEntityCellNames).
// Full re-scan runs every 10 seconds to catch new players.
static void ResolveEntityNamesForPoolPedsImpl()
{
    const uint32_t nameOff = Offsets::RageEntity::RpName;  // 0x98
    const uint32_t pedOff  = Offsets::RageEntity::PedPtr;  // 0x90
    const uintptr_t bandStart = s_scanBandStart;
    const uintptr_t bandEnd   = s_scanBandEnd;

    // Get multiplayer.dll base for vtable calc
    HMODULE hMp = GetModuleHandleW(L"multiplayer.dll");
    if (!hMp) hMp = GetModuleHandleW(L"RAGEMP.dll");
    if (!hMp) {
        PollCachedEntityCells();
        return;
    }
    const uintptr_t mpBase = (uintptr_t)hMp;
    const uintptr_t targetVtable = mpBase + Offsets::RageEntity::EntityVtableRVA;

    // Phase 1: Poll cached entity cells every frame (fast — ~20 reads)
    PollCachedEntityCells();

    // Phase 2: Full heap scan — only if needed (every 10s or first time)
    DWORD now = GetTickCount();
    bool needFullScan = (s_entityCellCount == 0) ||
                        (now - s_lastFullScanTick >= 10000);
    if (!needFullScan) return;
    if (s_entityCellCount > 0 && now - s_lastFullScanTick < 10000)
        return; // already scanned recently, just polling


    if (s_scanCursor < bandStart || s_scanCursor >= bandEnd)
        s_scanCursor = bandStart;

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(s_scanCursor), &mbi, sizeof(mbi)) == 0) {
        s_scanCursor = bandStart;
        return;
    }

    const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    size_t regionSize = mbi.RegionSize;
    if (regionSize == 0) {
        s_scanCursor = bandStart;
        return;
    }

    uintptr_t nextAddr = regionBase + regionSize;
    if (nextAddr <= s_scanCursor) {
        s_scanCursor = bandStart;
        return;
    }
    s_scanCursor = nextAddr;

    if (regionBase >= bandEnd) {
        s_scanCursor = bandStart;
        return;
    }

    if (!IsScannableHeapRegion(mbi))
        return;

    uintptr_t scanStart = regionBase;
    if (scanStart < bandStart) scanStart = bandStart;
    uintptr_t regionEnd = regionBase + regionSize;
    if (regionEnd > bandEnd) regionEnd = bandEnd;
    if (scanStart >= regionEnd)
        return;

    __try {
        for (uintptr_t cellBase = scanStart;
             cellBase + sizeof(uintptr_t) + nameOff + 4 <= regionEnd;
             cellBase += sizeof(uintptr_t)) {
            uintptr_t val = 0;
            if (!SafeReadQword(cellBase, &val))
                continue;
            // Match: first qword of entity cell == vtable pointer
            if (val != targetVtable)
                continue;

            // Found entity cell. Read ped ptr at +0x90.
            uintptr_t ped = 0;
            if (!SafeReadQword(cellBase + pedOff, &ped))
                continue;
            if (!IsPoolPlayerPed(ped))
                continue;

            // Read RP name at +0x98.
            char name[48];
            name[0] = 0;
            if (!SafeReadRpName(cellBase + nameOff, name, static_cast<int>(sizeof(name))))
                continue;

            CachePedName(ped, name);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool LookupCachedPedName(uintptr_t ped, char* out, int outSize)
{
    for (int i = 0; i < s_pedNameCacheCount; ++i) {
        if (s_pedNameCache[i].ped != ped)
            continue;
        for (int j = 0; j < outSize; ++j) {
            out[j] = s_pedNameCache[i].name[j];
            if (!s_pedNameCache[i].name[j])
                return out[0] != 0;
        }
        out[outSize - 1] = 0;
        return out[0] != 0;
    }
    return false;
}

static void RebuildNetSlotPlayers()
{
    const uintptr_t base = Game::GetModuleBase();
    if (!base)
        return;

    const uintptr_t slotsAddr = base + Offsets::RVA::NetPlayerPedSlots;
    if (!IsValidPtr(reinterpret_cast<const void*>(slotsAddr)))
        return;

    for (uint32_t i = 0; i < Offsets::NetPlayer::MaxSlots; ++i) {
        uintptr_t ped = 0;
        if (!SafeReadQword(slotsAddr + i * sizeof(uintptr_t), &ped))
            continue;
        AddPlayerPed(ped);
    }
}

static void RebuildPoolPedListImpl()
{
    uintptr_t freshPeds[256];
    int freshCount = 0;

    auto pushPed = [&](uintptr_t ped) {
        if (!IsPoolPlayerPed(ped) || freshCount >= (int)(sizeof(freshPeds) / sizeof(freshPeds[0])))
            return;
        for (int i = 0; i < freshCount; ++i) {
            if (freshPeds[i] == ped)
                return;
        }
        freshPeds[freshCount++] = ped;
    };

    if (CPed* local = SafeGetLocalPed())
        pushPed(reinterpret_cast<uintptr_t>(local));

    const uintptr_t base = Game::GetModuleBase();
    if (base) {
        const uintptr_t slotsAddr = base + Offsets::RVA::NetPlayerPedSlots;
        if (IsValidPtr(reinterpret_cast<const void*>(slotsAddr))) {
            for (uint32_t i = 0; i < Offsets::NetPlayer::MaxSlots; ++i) {
                uintptr_t ped = 0;
                if (SafeReadQword(slotsAddr + i * sizeof(uintptr_t), &ped))
                    pushPed(ped);
            }
        }
    }

    uintptr_t pool = 0;
    if (SafeResolveRagePool(&pool)) {
        const uint32_t maxSlots = SafeReadPoolMaxSlots(pool);
        for (uint32_t i = 0; i < maxSlots; ++i) {
            const uintptr_t slot = pool + Offsets::RagePlayerPool::SlotArray + i * Offsets::RagePlayerPool::SlotStride;
            if (!IsValidPtr(reinterpret_cast<const void*>(slot)))
                continue;

            uintptr_t ped = 0;
            if (!SafeReadPoolSlotPed(slot, &ped))
                continue;

            pushPed(ped);
        }
    }

    __try {
        if (Offsets::ReplayInterfacePtr && IsValidPtr(Offsets::ReplayInterfacePtr)) {
            const uint8_t* replayRaw = *Offsets::ReplayInterfacePtr;
            if (IsValidPtr(replayRaw)) {
                auto* replay = reinterpret_cast<CReplayInterface*>(const_cast<uint8_t*>(replayRaw));
                auto* pedIface = replay->GetPedInterface();
                if (pedIface) {
                    const int maxPeds = pedIface->GetMaxPeds();
                    for (int i = 0; i < maxPeds; ++i) {
                        CPed* ped = pedIface->GetPed(i);
                        if (!IsValidPtr(ped))
                            continue;
                        if (!SafeGetPlayerInfo(ped))
                            continue;
                        pushPed(reinterpret_cast<uintptr_t>(ped));
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    EnsureNameCs();
    s_playerPedCount = freshCount;
    for (int i = 0; i < freshCount; ++i)
        s_playerPeds[i] = freshPeds[i];
}

// ---- RAGE MP direct pool read (multiplayer.dll) ----
// Reads RP names directly from RAGE MP's CPed pool via static offsets.
// Zero heap scanning, zero lag, resolves ALL players immediately.
// Offsets verified via IDA Pro decompilation of multiplayer_dump.dll.
static uintptr_t GetRageMpDllBase()
{
    static uintptr_t base = 0;
    static bool logged = false;
    if (!base) {
        HMODULE hMp = nullptr;
        hMp = GetModuleHandleW(L"multiplayer.dll");
        if (!hMp) hMp = GetModuleHandleW(L"RAGEMP.dll");
        if (!hMp) hMp = GetModuleHandleW(L"multiplayer_dump.dll");
        if (hMp) {
            base = reinterpret_cast<uintptr_t>(hMp);
            if (!logged) {
                logged = true;
                char buf[96];
                wsprintfA(buf, "Names: multiplayer module base=%p", reinterpret_cast<void*>(base));
                NameDbgLog(buf);
            }
        }
    }
    if (!base && !logged) {
        logged = true;
        NameDbgSetFail("no_multiplayer_dll");
        NameDbgLogModulesOnce();
    }
    return base;
}

// Resolve RP names for ALL peds in RAGE MP's CPed pool directly via offsets.
// Called every ~500ms from RefreshPlayerCache. O(count) — no heap walk.
static void ResolveRageMpNamesDirect()
{
    ++s_nameDbg.directRuns;

    const uintptr_t mpBase = GetRageMpDllBase();
    if (!mpBase) {
        ++s_nameDbg.failNoMpDll;
        NameDbgSetFail("no_multiplayer_dll");
        return;
    }

    if (!s_nameDbg.mpBaseLogged) {
        s_nameDbg.mpBaseLogged = true;
        s_nameDbg.lastMpBase = mpBase;
        char buf[128];
        wsprintfA(buf, "Names: using mp offsets poolArray=+0x%X count=+0x%X entryEntity=+0x%X",
                  Offsets::RageMpDll::PedPoolPtrArray,
                  Offsets::RageMpDll::PedPoolCount,
                  Offsets::RageMpDll::EntryEntityPtr);
        NameDbgLog(buf);
    }

    const uintptr_t poolArrayAddr = mpBase + Offsets::RageMpDll::PedPoolPtrArray;
    const uintptr_t countAddr     = mpBase + Offsets::RageMpDll::PedPoolCount;

    uintptr_t poolArray = 0;
    uint32_t  poolCount = 0;
    if (!SafeReadQword(poolArrayAddr, &poolArray) ||
        !IsValidPtr(reinterpret_cast<const void*>(poolArray))) {
        ++s_nameDbg.failPoolArray;
        NameDbgSetFail("pool_array_invalid");
        s_nameDbg.lastMpBase = mpBase;
        s_nameDbg.lastPoolArray = poolArray;
        return;
    }

    __try {
        poolCount = *reinterpret_cast<const uint32_t*>(countAddr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++s_nameDbg.failPoolCount;
        NameDbgSetFail("pool_count_read_fault");
        return;
    }

    s_nameDbg.lastMpBase = mpBase;
    s_nameDbg.lastPoolArray = poolArray;
    s_nameDbg.lastPoolCount = poolCount;

    if (poolCount == 0 || poolCount > Offsets::RageMpDll::EntryMaxCount) {
        ++s_nameDbg.failPoolCount;
        NameDbgSetFail("pool_count_range");
        return;
    }

    for (uint32_t i = 0; i < poolCount; ++i) {
        uintptr_t entry = 0;
        if (!SafeReadQword(poolArray + i * sizeof(uintptr_t), &entry)) {
            ++s_nameDbg.skipNullEntry;
            continue;
        }
        if (!IsValidPtr(reinterpret_cast<const void*>(entry))) {
            ++s_nameDbg.skipNullEntry;
            continue;
        }

        ++s_nameDbg.entriesScanned;

        uintptr_t entity = 0;
        if (!SafeReadQword(entry + Offsets::RageMpDll::EntryEntityPtr, &entity) ||
            !IsValidPtr(reinterpret_cast<const void*>(entity))) {
            ++s_nameDbg.skipNullEntity;
            continue;
        }

        uintptr_t ped = 0;
        if (!SafeReadQword(entity + Offsets::RageEntity::PedPtr, &ped)) {
            ++s_nameDbg.skipNullPed;
            continue;
        }

        const uintptr_t nameAddr = entity + Offsets::RageEntity::RpName;
        char name[48];
        name[0] = 0;
        if (!SafeReadRpName(nameAddr, name, static_cast<int>(sizeof(name)))) {
            ++s_nameDbg.skipBadName;
            if (s_nameDbg.skipBadName <= 3) {
                s_nameDbg.lastEntry = entry;
                s_nameDbg.lastEntity = entity;
                s_nameDbg.lastPed = ped;
                NameDbgPreviewRawName(nameAddr);
            }
            continue;
        }

        s_nameDbg.lastEntry = entry;
        s_nameDbg.lastEntity = entity;
        s_nameDbg.lastPed = ped;
        lstrcpynA(s_nameDbg.lastCachedName, name, 40);

        if (!IsPoolPlayerPed(ped)) {
            ++s_nameDbg.skipPedFilter;
            if (s_nameDbg.skipPedFilter <= 2) {
                char buf[160];
                wsprintfA(buf,
                    "Names: REJECT ped filter ped=%p name='%s' (range 0x04-0x08 blocked)",
                    reinterpret_cast<void*>(ped), name);
                NameDbgLog(buf);
            }
            continue;
        }

        CachePedName(ped, name);
        ++s_nameDbg.namesCached;
        if (s_nameDbg.namesCached <= 3) {
            char buf[160];
            wsprintfA(buf, "Names: CACHED ped=%p entity=%p name='%s'",
                      reinterpret_cast<void*>(ped),
                      reinterpret_cast<void*>(entity), name);
            NameDbgLog(buf);
        }
    }
}

static void ResolveRageApiNamesForPoolPeds()
{
    struct StableBinding {
        std::uint32_t handle;
        uintptr_t ped;
    };
    static StableBinding bindings[256]{};
    static int bindingCount = 0;
    static std::uint64_t lastGeneration = 0;
    static LONG seenEpoch = -1;
    const LONG epoch = InterlockedCompareExchange(
        &s_sessionCacheEpoch,
        0,
        0);
    if (epoch != seenEpoch)
    {
        seenEpoch = epoch;
        bindingCount = 0;
        lastGeneration = 0;
    }
    // The generation marker is read first: the copy below moves 256 fat
    // entries, and it used to happen every frame only to be thrown away.
    const std::uint64_t pendingGeneration =
        RagePlayerNames::SnapshotGeneration();
    if (!pendingGeneration ||
        pendingGeneration == lastGeneration)
        return;
    RagePlayerNames::SnapshotEntry snapshots[256]{};
    RagePlayerNames::SnapshotEntry players[256]{};
    std::uint64_t generation = 0;
    const int snapshotCount = RagePlayerNames::CopySnapshot(
        snapshots,
        static_cast<int>(
            sizeof(snapshots) / sizeof(snapshots[0])),
        &generation);
    if (!generation)
        return;
    lastGeneration = generation;
    int playerCount = 0;
    for (int index = 0;
         index < snapshotCount &&
         playerCount < 256;
         ++index) {
        if (snapshots[index].kind !=
            RagePlayerNames::EntityKind::Player ||
            snapshots[index].local)
            continue;
        players[playerCount++] = snapshots[index];
    }

    s_pedNameCacheCount = 0;
    bool usedPeds[256]{};
    bool usedPlayers[256]{};
    int assignedPed[256];
    for (int index = 0; index < 256; ++index)
        assignedPed[index] = -1;
    Vector3 pedPositions[256]{};
    for (int index = 0; index < s_playerPedCount; ++index) {
        const uintptr_t ped = s_playerPeds[index];
        if (IsValidPtr(reinterpret_cast<const void*>(ped))) {
            pedPositions[index] =
                reinterpret_cast<const CPed*>(ped)->GetPosition();
            // Never attach player nicknames to fauna peds.
            if (Game::IsPedAnimal(
                    reinterpret_cast<const CPed*>(ped)))
                usedPeds[index] = true;
        } else {
            usedPeds[index] = true;
        }
    }

    // Keep stable handle->ped bindings whenever both sides still exist.
    for (int bindingIndex = 0;
        bindingIndex < bindingCount;
        ++bindingIndex) {
        int playerIndex = -1;
        int pedIndex = -1;
        for (int index = 0; index < playerCount; ++index) {
            if (!usedPlayers[index] &&
                players[index].handle == bindings[bindingIndex].handle) {
                playerIndex = index;
                break;
            }
        }
        for (int index = 0; index < s_playerPedCount; ++index) {
            if (!usedPeds[index] &&
                s_playerPeds[index] == bindings[bindingIndex].ped) {
                pedIndex = index;
                break;
            }
        }
        if (playerIndex < 0 || pedIndex < 0)
            continue;
        assignedPed[playerIndex] = pedIndex;
        usedPlayers[playerIndex] = true;
        usedPeds[pedIndex] = true;
    }

    const int maximumMatches =
        playerCount < s_playerPedCount
        ? playerCount
        : s_playerPedCount;
    for (int match = 0; match < maximumMatches; ++match) {
        int bestPed = -1;
        int bestPlayer = -1;
        float bestDistanceSquared = 225.0f;
        for (int pedIndex = 0;
            pedIndex < s_playerPedCount;
            ++pedIndex) {
            if (usedPeds[pedIndex])
                continue;
            for (int playerIndex = 0;
                playerIndex < playerCount;
                ++playerIndex) {
                if (usedPlayers[playerIndex])
                    continue;
                const float dx =
                    pedPositions[pedIndex].x -
                    players[playerIndex].x;
                const float dy =
                    pedPositions[pedIndex].y -
                    players[playerIndex].y;
                const float dz =
                    pedPositions[pedIndex].z -
                    players[playerIndex].z;
                const float distanceSquared =
                    dx * dx + dy * dy + dz * dz;
                if (distanceSquared >= bestDistanceSquared)
                    continue;
                bestDistanceSquared = distanceSquared;
                bestPed = pedIndex;
                bestPlayer = playerIndex;
            }
        }
        if (bestPed < 0 || bestPlayer < 0)
            break;
        assignedPed[bestPlayer] = bestPed;
        usedPeds[bestPed] = true;
        usedPlayers[bestPlayer] = true;
    }

    bindingCount = 0;
    for (int playerIndex = 0;
        playerIndex < playerCount;
        ++playerIndex) {
        const int pedIndex = assignedPed[playerIndex];
        if (pedIndex < 0)
            continue;
        const uintptr_t ped = s_playerPeds[pedIndex];
        const char* bestName = (players[playerIndex].nickname[0] && IsRealPersonNameCStr(players[playerIndex].nickname))
            ? players[playerIndex].nickname
            : (IsRealPersonNameCStr(players[playerIndex].name) ? players[playerIndex].name : nullptr);
        if (bestName) {
            CachePedName(ped, bestName);
        }
        if (players[playerIndex].handle &&
            bindingCount < 256) {
            bindings[bindingCount].handle =
                players[playerIndex].handle;
            bindings[bindingCount].ped = ped;
            ++bindingCount;
        }
    }
}

static bool LookupPoolNameById(uint16_t ragePlayerId, char* out, int outSize)
{
    if (RagePlayerNames::Get(ragePlayerId, out, outSize))
        return true;

    uintptr_t pool = 0;
    if (!SafeResolveRagePool(&pool))
        return false;

    const uint32_t maxSlots = SafeReadPoolMaxSlots(pool);
    for (uint32_t i = 0; i < maxSlots; ++i) {
        const uintptr_t slot = pool + Offsets::RagePlayerPool::SlotArray + i * Offsets::RagePlayerPool::SlotStride;
        uint32_t slotId = 0;
        __try {
            slotId = *reinterpret_cast<const uint32_t*>(slot + Offsets::RagePlayerPool::SlotId);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if ((slotId & 0xFFFF) != ragePlayerId)
            continue;

        uintptr_t ped = 0;
        if (!SafeReadPoolSlotPed(slot, &ped))
            return false;

        if (LookupCachedPedName(ped, out, outSize))
            return true;
        return TryReadEntityNameForPed(ped, out, outSize);
    }

    return false;
}

} // namespace

bool CPed::IsRageMpPlayer() const
{
    if (!IsValidPtr(this))
        return false;
    return SafeGetPlayerInfo(this) != nullptr;
}

bool CPed::GetRageEntityName(char* out, int outSize) const
{
    if (!out || outSize < 2)
        return false;
    out[0] = 0;
    return Game::TryGetPedRpName(this, out, outSize);
}

namespace Game {
    uintptr_t GetModuleBase()
    {
        static uintptr_t base = 0;
        if (!base) {
            HMODULE hExe = GetModuleHandleW(L"GTA5.exe");
            if (hExe)
                base = reinterpret_cast<uintptr_t>(hExe);
        }
        return base;
    }

    void LogNameDiagnostics()
    {
        NameDbgReport(true);
    }

    void RefreshPlayerCache()
    {
        if (!IsInWorldImpl()) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                NameDbgLog("Names: RefreshPlayerCache skipped (not in world yet)");
            }
            return;
        }

        const DWORD now = GetTickCount();
        if (s_cacheFrameTick == now)
            return;
        s_cacheFrameTick = now;

        // Rebuild ped list occasionally (cheap: just pointer reads).
        if (now - s_cacheTick >= 1000) {
            s_cacheTick = now;
            __try { RebuildPoolPedListImpl(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Direct read of native network names from multiplayer.dll pool
        if (now - s_nameScanTick >= 500) {
            s_nameScanTick = now;
            __try {
                ResolveRageMpNamesDirect();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                NameDbgSetFail("rage_direct_name_seh");
            }
        }

        // Match streamed CPeds to live RAGE players by their world positions;
        // nicknames and positions come from the V8 API cache.
        __try {
            ResolveRageApiNamesForPoolPeds();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            NameDbgSetFail("rage_api_name_bridge_seh");
        }

        NameDbgReport();
    }

    namespace {
        inline bool IsAnimalModelHash(std::uint32_t hash) noexcept
        {
            switch (hash)
            {
            case 0xCE5FF074u: case 0x573201B8u: case 0xAAB71F62u: case 0xA8683715u:
            case 0x14EC17EAu: case 0x56E29962u: case 0xFCFA9E1Eu: case 0x644AC75Eu:
            case 0x18012A9Fu: case 0xD86B5A95u: case 0x8BBAB455u: case 0x2FD800B7u:
            case 0x6AF51FAFu: case 0x471BE4B2u: case 0x4E8F95A2u: case 0x8D8AC8B9u:
            case 0x1250D7BAu: case 0xB11BAB56u: case 0x06A20728u: case 0x431D501Cu:
            case 0x6D362854u: case 0xDFB55C81u: case 0xC3B52966u: case 0x349F33E1u:
            case 0xC2D06F53u: case 0x9563221Du: case 0xD3939DFDu: case 0x3C831724u:
            case 0x06C3F072u: case 0x431FC24Cu: case 0xA148614Du: case 0xAD7844BBu:
            case 0xE71D5E68u: case 0x5C9D564Bu: // a_c_rabbit_02
                return true;
            default:
                return false;
            }
        }
    } // namespace

    bool IsPedAnimal(const CPed* ped) noexcept
    {
        if (!ped)
            return false;
        std::uint32_t hash = 0;
        bool byName = false;
        __try
        {
            const auto* base = reinterpret_cast<const std::uint8_t*>(ped);
            const auto* modelInfo =
                *reinterpret_cast<const std::uint8_t* const*>(base + 0x20);
            if (!modelInfo)
                return false;
            hash = *reinterpret_cast<const std::uint32_t*>(modelInfo + 0x18);
            // Ambient fauna models are named a_c_* / A_C_*.
            const char* modelName =
                *reinterpret_cast<const char* const*>(modelInfo + 0x1C8);
            if (modelName &&
                ((modelName[0] == 'a' || modelName[0] == 'A') &&
                 (modelName[1] == '_' ) &&
                 (modelName[2] == 'c' || modelName[2] == 'C') &&
                 modelName[3] == '_'))
                byName = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return byName || IsAnimalModelHash(hash);
    }

    bool IsPedPlayer(const CPed* ped)
    {
        if (!IsValidPtr(ped))
            return false;

        const uintptr_t target = reinterpret_cast<uintptr_t>(ped);
        __try {
            if (SafeGetPlayerInfo(ped))
                return true;

            char dummy[4];
            if (LookupCachedPedName(target, dummy, static_cast<int>(sizeof(dummy))))
                return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        return false;
    }

    void RefreshVisualCache(
        const CPed* const* peds,
        int count)
    {
        struct StableBinding final {
            uint32_t handle{};
            uintptr_t ped{};
        };
        static StableBinding bindings[256]{};
        static int bindingCount = 0;
        static uint64_t lastGeneration = 0;
        static LONG seenEpoch = -1;

        const LONG epoch = InterlockedCompareExchange(
            &s_sessionCacheEpoch,
            0,
            0);
        if (epoch != seenEpoch)
        {
            seenEpoch = epoch;
            bindingCount = 0;
            lastGeneration = 0;
            s_pedVisualCacheCount = 0;
            ClearPedVisualHash();
        }

        if (!peds || count <= 0)
            return;
        if (count > 256)
            count = 256;

        const std::uint64_t pendingGeneration =
            RagePlayerNames::SnapshotGeneration();
        if (!pendingGeneration ||
            pendingGeneration == lastGeneration)
            return;
        // This rebind is what a newly streamed player pays for on the frame it
        // appears, so its cost is timed and reported as its own scope.
        RUNTIME_PROFILE_BEGIN(rebindTimer);
        RagePlayerNames::SnapshotEntry snapshots[256]{};
        uint64_t generation = 0;
        const int snapshotCount =
            RagePlayerNames::CopySnapshot(
                snapshots,
                256,
                &generation);
        if (!generation)
        {
            RUNTIME_PROFILE_ACCUM(SnapshotCopy, rebindTimer);
            return;
        }
        lastGeneration = generation;

        uintptr_t candidates[256]{};
        Vector3 positions[256]{};
        bool usedPeds[256]{};
        int candidateCount = 0;
        for (int index = 0;
             index < count &&
             candidateCount < 256;
             ++index)
        {
            const CPed* ped = peds[index];
            if (!IsValidPtr(ped))
                continue;
            Vector3 position{};
            bool valid = false;
            __try {
                position = ped->GetPosition();
                valid = std::isfinite(position.x) &&
                    std::isfinite(position.y) &&
                    std::isfinite(position.z) &&
                    (position.x != 0.0f ||
                     position.y != 0.0f ||
                     position.z != 0.0f);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                valid = false;
            }
            if (!valid)
                continue;
            candidates[candidateCount] =
                reinterpret_cast<uintptr_t>(ped);
            positions[candidateCount] = position;
            ++candidateCount;
        }

        bool usedSnapshots[256]{};
        int assignedPed[256];
        for (int index = 0; index < 256; ++index) {
            assignedPed[index] = -1;
            if (index < snapshotCount &&
                snapshots[index].local)
                usedSnapshots[index] = true;
        }

        // Preserve a handle binding only while both entity kind and
        // position remain plausible. This rejects recycled CPed pointers.
        for (int bindingIndex = 0;
             bindingIndex < bindingCount;
             ++bindingIndex)
        {
            int snapshotIndex = -1;
            int pedIndex = -1;
            for (int index = 0;
                 index < snapshotCount;
                 ++index)
            {
                if (!usedSnapshots[index] &&
                    snapshots[index].handle ==
                        bindings[bindingIndex].handle)
                {
                    snapshotIndex = index;
                    break;
                }
            }
            for (int index = 0;
                 index < candidateCount;
                 ++index)
            {
                if (!usedPeds[index] &&
                    candidates[index] ==
                        bindings[bindingIndex].ped)
                {
                    pedIndex = index;
                    break;
                }
            }
            if (snapshotIndex < 0 || pedIndex < 0)
                continue;
            // RAGE remote players usually lack CPlayerInfo, so IsPedPlayer
            // is often false even for real mp.players. Trust the V8 kind.
            const float dx =
                positions[pedIndex].x -
                snapshots[snapshotIndex].x;
            const float dy =
                positions[pedIndex].y -
                snapshots[snapshotIndex].y;
            const float dz =
                positions[pedIndex].z -
                snapshots[snapshotIndex].z;
            if (dx * dx + dy * dy + dz * dz >
                225.0f)
                continue;
            assignedPed[snapshotIndex] = pedIndex;
            usedSnapshots[snapshotIndex] = true;
            usedPeds[pedIndex] = true;
        }

        VisualPolicy::Point snapshotPoints[256]{};
        VisualPolicy::Point pedPoints[256]{};
        for (int index = 0;
             index < snapshotCount;
             ++index)
        {
            snapshotPoints[index] = {
                snapshots[index].x,
                snapshots[index].y,
                snapshots[index].z,
                snapshots[index].kind ==
                    RagePlayerNames::EntityKind::Player,
                snapshots[index].kind ==
                    RagePlayerNames::EntityKind::Animal
            };
        }
        for (int index = 0;
             index < candidateCount;
             ++index)
        {
            // Leave isPlayer false so NPC snapshots can still match.
            // Mark fauna so Player snapshots cannot bind to them.
            const bool animal = IsPedAnimal(
                reinterpret_cast<const CPed*>(
                    candidates[index]));
            pedPoints[index] = {
                positions[index].x,
                positions[index].y,
                positions[index].z,
                false,
                animal
            };
        }
        VisualPolicy::AssignNearestOneToOne(
            snapshotPoints,
            snapshotCount,
            pedPoints,
            candidateCount,
            usedSnapshots,
            usedPeds,
            assignedPed,
            225.0f,
            25.0f);

        s_pedVisualCacheCount = 0;
        ClearPedVisualHash();
        bindingCount = 0;
        for (int snapshotIndex = 0;
             snapshotIndex < snapshotCount;
             ++snapshotIndex)
        {
            const int pedIndex =
                assignedPed[snapshotIndex];
            if (pedIndex < 0)
                continue;
            if (s_pedVisualCacheCount < 256)
            {
                PedVisualCacheEntry& cache =
                    s_pedVisualCache[
                        s_pedVisualCacheCount++];
                cache.ped = candidates[pedIndex];
                cache.snapshot =
                    snapshots[snapshotIndex];
                InsertPedVisualHash(
                    cache.ped,
                    s_pedVisualCacheCount - 1);
            }
            if (bindingCount < 256)
            {
                bindings[bindingCount].handle =
                    snapshots[snapshotIndex].handle;
                bindings[bindingCount].ped =
                    candidates[pedIndex];
                ++bindingCount;
            }
        }
        RUNTIME_PROFILE_ACCUM(SnapshotCopy, rebindTimer);
    }

    bool TryGetPedVisual(
        const CPed* ped,
        PedVisualData& output)
    {
        output = {};
        if (!IsValidPtr(ped))
            return false;
        const uintptr_t target =
            reinterpret_cast<uintptr_t>(ped);
        const int index =
            LookupPedVisualHash(target);
        if (index < 0)
            return false;
        const auto& snapshot =
            s_pedVisualCache[index].snapshot;
            output.valid = true;
            output.isPlayer =
                snapshot.kind ==
                RagePlayerNames::EntityKind::Player;
            output.isAnimal =
                snapshot.kind ==
                RagePlayerNames::EntityKind::Animal;
            output.isLocal = snapshot.local;
            output.visible = snapshot.visible;
            output.dead = snapshot.dead;
            output.knock = snapshot.knock;
            output.hp = snapshot.hp;
            output.adminLevel = snapshot.adminLevel;
            output.factionId = snapshot.factionId;
            output.factionType =
                snapshot.factionType;
            output.familyId = snapshot.familyId;
            output.sqlId = snapshot.sqlId;
            output.masked = snapshot.masked;
            output.nametagDisabled =
                snapshot.nametagDisabled;
            output.remoteId = snapshot.remoteId;
            output.rageHandle = snapshot.handle;
            output.boneMask = snapshot.boneMask;
            output.position = {
                snapshot.x,
                snapshot.y,
                snapshot.z
            };
            output.heading = snapshot.heading;
            for (int bone = 0;
                 bone < kVisualBoneCount;
                 ++bone)
            {
                output.bones[bone] = {
                    snapshot.bones[bone].x,
                    snapshot.bones[bone].y,
                    snapshot.bones[bone].z
                };
            }
            lstrcpynA(
                output.name,
                snapshot.name,
                static_cast<int>(
                    sizeof(output.name)));
            lstrcpynA(
                output.factionKey,
                snapshot.factionKey,
                static_cast<int>(
                    sizeof(output.factionKey)));
            lstrcpynA(
                output.factionTag,
                snapshot.factionTag,
                static_cast<int>(
                    sizeof(output.factionTag)));
            lstrcpynA(
                output.familyName,
                snapshot.familyName,
                static_cast<int>(
                    sizeof(output.familyName)));
            lstrcpynA(
                output.nickname,
                snapshot.nickname,
                static_cast<int>(
                    sizeof(output.nickname)));
        return true;
    }

    void ClearSessionCaches() noexcept
    {
        InterlockedIncrement(&s_sessionCacheEpoch);
        s_pedNameCacheCount = 0;
        s_pedVisualCacheCount = 0;
        ClearPedVisualHash();
        ClearDirectBoneHash();
        ClearSmoothHash();
        s_directBoneCacheCount = 0;
        s_directBoneCacheWrite = 0;
        s_frameContext = {};
        s_projectionCache = {};
        // Its slots are keyed on CPed pointers, and the game recycles those
        // across sessions: a surviving entry would hand an old answer to a new
        // ped that happened to land on the same address.
        Visibility::Reset();
        s_boneRoundRobin = 0;
        s_playerPedCount = 0;
        s_entityCellCount = 0;
        for (int index = 0; index < 256; ++index)
        {
            s_pedNameCache[index] = {};
            s_pedVisualCache[index] = {};
            s_directBoneCache[index] = {};
            s_worldSmooth[index] = {};
            s_playerPeds[index] = 0;
        }
        for (int index = 0; index < 64; ++index)
            s_entityCells[index] = {};
    }

    bool TryGetDirectPedBones(
        const CPed* ped,
        PedVisualData& output)
    {
        if (!IsValidPtr(ped) || !Offsets::GetBone)
            return false;

        const uintptr_t pedKey =
            reinterpret_cast<uintptr_t>(ped);
        const LONG epoch = InterlockedCompareExchange(
            &s_sessionCacheEpoch, 0, 0);
        const DWORD now = GetTickCount();
        // No age gate here. Reaching this function already means the caller
        // spent budget to get a fresh pose, and re-serving the cached rig
        // translated by the root advanced the animation at the tick-count
        // resolution (~15.6 ms) instead of the frame rate: above 60 FPS the
        // pose only actually moved every second or third frame. Callers that
        // want the stale rig ask TryGetCachedPedBones for it explicitly.
        int cacheSlot = LookupDirectBoneCache(pedKey);
        if (cacheSlot >= 0 &&
            s_directBoneCache[cacheSlot].epoch != epoch)
            cacheSlot = -1;

        // Prefer V8 snapshot memory walk; fallback to engine GetBone safely wrapped in SEH if V8 lacks bones.
        constexpr bool kAllowEngineGetBone = true;
        if (!kAllowEngineGetBone)
            return false;

        // sp¬ / amph bone masks passed to engine GetBone.
        enum BoneMask : int32_t
        {
            kMaskHead = 0x796E,
            kMaskNeck = 0x9995,
            kMaskSpine3 = 0x60F2,
            kMaskPelvis = 0x2E28,
            kMaskLUpperArm = 0xB1C5,
            kMaskLForearm = 0xEEEB,
            kMaskLHand = 0x49D9,
            kMaskRUpperArm = 0x9D4D,
            kMaskRForearm = 0x6E5C,
            kMaskRHand = 0xDEAD,
            kMaskLThigh = 0xE39F,
            kMaskLCalf = 0xF9BB,
            kMaskLFoot = 0x3779,
            kMaskRThigh = 0xCA72,
            kMaskRCalf = 0x9000,
            kMaskRFoot = 0xCC4D,
        };

        constexpr int kHead = 0;
        constexpr int kNeck = 1;
        constexpr int kSpine = 2;
        constexpr int kPelvis = 3;
        constexpr int kLeftUpperArm = 4;
        constexpr int kLeftForearm = 5;
        constexpr int kLeftHand = 6;
        constexpr int kRightUpperArm = 7;
        constexpr int kRightForearm = 8;
        constexpr int kRightHand = 9;
        constexpr int kLeftThigh = 10;
        constexpr int kLeftCalf = 11;
        constexpr int kLeftFoot = 12;
        constexpr int kRightThigh = 13;
        constexpr int kRightCalf = 14;
        constexpr int kRightFoot = 15;

        struct BoneRequest final
        {
            int slot{};
            int32_t mask{};
        };
        static constexpr BoneRequest kRequests[] = {
            {kHead, kMaskHead},
            {kNeck, kMaskNeck},
            {kSpine, kMaskSpine3},
            {kPelvis, kMaskPelvis},
            {kLeftUpperArm, kMaskLUpperArm},
            {kLeftForearm, kMaskLForearm},
            {kLeftHand, kMaskLHand},
            {kRightUpperArm, kMaskRUpperArm},
            {kRightForearm, kMaskRForearm},
            {kRightHand, kMaskRHand},
            {kLeftThigh, kMaskLThigh},
            {kLeftCalf, kMaskLCalf},
            {kLeftFoot, kMaskLFoot},
            {kRightThigh, kMaskRThigh},
            {kRightCalf, kMaskRCalf},
            {kRightFoot, kMaskRFoot},
        };

        Vector3 pedPosition{};
        Vector3 bones[kVisualBoneCount]{};
        std::uint32_t boneMask = 0;
        bool valid = false;

        __try {
            pedPosition = ped->GetPosition();
            if (!std::isfinite(pedPosition.x) ||
                !std::isfinite(pedPosition.y) ||
                !std::isfinite(pedPosition.z))
                __leave;

            auto* pedMutable = const_cast<CPed*>(ped);
            int resolved = 0;
            for (const BoneRequest& request : kRequests)
            {
                alignas(16) float out[4]{};
                Offsets::GetBone(pedMutable, out, request.mask);

                const Vector3 world{out[0], out[1], out[2]};
                if (!std::isfinite(world.x) ||
                    !std::isfinite(world.y) ||
                    !std::isfinite(world.z))
                    continue;
                if (std::fabs(world.x) < 0.01f &&
                    std::fabs(world.y) < 0.01f &&
                    std::fabs(world.z) < 0.01f)
                    continue;

                const float dx = world.x - pedPosition.x;
                const float dy = world.y - pedPosition.y;
                const float dz = world.z - pedPosition.z;
                if (dx * dx + dy * dy + dz * dz > 25.f)
                    continue;

                bones[request.slot] = world;
                boneMask |= (1u << request.slot);
                ++resolved;
            }

            if (resolved < 8 ||
                !(boneMask & (1u << kHead)) ||
                !(boneMask & (1u << kPelvis)))
                __leave;

            const float headToPelvis =
                bones[kHead].z - bones[kPelvis].z;
            if (headToPelvis < 0.15f || headToPelvis > 1.8f)
                __leave;

            valid = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            valid = false;
        }

        const bool rebinding = cacheSlot < 0;
        bool recycled = false;
        if (rebinding)
        {
            if (s_directBoneCacheCount < 256)
            {
                cacheSlot = s_directBoneCacheCount++;
            }
            else
            {
                cacheSlot = s_directBoneCacheWrite;
                s_directBoneCacheWrite =
                    (s_directBoneCacheWrite + 1) % 256;
                recycled = true;
            }
        }
        auto& cache = s_directBoneCache[cacheSlot];
        if (rebinding)
        {
            cache = {};
            cache.ped = pedKey;
        }
        if (recycled)
            RebuildDirectBoneHash();
        else
            InsertDirectBoneHash(pedKey, cacheSlot);
        cache.epoch = epoch;
        cache.tick = now;
        if (!valid)
        {
            // The rig that last resolved is kept, along with the root it was
            // measured against, so the caller's stale-cache fallback can still
            // place it. A ragdolling or prone ped fails the plausibility checks
            // above, and clearing the entry here made its skeleton disappear
            // outright instead of holding the last good pose.
            return false;
        }
        cache.rootPosition = pedPosition;

        for (int slot = 0; slot < kVisualBoneCount; ++slot)
        {
            if (boneMask & (1u << slot))
            {
                output.bones[slot] = bones[slot];
                cache.bones[slot] = bones[slot];
            }
        }
        output.boneMask = boneMask;
        cache.boneMask = boneMask;
        return true;
    }

    bool TryGetCachedPedBones(
        const CPed* ped,
        const Vector3& currentPosition,
        PedVisualData& output,
        DWORD maximumAgeMs) noexcept
    {
        if (!IsValidPtr(ped))
            return false;
        const uintptr_t pedKey =
            reinterpret_cast<uintptr_t>(ped);
        const LONG epoch = InterlockedCompareExchange(
            &s_sessionCacheEpoch, 0, 0);
        const DWORD now = GetTickCount();
        const int index = LookupDirectBoneCache(pedKey);
        if (index >= 0)
        {
            const auto& cache =
                s_directBoneCache[index];
            if (cache.epoch != epoch ||
                !cache.boneMask ||
                now - cache.tick > maximumAgeMs)
                return false;
            const Vector3 rootDelta{
                currentPosition.x -
                    cache.rootPosition.x,
                currentPosition.y -
                    cache.rootPosition.y,
                currentPosition.z -
                    cache.rootPosition.z
            };
            output.boneMask = cache.boneMask;
            for (int slot = 0;
                 slot < kVisualBoneCount;
                 ++slot)
            {
                if (!(cache.boneMask & (1u << slot)))
                    continue;
                output.bones[slot] = {
                    cache.bones[slot].x +
                        rootDelta.x,
                    cache.bones[slot].y +
                        rootDelta.y,
                    cache.bones[slot].z +
                        rootDelta.z
                };
            }
            return true;
        }
        return false;
    }

    void ResolveRenderedPedNames(const CPed* const* peds, int count)
    {
        if (!peds || count <= 0)
            return;

        // Heap scan runs on NameResolverThread; render thread only registers peds.
        __try {
            for (int i = 0; i < count; ++i) {
                const CPed* ped = peds[i];
                if (!IsValidPtr(ped))
                    continue;
                if (!SafeGetPlayerInfo(ped))
                    continue;
                AddPlayerPed(reinterpret_cast<uintptr_t>(ped));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    bool TryGetPedRpName(const CPed* ped, char* out, int outSize)
    {
        if (!out || outSize < 2)
            return false;
        out[0] = 0;
        if (!IsValidPtr(ped))
            return false;

        __try {
            const uintptr_t pedCell = reinterpret_cast<uintptr_t>(ped);
            if (LookupCachedPedName(pedCell, out, outSize)) {
                ++s_nameDbg.lookupHit;
                return true;
            }
            ++s_nameDbg.lookupMiss;
            return false;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            out[0] = 0;
            return false;
        }
    }

    bool GetRagePlayerNameById(uint16_t ragePlayerId, char* out, int outSize)
    {
        __try {
            return LookupPoolNameById(ragePlayerId, out, outSize);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (out && outSize > 0)
                out[0] = 0;
            return false;
        }
    }

    float g_ViewProjection[16] = {0};
    bool  g_ViewProjectionValid = false;

    namespace
    {
        bool RefreshProjectionCache(
            std::uint64_t frameId) noexcept
        {
            if (s_projectionCache.valid &&
                s_projectionCache.frameId == frameId)
                return true;
            ProjectionCache next{};
            next.frameId = frameId;
            if (!Offsets::ViewportPtr ||
                !IsValidPtr(Offsets::ViewportPtr))
            {
                s_projectionCache = next;
                return false;
            }
            __try
            {
                const auto* viewport =
                    *Offsets::ViewportPtr;
                if (!IsValidPtr(viewport))
                {
                    s_projectionCache = next;
                    return false;
                }
                const float* view =
                    reinterpret_cast<const float*>(
                        viewport +
                        Offsets::Viewport::ViewMatrix);
                const float* projection =
                    reinterpret_cast<const float*>(
                        viewport +
                        Offsets::Viewport::ProjectionMatrix);
                for (int index = 0; index < 16; ++index)
                {
                    next.view[index] = view[index];
                    next.projection[index] =
                        projection[index];
                }
                const ImVec2 display =
                    ImGui::GetIO().DisplaySize;
                next.width = display.x;
                next.height = display.y;
                next.valid =
                    next.width > 1.f &&
                    next.height > 1.f &&
                    (next.view[12] != 0.f ||
                     next.view[13] != 0.f ||
                     next.view[14] != 0.f);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                next.valid = false;
            }
            s_projectionCache = next;
            return next.valid;
        }

        bool ProjectCached(
            const Vector3& world,
            Vector2& screen) noexcept
        {
            if (!s_projectionCache.valid)
                return false;
            const float* view =
                s_projectionCache.view;
            const float* projection =
                s_projectionCache.projection;
            const float dx = world.x - view[12];
            const float dy = world.y - view[13];
            const float dz = world.z - view[14];
            const float vx =
                dx * view[0] +
                dy * view[1] +
                dz * view[2];
            const float vy =
                dx * view[4] +
                dy * view[5] +
                dz * view[6];
            const float vz =
                dx * view[8] +
                dy * view[9] +
                dz * view[10];
            const float sx =
                vx * projection[0] +
                vy * projection[4] +
                vz * projection[8] +
                projection[12];
            const float sy =
                vx * projection[1] +
                vy * projection[5] +
                vz * projection[9] +
                projection[13];
            const float sw =
                vx * projection[3] +
                vy * projection[7] +
                vz * projection[11] +
                projection[15];
            if (sw < 0.001f)
                return false;
            screen = {
                s_projectionCache.width * 0.5f +
                    (sx / sw) *
                    s_projectionCache.width * 0.5f,
                s_projectionCache.height * 0.5f -
                    (sy / sw) *
                    s_projectionCache.height * 0.5f
            };
            return std::isfinite(screen.x) &&
                std::isfinite(screen.y);
        }

        int s_syntheticCrowd = 0;

        // Fake peds are laid out in rows across the view, marching away from
        // the camera, so they exercise the same projection, text and skeleton
        // work a real gathering does. Positions come from the view basis, which
        // keeps every row on screen regardless of where the player looks.
        void AppendSyntheticCrowd(
            FramePedContext& next) noexcept
        {
            const int wanted = s_syntheticCrowd;
            if (wanted <= 0 || !s_projectionCache.valid)
                return;
            const float* view = s_projectionCache.view;
            const Vector3 camera{view[12], view[13], view[14]};
            const Vector3 right{view[0], view[1], view[2]};
            const Vector3 up{view[4], view[5], view[6]};
            // Row 2 points behind the camera: the projection matrix has
            // P[11] = -1, so a point placed along +row2 lands at w < 0 and
            // never projects.
            const Vector3 forward{
                -view[8], -view[9], -view[10]};
            const float phase =
                static_cast<float>(
                    GetTickCount64() % 8000ull) *
                0.000785f;
            constexpr int perRow = 12;
            constexpr int boneCount = kVisualBoneCount;
            // Standing rig: height offsets and a sideways spread per bone, in
            // metres, ordered like RagePlayerNames::BoneSlot.
            static constexpr float kBoneUp[boneCount] = {
                0.72f, 0.58f, 0.35f, 0.00f,
                0.48f, 0.28f, 0.10f,
                0.48f, 0.28f, 0.10f,
                -0.10f, -0.45f, -0.80f,
                -0.10f, -0.45f, -0.80f
            };
            static constexpr float kBoneSide[boneCount] = {
                0.00f, 0.00f, 0.00f, 0.00f,
                -0.20f, -0.26f, -0.30f,
                0.20f, 0.26f, 0.30f,
                -0.12f, -0.14f, -0.15f,
                0.12f, 0.14f, 0.15f
            };
            for (int index = 0;
                 index < wanted &&
                 next.count < kMaxFramePeds;
                 ++index)
            {
                const int row = index / perRow;
                const int column = index % perRow;
                const float depth =
                    11.f + static_cast<float>(row) * 8.5f;
                const float spread =
                    1.5f + static_cast<float>(row) * 0.85f;
                const float sway =
                    0.35f * std::sin(
                        phase +
                        static_cast<float>(index) * 0.7f);
                const float side =
                    (static_cast<float>(column) -
                     static_cast<float>(perRow - 1) * 0.5f) *
                        spread +
                    sway;
                Vector3 base{};
                base.x = camera.x + forward.x * depth +
                    right.x * side - up.x * 1.05f;
                base.y = camera.y + forward.y * depth +
                    right.y * side - up.y * 1.05f;
                base.z = camera.z + forward.z * depth +
                    right.z * side - up.z * 1.05f;

                FramePedEntry& entry =
                    next.entries[next.count];
                entry = {};
                entry.synthetic = true;
                entry.valid = true;
                entry.isPlayer = true;
                entry.livePosition = base;
                entry.smoothedPosition = base;
                entry.health = 40.f +
                    static_cast<float>((index * 37) % 160);
                entry.maxHealth = 200.f;
                entry.armor = static_cast<float>(
                    (index * 53) % 100);
                if (next.localPositionValid)
                {
                    const float dx =
                        base.x - next.localPosition.x;
                    const float dy =
                        base.y - next.localPosition.y;
                    const float dz =
                        base.z - next.localPosition.z;
                    entry.distanceSquared =
                        dx * dx + dy * dy + dz * dz;
                }
                else
                {
                    entry.distanceSquared = depth * depth;
                }

                PedVisualData& visual = entry.visual;
                visual.valid = true;
                visual.isPlayer = true;
                visual.visible = (index % 3) != 0;
                visual.position = base;
                visual.rageHandle =
                    0xF0000000u +
                    static_cast<std::uint32_t>(index);
                visual.remoteId =
                    static_cast<std::uint16_t>(600 + index);
                visual.hp = entry.health * 0.5f;
                visual.sqlId = 100000 + index;
                visual.factionId = (index % 4) + 1;
                visual.familyId = (index % 5) + 1;
                sprintf_s(
                    visual.nickname,
                    "Synthetic %03d",
                    index);
                sprintf_s(visual.name, "Crowd %03d", index);
                sprintf_s(
                    visual.factionKey,
                    "faction_%d",
                    visual.factionId);
                sprintf_s(
                    visual.factionTag,
                    "F%d",
                    visual.factionId);
                sprintf_s(
                    visual.familyName,
                    "Family %d",
                    visual.familyId);
                for (int bone = 0; bone < boneCount; ++bone)
                {
                    Vector3 point{};
                    point.x = base.x +
                        up.x * kBoneUp[bone] +
                        right.x * kBoneSide[bone];
                    point.y = base.y +
                        up.y * kBoneUp[bone] +
                        right.y * kBoneSide[bone];
                    point.z = base.z +
                        up.z * kBoneUp[bone] +
                        right.z * kBoneSide[bone];
                    visual.bones[bone] = point;
                    visual.boneMask |= 1u << bone;
                }
                entry.visualValid = true;
                entry.boneDemand = true;

                Vector3 coarse = base;
                coarse.z += 0.55f;
                Vector2 projected{};
                if (ProjectCached(coarse, projected))
                {
                    ++next.projections;
                    entry.coarseScreen = projected;
                    constexpr float margin = 64.f;
                    entry.coarseVisible =
                        projected.x >= -margin &&
                        projected.y >= -margin &&
                        projected.x <=
                            s_projectionCache.width + margin &&
                        projected.y <=
                            s_projectionCache.height + margin;
                }
                for (int bone = 0; bone < boneCount; ++bone)
                {
                    Vector2 bonePoint{};
                    if (!ProjectCached(
                            visual.bones[bone],
                            bonePoint))
                        continue;
                    entry.projectedBones[bone] = bonePoint;
                    entry.projectedBoneMask |= 1u << bone;
                    ++next.projections;
                }
                ++next.count;
            }
        }

        DWORD BoneCacheAge(
            const CPed* ped,
            LONG epoch) noexcept
        {
            const uintptr_t key =
                reinterpret_cast<uintptr_t>(ped);
            const int index = LookupDirectBoneCache(key);
            if (index < 0)
                return 0x7FFFFFFFu;
            const auto& cache = s_directBoneCache[index];
            if (cache.epoch != epoch || !cache.boneMask)
                return 0x7FFFFFFFu;
            return GetTickCount() - cache.tick;
        }

        WorldSmoothEntry* ResolveSmoothSlot(
            const FramePedEntry& entry,
            LONG epoch) noexcept
        {
            const uintptr_t key =
                reinterpret_cast<uintptr_t>(
                    entry.ped);
            // Hash first: a hit is the common case and used to cost a scan of
            // all 256 slots for every ped in the crowd.
            const int hashed = LookupSmoothHash(key);
            if (hashed >= 0)
            {
                WorldSmoothEntry& candidate =
                    s_worldSmooth[hashed];
                if (candidate.epoch == epoch &&
                    candidate.rageHandle ==
                        entry.visual.rageHandle &&
                    candidate.remoteId ==
                        entry.visual.remoteId)
                    return &candidate;
            }
            int emptyIndex = -1;
            int oldestIndex = 0;
            for (int index = 0;
                 index < Game::kMaxFramePeds;
                 ++index)
            {
                WorldSmoothEntry& candidate =
                    s_worldSmooth[index];
                if (candidate.ped == key &&
                    candidate.epoch == epoch &&
                    candidate.rageHandle ==
                        entry.visual.rageHandle &&
                    candidate.remoteId ==
                        entry.visual.remoteId)
                {
                    InsertSmoothHash(key, index);
                    return &candidate;
                }
                if (!candidate.ped && emptyIndex < 0)
                    emptyIndex = index;
                if (candidate.updatedAt <
                    s_worldSmooth[oldestIndex].updatedAt)
                    oldestIndex = index;
            }
            const int chosen = emptyIndex >= 0
                ? emptyIndex
                : oldestIndex;
            // The slot is about to be rebound to this ped, so the stale key has
            // to leave the index.
            RebuildSmoothHash();
            InsertSmoothHash(key, chosen);
            return &s_worldSmooth[chosen];
        }

#if RAGEMP_DEV_BUILD
        std::uint64_t g_PoseDiagSamples = 0;
        std::uint64_t g_PoseDiagFrames = 0;
        double g_PoseDiagSpeed = 0.0;
        std::uint64_t g_PoseDiagSpeedCount = 0;
#endif

        void SmoothFrameEntry(
            FramePedEntry& entry,
            LONG epoch,
            ULONGLONG now,
            bool processBones) noexcept
        {
            auto* slot =
                ResolveSmoothSlot(entry, epoch);
            if (!slot)
                return;
            const uintptr_t key =
                reinterpret_cast<uintptr_t>(
                    entry.ped);
            const bool identityChanged =
                slot->ped != key ||
                slot->epoch != epoch ||
                slot->rageHandle !=
                    entry.visual.rageHandle ||
                slot->remoteId !=
                    entry.visual.remoteId;
            const double nowSec = NowSeconds();
            float dt = slot->updatedAtSec > 0.0 &&
                nowSec > slot->updatedAtSec
                ? static_cast<float>(
                    nowSec - slot->updatedAtSec)
                : 1.f / 60.f;
            if (dt < 0.0005f)
                dt = 0.0005f;
            if (dt > 0.1f)
                dt = 0.1f;
            const float rootDx =
                entry.livePosition.x - slot->root.x;
            const float rootDy =
                entry.livePosition.y - slot->root.y;
            const float rootDz =
                entry.livePosition.z - slot->root.z;
            const bool teleport =
                rootDx * rootDx +
                rootDy * rootDy +
                rootDz * rootDz > 25.f;
            if (identityChanged ||
                !slot->updatedAt ||
                now - slot->updatedAt > 1000 ||
                teleport)
            {
                *slot = {};
                slot->ped = key;
                slot->epoch = epoch;
                slot->rageHandle =
                    entry.visual.rageHandle;
                slot->remoteId =
                    entry.visual.remoteId;
                slot->root = entry.livePosition;
                slot->poseAnchor = entry.livePosition;
                slot->boneMask =
                    processBones
                        ? entry.visual.boneMask
                        : 0u;
                slot->poseAtSec = nowSec;
                if (processBones)
                {
                    for (int bone = 0;
                         bone < kVisualBoneCount;
                         ++bone)
                    {
                        slot->bones[bone] =
                            entry.visual.bones[bone];
                        slot->poseOffset[bone] = {
                            entry.visual.bones[bone].x -
                                entry.livePosition.x,
                            entry.visual.bones[bone].y -
                                entry.livePosition.y,
                            entry.visual.bones[bone].z -
                                entry.livePosition.z
                        };
                        slot->poseVelocity[bone] = {};
                    }
                }
            }
            else
            {
                // Only enough of a filter to swallow a single bad read. Any
                // slower and the box visibly trails the ped it belongs to,
                // because livePosition off the nav matrix is already exact.
                const float rootAlpha =
                    1.f - std::exp(-240.f * dt);
                slot->root.x +=
                    (entry.livePosition.x -
                     slot->root.x) * rootAlpha;
                slot->root.y +=
                    (entry.livePosition.y -
                     slot->root.y) * rootAlpha;
                slot->root.z +=
                    (entry.livePosition.z -
                     slot->root.z) * rootAlpha;
                // Applies to the pose only, never to the translation, so it
                // softens the gait without putting the rig behind the body.
                const float boneAlpha =
                    1.f - std::exp(-90.f * dt);
                // Tracked even while bones are not wanted, so a ped that comes
                // back into demand does not start from a long stale anchor.
                const Vector3 anchorDelta{
                    entry.livePosition.x -
                        slot->poseAnchor.x,
                    entry.livePosition.y -
                        slot->poseAnchor.y,
                    entry.livePosition.z -
                        slot->poseAnchor.z
                };
                slot->poseAnchor = entry.livePosition;
                if (processBones)
                {
                    // Carry the rig rigidly onto the new root first. Whatever
                    // the filter below does, the skeleton stays on the ped.
                    for (int bone = 0;
                         bone < kVisualBoneCount;
                         ++bone)
                    {
                        if (!(slot->boneMask & (1u << bone)))
                            continue;
                        slot->bones[bone].x += anchorDelta.x;
                        slot->bones[bone].y += anchorDelta.y;
                        slot->bones[bone].z += anchorDelta.z;
                    }

                    // The offsets are identical on frames where the rig was not
                    // re-read, so any change at all means a fresh sample.
                    float poseDelta = 0.f;
                    for (int bone = 0;
                         bone < kVisualBoneCount;
                         ++bone)
                    {
                        const auto bit = 1u << bone;
                        if (!(entry.visual.boneMask & bit) ||
                            !(slot->boneMask & bit))
                            continue;
                        const float ox =
                            entry.visual.bones[bone].x -
                            entry.livePosition.x -
                            slot->poseOffset[bone].x;
                        const float oy =
                            entry.visual.bones[bone].y -
                            entry.livePosition.y -
                            slot->poseOffset[bone].y;
                        const float oz =
                            entry.visual.bones[bone].z -
                            entry.livePosition.z -
                            slot->poseOffset[bone].z;
                        poseDelta +=
                            ox * ox + oy * oy + oz * oz;
                    }
                    const bool freshPose = poseDelta > 1e-8f;

#if RAGEMP_DEV_BUILD
                    // Every budgeted ped should now report a fresh pose on
                    // every frame. A low fresh percentage here means the budget
                    // is overflowing, which is what makes a gait step.
                    g_PoseDiagSamples += freshPose ? 1 : 0;
                    ++g_PoseDiagFrames;
                    if (freshPose)
                    {
                        const int head = static_cast<int>(
                            RagePlayerNames::BoneSlot::Head);
                        if ((slot->boneMask & (1u << head)) &&
                            (entry.visual.boneMask & (1u << head)))
                        {
                            const Vector3& v =
                                slot->poseVelocity[head];
                            g_PoseDiagSpeed += std::sqrt(
                                v.x * v.x + v.y * v.y + v.z * v.z);
                            ++g_PoseDiagSpeedCount;
                        }
                    }
#endif

                    if (freshPose)
                    {
                        float sampleDt =
                            slot->poseAtSec > 0.0 &&
                            nowSec > slot->poseAtSec
                            ? static_cast<float>(
                                nowSec - slot->poseAtSec)
                            : dt;
                        sampleDt =
                            sampleDt < 0.004f ? 0.004f
                            : (sampleDt > 0.4f ? 0.4f : sampleDt);
                        const float inverse = 1.f / sampleDt;
                        for (int bone = 0;
                             bone < kVisualBoneCount;
                             ++bone)
                        {
                            const auto bit = 1u << bone;
                            if (!(entry.visual.boneMask & bit))
                                continue;
                            const Vector3 offset{
                                entry.visual.bones[bone].x -
                                    entry.livePosition.x,
                                entry.visual.bones[bone].y -
                                    entry.livePosition.y,
                                entry.visual.bones[bone].z -
                                    entry.livePosition.z
                            };
                            if (!(slot->boneMask & bit))
                            {
                                slot->poseOffset[bone] = offset;
                                slot->poseVelocity[bone] = {};
                                slot->bones[bone] =
                                    entry.visual.bones[bone];
                                continue;
                            }
                            Vector3 velocity{
                                (offset.x -
                                 slot->poseOffset[bone].x) *
                                    inverse,
                                (offset.y -
                                 slot->poseOffset[bone].y) *
                                    inverse,
                                (offset.z -
                                 slot->poseOffset[bone].z) *
                                    inverse
                            };
                            // A limb tip can genuinely reach several metres a
                            // second; past that it is a bad sample, not motion.
                            const float speedSq =
                                velocity.x * velocity.x +
                                velocity.y * velocity.y +
                                velocity.z * velocity.z;
                            constexpr float kMaxSpeed = 9.f;
                            if (speedSq >
                                kMaxSpeed * kMaxSpeed)
                            {
                                const float scale = kMaxSpeed /
                                    std::sqrt(speedSq);
                                velocity.x *= scale;
                                velocity.y *= scale;
                                velocity.z *= scale;
                            }
                            // Half of the new estimate: a single sample of a
                            // swinging limb is noisy.
                            slot->poseVelocity[bone].x =
                                slot->poseVelocity[bone].x * 0.5f +
                                velocity.x * 0.5f;
                            slot->poseVelocity[bone].y =
                                slot->poseVelocity[bone].y * 0.5f +
                                velocity.y * 0.5f;
                            slot->poseVelocity[bone].z =
                                slot->poseVelocity[bone].z * 0.5f +
                                velocity.z * 0.5f;
                            slot->poseOffset[bone] = offset;
                        }
                        slot->poseAtSec = nowSec;
                    }

                    // Carrying the pose forward is capped: with a ped that
                    // stopped animating, an uncapped prediction would walk the
                    // rig away from the body.
                    float age = slot->poseAtSec > 0.0 &&
                        nowSec > slot->poseAtSec
                        ? static_cast<float>(
                            nowSec - slot->poseAtSec)
                        : 0.f;
                    if (age > 0.12f)
                        age = 0.12f;

                    for (int bone = 0;
                         bone < kVisualBoneCount;
                         ++bone)
                    {
                        const auto bit = 1u << bone;
                        if (!(entry.visual.boneMask & bit))
                            continue;
                        if (!(slot->boneMask & bit))
                        {
                            slot->bones[bone] =
                                entry.visual.bones[bone];
                            continue;
                        }
                        // livePosition, not the filtered root: the offsets were
                        // measured against the live root when they were read,
                        // so mixing in the filtered one reintroduced exactly
                        // the positional lag this function is meant to avoid.
                        const Vector3 predicted{
                            entry.livePosition.x +
                                slot->poseOffset[bone].x +
                                slot->poseVelocity[bone].x * age,
                            entry.livePosition.y +
                                slot->poseOffset[bone].y +
                                slot->poseVelocity[bone].y * age,
                            entry.livePosition.z +
                                slot->poseOffset[bone].z +
                                slot->poseVelocity[bone].z * age
                        };
                        const float dx =
                            predicted.x - slot->bones[bone].x;
                        const float dy =
                            predicted.y - slot->bones[bone].y;
                        const float dz =
                            predicted.z - slot->bones[bone].z;
                        if (dx * dx + dy * dy + dz * dz > 9.f)
                        {
                            slot->bones[bone] = predicted;
                            continue;
                        }
                        slot->bones[bone].x += dx * boneAlpha;
                        slot->bones[bone].y += dy * boneAlpha;
                        slot->bones[bone].z += dz * boneAlpha;
                    }
                    slot->boneMask |=
                        entry.visual.boneMask;
                }
            }
            slot->updatedAt = now;
            slot->updatedAtSec = nowSec;
            entry.smoothedPosition = slot->root;
            entry.visual.position = slot->root;
            entry.visual.boneMask =
                processBones
                    ? slot->boneMask
                    : 0u;
            if (processBones)
            {
                for (int bone = 0;
                     bone < kVisualBoneCount;
                     ++bone)
                {
                    if (slot->boneMask & (1u << bone))
                        entry.visual.bones[bone] =
                            slot->bones[bone];
                }
            }
        }
    }

    void CameraBasisDiagnostics(
        float& rightLength,
        float& upLength,
        float& backLength,
        float& projectionX,
        float& projectionY) noexcept
    {
        rightLength = 0.f;
        upLength = 0.f;
        backLength = 0.f;
        projectionX = 0.f;
        projectionY = 0.f;
        if (!s_projectionCache.valid)
            return;
        const float* view = s_projectionCache.view;
        const auto rowLength =
            [](const float* row) noexcept {
                return std::sqrt(
                    row[0] * row[0] +
                    row[1] * row[1] +
                    row[2] * row[2]);
            };
        rightLength = rowLength(view + 0);
        upLength = rowLength(view + 4);
        backLength = rowLength(view + 8);
        projectionX = s_projectionCache.projection[0];
        projectionY = s_projectionCache.projection[5];
    }

    std::optional<Vector2> WorldToScreen(
        const Vector3& world)
    {
        const ULONGLONG now = GetTickCount64();
        const std::uint64_t frameId =
            s_frameContext.frameId &&
            now - s_frameContext.generatedAt <= 8
                ? s_frameContext.frameId
                : static_cast<std::uint64_t>(now);
        if (!RefreshProjectionCache(frameId))
            return std::nullopt;
        Vector2 screen{};
        if (!ProjectCached(world, screen))
            return std::nullopt;
        return screen;
    }

    void BuildFramePedContext(
        std::uint64_t frameId,
        bool aimNeedsBones,
        std::uint32_t espBoneProfileMask,
        bool visibilityDemand,
        int adminThreshold) noexcept
    {
        if (frameId &&
            s_frameContext.frameId == frameId)
            return;

        // Only the entries the previous frame actually filled are cleared:
        // wiping all 256 fat slots was ~150 KB of memset per frame.
        FramePedContext& next = s_frameContext;
        const int previousCount = next.count > 0
            ? (next.count < kMaxFramePeds
                ? next.count
                : kMaxFramePeds)
            : 0;
        for (int index = 0; index < previousCount; ++index)
            next.entries[index] = {};
        next.frameId = 0;
        next.generatedAt = 0;
        next.sessionEpoch = 0;
        next.localPed = nullptr;
        next.localPosition = {};
        next.localPositionValid = false;
        next.count = 0;
        next.boneRefreshes = 0;
        next.boneCacheHits = 0;
        next.projections = 0;
        next.frameId = frameId;
        next.generatedAt = GetTickCount64();
        next.sessionEpoch =
            InterlockedCompareExchange(
                &s_sessionCacheEpoch, 0, 0);
        RefreshProjectionCache(frameId);
        next.localPed = GetLocalPed();
        if (!next.localPed)
            return;
        __try
        {
            next.localPosition =
                next.localPed->GetPosition();
            next.localPositionValid =
                std::isfinite(next.localPosition.x) &&
                std::isfinite(next.localPosition.y) &&
                std::isfinite(next.localPosition.z);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            next.localPositionValid = false;
        }

        const auto range = GetPedRange();
        if (!range)
            return;
        const CPed* active[kMaxFramePeds]{};
        // The pool can hold more peds than a frame can carry, and taking the
        // first 256 let a distant NPC push a player standing next to us out of
        // the snapshot. A max-heap keyed on distance keeps the nearest 256 in
        // one pass, swapping 24-byte records instead of fat entries.
        IngestRecord* const heap = s_ingestHeap;
        int heapCount = 0;
        for (CPed* ped : *range)
        {
            if (!ped || ped == next.localPed)
                continue;
            IngestRecord record{};
            record.ped = ped;
            record.health = 0.f;
            record.distanceSquared = 0.f;
            bool valid = false;
            __try
            {
                record.position = ped->GetPosition();
                record.health = ped->GetHealth();
                valid =
                    std::isfinite(record.position.x) &&
                    std::isfinite(record.position.y) &&
                    std::isfinite(record.position.z) &&
                    (record.position.x != 0.f ||
                     record.position.y != 0.f ||
                     record.position.z != 0.f);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                valid = false;
            }
            if (!valid)
                continue;
            if (next.localPositionValid)
            {
                const float dx = record.position.x -
                    next.localPosition.x;
                const float dy = record.position.y -
                    next.localPosition.y;
                const float dz = record.position.z -
                    next.localPosition.z;
                record.distanceSquared =
                    dx * dx + dy * dy + dz * dz;
                if (record.distanceSquared < 0.01f)
                    continue;
            }
            if (heapCount < kMaxFramePeds)
            {
                heap[heapCount] = record;
                int child = heapCount++;
                while (child > 0)
                {
                    const int parent = (child - 1) / 2;
                    if (heap[parent].distanceSquared >=
                        heap[child].distanceSquared)
                        break;
                    const IngestRecord swap = heap[parent];
                    heap[parent] = heap[child];
                    heap[child] = swap;
                    child = parent;
                }
                continue;
            }
            if (record.distanceSquared >=
                heap[0].distanceSquared)
                continue;
            heap[0] = record;
            int parent = 0;
            for (;;)
            {
                const int left = parent * 2 + 1;
                const int right = left + 1;
                int largest = parent;
                if (left < heapCount &&
                    heap[largest].distanceSquared <
                        heap[left].distanceSquared)
                    largest = left;
                if (right < heapCount &&
                    heap[largest].distanceSquared <
                        heap[right].distanceSquared)
                    largest = right;
                if (largest == parent)
                    break;
                const IngestRecord swap = heap[parent];
                heap[parent] = heap[largest];
                heap[largest] = swap;
                parent = largest;
            }
        }
        for (int index = 0; index < heapCount; ++index)
        {
            FramePedEntry& entry = next.entries[index];
            entry.ped = heap[index].ped;
            entry.livePosition = heap[index].position;
            entry.health = heap[index].health;
            entry.distanceSquared =
                heap[index].distanceSquared;
            entry.valid = true;
            entry.dead = entry.health <= 0.f;
            active[index] = heap[index].ped;
        }
        next.count = heapCount;

        RefreshVisualCache(active, next.count);
        const ImVec2 display =
            ImGui::GetIO().DisplaySize;
        const Vector2 center{
            display.x * 0.5f,
            display.y * 0.5f
        };
        int priority[kMaxFramePeds]{};
        float score[kMaxFramePeds]{};
        int boneDemandCount = 0;
        for (int index = 0;
             index < next.count;
             ++index)
        {
            FramePedEntry& entry =
                next.entries[index];
            entry.visualValid =
                TryGetPedVisual(
                    entry.ped,
                    entry.visual);
            if (!entry.visualValid)
            {
                entry.visual = {};
                entry.visual.position =
                    entry.livePosition;
            }
            entry.isPlayer =
                entry.visualValid &&
                entry.visual.isPlayer;
            if (!entry.visualValid)
            {
                __try {
                    entry.isPlayer =
                        IsPedPlayer(entry.ped);
                } __except (
                    EXCEPTION_EXECUTE_HANDLER) {
                    entry.isPlayer = false;
                }
            }
            __try {
                entry.isAnimal =
                    entry.visual.isAnimal ||
                    IsPedAnimal(entry.ped);
            } __except (
                EXCEPTION_EXECUTE_HANDLER) {
                entry.isAnimal =
                    entry.visual.isAnimal;
            }
            if (entry.isAnimal)
                entry.isPlayer = false;
            entry.dead = entry.dead ||
                entry.visual.dead ||
                entry.visual.knock;

            Vector3 coarse =
                entry.livePosition;
            coarse.z += 0.55f;
            Vector2 projected{};
            if (ProjectCached(coarse, projected))
            {
                ++next.projections;
                entry.coarseScreen = projected;
                // Draw/update only the camera frustum. A small guard band
                // prevents boxes popping while a ped crosses the screen edge.
                constexpr float margin = 64.f;
                entry.coarseVisible =
                    projected.x >= -margin &&
                    projected.y >= -margin &&
                    projected.x <=
                        display.x + margin &&
                    projected.y <=
                        display.y + margin;
            }
            if (entry.coarseVisible)
            {
                __try {
                    entry.maxHealth =
                        entry.ped->GetMaxHealth();
                    entry.armor =
                        entry.ped->GetArmor();
                } __except (
                    EXCEPTION_EXECUTE_HANDLER) {
                    entry.maxHealth = 200.f;
                    entry.armor = 0.f;
                }
            }
            std::uint32_t profileBits = 0;
            if (entry.isAnimal)
            {
                profileBits =
                    1u << static_cast<int>(
                        VisualPolicy::Profile::Animal);
            }
            else if (!entry.isPlayer)
            {
                profileBits =
                    1u << static_cast<int>(
                        VisualPolicy::Profile::Npc);
            }
            else if (entry.visual.adminLevel >
                adminThreshold)
            {
                profileBits =
                    1u << static_cast<int>(
                        VisualPolicy::Profile::Admin);
            }
            else
            {
                profileBits =
                    (1u << static_cast<int>(
                        VisualPolicy::Profile::Players)) |
                    (1u << static_cast<int>(
                        VisualPolicy::Profile::
                            FactionPlayers)) |
                    (1u << static_cast<int>(
                        VisualPolicy::Profile::
                            FamilyPlayers));
            }
            entry.boneDemand =
                entry.coarseVisible &&
                (aimNeedsBones ||
                 (espBoneProfileMask &
                  profileBits) != 0);
            if (entry.boneDemand)
                ++boneDemandCount;
            const DWORD age =
                BoneCacheAge(
                    entry.ped,
                    next.sessionEpoch);
            const float dx =
                entry.coarseScreen.x - center.x;
            const float dy =
                entry.coarseScreen.y - center.y;
            const float centerDistance =
                entry.coarseVisible
                    ? dx * dx + dy * dy
                    : 100000000.f;
            score[index] = entry.boneDemand
                ? static_cast<float>(
                    age > 5000 ? 5000 : age) *
                      100000.f -
                      centerDistance
                : -1.0e30f;
            priority[index] = index;
        }
        for (int index = 1;
             index < next.count;
             ++index)
        {
            const int value = priority[index];
            int insert = index;
            while (insert > 0 &&
                score[priority[insert - 1]] <
                    score[value])
            {
                priority[insert] =
                    priority[insert - 1];
                --insert;
            }
            priority[insert] = value;
        }

#if RAGEMP_DEV_BUILD
        {
            static ULONGLONG s_LastPoseLog = 0;
            const ULONGLONG nowPose = GetTickCount64();
            if (nowPose - s_LastPoseLog > 500 &&
                g_PoseDiagFrames > 0)
            {
                s_LastPoseLog = nowPose;
                char msg[192]{};
                sprintf_s(
                    msg,
                    "Pose: entries=%llu fresh=%llu (%.0f%%) "
                    "headSpeed=%.3f demand=%d",
                    g_PoseDiagFrames,
                    g_PoseDiagSamples,
                    100.0 *
                        static_cast<double>(g_PoseDiagSamples) /
                        static_cast<double>(g_PoseDiagFrames),
                    g_PoseDiagSpeedCount
                        ? g_PoseDiagSpeed /
                              static_cast<double>(g_PoseDiagSpeedCount)
                        : 0.0,
                    boneDemandCount);
                hooks::HLog(msg);
                g_PoseDiagFrames = 0;
                g_PoseDiagSamples = 0;
                g_PoseDiagSpeed = 0.0;
                g_PoseDiagSpeedCount = 0;
            }
        }
#endif

        // A dozen peds a frame was too few: anyone outside the budget kept a
        // frozen gait, so a sprinting ped dragged a stale pose behind them.
        // Sixteen GetBone calls per ped is cheap enough to cover every ped that
        // is actually on screen and wanted, and the priority sort above still
        // spends the budget on the freshest need first when a crowd overflows.
        int boneBudget = 0;
        if (boneDemandCount > 0)
        {
            boneBudget = boneDemandCount <= 48
                ? boneDemandCount
                : (boneDemandCount <= 96 ? 40 : 32);
        }
        for (int rank = 0;
             rank < next.count &&
             boneBudget > 0;
             ++rank)
        {
            FramePedEntry& entry =
                next.entries[priority[rank]];
            if (!entry.boneDemand)
                continue;
            if (TryGetDirectPedBones(
                    entry.ped,
                    entry.visual))
                ++next.boneRefreshes;
            --boneBudget;
        }

        for (int index = 0;
             index < next.count;
             ++index)
        {
            FramePedEntry& entry =
                next.entries[index];
            if (!entry.boneDemand)
            {
                entry.visual.boneMask = 0;
                SmoothFrameEntry(
                    entry,
                    next.sessionEpoch,
                    next.generatedAt,
                    false);
                continue;
            }
            // Only reached when a crowd overflows the budget above. Leaving the
            // mask empty made the skeleton blink as the budget rotated through
            // the crowd, so a stale pose translated by the root delta is used
            // instead: the rig stays attached to the ped and stops flickering.
            if (!entry.visual.boneMask &&
                TryGetCachedPedBones(
                    entry.ped,
                    entry.livePosition,
                    entry.visual,
                    750))
                ++next.boneCacheHits;
            if (!entry.visual.boneMask &&
                TryGetCachedPedBones(
                    entry.ped,
                    entry.livePosition,
                    entry.visual,
                    5000))
                ++next.boneCacheHits;
            SmoothFrameEntry(
                entry,
                next.sessionEpoch,
                next.generatedAt,
                true);
            for (int bone = 0;
                 bone < kVisualBoneCount;
                 ++bone)
            {
                const auto bit = 1u << bone;
                if (!(entry.visual.boneMask & bit))
                    continue;
                Vector2 projected{};
                if (!ProjectCached(
                        entry.visual.bones[bone],
                        projected))
                    continue;
                entry.projectedBones[bone] =
                    projected;
                entry.projectedBoneMask |= bit;
                ++next.projections;
            }
        }
        ++s_boneRoundRobin;
        // Bones are final at this point, which is what the probes need to aim
        // at, and no consumer has read the frame yet. The camera comes from the
        // same projection cache the bone projections above used, so the ray and
        // the drawn skeleton agree on where the lens is.
        if (s_projectionCache.valid)
        {
            Visibility::Update(
                next,
                Vector3(
                    s_projectionCache.view[12],
                    s_projectionCache.view[13],
                    s_projectionCache.view[14]),
                center,
                visibilityDemand);
        }
        // Appended last: every loop above reads ped memory, and these entries
        // have no ped behind them.
        AppendSyntheticCrowd(next);
    }

    const FramePedContext&
    GetFramePedContext() noexcept
    {
        return s_frameContext;
    }

    bool LastViewCamera(Vector3& out) noexcept
    {
        if (!s_projectionCache.valid)
            return false;
        const float* view = s_projectionCache.view;
        out = Vector3(view[12], view[13], view[14]);
        return std::isfinite(out.x) &&
            std::isfinite(out.y) &&
            std::isfinite(out.z);
    }

    int& SyntheticCrowdCount() noexcept
    {
        return s_syntheticCrowd;
    }

    const FramePedEntry* FindFramePed(
        const CPed* ped) noexcept
    {
        if (!ped)
            return nullptr;
        for (int index = 0;
             index < s_frameContext.count;
             ++index)
        {
            if (s_frameContext.entries[index].ped ==
                ped)
                return &s_frameContext.entries[index];
        }
        return nullptr;
    }
}
