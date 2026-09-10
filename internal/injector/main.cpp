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

    // Overwrite a file with zeros and delete it. Used by --wipe to match the
    // kernel path's forensic cleanup: once the payload is mapped, its bytes
    // have no reason to stay on disk.
    bool WipeFileFromDisk(const wchar_t* path)
    {
        HANDLE file = CreateFileW(
            path, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        GetFileSizeEx(file, &size);
        uint8_t zeros[65536]{};
        LARGE_INTEGER begin{};
        SetFilePointerEx(file, begin, nullptr, FILE_BEGIN);
        LONGLONG left = size.QuadPart;
        bool ok = true;
        while (left > 0 && ok) {
            const DWORD chunk = left > static_cast<LONGLONG>(sizeof(zeros))
                ? static_cast<DWORD>(sizeof(zeros))
                : static_cast<DWORD>(left);
            DWORD written = 0;
            ok = WriteFile(file, zeros, chunk, &written, nullptr) &&
                written == chunk;
            left -= written;
        }
        CloseHandle(file);
        return ok && DeleteFileW(path) != FALSE;
    }

    // The local copy of the image holds the fully resolved payload (imports
    // fixed up, relocations applied) — everything a memory dump of THIS
    // process would need. Zero it no matter which path wmain exits through.
    struct ZeroOnExit
    {
        std::vector<uint8_t>& v;
        ~ZeroOnExit() {
            if (!v.empty())
                SecureZeroMemory(v.data(), v.size());
        }
    };

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

    // A zeroed page still reads as zeros; a decommitted page does not read
    // at all. Headers and the .init section (DllMain and its helpers, dead
    // after attach) are decommitted so a
    // memory dumper gets a read error instead of their contents.
    void DecommitRange(HANDLE process, void* remoteBase, uint32_t rva, uint32_t size)
    {
        if (!size)
            return;
        VirtualFreeEx(process,
                      reinterpret_cast<uint8_t*>(remoteBase) + rva,
                      size, MEM_DECOMMIT);
    }

    void DecommitSection(
        HANDLE process,
        void* remoteBase,
        const PeInfo& pe,
        const char* wanted)
    {
        if (!wanted)
            return;
        for (uint16_t i = 0; i < pe.sectionCount; ++i) {
            const auto& s = pe.sections[i];
            char name[9]{};
            memcpy(name, s.Name, 8);
            if (strcmp(name, wanted) != 0)
                continue;
            const uint32_t size =
                s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;
            DecommitRange(process, remoteBase, s.VirtualAddress, size);
            Log("%s section decommitted (%u bytes at RVA 0x%X)",
                wanted, size, s.VirtualAddress);
            return;
        }
    }

    // After the protected entry has returned, drop the initial RWX: headers
    // are decommitted and each section gets the tightest protection its
    // characteristics allow. A private RWX region of
    // image size is the loudest VAD-scan signal; this removes it.
    //
    // Two sections are special-cased:
    //   .init  — already decommitted by DecommitInitSection, skip it here.
    //   .vault — stays PAGE_EXECUTE_READ even though its characteristics say
    //     writable: the payload re-encrypts it at rest and flips protection
    //     itself around each call, so it must never be left RWX.
    void ApplySectionProtections(HANDLE process, void* remoteBase, const PeInfo& pe)
    {
        auto protect = [&](uint32_t rva, uint32_t size, DWORD prot) {
            if (!size) return;
            DWORD oldProt = 0;
            VirtualProtectEx(process,
                             reinterpret_cast<uint8_t*>(remoteBase) + rva,
                             size, prot, &oldProt);
        };

        DecommitRange(process, remoteBase, 0, pe.sizeOfHeaders);

        for (uint16_t i = 0; i < pe.sectionCount; ++i) {
            const auto& s = pe.sections[i];
            uint32_t size = s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;
            if (!size) continue;

            char name[9]{};
            memcpy(name, s.Name, 8);
            if (strcmp(name, ".init") == 0 ||
                strcmp(name, ".reloc") == 0 ||
                strcmp(name, ".rsrc") == 0)
                continue;

            const DWORD c = s.Characteristics;
            const bool x = (c & IMAGE_SCN_MEM_EXECUTE) != 0;
            const bool r = (c & IMAGE_SCN_MEM_READ) != 0;
            const bool w = (c & IMAGE_SCN_MEM_WRITE) != 0;

            DWORD prot = PAGE_NOACCESS;
            if (strcmp(name, ".vault") == 0)
                prot = PAGE_EXECUTE_READ;
            else if (x) prot = r ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
            else if (w) prot = PAGE_READWRITE;
            else if (r) prot = PAGE_READONLY;

            protect(s.VirtualAddress, size, prot);
        }
    }

    std::vector<uintptr_t> CollectTlsCallbacks(
        uint8_t* localImage,
        const PeInfo& pe,
        uintptr_t remoteBase)
    {
        std::vector<uintptr_t> result;
        const auto& directory =
            pe.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
        if (!directory.VirtualAddress)
            return result;
        auto* tls = reinterpret_cast<IMAGE_TLS_DIRECTORY64*>(
            RvaToLocal(localImage, pe, directory.VirtualAddress));
        if (!tls || !tls->AddressOfCallBacks)
            return result;

        const auto callbacksVa =
            static_cast<uintptr_t>(tls->AddressOfCallBacks);
        uint32_t callbacksRva = 0;
        if (callbacksVa >= remoteBase &&
            callbacksVa < remoteBase + pe.sizeOfImage) {
            callbacksRva =
                static_cast<uint32_t>(callbacksVa - remoteBase);
        } else if (callbacksVa >= pe.imageBase &&
                   callbacksVa < pe.imageBase + pe.sizeOfImage) {
            callbacksRva =
                static_cast<uint32_t>(callbacksVa - pe.imageBase);
        } else {
            return result;
        }

        auto* callbacks = reinterpret_cast<uint64_t*>(
            RvaToLocal(localImage, pe, callbacksRva));
        if (!callbacks)
            return result;
        for (std::size_t index = 0;
             index < 64 && callbacks[index];
             ++index) {
            const auto callback =
                static_cast<uintptr_t>(callbacks[index]);
            if (callback >= remoteBase &&
                callback < remoteBase + pe.sizeOfImage) {
                result.push_back(callback);
            }
        }
        return result;
    }

    // One remote start runs every TLS callback and then the protected PE entry.
    // VMProtect may add TLS initialisation; skipping it leaves the VM runtime
    // half-initialised and the first virtualised function faults.
    std::vector<uint8_t> BuildEntryStub(
        uintptr_t base,
        const std::vector<uintptr_t>& tlsCallbacks,
        uintptr_t entry)
    {
        std::vector<uint8_t> stub;
        stub.reserve(32 + (tlsCallbacks.size() + 1) * 32);
        auto byte = [&](uint8_t value) { stub.push_back(value); };
        auto value32 = [&](uint32_t value) {
            const auto* data = reinterpret_cast<const uint8_t*>(&value);
            stub.insert(stub.end(), data, data + sizeof(value));
        };
        auto value64 = [&](uint64_t value) {
            const auto* data = reinterpret_cast<const uint8_t*>(&value);
            stub.insert(stub.end(), data, data + sizeof(value));
        };

        // shadow space + x64 call alignment
        byte(0x48); byte(0x83); byte(0xEC); byte(0x28);
        auto emitCall = [&](uintptr_t target) {
            // mov rcx, base
            byte(0x48); byte(0xB9); value64(base);
            // mov edx, DLL_PROCESS_ATTACH
            byte(0xBA); value32(DLL_PROCESS_ATTACH);
            // xor r8d, r8d
            byte(0x45); byte(0x31); byte(0xC0);
            // mov rax, target; call rax
            byte(0x48); byte(0xB8); value64(target);
            byte(0xFF); byte(0xD0);
        };
        for (const auto callback : tlsCallbacks)
            emitCall(callback);
        emitCall(entry);
        byte(0x48); byte(0x83); byte(0xC4); byte(0x28);
        byte(0xC3);
        return stub;
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
    bool wipe = false;
    const wchar_t* dllPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--watch") == 0)
            watch = true;
        else if (_wcsicmp(argv[i], L"--wipe") == 0)
            wipe = true;
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
    ZeroOnExit imageWiper{image};
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

    const auto tlsCallbacks = CollectTlsCallbacks(
        image.data(),
        pe,
        reinterpret_cast<uintptr_t>(remoteBase));
    if (!tlsCallbacks.empty())
        Log("running %zu TLS callback(s)", tlsCallbacks.size());

    const auto stub = BuildEntryStub(
        reinterpret_cast<uintptr_t>(remoteBase),
        tlsCallbacks,
        reinterpret_cast<uintptr_t>(remoteBase) + pe.entryRva);
    void* remoteStub = VirtualAllocEx(
        process, nullptr, stub.size(), MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    if (!remoteStub ||
        !WriteProcessMemory(
            process,
            remoteStub,
            stub.data(),
            stub.size(),
            nullptr)) {
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

    // VMProtect's entry wrapper has returned, so its header page can now be
    // removed safely. Decommit dead startup/reconstruction metadata and then
    // tighten every remaining section to its final protection.
    if (exitCode == 1) {
        DecommitSection(process, remoteBase, pe, ".init");
        DecommitSection(process, remoteBase, pe, ".reloc");
        DecommitSection(process, remoteBase, pe, ".rsrc");
        ApplySectionProtections(process, remoteBase, pe);
    }

    CloseHandle(process);

    if (wipe && exitCode == 1) {
        if (WipeFileFromDisk(dllPath))
            Log("payload wiped from disk: %ls", dllPath);
        else
            Log("WARNING: payload wipe failed for %ls: %lu", dllPath,
                GetLastError());
    }

    Log("done. Check C:\\RAGEMP\\cheat_log.txt and press F4 in game.");
    return 0;
}
