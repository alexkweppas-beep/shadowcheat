#include "security/xorstr.hpp"
#include "../headers/includes.h"
#include <wrl/client.h>
#include <d3d11.h>
#include <vector>
#include <fstream>
#include <filesystem>

// читаем файл по std::filesystem::path (работает с кириллицей в путях)
static bool read_file_bytes(const std::filesystem::path& p, std::vector<unsigned char>& out, std::wstring& err)
{
    err.clear();
    out.clear();

    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec)
    {
        return false;
    }

    auto sz = std::filesystem::file_size(p, ec);
    if (ec)
    {
        return false;
    }

    if (sz == 0 || sz > (50ull * 1024ull * 1024ull))
    {
        return false;
    }

    std::ifstream f(p, std::ios::binary);
    if (!f)
    {
        return false;
    }

    out.resize((size_t)sz);
    f.read(reinterpret_cast<char*>(out.data()), (std::streamsize)sz);
    if (!f)
    {
        out.clear();
        return false;
    }

    return true;
}

static bool LoadTextureFromBytesSafe(
    const unsigned char* bytes,
    int bytes_len,
    ID3D11Device* device,
    ID3D11ShaderResourceView** out_srv,
    int* out_w = nullptr,
    int* out_h = nullptr)
{
    if (!device || !out_srv || !bytes || bytes_len <= 0) return false;
    *out_srv = nullptr;

    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load_from_memory(bytes, bytes_len, &w, &h, &n, 4);
    if (!data) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = data;
    sub.SysMemPitch = (UINT)(w * 4);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = device->CreateTexture2D(&desc, &sub, tex.GetAddressOf());
    stbi_image_free(data);
    if (FAILED(hr) || !tex) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(tex.Get(), &srv, out_srv);
    if (FAILED(hr) || !(*out_srv)) return false;

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return true;
}

void c_widgets::load_texture(std::string path, ImTextureID* img)
{
    if (!var)
    {
        return;
    }

    if (!var->dx.device)
    {
        return;
    }

    size_t ok = 0, fail = 0;

    if (path.empty())
        return;

    std::vector<unsigned char> bytes;
    std::wstring ferr;
    if (!read_file_bytes(path, bytes, ferr))
    {
        // Try parent directory (e.g. if running from Release/)
        if (!read_file_bytes(xorstr_("..\\") + path, bytes, ferr))
        {
            return;
        }
    }

    if (*img)
    {
        *img = nullptr;
    }

    ID3D11ShaderResourceView* new_tex = nullptr;
    int w = 0, h = 0;

    bool ret = LoadTextureFromBytesSafe(bytes.data(), (int)bytes.size(), var->dx.device, &new_tex, &w, &h);
    if (!ret || !new_tex)
    {
        return;
    }

    *img = new_tex;
}
