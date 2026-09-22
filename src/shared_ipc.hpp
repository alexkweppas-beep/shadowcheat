#pragma once
#include <windows.h>
#include <cstdint>

#pragma pack(push, 1)
struct ShadowSharedAuth {
    uint32_t magic;            // 0x53484457 ("SHDW")
    uint32_t version;          // 1
    uint64_t timestamp;        // GetTickCount64()
    char     username[64];     // Login username
    char     token[512];       // JWT Token / Session Token
    char     hwid[128];        // User HWID
    int32_t  days_remaining;   // Subscription days
    uint32_t pid;              // Loader PID
};

struct ShadowLaunchAuth {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t loader_pid;
    uint64_t issued_tick;
    uint64_t nonce;
    int32_t  hours_remaining;
    uint32_t variant_id;
    char     username[64];
    char     launch_ticket[1024];
    char     customer_tag[64];
    char     artifact_hash[65];
};

// ─── Trigger / Silent Aim settings (хранится в лоадере, читается треда тригера) ───
struct TriggerSharedSettings {
    // Trigger
    volatile bool    trigger_enabled;  // тригер вкл/выкл
    volatile int     trigger_bind_vk;  // VK код клавиши-бинда (0 = disabled)
    volatile int     trigger_delay_ms; // задержка 0-5 мс
    // Silent Aim
    volatile bool    silent_enabled;   // сайлент вкл/выкл
    volatile bool    has_silent;       // куплен ли сайлент аддон
    volatile int     silent_bind_vk;   // VK код клавиши бинда сайлента
    volatile float   silent_fov;       // FOV 0-360
    volatile int     silent_hitbox;    // 0=Голова, 1=Грудь, 2=Пах, 3=Nearest
    // Control
    volatile bool    cheat_active;     // лоадер смаппировал и запустил чит (Enable нажат)
    volatile int32_t game_pid;         // PID процесса GTA5.exe
    volatile uint64_t game_base;       // ImageBase GTA5
};
#pragma pack(pop)

#define SHADOW_SHARED_MEMORY_NAME "Local\\SHADOW_IPC_AUTH_SECTION"
#define SHADOW_SHARED_MAGIC       0x53484457
#define SHADOW_LAUNCH_AUTH_MAGIC  0x53484C41
#define SHADOW_LAUNCH_AUTH_VERSION 3
