// shadow_injector — usermode manual-map injector for shadow_internal.dll
//
// This is the no-driver delivery path. It maps the payload into GTA5.exe from
// user mode: headers, sections, relocations, imports, then starts
// DllMain(base, DLL_PROCESS_ATTACH, 0) on a new thread through a small stub,
// because CreateRemoteThread passes a single argument and DllMain needs three.
//
// No driver, no HVCI/blocklist concern: nothing is loaded into the kernel.
//
// Two details are easy to get wrong and both were learned the hard way:
//   - Imports are resolved against the remote module's own export table,
//     because the target may load a different build of a DLL (RAGE MP ships
//     its own D3DCompiler_47.dll) and the local copy's offsets would be wrong.
//   - Export forwarders (kernel32's HeapAlloc forwards to NTDLL, and so on)
//     must be followed; reading the export RVA as an address points the import
//     at a string, and the first call jumps into data.

#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
    void Log(const char* fmt, ...)
    {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        buf[sizeof(buf) - 1] = 0;
        printf("%s\n", buf);
    }

    bool EnableDebugPrivilege()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(
                GetCurrentProcess(),
                TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                &token))
            return false;
        LUID luid{};
        const bool found =
            LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid) != FALSE;
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        const bool ok = found &&
            AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr,
                                  nullptr) != FALSE;
        CloseHandle(token);
        return ok;
    }

    DWORD FindPid(const wchar_t* name)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return 0;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        DWORD pid = 0;
        if (Process32FirstW(snap, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, name) == 0) {
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);
        return pid;
    }

    std::vector<uint8_t> ReadWholeFile(const wchar_t* path)
    {
        HANDLE file = CreateFileW(
            path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return {};
        LARGE_INTEGER size{};
        GetFileSizeEx(file, &size);
        std::vector<uint8_t> data(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        const bool ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()),
                                 &read, nullptr) && read == data.size();
        CloseHandle(file);
        if (!ok)
            return {};
        return data;
    }

    struct PeInfo
    {
        IMAGE_NT_HEADERS64* nt{};
        IMAGE_SECTION_HEADER* sections{};
        uint16_t sectionCount{};
        uint64_t imageBase{};
        uint32_t sizeOfImage{};
        uint32_t sizeOfHeaders{};
        uint32_t entryRva{};
    };

    bool ParsePe(uint8_t* data, PeInfo& out)
    {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(data);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;
        out.nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(data + dos->e_lfanew);
        if (out.nt->Signature != IMAGE_NT_SIGNATURE)
            return false;
        if (out.nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return false;
        out.sections = IMAGE_FIRST_SECTION(out.nt);
        out.sectionCount = out.nt->FileHeader.NumberOfSections;
        out.imageBase = out.nt->OptionalHeader.ImageBase;
        out.sizeOfImage = out.nt->OptionalHeader.SizeOfImage;
        out.sizeOfHeaders = out.nt->OptionalHeader.SizeOfHeaders;
        out.entryRva = out.nt->OptionalHeader.AddressOfEntryPoint;
        return true;
    }

    uint8_t* RvaToLocal(uint8_t* data, const PeInfo& pe, uint32_t rva)
    {
        for (uint16_t i = 0; i < pe.sectionCount; ++i) {
            const auto& s = pe.sections[i];
            if (rva >= s.VirtualAddress &&
                rva < s.VirtualAddress + s.Misc.VirtualSize)
                return data + s.PointerToRawData + (rva - s.VirtualAddress);
        }
        return nullptr;
    }

    // Reads the remote module's export table and returns the absolute address
    // of one exported function. The target may load a different build of the
    // same DLL (RAGE MP ships its own D3DCompiler_47.dll), so resolving through
    // the local copy's offsets lands in the middle of the wrong function.
    uintptr_t RemoteModuleBase(HANDLE process, DWORD pid, const wchar_t* name);
    bool RemoteLoadLibrary(HANDLE process, const wchar_t* name);
    uintptr_t ResolveRemoteExport(
        HANDLE process, DWORD pid, uintptr_t remoteDll, const char* name,
        int depth = 0);

    // A function RVA that lands inside the export directory is not code but a
    // forwarder string ("NTDLL.RtlAllocateHeap"). Follow it to the real module,
    // otherwise the import points at ASCII and the first call jumps into data.
    uintptr_t FollowForwarder(
        HANDLE process, DWORD pid, uintptr_t remoteDll, uintptr_t stringVa,
        int depth)
    {
        char forwarder[128]{};
        if (!ReadProcessMemory(
                process, reinterpret_cast<LPCVOID>(stringVa), forwarder,
                sizeof(forwarder) - 1, nullptr))
            return 0;
        forwarder[sizeof(forwarder) - 1] = 0;
        char* dot = strchr(forwarder, '.');
        if (!dot)
            return 0;
        *dot = 0;
        const char* targetName = dot + 1;

        wchar_t moduleName[MAX_PATH]{};
        MultiByteToWideChar(CP_ACP, 0, forwarder, -1, moduleName, MAX_PATH);
        // Forwarder names carry no extension.
        wcscat_s(moduleName, L".dll");

        uintptr_t targetBase = RemoteModuleBase(process, pid, moduleName);
        if (!targetBase) {
            if (!RemoteLoadLibrary(process, moduleName))
                return 0;
            targetBase = RemoteModuleBase(process, pid, moduleName);
            if (!targetBase)
                return 0;
        }
        return ResolveRemoteExport(
            process, pid, targetBase, targetName, depth + 1);
    }

    uintptr_t ResolveRemoteExport(
        HANDLE process, DWORD pid, uintptr_t remoteDll, const char* name,
        int depth)
    {
        if (depth > 4)
            return 0;
        uint8_t headerBuf[0x1000]{};
        if (!ReadProcessMemory(
                process, reinterpret_cast<LPCVOID>(remoteDll), headerBuf,
                sizeof(headerBuf), nullptr))
            return 0;
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(headerBuf);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
            headerBuf + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;
        const auto& exportDir =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!exportDir.VirtualAddress)
            return 0;

        IMAGE_EXPORT_DIRECTORY exports{};
        if (!ReadProcessMemory(
                process,
                reinterpret_cast<LPCVOID>(remoteDll + exportDir.VirtualAddress),
                &exports, sizeof(exports), nullptr))
            return 0;

        const uint32_t count = exports.NumberOfNames;
        std::vector<uint32_t> nameRvas(count);
        std::vector<uint16_t> ordinals(count);
        if (!ReadProcessMemory(
                process,
                reinterpret_cast<LPCVOID>(
                    remoteDll + exports.AddressOfNames),
                nameRvas.data(), count * sizeof(uint32_t), nullptr))
            return 0;
        if (!ReadProcessMemory(
                process,
                reinterpret_cast<LPCVOID>(
                    remoteDll + exports.AddressOfNameOrdinals),
                ordinals.data(), count * sizeof(uint16_t), nullptr))
            return 0;

        for (uint32_t i = 0; i < count; ++i) {
            char remoteName[128]{};
            if (!ReadProcessMemory(
                    process,
                    reinterpret_cast<LPCVOID>(remoteDll + nameRvas[i]),
                    remoteName, sizeof(remoteName) - 1, nullptr))
                return 0;
            remoteName[sizeof(remoteName) - 1] = 0;
            if (strcmp(remoteName, name) != 0)
                continue;
            uint32_t functionRva = 0;
            if (!ReadProcessMemory(
                    process,
                    reinterpret_cast<LPCVOID>(
                        remoteDll + exports.AddressOfFunctions +
                            ordinals[i] * sizeof(uint32_t)),
                    &functionRva, sizeof(functionRva), nullptr))
                return 0;
            // Inside the export directory range means a forwarder string, not
            // code.
            if (functionRva >= exportDir.VirtualAddress &&
                functionRva < exportDir.VirtualAddress + exportDir.Size) {
                return FollowForwarder(
                    process, pid, remoteDll, remoteDll + functionRva, depth);
            }
            return remoteDll + functionRva;
        }
        return 0;
    }

    uintptr_t RemoteModuleBase(HANDLE process, DWORD pid, const wchar_t* name)
    {
        HANDLE snap = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE)
            return 0;
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        uintptr_t base = 0;
        if (Module32FirstW(snap, &entry)) {
            do {
                const wchar_t* file = wcsrchr(entry.szModule, L'\\');
                file = file ? file + 1 : entry.szModule;
                if (_wcsicmp(file, name) == 0) {
                    base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                    break;
                }
            } while (Module32NextW(snap, &entry));
        }
        CloseHandle(snap);
        return base;
    }

    // Load a missing DLL into the target by calling its LoadLibraryW.
    bool RemoteLoadLibrary(HANDLE process, const wchar_t* name)
    {
        wchar_t fullPath[MAX_PATH]{};
        const UINT len = GetSystemDirectoryW(fullPath, MAX_PATH);
        if (!len || len >= MAX_PATH)
            return false;
        wcscat_s(fullPath, L"\\");
        wcscat_s(fullPath, name);

        const SIZE_T bytes = (wcslen(fullPath) + 1) * sizeof(wchar_t);
        void* remote = VirtualAllocEx(
            process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE);
        if (!remote)
            return false;
        bool ok = WriteProcessMemory(process, remote, fullPath, bytes, nullptr)
            != FALSE;
        if (ok) {
            auto* loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
                GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                               "LoadLibraryW"));
            HANDLE thread = CreateRemoteThread(
                process, nullptr, 0, loadLibrary, remote, 0, nullptr);
            if (thread) {
                WaitForSingleObject(thread, 10000);
                CloseHandle(thread);
            } else {
                ok = false;
            }
        }
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return ok;
    }

    bool ResolveImports(
        HANDLE process, DWORD pid, uint8_t* localImage, const PeInfo& pe,
        uintptr_t remoteBase)
    {
        const auto& dir =
            pe.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress)
            return true;

        auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            RvaToLocal(localImage, pe, dir.VirtualAddress));
        if (!desc)
            return false;

        int unresolved = 0;
        for (; desc->Name; ++desc) {
            const char* dllNameA = reinterpret_cast<const char*>(
                RvaToLocal(localImage, pe, desc->Name));
            if (!dllNameA)
                return false;
            wchar_t dllName[MAX_PATH]{};
            MultiByteToWideChar(CP_ACP, 0, dllNameA, -1, dllName, MAX_PATH);

            uintptr_t remoteDll = RemoteModuleBase(process, pid, dllName);
            if (!remoteDll) {
                Log("  module %ls not in target, loading remotely", dllName);
                if (!RemoteLoadLibrary(process, dllName))
                    return false;
                remoteDll = RemoteModuleBase(process, pid, dllName);
                if (!remoteDll) {
                    Log("  module %ls still missing after load", dllName);
                    return false;
                }
            }

            auto* thunkLocal = reinterpret_cast<IMAGE_THUNK_DATA64*>(
                RvaToLocal(localImage, pe,
                           desc->FirstThunk));
            auto* namesLocal = reinterpret_cast<IMAGE_THUNK_DATA64*>(
                RvaToLocal(localImage, pe,
                           desc->OriginalFirstThunk
                               ? desc->OriginalFirstThunk
                               : desc->FirstThunk));
            if (!thunkLocal || !namesLocal)
                return false;

            for (; namesLocal->u1.AddressOfData; ++namesLocal, ++thunkLocal) {
                uintptr_t function = 0;
                if (namesLocal->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                    Log("  ordinal import in %ls — not supported", dllName);
                    ++unresolved;
                    continue;
                }
                auto* importByName =
                    reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(RvaToLocal(
                        localImage, pe,
                        static_cast<uint32_t>(
                            namesLocal->u1.AddressOfData)));
                if (!importByName)
                    return false;
                function = ResolveRemoteExport(
                    process, pid, remoteDll, importByName->Name);
                if (!function) {
                    // Some exports are reached through an API-set forwarder
                    // that is not listed under the host DLL's own name table.
                    // For a System32 module the local copy is the same build at
                    // the same base, and GetProcAddress follows the forwarder,
                    // so the offset carries over.
                    HMODULE localDll = LoadLibraryW(dllName);
                    if (localDll) {
                        if (auto* proc =
                                GetProcAddress(localDll, importByName->Name)) {
                            function = reinterpret_cast<uintptr_t>(proc) -
                                reinterpret_cast<uintptr_t>(localDll) +
                                remoteDll;
                        }
                    }
                }
                if (!function) {
                    Log("  unresolved import: %ls!%s", dllName,
                        importByName->Name);
                    ++unresolved;
                    continue;
                }
                thunkLocal->u1.Function = function;
            }
        }
        if (unresolved) {
            Log("  %d unresolved imports", unresolved);
            return false;
        }
        return true;
    }

    void ApplyRelocations(
        uint8_t* localImage, const PeInfo& pe, int64_t delta)
    {
        const auto& dir = pe.nt->OptionalHeader
            .DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (!dir.VirtualAddress)
            return;
        auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
            RvaToLocal(localImage, pe, dir.VirtualAddress));
        if (!block)
            return;
        const uint8_t* end =
            reinterpret_cast<uint8_t*>(block) + dir.Size;
        while (reinterpret_cast<uint8_t*>(block) < end &&
               block->SizeOfBlock >= sizeof(IMAGE_BASE_RELOCATION)) {
            const uint32_t count =
                (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
            auto* entries = reinterpret_cast<uint16_t*>(block + 1);
            for (uint32_t i = 0; i < count; ++i) {
                const uint16_t type = entries[i] >> 12;
                const uint16_t offset = entries[i] & 0x0FFF;
                if (type == IMAGE_REL_BASED_ABSOLUTE)
                    continue;
                if (type != IMAGE_REL_BASED_DIR64)
                    continue;
                auto* slot = reinterpret_cast<uint64_t*>(RvaToLocal(
                    localImage, pe, block->VirtualAddress + offset));
                if (slot)
                    *slot += delta;
            }
            block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
                reinterpret_cast<uint8_t*>(block) + block->SizeOfBlock);
        }
    }

    // After DllMain has run, drop the initial RWX: headers become read-only
    // (the payload zeroed them itself) and each section gets the tightest
    // protection its characteristics allow. A private RWX region of image
    // size is the loudest VAD-scan signal; this removes it.
    void ApplySectionProtections(HANDLE process, void* remoteBase, const PeInfo& pe)
    {
        auto protect = [&](uint32_t rva, uint32_t size, DWORD prot) {
            if (!size) return;
            DWORD oldProt = 0;
            VirtualProtectEx(process,
                             reinterpret_cast<uint8_t*>(remoteBase) + rva,
                             size, prot, &oldProt);
        };

        protect(0, pe.sizeOfHeaders, PAGE_READONLY);

        for (uint16_t i = 0; i < pe.sectionCount; ++i) {
            const auto& s = pe.sections[i];
            uint32_t size = s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;
            if (!size) continue;

            const DWORD c = s.Characteristics;
            const bool x = (c & IMAGE_SCN_MEM_EXECUTE) != 0;
            const bool r = (c & IMAGE_SCN_MEM_READ) != 0;
            const bool w = (c & IMAGE_SCN_MEM_WRITE) != 0;

            DWORD prot = PAGE_NOACCESS;
            if (x)      prot = r ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
            else if (w) prot = PAGE_READWRITE;
            else if (r) prot = PAGE_READONLY;

            protect(s.VirtualAddress, size, prot);
        }
    }

    // Stub run by the new remote thread. CreateRemoteThread passes a single
    // argument, DllMain wants (base, reason, reserved), so the stub fixes the
    // registers and then calls the real entry. Returning from the start
    // routine is legal: the thread start thunk exits the thread for us.
    void BuildEntryStub(
        uint8_t* stub, uintptr_t base, uintptr_t entry)
    {
        uint32_t off = 0;
        // mov rcx, base
        stub[off++] = 0x48; stub[off++] = 0xB9;
        memcpy(stub + off, &base, 8); off += 8;
        // mov edx, 1  (DLL_PROCESS_ATTACH)
        stub[off++] = 0xBA;
        const uint32_t one = 1;
        memcpy(stub + off, &one, 4); off += 4;
        // xor r8d, r8d
        stub[off++] = 0x45; stub[off++] = 0x31; stub[off++] = 0xC0;
        // sub rsp, 0x28  (shadow space + alignment)
        stub[off++] = 0x48; stub[off++] = 0x83; stub[off++] = 0xEC;
        stub[off++] = 0x28;
        // mov rax, entry
        stub[off++] = 0x48; stub[off++] = 0xB8;
        memcpy(stub + off, &entry, 8); off += 8;
        // call rax
        stub[off++] = 0xFF; stub[off++] = 0xD0;
        // add rsp, 0x28
        stub[off++] = 0x48; stub[off++] = 0x83; stub[off++] = 0xC4;
        stub[off++] = 0x28;
        // ret
        stub[off++] = 0xC3;
    }

    // Mirrors the kernel driver's readiness gate: the payload must not be
    // mapped until the graphics stack exists, and it must be mapped before
    // multiplayer.dll loads, which is the timing the kernel path has always
    // used and the only one its code paths are known to survive.
    DWORD WaitForGameReady()
    {
        Log("watching for GTA5.exe...");
        const DWORD start = GetTickCount();
        DWORD pid = 0;
        while (GetTickCount() - start < 300000) {
            if (!pid)
                pid = FindPid(L"GTA5.exe");
            if (pid) {
                HANDLE probe = OpenProcess(
                    PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                if (probe) {
                    const bool dxgi =
                        RemoteModuleBase(probe, pid, L"dxgi.dll") != 0;
                    const bool d3d11 =
                        RemoteModuleBase(probe, pid, L"d3d11.dll") != 0;
                    CloseHandle(probe);
                    if (dxgi && d3d11)
                        return pid;
                } else {
                    pid = 0;  // process died or restarted
                }
            }
            Sleep(200);
        }
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    bool watch = false;
    const wchar_t* dllPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--watch") == 0)
            watch = true;
        else
            dllPath = argv[i];
    }
    // Default payload: shadow_internal.dll next to the injector.
    std::wstring defaultPath;
    if (!dllPath) {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* slash = wcsrchr(exePath, L'\\');
        if (slash)
            *slash = 0;
        defaultPath = std::wstring(exePath) + L"\\shadow_internal.dll";
        dllPath = defaultPath.c_str();
    }

    EnableDebugPrivilege();

    DWORD pid = 0;
    if (watch) {
        pid = WaitForGameReady();
        if (!pid) {
            Log("timed out waiting for GTA5.exe to become ready");
            return 1;
        }
        Log("GTA5.exe pid=%lu graphics ready, injecting", pid);
    } else {
        pid = FindPid(L"GTA5.exe");
        if (!pid) {
            Log("GTA5.exe not found");
            return 1;
        }
        Log("GTA5.exe pid=%lu", pid);
    }

    std::vector<uint8_t> image = ReadWholeFile(dllPath);
    if (image.empty()) {
        Log("cannot read %ls", dllPath);
        return 1;
    }
    PeInfo pe{};
    if (!ParsePe(image.data(), pe)) {
        Log("not a PE64: %ls", dllPath);
        return 1;
    }
    Log("payload: %zu bytes, image size 0x%X, entry RVA 0x%X",
        image.size(), pe.sizeOfImage, pe.entryRva);

    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
        FALSE, pid);
    if (!process) {
        Log("OpenProcess failed: %lu", GetLastError());
        return 1;
    }

    void* remoteBase = VirtualAllocEx(
        process, nullptr, pe.sizeOfImage, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!remoteBase) {
        Log("VirtualAllocEx failed: %lu", GetLastError());
        CloseHandle(process);
        return 1;
    }
    Log("mapped at %p", remoteBase);

    const int64_t delta =
        reinterpret_cast<uintptr_t>(remoteBase) - pe.imageBase;
    if (delta)
        ApplyRelocations(image.data(), pe, delta);

    if (!ResolveImports(
            process, pid, image.data(), pe,
            reinterpret_cast<uintptr_t>(remoteBase))) {
        Log("import resolution failed");
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    // Headers, then sections at their virtual addresses.
    if (!WriteProcessMemory(
            process, remoteBase, image.data(), pe.sizeOfHeaders, nullptr)) {
        Log("header write failed: %lu", GetLastError());
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }
    for (uint16_t i = 0; i < pe.sectionCount; ++i) {
        const auto& s = pe.sections[i];
        if (!s.SizeOfRawData)
            continue;
        void* dst = reinterpret_cast<uint8_t*>(remoteBase) + s.VirtualAddress;
        if (!WriteProcessMemory(
                process, dst, image.data() + s.PointerToRawData,
                s.SizeOfRawData, nullptr)) {
            Log("section %u write failed: %lu", i, GetLastError());
            VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
            CloseHandle(process);
            return 1;
        }
    }
    Log("image written");

    // TLS callbacks, if the image has any. Static CRT images usually register
    // none, but a loader would call them before DllMain, so we do.
    const auto& tlsDir =
        pe.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (tlsDir.VirtualAddress) {
        auto* tls = reinterpret_cast<IMAGE_TLS_DIRECTORY64*>(
            RvaToLocal(image.data(), pe, tlsDir.VirtualAddress));
        if (tls && tls->AddressOfCallBacks) {
            // The callback array is an array of VAs; they were relocated above.
            auto* callbacks = reinterpret_cast<uint64_t*>(RvaToLocal(
                image.data(), pe,
                static_cast<uint32_t>(
                    tls->AddressOfCallBacks - pe.imageBase)));
            if (callbacks && *callbacks) {
                Log("WARNING: TLS callbacks present, not supported by the stub");
            }
        }
    }

    uint8_t stub[64]{};
    BuildEntryStub(
        stub,
        reinterpret_cast<uintptr_t>(remoteBase),
        reinterpret_cast<uintptr_t>(remoteBase) + pe.entryRva);
    void* remoteStub = VirtualAllocEx(
        process, nullptr, sizeof(stub), MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!remoteStub ||
        !WriteProcessMemory(process, remoteStub, stub, sizeof(stub), nullptr)) {
        Log("stub write failed: %lu", GetLastError());
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    HANDLE thread = CreateRemoteThread(
        process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteStub),
        nullptr, 0, nullptr);
    if (!thread) {
        Log("CreateRemoteThread failed: %lu", GetLastError());
        VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }
    Log("entry thread started, waiting for DllMain...");
    WaitForSingleObject(thread, 30000);
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    Log("DllMain returned, thread exit code %lu", exitCode);

    VirtualFreeEx(process, remoteStub, 0, MEM_RELEASE);

    // DllMain wiped the PE header while the region was still RWX; now
    // tighten every section to its final protection.
    if (exitCode == 1)
        ApplySectionProtections(process, remoteBase, pe);

    CloseHandle(process);
    Log("done. Check C:\\RAGEMP\\cheat_log.txt and press F4 in game.");
    return 0;
}
