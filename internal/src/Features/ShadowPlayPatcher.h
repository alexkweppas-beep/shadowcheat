#pragma once

#include <windows.h>
#include <string>
#include <string_view>
#include <vector>
#include <set>
#include <optional>
#include <cstdint>
#include <cstring>

namespace patcher {

namespace patterns {
    constexpr std::string_view affinity_callback = "48 89 5C 24 08 57 48 83 EC 20 48 8B FA C7 44 24 38 00 00 00 00 48 8B D9";
    constexpr std::string_view nvfbc_bprotected_check = "41 39 5C 24 20 74 ? C7 05 ? ? ? ? FC FF FF FF";
    constexpr std::string_view capcore_capture_protected_error = "83 FE FC 75 ? C7 05 ? ? ? ? 07 00 00 00 83 3D";
    constexpr std::string_view capcore_h264_capture_protected_error = "83 FF FC 75 ? C7 05 ? ? ? ? 07 00 00 00 83 3D";
    constexpr std::string_view capcore_capture_frame_status = "83 FE 0A BB FC FF FF FF 41 0F 45 DC";
    constexpr std::string_view capcore_create_core_setup = "45 32 E4 BD 01 00 00 00 8B 01 3D ? ? ? ? 72 ? 44 0F B6 ? ? ? ? ? 3D ? ? ? ? 72 ? 8B";
    constexpr std::string_view capcore_init_video_capture_check = "80 BF 28 21 00 00 00 74 ? 48 83 BF 20 20 00 00 00";
    constexpr std::string_view nvspcaps_session_provider_setup = "C1 E9 05 80 E1 01 8B 40 48 89 44 24 ? 88 4C 24 ?";
    constexpr std::string_view nvspcaps_dda_resolution_clamp = "E8 ? ? ? ? 84 C0 74 04 8B CB EB 50 80 BF 41 35 00 00";
    constexpr std::string_view nvspcaps_fps_clamp = "83 F8 06 75 ? 8B 87 00 1B 00 00 85 C0";
    constexpr std::string_view nvspcaps_max_dims_init = "BB 80 07 00 00 4C 89 BC 24 ? ? ? ? 44 8B BF ? ? ? ? 41 BE 38 04 00 00";
    constexpr std::string_view nvspcaps_override_width_clamp = "8B 87 ? ? ? ? 8B C8 89 87 ? ? ? ? 8B 87 ? ? ? ? 44 3B";
    constexpr std::string_view nvspcaps_override_height_clamp = "44 8B B7 ? ? ? ? 4C 8B BC 24 ? ? ? ? 44 89 B7";
    constexpr std::string_view nvspcaps_skip_height_downscale = "41 3B F6 76 ? 0F 57 D2 41 8B C6";
    constexpr std::string_view nvspcaps_skip_width_downscale = "3B E9 76 ? 0F 57 D2 8B C1";
}

namespace opcodes {
    constexpr uint8_t mov_eax_1_ret[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };
    constexpr uint8_t xor_ebx_ebx_and_nops[] = { 0x31, 0xDB, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    constexpr uint8_t nop_pair[] = { 0x90, 0x90 };
    constexpr uint8_t force_new_capture_dda_core[] = {
        0x41, 0xB4, 0x01,
        0xBD, 0x02, 0x00, 0x00, 0x00,
        0xEB, 0x1C,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90
    };
    constexpr uint8_t force_nvspcaps_provider_setup[] = {
        0xC7, 0x44, 0x24, 0x28, 0x02, 0x00, 0x00, 0x00,
        0xC6, 0x44, 0x24, 0x20, 0x01,
        0x90, 0x90, 0x90, 0x90
    };
    constexpr uint8_t nvfbc_restore_and_continue[] = {
        0x41, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00,
        0x41, 0x83, 0x3C, 0x24, 0x00,
        0x75, 0x07,
        0x8B, 0x46, 0x1C,
        0x41, 0x89, 0x04, 0x24,
        0x41, 0x83, 0x7C, 0x24, 0x04, 0x00,
        0x75, 0x08,
        0x8B, 0x46, 0x20,
        0x41, 0x89, 0x44, 0x24, 0x04,
        0x90, 0x90
    };
    constexpr uint8_t bypass_dda_resolution_clamp[] = {
        0xEB, 0x0B,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    constexpr uint8_t bypass_fps_clamp_jump[] = {
        0xEB, 0x52
    };
    constexpr uint8_t mov_ebx_2560[] = { 0xBB, 0x00, 0x0A, 0x00, 0x00 };
    constexpr uint8_t mov_r14d_1440[] = { 0x41, 0xBE, 0xA0, 0x05, 0x00, 0x00 };
    constexpr uint8_t mov_r14d_1440_nop[] = { 0x41, 0xBE, 0xA0, 0x05, 0x00, 0x00, 0x90 };
    constexpr uint8_t six_nops[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    constexpr uint8_t jmp_short = 0xEB;
}

namespace offsets {
    constexpr size_t capcore_jump_instruction_offset = 3;
    constexpr size_t capcore_frame_status_offset = 3;
    constexpr size_t capcore_init_video_jump_offset = 7;
    constexpr size_t nvspcaps_fps_clamp_offset = 3;
    constexpr size_t nvspcaps_max_dims_height_offset = 21;
    constexpr size_t nvspcaps_override_width_offset = 8;
    constexpr size_t nvspcaps_skip_height_downscale_offset = 3;
    constexpr size_t nvspcaps_skip_width_downscale_offset = 2;
}

namespace registry_config {
    constexpr const wchar_t* subkey_path = L"SOFTWARE\\NVIDIA Corporation\\Global\\NvApp\\ShadowPlay\\FTS";
    constexpr const wchar_t* value_name = L"{497B8458-4244-4EE6-BFEA-F3D2BA294F21}";
    constexpr DWORD dda_force_value = 0x24;

    constexpr const wchar_t* nvspcaps_user_path = L"Software\\NVIDIA Corporation\\Global\\ShadowPlay\\NVSPCAPS";
    constexpr const wchar_t* resolution_preset_guid = L"0f0334de-a7a7-4ed4-b10c-3183fbefe3f3";
    constexpr const wchar_t* recording_fps_guid = L"0ec8bedc-956e-4d84-862c-b96a02ae2e2e";
    constexpr const wchar_t* bitrate_guid = L"83deb5b7-995f-4c51-bb2c-eee05a774062";
    constexpr const wchar_t* video_width_name = L"VideoWidth";
    constexpr const wchar_t* video_height_name = L"VideoHeight";
    constexpr const wchar_t* recording_fps_name = L"RecordingFPS";
    constexpr const wchar_t* default_bitrate_name = L"DefaultBitrate";
    constexpr const wchar_t* adaptive_bitrate_name = L"m7Yhs4x6";
    constexpr DWORD high_quality_bitrate = 50000000;
    constexpr float target_framerate = 60.0f;
}

enum class PatchStatus {
    success,
    already_patched,
    failed
};

struct TargetModule {
    HMODULE base_address{ nullptr };
    size_t image_size{ 0 };
    std::wstring module_name;
};

struct PatchResult {
    DWORD process_id{ 0 };
    std::wstring process_name;
    std::wstring module_name;
    std::string function_name;
    uintptr_t target_va{ 0 };
    uintptr_t target_rva{ 0 };
    PatchStatus status{ PatchStatus::failed };
};

class Patcher {
public:
    Patcher() = default;
    ~Patcher() = default;

    Patcher(const Patcher&) = delete;
    Patcher& operator=(const Patcher&) = delete;

    std::vector<PatchResult> patch_all_targets();
    std::vector<PatchResult> patch_process_modules(DWORD process_id, const std::wstring& process_name);

private:
    std::set<std::pair<DWORD, uintptr_t>> patched_locations;

    static bool enable_debug_privilege();
    static bool configure_shadowplay_registry();
    static std::vector<std::optional<uint8_t>> parse_pattern(std::string_view pattern);
    static std::vector<std::pair<DWORD, std::wstring>> find_target_processes();
    static std::vector<TargetModule> find_target_modules(HANDLE process_handle);
    static std::vector<uintptr_t> scan_process_memory_all(
        HANDLE process_handle,
        uintptr_t base,
        size_t size,
        std::string_view pattern
    );
    static std::optional<uintptr_t> scan_process_memory(
        HANDLE process_handle,
        uintptr_t base,
        size_t size,
        std::string_view pattern
    );
    static bool write_memory_safe(HANDLE process_handle, uintptr_t address, const void* data, size_t size);

    void patch_nvspcap(HANDLE process_handle, const TargetModule& mod, DWORD process_id, const std::wstring& process_name, std::vector<PatchResult>& results);
    void patch_nvfbc(HANDLE process_handle, const TargetModule& mod, DWORD process_id, const std::wstring& process_name, std::vector<PatchResult>& results);
    void patch_capcore(HANDLE process_handle, const TargetModule& mod, DWORD process_id, const std::wstring& process_name, std::vector<PatchResult>& results);
};

}
