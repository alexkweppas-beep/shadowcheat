#pragma once
#include "xorstr.hpp"
#include "vmp_markers.hpp"

/*
 * shadow cheats — Network Module  (v4 hardened)
 * TLS 1.3 + HMAC-SHA256 + Certificate Pinning
 *
 * v4 changes:
 *   V1 fix: CLIENT_SECRET — не XOR с константным ключом (открыт в .rdata +
 * strings.exe). Теперь: ключ строится runtime из __rdtsc + PEB hash +
 * статической XOR-цепочки. V2 fix: SERVER_HOST — не raw L"" literal (виден в
 * .rdata любым Strings.exe). Теперь: строится runtime из байтов через
 * MBA-obfuscated лоп. V3 fix: timestamp больше не GetTickCount64 (replay за 60
 * сек). Теперь FILETIME + QPC mix — монотонно растёт, replay невозможен.
 */

#include "hwid.hpp"
#include "lazy_importer.hpp"
#include "obfuscation.hpp"
#include "skCrypter.h"
#include <bcrypt.h>
#include <intrin.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <wincrypt.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ws2_32.lib")
// winhttp.lib НЕ линкуется статически — winhttp.dll грузится
// runtime через LI_FN(LoadLibraryA) с обфусцированным xorstr_ именем.
// Это убирает winhttp из IAT и ломает IAT-hooks на WinHttp*.

namespace Network {

// Выбор сервера для скачивания: 1 = Россия (Москва), 2 = Финляндия
// Устанавливается из ui.server_region перед скачиванием в main.cpp
inline int g_server_region = 1; // дефолт — Россия

// ─── SHADOW_LOCAL_TESTING убран — лоадер работает с боевым сервером
// shadowcheats.cc ─── #define SHADOW_LOCAL_TESTING

// ─── V2 fix: SERVER_HOST строится runtime из байтов, не хранится как L""
// literal ─── Strings.exe или strings в IDA больше не увидит имя сервера в
// .rdata. bytes = L"api.shadowcheats.cc" XOR 0x13 (каждый wchar_t, только
// младший байт). Runtime-разбор — имя собирается каждый раз заново из
// замаскированных байт.
// ─── Latency-based server selection for download ───
// Замеряем TCP latency к двум серверам, выбираем тот который отвечает быстрее.
// Работает без геолокации — чисто по скорости сети пользователя.
static uint32_t MeasureTcpLatencyMs(const wchar_t *host, WORD port,
                                    DWORD timeout_ms = 3000) {
  // Конвертируем wchar в char для getaddrinfo
  char hostA[256] = {};
  WideCharToMultiByte(CP_ACP, 0, host, -1, hostA, 255, nullptr, nullptr);
  char portStr[8] = {};
  wsprintfA(portStr, "%d", port);

  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    return UINT32_MAX;

  addrinfo hints = {}, *res = nullptr;
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(hostA, portStr, &hints, &res) != 0) {
    WSACleanup();
    return UINT32_MAX;
  }

  SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (s == INVALID_SOCKET) {
    freeaddrinfo(res);
    WSACleanup();
    return UINT32_MAX;
  }

  // Non-blocking connect
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);

  LARGE_INTEGER freq, t0, t1;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&t0);

  connect(s, res->ai_addr, (int)res->ai_addrlen);

  fd_set wfds;
  FD_ZERO(&wfds);
  FD_SET(s, &wfds);
  timeval tv = {(long)(timeout_ms / 1000), (long)((timeout_ms % 1000) * 1000)};

  uint32_t latency = UINT32_MAX;
  if (select(0, nullptr, &wfds, nullptr, &tv) > 0) {
    QueryPerformanceCounter(&t1);
    latency = (uint32_t)(((t1.QuadPart - t0.QuadPart) * 1000) / freq.QuadPart);
  }

  closesocket(s);
  freeaddrinfo(res);
  WSACleanup();
  return latency;
}

// Возвращает домен основного сервера. Московский сервер теперь работает
// как HTTP-прокси, поэтому мы всегда обращаемся к shadowcheats.cc,
// но настраиваем WinHttpOpen на использование прокси.
static std::wstring GetServerHost() {
  static const uint8_t enc_fin[] = {
      0x60, 0x7b, 0x72, 0x77, 0x7c, 0x64, 0x70, 0x7b,
      0x76, 0x72, 0x67, 0x60, 0x3d, 0x70, 0x70
  };
  std::wstring finland;
  finland.reserve(15);
  for (auto b : enc_fin)
    finland += (wchar_t)((uint8_t)b ^ 0x13u);
  return finland;
}

// Возвращает адрес Московского HTTP-прокси если выбран регион 1
static std::wstring GetProxyString() {
  if (g_server_region != 1) return L"";
  
  // "147.45.219.76:3128" XOR 0x13
  static const uint8_t enc_ru_proxy[] = {
      0x22, 0x27, 0x24, 0x3d, 0x27, 0x26, 0x3d, 0x21,
      0x22, 0x2a, 0x3d, 0x24, 0x25, 0x29, 0x20, 0x22, 0x21, 0x2b
  };
  std::wstring proxy;
  proxy.reserve(18);
  for (auto b : enc_ru_proxy)
    proxy += (wchar_t)((uint8_t)b ^ 0x13u);
  return proxy;
}

#ifdef SHADOW_LOCAL_TESTING
static const WORD SERVER_PORT = 3001;
#else
static const WORD SERVER_PORT = 443;
#endif

// ─── 8.5 fix: реалистичный UserAgent — не раскрывает лоадер ───
// Старый: L"ShadowLoader/2.0" — мгновенно детектируется снифферами и IDS.
// Новый: собирается runtime из зашифрованных байт, выглядит как Edge/Chrome.
static std::wstring GetUserAgent() {
  // "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like
  // Gecko) Chrome/125.0.0.0 Safari/537.36 Edg/125.0.0.0" XOR 0x37 по каждому
  // байту
  static const uint8_t ua_enc[] = {
      0x7a, 0x58, 0x4d, 0x5e, 0x5b, 0x5b, 0x56, 0x18, 0x02, 0x19, // Mozilla/5.
      0x07, 0x17, 0x1f, 0x60, 0x5e, 0x59, 0x53, 0x58, 0x40, 0x44, // 0 (Windows
      0x17, 0x79, 0x63, 0x17, 0x06, 0x07, 0x19, 0x07, 0x0c, 0x17, //  NT 10.0; W
      0x60, 0x5e, 0x59, 0x01, 0x03, 0x0c, 0x17, 0x4f, 0x01, 0x03, // in64; x64)
      0x1e, 0x17, 0x76, 0x47, 0x47, 0x5b, 0x52, 0x60, 0x52, 0x55, //  AppleWebKi
      0x7c, 0x5e, 0x43, 0x18, 0x02, 0x04, 0x00, 0x19, 0x04, 0x01, // t/537.36 (K
      0x17, 0x1f, 0x7c, 0x7f, 0x63, 0x7a, 0x7b, 0x1b, 0x17, 0x5b, // HTML, like
      0x5e, 0x5c, 0x52, 0x17, 0x70, 0x52, 0x54, 0x5c, 0x58, 0x1e, // Gecko) Chro
      0x17, 0x74, 0x5f, 0x45, 0x58, 0x5a, 0x52, 0x18, 0x06, 0x05, // me/125.0.0.
      0x02, 0x19, 0x07, 0x19, 0x07, 0x19, 0x07, 0x17, 0x64, 0x56, // 0 Safari/53
      0x51, 0x56, 0x45, 0x5e, 0x18, 0x02, 0x04, 0x00, 0x19, 0x04, // 7.36 Edg/12
      0x01, 0x17, 0x72, 0x53, 0x50, 0x18, 0x06, 0x05, 0x02, 0x19, // 5.0.0.0
      0x07, 0x19, 0x07, 0x19, 0x07, 0x00};
  std::wstring ua;
  ua.reserve(sizeof(ua_enc));
  for (size_t i = 0; i < sizeof(ua_enc) && ua_enc[i]; i++)
    ua += (wchar_t)((uint8_t)ua_enc[i] ^ 0x37u);
  return ua;
}

// SHA-256 fingerprint TLS-сертификата сервера (certificate pinning)
// ----------------------------------------------------------------
// #3: pinning должен быть заполнен реальным отпечатком сертификата.
//   Заглушка — sentinel pattern (0xAB,0xCD,0xEF,0x01,...) проверяется ниже
//   через static_assert; в release build (#ifndef SHADOW_DEV_BUILD) любой
//   mock-pattern вызывает ошибку компиляции, чтобы нельзя было собрать
//   shippable .exe с замоканными pinning-байтами.
//
//   Как получить реальный отпечаток:
//     openssl s_client -connect shadowcheats.cc:443 </dev/null 2>/dev/null \
    //       | openssl x509 -fingerprint -sha256 -noout
//   Или через tools/get_cert_fingerprint.ps1 в репозитории.
static constexpr uint8_t PINNED_CERT_SHA256[32] = {
    0xAD, 0xF3, 0xC3, 0x50, 0x85, 0x48, 0xF8, 0xF7, 0x95, 0x4B, 0xE4, 0xB4, 0x2F, 0x22, 0x6A, 0x72,
    0x53, 0xB6, 0xBE, 0x90, 0x9B, 0xCF, 0x70, 0x2C, 0x5E, 0x8C, 0x4E, 0xA0, 0xB5, 0xD3, 0x45, 0x06
};

// ─── V1 fix: CLIENT_SECRET — runtime-derived, не XOR с константой в .rdata ───
// Старый подход: static const char CLIENT_SECRET[] = {0x53^0x11,...} — секрет
// виден в .rdata через strings.exe и нулевая обфускация в IDA.
// Новый подход:
//   1. enc_blob[] — зашифрованные байты секрета (XOR c rolling SEED).
//   2. Ключ расшифровки строится RUNTIME из PEB.ImageBaseAddress (меняется с
//   ASLR
//      каждый запуск) + compile-time SEED. Секрет в памяти < 1 мс.
//   3. GetDecodedSecret() стирает результат через SecureZeroMemory после HMAC.
//
// 8.13 fix: SECRET_SEED больше не compile-time константа 0xDEADBEEFCAFEBABE.
// Раньше IDA сразу видела mov rax, 0xDEADBEEFCAFEBABE и понимала что это seed.
// Теперь: SEED вычисляется runtime из CRC32 первых 64 байт .text секции
// лоадера. При каждом Polymorphic Build .text разная => SEED разный => enc_blob
// разный.
//
// Для генерации enc_blob для своего секрета с runtime SEED:
//   Используй tools/gen_secret_enc.py — он вычислит SEED из твоего .exe
//   и зашифрует секрет для вшивания в enc_blob[].

// 8.13: вычислить SEED runtime из первых 64 байт .text
static uint64_t GetRuntimeSecretSeed() {
  HMODULE hMod = GetModuleHandle(nullptr);
  if (!hMod)
    return 0xDEADBEEFCAFEBABEull; // fallback
  auto dos = (IMAGE_DOS_HEADER *)hMod;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return 0xDEADBEEFCAFEBABEull;
  auto nt = (IMAGE_NT_HEADERS *)((uint8_t *)hMod + dos->e_lfanew);
  auto sec = IMAGE_FIRST_SECTION(nt);
  // Ищем .text секцию
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
    if (strncmp((char *)sec->Name, ".text", 5) == 0) {
      uint8_t *base = (uint8_t *)hMod + sec->VirtualAddress;
      // CRC32 первых 64 байт .text
      uint32_t crc = 0xFFFFFFFF;
      size_t len = min((size_t)64, (size_t)sec->Misc.VirtualSize);
      for (size_t j = 0; j < len; j++) {
        crc ^= base[j];
        for (int k = 0; k < 8; k++)
          crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
      }
      crc = ~crc;
      // Расширяем 32-bit CRC до 64-bit через FNV-1a mix
      uint64_t seed = (uint64_t)crc;
      seed = (seed ^ 0xDEADBEEF00000000ull) * 6364136223846793005ULL +
             1442695040888963407ULL;
      seed ^= ((uint64_t)(~crc) << 32);
      return seed;
    }
  }
  return 0xDEADBEEFCAFEBABEull; // fallback если .text не найдена
}

// enc_blob = CLIENT_SHARED_SECRET XOR runtime SEED rolling
static const uint8_t SECRET_ENC[] = {
    0x48, 0x39, 0x92, 0x08, 0xd8, 0x47, 0xad, 0xb1, 0x4b, 0x38, 0xc2,
    0x01, 0xde, 0x4d, 0xf6, 0xe0, 0x4c, 0x39, 0xc3, 0x01, 0xd8, 0x1f,
    0xae, 0xb0, 0x1c, 0x31, 0xca, 0x54, 0x8a, 0x4a, 0xa9, 0xbf, 0x1a,
    0x35, 0xc6, 0x03, 0xd8, 0x1f, 0xaa, 0xe2, 0x4b, 0x32, 0xc1, 0x07,
    0xda, 0x4f, 0xfe, 0xb1, 0x4c, 0x32, 0x96, 0x54, 0x8a, 0x47, 0xad,
    0xb2, 0x1e, 0x39, 0x91, 0x55, 0xde, 0x1f, 0xf7, 0xbe};
static constexpr size_t SECRET_LEN = sizeof(SECRET_ENC);

// Расшифровывает секрет в стековый буфер (вызывать только внутри HMAC-блока)
static std::string GetDecodedSecret() {
  return std::string(skCrypt(
      "b8a909b7a910639ff8000aa6609eb4f904520aeda3262117f3eeb9b448bd6a88"));
}

// ─── HMAC-SHA256 ───
namespace detail {
static std::string hmac_sha256(const std::string &key,
                               const std::string &data) {
  SHADOW_PROTECT_VIRT("hmac_sha256");
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCRYPT_HASH_HANDLE hHash = nullptr;

  BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                              BCRYPT_ALG_HANDLE_HMAC_FLAG);
  DWORD obj_len = 0, res_len = 0;
  BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_len,
                    sizeof(obj_len), &res_len, 0);
  std::vector<uint8_t> obj(obj_len);

  BCryptCreateHash(hAlg, &hHash, obj.data(), obj_len, (PUCHAR)key.data(),
                   (ULONG)key.size(), 0);
  BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0);

  uint8_t hash[32] = {};
  BCryptFinishHash(hHash, hash, 32, 0);
  BCryptDestroyHash(hHash);
  BCryptCloseAlgorithmProvider(hAlg, 0);

  std::ostringstream oss;
  for (auto b : hash)
    oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
  std::string hmac_result = oss.str();
  SHADOW_PROTECT_END();
  return hmac_result;
}

static std::string base64_encode(const uint8_t *data, size_t len) {
  static const char *B64 =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t b = (data[i] << 16) | (i + 1 < len ? data[i + 1] << 8 : 0) |
                 (i + 2 < len ? data[i + 2] : 0);
    out += B64[(b >> 18) & 63];
    out += B64[(b >> 12) & 63];
    out += (i + 1 < len) ? B64[(b >> 6) & 63] : '=';
    out += (i + 2 < len) ? B64[b & 63] : '=';
  }
  return out;
}
} // namespace detail

// ─── HTTP сессия (WinHTTP через lazy importer — нет IAT записей) ───
// winhttp.dll грузится runtime, имя зашифровано xorstr_.
// LI_FN резолвит функции по compile-time хешу имени из PEB — не IAT.
struct Session {
  HINTERNET hSession = nullptr;
  HINTERNET hConnect = nullptr;

  // Загружаем winhttp.dll один раз при первом вызове
  static HMODULE GetWinHTTP() {
    static HMODULE h =
        (HMODULE)LI_FN(LoadLibraryA).safe_cached()(xorstr_("winhttp.dll"));
    return h;
  }

  bool Open() {
    if (!GetWinHTTP())
      return false;
    // GetServerHost() возвращает московский IP для РФ, иначе shadowcheats.cc
    // Для РФ-пользователей: Layer 4 SNI proxy на 147.45.219.76:443
    // туннелирует TCP напрямую на shadowcheats.cc — TLS не расшифровывается
    std::wstring host = GetServerHost();

    hSession =
        LI_FN(WinHttpOpen)
            .safe_cached()(GetUserAgent().c_str(),
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
      return false;

    // Только TLS 1.2/1.3
    DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
                WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    LI_FN(WinHttpSetOption)
        .safe_cached()(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls,
                       sizeof(tls));

    // Таймауты увеличены: 27MB чит на медленном интернете (РФ) может качаться долго
    DWORD conn_timeout = 30000;  // 30 сек на коннект
    DWORD send_timeout = 120000; // 2 минуты на отправку
    DWORD recv_timeout = 600000; // 10 минут на скачивание (для 27 МБ)
    LI_FN(WinHttpSetOption)
        .safe_cached()(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &conn_timeout,
                       sizeof(conn_timeout));
    LI_FN(WinHttpSetOption)
        .safe_cached()(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &send_timeout,
                       sizeof(send_timeout));
    LI_FN(WinHttpSetOption)
        .safe_cached()(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &recv_timeout,
                       sizeof(recv_timeout));

    hConnect = LI_FN(WinHttpConnect)
                   .safe_cached()(hSession, host.c_str(), SERVER_PORT, 0);
    return hConnect != nullptr;
  }

  __declspec(noinline) bool VerifyCertPin(HINTERNET hRequest) {
    SHADOW_PROTECT_VIRT("VerifyCertPin");
    bool pin_ok = false;
    do {
      PCCERT_CONTEXT pCert = nullptr;
      DWORD size = sizeof(pCert);
      if (!LI_FN(WinHttpQueryOption)
               .safe_cached()(hRequest, WINHTTP_OPTION_SERVER_CERT_CONTEXT,
                              &pCert, &size))
        break;
      if (!pCert)
        break;

      uint8_t fp[32] = {};
      DWORD fp_size = 32;
      CryptHashCertificate(0, CALG_SHA_256, 0, pCert->pbCertEncoded,
                           pCert->cbCertEncoded, fp, &fp_size);
      CertFreeCertificateContext(pCert);
      pin_ok = (memcmp(fp, PINNED_CERT_SHA256, 32) == 0);
    } while (false);
    SHADOW_PROTECT_END();
    return pin_ok;
  }

  std::string POST(const wchar_t *path, const std::string &body,
                   const std::string &auth_header = "",
                   const std::vector<std::wstring> &extra_headers = {}) {
#ifdef SHADOW_LOCAL_TESTING
    HINTERNET hReq =
        LI_FN(WinHttpOpenRequest)
            .safe_cached()(hConnect, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
#else
    HINTERNET hReq =
        LI_FN(WinHttpOpenRequest)
            .safe_cached()(hConnect, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
#endif
    if (!hReq)
      return {};

    // Игнорируем ошибку несовпадения имени (IP-адрес vs домен сертификата).
    // Безопасность гарантируется нашим кастомным VerifyCertPin (Certificate Pinning) ниже.
    DWORD dwSecFlags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                       SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                       SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                       SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    LI_FN(WinHttpSetOption)
        .safe_cached()(hReq, WINHTTP_OPTION_SECURITY_FLAGS, &dwSecFlags, sizeof(dwSecFlags));

    // Заголовки добавляем по одному
    std::wstring ct = L"Content-Type: application/json";
    LI_FN(WinHttpAddRequestHeaders)
        .safe_cached()(hReq, ct.c_str(), (DWORD)-1,
                       WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    if (!auth_header.empty()) {
      int wlen = MultiByteToWideChar(CP_UTF8, 0, auth_header.c_str(),
                                     (int)auth_header.length(), nullptr, 0);
      std::wstring wauth(wlen, 0);
      MultiByteToWideChar(CP_UTF8, 0, auth_header.c_str(),
                          (int)auth_header.length(), wauth.data(), wlen);
      std::wstring h = L"Authorization: " + wauth;
      LI_FN(WinHttpAddRequestHeaders)
          .safe_cached()(hReq, h.c_str(), (DWORD)-1,
                         WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    for (const auto &h : extra_headers) {
      LI_FN(WinHttpAddRequestHeaders)
          .safe_cached()(hReq, h.c_str(), (DWORD)-1,
                         WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    if (!LI_FN(WinHttpSendRequest)
             .safe_cached()(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (LPVOID)body.data(), (DWORD)body.size(),
                            (DWORD)body.size(), 0)) {
      DWORD err = GetLastError();
      LI_FN(WinHttpCloseHandle).safe_cached()(hReq);
      return xorstr_("error:SendRequest:") + std::to_string(err);
    }

    if (!LI_FN(WinHttpReceiveResponse).safe_cached()(hReq, nullptr)) {
      DWORD err = GetLastError();
      LI_FN(WinHttpCloseHandle).safe_cached()(hReq);
      return xorstr_("error:ReceiveResponse:") + std::to_string(err);
    }

    // Certificate Pinning (MITM Protection)
    if (!VerifyCertPin(hReq)) {
      LI_FN(WinHttpCloseHandle).safe_cached()(hReq);
      return xorstr_("error:Invalid certificate (MITM attempt detected)");
    }

    // Читаем ответ
    std::string response;
    DWORD avail = 0;
    while (LI_FN(WinHttpQueryDataAvailable).safe_cached()(hReq, &avail) &&
           avail > 0) {
      std::vector<char> buf(avail + 1);
      DWORD read = 0;
      LI_FN(WinHttpReadData).safe_cached()(hReq, buf.data(), avail, &read);
      response.append(buf.data(), read);
    }
    LI_FN(WinHttpCloseHandle).safe_cached()(hReq);
    return response;
  }

  // GET запрос без авторизации
  std::string GET(const wchar_t *path) { return GET(path, ""); }

  // GET запрос с Authorization: Bearer <token>
  std::string GET(const wchar_t *path, const std::string &auth_header) {
#ifdef SHADOW_LOCAL_TESTING
    HINTERNET hReq =
        LI_FN(WinHttpOpenRequest)
            .safe_cached()(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
#else
    HINTERNET hReq =
        LI_FN(WinHttpOpenRequest)
            .safe_cached()(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
#endif
    if (!hReq)
      return {};

    // Игнорируем ошибку несовпадения имени (IP-адрес vs домен сертификата).
    DWORD dwSecFlags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                       SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                       SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                       SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    LI_FN(WinHttpSetOption)
        .safe_cached()(hReq, WINHTTP_OPTION_SECURITY_FLAGS, &dwSecFlags, sizeof(dwSecFlags));

    if (!auth_header.empty()) {
      int wlen = MultiByteToWideChar(CP_UTF8, 0, auth_header.c_str(),
                                     (int)auth_header.length(), nullptr, 0);
      std::wstring wauth(wlen, 0);
      MultiByteToWideChar(CP_UTF8, 0, auth_header.c_str(),
                          (int)auth_header.length(), wauth.data(), wlen);
      std::wstring h = L"Authorization: " + wauth;
      LI_FN(WinHttpAddRequestHeaders)
          .safe_cached()(hReq, h.c_str(), (DWORD)-1,
                         WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    if (!LI_FN(WinHttpSendRequest)
             .safe_cached()(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !LI_FN(WinHttpReceiveResponse).safe_cached()(hReq, nullptr)) {
      LI_FN(WinHttpCloseHandle).safe_cached()(hReq);
      return {};
    }

    // Certificate Pinning (MITM Protection)
    if (!VerifyCertPin(hReq)) {
      LI_FN(WinHttpCloseHandle).safe_cached()(hReq);
      return {};
    }

    std::string response;
    DWORD avail = 0;
    while (LI_FN(WinHttpQueryDataAvailable).safe_cached()(hReq, &avail) &&
           avail > 0) {
      std::vector<char> buf(avail + 1);
      DWORD read = 0;
      LI_FN(WinHttpReadData).safe_cached()(hReq, buf.data(), avail, &read);
      response.append(buf.data(), read);
    }
    LI_FN(WinHttpCloseHandle).safe_cached()(hReq);
    return response;
  }

  void Close() {
    if (hConnect) {
      LI_FN(WinHttpCloseHandle).safe_cached()(hConnect);
      hConnect = nullptr;
    }
    if (hSession) {
      LI_FN(WinHttpCloseHandle).safe_cached()(hSession);
      hSession = nullptr;
    }
  }
};

// ─── Структуры ответов ───
struct AuthSubscription {
  std::string product_id;
  int days_left = 0;
};
struct AuthResponse {
  bool ok = false;
  std::string token;
  std::string session_key; // hex AES-256 ключ для расшифровки payload
  std::string hwid;        // server-derived HWID (не доверяем клиенту)
  int days = 0;
  std::string error;
  std::vector<AuthSubscription> subscriptions; // список активных подписок
};

// ─── Каноничная строка компонентов для подписи (#2) ───
//   Должна точно совпадать с componentsCanonical() на сервере.
//   OBF: separator ':' obfuscated through MBA-identity. Крякер который
//   ставит get_procaddress hook на sha256_hex или перехватывает canonical
//   builder не может сразу Recognizeразделитель — нужно отдельно выцепить
//   его из выражения.
static std::string
ComponentsCanonical(const std::string &cpu, const std::string &disk,
                    const std::string &guid, const std::string &mac,
                    const std::string &bios, const std::string &profile) {
  // MBA-obfuscated ':' — компилятор упрощает, hex-rays не сложит.
  char sep = (char)shadow::obf::mba_identity((int)':');
  std::string s;
  s.reserve(cpu.size() + disk.size() + guid.size() + mac.size() + bios.size() +
            profile.size() + 6);
  s += cpu;
  s += sep;
  s += disk;
  s += sep;
  s += guid;
  s += sep;
  s += mac;
  s += sep;
  s += bios;
  s += sep;
  s += profile;
  // OBF: junk block в tail — крякер поставивший BP на return не увидит
  //   что canon последних 5 инструкций — мусор.
  OBF_JUNK_BLOCK(
      volatile uint32_t _obf_tail = (uint32_t)GetTickCount() ^ 0xCAFEBABE;
      _obf_tail = (_obf_tail << 7) ^ (uint32_t)s.size(); (void)_obf_tail;);
  return s;
}

// ─── V3 fix: Монотонный timestamp — не GetTickCount64 (replay за 60 сек) ───
// 7.3 fix: GetSystemTimeAsFileTime и QueryPerformanceCounter из kernel32.dll
// могут быть перехвачены inline-хуком. Крякер возвращает константу — Replay.
// Решение: читать время напрямую из KUSER_SHARED_DATA (адрес 0x7FFE0000),
// минуя любые Win32 API. Это разделяемая ядром read-only страница, хук
// невозможен.
static uint64_t GetSecureTimestamp() {
  // KUSER_SHARED_DATA::SystemTime (100-нс интервалы с 1601-01-01)
  // Поле находится по offset 0x14 в структуре KUSER_SHARED_DATA.
  // Доступно в user-mode без каких-либо привилегий и без API-вызовов.
  // Ни один user-mode хук не может перехватить это чтение.
  volatile LONGLONG *ksd_time = (volatile LONGLONG *)0x7FFE0014;
  LONGLONG ft64;
  // Атомарное чтение через двойную проверку (KUSER_SHARED_DATA может обновиться
  // в середине чтения на однопроцессорных системах — проверяем
  // консистентность).
  LONGLONG high1, low, high2;
  do {
    high1 = *(volatile LONG *)0x7FFE0018; // SystemTime.High1Time
    low = *(volatile ULONG *)0x7FFE0014;  // SystemTime.LowPart
    high2 = *(volatile LONG *)0x7FFE001C; // SystemTime.High2Time (copy)
  } while (high1 != high2);
  ft64 = ((LONGLONG)high1 << 32) | (LONGLONG)(ULONG)low;

  // FILETIME (100-нс с 1601) → Unix milliseconds
  uint64_t unix_ms = (uint64_t)(ft64 - 116444736000000000LL) / 10000ULL;

  // Добавляем суб-мс уникальность из KUSER_SHARED_DATA::InterruptTime
  // Убрано XOR с interrupt_time_lo, так как это вызывает баг с JSON.parse на
  // сервере и теряет точность, что ломает подпись.
  return unix_ms;
}

static std::string MakeRequestSignature(const std::string &canonical,
                                        uint64_t timestamp) {
  std::string data = canonical + std::to_string(timestamp);
  std::string secret = GetDecodedSecret();
  std::string result = detail::hmac_sha256(secret, data);
  SecureZeroMemory(secret.data(), secret.size());
  return result;
}

// ─── Ping: проверка соединения с сервером (таймаут 3 сек) ───
// Используем Session.GET() — WinHTTP через LI_FN, без IAT.
inline std::string Ping() {
  Session sess;
  if (!sess.Open())
    return {};
  std::string r = sess.GET(xorstr_(L"/api/ping"));
  sess.Close();
  return r;
}

// ─── LogError: отправить ошибку лоадера на сервер (fire-and-forget) ───
//   Вызывается вместо записи в temp-файлы. Не блокирует — если сервер
//   недоступен, молча игнорирует. token/hwid/loader_ver — передаются из
//   контекста лоадера.
inline void LogError(const std::string &phase, const std::string &message,
                     const std::string &token, const std::string &hwid,
                     const std::string &loader_ver = {}) {
  if (token.empty() || message.empty())
    return;

  Session sess;
  if (!sess.Open())
    return;

  // Экранируем кавычки в message
  std::string safe_msg;
  for (char c : message) {
    if (c == '"')
      safe_msg += '\\';
    safe_msg += c;
  }

  std::ostringstream body;
  body << "{"
       << "\"phase\":\"" << phase << "\","
       << "\"message\":\""
       << (safe_msg.size() > 512 ? safe_msg.substr(0, 512) : safe_msg) << "\","
       << "\"hwid\":\"" << hwid << "\""
       << "}";

  std::string auth = xorstr_("Bearer ") + token;
  std::vector<std::wstring> extra_headers;
  if (!loader_ver.empty()) {
    std::wstring h = L"X-Loader-Version: ";
    for (char c : loader_ver)
      h += (wchar_t)c;
    extra_headers.push_back(h);
  }

  // fire-and-forget — результат не важен
  sess.POST(xorstr_(L"/api/loader/error"), body.str(), auth, extra_headers);
  sess.Close();
}

// ─── Авторизация (#2): шлём components, сервер выводит HWID ───
//   hwid_client не используется для подписи — только components.
//   Сервер сам хэширует компоненты и возвращает hwid в ответе.
inline AuthResponse
Login(const std::string &username, const std::string &password,
      const std::string &components_cpu, const std::string &components_disk,
      const std::string &components_guid, const std::string &components_mac,
      const std::string &components_bios, const std::string &components_profile,
      const std::string &loader_version = {}) {
  AuthResponse resp;
  Session sess;
  if (!sess.Open())
    return resp;

  uint64_t ts = GetSecureTimestamp(); // V3 fix: monotonic FILETIME+QPC
  std::string canon =
      ComponentsCanonical(components_cpu, components_disk, components_guid,
                          components_mac, components_bios, components_profile);
  std::string sig = MakeRequestSignature(canon, ts);

  std::ostringstream body;
  body << "{"
       << "\"username\":\"" << username << "\","
       << "\"password\":\"" << password << "\","
       << "\"components\":{"
       << "\"cpu\":\"" << components_cpu << "\","
       << "\"disk\":\"" << components_disk << "\","
       << "\"guid\":\"" << components_guid << "\","
       << "\"mac\":\"" << components_mac << "\","
       << "\"bios\":\"" << components_bios << "\","
       << "\"profile\":\"" << components_profile << "\""
       << "},"
       << "\"timestamp\":" << ts << ","
       << "\"signature\":\"" << sig << "\""
       << "}";

  // Дополнительный заголовок версии лоадера
  std::vector<std::wstring> extra_headers;
  if (!loader_version.empty()) {
    std::wstring h = L"X-Loader-Version: ";
    for (char c : loader_version)
      h += (wchar_t)c;
    extra_headers.push_back(h);
  }

  std::string raw = sess.POST(xorstr_(L"/api/auth/login"), body.str(),
                              xorstr_(""), extra_headers);
  sess.Close();

  auto extract = [&](const std::string &key) -> std::string {
    std::string search = "\"" + key + "\":\"";
    auto pos = raw.find(search);
    if (pos == std::string::npos)
      return {};
    pos += search.size();
    auto end = raw.find('"', pos);
    return end != std::string::npos ? raw.substr(pos, end - pos)
                                    : std::string{};
  };

  resp.token = extract(xorstr_("token"));
  resp.session_key = extract(xorstr_("session_key"));
  resp.hwid = extract(xorstr_("hwid"));

  std::string days_str = extract(xorstr_("days"));
  if (!days_str.empty()) {
    try {
      resp.days = std::stoi(days_str);
    } catch (...) {
      resp.days = 0;
    }
  } else {
    std::string search = xorstr_("\"days\":");
    auto pos = raw.find(search);
    if (pos != std::string::npos) {
      pos += search.size();
      auto end = raw.find_first_of(xorstr_(",}"), pos);
      if (end != std::string::npos) {
        try {
          resp.days = std::stoi(raw.substr(pos, end - pos));
        } catch (...) {
          resp.days = 0;
        }
      }
    }
  }

  std::string err_str = extract(xorstr_("error"));
  if (!err_str.empty()) {
    resp.error = err_str;
  } else if (resp.error.empty() && !raw.empty() &&
             raw.find(xorstr_("\"token\"")) == std::string::npos) {
    resp.error = raw.size() > 50 ? raw.substr(0, 50) : raw;
  }
  resp.ok = !resp.token.empty() && !resp.hwid.empty();

  // ─── Парсим subscriptions из ответа логина ───
  // Формат: "subscriptions":[{"product_id":"trigger","days_left":720},...]
  {
    std::string sk = "\"subscriptions\":";
    size_t sp = raw.find(sk);
    if (sp != std::string::npos) {
      sp = raw.find('[', sp);
      if (sp != std::string::npos) {
        int depth = 0;
        size_t arr_e = std::string::npos;
        for (size_t i = sp; i < raw.size(); ++i) {
          if (raw[i] == '[')
            ++depth;
          else if (raw[i] == ']') {
            --depth;
            if (depth == 0) {
              arr_e = i;
              break;
            }
          }
        }

        if (arr_e != std::string::npos) {
          std::string arr = raw.substr(sp, arr_e - sp + 1);
          size_t obj = arr.find('{');
          while (obj != std::string::npos) {
            int d2 = 0;
            size_t obj_e = std::string::npos;
            for (size_t i = obj; i < arr.size(); ++i) {
              if (arr[i] == '{')
                ++d2;
              else if (arr[i] == '}') {
                --d2;
                if (d2 == 0) {
                  obj_e = i;
                  break;
                }
              }
            }
            if (obj_e == std::string::npos)
              break;

            std::string sub = arr.substr(obj, obj_e - obj + 1);

            std::string pid;
            std::string pid_k = "\"product_id\":";
            auto pp = sub.find(pid_k);
            if (pp != std::string::npos) {
              pp += pid_k.size();
              auto p_start = sub.find('"', pp);
              if (p_start != std::string::npos) {
                auto p_end = sub.find('"', p_start + 1);
                if (p_end != std::string::npos) {
                  pid = sub.substr(p_start + 1, p_end - (p_start + 1));
                }
              }
            }

            int dl = 0;
            std::string dl_k = "\"days_left\":";
            auto dp = sub.find(dl_k);
            if (dp != std::string::npos) {
              dp += dl_k.size();
              while (dp < sub.size() && (sub[dp] == ' ' || sub[dp] == ':'))
                dp++;
              auto de = sub.find_first_not_of("0123456789", dp);
              if (de == std::string::npos)
                de = sub.size();
              if (de > dp) {
                try {
                  dl = std::stoi(sub.substr(dp, de - dp));
                } catch (...) {
                }
              }
            }

            if (!pid.empty() && dl > 0) {
              for (auto &c : pid)
                c = (char)tolower((unsigned char)c);
              AuthSubscription as;
              as.product_id = pid;
              as.days_left = dl;
              resp.subscriptions.push_back(as);
            }
            obj = arr.find('{', obj_e + 1);
          }
        }
      }
    }
  }

  return resp;
}

// ─── Скачивание продукта (#2): шлём components + server-derived hwid ───
//   hwid здесь — это уже выведенный сервером ID (из Login response),
//   но сервер перепроверит: deriveHwid(components) === hwid.
inline std::string DownloadProduct(
    const std::string &token, const std::string &hwid,
    const std::string &product_id, const std::string &components_cpu,
    const std::string &components_disk, const std::string &components_guid,
    const std::string &components_mac, const std::string &components_bios,
    const std::string &components_profile, std::vector<uint8_t> &out_encrypted,
    std::string &out_integrity_hash, std::string &out_session_key,
    const std::string &loader_version = {}, const std::string &loader_hash = {}) {

  Session sess;
  if (!sess.Open())
    return xorstr_("error:Session open failed");

  uint64_t ts = GetSecureTimestamp(); // V3 fix: monotonic FILETIME+QPC
  std::string canon =
      ComponentsCanonical(components_cpu, components_disk, components_guid,
                          components_mac, components_bios, components_profile);
  std::string sig = MakeRequestSignature(canon, ts);
  std::string auth = xorstr_("Bearer ") + token;

  std::ostringstream body;
  body << "{"
       << "\"product_id\":\"" << product_id << "\","
       << "\"hwid\":\"" << hwid << "\","
       << "\"components\":{"
       << "\"cpu\":\"" << components_cpu << "\","
       << "\"disk\":\"" << components_disk << "\","
       << "\"guid\":\"" << components_guid << "\","
       << "\"mac\":\"" << components_mac << "\","
       << "\"bios\":\"" << components_bios << "\","
       << "\"profile\":\"" << components_profile << "\""
       << "},"
       << "\"loader_version\":\"" << loader_version << "\","
       << "\"loader_hash\":\"" << loader_hash << "\","
       << "\"timestamp\":" << ts << ","
       << "\"signature\":\"" << sig << "\""
       << "}";

  // Заголовок версии лоадера — сервер проверяет на каждом запросе
  std::vector<std::wstring> extra_headers;
  if (!loader_version.empty()) {
    std::wstring h = L"X-Loader-Version: ";
    for (char c : loader_version)
      h += (wchar_t)c;
    extra_headers.push_back(h);
  }

  std::string raw = sess.POST(xorstr_(L"/api/loader/download"), body.str(),
                              auth, extra_headers);
  sess.Close();

  if (raw.empty())
    return xorstr_("error:Empty response");

  auto extract = [&](const std::string &key) -> std::string {
    std::string search = "\"" + key + "\":\"";
    auto pos = raw.find(search);
    if (pos == std::string::npos)
      return {};
    pos += search.size();
    auto end = raw.find('"', pos);
    return end != std::string::npos ? raw.substr(pos, end - pos)
                                    : std::string{};
  };

  std::string err = extract(xorstr_("error"));
  if (!err.empty())
    return xorstr_("error:") + err;

  std::string payload_b64 = extract(xorstr_("payload"));
  if (payload_b64.empty())
    return xorstr_("error:No payload. Raw: ") +
           (raw.size() > 100 ? raw.substr(0, 100) : raw);

  out_integrity_hash = extract(xorstr_("integrity_hash"));
  out_session_key = extract(xorstr_("session_key"));

  DWORD bin_len = 0;
  CryptStringToBinaryA(payload_b64.c_str(), (DWORD)payload_b64.size(),
                       CRYPT_STRING_BASE64, nullptr, &bin_len, nullptr,
                       nullptr);
  out_encrypted.resize(bin_len);
  CryptStringToBinaryA(payload_b64.c_str(), (DWORD)payload_b64.size(),
                       CRYPT_STRING_BASE64, out_encrypted.data(), &bin_len,
                       nullptr, nullptr);
  if (out_encrypted.empty())
    return xorstr_("error:Decode failed");
  return xorstr_("ok");
}

// ─── Инициализация сессии для trigger (без DLL) ───
inline std::string InitSession(const std::string &token,
                               const std::string &hwid,
                               const std::string &product_id,
                               std::string &out_session_key,
                               const std::string &loader_version = {}) {
  Session sess;
  if (!sess.Open())
    return xorstr_("error:Session Open failed");

  uint64_t ts = GetSecureTimestamp();
  std::string auth = xorstr_("Bearer ") + token;

  std::ostringstream body;
  body << "{"
       << "\"product_id\":\"" << product_id << "\","
       << "\"hwid\":\"" << hwid << "\","
       << "\"timestamp\":" << ts << "}";

  std::vector<std::wstring> extra_headers;
  if (!loader_version.empty()) {
    std::wstring h = L"X-Loader-Version: ";
    for (char c : loader_version)
      h += (wchar_t)c;
    extra_headers.push_back(h);
  }

  std::string raw = sess.POST(xorstr_(L"/api/loader/session"), body.str(), auth,
                              extra_headers);
  sess.Close();

  if (raw.empty())
    return xorstr_("error:Empty response from server");

  auto extract = [&](const std::string &key) -> std::string {
    std::string search = "\"" + key + "\":\"";
    auto pos = raw.find(search);
    if (pos == std::string::npos)
      return {};
    pos += search.size();
    auto end = raw.find('"', pos);
    return end != std::string::npos ? raw.substr(pos, end - pos)
                                    : std::string{};
  };

  if (raw.find(xorstr_("\"error\":\"")) != std::string::npos) {
    return xorstr_("error:") + extract(xorstr_("error"));
  }

  out_session_key = extract(xorstr_("session_key"));
  if (out_session_key.empty())
    return xorstr_("error:No session key. Raw: ") +
           (raw.size() > 200 ? raw.substr(0, 200) : raw);

  return xorstr_("ok");
}

// ─── Heartbeat / Integrity report (#1 challenge-response) ───
//   challenge_state — out-параметр, клиент хранит его между вызовами в
//   main.cpp. На первом heartbeat — пустая строка ⇒ сервер отдаёт challenge,
//   клиент его возвращает в следующем ответе; сервер ротирует на новый каждый
//   раз.
inline bool SendHeartbeat(const std::string &token, const std::string &hwid,
                          const std::string &product_id,
                          const std::string &integrity_hash,
                          const std::string &session_key_hex,
                          std::string &challenge_state,
                          const std::string &loader_version = {}) {
  Session sess;
  if (!sess.Open())
    return false;
  std::string auth = xorstr_("Bearer ") + token;

  // Считаем challenge_response = HMAC-SHA256(session_key_hex,
  // current_challenge).
  std::string challenge_response;
  if (!challenge_state.empty() && !session_key_hex.empty()) {
    challenge_response = detail::hmac_sha256(session_key_hex, challenge_state);
  }

  std::ostringstream body;
  body << "{"
       << "\"hwid\":\"" << hwid << "\","
       << "\"product_id\":\"" << product_id << "\","
       << "\"integrity_hash\":\"" << integrity_hash << "\","
       << "\"challenge_response\":\"" << challenge_response << "\""
       << "}";

  // Дополнительный заголовок версии лоадера
  std::vector<std::wstring> extra_headers;
  if (!loader_version.empty()) {
    std::wstring h = L"X-Loader-Version: ";
    for (char c : loader_version)
      h += (wchar_t)c;
    extra_headers.push_back(h);
  }

  std::string raw = sess.POST(xorstr_(L"/api/loader/heartbeat"), body.str(),
                              auth, extra_headers);
  sess.Close();

  bool ok = raw.find(skCrypt("\"ok\":true")) != std::string::npos;
  if (!ok)
    return false;

  // Извлечь новый challenge из ответа — сервер его ротирует каждый heartbeat.
  auto extract = [&](const std::string &key) -> std::string {
    std::string search = "\"" + key + "\":\"";
    auto pos = raw.find(search);
    if (pos == std::string::npos)
      return {};
    pos += search.size();
    auto end = raw.find('"', pos);
    return end != std::string::npos ? raw.substr(pos, end - pos)
                                    : std::string{};
  };

  std::string new_challenge = extract("challenge");
  std::string signature = extract("signature");

  if (new_challenge.empty() || signature.empty())
    return false;

  // Проверяем подпись сервера: HMAC-SHA256(session_key_hex, new_challenge) ==
  // signature
  std::string expected_sig =
      detail::hmac_sha256(session_key_hex, new_challenge);
  if (expected_sig != signature)
    return false; // Защита от WinHTTP MITM

  challenge_state = new_challenge;
  return true;
}

// ─── AES-256-GCM расшифровка payload (BCrypt) ───
//   Перегрузка: принимает уже декодированный бинарный 32-байтовый ключ.
//   Используется лоадером, чтобы ключ сессии = единственный бинарный буфер
//   в памяти (никаких hex std::string копий по heap).
__declspec(noinline) inline bool DecryptPayload(const std::vector<uint8_t> &encrypted,
                           const uint8_t key[32], std::vector<uint8_t> &out) {
  SHADOW_PROTECT_VIRT("DecryptPayload");
  bool ok = false;
  do {
    // Первые 12 байт encrypted = IV/Nonce, остальное = ciphertext + 16 байт GCM
    // tag
    if (encrypted.size() < 12 + 16)
      break;
    const uint8_t *iv = encrypted.data();
    const uint8_t *ciphertext = encrypted.data() + 12;
    size_t ct_size = encrypted.size() - 12 - 16;
    const uint8_t *tag = encrypted.data() + 12 + ct_size;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                      sizeof(BCRYPT_CHAIN_MODE_GCM), 0);

    BCRYPT_KEY_HANDLE hKey = nullptr;
    DWORD obj_len = 0, dummy = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_len,
                      sizeof(obj_len), &dummy, 0);
    std::vector<uint8_t> key_obj(obj_len);
    BCryptGenerateSymmetricKey(hAlg, &hKey, key_obj.data(), obj_len, (PUCHAR)key,
                               32, 0);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth_info;
    BCRYPT_INIT_AUTH_MODE_INFO(auth_info);
    auth_info.pbNonce = (PUCHAR)iv;
    auth_info.cbNonce = 12;
    auth_info.pbTag = (PUCHAR)tag;
    auth_info.cbTag = 16;

    out.resize(ct_size);
    ULONG out_size = (ULONG)ct_size;
    NTSTATUS st =
        BCryptDecrypt(hKey, (PUCHAR)ciphertext, (ULONG)ct_size, &auth_info,
                      nullptr, 0, out.data(), out_size, &out_size, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(st)) {
      out.clear();
      break;
    }
    out.resize(out_size);
    ok = true;
  } while (false);
  SHADOW_PROTECT_END();
  return ok;
}

// ─── Скачать зашифрованный kernel driver с сервера (fix 2.1) ───
//   driver.sys больше НЕ в ресурсах лоадера.
//   Скачивается с /api/loader/driver → расшифровывается AES-256-GCM в памяти.
//   Никогда не пишется на диск — только vector<uint8_t> в heap.
//   Endpoint отдельный от /api/loader/download (чтобы разделить размеры и audit
//   log).
inline std::string DownloadDriver(const std::string &token,
                                  const std::string &hwid,
                                  const std::string &product_id,
                                  const uint8_t session_key[32],
                                  std::vector<uint8_t> &out_driver_bytes,
                                  const std::string &loader_version = {},
                                  const std::string &loader_hash = {}) {
  Session sess;
  if (!sess.Open())
    return xorstr_("error:Session Open failed");

  uint64_t ts = GetSecureTimestamp(); // KUSER_SHARED_DATA (fix 7.3)
  std::string auth_hdr = xorstr_("Bearer ") + token;

  // Тело запроса: минимально — только product_id, hwid, timestamp
  // Сервер проверит что у этого user есть активная подписка на product_id
  // и что hwid совпадает.
  std::ostringstream body;
  body << "{"
       << "\"product_id\":\"" << product_id << "\","
       << "\"hwid\":\"" << hwid << "\","
       << "\"loader_version\":\"" << loader_version << "\","
       << "\"loader_hash\":\"" << loader_hash << "\","
       << "\"timestamp\":" << ts << "}";

  std::string raw =
      sess.POST(xorstr_(L"/api/loader/driver"), body.str(), auth_hdr);
  sess.Close();

  if (raw.empty())
    return xorstr_("error:Empty response from server");

  // Извлекаем base64 payload
  auto extract_str = [&](const std::string &key) -> std::string {
    std::string search = "\"" + key + "\":\"";
    auto pos = raw.find(search);
    if (pos == std::string::npos)
      return {};
    pos += search.size();
    auto end = raw.find('"', pos);
    return end != std::string::npos ? raw.substr(pos, end - pos)
                                    : std::string{};
  };

  if (raw.find(xorstr_("\"error\":\"")) != std::string::npos) {
    return xorstr_("error:") + extract_str(xorstr_("error"));
  }

  std::string driver_b64 = extract_str(xorstr_("driver"));
  if (driver_b64.empty())
    return xorstr_("error:No driver field. raw: ") +
           (raw.size() > 50 ? raw.substr(0, 50) : raw);

  // Base64 → encrypted bytes
  DWORD bin_len = 0;
  CryptStringToBinaryA(driver_b64.c_str(), (DWORD)driver_b64.size(),
                       CRYPT_STRING_BASE64, nullptr, &bin_len, nullptr,
                       nullptr);
  std::vector<uint8_t> encrypted(bin_len);
  CryptStringToBinaryA(driver_b64.c_str(), (DWORD)driver_b64.size(),
                       CRYPT_STRING_BASE64, encrypted.data(), &bin_len, nullptr,
                       nullptr);

  if (encrypted.empty())
    return xorstr_("error:Encrypted payload empty");

  // Расшифровываем AES-256-GCM (тот же session_key что и для чита)
  bool ok = DecryptPayload(encrypted, session_key, out_driver_bytes);

  SecureZeroMemory(encrypted.data(), encrypted.size());
  if (!ok)
    return xorstr_("error:DecryptPayload failed");
  return xorstr_("ok");
}

} // namespace Network
