// compress_dll.cpp — post-build DLL compressor
//
// Uses the Windows Compression API (cabinet.dll) with XPRESS_HUFFMAN, which
// gives significantly better ratios than LZNT1 on VMP-packed binaries.
// No external dependencies — cabinet.dll is built into every Windows 8+ install.
//
// Output format:
//   [4 bytes] magic          = 'SHZP' (0x505A4853)
//   [4 bytes] original_size  = uncompressed size
//   [4 bytes] compressed_size
//   [compressed_size bytes]  = XPRESS_HUFFMAN payload
//
// Usage: compress_dll.exe <input> <output>

#include <Windows.h>
#include <compressapi.h>
#include <cstdio>
#include <vector>

#pragma comment(lib, "cabinet.lib")

static constexpr UINT32 kMagic = 0x505A4853u; // 'SHZP'

static std::vector<uint8_t> ReadAll(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    std::vector<uint8_t> buf(static_cast<size_t>(sz.QuadPart));
    DWORD rd = 0;
    ::ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &rd, nullptr);
    CloseHandle(h);
    if (rd != static_cast<DWORD>(buf.size())) return {};
    return buf;
}

static bool WriteAll(const wchar_t* path, const void* data, size_t size)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0,
                           nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    ::WriteFile(h, data, static_cast<DWORD>(size), &wr, nullptr);
    CloseHandle(h);
    return wr == static_cast<DWORD>(size);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        fwprintf(stderr, L"Usage: compress_dll.exe <input> <output>\n");
        return 1;
    }

    auto input = ReadAll(argv[1]);
    if (input.empty()) {
        fwprintf(stderr, L"[compress_dll] Cannot read: %ls\n", argv[1]);
        return 1;
    }

    // Create compressor — XPRESS_HUFFMAN gives the best ratio/speed tradeoff
    COMPRESSOR_HANDLE hComp = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &hComp)) {
        fwprintf(stderr, L"[compress_dll] CreateCompressor failed: %lu\n", GetLastError());
        return 1;
    }

    // Query compressed size first
    SIZE_T compSize = 0;
    Compress(hComp, input.data(), input.size(), nullptr, 0, &compSize);

    std::vector<uint8_t> compressed(compSize);
    if (!Compress(hComp, input.data(), input.size(),
                  compressed.data(), compSize, &compSize)) {
        fwprintf(stderr, L"[compress_dll] Compress failed: %lu\n", GetLastError());
        CloseCompressor(hComp);
        return 1;
    }
    CloseCompressor(hComp);
    compressed.resize(compSize);

    // Build output: 12-byte header + compressed payload
    struct Header {
        UINT32 magic;
        UINT32 original_size;
        UINT32 compressed_size;
    } hdr;
    hdr.magic           = kMagic;
    hdr.original_size   = static_cast<UINT32>(input.size());
    hdr.compressed_size = static_cast<UINT32>(compSize);

    std::vector<uint8_t> out(sizeof(hdr) + compSize);
    memcpy(out.data(),             &hdr,           sizeof(hdr));
    memcpy(out.data() + sizeof(hdr), compressed.data(), compSize);

    if (!WriteAll(argv[2], out.data(), out.size())) {
        fwprintf(stderr, L"[compress_dll] Cannot write: %ls\n", argv[2]);
        return 1;
    }

    const double ratio = 100.0 - (compSize * 100.0 / input.size());
    wprintf(L"[compress_dll] %ls\n"
            L"  original:   %7u bytes  (%.2f MB)\n"
            L"  compressed: %7u bytes  (%.2f MB)\n"
            L"  saved:      %.1f%%\n",
            argv[2],
            hdr.original_size,  hdr.original_size / 1048576.0,
            (UINT32)compSize,   compSize          / 1048576.0,
            ratio);
    return 0;
}
