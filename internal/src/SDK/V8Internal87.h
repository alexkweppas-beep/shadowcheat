#pragma once

#include <cstddef>
#include <cstdint>

// Pinned to V8 8.7.220.9 (RAGE MP rev 396fe4b).
// This build does not use pointer compression. Every accessor must still
// validate the instance type before following the next tagged field.
namespace V8Internal87
{
    inline constexpr std::uintptr_t kHeapObjectTag = 1;

    inline constexpr std::uint16_t kForeignType = 0x46;
    inline constexpr std::uint16_t kFunctionTemplateInfoType = 0x4F;
    inline constexpr std::uint16_t kCallHandlerInfoType = 0x5D;
    inline constexpr std::uint16_t kSharedFunctionInfoType = 0xAF;
    inline constexpr std::uint16_t kJsFunctionType = 0x444;

    inline constexpr std::size_t kMapOffset = 0x00;
    inline constexpr std::size_t kMapInstanceTypeOffset = 0x0B;

    inline constexpr std::size_t kJsFunctionSharedOffset = 0x18;
    inline constexpr std::size_t kSharedFunctionDataOffset = 0x08;
    inline constexpr std::size_t kFunctionTemplateCallCodeOffset = 0x30;
    inline constexpr std::size_t kCallHandlerCallbackOffset = 0x08;
    inline constexpr std::size_t kForeignAddressOffset = 0x08;

    inline constexpr std::uint16_t kFirstNonStringType = 0x40;
    inline constexpr std::uint16_t kHeapNumberType = 0x42;
    inline constexpr std::uint16_t kOddballType = 0x43;
    inline constexpr std::uint16_t kFixedArrayType = 0x75;
    inline constexpr std::uint16_t kJsArrayType = 0x423;

    inline bool IsHeapObject(std::uintptr_t tagged) noexcept
    {
        return (tagged & kHeapObjectTag) != 0;
    }

    inline std::uintptr_t Untag(std::uintptr_t tagged) noexcept
    {
        return tagged - kHeapObjectTag;
    }

    inline std::uintptr_t NormalizeMaybeObject(
        std::uintptr_t tagged) noexcept
    {
        // Weak heap references use tag 0b11; normalize to the regular
        // heap-object tag 0b01 before instance-type validation.
        return (tagged & 0x3U) == 0x3U
            ? tagged - 0x2U : tagged;
    }
}
