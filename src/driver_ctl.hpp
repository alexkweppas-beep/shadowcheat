#pragma once
#include "xorstr.hpp"

/* IOCTL-связь лаунчера с драйвером: подключение, handshake, PID цели и heartbeat.
 * После handshake heartbeat нужен регулярно, иначе watchdog завершит лаунчер. */

#include <windows.h>
#include <winioctl.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

// IOCTL-контракт с драйвером — локальная копия (launcher самодостаточен).
#include "driver_ioctl.hpp"

namespace shadow_driver_ctl {

    static HANDLE g_driver_handle = nullptr;
    static uint64_t g_driver_session_key = 0;   // 64-bit session key для heartbeat
    static uint32_t g_driver_request_id  = 0;

    static inline uint32_t next_request_id() {
        return ++g_driver_request_id;
    }

    static inline uint32_t crc32_calc(const uint8_t* data, size_t len) {
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                uint32_t mask = -(crc & 1u);
                crc = (crc >> 1) ^ (0xEDB88320u & mask);
            }
        }
        return ~crc;
    }

    // ─── Открытие подключения к драйверу
    static bool OpenDriver() {
        if (g_driver_handle && g_driver_handle != INVALID_HANDLE_VALUE) return true;
        char dev_path[32] = {};
        shadow_format_device_path(dev_path, sizeof(dev_path));
        g_driver_handle = CreateFileA(
            dev_path,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        
        if (!g_driver_handle || g_driver_handle == INVALID_HANDLE_VALUE) {
            g_driver_handle = nullptr;
            return false;
        }
        return true;
    }

    // ─── Проверка: загружен ли драйвер и доступен ли по IOCTL
    static bool IsDriverLoaded() {
        return OpenDriver();
    }

    static void CloseDriver() {
        if (g_driver_handle && g_driver_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(g_driver_handle);
        }
        g_driver_handle = nullptr;
        g_driver_session_key = 0;
    }

    // ─── Первичный handshake
    // Передаём session_key (64-bit nonce) + loader_pid.
    // Драйвер установит session_key, зарегистрирует ObCallbacks, watchdog и т.д.
    static bool DriverHandshake(uint64_t session_key, uint32_t loader_pid) {
        if (!OpenDriver()) return false;
        driver::HandshakeRequest req{};
        req.session_key = session_key;
        req.loader_pid  = loader_pid;
        req.build_seed  = 0;
        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(g_driver_handle, SHADOW_CODE_HANDSHAKE,
                                   &req, sizeof(req), nullptr, 0, &bytes, nullptr);
        if (!ok) return false;
        g_driver_session_key = session_key;
        g_driver_request_id  = 0;
        return true;
    }

    // ─── Сообщить драйверу PID целевого процесса (игры)
    static bool DriverSetTarget(uint32_t target_pid) {
        if (!g_driver_handle || !g_driver_session_key) return false;
        driver::SetTargetRequest req{};
        req.session_key = g_driver_session_key;
        req.request_id  = next_request_id();
        req.target_pid  = target_pid;
        DWORD bytes = 0;
        return !!DeviceIoControl(g_driver_handle, SHADOW_CODE_SET_TARGET,
                                   &req, sizeof(req), nullptr, 0, &bytes, nullptr);
    }

    // ─── Watchdog ping (heartbeat)
    static bool DriverHeartbeat() {
        if (!g_driver_handle || !g_driver_session_key) return false;
        driver::HeartbeatRequest req{};
        req.session_key = g_driver_session_key;
        req.request_id  = next_request_id();
        DWORD bytes = 0;
        return !!DeviceIoControl(g_driver_handle, SHADOW_CODE_HEARTBEAT,
                                   &req, sizeof(req), nullptr, 0, &bytes, nullptr);
    }

    // ─── Защита процесса через ObCallback ядра
    static bool DriverProtectProcess(uint32_t pid, uint32_t flags = 0x3) {
        if (!g_driver_handle || !g_driver_session_key) return false;
        driver::ProtectProcessRequest req{};
        req.session_key   = g_driver_session_key;
        req.request_id    = next_request_id();
        req.target_pid    = pid;
        req.protect_flags = flags;
        DWORD bytes = 0;
        return !!DeviceIoControl(g_driver_handle, SHADOW_CODE_PROTECT_PROCESS,
                                   &req, sizeof(req), nullptr, 0, &bytes, nullptr);
    }

    // ─── Очистка Working Set процесса (скрытие из диспетчера задач)
    static bool DriverTrimWorkingSet(uint32_t target_pid = 0) {
        if (!g_driver_handle || !g_driver_session_key) return false;
        driver::TrimWsRequest req{};
        req.session_key = g_driver_session_key;
        req.request_id  = next_request_id();
        req.target_pid  = target_pid;
        DWORD bytes = 0;
        return !!DeviceIoControl(g_driver_handle, SHADOW_CODE_TRIM_WS,
                                   &req, sizeof(req), nullptr, 0, &bytes, nullptr);
    }

    // ─── Затирание PE-заголовка
    static bool DriverWipePeHeader(uint32_t target_pid, uint64_t base_address, uint32_t header_size) {
        if (!g_driver_handle || !g_driver_session_key) return false;
        driver::WipeHeaderRequest req{};
        req.session_key  = g_driver_session_key;
        req.request_id   = next_request_id();
        req.target_pid   = target_pid;
        req.base_address = base_address;
        req.header_size  = header_size;
        DWORD bytes = 0;
        return !!DeviceIoControl(g_driver_handle, SHADOW_CODE_WIPE_HEADER,
                                   &req, sizeof(req), nullptr, 0, &bytes, nullptr);
    }

    // ─── Получение ImageBase через драйвер
    static uint64_t DriverGetImageBase(uint32_t pid) {
        if (!g_driver_handle || g_driver_handle == INVALID_HANDLE_VALUE) return 0;
        uint64_t image_address = 0;
        driver::ImageRequest req{};
        req.security_code = SHADOW_SECURITY_CODE;
        req.process_id    = pid;
        req.address       = &image_address;
        DWORD bytes = 0;
        DeviceIoControl(g_driver_handle, SHADOW_CODE_IMAGE,
                        &req, sizeof(req), nullptr, 0, &bytes, nullptr);
        return image_address;
    }


    // ─── Inline-патч cheat binary: впрыснуть session_key в .data placeholder ─
    static bool PatchCheatSessionKey(std::vector<uint8_t>& cheat_bytes,
                                      uint64_t session_key)
    {
        if (cheat_bytes.size() < sizeof(IMAGE_DOS_HEADER)) return false;
        auto dos = (IMAGE_DOS_HEADER*)cheat_bytes.data();
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        auto nt = (IMAGE_NT_HEADERS*)(cheat_bytes.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

        static const uint8_t SENTINEL[8] = { 0xBE, 0xBA, 0xFE, 0xCA,
                                              0xCE, 0xFA, 0xED, 0xFE };
        int patched_count = 0;

        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            auto sec = IMAGE_FIRST_SECTION(nt) + i;
            uint8_t* base = cheat_bytes.data() + sec->PointerToRawData;
            DWORD size = sec->SizeOfRawData;
            if (size < 8) continue;

            for (DWORD off = 0; off + 8 <= size; off++) {
                if (memcmp(base + off, SENTINEL, 8) != 0) continue;
                uint64_t v = session_key;
                memcpy(base + off, &v, sizeof(uint64_t));
                patched_count++;
                break;
            }
            if (patched_count) break;
        }
        return patched_count > 0;
    }

    static bool PatchCheatCredentials(std::vector<uint8_t>& cheat_bytes,
                                      const std::string& token,
                                      const std::string& hwid)
    {
        if (cheat_bytes.size() < sizeof(IMAGE_DOS_HEADER)) return false;
        auto dos = (IMAGE_DOS_HEADER*)cheat_bytes.data();
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        auto nt = (IMAGE_NT_HEADERS*)(cheat_bytes.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

        const char* TOKEN_SENTINEL = xorstr_("SHADOW_TOKEN_SENTINEL_");
        const char* HWID_SENTINEL  = xorstr_("SHADOW_HWID_SENTINEL_");
        const DWORD BUF_SIZE = 512; // ← буфер увеличен с 256 до 512
        
        int patched_count = 0;

        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            auto sec = IMAGE_FIRST_SECTION(nt) + i;
            uint8_t* base = cheat_bytes.data() + sec->PointerToRawData;
            DWORD size = sec->SizeOfRawData;
            if (size < BUF_SIZE) continue;

            for (DWORD off = 0; off + BUF_SIZE <= size; off++) {
                if (memcmp(base + off, TOKEN_SENTINEL, strlen(TOKEN_SENTINEL)) == 0) {
                    memset(base + off, 0, BUF_SIZE);
                    size_t copy_len = token.length();
                    if (copy_len >= BUF_SIZE) copy_len = BUF_SIZE - 1;
                    memcpy(base + off, token.c_str(), copy_len);
                    patched_count++;
                }
                else if (memcmp(base + off, HWID_SENTINEL, strlen(HWID_SENTINEL)) == 0) {
                    memset(base + off, 0, BUF_SIZE);
                    size_t copy_len = hwid.length();
                    if (copy_len >= BUF_SIZE) copy_len = BUF_SIZE - 1;
                    memcpy(base + off, hwid.c_str(), copy_len);
                    patched_count++;
                }
            }
        }
        return patched_count > 0;
    }
} // namespace shadow_driver_ctl
