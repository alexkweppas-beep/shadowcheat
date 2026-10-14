#pragma once
/* Общий CleanRequest для лаунчера и драйвера. Запрос находится в памяти лаунчера.
 * Используются базовые типы, чтобы заголовок собирался в юзермоде и ядре без зависимости от CRT. */

#define SHADOW_CLEAN_MAGIC  0x5348434C4E303031ULL  // 'SHCLN001'

// Что драйвер делает (bitmask).
#define SC_DO_USN       (1u << 0)  // USN journal delete на обоих томах
#define SC_DO_EVENTLOG  (1u << 1)  // force-truncate System/Application/Security .evtx
#define SC_DO_REGISTRY  (1u << 2)  // реестр (HKLM + HKCU через user_sid)
#define SC_DO_FILES     (1u << 3)  // force-delete список file_paths[]
#define SC_DO_RAWNTFS   (1u << 4)  // raw NTFS: zero $LogFile + освобождённые MFT-записи (BSOD-риск, валидируется)
#define SC_DO_ALL       0xFu

#define SC_MAX_FILES    32
#define SC_PATH_LEN     260

#pragma pack(push, 8)
typedef struct _CleanRequest {
    unsigned long long magic;           // SHADOW_CLEAN_MAGIC
    unsigned long      flags;           // SC_DO_*
    unsigned long      loader_pid;
    wchar_t  user_sid[64];              // "S-1-5-21-..." — для \Registry\User\<sid>
    wchar_t  system_volume[8];          // L"C:"
    wchar_t  loader_volume[8];          // диск exe лоадера
    wchar_t  loader_path[SC_PATH_LEN];  // полный путь к exe лоадера
    wchar_t  loader_dir[SC_PATH_LEN];   // каталог лоадера
    wchar_t  loader_base[64];           // имя exe без расширения ("loader")
    wchar_t  user_profile[SC_PATH_LEN]; // %USERPROFILE% (Downloads/Desktop)
    wchar_t  dns_domain[128];           // домен для выкидывания из DNS-кэша
    // Точные пути для force-delete из ядра (юзермод их перечисляет, драйвер
    // только удаляет — активность удаления не светится как юзермод-операция).
    unsigned long file_count;
    wchar_t  file_paths[SC_MAX_FILES][SC_PATH_LEN];
    // MFT record index каждого файла (параллельно file_paths), захваченный
    // юзермодом через FSCTL_READ_FILE_USN_DATA ДО удаления. 0 = неизвестно.
    // Драйвер затирает эти записи в $MFT после удаления файлов — имя удалённого
    // файла не лингерит в MFT для VortexMFTPlus/Recovery.
    unsigned long long mft_indices[SC_MAX_FILES];
} CleanRequest;
#pragma pack(pop)

typedef NTSTATUS (/* __stdcall */ *PFN_ShadowCleanEntry)(CleanRequest* req, unsigned long long reserved);
