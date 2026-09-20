#include "WebRadar.h"

#include "CloudStorage.h"
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
constexpr int kMaxVehicles = 128;
constexpr int kJsonBytes = 16384;
constexpr wchar_t kPushPath[] = {
    L'/', L'a', L'p', L'i', L'/', L'r', L'a', L'd', L'a', L'r',
    L'/', L'p', L'u', L's', L'h', L'\0'};
constexpr char kPageUrl[] = "https://shadowcheats.cc/radar";

struct PlayerSnap final {
    std::uint32_t id{};
    float x{};
    float y{};
    float z{};
    float h{};
    float hp{};
    bool veh{};
    char name[48]{};
};

struct VehicleSnap final {
    float x{};
    float y{};
    float h{};
};

struct Frame final {
    float selfX{};
    float selfY{};
    float selfZ{};
    float selfH{};
    int playerCount{};
    int vehicleCount{};
    bool empty{};
    PlayerSnap players[kMaxPlayers]{};
    VehicleSnap vehicles[kMaxVehicles]{};
};

Settings g_Settings{};
Frame g_Buffers[2]{};
std::atomic<int> g_Published{-1};
std::atomic<bool> g_HasNew{false};
std::atomic<bool> g_Running{false};
std::atomic<bool> g_Busy{false};
std::atomic<unsigned> g_LastStatus{0};
std::atomic<ULONGLONG> g_LastSendMs{0};
HANDLE g_Wake{nullptr};
HANDLE g_Thread{nullptr};
char g_Status[96]{"Radar off"};
ULONGLONG g_LastCaptureMs{0};

void SetStatus(const char* text) noexcept
{
    lstrcpynA(g_Status, text, sizeof(g_Status));
}

bool InRadius(float ax, float ay, float bx, float by, float radius) noexcept
{
    const float dx = ax - bx;
    const float dy = ay - by;
    return (dx * dx + dy * dy) <= radius * radius;
}

void CopyName(char* dest, const char* src) noexcept
{
    lstrcpynA(dest, src && src[0] ? src : "player", 48);
}

int FillPlayers(Frame& frame, float radius) noexcept
{
    RagePlayerNames::SnapshotEntry entries[256]{};
    const int count = RagePlayerNames::CopySnapshot(entries, 256);
    int written = 0;
    for (int i = 0; i < count && written < kMaxPlayers; ++i) {
        const auto& entry = entries[i];
        if (entry.local) {
            frame.selfX = entry.x;
            frame.selfY = entry.y;
            frame.selfZ = entry.z;
            frame.selfH = entry.heading;
            continue;
        }
        if (!InRadius(frame.selfX, frame.selfY, entry.x, entry.y, radius))
            continue;
        PlayerSnap& dest = frame.players[written++];
        dest.id = entry.remoteId ? entry.remoteId : entry.handle;
        dest.x = entry.x;
        dest.y = entry.y;
        dest.z = entry.z;
        dest.h = entry.heading;
        dest.hp = entry.hp;
        dest.veh = false;
        CopyName(dest.name, entry.name[0] ? entry.name : entry.nickname);
    }
    return written;
}

int FillVehicles(Frame& frame, float radius) noexcept
{
    GameEntities::Live live[256]{};
    const int count = GameEntities::Copy(live, 256, GameEntities::kVehicles);
    int written = 0;
    for (int i = 0; i < count && written < kMaxVehicles; ++i) {
        const auto& item = live[i];
        if (!InRadius(frame.selfX, frame.selfY, item.position.x, item.position.y, radius))
            continue;
        VehicleSnap& dest = frame.vehicles[written++];
        dest.x = item.position.x;
        dest.y = item.position.y;
        dest.h = item.heading;
    }
    return written;
}

void CaptureFrame(Frame& frame) noexcept
{
    frame = {};
    const auto& ctx = Game::GetFramePedContext();
    if (ctx.localPositionValid) {
        frame.selfX = ctx.localPosition.x;
        frame.selfY = ctx.localPosition.y;
        frame.selfZ = ctx.localPosition.z;
    }
    frame.playerCount = FillPlayers(frame, g_Settings.radius);
    frame.vehicleCount = FillVehicles(frame, g_Settings.radius);
}

void Append(char*& cursor, char* end, const char* text) noexcept
{
    if (!cursor || !text)
        return;
    while (*text && cursor + 1 < end)
        *cursor++ = *text++;
    *cursor = 0;
}

void AppendEscaped(char*& cursor, char* end, const char* text) noexcept
{
    if (!text)
        return;
    for (; *text && cursor + 2 < end; ++text) {
        if (*text == '\\' || *text == '"') {
            *cursor++ = '\\';
            *cursor++ = *text;
            continue;
        }
        if (static_cast<unsigned char>(*text) < 32)
            continue;
        *cursor++ = *text;
    }
    *cursor = 0;
}

void AppendNumber(char*& cursor, char* end, const char* format, double value) noexcept
{
    char buf[48]{};
    sprintf_s(buf, format, value);
    Append(cursor, end, buf);
}

void WriteSelf(char*& cursor, char* end, const Frame& frame) noexcept
{
    Append(cursor, end, "\"self\":{\"x\":");
    AppendNumber(cursor, end, "%.2f", frame.selfX);
    Append(cursor, end, ",\"y\":");
    AppendNumber(cursor, end, "%.2f", frame.selfY);
    Append(cursor, end, ",\"z\":");
    AppendNumber(cursor, end, "%.2f", frame.selfZ);
    Append(cursor, end, ",\"h\":");
    AppendNumber(cursor, end, "%.2f", frame.selfH);
    Append(cursor, end, "}");
}

void WritePlayer(char*& cursor, char* end, const PlayerSnap& player) noexcept
{
    Append(cursor, end, "{\"id\":");
    AppendNumber(cursor, end, "%.0f", static_cast<double>(player.id));
    Append(cursor, end, ",\"n\":\"");
    AppendEscaped(cursor, end, player.name);
    Append(cursor, end, "\",\"x\":");
    AppendNumber(cursor, end, "%.2f", player.x);
    Append(cursor, end, ",\"y\":");
    AppendNumber(cursor, end, "%.2f", player.y);
    Append(cursor, end, ",\"h\":");
    AppendNumber(cursor, end, "%.2f", player.h);
    Append(cursor, end, ",\"hp\":");
    AppendNumber(cursor, end, "%.1f", player.hp);
    Append(cursor, end, player.veh ? ",\"veh\":true}" : ",\"veh\":false}");
}

void BuildJson(const Frame& frame, char* out, int capacity) noexcept
{
    char* cursor = out;
    char* end = out + capacity;
    Append(cursor, end, "{\"at\":");
    AppendNumber(cursor, end, "%.0f", static_cast<double>(time(nullptr)));
    Append(cursor, end, ",");
    WriteSelf(cursor, end, frame);
    Append(cursor, end, ",\"players\":[");
    for (int i = 0; i < frame.playerCount; ++i) {
        if (i)
            Append(cursor, end, ",");
        WritePlayer(cursor, end, frame.players[i]);
    }
    Append(cursor, end, "],\"vehicles\":[");
    for (int i = 0; i < frame.vehicleCount; ++i) {
        if (i)
            Append(cursor, end, ",");
        const VehicleSnap& vehicle = frame.vehicles[i];
        Append(cursor, end, "{\"x\":");
        AppendNumber(cursor, end, "%.2f", vehicle.x);
        Append(cursor, end, ",\"y\":");
        AppendNumber(cursor, end, "%.2f", vehicle.y);
        Append(cursor, end, ",\"h\":");
        AppendNumber(cursor, end, "%.2f", vehicle.h);
        Append(cursor, end, "}");
    }
    Append(cursor, end, "]}");
}

void SendFrame(const Frame& frame) noexcept
{
    if (g_Busy.exchange(true))
        return;
    char body[kJsonBytes]{};
    BuildJson(frame, body, kJsonBytes);
    unsigned long status = 0;
    const bool ok = CloudStorage::PostJson(kPushPath, body, status);
    g_LastStatus.store(status);
    g_LastSendMs.store(GetTickCount64());
    if (!ok) {
        SetStatus(CloudStorage::LastError());
    } else {
        char line[64]{};
        sprintf_s(line, "Sent HTTP %lu", status);
        SetStatus(line);
    }
    g_Busy.store(false);
}

DWORD WINAPI Worker(LPVOID) noexcept
{
    while (g_Running.load()) {
        WaitForSingleObject(g_Wake, 400);
        if (!g_Running.load())
            break;
        const int index = g_Published.load();
        if (index < 0 || !g_HasNew.exchange(false))
            continue;
        SendFrame(g_Buffers[index]);
    }
    return 0;
}

void StartWorker() noexcept
{
    if (g_Thread)
        return;
    g_Wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_Running.store(true);
    g_Thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    SetStatus("Radar waiting");
}

void StopWorker() noexcept
{
    g_Running.store(false);
    if (g_Wake)
        SetEvent(g_Wake);
    if (g_Thread) {
        WaitForSingleObject(g_Thread, 1500);
        CloseHandle(g_Thread);
        g_Thread = nullptr;
    }
    if (g_Wake) {
        CloseHandle(g_Wake);
        g_Wake = nullptr;
    }
    SetStatus("Radar off");
}

void Publish(const Frame& frame) noexcept
{
    const int write = g_Published.load() == 0 ? 1 : 0;
    g_Buffers[write] = frame;
    g_Published.store(write);
    g_HasNew.store(true);
    if (g_Wake)
        SetEvent(g_Wake);
}

} // namespace

Settings& GetSettings() noexcept
{
    return g_Settings;
}

void Update() noexcept
{
    if (!g_Settings.enabled) {
        if (g_Thread) {
            Frame empty{};
            empty.empty = true;
            Publish(empty);
            StopWorker();
        }
        return;
    }
    StartWorker();
    const float hz = g_Settings.hz < 1.f ? 1.f : (g_Settings.hz > 10.f ? 10.f : g_Settings.hz);
    const ULONGLONG minGap = static_cast<ULONGLONG>(1000.f / hz);
    const ULONGLONG now = GetTickCount64();
    if (now - g_LastCaptureMs < minGap)
        return;
    g_LastCaptureMs = now;
    Frame frame{};
    CaptureFrame(frame);
    Publish(frame);
}

void Shutdown() noexcept
{
    g_Settings.enabled = false;
    StopWorker();
}

const char* StatusText() noexcept
{
    return g_Status;
}

const char* PageUrl() noexcept
{
    return kPageUrl;
}

bool CopyPageUrl() noexcept
{
    const char* url = kPageUrl;
    const size_t bytes = strlen(url) + 1;
    if (!OpenClipboard(nullptr))
        return false;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }
    void* locked = GlobalLock(mem);
    if (!locked) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    memcpy(locked, url, bytes);
    GlobalUnlock(mem);
    SetClipboardData(CF_TEXT, mem);
    CloseClipboard();
    return true;
}

unsigned LastStatus() noexcept
{
    return g_LastStatus.load();
}

unsigned LastSendAgeMs() noexcept
{
    const ULONGLONG last = g_LastSendMs.load();
    if (!last)
        return 0;
    return static_cast<unsigned>(GetTickCount64() - last);
}

} // namespace WebRadar
