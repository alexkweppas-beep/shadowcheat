/*
 * CloudStorage.cpp — Shadow Backend edition
 *
 * Replaces the Supabase edge-function transport with our own
 * Express backend (api.shadowcheats.cc).
 *
 * Token source: reads JWT from the loader's shared memory
 * (ShadowSharedAuth::token) instead of cloud.session.
 *
 * Endpoint mapping:
 *   kind="config"  → /api/configs/{list|save|load|delete}  (product=ragemp)
 *   kind="script"  → /api/scripts/{list|save|load|delete}
 *
 * Public API (CloudStorage.h) is unchanged:
 *   List   → enumerate names
 *   Get    → load one item by name (we use name as key, not UUID)
 *   Upsert → create or update
 *   Delete → remove by name
 *
 * NOTE: IsSafeId / Base64 helpers kept for source-compat but Get/Delete
 * now accept a name string, not a UUID.  The header still exposes the
 * old signatures so the rest of the DLL doesn't need to change.
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CloudStorage.h"

#pragma comment(lib, "winhttp.lib")

// ── Shared memory layout (keep in sync with launcher/src/shared_ipc.hpp) ──
#pragma pack(push, 1)
struct ShadowSharedAuth {
    uint32_t magic;
    uint32_t version;
    uint64_t timestamp;
    char     username[64];
    char     token[512];
    char     hwid[128];
    int32_t  days_remaining;
    uint32_t pid;
};
#pragma pack(pop)

#define SHADOW_SHARED_MEMORY_NAME "Local\\SHADOW_IPC_AUTH_SECTION"
#define SHADOW_SHARED_MAGIC       0x53484457u

namespace CloudStorage
{
    namespace
    {
        // ── Backend host (api.shadowcheats.cc) ──
        // Stored as byte array to avoid plain-string in .rdata
        // L"shadowcheats.cc"
        constexpr wchar_t kHost[] = {
            L's',L'h',L'a',L'd',L'o',L'w',L'c',L'h',L'e',L'a',L't',L's',L'.',L'c',L'c',L'\0'
        };

        // product sent for kind="config" calls
        constexpr char kConfigProduct[] = "ragemp";

        // Live JWT token copied from shared memory
        char g_Token[512]{};
        char g_Error[128] = "Cloud unavailable";

        void SetError(const char* value)
        {
            lstrcpynA(g_Error, value ? value : "Cloud error", sizeof(g_Error));
        }

        bool IsSafeKind(const char* kind)
        {
            return kind &&
                (!lstrcmpA(kind, "config") || !lstrcmpA(kind, "script"));
        }

        // Name safety — printable UTF-8, no slashes, 1-63 chars
        bool IsSafeName(const char* name)
        {
            if (!name) return false;
            const int len = lstrlenA(name);
            if (len < 1 || len > 63) return false;
            for (int i = 0; i < len; ++i) {
                const unsigned char c = static_cast<unsigned char>(name[i]);
                if (c < 0x20 || c == 0x7f || c == '/' || c == '\\')
                    return false;
            }
            return true;
        }

        // ── JSON builder (minimal, no heap) ──
        // Writes  {"product":"ragemp","name":"...","data":"..."}  into buf.
        // Returns false if buf is too small.
        static bool BuildJsonBody(
            char* buf, std::size_t cap,
            const char* product,   // may be nullptr → omit field
            const char* name,      // may be nullptr → omit field
            const char* data)      // may be nullptr → omit field
        {
            char tmp[8192] = {0};
            int written = 0;
            written += wsprintfA(tmp + written, "{");
            bool first = true;
            char esc_buf[4096];

            auto AppendStr = [&](const char* key, const char* val) {
                if (!val) return;
                if (!first) written += wsprintfA(tmp + written, ",");
                first = false;
                
                int ei = 0;
                for (const char* p = val; *p && ei < 4090; ++p) {
                    if (*p == '"' || *p == '\\') esc_buf[ei++] = '\\';
                    esc_buf[ei++] = *p;
                }
                esc_buf[ei] = 0;
                written += wsprintfA(tmp + written, "\"%s\":\"%s\"", key, esc_buf);
            };

            AppendStr("product", product);
            AppendStr("name",    name);
            AppendStr("data",    data);

            written += wsprintfA(tmp + written, "}");
            if ((std::size_t)written >= cap) return false;
            memcpy(buf, tmp, written + 1);
            return true;
        }

        // ── Generic HTTPS POST to our backend ──
        static bool Request(
            const wchar_t* path,
            const char*    jsonBody,   // may be nullptr for list endpoint
            char*          response,
            std::size_t    responseCapacity,
            DWORD&         status)
        {
            status = 0;
            if (!g_Token[0] || !path || !response || responseCapacity < 2) {
                SetError("Cloud session unavailable");
                return false;
            }
            response[0] = 0;

            HINTERNET hSession = WinHttpOpen(
                L"Shadow-Cloud/2",
                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0);
            if (!hSession) { SetError("WinHttpOpen failed"); return false; }
            WinHttpSetTimeouts(hSession, 5000, 5000, 8000, 12000);

            HINTERNET hConn = WinHttpConnect(
                hSession, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!hConn) {
                WinHttpCloseHandle(hSession);
                SetError("WinHttpConnect failed");
                return false;
            }

            const wchar_t* method = jsonBody ? L"POST" : L"GET";
            HINTERNET hReq = WinHttpOpenRequest(
                hConn, method, path, nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE);
            if (!hReq) {
                WinHttpCloseHandle(hConn);
                WinHttpCloseHandle(hSession);
                SetError("WinHttpOpenRequest failed");
                return false;
            }

            // Build Authorization header using the JWT from shared memory
            wchar_t authHdr[640]{};
            {
                wchar_t wToken[512]{};
                MultiByteToWideChar(CP_UTF8, 0, g_Token, -1,
                    wToken, static_cast<int>(sizeof(wToken) / sizeof(wToken[0])));
                swprintf_s(authHdr,
                    L"Authorization: Bearer %s\r\n"
                    L"Content-Type: application/json\r\n"
                    L"Cache-Control: no-store\r\n",
                    wToken);
                SecureZeroMemory(wToken, sizeof(wToken));
            }

            const DWORD bodyLen = jsonBody
                ? static_cast<DWORD>(lstrlenA(jsonBody)) : 0;

            bool ok =
                WinHttpSendRequest(
                    hReq,
                    authHdr, static_cast<DWORD>(-1L),
                    bodyLen ? const_cast<char*>(jsonBody) : nullptr,
                    bodyLen, bodyLen, 0) != FALSE &&
                WinHttpReceiveResponse(hReq, nullptr) != FALSE;

            SecureZeroMemory(authHdr, sizeof(authHdr));

            if (ok) {
                DWORD szStatus = sizeof(status);
                ok = WinHttpQueryHeaders(
                    hReq,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &status, &szStatus,
                    WINHTTP_NO_HEADER_INDEX) != FALSE;
            }

            std::size_t used = 0;
            while (ok) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(hReq, &avail)) { ok = false; break; }
                if (!avail) break;
                if (used + avail + 1 > responseCapacity) {
                    SetError("Cloud response too large");
                    ok = false;
                    break;
                }
                DWORD rd = 0;
                if (!WinHttpReadData(hReq, response + used, avail, &rd)) {
                    ok = false; break;
                }
                used += rd;
            }
            response[used] = 0;

            WinHttpCloseHandle(hReq);
            WinHttpCloseHandle(hConn);
            WinHttpCloseHandle(hSession);

            if (!ok) { SetError("Cloud network error"); return false; }
            if (status == 401) {
                SecureZeroMemory(g_Token, sizeof(g_Token));
                SetError("Cloud session expired");
                return false;
            }
            if (status < 200 || status >= 300) {
                SetError(response[0] ? response : "Cloud request rejected");
                return false;
            }
            SetError("");
            return true;
        }

        // ── Try to refresh token from shared memory ──
        static bool RefreshTokenFromSharedMem()
        {
            // The loader self-cleans and exits right after inject, so it no
            // longer anchors the mapping object. A name-opened object dies
            // with its last handle, so a successful read keeps our handle
            // open — the section then outlives the loader for later refreshes.
            static HANDLE s_KeepAlive = nullptr;
            HANDLE hMap = s_KeepAlive;
            if (!hMap)
            {
                hMap = OpenFileMappingA(
                    FILE_MAP_READ, FALSE, SHADOW_SHARED_MEMORY_NAME);
                if (!hMap) return false;
            }

            const auto* auth = static_cast<const ShadowSharedAuth*>(
                MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(ShadowSharedAuth)));
            bool ok = false;
            if (auth && auth->magic == SHADOW_SHARED_MAGIC &&
                auth->version == 1 && auth->token[0])
            {
                lstrcpynA(g_Token, auth->token, sizeof(g_Token));
                ok = true;
            }
            if (auth) UnmapViewOfFile(auth);
            if (ok)
                s_KeepAlive = hMap;      // hold the section object open
            else if (!s_KeepAlive)
                CloseHandle(hMap);
            return ok;
        }

        // ── Parse comma-separated list response into Item array ──
        // Server returns: "name1,name2,name3"  (plain text, comma-separated)
        static int ParseListResponse(
            const char* response,
            Item* items,
            int   capacity)
        {
            int count = 0;
            const char* p = response;
            while (*p && count < capacity) {
                const char* comma = strchr(p, ',');
                std::size_t len = comma ? (std::size_t)(comma - p) : lstrlenA(p);
                if (len > 0 && len < sizeof(items[count].name)) {
                    memcpy(items[count].name, p, len);
                    items[count].name[len] = 0;
                    // We use name as id too (no UUID in our backend)
                    lstrcpynA(items[count].id, items[count].name,
                              sizeof(items[count].id));
                    ++count;
                }
                if (!comma) break;
                p = comma + 1;
            }
            return count;
        }

    } // anonymous namespace

    // ────────────────────────────────────────────────────────────
    //  Public API
    // ────────────────────────────────────────────────────────────

    void Initialize()
    {
        SecureZeroMemory(g_Token, sizeof(g_Token));
        RefreshTokenFromSharedMem();
    }

    bool EnsureSession()
    {
        if (g_Token[0]) return true;
        return RefreshTokenFromSharedMem();
    }

    void Shutdown()
    {
        SecureZeroMemory(g_Token, sizeof(g_Token));
    }

    bool IsAvailable()
    {
        if (g_Token[0]) return true;
        return RefreshTokenFromSharedMem();
    }

    const char* LastError()
    {
        return g_Error;
    }

    // SubscriptionEndsAt not available via our backend (loader manages days)
    std::int64_t SubscriptionEndsAt() noexcept
    {
        return 0;
    }

    // ── List: GET /api/configs/list?product=ragemp  or  GET /api/scripts/list ──
    bool List(const char* kind, Item* items, int capacity, int& count)
    {
        count = 0;
        if (!IsSafeKind(kind) || !items || capacity <= 0) return false;

        if (!EnsureSession()) {
            SetError("Cloud session unavailable");
            return false;
        }

        wchar_t path[128]{};
        if (!lstrcmpA(kind, "config")) {
            swprintf_s(path, L"/api/configs/list?product=%S", kConfigProduct);
        } else {
            swprintf_s(path, L"/api/scripts/list");
        }

        char response[8192]{};
        DWORD status = 0;
        if (!Request(path, nullptr, response, sizeof(response), status))
            return false;

        count = ParseListResponse(response, items, capacity);
        return true;
    }

    // ── Get: POST /api/configs/load  or  /api/scripts/load ──
    // `id` parameter is treated as the item name (not UUID)
    bool Get(
        const char* kind,
        const char* id,       // name of the item
        char*       content,
        std::size_t capacity)
    {
        if (!IsSafeKind(kind) || !id || !content || capacity < 2)
            return false;

        if (!EnsureSession()) {
            SetError("Cloud session unavailable");
            return false;
        }

        wchar_t path[64]{};
        if (!lstrcmpA(kind, "config")) {
            swprintf_s(path, L"/api/configs/load");
        } else {
            swprintf_s(path, L"/api/scripts/load");
        }

        // Build JSON body: {"product":"ragemp","name":"..."} or {"name":"..."}
        char body[512]{};
        if (!lstrcmpA(kind, "config")) {
            if (!BuildJsonBody(body, sizeof(body), kConfigProduct, id, nullptr))
                return false;
        } else {
            if (!BuildJsonBody(body, sizeof(body), nullptr, id, nullptr))
                return false;
        }

        char raw[16384]{};
        DWORD status = 0;
        if (!Request(path, body, raw, sizeof(raw), status))
            return false;

        // Response is JSON: {"data":"..."}  — extract value between "data":"..."
        const char* dataKey = "\"data\":\"";
        const char* start = strstr(raw, dataKey);
        if (!start) {
            // Possibly plain-text response
            lstrcpynA(content, raw, static_cast<int>(capacity));
            return true;
        }
        start += lstrlenA(dataKey);
        // Un-escape: copy until closing unescaped quote
        std::size_t out = 0;
        for (const char* p = start; *p && out + 1 < capacity; ++p) {
            if (*p == '\\' && *(p + 1)) {
                ++p;
                content[out++] = *p;
            } else if (*p == '"') {
                break;
            } else {
                content[out++] = *p;
            }
        }
        content[out] = 0;
        return true;
    }

    // ── Upsert: POST /api/configs/save  or  /api/scripts/save ──
    bool Upsert(
        const char* kind,
        const char* name,
        const char* content)
    {
        if (!IsSafeKind(kind) || !IsSafeName(name) || !content)
            return false;

        if (!EnsureSession()) {
            SetError("Cloud session unavailable");
            return false;
        }

        wchar_t path[64]{};
        if (!lstrcmpA(kind, "config")) {
            swprintf_s(path, L"/api/configs/save");
        } else {
            swprintf_s(path, L"/api/scripts/save");
        }

        char body[8192]{};
        if (!lstrcmpA(kind, "config")) {
            if (!BuildJsonBody(body, sizeof(body), kConfigProduct, name, content))
                return false;
        } else {
            if (!BuildJsonBody(body, sizeof(body), nullptr, name, content))
                return false;
        }

        char response[128]{};
        DWORD status = 0;
        return Request(path, body, response, sizeof(response), status);
    }

    // ── Delete: POST /api/configs/delete  or  /api/scripts/delete ──
    // `id` parameter is the item name
    bool Delete(const char* kind, const char* id)
    {
        if (!IsSafeKind(kind) || !id) return false;

        if (!EnsureSession()) {
            SetError("Cloud session unavailable");
            return false;
        }

        wchar_t path[64]{};
        if (!lstrcmpA(kind, "config")) {
            swprintf_s(path, L"/api/configs/delete");
        } else {
            swprintf_s(path, L"/api/scripts/delete");
        }

        char body[256]{};
        if (!lstrcmpA(kind, "config")) {
            if (!BuildJsonBody(body, sizeof(body), kConfigProduct, id, nullptr))
                return false;
        } else {
            if (!BuildJsonBody(body, sizeof(body), nullptr, id, nullptr))
                return false;
        }

        char response[64]{};
        DWORD status = 0;
        return Request(path, body, response, sizeof(response), status);
    }

} // namespace CloudStorage
