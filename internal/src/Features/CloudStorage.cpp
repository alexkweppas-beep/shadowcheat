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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <winhttp.h>
#include <new>

#include "../xorstr.hpp"
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

// Live JWT token copied from shared memory
char g_Token[512]{};
char g_Error[128] = "Cloud unavailable";

// Unix timestamp of subscription expiry (0 = unknown/expired).
// Computed from auth->days_remaining which the loader fills in *days*.
std::int64_t g_ExpiresAt = 0;

char g_Username[64]{};

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

// ── Generic HTTPS POST to our backend ──
static bool Request(const wchar_t *path,
                    const char *bodyPayload, // may be nullptr for list endpoint
                    bool isJson,
                    char *response, std::size_t responseCapacity,
                    DWORD &status) {
  status = 0;
  if (!g_Token[0] || !path || !response || responseCapacity < 2) {
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

  DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                  SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                  SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                  SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
  WinHttpSetOption(hReq, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));

  // Build Authorization header using the JWT from shared memory
  wchar_t authHdr[640]{};
  {
    wchar_t wToken[512]{};
    MultiByteToWideChar(CP_UTF8, 0, g_Token, -1, wToken,
                        static_cast<int>(sizeof(wToken) / sizeof(wToken[0])));
    swprintf_s(authHdr,
               L"Authorization: Bearer %s\r\n"
               L"Content-Type: %s\r\n"
               L"Cache-Control: no-store\r\n",
               wToken, isJson ? L"application/json" : L"text/plain");
    SecureZeroMemory(wToken, sizeof(wToken));
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
static bool RefreshTokenFromSharedMem() {
  // The loader self-cleans and exits right after inject, so it no
  // longer anchors the mapping object. A name-opened object dies
  // with its last handle, so a successful read keeps our handle
  // open — the section then outlives the loader for later refreshes.
  static HANDLE s_KeepAlive = nullptr;
  HANDLE hMap = s_KeepAlive;
  if (!hMap) {
    hMap = OpenFileMappingA(FILE_MAP_READ, FALSE,
                            xorstr_("Local\\SHADOW_IPC_AUTH_SECTION"));
    if (!hMap)
      return false;
  }

  const auto *auth = static_cast<const ShadowSharedAuth *>(
      MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(ShadowSharedAuth)));
  bool ok = false;
  if (auth && auth->magic == SHADOW_SHARED_MAGIC && auth->version == 1 &&
      auth->token[0]) {
    lstrcpynA(g_Token, auth->token, sizeof(g_Token));
    lstrcpynA(g_Username, auth->username, sizeof(g_Username));
    // days_remaining actually stores HOURS remaining, not days
    const int32_t hoursLeft = auth->days_remaining;
    if (hoursLeft > 0) {
      const std::int64_t now = static_cast<std::int64_t>(time(nullptr));
      g_ExpiresAt = now + static_cast<std::int64_t>(hoursLeft) * 3600;
    } else {
      g_ExpiresAt = 0;
    }
    ok = true;
  }
  if (auth)
    UnmapViewOfFile(auth);
  if (ok)
    s_KeepAlive = hMap; // hold the section object open
  else if (!s_KeepAlive)
    CloseHandle(hMap);
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

} // anonymous namespace

// ────────────────────────────────────────────────────────────
//  Public API
// ────────────────────────────────────────────────────────────

void Initialize() {
  SecureZeroMemory(g_Token, sizeof(g_Token));
  RefreshTokenFromSharedMem();
}

bool EnsureSession() {
  if (g_Token[0])
    return true;
  return RefreshTokenFromSharedMem();
}

void Shutdown() { SecureZeroMemory(g_Token, sizeof(g_Token)); }

bool IsAvailable() {
  if (g_Token[0])
    return true;
  return RefreshTokenFromSharedMem();
}

const char *LastError() { return g_Error; }

// Returns Unix timestamp of subscription expiry.
// Populated from the loader's shared memory (days_remaining field = hours).
std::int64_t SubscriptionEndsAt() noexcept { return g_ExpiresAt; }

const char* GetUsername() noexcept { return g_Username; }

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
