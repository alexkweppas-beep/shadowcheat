#include "MenuTexture.h"
#include "WeaponIconBlob.h"
#include "asset_silhouette.h"

#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <winhttp.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "winhttp.lib")

namespace MenuTexture
{
namespace
{
    constexpr int kMaxCached = 128;
    constexpr int kMaxIndex = 128;
    constexpr wchar_t kHost[] = L"oeefhdljyfrcjanbzzxo.supabase.co";
    constexpr wchar_t kPackPath[] =
        L"/storage/v1/object/public/public-assets/weapon-icons/weapon-icons.pack";

    struct CacheEntry final
    {
        char name[64]{};
        ID3D11ShaderResourceView* srv{nullptr};
        unsigned width{0};
        unsigned height{0};
    };

    struct OwnedEntry final
    {
        char name[64]{};
        WeaponIconBlob::Entry entry{};
    };

    ID3D11Device* g_Device = nullptr;
    CacheEntry g_Cache[kMaxCached]{};
    int g_CacheCount = 0;
    bool g_ComInit = false;

    unsigned char* g_PackBytes = nullptr;
    std::size_t g_PackBytesSize = 0;
    unsigned char* g_Blob = nullptr;
    std::size_t g_BlobSize = 0;
    OwnedEntry g_OwnedIndex[kMaxIndex]{};
    WeaponIconBlob::Entry g_IndexView[kMaxIndex]{};
    int g_IndexCount = 0;

    enum class PackState : int
    {
        Idle = 0,
        Loading,
        Ready,
        Failed
    };
    volatile LONG g_PackState = static_cast<LONG>(PackState::Idle);
    HANDLE g_PackThread = nullptr;

    CacheEntry* FindCache(const char* name) noexcept
    {
        if (!name || !name[0])
            return nullptr;
        for (int i = 0; i < g_CacheCount; ++i)
        {
            if (_stricmp(g_Cache[i].name, name) == 0)
                return &g_Cache[i];
        }
        return nullptr;
    }

    bool ParsePack(const unsigned char* data, std::size_t size) noexcept
    {
        if (!data || size < 12)
            return false;
        if (data[0] != 'W' || data[1] != 'I' || data[2] != 'C' || data[3] != 'O')
            return false;
        const std::uint32_t version =
            static_cast<std::uint32_t>(data[4]) |
            (static_cast<std::uint32_t>(data[5]) << 8) |
            (static_cast<std::uint32_t>(data[6]) << 16) |
            (static_cast<std::uint32_t>(data[7]) << 24);
        const std::uint32_t count =
            static_cast<std::uint32_t>(data[8]) |
            (static_cast<std::uint32_t>(data[9]) << 8) |
            (static_cast<std::uint32_t>(data[10]) << 16) |
            (static_cast<std::uint32_t>(data[11]) << 24);
        if (version != 1 || count == 0 || count > kMaxIndex)
            return false;

        std::size_t cursor = 12;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (cursor + 2 > size)
                return false;
            const std::uint16_t nameLen =
                static_cast<std::uint16_t>(data[cursor]) |
                (static_cast<std::uint16_t>(data[cursor + 1]) << 8);
            cursor += 2;
            if (nameLen == 0 || nameLen >= sizeof(g_OwnedIndex[0].name) ||
                cursor + nameLen + 8 > size)
                return false;
            memcpy(g_OwnedIndex[i].name, data + cursor, nameLen);
            g_OwnedIndex[i].name[nameLen] = 0;
            cursor += nameLen;
            const std::uint32_t offset =
                static_cast<std::uint32_t>(data[cursor]) |
                (static_cast<std::uint32_t>(data[cursor + 1]) << 8) |
                (static_cast<std::uint32_t>(data[cursor + 2]) << 16) |
                (static_cast<std::uint32_t>(data[cursor + 3]) << 24);
            const std::uint32_t entrySize =
                static_cast<std::uint32_t>(data[cursor + 4]) |
                (static_cast<std::uint32_t>(data[cursor + 5]) << 8) |
                (static_cast<std::uint32_t>(data[cursor + 6]) << 16) |
                (static_cast<std::uint32_t>(data[cursor + 7]) << 24);
            cursor += 8;
            g_OwnedIndex[i].entry.name = g_OwnedIndex[i].name;
            g_OwnedIndex[i].entry.offset = offset;
            g_OwnedIndex[i].entry.size = entrySize;
            g_IndexView[i] = g_OwnedIndex[i].entry;
        }

        const std::size_t blobOffset = cursor;
        if (blobOffset >= size)
            return false;
        g_Blob = const_cast<unsigned char*>(data + blobOffset);
        g_BlobSize = size - blobOffset;
        g_IndexCount = static_cast<int>(count);

        for (int i = 0; i < g_IndexCount; ++i)
        {
            const auto& e = g_IndexView[i];
            if (static_cast<std::size_t>(e.offset) + e.size > g_BlobSize)
                return false;
        }

        WeaponIconBlob::SetPack(
            g_Blob,
            g_BlobSize,
            g_IndexView,
            g_IndexCount);
        return true;
    }

    bool DownloadPack() noexcept
    {
        HINTERNET session = WinHttpOpen(
            L"Butterfly-Assets/1",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (!session)
            return false;
        WinHttpSetTimeouts(session, 4000, 4000, 8000, 15000);
        HINTERNET connection = WinHttpConnect(
            session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connection)
        {
            WinHttpCloseHandle(session);
            return false;
        }
        HINTERNET request = WinHttpOpenRequest(
            connection,
            L"GET",
            kPackPath,
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!request)
        {
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }

        BOOL ok = WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0);
        if (ok)
            ok = WinHttpReceiveResponse(request, nullptr);

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (ok)
        {
            WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &status,
                &statusSize,
                WINHTTP_NO_HEADER_INDEX);
            ok = (status == 200);
        }

        unsigned char* buffer = nullptr;
        std::size_t used = 0;
        std::size_t capacity = 0;
        while (ok)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available))
            {
                ok = FALSE;
                break;
            }
            if (!available)
                break;
            if (used + available > 8 * 1024 * 1024)
            {
                ok = FALSE;
                break;
            }
            if (used + available > capacity)
            {
                const std::size_t next =
                    capacity == 0 ? 1024 * 1024 : capacity * 2;
                const std::size_t need =
                    used + available > next ? used + available : next;
                unsigned char* grown = nullptr;
                if (!buffer)
                {
                    grown = static_cast<unsigned char*>(
                        HeapAlloc(GetProcessHeap(), 0, need));
                }
                else
                {
                    grown = static_cast<unsigned char*>(
                        HeapReAlloc(
                            GetProcessHeap(),
                            0,
                            buffer,
                            need));
                }
                if (!grown)
                {
                    ok = FALSE;
                    break;
                }
                buffer = grown;
                capacity = need;
            }
            DWORD read = 0;
            if (!WinHttpReadData(
                    request,
                    buffer + used,
                    available,
                    &read))
            {
                ok = FALSE;
                break;
            }
            used += read;
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        if (!ok || !buffer || used < 12)
        {
            if (buffer)
                HeapFree(GetProcessHeap(), 0, buffer);
            return false;
        }

        if (!ParsePack(buffer, used))
        {
            HeapFree(GetProcessHeap(), 0, buffer);
            WeaponIconBlob::ClearPack();
            g_Blob = nullptr;
            g_BlobSize = 0;
            g_IndexCount = 0;
            return false;
        }

        g_PackBytes = buffer;
        g_PackBytesSize = used;
        return true;
    }

    DWORD WINAPI PackThreadProc(LPVOID) noexcept
    {
        const bool ok = DownloadPack();
        InterlockedExchange(
            &g_PackState,
            static_cast<LONG>(
                ok ? PackState::Ready : PackState::Failed));
        return 0;
    }

    void StartPackDownload() noexcept
    {
        const LONG prev = InterlockedCompareExchange(
            &g_PackState,
            static_cast<LONG>(PackState::Loading),
            static_cast<LONG>(PackState::Idle));
        if (prev != static_cast<LONG>(PackState::Idle))
            return;
        g_PackThread = CreateThread(
            nullptr, 0, PackThreadProc, nullptr, 0, nullptr);
        if (!g_PackThread)
        {
            InterlockedExchange(
                &g_PackState,
                static_cast<LONG>(PackState::Failed));
        }
    }

    bool LoadPngMemory(
        const unsigned char* data,
        std::uint32_t size,
        ID3D11ShaderResourceView** outSrv,
        unsigned* outWidth = nullptr,
        unsigned* outHeight = nullptr) noexcept
    {
        if (!g_Device || !data || size == 0 || !outSrv)
            return false;
        *outSrv = nullptr;
        if (outWidth)
            *outWidth = 0;
        if (outHeight)
            *outHeight = 0;

        IWICImagingFactory* factory = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory)
            return false;

        IWICStream* stream = nullptr;
        hr = factory->CreateStream(&stream);
        if (FAILED(hr) || !stream)
        {
            factory->Release();
            return false;
        }

        hr = stream->InitializeFromMemory(
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(data)),
            size);
        if (FAILED(hr))
        {
            stream->Release();
            factory->Release();
            return false;
        }

        IWICBitmapDecoder* decoder = nullptr;
        hr = factory->CreateDecoderFromStream(
            stream,
            nullptr,
            WICDecodeMetadataCacheOnLoad,
            &decoder);
        stream->Release();
        if (FAILED(hr) || !decoder)
        {
            factory->Release();
            return false;
        }

        IWICBitmapFrameDecode* frame = nullptr;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr) || !frame)
        {
            decoder->Release();
            factory->Release();
            return false;
        }

        IWICFormatConverter* converter = nullptr;
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr) || !converter)
        {
            frame->Release();
            decoder->Release();
            factory->Release();
            return false;
        }

        hr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            converter->Release();
            frame->Release();
            decoder->Release();
            factory->Release();
            return false;
        }

        UINT width = 0;
        UINT height = 0;
        converter->GetSize(&width, &height);
        if (width == 0 || height == 0 || width > 2048 || height > 2048)
        {
            converter->Release();
            frame->Release();
            decoder->Release();
            factory->Release();
            return false;
        }

        const UINT stride = width * 4;
        const UINT bytes = stride * height;
        BYTE* pixels = static_cast<BYTE*>(
            HeapAlloc(GetProcessHeap(), 0, bytes));
        if (!pixels)
        {
            converter->Release();
            frame->Release();
            decoder->Release();
            factory->Release();
            return false;
        }

        hr = converter->CopyPixels(nullptr, stride, bytes, pixels);
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        if (FAILED(hr))
        {
            HeapFree(GetProcessHeap(), 0, pixels);
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub{};
        sub.pSysMem = pixels;
        sub.SysMemPitch = stride;

        ID3D11Texture2D* tex = nullptr;
        hr = g_Device->CreateTexture2D(&desc, &sub, &tex);
        HeapFree(GetProcessHeap(), 0, pixels);
        if (FAILED(hr) || !tex)
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = g_Device->CreateShaderResourceView(tex, &srvDesc, outSrv);
        tex->Release();
        if (FAILED(hr) || !*outSrv)
            return false;
        if (outWidth)
            *outWidth = width;
        if (outHeight)
            *outHeight = height;
        return true;
    }
}

void Initialize(ID3D11Device* device) noexcept
{
    g_Device = device;
    if (!g_ComInit)
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE)
            g_ComInit = true;
    }
    StartPackDownload();
}

void Shutdown() noexcept
{
    for (int i = 0; i < g_CacheCount; ++i)
    {
        if (g_Cache[i].srv)
        {
            g_Cache[i].srv->Release();
            g_Cache[i].srv = nullptr;
        }
        g_Cache[i].name[0] = 0;
    }
    g_CacheCount = 0;
    g_Device = nullptr;

    if (g_PackThread)
    {
        WaitForSingleObject(g_PackThread, 5000);
        CloseHandle(g_PackThread);
        g_PackThread = nullptr;
    }
    WeaponIconBlob::ClearPack();
    if (g_PackBytes)
    {
        HeapFree(GetProcessHeap(), 0, g_PackBytes);
        g_PackBytes = nullptr;
    }
    g_PackBytesSize = 0;
    g_Blob = nullptr;
    g_BlobSize = 0;
    g_IndexCount = 0;
    InterlockedExchange(
        &g_PackState,
        static_cast<LONG>(PackState::Idle));
}

// Decoding a PNG and creating a texture happens inside the draw call, so a
// crowd that brings a dozen unseen weapons into view used to stall a single
// frame with a dozen decodes. One admission per short window spreads them out;
// until an icon is ready the overlay draws its font glyph instead.
static DWORD g_LastIconDecodeTick = 0;

ID3D11ShaderResourceView* GetWeaponIcon(const char* iconFile) noexcept
{
    if (!iconFile || !iconFile[0] || !g_Device)
        return nullptr;
    if (CacheEntry* hit = FindCache(iconFile))
        return hit->srv;
    if (!WeaponIconBlob::IsReady())
    {
        StartPackDownload();
        return nullptr;
    }
    if (g_CacheCount >= kMaxCached)
        return nullptr;
    const DWORD nowTick = GetTickCount();
    if (g_LastIconDecodeTick &&
        nowTick - g_LastIconDecodeTick < 8)
        return nullptr;
    g_LastIconDecodeTick = nowTick;

    const WeaponIconBlob::Entry* entry = WeaponIconBlob::Find(iconFile);
    if (!entry)
        return nullptr;
    const unsigned char* bytes = WeaponIconBlob::Bytes(*entry);
    if (!bytes)
        return nullptr;

    ID3D11ShaderResourceView* srv = nullptr;
    unsigned texW = 0;
    unsigned texH = 0;
    if (!LoadPngMemory(bytes, entry->size, &srv, &texW, &texH) || !srv)
        return nullptr;

    CacheEntry& slot = g_Cache[g_CacheCount++];
    lstrcpynA(slot.name, iconFile, static_cast<int>(sizeof(slot.name)));
    slot.srv = srv;
    slot.width = texW;
    slot.height = texH;
    return srv;
}

void* GetWeaponIconImGui(const char* iconFile) noexcept
{
    return reinterpret_cast<void*>(GetWeaponIcon(iconFile));
}

void* GetSilhouetteImGui(
    unsigned* outWidth,
    unsigned* outHeight) noexcept
{
    // Embedded, so the stage never waits on the cloud pack.
    static ID3D11ShaderResourceView* srv = nullptr;
    static unsigned width = 0;
    static unsigned height = 0;
    static bool failed = false;

    if (!srv && !failed && g_Device)
    {
        if (!LoadPngMemory(
                SilhouetteAsset::kPng,
                SilhouetteAsset::kPngSize,
                &srv,
                &width,
                &height))
        {
            failed = true;
            srv = nullptr;
        }
    }
    if (outWidth)
        *outWidth = width;
    if (outHeight)
        *outHeight = height;
    return reinterpret_cast<void*>(srv);
}

void* GetGlowImGui() noexcept
{
    static ID3D11ShaderResourceView* srv = nullptr;
    static bool failed = false;
    if (srv || failed || !g_Device)
        return reinterpret_cast<void*>(srv);

    constexpr UINT kSize = 64;
    constexpr UINT stride = kSize * 4;
    BYTE* pixels = static_cast<BYTE*>(
        HeapAlloc(GetProcessHeap(), 0, stride * kSize));
    if (!pixels)
    {
        failed = true;
        return nullptr;
    }

    const float centre = (kSize - 1) * 0.5f;
    for (UINT y = 0; y < kSize; ++y)
    {
        for (UINT x = 0; x < kSize; ++x)
        {
            const float dx = (static_cast<float>(x) - centre) / centre;
            const float dy = (static_cast<float>(y) - centre) / centre;
            const float distance = std::sqrt(dx * dx + dy * dy);
            // Squared falloff, and clamped at the edge so the sprite ends on
            // exactly zero. A linear ramp leaves a visible disc rim once a few
            // of these are stacked; a curve that reaches zero reads as light.
            float alpha = 1.f - distance;
            alpha = alpha > 0.f ? alpha * alpha * alpha : 0.f;
            BYTE* p = pixels + y * stride + x * 4;
            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            p[3] = static_cast<BYTE>(alpha * 255.f + 0.5f);
        }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kSize;
    desc.Height = kSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = pixels;
    sub.SysMemPitch = stride;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = g_Device->CreateTexture2D(&desc, &sub, &tex);
    HeapFree(GetProcessHeap(), 0, pixels);
    if (FAILED(hr) || !tex)
    {
        failed = true;
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = g_Device->CreateShaderResourceView(tex, &srvDesc, &srv);
    tex->Release();
    if (FAILED(hr))
    {
        srv = nullptr;
        failed = true;
    }
    return reinterpret_cast<void*>(srv);
}

bool GetWeaponIconSize(
    const char* iconFile,
    unsigned* outWidth,
    unsigned* outHeight) noexcept
{
    if (!outWidth || !outHeight)
        return false;
    *outWidth = 0;
    *outHeight = 0;
    if (!GetWeaponIcon(iconFile))
        return false;
    CacheEntry* hit = FindCache(iconFile);
    if (!hit || hit->width == 0 || hit->height == 0)
        return false;
    *outWidth = hit->width;
    *outHeight = hit->height;
    return true;
}

void FitWeaponIconSize(
    const char* iconFile,
    float maxW,
    float maxH,
    float* outW,
    float* outH) noexcept
{
    if (!outW || !outH)
        return;
    // GTA weapon PNGs are typically ~2:1 landscape.
    float aspect = 2.f;
    unsigned tw = 0;
    unsigned th = 0;
    if (GetWeaponIconSize(iconFile, &tw, &th) && tw > 0 && th > 0)
        aspect = static_cast<float>(tw) / static_cast<float>(th);

    float w = maxW;
    float h = w / aspect;
    if (h > maxH)
    {
        h = maxH;
        w = h * aspect;
    }
    *outW = w;
    *outH = h;
}
}
