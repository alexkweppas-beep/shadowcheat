#pragma once
// Локальная копия IOCTL-контракта с драйвером (launcher-самодостаточность после
// сплита репо на компонентные ветки). Синхронизирована с driver-side util.h
// (серверный проект «фикс»). Меняешь здесь — синхронизируй там.

#include <Windows.h>
#include <winioctl.h>
#include <cstdint>

#ifndef FILE_SPECIAL_ACCESS
#define FILE_SPECIAL_ACCESS (FILE_ANY_ACCESS)
#endif

 
//  Protocol synced with driver-side util.h (фикс/main/util.h).
//  CHANGE ANYTHING HERE → SYNC driver util.h!
//  Layout структур обязан совпадать (одинаковые типы/offsets).
 

// ─── Существующие IOCTL коды — синхронизированы с driver util.h ───
// (функциональные коды взяты из main/util.h — не менять!)
#define SHADOW_FUNC_VIRTUAL   0xac23f19
#define SHADOW_FUNC_PHYSICAL  0xc3429b2
#define SHADOW_FUNC_IMAGE     0x91bc7a2

#define SHADOW_CODE_VIRTUAL   CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_VIRTUAL,  METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define SHADOW_CODE_PHYSICAL  CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_PHYSICAL, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define SHADOW_CODE_IMAGE     CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_IMAGE,    METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

// ─── Новые IOCTL коды — синхронизированы с driver util.h ───
#define SHADOW_FUNC_HANDSHAKE        0xd1a4b33
#define SHADOW_FUNC_HEARTBEAT        0xe2b5c44
#define SHADOW_FUNC_SET_TARGET       0xf3c6d55
#define SHADOW_FUNC_PROTECT_PROCESS  0xa4d7e66
#define SHADOW_FUNC_ACCESS_LOG       0xb5e8f77
#define SHADOW_FUNC_TRIM_WS          0xc6f9a88
#define SHADOW_FUNC_WIPE_HEADER      0xd7aab99
#define SHADOW_FUNC_CLEANUP          0xe8bcbaa

#define SHADOW_CODE_HANDSHAKE        CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_HANDSHAKE,       METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define SHADOW_CODE_HEARTBEAT        CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_HEARTBEAT,       METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define SHADOW_CODE_SET_TARGET       CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_SET_TARGET,      METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define SHADOW_CODE_PROTECT_PROCESS  CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_PROTECT_PROCESS, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define SHADOW_CODE_ACCESS_LOG       CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_ACCESS_LOG,      METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define SHADOW_CODE_TRIM_WS          CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_TRIM_WS,         METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define SHADOW_CODE_WIPE_HEADER      CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_WIPE_HEADER,     METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define SHADOW_CODE_CLEANUP          CTL_CODE(FILE_DEVICE_UNKNOWN, SHADOW_FUNC_CLEANUP,         METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

// ─── Security code (для базовых операций virtual/physical/image) ───
#define SHADOW_SECURITY_CODE  0xb91ac76

// ─── Операционные режимы (mirror driver util.h) ───
#define SHADOW_PHYSICAL_READ   0xa91f72c
#define SHADOW_PHYSICAL_WRITE  0xc19a341
#define SHADOW_VIRTUAL_READ    0xb9210a3
#define SHADOW_VIRTUAL_WRITE   0xa193f81

// Устройство: драйвер создаёт \Device\monitorinspect; user-mode открывает \\.\monitorinspect
static inline void shadow_format_device_path(char* buf, size_t buf_sz) {
    const char* static_name = "\\\\.\\monitorinspect";
    size_t i = 0;
    while (static_name[i] && i < buf_sz - 1) {
        buf[i] = static_name[i];
        i++;
    }
    buf[i] = '\0';
}

namespace driver
{
    // ─── Layout sync'd with driver util.h
    #pragma pack(push, 8)

    // Базовые операции (security_code auth — sync с t_virtual/t_physical/t_image)
    struct VirtualRequest {
        int32_t  security_code;
        int32_t  process_id;
        int32_t  virtual_mode;
        uint32_t _pad;
        uint64_t address;
        uint64_t buffer;
        uint64_t size;
    };
    struct PhysicalRequest {
        int32_t  security_code;
        int32_t  process_id;
        int32_t  physical_mode;
        uint32_t _pad;
        uint64_t address;
        uint64_t buffer;
        uint64_t size;
    };
    struct ImageRequest {
        int32_t   security_code;
        int32_t   process_id;
        uint64_t* address;
    };

    // Новые операции (session_key auth)
    struct HandshakeRequest {
        uint64_t session_key;
        uint32_t loader_pid;
        uint32_t build_seed;
    };
    struct HeartbeatRequest {
        uint64_t session_key;
        uint32_t request_id;
        uint32_t _pad;
    };
    struct SetTargetRequest {
        uint64_t session_key;
        uint32_t request_id;
        uint32_t target_pid;
    };
    struct ProtectProcessRequest {
        uint64_t session_key;
        uint32_t request_id;
        uint32_t target_pid;
        uint32_t protect_flags;  // 0x1 = no dump, 0x2 = no open
        uint32_t _pad;
    };

    // Access log (кто пытался подключиться к нашим процессам)
    static constexpr uint32_t ACCESS_LOG_SIZE = 64;
    struct AccessEvent {
        uint32_t requester_pid;
        uint32_t target_pid;
        uint32_t desired_access;
        uint32_t _pad;
        uint64_t timestamp;
    };
    struct AccessLogDumpRequest {
        uint64_t    session_key;
        uint32_t    request_id;
        uint32_t    count;                       // [out]
        AccessEvent events[ACCESS_LOG_SIZE];     // [out]
    };

    struct TrimWsRequest {
        uint64_t session_key;
        uint32_t request_id;
        uint32_t target_pid;  // 0 = лоадер
    };
    struct WipeHeaderRequest {
        uint64_t session_key;
        uint32_t request_id;
        uint32_t target_pid;
        uint64_t base_address;
        uint32_t header_size;
        uint32_t _pad;
    };

    // Cleanup flags (bitmask) — sync с driver util.h CLEANUP_FLAG_*
    static constexpr uint32_t CLEANUP_FLAG_BAM         = (1u << 0);
    static constexpr uint32_t CLEANUP_FLAG_USN_JOURNAL = (1u << 1);
    static constexpr uint32_t CLEANUP_FLAG_SYSINFO     = (1u << 2);
    static constexpr uint32_t CLEANUP_FLAG_DNS         = (1u << 3);
    static constexpr uint32_t CLEANUP_FLAG_EVENT_LOG   = (1u << 4);
    static constexpr uint32_t CLEANUP_FLAG_PREFETCH    = (1u << 5);
    static constexpr uint32_t CLEANUP_FLAG_SELF_DELETE = (1u << 6);
    static constexpr uint32_t CLEANUP_FLAG_ALL         = 0x7Fu;

    struct CleanupRequest {
        uint64_t session_key;
        uint32_t request_id;
        uint32_t flags;            // bitmask CLEANUP_FLAG_*
        wchar_t  loader_path[260]; // путь к .exe лоадера (для CLEANUP_FLAG_SELF_DELETE)
    };

    #pragma pack(pop)

    // Глобальные: session_key (loader патчит перед inject) и счётчик запросов
    // volatile гарантирует literal sentinel в .data для loader patch scan
    extern volatile uint64_t g_cheat_session_key;
    extern uint32_t g_cheat_request_id;
}
