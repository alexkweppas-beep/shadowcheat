#include "WebRadar.h"

#include "CloudStorage.h"
#include "Friends.h"
#include "GameEntities.h"
#include "RagePlayerNames.h"
#include "../SDK/Game.h"

#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace WebRadar {
namespace {

constexpr int kMaxPlayers = 64;
constexpr int kJsonBytes  = 8192;

constexpr wchar_t kPushPath[] = {
    L'/', L'a', L'p', L'i', L'/', L'r', L'a', L'd', L'a', L'r',
    L'/', L'p', L'u', L's', L'h', L'\0'};

struct PlayerSnap final {
    std::uint32_t id{};
    float x{};
    float y{};
    float h{};
    float hp{-1.f};
    bool  fr{};    // is friend
    char  name[48]{};
};

struct Frame final {
    float selfX{};
    float selfY{};
    float selfH{};
    int   playerCount{};
    bool  empty{};
    PlayerSnap players[kMaxPlayers]{};
};

Settings g_Settings{};
// Double-buffer: game thread writes, network thread reads.
Frame              g_Buffers[2]{};
std::atomic<int>   g_Published{-1};
std::atomic<bool>  g_HasNew{false};
std::atomic<bool>  g_Running{false};
std::atomic<bool>  g_Busy{false};
std::atomic<unsigned> g_LastStatus{0};
std::atomic<ULONGLONG> g_LastSendMs{0};
HANDLE g_Wake{nullptr};
HANDLE g_Thread{nullptr};
char   g_Status[96]{"Radar off"};
ULONGLONG g_LastCaptureMs{0};
char   g_RadarToken[16]{};
char   g_PageUrlBuffer[128]{};

// ── Token ──────────────────────────────────────────────────────────────
void EnsureToken() noexcept {
    if (g_RadarToken[0] != '\0') return;
    constexpr char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    srand(static_cast<unsigned>(time(nullptr)) ^ GetCurrentProcessId());
    for (int i = 0; i < 12; ++i)
        g_RadarToken[i] = charset[rand() % (sizeof(charset) - 1)];
    g_RadarToken[12] = '\0';
}

// ── Helpers ────────────────────────────────────────────────────────────
void SetStatus(const char* text) noexcept { lstrcpynA(g_Status, text, sizeof(g_Status)); }

bool InRadius(float ax, float ay, float bx, float by, float radius) noexcept {
    const float dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy <= radius * radius;
}

// ── Snapshot ───────────────────────────────────────────────────────────
int FillPlayers(Frame& frame, float radius) noexcept {
    RagePlayerNames::SnapshotEntry entries[256]{};
    const int count = RagePlayerNames::CopySnapshot(entries, 256);
    int written = 0;
    for (int i = 0; i < count && written < kMaxPlayers; ++i) {
        const auto& entry = entries[i];
        if (entry.local) {
            frame.selfX = entry.x;
            frame.selfY = entry.y;
            frame.selfH = entry.heading;
            continue;
        }
        if (!InRadius(frame.selfX, frame.selfY, entry.x, entry.y, radius))
            continue;
        PlayerSnap& dest = frame.players[written++];
        dest.id = entry.remoteId ? entry.remoteId : entry.handle;
        dest.x  = entry.x;
        dest.y  = entry.y;
        dest.h  = entry.heading;
        if (entry.hp >= 0.f)
            dest.hp = entry.hp;
        else if (entry.hpSync >= 0.f)
            dest.hp = entry.hpSync;
        else if (entry.hpRaw >= 0.f)
            dest.hp = entry.hpRaw;
        else
            dest.hp = 100.f;
        lstrcpynA(dest.name,
            entry.name[0]     ? entry.name     :
            entry.nickname[0] ? entry.nickname : "player", 48);
        dest.fr = Friends::IsFriend(dest.name);
    }
    return written;
}

void CaptureFrame(Frame& frame) noexcept {
    frame = {};
    const auto& ctx = Game::GetFramePedContext();
    if (ctx.localPositionValid) {
        frame.selfX = ctx.localPosition.x;
        frame.selfY = ctx.localPosition.y;
    }
    frame.playerCount = FillPlayers(frame, g_Settings.radius);
}

// ── JSON builder (minimal, no sprintf loops) ───────────────────────────
void Append(char*& cur, char* end, const char* text) noexcept {
    if (!cur || !text) return;
    while (*text && cur + 1 < end) *cur++ = *text++;
    *cur = 0;
}

void AppendEscaped(char*& cur, char* end, const char* text) noexcept {
    if (!text) return;
    for (; *text && cur + 2 < end; ++text) {
        if (*text == '\\' || *text == '"') { *cur++ = '\\'; }
        if (static_cast<unsigned char>(*text) < 32) continue;
        *cur++ = *text;
    }
    *cur = 0;
}

// Fast integer serialiser — avoids sprintf_s for every number
void AppendInt(char*& cur, char* end, int v) noexcept {
    char buf[16];
    int len = 0;
    if (v < 0) { buf[len++] = '-'; v = -v; }
    char tmp[12]; int tlen = 0;
    do { tmp[tlen++] = '0' + v % 10; v /= 10; } while (v);
    for (int i = tlen - 1; i >= 0; --i) buf[len++] = tmp[i];
    buf[len] = 0;
    Append(cur, end, buf);
}

void AppendFloat1(char*& cur, char* end, float v) noexcept {
    // 1 decimal precision (enough for coords)
    char buf[32];
    sprintf_s(buf, "%.1f", v);
    Append(cur, end, buf);
}

void BuildJson(const Frame& frame, char* out, int capacity) noexcept {
    EnsureToken();
    char* cur = out;
    char* end = out + capacity;

    Append(cur, end, "{\"token\":\"");
    Append(cur, end, g_RadarToken);
    Append(cur, end, "\",\"self\":{\"x\":");
    AppendFloat1(cur, end, frame.selfX);
    Append(cur, end, ",\"y\":");
    AppendFloat1(cur, end, frame.selfY);
    Append(cur, end, ",\"h\":");
    AppendFloat1(cur, end, frame.selfH);
    Append(cur, end, "},\"players\":[");

    for (int i = 0; i < frame.playerCount; ++i) {
        const PlayerSnap& p = frame.players[i];
        if (i) Append(cur, end, ",");
        Append(cur, end, "{\"id\":");
        AppendInt(cur, end, static_cast<int>(p.id));
        Append(cur, end, ",\"n\":\"");
        AppendEscaped(cur, end, p.name);
        Append(cur, end, "\",\"x\":");
        AppendFloat1(cur, end, p.x);
        Append(cur, end, ",\"y\":");
        AppendFloat1(cur, end, p.y);
        Append(cur, end, ",\"h\":");
        AppendFloat1(cur, end, p.h);
        if (p.hp >= 0.f) {
            Append(cur, end, ",\"hp\":");
            AppendInt(cur, end, static_cast<int>(p.hp));
        }
        if (p.fr) Append(cur, end, ",\"fr\":1");
        Append(cur, end, "}");
    }
    Append(cur, end, "]}");
}

// ── Network thread ─────────────────────────────────────────────────────
void SendFrame(const Frame& frame) noexcept {
    if (g_Busy.exchange(true)) return;
    char body[kJsonBytes]{};
    BuildJson(frame, body, kJsonBytes);
    unsigned long status = 0;
    const bool ok = CloudStorage::PostJson(kPushPath, body, status);
    g_LastStatus.store(status);
    g_LastSendMs.store(GetTickCount64());
    if (!ok) { SetStatus(CloudStorage::LastError()); }
    else {
        char line[64]{};
        sprintf_s(line, "Sent HTTP %lu", status);
        SetStatus(line);
    }
    g_Busy.store(false);
}

DWORD WINAPI Worker(LPVOID) noexcept {
    // Low priority — doesn't compete with game render thread
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    ULONGLONG lastSent = 0;
    while (g_Running.load()) {
        // Sleep 50ms max so we react to hz changes quickly
        WaitForSingleObject(g_Wake, 50);
        if (!g_Running.load()) break;
        const ULONGLONG now = GetTickCount64();
        // Compute send gap dynamically from current hz setting (1-20 Hz)
    const float hz = g_Settings.hz < 1.f ? 1.f : (g_Settings.hz > 15.f ? 15.f : g_Settings.hz);
        const ULONGLONG sendGap = static_cast<ULONGLONG>(1000.f / hz);
        if (now - lastSent < sendGap) continue;
        const int index = g_Published.load();
        if (index < 0 || !g_HasNew.exchange(false)) continue;
        lastSent = now;
        SendFrame(g_Buffers[index]);
    }
    return 0;
}

void StartWorker() noexcept {
    if (g_Thread) return;
    g_Wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_Running.store(true);
    g_Thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    SetStatus("Radar waiting");
}

void StopWorker() noexcept {
    g_Running.store(false);
    if (g_Wake) SetEvent(g_Wake);
    if (g_Thread) {
        WaitForSingleObject(g_Thread, 2000);
        CloseHandle(g_Thread);
        g_Thread = nullptr;
    }
    if (g_Wake) { CloseHandle(g_Wake); g_Wake = nullptr; }
    SetStatus("Radar off");
}

void Publish(const Frame& frame) noexcept {
    const int write = g_Published.load() == 0 ? 1 : 0;
    g_Buffers[write] = frame;
    g_Published.store(write);
    g_HasNew.store(true);
    if (g_Wake) SetEvent(g_Wake);
}

} // namespace

// ── Public API ─────────────────────────────────────────────────────────
Settings& GetSettings() noexcept { return g_Settings; }

void Update() noexcept {
    if (!g_Settings.enabled) {
        if (g_Thread) {
            Frame empty{}; empty.empty = true;
            Publish(empty);
            StopWorker();
        }
        return;
    }
    StartWorker();
    // hz controls both capture rate and send rate — settable in menu 1-20
    const float hz = g_Settings.hz < 1.f ? 1.f : (g_Settings.hz > 15.f ? 15.f : g_Settings.hz);
    const ULONGLONG minGap = static_cast<ULONGLONG>(1000.f / hz);
    const ULONGLONG now = GetTickCount64();
    if (now - g_LastCaptureMs < minGap) return;
    g_LastCaptureMs = now;
    Frame frame{};
    CaptureFrame(frame);
    Publish(frame);
}

void Shutdown() noexcept { g_Settings.enabled = false; StopWorker(); }

const char* StatusText() noexcept { return g_Status; }

const char* PageUrl() noexcept {
    EnsureToken();
    if (g_PageUrlBuffer[0] == '\0')
        sprintf_s(g_PageUrlBuffer, "https://shadowcheats.cc/radar/%s", g_RadarToken);
    return g_PageUrlBuffer;
}

bool CopyPageUrl() noexcept {
    const char* url = PageUrl();
    const size_t bytes = strlen(url) + 1;
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) { CloseClipboard(); return false; }
    void* locked = GlobalLock(mem);
    if (!locked) { GlobalFree(mem); CloseClipboard(); return false; }
    memcpy(locked, url, bytes);
    GlobalUnlock(mem);
    SetClipboardData(CF_TEXT, mem);
    CloseClipboard();
    return true;
}

unsigned LastStatus()     noexcept { return g_LastStatus.load(); }
unsigned LastSendAgeMs()  noexcept {
    const ULONGLONG last = g_LastSendMs.load();
    return last ? static_cast<unsigned>(GetTickCount64() - last) : 0;
}

} // namespace WebRadar
