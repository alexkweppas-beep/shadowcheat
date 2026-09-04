#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>

// Exact profile for the private-server RAGE Multiplayer client build that was
// inspected on 2026-08-06. Runtime subsystems must fail closed when this
// identity changes; raw V8 RVAs are never accepted on an unknown image.
namespace RageMpBuildProfile
{
    inline constexpr wchar_t kModuleName[] = L"multiplayer.dll";
    inline constexpr char kClientRevision[] = "396fe4b";
    inline constexpr char kFileVersion[] = "1.1.0.0";
    inline constexpr char kExpectedV8Version[] = "8.7.220.9";

    inline constexpr std::uint64_t kFileSize = 38'412'088ULL;
    inline constexpr std::uint32_t kPeTimestamp = 0x6A6F9E5FU;
    inline constexpr std::uint32_t kImageSize = 0x05263000U;
    inline constexpr char kSha256[] =
        "61517a0ab02694bc94f664d21258778d"
        "1d99f4a8ac18463f3b503244c8d20482";

    struct PeIdentity final
    {
        std::uint32_t timestamp{};
        std::uint32_t imageSize{};
        std::uint16_t machine{};
    };

    inline bool ReadLoadedIdentity(
        HMODULE module,
        PeIdentity& identity) noexcept
    {
        identity = {};
        if (!module)
            return false;

        __try {
            const auto* base = reinterpret_cast<const std::uint8_t*>(module);
            const auto* dos =
                reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
                dos->e_lfanew <= 0)
                return false;

            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + static_cast<std::size_t>(dos->e_lfanew));
            if (nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                return false;

            identity.timestamp = nt->FileHeader.TimeDateStamp;
            identity.imageSize = nt->OptionalHeader.SizeOfImage;
            identity.machine = nt->FileHeader.Machine;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            identity = {};
            return false;
        }
    }

    inline bool MatchesLoadedModule(HMODULE module) noexcept
    {
        PeIdentity identity{};
        return ReadLoadedIdentity(module, identity) &&
            identity.machine == IMAGE_FILE_MACHINE_AMD64 &&
            identity.timestamp == kPeTimestamp &&
            identity.imageSize == kImageSize;
    }
}
