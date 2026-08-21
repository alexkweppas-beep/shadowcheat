#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CloudStorage.h"

#pragma comment(lib, "winhttp.lib")

namespace CloudStorage
{
    namespace
    {
        constexpr wchar_t kHost[] = L"oeefhdljyfrcjanbzzxo.supabase.co";
        constexpr wchar_t kSessionPath[] = L"C:\\RAGEMP\\cloud.session";
        char g_Token[65]{};
        std::int64_t g_ExpiresAt = 0;
        char g_Error[96] = "Cloud session unavailable";

        void SetError(const char* value)
        {
            lstrcpynA(g_Error, value ? value : "Cloud error", sizeof(g_Error));
        }

        bool IsSafeKind(const char* kind)
        {
            return kind &&
                (!lstrcmpA(kind, "config") || !lstrcmpA(kind, "script"));
        }

        bool IsSafeId(const char* id)
        {
            if (!id || lstrlenA(id) != 36)
                return false;
            for (int i = 0; i < 36; ++i) {
                const char c = id[i];
                if (i == 8 || i == 13 || i == 18 || i == 23) {
                    if (c != '-') return false;
                } else if (!((c >= '0' && c <= '9') ||
                             (c >= 'a' && c <= 'f') ||
                             (c >= 'A' && c <= 'F'))) {
                    return false;
                }
            }
            return true;
        }

        bool Utf8NameIsSafe(const char* name)
        {
            if (!name)
                return false;
            const int length = lstrlenA(name);
            if (length < 1 || length > 63)
                return false;
            for (int i = 0; i < length; ++i) {
                const unsigned char c =
                    static_cast<unsigned char>(name[i]);
                if (c < 0x20 || c == 0x7f || c == '/' || c == '\\')
                    return false;
            }
            return true;
        }

        bool Base64Encode(
            const unsigned char* data,
            std::size_t size,
            char* output,
            std::size_t capacity)
        {
            static constexpr char alphabet[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const std::size_t required = ((size + 2) / 3) * 4 + 1;
            if (!data || !output || capacity < required)
                return false;
            std::size_t in = 0;
            std::size_t out = 0;
            while (in < size) {
                const unsigned a = data[in++];
                const unsigned b = in < size ? data[in++] : 0;
                const unsigned c = in < size ? data[in++] : 0;
                const unsigned value = (a << 16) | (b << 8) | c;
                output[out++] = alphabet[(value >> 18) & 63];
                output[out++] = alphabet[(value >> 12) & 63];
                output[out++] =
                    (in - 1 <= size) && (in > size + 1)
                    ? '=' : alphabet[(value >> 6) & 63];
                output[out++] =
                    in > size ? '=' : alphabet[value & 63];
            }
            const std::size_t remainder = size % 3;
            if (remainder == 1) {
                output[out - 2] = '=';
                output[out - 1] = '=';
            } else if (remainder == 2) {
                output[out - 1] = '=';
            }
            output[out] = 0;
            return true;
        }

        int Base64Value(char c)
        {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        }

        bool Base64Decode(
            const char* input,
            char* output,
            std::size_t capacity)
        {
            if (!input || !output || capacity == 0)
                return false;
            std::size_t out = 0;
            int accumulator = 0;
            int bits = -8;
            for (const char* p = input; *p; ++p) {
                if (*p == '=') break;
                const int value = Base64Value(*p);
                if (value < 0) return false;
                accumulator = (accumulator << 6) | value;
                bits += 6;
                if (bits >= 0) {
                    if (out + 1 >= capacity) return false;
                    output[out++] =
                        static_cast<char>((accumulator >> bits) & 0xff);
                    bits -= 8;
                }
            }
            output[out] = 0;
            return true;
        }

        bool UrlEncode(
            const char* input,
            wchar_t* output,
            std::size_t capacity)
        {
            static constexpr char hex[] = "0123456789ABCDEF";
            if (!input || !output || capacity == 0)
                return false;
            std::size_t out = 0;
            for (const unsigned char* p =
                    reinterpret_cast<const unsigned char*>(input);
                 *p; ++p) {
                const unsigned char c = *p;
                const bool safe =
                    (c >= 'A' && c <= 'Z') ||
                    (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.';
                if (safe) {
                    if (out + 1 >= capacity) return false;
                    output[out++] = static_cast<wchar_t>(c);
                } else {
                    if (out + 3 >= capacity) return false;
                    output[out++] = L'%';
                    output[out++] = hex[(c >> 4) & 15];
                    output[out++] = hex[c & 15];
                }
            }
            output[out] = 0;
            return true;
        }

        bool Request(
            const wchar_t* path,
            const char* body,
            char* response,
            std::size_t responseCapacity,
            DWORD& status)
        {
            status = 0;
            if (!g_Token[0] || !path || !response ||
                responseCapacity < 2) {
                SetError("Cloud session unavailable");
                return false;
            }
            response[0] = 0;

            HINTERNET session = WinHttpOpen(
                L"Butterfly-Cloud/1",
                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0);
            if (!session) {
                SetError("Network session failed");
                return false;
            }
            WinHttpSetTimeouts(session, 3000, 3000, 5000, 8000);
            HINTERNET connection = WinHttpConnect(
                session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
            HINTERNET request = connection
                ? WinHttpOpenRequest(
                    connection, L"POST", path, nullptr,
                    WINHTTP_NO_REFERER,
                    WINHTTP_DEFAULT_ACCEPT_TYPES,
                    WINHTTP_FLAG_SECURE)
                : nullptr;
            if (!request) {
                if (connection) WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                SetError("Network request failed");
                return false;
            }

            wchar_t headers[256]{};
            wchar_t wideToken[65]{};
            MultiByteToWideChar(
                CP_ACP, 0, g_Token, -1,
                wideToken,
                static_cast<int>(
                    sizeof(wideToken) / sizeof(wideToken[0])));
            swprintf_s(
                headers,
                L"Authorization: Bearer %s\r\n"
                L"Content-Type: text/plain; charset=utf-8\r\n"
                L"Cache-Control: no-store\r\n",
                wideToken);
            SecureZeroMemory(wideToken, sizeof(wideToken));

            const DWORD bodySize =
                body ? static_cast<DWORD>(lstrlenA(body)) : 0;
            bool ok =
                WinHttpSendRequest(
                    request,
                    headers,
                    static_cast<DWORD>(-1L),
                    bodySize ? const_cast<char*>(body) : nullptr,
                    bodySize,
                    bodySize,
                    0) != FALSE &&
                WinHttpReceiveResponse(request, nullptr) != FALSE;
            if (ok) {
                DWORD statusSize = sizeof(status);
                ok = WinHttpQueryHeaders(
                    request,
                    WINHTTP_QUERY_STATUS_CODE |
                        WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &status,
                    &statusSize,
                    WINHTTP_NO_HEADER_INDEX) != FALSE;
            }

            std::size_t used = 0;
            while (ok) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) {
                    ok = false;
                    break;
                }
                if (!available) break;
                if (used + available + 1 > responseCapacity) {
                    SetError("Cloud response too large");
                    ok = false;
                    break;
                }
                DWORD read = 0;
                if (!WinHttpReadData(
                        request,
                        response + used,
                        available,
                        &read)) {
                    ok = false;
                    break;
                }
                used += read;
            }
            response[used] = 0;
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);

            if (!ok) {
                SetError("Cloud network error");
                return false;
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
    }

    bool LoadSessionFromDisk()
    {
        HANDLE file = CreateFileW(
            kSessionPath,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        char buf[96]{};
        DWORD read = 0;
        const bool ok =
            ReadFile(file, buf, sizeof(buf) - 1, &read, nullptr) &&
            read >= 64;
        CloseHandle(file);
        if (!ok) {
            SetError("Cloud session read failed");
            return false;
        }

        for (int i = 0; i < 64; ++i) {
            const char c = buf[i];
            if (!((c >= '0' && c <= '9') ||
                  (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) {
                SecureZeroMemory(buf, sizeof(buf));
                SetError("Cloud session invalid");
                return false;
            }
        }

        memcpy(g_Token, buf, 64);
        g_Token[64] = 0;
        g_ExpiresAt = 0;
        if (read > 65 && buf[64] == '\n')
        {
            buf[read] = 0;
            g_ExpiresAt = _strtoi64(buf + 65, nullptr, 10);
            if (g_ExpiresAt < 0)
                g_ExpiresAt = 0;
        }
        SecureZeroMemory(buf, sizeof(buf));
        DeleteFileW(kSessionPath);
        SetError("");
        return true;
    }

    void Initialize()
    {
        SecureZeroMemory(g_Token, sizeof(g_Token));
        g_ExpiresAt = 0;
        LoadSessionFromDisk();
    }

    bool EnsureSession()
    {
        if (g_Token[0])
            return true;
        return LoadSessionFromDisk();
    }

    void Shutdown()
    {
        SecureZeroMemory(g_Token, sizeof(g_Token));
        g_ExpiresAt = 0;
    }

    bool IsAvailable()
    {
        if (g_Token[0])
            return true;
        // Late recover if forensic wipe / inject-ready race delayed the read.
        return LoadSessionFromDisk();
    }

    const char* LastError()
    {
        return g_Error;
    }

    std::int64_t SubscriptionEndsAt() noexcept
    {
        return g_ExpiresAt;
    }

    bool List(const char* kind, Item* items, int capacity, int& count)
    {
        count = 0;
        if (!IsSafeKind(kind) || !items || capacity <= 0)
            return false;
        wchar_t path[160]{};
        swprintf_s(
            path,
            L"/functions/v1/cloud-library?action=list&kind=%S",
            kind);
        char response[8192]{};
        DWORD status = 0;
        if (!Request(path, nullptr, response, sizeof(response), status))
            return false;

        char* cursor = response;
        while (*cursor && count < capacity) {
            char* lineEnd = strchr(cursor, '\n');
            if (lineEnd) *lineEnd = 0;
            char* separator = strchr(cursor, '\t');
            if (separator) {
                *separator = 0;
                if (IsSafeId(cursor)) {
                    lstrcpynA(items[count].id, cursor, sizeof(items[count].id));
                    if (Base64Decode(
                            separator + 1,
                            items[count].name,
                            sizeof(items[count].name))) {
                        ++count;
                    }
                }
            }
            if (!lineEnd) break;
            cursor = lineEnd + 1;
        }
        return true;
    }

    bool Get(
        const char* kind,
        const char* id,
        char* content,
        std::size_t capacity)
    {
        if (!IsSafeKind(kind) || !IsSafeId(id) ||
            !content || capacity < 2)
            return false;
        wchar_t path[192]{};
        swprintf_s(
            path,
            L"/functions/v1/cloud-library?action=get&kind=%S&id=%S",
            kind,
            id);
        DWORD status = 0;
        return Request(path, nullptr, content, capacity, status);
    }

    bool Upsert(
        const char* kind,
        const char* name,
        const char* content)
    {
        if (!IsSafeKind(kind) || !Utf8NameIsSafe(name) || !content)
            return false;
        char encoded[128]{};
        if (!Base64Encode(
                reinterpret_cast<const unsigned char*>(name),
                lstrlenA(name),
                encoded,
                sizeof(encoded)))
            return false;
        wchar_t escaped[384]{};
        if (!UrlEncode(
                encoded,
                escaped,
                sizeof(escaped) / sizeof(escaped[0])))
            return false;
        wchar_t path[512]{};
        swprintf_s(
            path,
            L"/functions/v1/cloud-library?action=upsert&kind=%S&name=%s",
            kind,
            escaped);
        char response[128]{};
        DWORD status = 0;
        return Request(path, content, response, sizeof(response), status);
    }

    bool Delete(const char* kind, const char* id)
    {
        if (!IsSafeKind(kind) || !IsSafeId(id))
            return false;
        wchar_t path[192]{};
        swprintf_s(
            path,
            L"/functions/v1/cloud-library?action=delete&kind=%S&id=%S",
            kind,
            id);
        char response[64]{};
        DWORD status = 0;
        return Request(path, nullptr, response, sizeof(response), status);
    }
}
