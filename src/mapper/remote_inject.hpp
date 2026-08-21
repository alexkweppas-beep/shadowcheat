#pragma once
/*
 *  loader/src/mapper/remote_inject.hpp — usermode manual-map into a target
 *  process. No driver, no kernel.
 *
 *  This is the delivery path for the internal product: the payload DLL is
 *  mapped into GTA5.exe from user mode and its DllMain is started on a new
 *  thread. Nothing is loaded into the kernel, so there is nothing to disable
 *  (no HVCI, no vulnerable-driver blocklist, no Secure Boot concern).
 *
 *  Two details are easy to get wrong and both were learned the hard way:
 *    - Imports are resolved against the remote module's own export table,
 *      because the target may load a different build of a DLL (RAGE MP ships
 *      its own D3DCompiler_47.dll) and the local copy's offsets would be wrong.
 *    - Export forwarders (kernel32's HeapAlloc forwards to NTDLL, and so on)
 *      must be followed; reading the export RVA as an address points the
 *      import at a string, and the first call jumps into data.
 */

#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace RemoteInject {

    static std::string g_last_error;
    static void set_err(const char* fmt, ...) {
        char buf[256];
        va_list ap; va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        buf[sizeof(buf) - 1] = 0;
        g_last_error = buf;
    }
    const std::string& LastError() { return g_last_error; }

    static void EnableDebugPrivilege() {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(),
                              TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
            return;
        LUID luid{};
        if (LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
            TOKEN_PRIVILEGES tp{};
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        }
        CloseHandle(token);
    }

    static DWORD FindPid(const wchar_t* name) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        DWORD pid = 0;
        if (Process32FirstW(snap, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, name) == 0) { pid = entry.th32ProcessID; break; }
            } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);
        return pid;
    }

    struct PeInfo {
        IMAGE_NT_HEADERS64* nt{};
        IMAGE_SECTION_HEADER* sections{};
        uint16_t sectionCount{};
        uint64_t imageBase{};
        uint32_t sizeOfImage{};
        uint32_t sizeOfHeaders{};
        uint32_t entryRva{};
    };

    static bool ParsePe(uint8_t* data, size_t size, PeInfo& out) {
        if (size < sizeof(IMAGE_DOS_HEADER)) return false;
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(data);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        out.nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(data + dos->e_lfanew);
        if (out.nt->Signature != IMAGE_NT_SIGNATURE) return false;
        if (out.nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
        out.sections = IMAGE_FIRST_SECTION(out.nt);
        out.sectionCount = out.nt->FileHeader.NumberOfSections;
        out.imageBase = out.nt->OptionalHeader.ImageBase;
        out.sizeOfImage = out.nt->OptionalHeader.SizeOfImage;
        out.sizeOfHeaders = out.nt->OptionalHeader.SizeOfHeaders;
        out.entryRva = out.nt->OptionalHeader.AddressOfEntryPoint;
        return true;
    }

    static uint8_t* RvaToLocal(uint8_t* data, const PeInfo& pe, uint32_t rva) {
        for (uint16_t i = 0; i < pe.sectionCount; ++i) {
            const auto& s = pe.sections[i];
            if (rva >= s.VirtualAddress && rva < s.VirtualAddress + s.Misc.VirtualSize)
                return data + s.PointerToRawData + (rva - s.VirtualAddress);
        }
        return nullptr;
    }

    static uintptr_t RemoteModuleBase(HANDLE process, DWORD pid, const wchar_t* name) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
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

    static bool RemoteLoadLibrary(HANDLE process, const wchar_t* name) {
        wchar_t fullPath[MAX_PATH]{};
        UINT len = GetSystemDirectoryW(fullPath, MAX_PATH);
        if (!len || len >= MAX_PATH) return false;
        wcscat_s(fullPath, L"\\"); wcscat_s(fullPath, name);
        SIZE_T bytes = (wcslen(fullPath) + 1) * sizeof(wchar_t);
        void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote) return false;
        bool ok = WriteProcessMemory(process, remote, fullPath, bytes, nullptr) != FALSE;
        if (ok) {
            auto* ll = reinterpret_cast<LPTHREAD_START_ROUTINE>(
                GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
            HANDLE thread = CreateRemoteThread(process, nullptr, 0, ll, remote, 0, nullptr);
            if (thread) { WaitForSingleObject(thread, 10000); CloseHandle(thread); }
            else ok = false;
        }
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return ok;
    }

    // Reads the remote module's export table for one function. Following
    // forwarders is what makes this correct where a plain RVA read is not.
    static uintptr_t ResolveRemoteExport(HANDLE process, DWORD pid, uintptr_t remoteDll,
                                         const char* name, int depth);

    static uintptr_t FollowForwarder(HANDLE process, DWORD pid, uintptr_t stringVa, int depth) {
        char forwarder[128]{};
        if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(stringVa),
                               forwarder, sizeof(forwarder) - 1, nullptr))
            return 0;
        forwarder[sizeof(forwarder) - 1] = 0;
        char* dot = strchr(forwarder, '.');
        if (!dot) return 0;
        *dot = 0;
        const char* targetName = dot + 1;
        wchar_t moduleName[MAX_PATH]{};
        MultiByteToWideChar(CP_ACP, 0, forwarder, -1, moduleName, MAX_PATH);
        wcscat_s(moduleName, L".dll");
        uintptr_t targetBase = RemoteModuleBase(process, pid, moduleName);
        if (!targetBase) {
            if (!RemoteLoadLibrary(process, moduleName)) return 0;
            targetBase = RemoteModuleBase(process, pid, moduleName);
            if (!targetBase) return 0;
        }
        return ResolveRemoteExport(process, pid, targetBase, targetName, depth + 1);
    }

    static uintptr_t ResolveRemoteExport(HANDLE process, DWORD pid, uintptr_t remoteDll,
                                         const char* name, int depth) {
        if (depth > 4) return 0;
        uint8_t headerBuf[0x1000]{};
        if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(remoteDll),
                               headerBuf, sizeof(headerBuf), nullptr))
            return 0;
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(headerBuf);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(headerBuf + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        const auto& exportDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!exportDir.VirtualAddress) return 0;

        IMAGE_EXPORT_DIRECTORY exports{};
        if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(remoteDll + exportDir.VirtualAddress),
                               &exports, sizeof(exports), nullptr))
            return 0;
        const uint32_t count = exports.NumberOfNames;
        std::vector<uint32_t> nameRvas(count);
        std::vector<uint16_t> ordinals(count);
        if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(remoteDll + exports.AddressOfNames),
                               nameRvas.data(), count * sizeof(uint32_t), nullptr))
            return 0;
        if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(remoteDll + exports.AddressOfNameOrdinals),
                               ordinals.data(), count * sizeof(uint16_t), nullptr))
            return 0;

        for (uint32_t i = 0; i < count; ++i) {
            char remoteName[128]{};
            if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(remoteDll + nameRvas[i]),
                                   remoteName, sizeof(remoteName) - 1, nullptr))
                return 0;
            remoteName[sizeof(remoteName) - 1] = 0;
            if (strcmp(remoteName, name) != 0) continue;
            uint32_t functionRva = 0;
            if (!ReadProcessMemory(process,
                                   reinterpret_cast<LPCVOID>(remoteDll + exports.AddressOfFunctions +
                                       ordinals[i] * sizeof(uint32_t)),
                                   &functionRva, sizeof(functionRva), nullptr))
                return 0;
            if (functionRva >= exportDir.VirtualAddress &&
                functionRva < exportDir.VirtualAddress + exportDir.Size) {
                return FollowForwarder(process, pid, remoteDll + functionRva, depth);
            }
            return remoteDll + functionRva;
        }
        return 0;
    }

    static bool ResolveImports(HANDLE process, DWORD pid, uint8_t* localImage,
                               const PeInfo& pe, uintptr_t remoteBase) {
        const auto& dir = pe.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress) return true;
        auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(RvaToLocal(localImage, pe, dir.VirtualAddress));
        if (!desc) return false;

        int unresolved = 0;
        for (; desc->Name; ++desc) {
            const char* dllNameA = reinterpret_cast<const char*>(RvaToLocal(localImage, pe, desc->Name));
            if (!dllNameA) return false;
            wchar_t dllName[MAX_PATH]{};
            MultiByteToWideChar(CP_ACP, 0, dllNameA, -1, dllName, MAX_PATH);

            uintptr_t remoteDll = RemoteModuleBase(process, pid, dllName);
            if (!remoteDll) {
                if (!RemoteLoadLibrary(process, dllName)) { set_err("load %s", dllNameA); return false; }
                remoteDll = RemoteModuleBase(process, pid, dllName);
                if (!remoteDll) { set_err("still missing %s", dllNameA); return false; }
            }

            auto* thunkLocal = reinterpret_cast<IMAGE_THUNK_DATA64*>(RvaToLocal(localImage, pe, desc->FirstThunk));
            auto* namesLocal = reinterpret_cast<IMAGE_THUNK_DATA64*>(RvaToLocal(
                localImage, pe, desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
            if (!thunkLocal || !namesLocal) return false;

            for (; namesLocal->u1.AddressOfData; ++namesLocal, ++thunkLocal) {
                if (namesLocal->u1.Ordinal & IMAGE_ORDINAL_FLAG64) { ++unresolved; continue; }
                auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(RvaToLocal(
                    localImage, pe, static_cast<uint32_t>(namesLocal->u1.AddressOfData)));
                if (!importByName) return false;
                uintptr_t function = ResolveRemoteExport(process, pid, remoteDll, importByName->Name, 0);
                if (!function) {
                    // API-set forwarders are not listed under the host DLL's own
                    // names. For a System32 module the local copy is the same
                    // build at the same base, and GetProcAddress follows them.
                    HMODULE localDll = LoadLibraryW(dllName);
                    if (localDll) {
                        if (auto* proc = GetProcAddress(localDll, importByName->Name)) {
                            function = reinterpret_cast<uintptr_t>(proc) -
                                reinterpret_cast<uintptr_t>(localDll) + remoteDll;
                        }
                    }
                }
                if (!function) { set_err("import %s!%s", dllNameA, importByName->Name); ++unresolved; continue; }
                thunkLocal->u1.Function = function;
            }
        }
        if (unresolved) { set_err("%d unresolved imports", unresolved); return false; }
        return true;
    }

    static void ApplyRelocations(uint8_t* localImage, const PeInfo& pe, int64_t delta) {
        const auto& dir = pe.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (!dir.VirtualAddress) return;
        auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(RvaToLocal(localImage, pe, dir.VirtualAddress));
        if (!block) return;
        const uint8_t* end = reinterpret_cast<uint8_t*>(block) + dir.Size;
        while (reinterpret_cast<uint8_t*>(block) < end &&
               block->SizeOfBlock >= sizeof(IMAGE_BASE_RELOCATION)) {
            const uint32_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
            auto* entries = reinterpret_cast<uint16_t*>(block + 1);
            for (uint32_t i = 0; i < count; ++i) {
                const uint16_t type = entries[i] >> 12;
                const uint16_t offset = entries[i] & 0x0FFF;
                if (type != IMAGE_REL_BASED_DIR64) continue;
                auto* slot = reinterpret_cast<uint64_t*>(RvaToLocal(localImage, pe, block->VirtualAddress + offset));
                if (slot) *slot += delta;
            }
            block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
                reinterpret_cast<uint8_t*>(block) + block->SizeOfBlock);
        }
    }

    // Stub run by the new remote thread: CreateRemoteThread passes one argument,
    // DllMain wants (base, reason, reserved), so the stub fixes the registers.
    static void BuildEntryStub(uint8_t* stub, uintptr_t base, uintptr_t entry) {
        uint32_t off = 0;
        stub[off++] = 0x48; stub[off++] = 0xB9;                       // mov rcx, base
        memcpy(stub + off, &base, 8); off += 8;
        stub[off++] = 0xBA;                                           // mov edx, 1
        const uint32_t one = 1; memcpy(stub + off, &one, 4); off += 4;
        stub[off++] = 0x45; stub[off++] = 0x31; stub[off++] = 0xC0;   // xor r8d, r8d
        stub[off++] = 0x48; stub[off++] = 0x83; stub[off++] = 0xEC; stub[off++] = 0x28; // sub rsp, 0x28
        stub[off++] = 0x48; stub[off++] = 0xB8;                       // mov rax, entry
        memcpy(stub + off, &entry, 8); off += 8;
        stub[off++] = 0xFF; stub[off++] = 0xD0;                       // call rax
        stub[off++] = 0x48; stub[off++] = 0x83; stub[off++] = 0xC4; stub[off++] = 0x28; // add rsp, 0x28
        stub[off++] = 0xC3;                                           // ret
    }

    // After DllMain has run, drop the initial RWX: headers become read-only
    // (the payload zeroed them itself) and each section gets the tightest
    // protection its characteristics allow. A private RWX region of image
    // size is the loudest VAD-scan signal; this removes it.
    static void ApplySectionProtections(HANDLE process, void* remoteBase, const PeInfo& pe) {
        auto protect = [&](uint32_t rva, uint32_t size, DWORD prot) {
            if (!size) return;
            DWORD old_prot = 0;
            VirtualProtectEx(process,
                             reinterpret_cast<uint8_t*>(remoteBase) + rva,
                             size, prot, &old_prot);
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
            if (x)       prot = r ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
            else if (w)  prot = PAGE_READWRITE;
            else if (r)  prot = PAGE_READONLY;

            protect(s.VirtualAddress, size, prot);
        }
    }

    // Manual-maps dllBytes into the process and runs DllMain(DLL_PROCESS_ATTACH)
    // on a new thread. Returns true once DllMain has returned.
    static bool MapIntoProcess(DWORD pid, const std::vector<uint8_t>& dllBytes) {
        if (!pid) { set_err("no pid"); return false; }
        std::vector<uint8_t> image = dllBytes;
        PeInfo pe{};
        if (!ParsePe(image.data(), image.size(), pe)) { set_err("not a PE64"); return false; }

        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
                                     FALSE, pid);
        if (!process) { set_err("OpenProcess %lu", GetLastError()); return false; }

        void* remoteBase = VirtualAllocEx(process, nullptr, pe.sizeOfImage,
                                          MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!remoteBase) { set_err("VirtualAllocEx %lu", GetLastError()); CloseHandle(process); return false; }

        const int64_t delta = reinterpret_cast<uintptr_t>(remoteBase) - pe.imageBase;
        if (delta) ApplyRelocations(image.data(), pe, delta);
        if (!ResolveImports(process, pid, image.data(), pe, reinterpret_cast<uintptr_t>(remoteBase))) {
            VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE); CloseHandle(process); return false;
        }

        if (!WriteProcessMemory(process, remoteBase, image.data(), pe.sizeOfHeaders, nullptr)) {
            set_err("header write %lu", GetLastError());
            VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE); CloseHandle(process); return false;
        }
        for (uint16_t i = 0; i < pe.sectionCount; ++i) {
            const auto& s = pe.sections[i];
            if (!s.SizeOfRawData) continue;
            void* dst = reinterpret_cast<uint8_t*>(remoteBase) + s.VirtualAddress;
            if (!WriteProcessMemory(process, dst, image.data() + s.PointerToRawData, s.SizeOfRawData, nullptr)) {
                set_err("section %u write %lu", i, GetLastError());
                VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE); CloseHandle(process); return false;
            }
        }

        uint8_t stub[64]{};
        BuildEntryStub(stub, reinterpret_cast<uintptr_t>(remoteBase),
                       reinterpret_cast<uintptr_t>(remoteBase) + pe.entryRva);
        void* remoteStub = VirtualAllocEx(process, nullptr, sizeof(stub),
                                          MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!remoteStub || !WriteProcessMemory(process, remoteStub, stub, sizeof(stub), nullptr)) {
            set_err("stub write %lu", GetLastError());
            VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE); CloseHandle(process); return false;
        }

        HANDLE thread = CreateRemoteThread(process, nullptr, 0,
                                           reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteStub),
                                           nullptr, 0, nullptr);
        if (!thread) {
            set_err("CreateRemoteThread %lu", GetLastError());
            VirtualFreeEx(process, remoteBase, 0, MEM_RELEASE); CloseHandle(process); return false;
        }
        WaitForSingleObject(thread, 30000);
        DWORD exitCode = 0;
        GetExitCodeThread(thread, &exitCode);
        CloseHandle(thread);
        VirtualFreeEx(process, remoteStub, 0, MEM_RELEASE);

        // DllMain returns TRUE(1) on success; an exception code means it faulted.
        if (exitCode != 1) { set_err("DllMain exit 0x%08X", exitCode); CloseHandle(process); return false; }

        // DllMain wiped the PE header while the region was still RWX; now
        // tighten every section to its final protection.
        ApplySectionProtections(process, remoteBase, pe);
        CloseHandle(process);
        return true;
    }

    // Finds GTA5 and injects. Returns true once the payload's DllMain returned.
    static bool InjectIntoGame(const std::vector<uint8_t>& dllBytes) {
        EnableDebugPrivilege();
        DWORD pid = FindPid(L"GTA5.exe");
        if (!pid) { set_err("GTA5.exe not found"); return false; }
        return MapIntoProcess(pid, dllBytes);
    }

} // namespace RemoteInject
