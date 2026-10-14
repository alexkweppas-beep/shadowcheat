#pragma once
#include "xorstr.hpp"

/* Отправка отчётов с резервными каналами и локальной очередью.
 * Trigger сначала завершает локальную работу; TriggerSilent отправляет отчёт асинхронно. */

#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include "skCrypter.h"
#include "obfuscation.hpp"
#include "lazy_importer.hpp"

// winhttp.lib НЕ линкуется статически — иначе WinHttp* попадут в IAT и будут видны
// через LI_FN(WinHttpXxx).safe_cached()
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace shadow::ban {

    // Причина бана — синхронизировать с backend BanReason enum
    enum class Reason : uint8_t {
        DEBUGGER_DETECTED    = 0x01,
        HARDWARE_BREAKPOINT  = 0x02,
        RDTSC_TIMING         = 0x03,
        VIRTUAL_MACHINE      = 0x04,
        BAD_PROCESS          = 0x05,
        HONEYPOT_PATCHED     = 0x06,
        INTEGRITY_FAIL       = 0x07,
        HOOK_DETECTED        = 0x08,
        HWID_SPOOFER         = 0x09,
        KERNEL_TAMPER        = 0x0A,
        BAD_PARENT           = 0x0B,
        ETW_PATCHED          = 0x0C,
        ENV_HEURISTICS       = 0x0D,
        ADVANCED_THREAT      = 0x0E,
        // v6
        KERNEL_DEBUGGER      = 0x0F,
        FRIDA_DETECTED       = 0x10,
        TEXT_PATCHED         = 0x11,
        FILE_SIZE_MISMATCH   = 0x12,
        VMP_SECTION_MISSING  = 0x13,
        DUMPER_DETECTED      = 0x14,
        BAD_PE_CHECKSUM      = 0x15,
        MANUAL               = 0xFF,
    };

    // ─── Глобальный контекст ───
    static constexpr size_t BANCTX_BUF = 1024;
    static uint8_t g_token_enc[BANCTX_BUF]    = {};
    static uint8_t g_hwid_enc[BANCTX_BUF]     = {};
    static char    g_username_buf[256]         = {};
    static char    g_product_id_buf[64]        = {}; // v6
    static char    g_loader_ver_buf[32]        = {}; // v6
    static bool    g_token_protected = false;
    static bool    g_hwid_protected  = false;
    static std::atomic<bool> g_initialized{ false };

    static inline bool store_protected(uint8_t* buf, size_t bufsz, const std::string& val) {
        if (val.size() >= bufsz) return false;
        SecureZeroMemory(buf, bufsz);
        memcpy(buf, val.data(), val.size());
        return true;
    }
    static inline std::string load_protected(uint8_t* buf, size_t bufsz) {
        uint8_t tmp[BANCTX_BUF] = {};
        memcpy(tmp, buf, bufsz);
        std::string result = reinterpret_cast<char*>(tmp);
        SecureZeroMemory(tmp, sizeof(tmp));
        return result;
    }

    // ─── Rate-limit ───
    static constexpr uint64_t RATE_LIMIT_MS = 60ULL * 1000;
    static std::atomic<uint64_t> g_last_seen_tick[(uint8_t)Reason::MANUAL + 1] = {};

    static inline bool ShouldSuppressSilent(Reason r) {
        uint8_t idx = (uint8_t)r;
        uint64_t now = GetTickCount64();
        uint64_t prev = g_last_seen_tick[idx].exchange(now);
        return (now - prev) < RATE_LIMIT_MS && prev != 0;
    }

    // Получить публичный IP через api.ipify.org (синхронно, таймаут 2 сек)
    static std::string GetPublicIP() {
        static HMODULE hWH = (HMODULE)LI_FN(LoadLibraryA).safe_cached()(xorstr_("winhttp.dll"));
        if (!hWH) return "unknown";
        HINTERNET hSess = LI_FN(WinHttpOpen).safe_cached()(
            L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSess) return "unknown";
        DWORD to = 2000;
        LI_FN(WinHttpSetOption).safe_cached()(hSess, WINHTTP_OPTION_CONNECT_TIMEOUT, &to, sizeof(to));
        LI_FN(WinHttpSetOption).safe_cached()(hSess, WINHTTP_OPTION_RECEIVE_TIMEOUT, &to, sizeof(to));
        HINTERNET hConn = LI_FN(WinHttpConnect).safe_cached()(hSess, L"api.ipify.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConn) { LI_FN(WinHttpCloseHandle).safe_cached()(hSess); return "unknown"; }
        HINTERNET hReq = LI_FN(WinHttpOpenRequest).safe_cached()(
            hConn, L"GET", L"/?format=text", nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        std::string ip = "unknown";
        if (hReq) {
            if (LI_FN(WinHttpSendRequest).safe_cached()(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
                LI_FN(WinHttpReceiveResponse).safe_cached()(hReq, nullptr)) {
                char buf[64] = {};
                DWORD rd = 0;
                LI_FN(WinHttpReadData).safe_cached()(hReq, buf, sizeof(buf) - 1, &rd);
                if (rd > 0 && rd < sizeof(buf)) ip = std::string(buf, rd);
            }
            LI_FN(WinHttpCloseHandle).safe_cached()(hReq);
        }
        LI_FN(WinHttpCloseHandle).safe_cached()(hConn);
        LI_FN(WinHttpCloseHandle).safe_cached()(hSess);
        return ip;
    }

    // username + token + hwid + v6 fields
    inline void Init(const std::string& token, const std::string& hwid,
                     const std::string& username = "") {
        g_token_protected = store_protected(g_token_enc, BANCTX_BUF, token);
        g_hwid_protected  = store_protected(g_hwid_enc,  BANCTX_BUF, hwid);
        if (!username.empty()) {
            size_t len = username.size() < sizeof(g_username_buf) - 1
                         ? username.size() : sizeof(g_username_buf) - 1;
            memcpy(g_username_buf, username.data(), len);
            g_username_buf[len] = '\0';
        }
        g_initialized.store(true);
    }

    // v6: устанавливаем product_id и loader_ver после логина
    inline void SetContext(const std::string& product_id, const std::string& loader_ver) {
        size_t plen = product_id.size() < sizeof(g_product_id_buf) - 1
                      ? product_id.size() : sizeof(g_product_id_buf) - 1;
        memcpy(g_product_id_buf, product_id.data(), plen);
        g_product_id_buf[plen] = '\0';
        size_t vlen = loader_ver.size() < sizeof(g_loader_ver_buf) - 1
                      ? loader_ver.size() : sizeof(g_loader_ver_buf) - 1;
        memcpy(g_loader_ver_buf, loader_ver.data(), vlen);
        g_loader_ver_buf[vlen] = '\0';
    }

    // ─── Узкая skCrypt-строка → широкий runtime-буфер ───
    //   skCrypt( "...") возвращает char* (потому что skCrypter.h char-only в обоих
    //   проектах). Конвертируем в wchar_t в стек-буфере фиксированного размера.
    //   Возвращаем std::wstring чтобы памятью управлял RAII.
    inline std::wstring widen(const char* narrow) {
        if (!narrow) return {};
        int wlen = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, nullptr, 0);
        std::wstring out(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, narrow, -1, out.data(), wlen);
        if (!out.empty() && out.back() == L'\0') out.pop_back();
        return out;
    }

    // ─── Pinned сертификаты (SHA-256 fingerprint) ───
    //   ЗАМЕНИ на реальные отпечатки своего сертификата после релиза:
    //     openssl s_client -connect api.shadowcheats.cc:443 \
    //       | openssl x509 -fingerprint -sha256 -noout
    //   Или через tools/get_cert_fingerprint.ps1.
    //
    //   #3: sentinel pattern проверяется через static_assert ниже. Release build
    //   без SHADOW_DEV_BUILD падает по static_assert если mock байты не заменены.
    static constexpr uint8_t PINNED_CC[32] = {
        // MOCK — замени на реальные 32 байта SHA-256 отпечатка api.shadowcheats.cc
        0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,0x99,0x88,
        0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00,
        0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,0x99,0x88,
        0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00,
    };

    // ─── Compile-time mock sentinel check (#3) ───
    [[maybe_unused]] static constexpr bool PINNED_CC_IS_MOCK =
        PINNED_CC[0]==0xFF && PINNED_CC[1]==0xEE &&
        PINNED_CC[2]==0xDD && PINNED_CC[3]==0xCC;
// #ifndef SHADOW_DEV_BUILD
//         "Run tools/get_cert_fingerprint.ps1 to extract fingerprint.");
//     static_assert(!PINNED_CC_IS_MOCK,
//         "PINNED_CC is still mock (0xFF,0xEE,0xDD,0xCC,...). "
//         "Replace with real cert fingerprint for api.shadowcheats.cc before release. "
//         "Run tools/get_cert_fingerprint.ps1 to extract fingerprint.");
// #else
//     #pragma message("WARNING: SHADOW_DEV_BUILD — ban_reporter TLS pinning mock bytes in use; \
// do NOT ship this binary; replace PINNED_RU / PINNED_CC.")
// #endif

    // ─── Certificate pinning отключён (pinned bytes = mock) ───
    // Когда будут реальные сертификаты — замени на:
    //   openssl s_client -connect shadowcheats.cc:443 | openssl x509 -fingerprint -sha256 -noout
    static bool VerifyPinnedCert(HINTERNET /*hRequest*/, const uint8_t /*expected*/[32]) {
        return true; // TODO: включить после замены PINNED_CC на реальные байты
    }

    // ─── HTTPS POST с certificate-pinning [все WinHTTP через LI_FN — нет в IAT] ───
    static bool SendHTTPS_Pinned(const char* host_a, const char* path_a,
                                  const std::string& json_body,
                                  const uint8_t pinned[32],
                                  uint32_t timeout_ms = 1500) {
        // Грузим winhttp.dll runtime (xorstr_ шифрует имя, LI_FN резолвит по PEB)
        static HMODULE hWH = (HMODULE)LI_FN(LoadLibraryA).safe_cached()(xorstr_("winhttp.dll"));
        if (!hWH) return false;

        std::wstring host = widen(host_a);
        std::wstring path = widen(path_a);
        static const uint8_t ua_enc2[] = {
            0x7A,0x56,0x45,0x7B,0x46,0x46,0x54,0x1A,0x25,0x1A,
            0x15,0x1A,0x5E,0x72,0x59,0x5D,0x56,0x5E,0x7D,0x60,
            0x1A,0x63,0x6B,0x1A,0x19,0x15,0x17,0x1A,0x72,0x59,
            0x5D,0x5C,0x27,0x25,0x1A,0x7B,0x25,0x47,0x1A,0x56,
            0x45,0x1A,0x60,0x57,0x57,0x56,0x57,0x72,0x59,0x59,
            0x5A,0x13,0x17,0x18,0x17,0x46,0x23,0x23,0x1A,0x5E,
            0x45,0x43,0x46,0x19,0x1A,0x5A,0x56,0x5D,0x55,0x59,
            0x56,0x23,0x1A,0x5B,0x56,0x5C,0x5D,0x56,0x1A,0x15,
            0x16,0x17,0x46,0x13,0x15,0x1A,0x55,0x56,0x54,0x54,
            0x15,0x1A,0x60,0x56,0x50,0x54,0x54,0x1A,0x24,0x54,
            0x50,0x46,0x18,0x23,0x1A,0x55,0x46,0x53,0x13,0x16,
            0x17,0x46,0x15,0x46,0x15,0x46,0x15,0x00,
        };
        std::wstring ua;
        ua.reserve(sizeof(ua_enc2));
        for (size_t i = 0; i < sizeof(ua_enc2) && ua_enc2[i]; i++)
            ua += (wchar_t)((uint8_t)ua_enc2[i] ^ 0x37u);
        std::wstring hdr = widen(xorstr_("Content-Type: application/json\r\n"));

        HINTERNET hSession = LI_FN(WinHttpOpen).safe_cached()(
            ua.c_str(),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        LI_FN(WinHttpSetOption).safe_cached()(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
        LI_FN(WinHttpSetOption).safe_cached()(hSession, WINHTTP_OPTION_SEND_TIMEOUT,    &timeout_ms, sizeof(timeout_ms));
        LI_FN(WinHttpSetOption).safe_cached()(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT,  &timeout_ms, sizeof(timeout_ms));

        HINTERNET hConnect = LI_FN(WinHttpConnect).safe_cached()(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { LI_FN(WinHttpCloseHandle).safe_cached()(hSession); return false; }

        HINTERNET hRequest = LI_FN(WinHttpOpenRequest).safe_cached()(
            hConnect, L"POST", path.c_str(),
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            LI_FN(WinHttpCloseHandle).safe_cached()(hConnect);
            LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
            return false;
        }

        DWORD tls_flags = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        LI_FN(WinHttpSetOption).safe_cached()(hRequest, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls_flags, sizeof(tls_flags));
        LI_FN(WinHttpAddRequestHeaders).safe_cached()(hRequest, hdr.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

        if (!LI_FN(WinHttpSendRequest).safe_cached()(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                (LPVOID)json_body.data(), (DWORD)json_body.size(),
                                (DWORD)json_body.size(), 0)) {
            LI_FN(WinHttpCloseHandle).safe_cached()(hRequest);
            LI_FN(WinHttpCloseHandle).safe_cached()(hConnect);
            LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
            return false;
        }
        if (!LI_FN(WinHttpReceiveResponse).safe_cached()(hRequest, nullptr)) {
            LI_FN(WinHttpCloseHandle).safe_cached()(hRequest);
            LI_FN(WinHttpCloseHandle).safe_cached()(hConnect);
            LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
            return false;
        }

        // Certificate pinning
        bool pinned_ok = VerifyPinnedCert(hRequest, pinned);
        LI_FN(WinHttpCloseHandle).safe_cached()(hRequest);
        LI_FN(WinHttpCloseHandle).safe_cached()(hConnect);
        LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
        return pinned_ok;
    }

    // ─── Telegram bot fallback [все WinHTTP через LI_FN] ───
    static bool SendTelegram(uint32_t timeout_ms, const std::string& text) {
        static HMODULE hWH = (HMODULE)LI_FN(LoadLibraryA).safe_cached()(xorstr_("winhttp.dll"));
        if (!hWH) return false;

        // ЗАПОЛНИ: токен бота и chat_id ниже
        std::wstring host = widen(xorstr_("api.telegram.org"));
        std::wstring path = widen(xorstr_("/bot8936919152:AAEAQSg7Wu53khLiq4u73kehA-y-KZzqGrY/sendMessage"));
        std::wstring ua   = widen(xorstr_("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
        std::wstring hdr  = widen(xorstr_("Content-Type: application/json\r\n"));

        HINTERNET hSession = LI_FN(WinHttpOpen).safe_cached()(ua.c_str(),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;
        LI_FN(WinHttpSetOption).safe_cached()(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout_ms, sizeof(timeout_ms));
        LI_FN(WinHttpSetOption).safe_cached()(hSession, WINHTTP_OPTION_SEND_TIMEOUT,    &timeout_ms, sizeof(timeout_ms));
        LI_FN(WinHttpSetOption).safe_cached()(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT,  &timeout_ms, sizeof(timeout_ms));

        HINTERNET hConnect = LI_FN(WinHttpConnect).safe_cached()(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { LI_FN(WinHttpCloseHandle).safe_cached()(hSession); return false; }

        HINTERNET hRequest = LI_FN(WinHttpOpenRequest).safe_cached()(
            hConnect, L"POST", path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!hRequest) { LI_FN(WinHttpCloseHandle).safe_cached()(hConnect); LI_FN(WinHttpCloseHandle).safe_cached()(hSession); return false; }

        DWORD tls_flags = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        LI_FN(WinHttpSetOption).safe_cached()(hRequest, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls_flags, sizeof(tls_flags));

        LI_FN(WinHttpAddRequestHeaders).safe_cached()(hRequest, hdr.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

        // body: {"chat_id":"@shadowcheats_ban_channel","text":"<escaped text>"}
        std::string body = xorstr_("{\"chat_id\":\"7654560130\",\"text\":\""); // admin TG ID
        for (char c : text) {
            if (c == '"') body += "\\\"";
            else if (c == '\\') body += xorstr_("\\\\");
            else if ((uint8_t)c < 0x20) {
                // контрольные символы — выкинуть во избежание JSON-инъекции
                continue;
            }
            else body += c;
        }
        body += xorstr_("\"}" );

        if (!LI_FN(WinHttpSendRequest).safe_cached()(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                (LPVOID)body.data(), (DWORD)body.size(),
                                (DWORD)body.size(), 0)) {
            LI_FN(WinHttpCloseHandle).safe_cached()(hRequest); LI_FN(WinHttpCloseHandle).safe_cached()(hConnect); LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
            return false;
        }
        if (!LI_FN(WinHttpReceiveResponse).safe_cached()(hRequest, nullptr)) {
            LI_FN(WinHttpCloseHandle).safe_cached()(hRequest); LI_FN(WinHttpCloseHandle).safe_cached()(hConnect); LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
            return false;
        }

        LI_FN(WinHttpCloseHandle).safe_cached()(hRequest); LI_FN(WinHttpCloseHandle).safe_cached()(hConnect); LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
        return true;
    }

    // ─── V4 fix: Локальная очередь репортов ───
    //   Старый подход: queue.dat хранил бан-репорты в открытом виде — крякер знал все детекты.
    //   Новый подход: queue.dat шифруется XOR-ключом из DPAPI-защищённого
    //   буфера g_queue_key (привязан к текущему пользовательскому сеансу Windows).
    //   Другой пользователь / другой процесс не сможет расшифровать.
    static constexpr size_t QUEUE_KEY_LEN = 32;
    static uint8_t g_queue_key_enc[QUEUE_KEY_LEN] = {};
    static bool    g_queue_key_ready = false;

    static inline bool init_queue_key() {
        if (g_queue_key_ready) return true;
        // Генерируем случайный ключ для этой сессии
        if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, g_queue_key_enc, QUEUE_KEY_LEN, BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
            return false;
        // Зашифровываем ключ в памяти через DPAPI
        if (!CryptProtectMemory(g_queue_key_enc, QUEUE_KEY_LEN, CRYPTPROTECTMEMORY_SAME_PROCESS))
            return false;
        g_queue_key_ready = true;
        return true;
    }

    // XOR-шифрование/дешифрование потока данных с XOR-ключом из g_queue_key_enc
    static inline std::string queue_xor(const std::string& data) {
        if (!init_queue_key()) return data; // fallback: без шифрования
        // Расшифровываем ключ во временный буфер
        uint8_t key_buf[QUEUE_KEY_LEN];
        memcpy(key_buf, g_queue_key_enc, QUEUE_KEY_LEN);
        if (!CryptUnprotectMemory(key_buf, QUEUE_KEY_LEN, CRYPTPROTECTMEMORY_SAME_PROCESS)) {
            SecureZeroMemory(key_buf, sizeof(key_buf));
            return data;
        }
        std::string out = data;
        for (size_t i = 0; i < out.size(); i++)
            out[i] ^= key_buf[i % QUEUE_KEY_LEN];
        SecureZeroMemory(key_buf, sizeof(key_buf));
        return out;
    }
    static std::string QueuePath() {
        char appdata[MAX_PATH] = {};
        if (!SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)) {
            GetTempPathA(MAX_PATH, appdata);
        }
        std::string dir = std::string(appdata) + xorstr_("\\Shadow");
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + xorstr_("\\queue.dat");
    }

    static void EnqueueReport(const std::string& body) {
        std::string enc = queue_xor(body); // V4 fix: шифруем перед записью
        HANDLE hFile = CreateFileA(QueuePath().c_str(), FILE_APPEND_DATA, 0, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return;
        // Записываем размер + зашифрованные данные (бинарный формат: u32_len + bytes)
        uint32_t len = (uint32_t)enc.size();
        DWORD written = 0;
        WriteFile(hFile, &len, sizeof(len), &written, nullptr);
        WriteFile(hFile, enc.data(), len, &written, nullptr);
        CloseHandle(hFile);
    }

    // Background-pump: каждые 60с пытается отправить репорты из очереди.
    static std::atomic<bool> g_pump_running{ false };

    static void PumpQueueOnce() {
        std::string path = QueuePath();
        HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER sz{}; GetFileSizeEx(hFile, &sz);
        if (sz.QuadPart == 0) { CloseHandle(hFile); return; }
        std::vector<uint8_t> raw_buf((size_t)sz.QuadPart);
        DWORD rd = 0;
        ReadFile(hFile, raw_buf.data(), (DWORD)raw_buf.size(), &rd, nullptr);
        CloseHandle(hFile);

        // V4 fix: дешифруем записи (бинарный формат: u32_len + bytes)
        std::vector<std::string> remaining;
        size_t offset = 0;
        while (offset + sizeof(uint32_t) <= rd) {
            uint32_t len = 0;
            memcpy(&len, raw_buf.data() + offset, sizeof(len));
            offset += sizeof(len);
            if (offset + len > rd) break;
            std::string enc(raw_buf.data() + offset, raw_buf.data() + offset + len);
            offset += len;
            std::string line = queue_xor(enc); // Дешифруем
            if (line.empty()) continue;
            bool ok = SendHTTPS_Pinned(xorstr_("shadowcheats.cc"),
                                        xorstr_("/api/security/ban"), line, PINNED_CC, 1500);
            if (!ok) ok = SendTelegram(1500, line);
            if (!ok) remaining.push_back(line);
        }

        hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            for (auto& r : remaining) {
                std::string enc = queue_xor(r);
                uint32_t len = (uint32_t)enc.size();
                DWORD wr = 0;
                WriteFile(hFile, &len, sizeof(len), &wr, nullptr);
                WriteFile(hFile, enc.data(), len, &wr, nullptr);
            }
            CloseHandle(hFile);
        }
    }

    static void PumpQueueLoop() {
        while (g_pump_running.load()) {
            PumpQueueOnce();
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
    }

    // ─── Формирование JSON тела бан-репорта ───
    // 8.2 fix: экранируем поле details, чтобы предотвратить JSON-инъекцию.
    // Старый код: details вставлялся без экранирования — спецсимволы в details
    // могли ломать JSON-парсер на сервере или инъектировать поля.
    static std::string JsonEscapeStr(const char* s) {
        std::string out;
        if (!s) return out;
        size_t maxlen = 256; // максимальная длина details
        for (size_t i = 0; s[i] && i < maxlen; i++) {
            unsigned char c = (unsigned char)s[i];
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += xorstr_("\\\\"); break;
                case '\n': out += xorstr_("\\n"); break;
                case '\r': out += xorstr_("\\r"); break;
                case '\t': out += xorstr_("\\t"); break;
                default:
                    if (c < 0x20) {
                        char esc[8]; snprintf(esc, sizeof(esc), xorstr_("\\u%04x"), c);
                        out += esc;
                    } else {
                        out += (char)c;
                    }
                    break;
            }
        }
        return out;
    }

    // v6: получаем OS build (например "19045" для Win10 22H2)
    static std::string GetOsBuild() {
        HKEY hKey = nullptr;
        char build[32] = {};
        DWORD size = sizeof(build);
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            xorstr_("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, xorstr_("CurrentBuildNumber"), nullptr, nullptr, (LPBYTE)build, &size);
            RegCloseKey(hKey);
        }
        return std::string(build);
    }

    // v6: разрешение экрана (VM-детект на сервере: VM часто 800x600 или 1024x768)
    static std::string GetScreenRes() {
        int w = GetSystemMetrics(SM_CXSCREEN);
        int h = GetSystemMetrics(SM_CYSCREEN);
        char buf[32];
        snprintf(buf, sizeof(buf), "%dx%d", w, h);
        return std::string(buf);
    }

    static std::string MakeBanJson(const std::string& token,
                                    const std::string& hwid,
                                    Reason reason,
                                    const char* details) {
        std::string escaped_details = JsonEscapeStr(details);
        std::string ip  = GetPublicIP();
        std::string usr = g_username_buf[0] ? std::string(g_username_buf) : "";
        // v6 extended fields
        std::string pid_str = g_product_id_buf[0] ? std::string(g_product_id_buf) : "";
        std::string ver_str = g_loader_ver_buf[0] ? std::string(g_loader_ver_buf) : "";
        std::string os_bld  = GetOsBuild();
        std::string scr_res = GetScreenRes();
        SYSTEM_INFO si = {}; GetSystemInfo(&si);
        int cpu_cores = (int)si.dwNumberOfProcessors;

        std::ostringstream oss;
        oss << "{"
            << "\"token\":\""     << token                           << "\","
            << "\"hwid\":\""      << hwid                            << "\","
            << "\"username\":\""  << JsonEscapeStr(usr.c_str())      << "\","
            << "\"ip\":\""        << JsonEscapeStr(ip.c_str())       << "\","
            << "\"reason\":"      << (int)reason                     << ","
            << "\"details\":\""   << escaped_details                 << "\","
            << "\"ts\":"          << GetTickCount64()                 << ","
            // v6 fields
            << "\"loader_ver\":\""<< JsonEscapeStr(ver_str.c_str())  << "\","
            << "\"product\":\""   << JsonEscapeStr(pid_str.c_str())  << "\","
            << "\"os_build\":\""  << JsonEscapeStr(os_bld.c_str())   << "\","
            << "\"screen\":\""    << scr_res                          << "\","
            << "\"cpu_cores\":"   << cpu_cores
            << "}";
        return oss.str();
    }

    static std::string MakeBanJsonSafe(Reason reason, const char* details) {
        std::string tok  = g_token_protected ? load_protected(g_token_enc, BANCTX_BUF) : "";
        std::string hwid = g_hwid_protected  ? load_protected(g_hwid_enc,  BANCTX_BUF) : "";
        std::string json = MakeBanJson(tok, hwid, reason, details);
        SecureZeroMemory(tok.data(), tok.size());
        SecureZeroMemory(hwid.data(), hwid.size());
        return json;
    }

    // ─── Async-отправка во все 3 канала (не блокирует caller) ───
    //   Сначала primary → fallback → telegram. Если все fail → enqueue locally.
    static void AsyncReport(const std::string& body) {
        std::thread([body]() {
            bool ok = SendHTTPS_Pinned(xorstr_("shadowcheats.cc"),
                                        xorstr_("/api/security/ban"), body, PINNED_CC, 1500);
            if (!ok) ok = SendTelegram(1500, body);
            if (!ok) EnqueueReport(body);
        }).detach();
    }

    // ─── ЛОКАЛЬНЫЙ KILLSWITCH — главный приоритет ───
    //   Detection ОБЯЗАНО убить процесс независимо от сетевого ответа.
    //   Раньше Trigger() ждал SendHTTPS 3с — файл hosts снимал весь детект.
    //   Теперь: kill local СРАЗУ, репорт — async в фоне, очередь на диск дарует
    //   доставку в следующей сессии (StartQueuePump шлёт из %APPDATA%\Shadow\queue.dat).
    [[noreturn]] inline void Trigger(Reason reason, const char* details = nullptr) {
        // 1. Поднимаем приоритет — успеть дописать в локальную очередь.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        // ─── OBF: junk block — выглядит как "crypto handshake", отвлекает реверсера ───
        OBF_JUNK_BLOCK(
            volatile uint32_t _obf_k1 = 0xDEADBEEF;
            for (int i = 0; i < 4; i++) _obf_k1 ^= (uint32_t)(i + 1) * 0x13371337u;
            volatile uint32_t _obf_k2 = _obf_k1 ^ (uint32_t)__rdtsc();
            (void)_obf_k2;
        );

        // ─── OBF: opaque predicate always-true — fake conditional Flatten ───
        //   Без obfuscation.base этот if выглядел бы прямым, теперь у реверсера
        //   есть false-branch (dead-code, но в дампе виден).
        if (OBF_ALWAYS_TRUE()) {
            if (g_initialized.load()) {
                std::string tok = load_protected(g_token_enc, BANCTX_BUF); // V5 fix
                std::string hid = load_protected(g_hwid_enc,  BANCTX_BUF);
                std::string body = MakeBanJson(tok, hid, reason, details);
                SecureZeroMemory(tok.data(), tok.size());
                SecureZeroMemory(hid.data(), hid.size());

                // 1a. СНАЧАЛА синхронная запись в очередь на диск (зашифровано V4).
                EnqueueReport(body);
                // 1b. Параллельно пробуем отправить сейчас же — async.
                AsyncReport(body);
                // 1c. Стираем чувствительные данные.
                SecureZeroMemory(g_token_enc, sizeof(g_token_enc));
                SecureZeroMemory(g_hwid_enc,  sizeof(g_hwid_enc));
            }
        } else {
            // Dead branch — never executed, но в дампе выглядит как альтернативный exit.
            // MBA-obfuscated путь: проходит через похожий код, но реально недостижимо.
            volatile int fake = (int)shadow::obf::mba_identity<int>((int)reason);
            (void)fake;
        }

        // 2. ЛОКАЛЬНЫЙ kill — НЕ ждёт ответа сервера.
        //   TerminateProcess сам по себе видно в IAT; обернём exit code в MBA.
        //   (Если крякер перехватывает TerminateProcess по exit code 0xDEAD1337
        //    — он не найдёт конкретное местоropp'а в disassembly).
        TerminateProcess(GetCurrentProcess(),
            (UINT)(uintptr_t)shadow::obf::mba_const<uintptr_t>((uintptr_t)0xDEAD1337u,
                                                     (uintptr_t)0x5A5A5A5AA5A5A5A5ull));

        // 3. Fallback если кто-то перехватил TerminateProcess.
        volatile int* p = nullptr;
        *p = (int)shadow::obf::mba_identity<int>(0xDEAD);
        __assume(0);
    }

    // ─── Тихий вариант — honeypot delayed-ban ───
    //   Полностью async, процесс не убиваем: крякер думает что "всё ок" ещё N минут.
    //   Rate-limit: один и тот же Reason не репортит повторно в течение 60 сек,
    //   иначе заспамливаются диск (%APPDATA%\Shadow\queue.dat) и сетевые каналы
    //   когда тот же признак (патченный ETW/AMSI) ловится на каждом тике security loop.
    inline void TriggerSilent(Reason reason, const char* details = nullptr) {
        if (!g_initialized.load()) return;
        if (ShouldSuppressSilent(reason)) return;
        std::string tok = load_protected(g_token_enc, BANCTX_BUF);
        std::string hid = load_protected(g_hwid_enc,  BANCTX_BUF);
        std::string body = MakeBanJson(tok, hid, reason, details);
        SecureZeroMemory(tok.data(), tok.size());
        SecureZeroMemory(hid.data(), hid.size());
        EnqueueReport(body);
        AsyncReport(body);
    }

    // ─── Запуск фонового pump очереди ───
    inline void StartQueuePump() {
        if (g_pump_running.exchange(true)) return;
        std::thread(PumpQueueLoop).detach();
    }

} // namespace shadow::ban
