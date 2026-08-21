#include "security/xorstr.hpp"
#include "http.hpp"
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include "../security/vmp_markers.hpp"
#include "../security/lazy_importer.hpp"

#pragma comment(lib, "winhttp.lib")

namespace http {

    static std::wstring s2ws(const std::string& s) {
        SHADOW_PROTECT_MUT("http_s2ws");
        if (s.empty()) {
            SHADOW_PROTECT_END();
            return std::wstring();
        }
        int size_needed = LI_FN(MultiByteToWideChar).safe_cached()(CP_UTF8, 0, &s[0], (int)s.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        LI_FN(MultiByteToWideChar).safe_cached()(CP_UTF8, 0, &s[0], (int)s.size(), &wstrTo[0], size_needed);
        SHADOW_PROTECT_END();
        return wstrTo;
    }

    static bool ParseUrl(const std::string& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& isHttps) {
        SHADOW_PROTECT_MUT("http_parse_url");
        std::wstring wUrl = s2ws(url);
        URL_COMPONENTS urlComp = { 0 };
        urlComp.dwStructSize = sizeof(urlComp);
        
        wchar_t hostName[256];
        wchar_t urlPath[1024];
        
        urlComp.lpszHostName = hostName;
        urlComp.dwHostNameLength = sizeof(hostName) / sizeof(wchar_t);
        urlComp.lpszUrlPath = urlPath;
        urlComp.dwUrlPathLength = sizeof(urlPath) / sizeof(wchar_t);

        if (!LI_FN(WinHttpCrackUrl).safe_cached()(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
            SHADOW_PROTECT_END();
            return false;
        }

        host = urlComp.lpszHostName;
        path = urlComp.lpszUrlPath;
        port = urlComp.nPort;
        isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
        
        SHADOW_PROTECT_END();
        return true;
    }

    static std::string execute_request(const std::string& url, const std::string& method, const std::string& data, const std::string& token, const std::string& hwid) {
        SHADOW_PROTECT_MUT("http_execute_request");
        std::wstring host, path;
        INTERNET_PORT port = 0;
        bool isHttps = false;

        if (!ParseUrl(url, host, path, port, isHttps)) {
            SHADOW_PROTECT_END();
            return "";
        }

        std::wstring user_agent = s2ws(xorstr_("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36"));
        HINTERNET hSession = LI_FN(WinHttpOpen).safe_cached()(
            user_agent.c_str(),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );

        if (!hSession) {
            SHADOW_PROTECT_END();
            return "";
        }

        // Set TLS 1.2 / TLS 1.3
        DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        LI_FN(WinHttpSetOption).safe_cached()(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls));

        // Set timeouts (5 seconds)
        DWORD timeout = 5000;
        LI_FN(WinHttpSetOption).safe_cached()(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        LI_FN(WinHttpSetOption).safe_cached()(hSession, WINHTTP_OPTION_SEND_TIMEOUT,    &timeout, sizeof(timeout));
        LI_FN(WinHttpSetOption).safe_cached()(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        HINTERNET hConnect = LI_FN(WinHttpConnect).safe_cached()(hSession, host.c_str(), port, 0);
        if (!hConnect) {
            LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
            SHADOW_PROTECT_END();
            return "";
        }

        DWORD dwFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
        std::wstring wMethod = s2ws(method);
        HINTERNET hRequest = LI_FN(WinHttpOpenRequest).safe_cached()(
            hConnect,
            wMethod.c_str(),
            path.c_str(),
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            dwFlags
        );

        if (!hRequest) {
            LI_FN(WinHttpCloseHandle).safe_cached()(hConnect);
            LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
            SHADOW_PROTECT_END();
            return "";
        }

        if (isHttps) {
            DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | 
                             SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | 
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID | 
                             SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            LI_FN(WinHttpSetOption).safe_cached()(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
        }

        // Add headers
        std::wstring ct_hdr = s2ws(xorstr_("Content-Type: application/json\r\n"));
        LI_FN(WinHttpAddRequestHeaders).safe_cached()(hRequest, ct_hdr.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        if (!token.empty()) {
            std::wstring auth_hdr = s2ws(xorstr_("Authorization: Bearer ")) + s2ws(token) + s2ws(xorstr_("\r\n"));
            LI_FN(WinHttpAddRequestHeaders).safe_cached()(hRequest, auth_hdr.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        if (!hwid.empty()) {
            std::wstring hwid_hdr = s2ws(xorstr_("X-HWID: ")) + s2ws(hwid) + s2ws(xorstr_("\r\n"));
            LI_FN(WinHttpAddRequestHeaders).safe_cached()(hRequest, hwid_hdr.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        bool bResults = LI_FN(WinHttpSendRequest).safe_cached()(
            hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            (LPVOID)data.c_str(),
            (DWORD)data.length(),
            (DWORD)data.length(),
            0
        );

        if (bResults) {
            bResults = LI_FN(WinHttpReceiveResponse).safe_cached()(hRequest, NULL);
        }

        std::string responseStr = "";
        if (bResults) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            do {
                dwSize = 0;
                if (!LI_FN(WinHttpQueryDataAvailable).safe_cached()(hRequest, &dwSize)) {
                    break;
                }
                std::vector<char> buffer(dwSize);
                if (LI_FN(WinHttpReadData).safe_cached()(hRequest, (LPVOID)buffer.data(), dwSize, &dwDownloaded)) {
                    responseStr.append(buffer.data(), dwDownloaded);
                }
            } while (dwSize > 0);
        }

        LI_FN(WinHttpCloseHandle).safe_cached()(hRequest);
        LI_FN(WinHttpCloseHandle).safe_cached()(hConnect);
        LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
        
        SHADOW_PROTECT_END();
        return responseStr;
    }

    std::string get(const std::string& url, const std::string& token, const std::string& hwid) {
        SHADOW_PROTECT_MUT("http_get");
        std::string res = execute_request(url, xorstr_("GET"), "", token, hwid);
        SHADOW_PROTECT_END();
        return res;
    }

    std::string post(const std::string& url, const std::string& json_data, const std::string& token, const std::string& hwid) {
        SHADOW_PROTECT_MUT("http_post");
        std::string res = execute_request(url, xorstr_("POST"), json_data, token, hwid);
        SHADOW_PROTECT_END();
        return res;
    }
}





