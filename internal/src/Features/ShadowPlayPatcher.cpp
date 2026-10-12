#include "ShadowPlayPatcher.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <charconv>
#include <algorithm>

namespace patcher {

namespace process_access {
    constexpr DWORD target_flags = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION;
}

namespace module_names {
    constexpr std::wstring_view nvspcap_prefix = L"nvspcap";
    constexpr std::wstring_view underscore_nvspcap_prefix = L"_nvspcap";
    constexpr std::wstring_view nvfbc_prefix = L"nvfbc";
    constexpr std::wstring_view capcore_prefix = L"capcore";
}

bool Patcher::enable_debug_privilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    const bool success = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr);
    CloseHandle(token);
    return success && GetLastError() == ERROR_SUCCESS;
}

bool Patcher::configure_shadowplay_registry() { return true; }

std::vector<std::optional<uint8_t>> Patcher::parse_pattern(std::string_view pattern) {
    std::vector<std::optional<uint8_t>> bytes;
    size_t index = 0;
    const size_t length = pattern.length();

    while (index < length) {
        while (index < length && pattern[index] == ' ') {
            ++index;
        }
        if (index >= length) {
            break;
        }

        if (pattern[index] == '?') {
            bytes.push_back(std::nullopt);
            if (index + 1 < length && pattern[index + 1] == '?') {
                ++index;
            }
            ++index;
        } else {
            if (index + 1 < length) {
                uint8_t value = 0;
                auto [ptr, ec] = std::from_chars(&pattern[index], &pattern[index + 2], value, 16);
                if (ec == std::errc()) {
                    bytes.push_back(value);
                    index += 2;
                } else {
                    ++index;
                }
            } else {
                ++index;
            }
        }
    }

    return bytes;
}

std::vector<std::pair<DWORD, std::wstring>> Patcher::find_target_processes() {
    std::vector<std::pair<DWORD, std::wstring>> targets;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return targets;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID > 4 && entry.th32ProcessID != GetCurrentProcessId()) {
                targets.emplace_back(entry.th32ProcessID, entry.szExeFile);
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return targets;
}

std::vector<TargetModule> Patcher::find_target_modules(HANDLE process_handle) {
    std::vector<TargetModule> result;
    HMODULE modules[1024];
    DWORD needed = 0;

    if (!EnumProcessModulesEx(process_handle, modules, sizeof(modules), &needed, LIST_MODULES_ALL)) {
        return result;
    }

    const size_t module_count = needed / sizeof(HMODULE);
    for (size_t i = 0; i < module_count; ++i) {
        WCHAR module_name[MAX_PATH];
        if (GetModuleBaseNameW(process_handle, modules[i], module_name, MAX_PATH)) {
            std::wstring name = module_name;
            std::transform(name.begin(), name.end(), name.begin(), ::towlower);
            if (name.find(module_names::nvspcap_prefix) != std::wstring::npos ||
                name.find(module_names::underscore_nvspcap_prefix) != std::wstring::npos ||
                name.find(module_names::nvfbc_prefix) != std::wstring::npos ||
                name.find(module_names::capcore_prefix) != std::wstring::npos) {
                MODULEINFO mod_info{};
                if (GetModuleInformation(process_handle, modules[i], &mod_info, sizeof(mod_info))) {
                    result.push_back(TargetModule{
                        modules[i],
                        mod_info.SizeOfImage,
                        module_name
                    });
                }
            }
        }
    }

    return result;
}

std::vector<uintptr_t> Patcher::scan_process_memory_all(
    HANDLE process_handle,
    uintptr_t base,
    size_t size,
    std::string_view pattern
) {
    std::vector<uintptr_t> matches;
    const auto pattern_bytes = parse_pattern(pattern);
    if (pattern_bytes.empty()) {
        return matches;
    }

    std::vector<uint8_t> buffer(size);
    SIZE_T bytes_read = 0;

    if (!ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(base), buffer.data(), size, &bytes_read) || bytes_read == 0) {
        return matches;
    }

    const size_t pattern_size = pattern_bytes.size();
    if (bytes_read < pattern_size) {
        return matches;
    }

    for (size_t i = 0; i <= bytes_read - pattern_size; ++i) {
        bool match = true;
        for (size_t j = 0; j < pattern_size; ++j) {
            if (pattern_bytes[j].has_value() && pattern_bytes[j].value() != buffer[i + j]) {
                match = false;
                break;
            }
        }

        if (match) {
            matches.push_back(base + i);
        }
    }

    return matches;
}

std::optional<uintptr_t> Patcher::scan_process_memory(
    HANDLE process_handle,
    uintptr_t base,
    size_t size,
    std::string_view pattern
) {
    const auto matches = scan_process_memory_all(process_handle, base, size, pattern);
    if (matches.empty()) {
        return std::nullopt;
    }
    return matches.front();
}

bool Patcher::write_memory_safe(HANDLE process_handle, uintptr_t address, const void* data, size_t size) {
    DWORD old_protect = 0;
    if (!VirtualProtectEx(process_handle, reinterpret_cast<LPVOID>(address), size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    SIZE_T written = 0;
    const bool success = WriteProcessMemory(process_handle, reinterpret_cast<LPVOID>(address), data, size, &written) && written == size;
    DWORD dummy = 0;
    VirtualProtectEx(process_handle, reinterpret_cast<LPVOID>(address), size, old_protect, &dummy);
    return success;
}

void Patcher::patch_nvspcap(HANDLE process_handle, const TargetModule& mod, DWORD process_id, const std::wstring& process_name, std::vector<PatchResult>& results) {
    const auto base_addr = reinterpret_cast<uintptr_t>(mod.base_address);

    // Only patch affinity_callback - makes ShadowPlay respect WDA_EXCLUDEFROMCAPTURE.
    // All other patches (dda_resolution_clamp, fps_clamp, max_dims, etc.) corrupt
    // the DDA pipeline causing green first frame and color distortion. DISABLED.

    const auto func_addr = scan_process_memory(process_handle, base_addr, mod.image_size, patterns::affinity_callback);
    if (func_addr.has_value() && !patched_locations.count({process_id, func_addr.value()})) {
        PatchResult res{};
        res.process_id = process_id;
        res.process_name = process_name;
        res.module_name = mod.module_name;
        res.function_name = "CWindowManager_EnumWindowsAffinityCallback";
        res.target_va = func_addr.value();
        res.target_rva = res.target_va - base_addr;

        if (write_memory_safe(process_handle, res.target_va, opcodes::mov_eax_1_ret, sizeof(opcodes::mov_eax_1_ret))) {
            res.status = PatchStatus::success;
            patched_locations.insert({process_id, res.target_va});
        }
        results.push_back(res);
    }

    // Removed session_provider_setup to allow high quality NVFBC to run instead of DDA
}

void Patcher::patch_nvfbc(HANDLE process_handle, const TargetModule& mod, DWORD process_id, const std::wstring& process_name, std::vector<PatchResult>& results) {
    const auto base_addr = reinterpret_cast<uintptr_t>(mod.base_address);

    const auto check_addr = scan_process_memory(process_handle, base_addr, mod.image_size, patterns::nvfbc_bprotected_check);
    if (check_addr.has_value() && !patched_locations.count({process_id, check_addr.value()})) {
        PatchResult res{};
        res.process_id = process_id;
        res.process_name = process_name;
        res.module_name = mod.module_name;
        res.function_name = "NvFBCCore_CheckGrabInfo_BypassProtectedAndRestoreDims";
        res.target_va = check_addr.value();
        res.target_rva = res.target_va - base_addr;

        if (write_memory_safe(process_handle, res.target_va, opcodes::nvfbc_restore_and_continue, sizeof(opcodes::nvfbc_restore_and_continue))) {
            res.status = PatchStatus::success;
            patched_locations.insert({process_id, res.target_va});
        }
        results.push_back(res);
    }
}

void Patcher::patch_capcore(HANDLE process_handle, const TargetModule& mod, DWORD process_id, const std::wstring& process_name, std::vector<PatchResult>& results) {
    const auto base_addr = reinterpret_cast<uintptr_t>(mod.base_address);

    const auto core_setup_addr = scan_process_memory(process_handle, base_addr, mod.image_size, patterns::capcore_create_core_setup);
    if (core_setup_addr.has_value() && !patched_locations.count({process_id, core_setup_addr.value()})) {
        PatchResult res{};
        res.process_id = process_id;
        res.process_name = process_name;
        res.module_name = mod.module_name;
        res.function_name = "NvCreateCaptureCore_ForceNewCaptureDDA";
        res.target_va = core_setup_addr.value();
        res.target_rva = res.target_va - base_addr;

        if (write_memory_safe(process_handle, res.target_va, opcodes::force_new_capture_dda_core, sizeof(opcodes::force_new_capture_dda_core))) {
            res.status = PatchStatus::success;
            patched_locations.insert({process_id, res.target_va});
        }
        results.push_back(res);
    }

    const auto init_check_addr = scan_process_memory(process_handle, base_addr, mod.image_size, patterns::capcore_init_video_capture_check);
    if (init_check_addr.has_value()) {
        const uintptr_t target_va = init_check_addr.value() + offsets::capcore_init_video_jump_offset;
        if (!patched_locations.count({process_id, target_va})) {
            PatchResult res{};
            res.process_id = process_id;
            res.process_name = process_name;
            res.module_name = mod.module_name;
            res.function_name = "CCaptureCore_InitVideoCapture_ForceNewCapture";
            res.target_va = target_va;
            res.target_rva = res.target_va - base_addr;

            if (write_memory_safe(process_handle, res.target_va, opcodes::nop_pair, sizeof(opcodes::nop_pair))) {
                res.status = PatchStatus::success;
                patched_locations.insert({process_id, res.target_va});
            }
            results.push_back(res);
        }
    }

    const auto fbc_status_addr = scan_process_memory(process_handle, base_addr, mod.image_size, patterns::capcore_capture_frame_status);
    if (fbc_status_addr.has_value()) {
        const uintptr_t target_va = fbc_status_addr.value() + offsets::capcore_frame_status_offset;
        if (!patched_locations.count({process_id, target_va})) {
            PatchResult res{};
            res.process_id = process_id;
            res.process_name = process_name;
            res.module_name = mod.module_name;
            res.function_name = "CVideoCaptureFbc_CaptureFrame_SuppressError";
            res.target_va = target_va;
            res.target_rva = res.target_va - base_addr;

            if (write_memory_safe(process_handle, res.target_va, opcodes::xor_ebx_ebx_and_nops, sizeof(opcodes::xor_ebx_ebx_and_nops))) {
                res.status = PatchStatus::success;
                patched_locations.insert({process_id, res.target_va});
            }
            results.push_back(res);
        }
    }

    const auto capture_err_addr = scan_process_memory(process_handle, base_addr, mod.image_size, patterns::capcore_capture_protected_error);
    if (capture_err_addr.has_value()) {
        const uintptr_t target_va = capture_err_addr.value() + offsets::capcore_jump_instruction_offset;
        if (!patched_locations.count({process_id, target_va})) {
            PatchResult res{};
            res.process_id = process_id;
            res.process_name = process_name;
            res.module_name = mod.module_name;
            res.function_name = "CVideoCapture_Capture_IgnoreProtectedContent";
            res.target_va = target_va;
            res.target_rva = res.target_va - base_addr;

            const uint8_t jmp_op = opcodes::jmp_short;
            if (write_memory_safe(process_handle, res.target_va, &jmp_op, sizeof(jmp_op))) {
                res.status = PatchStatus::success;
                patched_locations.insert({process_id, res.target_va});
            }
            results.push_back(res);
        }
    }

    const auto h264_err_addr = scan_process_memory(process_handle, base_addr, mod.image_size, patterns::capcore_h264_capture_protected_error);
    if (h264_err_addr.has_value()) {
        const uintptr_t target_va = h264_err_addr.value() + offsets::capcore_jump_instruction_offset;
        if (!patched_locations.count({process_id, target_va})) {
            PatchResult res{};
            res.process_id = process_id;
            res.process_name = process_name;
            res.module_name = mod.module_name;
            res.function_name = "H264VideoCapture_Capture_IgnoreProtectedContent";
            res.target_va = target_va;
            res.target_rva = res.target_va - base_addr;

            const uint8_t jmp_op = opcodes::jmp_short;
            if (write_memory_safe(process_handle, res.target_va, &jmp_op, sizeof(jmp_op))) {
                res.status = PatchStatus::success;
                patched_locations.insert({process_id, res.target_va});
            }
            results.push_back(res);
        }
    }
}

std::vector<PatchResult> Patcher::patch_process_modules(DWORD process_id, const std::wstring& process_name) {
    std::vector<PatchResult> results;
    HANDLE process_handle = OpenProcess(process_access::target_flags, FALSE, process_id);
    if (!process_handle) {
        return results;
    }

    const auto target_modules = find_target_modules(process_handle);
    for (const auto& mod : target_modules) {
        std::wstring lower_name = mod.module_name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::towlower);

        if (lower_name.find(module_names::nvspcap_prefix) != std::wstring::npos ||
            lower_name.find(module_names::underscore_nvspcap_prefix) != std::wstring::npos) {
            patch_nvspcap(process_handle, mod, process_id, process_name, results);
        } else if (lower_name.find(module_names::nvfbc_prefix) != std::wstring::npos) {
            patch_nvfbc(process_handle, mod, process_id, process_name, results);
        } else if (lower_name.find(module_names::capcore_prefix) != std::wstring::npos) {
            patch_capcore(process_handle, mod, process_id, process_name, results);
        }
    }

    CloseHandle(process_handle);
    return results;
}

std::vector<PatchResult> Patcher::patch_all_targets() {
    std::vector<PatchResult> results;
    enable_debug_privilege();
    configure_shadowplay_registry();
    const auto targets = find_target_processes();

    for (const auto& [pid, name] : targets) {
        const auto module_results = patch_process_modules(pid, name);
        for (const auto& res : module_results) {
            results.push_back(res);
        }
    }

    return results;
}

}

