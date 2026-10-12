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
#include <dpapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <winhttp.h>
#include <new>

#include "../xorstr.hpp"
#include "../vmp_markers.hpp"
#include "CloudStorage.h"

#pragma comment(lib, "winhttp.lib")

// ── Shared memory layout (keep in sync with launcher/src/shared_ipc.hpp) ──
#pragma pack(push, 1)
struct ShadowSharedAuth {
  uint32_t magic;
  uint32_t version;
  uint64_t timestamp;
  char username[64];
  char token[512];
  char hwid[128];
  int32_t days_remaining;
  uint32_t pid;
};
#pragma pack(pop)

#define SHADOW_SHARED_MAGIC 0x53484457u

// NOTE: SHADOW_SHARED_MEMORY_NAME is now inlined via xorstr_ at each use site
// to prevent the string from appearing in .rdata

namespace CloudStorage {
namespace {
// ── Backend host (shadowcheats.cc) ──
// Stored as byte array to avoid plain-string in .rdata
// L"shadowcheats.cc"
constexpr wchar_t kHost[] = {L's', L'h', L'a', L'd', L'o', L'w', L'c', L'h',
                             L'e', L'a', L't', L's', L'.', L'c', L'c', L'\0'};

// product sent for kind="config" calls
constexpr char kConfigProduct[] = "ragemp";

// The JWT is never kept as plaintext between requests. CryptProtectMemory
// binds this ciphertext to the current process; a blind memory/string dump no
// longer yields a reusable Bearer token. A debugger executing code in-process
// can still ask Windows to decrypt it, so this is exposure reduction, not a
// trust boundary.
alignas(CRYPTPROTECTMEMORY_BLOCK_SIZE)
unsigned char g_ProtectedToken[512]{};
volatile LONG g_HasToken = 0;
char g_Error[128] = "Cloud unavailable";

// Unix timestamp of subscription expiry (0 = unknown/expired).
// Computed from auth->days_remaining which the loader fills in *days*.
std::int64_t g_ExpiresAt = 0;

char g_Username[64]{};

void ClearToken() noexcept {
  SecureZeroMemory(g_ProtectedToken, sizeof(g_ProtectedToken));
  InterlockedExchange(&g_HasToken, 0);
}

bool HasToken() noexcept {
  return InterlockedCompareExchange(&g_HasToken, 0, 0) != 0;
}

__declspec(noinline) bool StoreToken(const char* value) noexcept {
  bool ok = false;
  SHADOW_PROTECT_ULTRA("Cloud.StoreToken");
  char plaintext[sizeof(g_ProtectedToken)]{};
  ok = value && value[0];
  if (ok) {
    lstrcpynA(plaintext, value, static_cast<int>(sizeof(plaintext)));
    ok = CryptProtectMemory(
             plaintext,
             sizeof(plaintext),
             CRYPTPROTECTMEMORY_SAME_PROCESS) != FALSE;
  }
  if (ok) {
    CopyMemory(g_ProtectedToken, plaintext, sizeof(g_ProtectedToken));
    InterlockedExchange(&g_HasToken, 1);
  } else {
    ClearToken();
  }
  SecureZeroMemory(plaintext, sizeof(plaintext));
  SHADOW_PROTECT_END();
  return ok;
}

__declspec(noinline) bool LoadToken(char* output, std::size_t capacity) noexcept {
  bool ok = false;
  SHADOW_PROTECT_ULTRA("Cloud.LoadToken");
  do {
    if (!output || capacity < sizeof(g_ProtectedToken) || !HasToken())
      break;
    CopyMemory(output, g_ProtectedToken, sizeof(g_ProtectedToken));
    if (!CryptUnprotectMemory(
            output,
            sizeof(g_ProtectedToken),
            CRYPTPROTECTMEMORY_SAME_PROCESS)) {
      SecureZeroMemory(output, capacity);
      break;
    }
    output[capacity - 1] = 0;
    ok = output[0] != 0;
  } while (false);
  SHADOW_PROTECT_END();
  return ok;
}

void SetError(const char *value) {
  lstrcpynA(g_Error, value ? value : "Cloud error", sizeof(g_Error));
}

bool IsSafeKind(const char *kind) {
  return kind && (!lstrcmpA(kind, "config") || !lstrcmpA(kind, "script"));
}

// Name safety — printable UTF-8, no slashes, 1-63 chars
bool IsSafeName(const char *name) {
  if (!name)
    return false;
  const int len = lstrlenA(name);
  if (len < 1 || len > 63)
    return false;
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
static bool BuildJsonBody(char *buf, std::size_t cap,
                          const char *product, // may be nullptr → omit field
                          const char *name,    // may be nullptr → omit field
                          const char *data)    // may be nullptr → omit field
{
  if (!buf || cap < 128) return false;
  buf[0] = '{';
  std::size_t pos = 1;
  bool first = true;

  auto append = [&](const char* str) {
      std::size_t len = lstrlenA(str);
      if (pos + len >= cap) return false;
      CopyMemory(buf + pos, str, len);
      pos += len;
      return true;
  };

  auto appendEscaped = [&](const char* key, const char* val) {
      if (!val) return true;
      if (!first) {
          if (!append(",")) return false;
      }
      first = false;
      if (!append("\"")) return false;
      if (!append(key)) return false;
      if (!append("\":\"")) return false;
      
      for (const char* p = val; *p; ++p) {
          if (pos + 3 >= cap) return false;
          if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t') {
              buf[pos++] = '\\';
              if (*p == '\n') buf[pos++] = 'n';
              else if (*p == '\r') buf[pos++] = 'r';
              else if (*p == '\t') buf[pos++] = 't';
              else buf[pos++] = *p;
          } else {
              buf[pos++] = *p;
          }
      }
      if (!append("\"")) return false;
      return true;
  };

  if (!appendEscaped("product", product)) return false;
  if (!appendEscaped("name", name)) return false;
  if (!appendEscaped("data", data)) return false;

  if (pos + 2 >= cap) return false;
  buf[pos++] = '}';
  buf[pos] = '\0';
  return true;
}

static __declspec(noinline) bool BuildAuthHeader(
    wchar_t* output,
    std::size_t capacity,
    bool isJson) noexcept {
  bool ok = false;
  SHADOW_PROTECT_ULTRA("Cloud.AuthHeader");
  do {
    if (!output || capacity < 64)
      break;
    char token[512]{};
    if (!LoadToken(token, sizeof(token))) {
      SecureZeroMemory(token, sizeof(token));
      break;
    }
    wchar_t wideToken[512]{};
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        0,
        token,
        -1,
        wideToken,
        static_cast<int>(sizeof(wideToken) / sizeof(wideToken[0])));
    SecureZeroMemory(token, sizeof(token));
    if (converted <= 0) {
      SecureZeroMemory(wideToken, sizeof(wideToken));
      break;
    }
    const int written = swprintf_s(
        output,
        capacity,
        L"Authorization: Bearer %s\r\n"
        L"Content-Type: %s\r\n"
        L"Cache-Control: no-store\r\n",
        wideToken,
        isJson ? L"application/json" : L"text/plain");
    SecureZeroMemory(wideToken, sizeof(wideToken));
    ok = written > 0;
  } while (false);
  SHADOW_PROTECT_END();
  return ok;
}

// ── Generic HTTPS POST to our backend ──
static bool Request(const wchar_t *path,
                    const char *bodyPayload, // may be nullptr for list endpoint
                    bool isJson,
                    char *response, std::size_t responseCapacity,
                    DWORD &status) {
  status = 0;
  if (!HasToken() || !path || !response || responseCapacity < 2) {
    SetError("Cloud session unavailable");
    return false;
  }
  response[0] = 0;

  HINTERNET hSession = WinHttpOpen(
      L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36", 
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) {
    SetError("WinHttpOpen failed");
    return false;
  }
  WinHttpSetTimeouts(hSession, 5000, 5000, 8000, 12000);

  HINTERNET hConn =
      WinHttpConnect(hSession, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConn) {
    WinHttpCloseHandle(hSession);
    SetError("WinHttpConnect failed");
    return false;
  }

  const wchar_t *method = bodyPayload ? L"POST" : L"GET";
  HINTERNET hReq =
      WinHttpOpenRequest(hConn, method, path, nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!hReq) {
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSession);
    SetError("WinHttpOpenRequest failed");
    return false;
  }

  // Keep WinHTTP's normal certificate validation. Ignoring CA, hostname and
  // expiry errors turns the short-lived plaintext below into a trivial MITM
  // token leak and defeats the point of protecting it at rest.

  // Build the header in a short-lived buffer. BuildAuthHeader decrypts the
  // token only on its own stack and wipes both narrow and wide copies.
  wchar_t authHdr[640]{};
  if (!BuildAuthHeader(
          authHdr,
          sizeof(authHdr) / sizeof(authHdr[0]),
          isJson)) {
    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSession);
    SetError("Cloud session unavailable");
    return false;
  }

  const DWORD bodyLen = bodyPayload ? static_cast<DWORD>(lstrlenA(bodyPayload)) : 0;

  DWORD lastErr = 0;
  bool ok = WinHttpSendRequest(hReq, authHdr, static_cast<DWORD>(-1L),
                               bodyLen ? const_cast<char *>(bodyPayload) : nullptr,
                               bodyLen, bodyLen, 0) != FALSE &&
            WinHttpReceiveResponse(hReq, nullptr) != FALSE;
  
  if (!ok) lastErr = GetLastError();

  SecureZeroMemory(authHdr, sizeof(authHdr));

  if (ok) {
    DWORD szStatus = sizeof(status);
    ok = WinHttpQueryHeaders(
             hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
             WINHTTP_HEADER_NAME_BY_INDEX, &status, &szStatus,
             WINHTTP_NO_HEADER_INDEX) != FALSE;
    if (!ok) lastErr = GetLastError();
  }

  std::size_t used = 0;
  while (ok) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(hReq, &avail)) {
      lastErr = GetLastError();
      ok = false;
      break;
    }
    if (!avail)
      break;
    if (used + avail + 1 > responseCapacity) {
      avail = static_cast<DWORD>(responseCapacity - used - 1);
    }
    if (avail == 0) {
      break;
    }
    DWORD rd = 0;
    if (!WinHttpReadData(hReq, response + used, avail, &rd)) {
      lastErr = GetLastError();
      ok = false;
      break;
    }
    used += rd;
    if (used + 1 >= responseCapacity) {
      break; // Truncate and stop reading
    }
  }
  response[used] = 0;

  WinHttpCloseHandle(hReq);
  WinHttpCloseHandle(hConn);
  WinHttpCloseHandle(hSession);

  if (!ok && lastErr != 0) {
    char err[64]{};
    _snprintf_s(err, sizeof(err), _TRUNCATE, "Cloud network error (%lu)", lastErr);
    SetError(err);
    return false;
  } else if (!ok) {
    return false; // Error already set by 'too large'
  }
  if (status == 401) {
    ClearToken();
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
static __declspec(noinline) bool RefreshTokenFromSharedMem() {
  bool ok = false;
  SHADOW_PROTECT_ULTRA("Cloud.Session");
  do {
    bool writable = true;
    HANDLE hMap = OpenFileMappingA(
        FILE_MAP_READ | FILE_MAP_WRITE,
        FALSE,
        xorstr_("Local\\SHADOW_IPC_AUTH_SECTION"));
    if (!hMap) {
      writable = false;
      hMap = OpenFileMappingA(
          FILE_MAP_READ,
          FALSE,
          xorstr_("Local\\SHADOW_IPC_AUTH_SECTION"));
    }
    if (!hMap)
      break;

    auto* auth = static_cast<ShadowSharedAuth*>(
        MapViewOfFile(
            hMap,
            writable ? FILE_MAP_READ | FILE_MAP_WRITE : FILE_MAP_READ,
            0,
            0,
            sizeof(ShadowSharedAuth)));
    if (auth && auth->magic == SHADOW_SHARED_MAGIC && auth->version == 1 &&
        auth->token[0]) {
      ok = StoreToken(auth->token);
      if (ok) {
        lstrcpynA(g_Username, auth->username, sizeof(g_Username));
        // days_remaining actually stores HOURS remaining, not days
        const int32_t hoursLeft = auth->days_remaining;
        if (hoursLeft > 0) {
          const std::int64_t now = static_cast<std::int64_t>(time(nullptr));
          g_ExpiresAt = now + static_cast<std::int64_t>(hoursLeft) * 3600;
        } else {
          g_ExpiresAt = 0;
        }
      }
      // NOTE: Do NOT wipe the token from shared memory here.
      // The shared memory section is owned by the loader process; when the
      // loader closes g_hSharedMemory the OS releases the backing store.
      // Wiping early causes EnsureSession() re-reads to find an empty token
      // and fail, breaking config/script load after the first Initialize().
    }
    if (auth)
      UnmapViewOfFile(auth);
    CloseHandle(hMap);
  } while (false);
  SHADOW_PROTECT_END();
  return ok;
}

// ── Parse comma-separated list response into Item array ──
// Server returns: "name1,name2" or "name1|ts1,name2|ts2"
static int ParseListResponse(const char *response, Item *items, int capacity) {
  int count = 0;
  const char *p = response;
  while (*p && count < capacity) {
    const char *comma = strchr(p, ',');
    std::size_t len = comma ? (std::size_t)(comma - p) : lstrlenA(p);
    if (len > 0 && len < sizeof(items[count].name)) {
      items[count].updatedAt = 0;
      const char *pipe = (const char*)memchr(p, '|', len);
      if (pipe) {
        std::size_t nameLen = (std::size_t)(pipe - p);
        if (nameLen < sizeof(items[count].name)) {
          memcpy(items[count].name, p, nameLen);
          items[count].name[nameLen] = 0;
        }
        items[count].updatedAt = std::atoll(pipe + 1);
      } else {
        memcpy(items[count].name, p, len);
        items[count].name[len] = 0;
      }
      lstrcpynA(items[count].id, items[count].name, sizeof(items[count].id));
      ++count;
    }
    if (!comma)
      break;
    p = comma + 1;
  }
  return count;
}

bool RequestJsonPost(
    const wchar_t *path,
    const char *body,
    DWORD &status) {
  char response[2048]{};
  return Request(path, body, true, response, sizeof(response), status);
}

} // anonymous namespace

// ────────────────────────────────────────────────────────────
//  Public API
// ────────────────────────────────────────────────────────────

void Initialize() {
  ClearToken();
  RefreshTokenFromSharedMem();
}

bool EnsureSession() {
  if (HasToken())
    return true;
  return RefreshTokenFromSharedMem();
}

void Shutdown() {
  ClearToken();
  SecureZeroMemory(g_Username, sizeof(g_Username));
}

bool IsAvailable() {
  if (HasToken())
    return true;
  return RefreshTokenFromSharedMem();
}

const char *LastError() { return g_Error; }

// Returns Unix timestamp of subscription expiry.
// Populated from the loader's shared memory (days_remaining field = hours).
std::int64_t SubscriptionEndsAt() noexcept { return g_ExpiresAt; }

const char* GetUsername() noexcept { return g_Username; }

bool PostJson(const wchar_t *path, const char *body, unsigned long &status) {
  status = 0;
  if (!EnsureSession()) {
    SetError("Cloud session unavailable");
    return false;
  }
  DWORD winStatus = 0;
  const bool ok = RequestJsonPost(path, body, winStatus);
  status = winStatus;
  return ok;
}

bool VerifySubscription() {
  if (!EnsureSession()) {
    SetError("Cloud session unavailable");
    return false; // Wait, we should return true if it's just unavailable? No, if session is entirely missing, that's bad.
  }
  
  wchar_t path[128]{};
  swprintf_s(path, L"/api/configs/list?product=%S", kConfigProduct);

  char response[2048]{};
  DWORD status = 0;
  bool ok = Request(path, nullptr, false, response, sizeof(response), status);
  
  // If request failed but status is 0, it means it's a network error (internet disconnected)
  // We do not want to crash the user for bad internet.
  if (!ok && status == 0) {
      return true; // Network error, pretend it's fine for now
  }
  
  // If status is 401/403 or similar unauthorized, subscription is invalid
  if (status == 401 || status == 403) {
      return false; // Explicitly rejected
  }
  
  return true; // Valid or other non-fatal error
}

// ── List: GET /api/configs/list?product=ragemp  or  GET /api/scripts/list ──
bool List(const char *kind, Item *items, int capacity, int &count) {
  count = 0;
  if (!IsSafeKind(kind) || !items || capacity <= 0)
    return false;

  if (!EnsureSession()) {
    SetError("Cloud session unavailable");
    return false;
  }

  wchar_t path[128]{};
  if (!lstrcmpA(kind, "config")) {
    swprintf_s(path, L"/api/configs/list?product=%S&with_dates=1", kConfigProduct);
  } else {
    swprintf_s(path, xorstr_(L"/api/scripts/list"));
  }

  char response[65536]{};
  DWORD status = 0;
  if (!Request(path, nullptr, false, response, sizeof(response), status))
    return false;

  count = ParseListResponse(response, items, capacity);
  return true;
}

// ── Get: POST /api/configs/load  or  /api/scripts/load ──
// `id` parameter is treated as the item name (not UUID)
bool Get(const char *kind,
         const char *id, // name of the item
         char *content, std::size_t capacity) {
  if (!IsSafeKind(kind) || !id || !content || capacity < 2)
    return false;

  if (!EnsureSession()) {
    SetError("Cloud session unavailable");
    return false;
  }

  wchar_t path[64]{};
  if (!lstrcmpA(kind, "config")) {
    swprintf_s(path, xorstr_(L"/api/configs/load"));
  } else {
    swprintf_s(path, xorstr_(L"/api/scripts/load"));
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

  // Config payload can be up to 128 KB; after JSON escaping and wrapping
  // in {"data":"..."} it can reach ~384 KB.  Use the heap.
  constexpr std::size_t kRawCap = 393216; // 384 KB
  char* raw = new (std::nothrow) char[kRawCap]();
  if (!raw) {
    SetError("Out of memory");
    return false;
  }
  DWORD status = 0;
  if (!Request(path, body, true, raw, kRawCap, status)) {
    delete[] raw;
    return false;
  }

  // Response is JSON: {"data":"..."}  — extract value between "data":"..."
  const char *dataKey = "\"data\":\"";
  const char *start = strstr(raw, dataKey);
  if (!start) {
    // Possibly plain-text response
    lstrcpynA(content, raw, static_cast<int>(capacity));
    delete[] raw;
    return true;
  }
  start += lstrlenA(dataKey);
  // Un-escape: copy until closing unescaped quote
  std::size_t out = 0;
  for (const char *p = start; *p && out + 1 < capacity; ++p) {
    if (*p == '\\' && *(p + 1)) {
      char next = *(p + 1);
      if (next == 'n') {
        content[out++] = '\n';
        ++p;
      } else if (next == 'r') {
        content[out++] = '\r';
        ++p;
      } else if (next == 't') {
        content[out++] = '\t';
        ++p;
      } else if (next == '"') {
        content[out++] = '"';
        ++p;
      } else if (next == '\\') {
        content[out++] = '\\';
        ++p;
      } else {
        content[out++] = '\\';
        content[out++] = next;
        ++p;
      }
    } else if (*p == '"') {
      break;
    } else {
      content[out++] = *p;
    }
  }
  content[out] = 0;
  delete[] raw;
  return true;
}

// ── Upsert: POST /api/configs/save?name=...&product=...  or  /api/scripts/save ──
bool Upsert(const char *kind, const char *name, const char *content) {
  if (!IsSafeKind(kind) || !IsSafeName(name) || !content)
    return false;

  if (!EnsureSession()) {
    SetError("Cloud session unavailable");
    return false;
  }

  if (!lstrcmpA(kind, "config")) {
    // Send raw config text as body; name and product go in the query string.
    // This avoids JSON-encoding a potentially large payload.
    wchar_t path[192]{};
    wchar_t wName[128]{};
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wName,
                        static_cast<int>(sizeof(wName) / sizeof(wName[0])));
    swprintf_s(path,
               xorstr_(L"/api/configs/save?product=%S&name=%s"),
               kConfigProduct, wName);
    char response[128]{};
    DWORD status = 0;
    return Request(path, content, false, response, sizeof(response), status);
  }

  // Scripts: keep JSON body
  wchar_t path[64]{};
  swprintf_s(path, xorstr_(L"/api/scripts/save"));

  const std::size_t contentLen =
      content ? static_cast<std::size_t>(lstrlenA(content)) : 0;
  // x2 for escaping + overhead for keys/quotes
  const std::size_t cap = contentLen * 2 + 1024;
  char* body = new (std::nothrow) char[cap];
  if (!body) {
    SetError("Out of memory");
    return false;
  }
  bool ok = BuildJsonBody(body, cap, nullptr, name, content);
  if (ok) {
    char response[128]{};
    DWORD status = 0;
    ok = Request(path, body, true, response, sizeof(response), status);
  } else {
    SetError("Failed to build JSON");
  }
  delete[] body;
  return ok;
}

// ── Delete: POST /api/configs/delete  or  /api/scripts/delete ──
// `id` parameter is the item name
bool Delete(const char *kind, const char *id) {
  if (!IsSafeKind(kind) || !id)
    return false;

  if (!EnsureSession()) {
    SetError("Cloud session unavailable");
    return false;
  }

  wchar_t path[64]{};
  if (!lstrcmpA(kind, "config")) {
    swprintf_s(path, xorstr_(L"/api/configs/delete"));
  } else {
    swprintf_s(path, xorstr_(L"/api/scripts/delete"));
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
  return Request(path, body, true, response, sizeof(response), status);
}

} // namespace CloudStorage
