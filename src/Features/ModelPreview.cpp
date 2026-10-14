#include "ModelPreview.h"

#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Main/Hooks.h"
#include "EspSkeleton.h"
#include "model_preview_assets.hpp"
#include "ModelPreviewMeshResources.h"

// Linker symbol for current module base (MSVC) — also used by ResolveDefaultModelPath.
extern "C" IMAGE_DOS_HEADER __ImageBase;

// The payload wipes its PE header at the end of DllMain, after which
// FindResourceW can no longer locate RCDATA in this image. DllMain primes
// this cache first; LoadDefaultAsset reads from it.
static const uint8_t* g_baked_res_data = nullptr;
static size_t         g_baked_res_size = 0;

namespace ModelPreview
{
	void CacheEmbeddedMeshResource()
	{
		const HMODULE self = reinterpret_cast<HMODULE>(&__ImageBase);
		const HRSRC res = FindResourceW(
			self, MAKEINTRESOURCEW(IDR_MODEL_PREVIEW_MESH), RT_RCDATA);
		if (!res)
			return;
		const HGLOBAL loaded = LoadResource(self, res);
		const DWORD size_dw = SizeofResource(self, res);
		const auto* bytes = static_cast<const uint8_t*>(LockResource(loaded));
		if (!bytes || !size_dw)
			return;
		g_baked_res_data = bytes;
		g_baked_res_size = size_dw;
	}
}

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "winhttp.lib")

#pragma warning(push)
#pragma warning(disable: 4996)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "../thirdparty/model_preview/stb_image.h"

#define CGLTF_IMPLEMENTATION
#include "../thirdparty/model_preview/cgltf.h"
#pragma warning(pop)

using namespace DirectX;

namespace ModelPreview
{
	struct Vertex
	{
		XMFLOAT3 pos;
		XMFLOAT3 normal;
		// UV defaults to (0, 0) so existing aggregate init `Vertex{ pos, normal }`
		// in load_test_cube / load_obj keeps working without a UV channel.
		XMFLOAT2 uv{ 0.f, 0.f };
	};

	struct CBuf
	{
		XMMATRIX mvp;
		XMMATRIX world;
		XMFLOAT4 cam_pos;
		XMFLOAT4 color;
		XMFLOAT4 params; // x = rot_y, y = rot_x
	};

	// One renderable chunk inside the loaded model. .glb meshes can have
	// multiple primitives, each with its own material / base-color texture.
	struct Primitive
	{
		UINT                       start_vertex;
		UINT                       vertex_count;
		ID3D11ShaderResourceView*  basecolor_srv; // owned, may be null -> default white
	};

	static ID3D11Device* g_device = nullptr;
	static ID3D11DeviceContext* g_ctx = nullptr;

	static ID3D11Texture2D* g_rt_tex = nullptr;
	static ID3D11RenderTargetView* g_rtv = nullptr;
	static ID3D11ShaderResourceView* g_srv = nullptr;
	static ID3D11Texture2D* g_ds_tex = nullptr;
	static ID3D11DepthStencilView* g_dsv = nullptr;

	static ID3D11VertexShader* g_vs = nullptr;
	static ID3D11PixelShader* g_ps = nullptr;
	static ID3D11InputLayout* g_layout = nullptr;
	static ID3D11Buffer* g_vbuf = nullptr;
	static ID3D11Buffer* g_cbuf = nullptr;
	static ID3D11RasterizerState* g_raster = nullptr;
	static ID3D11RasterizerState* g_raster_wire = nullptr;
	static ID3D11DepthStencilState* g_dss = nullptr;
	static ID3D11DepthStencilState* g_dss_no = nullptr; // no depth test - for silhouette debug
	static ID3D11BlendState* g_blend = nullptr;

	static UINT g_vertex_count = 0;
	static bool g_loaded = false;
	static bool g_inited = false;
	static bool g_load_attempted = false;
	static bool g_load_failed = false;
	static bool g_load_loading = false;
	static bool g_render_dirty = true;
	static float g_last_rotation_y = 0.f;
	static float g_last_rotation_x = 0.f;
	static float g_last_view_scale = 0.f;

	// Per-primitive draw ranges + material textures (populated by load_glb).
	// load_obj produces a single primitive with no texture (default white SRV
	// is bound, lighting modulates -> white chams).
	static std::vector<Primitive>      g_primitives;
	static ID3D11ShaderResourceView*   g_default_white_srv = nullptr;
	static ID3D11SamplerState*         g_sampler = nullptr;

	// Skeleton (bone) data. Each entry is a joint with its bind-pose
	// position in normalized model space and the index of its parent joint
	// (-1 for root). depth is the distance from the root in the parent
	// chain ? used to filter out face / finger / toe sub-rigs that are
	// deeper than the basic torso/arms/legs we want to render.
	struct BoneJoint
	{
		XMFLOAT3 pos;
		int      parent;
		int      depth;
	};
	static std::vector<BoneJoint> g_bones;

	template <typename T>
	static void safe_release(T*& value)
	{
		if (value)
		{
			value->Release();
			value = nullptr;
		}
	}

	static void release_core_resources()
	{
		safe_release(g_vbuf);
		safe_release(g_cbuf);
		safe_release(g_vs);
		safe_release(g_ps);
		safe_release(g_layout);
		safe_release(g_rtv);
		safe_release(g_srv);
		safe_release(g_rt_tex);
		safe_release(g_dsv);
		safe_release(g_ds_tex);
		safe_release(g_raster);
		safe_release(g_raster_wire);
		safe_release(g_dss);
		safe_release(g_dss_no);
		safe_release(g_blend);
		safe_release(g_default_white_srv);
		safe_release(g_sampler);
		safe_release(g_ctx);
		safe_release(g_device);
		g_vertex_count = 0;
		g_loaded = false;
		g_load_attempted = false;
		g_load_failed = false;
		g_load_loading = false;
		g_render_dirty = true;
		g_inited = false;
	}

	static bool core_resources_ready()
	{
		return g_device && g_ctx && g_rt_tex && g_rtv && g_srv && g_ds_tex && g_dsv &&
			g_vs && g_ps && g_layout && g_cbuf && g_raster && g_dss && g_blend && g_sampler;
	}

	// Square RT to avoid stretching when displayed in non-matching UI rect.
	static const UINT RT_W = 512;
	static const UINT RT_H = 512;

	// Use ddx/ddy in the pixel shader to derive a guaranteed-correct flat
	// face normal from world-space position. This is independent of the
	// imported normals or per-vertex normal averaging which sometimes go
	// wrong on game-extracted OBJs that have weird winding.
	//
	// Lighting is FIXED in world space (key + fill + ambient + spec + rim).
	// As the model rotates, different parts move through illumination -
	// this is what gives the eye a proper 3D / volume cue. Camera-relative
	// (headlight) lighting was the reason the old preview "looked flat".
	static const char* shader_src = R"(
cbuffer CB : register(b0)
{
    float4x4 viewproj;
    float4x4 world;    // unused now - rotation done in shader
    float4 cam_pos;
    float4 color;
    float4 params;     // x = rot_y, y = rot_x
};

Texture2D    g_basecolor : register(t0);
SamplerState g_samp      : register(s0);

struct VS_IN  { float3 pos : POSITION; float3 norm : NORMAL; float2 uv : TEXCOORD0; };
struct VS_OUT { float4 pos : SV_POSITION; float3 wpos : POSITION1; float3 wn : NORMAL; float2 uv : TEXCOORD0; };

VS_OUT vs_main(VS_IN i)
{
    VS_OUT o;

    // Rotation done entirely in the shader ? no CPU matrix convention issues.
    // params.x = user yaw, plus a 180? base offset so the model defaults to
    // a back/turned-around orientation regardless of how the .obj was authored.
    float cy = cos(params.x + 3.14159265); float sy = sin(params.x + 3.14159265);
    float cx = cos(params.y); float sx = sin(params.y);

    float3 p = i.pos;
    // RotY
    float3 p1 = float3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    // RotX
    float3 p2 = float3(p1.x, cx * p1.y - sx * p1.z, sx * p1.y + cx * p1.z);

    o.wpos = p2;

    // Rotate normal the same way
    float3 n = i.norm;
    float3 n1 = float3(cy * n.x + sy * n.z, n.y, -sy * n.x + cy * n.z);
    o.wn = float3(n1.x, cx * n1.y - sx * n1.z, sx * n1.y + cx * n1.z);

    o.uv = i.uv;

    float view_scale = params.z > 0.0 ? params.z : 1.0;
    float scale_y = 1.18 * view_scale;
    float scale_x = 2.38 * view_scale;
    o.pos = float4(p2.x * scale_x, p2.y * scale_y, p2.z * 0.1 + 0.5, 1.0);
    return o;
}

float4 ps_main(VS_OUT i, bool isFront : SV_IsFrontFace) : SV_TARGET
{
    float3 dx = ddx(i.wpos);
    float3 dy = ddy(i.wpos);
    float3 n = cross(dx, dy);
    float n_len = length(n);
    if (n_len < 1e-6) return float4(color.rgb * 0.5, 1.0);
    n /= n_len;
    if (!isFront) n = -n;

    float3 L_key  = normalize(float3( 0.45,  0.85, -0.40));
    float3 L_fill = normalize(float3(-0.55,  0.10, -0.30));

    // Softer lighting — menu preview reads harsh with full key/spec.
    float key  = saturate(dot(n, L_key))  * 0.55;
    float fill = saturate(dot(n, L_fill)) * 0.28;
    float back = saturate(dot(n, -L_key)) * 0.18;
    float hemi = n.y * 0.5 + 0.5;
    float ambient = lerp(0.34, 0.48, hemi);
    float lit = (key + fill + back + ambient) * 0.88;

    float3 V = normalize(cam_pos.xyz - i.wpos);
    float3 H = normalize(L_key + V);
    float spec = pow(saturate(dot(n, H)), 18.0) * 0.10;
    float fres = pow(1.0 - saturate(dot(n, V)), 2.2);
    float rim  = fres * 0.12;

    // Soft sample: mip bias + tiny 4-tap blur so the menu preview is less sharp.
    float2 texel = float2(1.5 / 512.0, 1.5 / 512.0);
    float4 tex_col =
        g_basecolor.SampleBias(g_samp, i.uv, 1.1) * 0.40 +
        g_basecolor.SampleBias(g_samp, i.uv + float2( texel.x,  0.0), 0.9) * 0.15 +
        g_basecolor.SampleBias(g_samp, i.uv + float2(-texel.x,  0.0), 0.9) * 0.15 +
        g_basecolor.SampleBias(g_samp, i.uv + float2( 0.0,  texel.y), 0.9) * 0.15 +
        g_basecolor.SampleBias(g_samp, i.uv + float2( 0.0, -texel.y), 0.9) * 0.15;
    float3 base = tex_col.rgb;
    // Gentle darken + desaturate so UI preview is less "crisp game asset".
    float grey = dot(base, float3(0.299, 0.587, 0.114));
    base = lerp(base, grey.xxx, 0.14) * 0.88;

    float3 col = base * lit + spec.xxx + rim.xxx;
    return float4(saturate(col), 1.0);
}
)";

	bool Init(ID3D11Device* device, ID3D11DeviceContext* ctx)
	{
		if (g_inited) return core_resources_ready();
		if (!device || !ctx) return false;
		release_core_resources();
		g_device = device;
		g_ctx = ctx;
		g_device->AddRef();
		g_ctx->AddRef();

		ID3DBlob* vs_blob = nullptr;
		ID3DBlob* ps_blob = nullptr;
		ID3DBlob* err = nullptr;

		HRESULT hr = D3DCompile(shader_src, strlen(shader_src), nullptr, nullptr, nullptr,
			"vs_main", "vs_5_0", 0, 0, &vs_blob, &err);
		if (FAILED(hr)) { if (err) err->Release(); release_core_resources(); return false; }

		hr = D3DCompile(shader_src, strlen(shader_src), nullptr, nullptr, nullptr,
			"ps_main", "ps_5_0", 0, 0, &ps_blob, &err);
		if (FAILED(hr)) { vs_blob->Release(); if (err) err->Release(); release_core_resources(); return false; }

		device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_vs);
		device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &g_ps);

		D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		device->CreateInputLayout(layout_desc, 3, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &g_layout);

		vs_blob->Release();
		ps_blob->Release();

		// Color render target
		D3D11_TEXTURE2D_DESC td_color = {};
		td_color.Width = RT_W; td_color.Height = RT_H;
		td_color.MipLevels = 1; td_color.ArraySize = 1;
		td_color.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td_color.SampleDesc.Count = 1;
		td_color.Usage = D3D11_USAGE_DEFAULT;
		td_color.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device->CreateTexture2D(&td_color, nullptr, &g_rt_tex)) || !g_rt_tex)
		{
			release_core_resources();
			return false;
		}
		if (FAILED(device->CreateRenderTargetView(g_rt_tex, nullptr, &g_rtv)) || !g_rtv)
		{
			release_core_resources();
			return false;
		}
		if (FAILED(device->CreateShaderResourceView(g_rt_tex, nullptr, &g_srv)) || !g_srv)
		{
			release_core_resources();
			return false;
		}

		// Depth stencil (separate desc - shared sample desc with color)
		D3D11_TEXTURE2D_DESC td_depth = {};
		td_depth.Width = RT_W; td_depth.Height = RT_H;
		td_depth.MipLevels = 1; td_depth.ArraySize = 1;
		td_depth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		td_depth.SampleDesc.Count = 1;
		td_depth.Usage = D3D11_USAGE_DEFAULT;
		td_depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(device->CreateTexture2D(&td_depth, nullptr, &g_ds_tex)) || !g_ds_tex)
		{
			release_core_resources();
			return false;
		}
		if (FAILED(device->CreateDepthStencilView(g_ds_tex, nullptr, &g_dsv)) || !g_dsv)
		{
			release_core_resources();
			return false;
		}

		// Constant buffer
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(CBuf);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		device->CreateBuffer(&bd, nullptr, &g_cbuf);

		// Rasterizer: solid, back-face cull (clean silhouette).
		// FrontCounterClockwise=TRUE because our left-handed view + the
		// XMMatrixRotationY(180) base flip swaps winding for the model.
		D3D11_RASTERIZER_DESC rd = {};
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_NONE; // safest for unknown winding
		rd.FrontCounterClockwise = FALSE;
		rd.DepthClipEnable = TRUE;
		rd.ScissorEnable = FALSE;   // <- explicitly disable scissor
		device->CreateRasterizerState(&rd, &g_raster);

		// Wireframe rasterizer for debug
		D3D11_RASTERIZER_DESC rd_wire = rd;
		rd_wire.FillMode = D3D11_FILL_WIREFRAME;
		device->CreateRasterizerState(&rd_wire, &g_raster_wire);

		// Depth: standard less + write
		D3D11_DEPTH_STENCIL_DESC dsd = {};
		dsd.DepthEnable = TRUE;
		dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dsd.DepthFunc = D3D11_COMPARISON_LESS;
		device->CreateDepthStencilState(&dsd, &g_dss);

		// No-depth state for silhouette debug
		D3D11_DEPTH_STENCIL_DESC dsd_no = {};
		dsd_no.DepthEnable = FALSE;
		dsd_no.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		device->CreateDepthStencilState(&dsd_no, &g_dss_no);

		// Opaque blend (no blending - just write the source color)
		D3D11_BLEND_DESC bld = {};
		bld.RenderTarget[0].BlendEnable = FALSE;
		bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		device->CreateBlendState(&bld, &g_blend);

		// Default 1x1 white SRV ? bound when a primitive has no base color
		// texture. Combined with the lighting-modulated PS this produces the
		// classic "white chams" look out of the box (used by load_obj).
		{
			uint32_t white_pixel = 0xFFFFFFFFu;
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_IMMUTABLE;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA sd = {};
			sd.pSysMem = &white_pixel;
			sd.SysMemPitch = 4;
			ID3D11Texture2D* tex = nullptr;
			if (SUCCEEDED(device->CreateTexture2D(&td, &sd, &tex)))
			{
				device->CreateShaderResourceView(tex, nullptr, &g_default_white_srv);
				tex->Release();
			}
		}

		// Soft linear wrap sampler (mip-aware so SampleBias can soften albedo).
		{
			D3D11_SAMPLER_DESC sd = {};
			sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
			sd.MinLOD = 0.f;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			sd.MipLODBias = 0.35f;
			device->CreateSamplerState(&sd, &g_sampler);
		}

		if (!core_resources_ready())
		{
			release_core_resources();
			return false;
		}

		g_inited = true;
		return true;
	}

	 
	// Releases per-primitive base color SRVs from a previous load. Called at
	// the start of every load_* function so re-loading doesn't leak textures.
	static void release_primitives()
	{
		g_bones.clear();
		for (auto& p : g_primitives)
			if (p.basecolor_srv) p.basecolor_srv->Release();
		g_primitives.clear();
	}

	// Diagnostic cube. Builds a simple known-good unit cube directly into
	// the vertex buffer, bypassing the OBJ loader. If the preview shows a
	// recognizable rotating cube with this, the rendering pipeline (matrices,
	// shaders, RT, viewport, state save/restore) is healthy and any visible
	// glitch is OBJ-specific (parser, vertex format, scaling, etc.).
	 
	bool LoadTestCube()
	{
		if (!g_inited) return false;

		std::vector<Vertex> vertices;
		// 8 corners of a unit cube centered at origin.
		const XMFLOAT3 c[8] = {
			{ -0.5f, -0.5f, -0.5f }, // 0
			{  0.5f, -0.5f, -0.5f }, // 1
			{  0.5f,  0.5f, -0.5f }, // 2
			{ -0.5f,  0.5f, -0.5f }, // 3
			{ -0.5f, -0.5f,  0.5f }, // 4
			{  0.5f, -0.5f,  0.5f }, // 5
			{  0.5f,  0.5f,  0.5f }, // 6
			{ -0.5f,  0.5f,  0.5f }, // 7
		};
		auto add_quad = [&](int a, int b, int cc, int d, XMFLOAT3 n)
		{
			Vertex va{ c[a], n }, vb{ c[b], n }, vcc{ c[cc], n }, vd{ c[d], n };
			vertices.push_back(va); vertices.push_back(vb); vertices.push_back(vcc);
			vertices.push_back(va); vertices.push_back(vcc); vertices.push_back(vd);
		};
		add_quad(0, 1, 2, 3, {  0,  0, -1 }); // -Z
		add_quad(5, 4, 7, 6, {  0,  0,  1 }); // +Z
		add_quad(4, 0, 3, 7, { -1,  0,  0 }); // -X
		add_quad(1, 5, 6, 2, {  1,  0,  0 }); // +X
		add_quad(3, 2, 6, 7, {  0,  1,  0 }); // +Y
		add_quad(4, 5, 1, 0, {  0, -1,  0 }); // -Y

		if (g_vbuf) { g_vbuf->Release(); g_vbuf = nullptr; }
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)(vertices.size() * sizeof(Vertex));
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA sd = {};
		sd.pSysMem = vertices.data();
		HRESULT hr = g_device->CreateBuffer(&bd, &sd, &g_vbuf);
		if (FAILED(hr)) return false;

		g_vertex_count = (UINT)vertices.size();
		release_primitives();
		g_primitives.push_back({ 0, g_vertex_count, nullptr });
		g_loaded = true;
		g_render_dirty = true;
		return true;
	}

	bool LoadObj_Unused(const std::string& path)
	{
		std::ifstream file(path);
		if (!file.is_open()) return false;

		std::vector<XMFLOAT3> positions;
		std::vector<XMFLOAT3> normals;
		std::vector<Vertex> vertices;

		// Helper to parse a single face vertex token "v", "v/vt", "v//vn" or "v/vt/vn".
		// Negative OBJ indices mean offsets from end - we don't expect them here but handle anyway.
		auto parse_token = [&](const std::string& tok, int& v_out, int& n_out)
		{
			v_out = 0; n_out = -1;
			if (tok.empty()) return;
			size_t s1 = tok.find('/');
			std::string vs = (s1 == std::string::npos) ? tok : tok.substr(0, s1);
			try { v_out = std::stoi(vs); }
			catch (...) { v_out = 0; }
			if (v_out > 0) v_out -= 1;
			else if (v_out < 0) v_out = (int)positions.size() + v_out;

			if (s1 != std::string::npos)
			{
				size_t s2 = tok.find('/', s1 + 1);
				if (s2 != std::string::npos && s2 + 1 < tok.size())
				{
					try { n_out = std::stoi(tok.substr(s2 + 1)); }
					catch (...) { n_out = -1; }
					if (n_out > 0) n_out -= 1;
					else if (n_out < 0) n_out = (int)normals.size() + n_out;
					else n_out = -1;
				}
			}
		};

		std::string line;
		while (std::getline(file, line))
		{
			// strip trailing \r (Windows files)
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
				line.pop_back();
			if (line.empty() || line[0] == '#') continue;

			// Tokenize - first whitespace-separated token is the directive.
			std::istringstream ss(line);
			std::string prefix;
			ss >> prefix;
			if (prefix.empty()) continue;

			if (prefix == "v")
			{
				XMFLOAT3 p;
				ss >> p.x >> p.y >> p.z;
				positions.push_back(p);
			}
			else if (prefix == "vn")
			{
				XMFLOAT3 n;
				ss >> n.x >> n.y >> n.z;
				normals.push_back(n);
			}
			else if (prefix == "f")
			{
				std::vector<int> vi, ni;
				std::string tok;
				while (ss >> tok)
				{
					int v_idx, n_idx;
					parse_token(tok, v_idx, n_idx);
					vi.push_back(v_idx);
					ni.push_back(n_idx);
				}
				if (vi.size() < 3) continue;

				// Triangulate as a fan
				for (size_t i = 1; i + 1 < vi.size(); ++i)
				{
					auto make_v = [&](int v, int n) -> Vertex {
						Vertex out{};
						if (v >= 0 && v < (int)positions.size())
							out.pos = positions[v];
						if (n >= 0 && n < (int)normals.size())
							out.normal = normals[n];
						return out;
					};
					vertices.push_back(make_v(vi[0],     ni[0]));
					vertices.push_back(make_v(vi[i],     ni[i]));
					vertices.push_back(make_v(vi[i + 1], ni[i + 1]));
				}
			}
			// All other directives (o, g, s, usemtl, mtllib, vt, vp...) ignored.
		}

		if (vertices.empty()) return false;

		// (No special filtering - render all triangles as-is.)

		// Centering: use the CENTROID (mean of all vertex positions) for the
		// horizontal axes (X / Z) ? keeps the actual body on the rotation
		// axis when an arm or held weapon reaches far in one direction.
		// For the vertical axis (Y) we use the bounding-box midpoint instead
		// ? it places the geometric top of the model at +Y and bottom at -Y
		// so the character fills the panel symmetrically. Using the centroid
		// for Y makes a character with dense hair / dense skirt drift toward
		// the bottom or top of the panel respectively.
		XMFLOAT3 mn = { FLT_MAX, FLT_MAX, FLT_MAX };
		XMFLOAT3 mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (auto& v : vertices)
		{
			mn.x = (std::min)(mn.x, v.pos.x); mn.y = (std::min)(mn.y, v.pos.y); mn.z = (std::min)(mn.z, v.pos.z);
			mx.x = (std::max)(mx.x, v.pos.x); mx.y = (std::max)(mx.y, v.pos.y); mx.z = (std::max)(mx.z, v.pos.z);
		}

		XMFLOAT3 centroid = { 0.f, 0.f, 0.f };
		{
			double sx = 0.0, sz = 0.0;
			for (auto& v : vertices) { sx += v.pos.x; sz += v.pos.z; }
			const double inv_n = 1.0 / (double)vertices.size();
			centroid.x = (float)(sx * inv_n);
			centroid.z = (float)(sz * inv_n);
		}
		const XMFLOAT3 center = { centroid.x, (mn.y + mx.y) * 0.5f, centroid.z };

		// Translate to chosen center.
		for (auto& v : vertices)
		{
			v.pos.x -= center.x;
			v.pos.y -= center.y;
			v.pos.z -= center.z;
		}

		// Scale by VERTICAL extent only ? for character preview the panel
		// is portrait-shaped, so height needs to drive the normalization.
		// Using the largest extent here would shrink a wide-armed character
		// vertically and leave empty space top + bottom.
		// (ext_y is invariant under translation, so we reuse the bbox from
		// the centering step above instead of recomputing it.)
		float ext_y = mx.y - mn.y;
		if (ext_y < 1e-4f) ext_y = 1.f; // safety
		float scale = 1.7f / ext_y;

		for (auto& v : vertices)
		{
			v.pos.x *= scale;
			v.pos.y *= scale;
			v.pos.z *= scale;
		}

		// Vertex buffer
		if (g_vbuf) { g_vbuf->Release(); g_vbuf = nullptr; }

		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)(vertices.size() * sizeof(Vertex));
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA sd = {};
		sd.pSysMem = vertices.data();
		HRESULT hr = g_device->CreateBuffer(&bd, &sd, &g_vbuf);
		if (FAILED(hr)) return false;

		g_vertex_count = (UINT)vertices.size();
		// Single primitive, no material ? render path will fall back to the
		// default white SRV which gives the white-chams look.
		release_primitives();
		g_primitives.push_back({ 0, g_vertex_count, nullptr });
		g_loaded = true;
		g_render_dirty = true;
		return true;
	}

	struct d3d_pipeline_state_snapshot
	{
		static constexpr UINT k_srv_count = 8;
		static constexpr UINT k_class_instance_capacity = 256;
		static constexpr UINT k_rtv_count = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

		ID3D11DeviceContext* ctx = nullptr;

		ID3D11RenderTargetView* rtvs[k_rtv_count] = {};
		ID3D11DepthStencilView* dsv = nullptr;
		D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
		UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
		UINT scissor_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		ID3D11RasterizerState* rasterizer = nullptr;
		ID3D11DepthStencilState* depth_stencil = nullptr;
		UINT stencil_ref = 0;
		ID3D11BlendState* blend = nullptr;
		FLOAT blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
		UINT sample_mask = 0xFFFFFFFFu;

		ID3D11InputLayout* input_layout = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ID3D11Buffer* vertex_buffer = nullptr;
		UINT vertex_stride = 0;
		UINT vertex_offset = 0;
		ID3D11Buffer* index_buffer = nullptr;
		DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
		UINT index_offset = 0;

		ID3D11VertexShader* vs = nullptr;
		ID3D11PixelShader* ps = nullptr;
		ID3D11GeometryShader* gs = nullptr;
		ID3D11HullShader* hs = nullptr;
		ID3D11DomainShader* ds = nullptr;
		ID3D11ClassInstance* vs_instances[k_class_instance_capacity] = {};
		ID3D11ClassInstance* ps_instances[k_class_instance_capacity] = {};
		ID3D11ClassInstance* gs_instances[k_class_instance_capacity] = {};
		ID3D11ClassInstance* hs_instances[k_class_instance_capacity] = {};
		ID3D11ClassInstance* ds_instances[k_class_instance_capacity] = {};
		UINT vs_instance_count = k_class_instance_capacity;
		UINT ps_instance_count = k_class_instance_capacity;
		UINT gs_instance_count = k_class_instance_capacity;
		UINT hs_instance_count = k_class_instance_capacity;
		UINT ds_instance_count = k_class_instance_capacity;

		ID3D11Buffer* vs_cbuf = nullptr;
		ID3D11Buffer* ps_cbuf = nullptr;
		ID3D11ShaderResourceView* ps_srvs[k_srv_count] = {};
		ID3D11SamplerState* ps_sampler = nullptr;
		ID3D11Buffer* so_target = nullptr;

		explicit d3d_pipeline_state_snapshot(ID3D11DeviceContext* context) : ctx(context)
		{
			ctx->OMGetRenderTargets(k_rtv_count, rtvs, &dsv);
			ctx->RSGetViewports(&viewport_count, viewports);
			ctx->RSGetScissorRects(&scissor_count, scissors);
			ctx->RSGetState(&rasterizer);
			ctx->OMGetDepthStencilState(&depth_stencil, &stencil_ref);
			ctx->OMGetBlendState(&blend, blend_factor, &sample_mask);

			ctx->IAGetInputLayout(&input_layout);
			ctx->IAGetPrimitiveTopology(&topology);
			ctx->IAGetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
			ctx->IAGetIndexBuffer(&index_buffer, &index_format, &index_offset);

			ctx->VSGetShader(&vs, vs_instances, &vs_instance_count);
			ctx->PSGetShader(&ps, ps_instances, &ps_instance_count);
			ctx->GSGetShader(&gs, gs_instances, &gs_instance_count);
			ctx->HSGetShader(&hs, hs_instances, &hs_instance_count);
			ctx->DSGetShader(&ds, ds_instances, &ds_instance_count);

			ctx->VSGetConstantBuffers(0, 1, &vs_cbuf);
			ctx->PSGetConstantBuffers(0, 1, &ps_cbuf);
			ctx->PSGetShaderResources(0, k_srv_count, ps_srvs);
			ctx->PSGetSamplers(0, 1, &ps_sampler);
			ctx->SOGetTargets(1, &so_target);
		}

		d3d_pipeline_state_snapshot(const d3d_pipeline_state_snapshot&) = delete;
		d3d_pipeline_state_snapshot& operator=(const d3d_pipeline_state_snapshot&) = delete;

		~d3d_pipeline_state_snapshot()
		{
			ctx->OMSetRenderTargets(k_rtv_count, rtvs, dsv);
			ctx->RSSetViewports(viewport_count, viewport_count > 0 ? viewports : nullptr);
			ctx->RSSetScissorRects(scissor_count, scissor_count > 0 ? scissors : nullptr);
			ctx->RSSetState(rasterizer);
			ctx->OMSetDepthStencilState(depth_stencil, stencil_ref);
			ctx->OMSetBlendState(blend, blend_factor, sample_mask);

			ctx->IASetInputLayout(input_layout);
			ctx->IASetPrimitiveTopology(topology);
			ctx->IASetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
			ctx->IASetIndexBuffer(index_buffer, index_format, index_offset);

			ctx->VSSetShader(vs, vs_instances, vs_instance_count);
			ctx->PSSetShader(ps, ps_instances, ps_instance_count);
			ctx->GSSetShader(gs, gs_instances, gs_instance_count);
			ctx->HSSetShader(hs, hs_instances, hs_instance_count);
			ctx->DSSetShader(ds, ds_instances, ds_instance_count);

			ctx->VSSetConstantBuffers(0, 1, &vs_cbuf);
			ctx->PSSetConstantBuffers(0, 1, &ps_cbuf);
			ctx->PSSetShaderResources(0, k_srv_count, ps_srvs);
			ctx->PSSetSamplers(0, 1, &ps_sampler);
			UINT so_offset = 0;
			ctx->SOSetTargets(1, &so_target, &so_offset);

			for (UINT i = 0; i < k_rtv_count; ++i) safe_release(rtvs[i]);
			safe_release(dsv);
			safe_release(rasterizer);
			safe_release(depth_stencil);
			safe_release(blend);
			safe_release(input_layout);
			safe_release(vertex_buffer);
			safe_release(index_buffer);
			safe_release(vs);
			safe_release(ps);
			safe_release(gs);
			safe_release(hs);
			safe_release(ds);
			safe_release(vs_cbuf);
			safe_release(ps_cbuf);
			safe_release(ps_sampler);
			safe_release(so_target);
			for (UINT i = 0; i < k_srv_count; ++i) safe_release(ps_srvs[i]);
			for (UINT i = 0; i < vs_instance_count; ++i) safe_release(vs_instances[i]);
			for (UINT i = 0; i < ps_instance_count; ++i) safe_release(ps_instances[i]);
			for (UINT i = 0; i < gs_instance_count; ++i) safe_release(gs_instances[i]);
			for (UINT i = 0; i < hs_instance_count; ++i) safe_release(hs_instances[i]);
			for (UINT i = 0; i < ds_instance_count; ++i) safe_release(ds_instances[i]);
		}
	};

	 
	// .glb loader (binary glTF). Pulls geometry + UVs + base-color textures
	// for every primitive in every mesh, decodes embedded PNG/JPEG via
	// stb_image, uploads each as an SRV, and records per-primitive draw
	// ranges so each chunk gets its own texture in render().
	 
	// Internal: take a parsed cgltf_data (no matter how it was loaded) and
	// build the vertex buffer + per-primitive textures. Owned by both
	// load_glb (file path) and load_glb_from_memory (RC resource).
	static bool finish_glb_load(cgltf_data* data)
	{
		release_primitives();
		std::vector<Vertex> all_vertices;
		// Expanded indexed nekomiya mesh is ~86k verts — reserve once.
		all_vertices.reserve(100000);

		hooks::HLog("ModelPreview: finish_glb geometry...");

		for (size_t mi = 0; mi < data->meshes_count; ++mi)
		{
			cgltf_mesh* mesh = &data->meshes[mi];
			for (size_t pi = 0; pi < mesh->primitives_count; ++pi)
			{
				cgltf_primitive* prim = &mesh->primitives[pi];
				if (prim->type != cgltf_primitive_type_triangles)
					continue;

				cgltf_accessor* pos_acc = nullptr;
				cgltf_accessor* nrm_acc = nullptr;
				cgltf_accessor* uv_acc  = nullptr;
				for (size_t ai = 0; ai < prim->attributes_count; ++ai)
				{
					cgltf_attribute& a = prim->attributes[ai];
					if      (a.type == cgltf_attribute_type_position) pos_acc = a.data;
					else if (a.type == cgltf_attribute_type_normal)   nrm_acc = a.data;
					else if (a.type == cgltf_attribute_type_texcoord && uv_acc == nullptr) uv_acc = a.data;
				}
				if (!pos_acc) continue;

				const size_t vc = pos_acc->count;
				if (vc == 0 || vc > 500000)
					continue;

				std::vector<XMFLOAT3> positions(vc);
				std::vector<XMFLOAT3> normals(vc, XMFLOAT3(0.f, 1.f, 0.f));
				std::vector<XMFLOAT2> uvs(vc, XMFLOAT2(0.f, 0.f));

				for (size_t i = 0; i < vc; ++i)
					cgltf_accessor_read_float(pos_acc, i, &positions[i].x, 3);
				if (nrm_acc)
					for (size_t i = 0; i < vc; ++i)
						cgltf_accessor_read_float(nrm_acc, i, &normals[i].x, 3);
				if (uv_acc)
					for (size_t i = 0; i < vc; ++i)
						cgltf_accessor_read_float(uv_acc, i, &uvs[i].x, 2);

				const UINT prim_start = (UINT)all_vertices.size();

				if (prim->indices)
				{
					const size_t ic = prim->indices->count;
					if (ic > 0 && ic <= 3000000)
					{
						all_vertices.reserve(all_vertices.size() + ic);
						for (size_t i = 0; i < ic; ++i)
						{
							size_t v_idx = cgltf_accessor_read_index(prim->indices, i);
							if (v_idx >= vc) continue;
							all_vertices.push_back(Vertex{ positions[v_idx], normals[v_idx], uvs[v_idx] });
						}
					}
				}
				else
				{
					for (size_t i = 0; i < vc; ++i)
						all_vertices.push_back(Vertex{ positions[i], normals[i], uvs[i] });
				}
				const UINT prim_count = (UINT)all_vertices.size() - prim_start;

				// Skip embedded PNG decode/upload for now — stbi on Present
				// thread was crashing the process (null HeapHandle). White
				// default SRV still gives a lit chams-style preview.
				g_primitives.push_back({ prim_start, prim_count, nullptr });
			}
		}

		if (all_vertices.empty())
		{
			hooks::HLog("ModelPreview: finish_glb no vertices");
			release_primitives();
			return false;
		}

		{
			char line[128];
			_snprintf(line, sizeof(line),
				"ModelPreview: verts=%zu prims=%zu",
				all_vertices.size(), g_primitives.size());
			hooks::HLog(line);
		}

		XMFLOAT3 mn = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
		XMFLOAT3 mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (auto& v : all_vertices)
		{
			mn.x = (std::min)(mn.x, v.pos.x); mn.y = (std::min)(mn.y, v.pos.y); mn.z = (std::min)(mn.z, v.pos.z);
			mx.x = (std::max)(mx.x, v.pos.x); mx.y = (std::max)(mx.y, v.pos.y); mx.z = (std::max)(mx.z, v.pos.z);
		}

		XMFLOAT3 centroid = { 0.f, 0.f, 0.f };
		{
			double sx = 0.0, sz = 0.0;
			for (auto& v : all_vertices) { sx += v.pos.x; sz += v.pos.z; }
			const double inv_n = 1.0 / (double)all_vertices.size();
			centroid.x = (float)(sx * inv_n);
			centroid.z = (float)(sz * inv_n);
		}
		const XMFLOAT3 center = { centroid.x, (mn.y + mx.y) * 0.5f, centroid.z };
		for (auto& v : all_vertices)
		{
			v.pos.x -= center.x; v.pos.y -= center.y; v.pos.z -= center.z;
		}

		float ext_y = mx.y - mn.y;
		if (ext_y < 1e-4f) ext_y = 1.f;
		float scale = 1.7f / ext_y;
		for (auto& v : all_vertices)
		{
			v.pos.x *= scale; v.pos.y *= scale; v.pos.z *= scale;
		}
		if (g_vbuf) { g_vbuf->Release(); g_vbuf = nullptr; }

		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)(all_vertices.size() * sizeof(Vertex));
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA sd = {};
		sd.pSysMem = all_vertices.data();
		HRESULT hr = g_device->CreateBuffer(&bd, &sd, &g_vbuf);
		if (FAILED(hr))
		{
			char line[96];
			_snprintf(line, sizeof(line),
				"ModelPreview: CreateBuffer FAILED hr=0x%08X",
				(unsigned)hr);
			hooks::HLog(line);
			release_primitives();
			return false;
		}

		g_vertex_count = (UINT)all_vertices.size();
		g_loaded = true;
		g_render_dirty = true;
		hooks::HLog("ModelPreview: finish_glb OK");
		return true;
	}

	bool LoadGlb(const std::string& path)
	{
		if (!g_inited) return false;

		cgltf_options options = {};
		cgltf_data*   data    = nullptr;
		if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
		{
			printf("[glb] parse_file FAILED: %s\n", path.c_str());
			return false;
		}
		if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
		{
			printf("[glb] load_buffers FAILED\n");
			cgltf_free(data);
			return false;
		}

		printf("[glb] %s\n", path.c_str());
		bool ok = finish_glb_load(data);
		cgltf_free(data);
		return ok;
	}

	bool LoadGlbFromMemory(const void* glb_bytes, size_t glb_size)
	{
		if (!g_inited || !glb_bytes || glb_size == 0) return false;

		{
			const auto* b = static_cast<const unsigned char*>(glb_bytes);
			char line[128];
			_snprintf(line, sizeof(line),
				"ModelPreview: parse size=%zu magic=%c%c%c%c",
				glb_size,
				glb_size > 0 ? (char)b[0] : '?',
				glb_size > 1 ? (char)b[1] : '?',
				glb_size > 2 ? (char)b[2] : '?',
				glb_size > 3 ? (char)b[3] : '?');
			hooks::HLog(line);
		}

		cgltf_options options = {};
		cgltf_data*   data    = nullptr;
		const cgltf_result parse_rc =
			cgltf_parse(&options, glb_bytes, glb_size, &data);
		if (parse_rc != cgltf_result_success)
		{
			char line[96];
			_snprintf(line, sizeof(line),
				"ModelPreview: cgltf_parse FAILED rc=%d",
				(int)parse_rc);
			hooks::HLog(line);
			return false;
		}
		// .glb binary chunk is already inside the buffer — NULL path.
		const cgltf_result buf_rc =
			cgltf_load_buffers(&options, data, nullptr);
		if (buf_rc != cgltf_result_success)
		{
			char line[96];
			_snprintf(line, sizeof(line),
				"ModelPreview: cgltf_load_buffers FAILED rc=%d",
				(int)buf_rc);
			hooks::HLog(line);
			cgltf_free(data);
			return false;
		}
		bool ok = finish_glb_load(data);
		cgltf_free(data);
		return ok;
	}

	void RenderInternal(float rotation_y, float rotation_x, bool silhouette, int debug_mode, float view_scale)
	{
		(void)silhouette; (void)debug_mode; // unused now
		if (!g_inited || !core_resources_ready()) return;
		if (!g_loaded && !g_load_failed && !g_load_loading)
			LoadDefaultAsset();
		if (!g_loaded || (g_vertex_count == 0 && g_primitives.empty())) return;

		d3d_pipeline_state_snapshot state(g_ctx);

		// Fully transparent background ? the model gets composited onto
		// whatever the menu draws behind the preview window, no visible box.
		float clear_color[4] = { 0.f, 0.f, 0.f, 0.f };
		g_ctx->ClearRenderTargetView(g_rtv, clear_color);
		g_ctx->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);

		ID3D11RenderTargetView* my_rtv = g_rtv;
		// Unbind any SRV from PS slots first - if the previous draw used our
		// render target's SRV (it is bound to ImGui as the preview image),
		// trying to set the same texture as RTV would be a hazard.
		ID3D11ShaderResourceView* null_srvs[8] = {};
		g_ctx->PSSetShaderResources(0, 8, null_srvs);
		g_ctx->OMSetRenderTargets(1, &my_rtv, g_dsv);

		D3D11_VIEWPORT vp = {};
		vp.Width = (float)RT_W; vp.Height = (float)RT_H;
		vp.MaxDepth = 1.f;
		g_ctx->RSSetViewports(1, &vp);

		// Set a scissor rect that covers the entire RT (in case the previous
		// state had a scissor enabled and clipping our preview).
		D3D11_RECT my_scissor = { 0, 0, (LONG)RT_W, (LONG)RT_H };
		g_ctx->RSSetScissorRects(1, &my_scissor);

		g_ctx->RSSetState(g_raster);
		g_ctx->OMSetDepthStencilState(g_dss, 0);

		// Opaque blend so previous alpha state can't dim the model.
		FLOAT bf[4] = { 0, 0, 0, 0 };
		g_ctx->OMSetBlendState(g_blend, bf, 0xFFFFFFFFu);

		// Rotation is done entirely in the HLSL vertex shader now.
		// Just pass identity matrices and the rotation angles via params.
		XMMATRIX ident = XMMatrixIdentity();

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(g_ctx->Map(g_cbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return;
		CBuf* cb = (CBuf*)mapped.pData;
		cb->mvp     = ident;
		cb->world   = ident;
		XMFLOAT3 cam_f = { 0.f, 0.3f, -3.f };
		cb->cam_pos = XMFLOAT4(cam_f.x, cam_f.y, cam_f.z, 1.f);
		cb->color   = XMFLOAT4(0.78f, 0.76f, 0.88f, 1.f);
		cb->params  = XMFLOAT4(rotation_y, rotation_x, view_scale, 0.f);
		g_ctx->Unmap(g_cbuf, 0);

		g_ctx->IASetInputLayout(g_layout);
		g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		UINT stride = sizeof(Vertex), offset = 0;
		g_ctx->IASetVertexBuffers(0, 1, &g_vbuf, &stride, &offset);
		g_ctx->VSSetShader(g_vs, nullptr, 0);
		g_ctx->PSSetShader(g_ps, nullptr, 0);
		// Disable the rest of the programmable pipeline that the host app may
		// have left bound (geometry/hull/domain shaders, stream output).
		g_ctx->GSSetShader(nullptr, nullptr, 0);
		g_ctx->HSSetShader(nullptr, nullptr, 0);
		g_ctx->DSSetShader(nullptr, nullptr, 0);
		ID3D11Buffer* null_so[1] = { nullptr };
		UINT null_off[1] = { 0 };
		g_ctx->SOSetTargets(1, null_so, null_off);
		// Unbind any index buffer the previous frame may have left.
		g_ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
		g_ctx->VSSetConstantBuffers(0, 1, &g_cbuf);
		g_ctx->PSSetConstantBuffers(0, 1, &g_cbuf);
		g_ctx->PSSetSamplers(0, 1, &g_sampler);

		// Per-primitive draw ? each .glb material chunk gets its own
		// base color SRV. OBJ / cube paths push a single primitive with a
		// null SRV so the default white texture is bound (= white chams).
		if (!g_primitives.empty())
		{
			for (auto& p : g_primitives)
			{
				ID3D11ShaderResourceView* srv = p.basecolor_srv ? p.basecolor_srv : g_default_white_srv;
				g_ctx->PSSetShaderResources(0, 1, &srv);
				g_ctx->Draw(p.vertex_count, p.start_vertex);
			}
		}
		else
		{
			g_ctx->PSSetShaderResources(0, 1, &g_default_white_srv);
			g_ctx->Draw(g_vertex_count, 0);
		}

	}

	void Shutdown()
	{
		release_primitives();
		release_core_resources();
	}

	ImTextureID GetTexture() { return (ImTextureID)g_srv; }
	bool IsLoaded() { return g_loaded; }
	int  GetTriangleCount() { return (int)(g_vertex_count / 3); }



	 
	// Skeleton overlay ? loads only the armature/joints from a .glb and
	// renders bones as 2D ImGui lines on top of the model preview. Avoids
	// the 3D pipeline entirely (which previously had trouble with this
	// project's .glb geometry accessors), so it just needs the joint
	// positions and the parent index per joint.
	 
	int GetBoneCount()
	{
		return g_bones.empty() ? 16 : static_cast<int>(g_bones.size());
	}

	// Internal: extract joint hierarchy + bind-pose positions from an
	// already-parsed cgltf_data and populate g_bones. Used by both file
	// and memory entry points.
	static bool finish_bones_load(cgltf_data* data)
	{
		g_bones.clear();

		if (data->skins_count == 0)
			return false;
		cgltf_skin* skin = &data->skins[0];

		// Prefer inverse-bind matrices: joint origins in mesh bind space
		// (same space as unskinned POSITION attributes we render).
		const bool use_ibm =
			skin->inverse_bind_matrices != nullptr &&
			skin->inverse_bind_matrices->count >= skin->joints_count;

		auto invert4_col_major = [](const float in[16], float out[16]) -> bool
		{
			// in/out are glTF column-major. Gauss-Jordan on row form.
			float a[4][8]{};
			for (int c = 0; c < 4; ++c)
				for (int r = 0; r < 4; ++r)
				{
					a[r][c] = in[c * 4 + r];
					a[r][c + 4] = (r == c) ? 1.f : 0.f;
				}
			for (int col = 0; col < 4; ++col)
			{
				int pivot = col;
				float best = fabsf(a[col][col]);
				for (int row = col + 1; row < 4; ++row)
				{
					const float v = fabsf(a[row][col]);
					if (v > best) { best = v; pivot = row; }
				}
				if (best < 1e-12f)
					return false;
				if (pivot != col)
					for (int j = 0; j < 8; ++j)
						std::swap(a[col][j], a[pivot][j]);
				const float diag = a[col][col];
				for (int j = 0; j < 8; ++j)
					a[col][j] /= diag;
				for (int row = 0; row < 4; ++row)
				{
					if (row == col)
						continue;
					const float f = a[row][col];
					for (int j = 0; j < 8; ++j)
						a[row][j] -= f * a[col][j];
				}
			}
			for (int c = 0; c < 4; ++c)
				for (int r = 0; r < 4; ++r)
					out[c * 4 + r] = a[r][c + 4];
			return true;
		};

		for (size_t i = 0; i < skin->joints_count; ++i)
		{
			cgltf_node* joint = skin->joints[i];
			XMFLOAT3 pos(0.f, 0.f, 0.f);

			bool got_ibm = false;
			if (use_ibm)
			{
				cgltf_float ibm[16]{};
				cgltf_float bind[16]{};
				if (cgltf_accessor_read_float(
						skin->inverse_bind_matrices, i, ibm, 16) &&
					invert4_col_major(ibm, bind))
				{
					// Column-vector bind * (0,0,0,1) → column 3.
					pos = XMFLOAT3(bind[12], bind[13], bind[14]);
					got_ibm = true;
				}
			}
			if (!got_ibm)
			{
				cgltf_float m[16];
				cgltf_node_transform_world(joint, m);
				pos = XMFLOAT3(m[12], m[13], m[14]);
			}

			int parent_idx = -1;
			cgltf_node* p = joint->parent;
			while (p)
			{
				for (size_t j = 0; j < skin->joints_count; ++j)
				{
					if (skin->joints[j] == p) { parent_idx = (int)j; break; }
				}
				if (parent_idx >= 0) break;
				p = p->parent;
			}

			g_bones.push_back({ pos, parent_idx, 0 });
		}

		if (g_bones.empty())
			return false;

		for (size_t i = 0; i < g_bones.size(); ++i)
		{
			int d = 0;
			int p = g_bones[i].parent;
			while (p >= 0)
			{
				++d;
				p = g_bones[p].parent;
			}
			g_bones[i].depth = d;
		}

		// Same normalize as finish_glb_load / mesh: full Y bbox (not
		// percentile), XZ centroid, mid-Y center, scale 1.7/ext_y, no y_offset.
		XMFLOAT3 mn = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
		XMFLOAT3 mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (auto& b : g_bones)
		{
			mn.x = (std::min)(mn.x, b.pos.x);
			mn.y = (std::min)(mn.y, b.pos.y);
			mn.z = (std::min)(mn.z, b.pos.z);
			mx.x = (std::max)(mx.x, b.pos.x);
			mx.y = (std::max)(mx.y, b.pos.y);
			mx.z = (std::max)(mx.z, b.pos.z);
		}

		XMFLOAT3 centroid = { 0.f, 0.f, 0.f };
		{
			double sx = 0.0, sz = 0.0;
			for (auto& b : g_bones) { sx += b.pos.x; sz += b.pos.z; }
			const double inv_n = 1.0 / (double)g_bones.size();
			centroid.x = (float)(sx * inv_n);
			centroid.z = (float)(sz * inv_n);
		}
		const XMFLOAT3 center = { centroid.x, (mn.y + mx.y) * 0.5f, centroid.z };
		for (auto& b : g_bones)
		{
			b.pos.x -= center.x; b.pos.y -= center.y; b.pos.z -= center.z;
		}

		float ext_y = mx.y - mn.y;
		if (ext_y < 1e-4f) ext_y = 1.f;
		const float scale = 1.7f / ext_y;
		for (auto& b : g_bones)
		{
			b.pos.x *= scale; b.pos.y *= scale; b.pos.z *= scale;
		}

		return true;
	}

	bool LoadBonesGlb(const std::string& path)
	{
		cgltf_options options = {};
		cgltf_data*   data    = nullptr;
		if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
			return false;
		if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
		{
			cgltf_free(data);
			return false;
		}
		bool ok = finish_bones_load(data);
		cgltf_free(data);
		return ok;
	}

	bool LoadBonesGlbFromMemory(const void* glb_bytes, size_t glb_size)
	{
		if (!glb_bytes || glb_size == 0) return false;

		cgltf_options options = {};
		cgltf_data*   data    = nullptr;
		if (cgltf_parse(&options, glb_bytes, glb_size, &data) != cgltf_result_success)
			return false;
		if (cgltf_load_buffers(&options, data, nullptr) != cgltf_result_success)
		{
			cgltf_free(data);
			return false;
		}
		bool ok = finish_bones_load(data);
		cgltf_free(data);
		return ok;
	}

	int ProjectCanonicalBones(
		ImVec2 img_min,
		ImVec2 img_max,
		float rot_y,
		float rot_x,
		float view_scale,
		ImVec2* output,
		bool* available,
		int capacity) noexcept
	{
		const int count = static_cast<int>(
			RagePlayerNames::BoneSlot::Count);
		if (!output || !available || capacity < count)
			return 0;
		for (int index = 0; index < count; ++index)
		{
			output[index] = ImVec2{};
			available[index] = false;
		}
		if (g_bones.size() != static_cast<size_t>(count))
			return 0;

		const float width = img_max.x - img_min.x;
		const float height = img_max.y - img_min.y;
		if (!std::isfinite(img_min.x) ||
			!std::isfinite(img_min.y) ||
			!std::isfinite(img_max.x) ||
			!std::isfinite(img_max.y) ||
			!std::isfinite(rot_y) ||
			!std::isfinite(rot_x) ||
			!std::isfinite(view_scale) ||
			width <= 0.f ||
			height <= 0.f)
		{
			return 0;
		}

		const float resolved_view_scale =
			view_scale > 0.f ? view_scale : 1.f;
		const float base_yaw = 3.14159265f;
		const float cy = cosf(rot_y + base_yaw);
		const float sy = sinf(rot_y + base_yaw);
		const float cx = cosf(rot_x);
		const float sx = sinf(rot_x);
		const float scale_x =
			2.38f * resolved_view_scale;
		const float scale_y =
			1.18f * resolved_view_scale;
		int projected_count = 0;
		for (int index = 0; index < count; ++index)
		{
			const XMFLOAT3& point =
				g_bones[static_cast<size_t>(index)].pos;
			const float x1 =
				cy * point.x + sy * point.z;
			const float y1 = point.y;
			const float z1 =
				-sy * point.x + cy * point.z;
			const float y2 =
				cx * y1 - sx * z1;
			const float ndc_x = x1 * scale_x;
			const float ndc_y = y2 * scale_y;
			const ImVec2 projected(
				img_min.x +
					(ndc_x * 0.5f + 0.5f) *
					width,
				img_max.y -
					(ndc_y * 0.5f + 0.5f) *
					height);
			if (!std::isfinite(projected.x) ||
				!std::isfinite(projected.y))
			{
				continue;
			}
			output[index] = projected;
			available[index] = true;
			++projected_count;
		}
		return projected_count;
	}

	bool ProjectCanonicalBones(
		ImVec2 img_min,
		ImVec2 img_max,
		float rot_y,
		float rot_x,
		float view_scale,
		ImVec2* output,
		int capacity) noexcept
	{
		const int count = static_cast<int>(
			RagePlayerNames::BoneSlot::Count);
		bool available[count]{};
		if (ProjectCanonicalBones(
				img_min,
				img_max,
				rot_y,
				rot_x,
				view_scale,
				output,
				available,
				capacity) != count)
		{
			return false;
		}
		return EspSkeleton::ValidateProjected(
			output,
			available,
			true);
	}

	void DrawSkeletonOverlay(ImDrawList* dl,
	                           ImVec2 img_min, ImVec2 img_max,
	                           float  rot_y,   float  rot_x,
	                           ImU32  color,   float  thickness,
	                           float  view_scale)
	{
		if (!dl) return;

		// Same rotation pipeline as the vertex shader (180? base yaw, then
		// user yaw, then pitch around X), then orthographic XY projection
		// with the same anisotropic scale the shader uses (X stretched to
		// compensate for the portrait panel display) so the skeleton tracks
		// the rendered model exactly.
		const float base_yaw = 3.14159265f;
		const float cy       = cosf(rot_y + base_yaw);
		const float sy       = sinf(rot_y + base_yaw);
		const float cx       = cosf(rot_x);
		const float sx       = sinf(rot_x);
		const float scale_x  = 2.38f * view_scale;
		const float scale_y  = 1.18f * view_scale;

		auto project = [&](const XMFLOAT3& p) -> ImVec2
		{
			float x1 = cy * p.x + sy * p.z;
			float y1 = p.y;
			float z1 = -sy * p.x + cy * p.z;
			float x2 = x1;
			float y2 = cx * y1 - sx * z1;
			float ndc_x = x2 * scale_x;
			float ndc_y = y2 * scale_y;
			return ImVec2(
				img_min.x + (ndc_x * 0.5f + 0.5f) * (img_max.x - img_min.x),
				img_max.y - (ndc_y * 0.5f + 0.5f) * (img_max.y - img_min.y));
		};

		const float dot_r = (std::max)(thickness * 0.9f, 1.0f);

		// Baked anatomical subset (pelvis/spine/neck/head/limbs) — same
		// topology as Visuals ESP, not the full hair/finger/skirt armature.
		if (g_bones.size() ==
			static_cast<size_t>(
				RagePlayerNames::BoneSlot::Count))
		{
			ImVec2 pts[static_cast<int>(
				RagePlayerNames::BoneSlot::Count)]{};
			bool valid[static_cast<int>(
				RagePlayerNames::BoneSlot::Count)]{};
			if (ProjectCanonicalBones(
					img_min,
					img_max,
					rot_y,
					rot_x,
					view_scale,
					pts,
					static_cast<int>(
						RagePlayerNames::BoneSlot::Count)))
			{
				for (bool& value : valid)
					value = true;
				EspSkeleton::DrawProjected(
					dl,
					pts,
					valid,
					color,
					thickness);
				for (const ImVec2& point : pts)
					dl->AddCircleFilled(
						point,
						dot_r,
						color,
						8);
				return;
			}
		}

		// Fallback stick figure when bones failed to load.
		enum
		{
			J_HEAD, J_NECK, J_CHEST, J_PELVIS,
			J_L_SHO, J_L_ELB, J_L_WRI,
			J_R_SHO, J_R_ELB, J_R_WRI,
			J_L_HIP, J_L_KNE, J_L_ANK,
			J_R_HIP, J_R_KNE, J_R_ANK,
			J_COUNT
		};

		static const XMFLOAT3 joints[J_COUNT] =
		{
			{  0.00f,  0.55f, 0.f },
			{  0.00f,  0.42f, 0.f },
			{  0.00f,  0.25f, 0.f },
			{  0.00f, -0.07f, 0.f },
			{ -0.13f,  0.35f, 0.f },
			{ -0.22f,  0.08f, 0.f },
			{ -0.22f, -0.10f, 0.f },
			{  0.13f,  0.35f, 0.f },
			{  0.22f,  0.08f, 0.f },
			{  0.22f, -0.10f, 0.f },
			{ -0.10f, -0.12f, 0.f },
			{ -0.11f, -0.45f, 0.f },
			{ -0.11f, -0.72f, 0.f },
			{  0.10f, -0.12f, 0.f },
			{  0.11f, -0.45f, 0.f },
			{  0.11f, -0.72f, 0.f },
		};

		ImVec2 pts[J_COUNT];
		bool valid[J_COUNT];
		for (int i = 0; i < J_COUNT; ++i)
		{
			pts[i] = project(joints[i]);
			valid[i] = true;
		}
		EspSkeleton::DrawProjected(
			dl,
			pts,
			valid,
			color,
			thickness);

		for (int i = 0; i < J_COUNT; ++i)
			dl->AddCircleFilled(pts[i], dot_r, color, 8);
	}

namespace
{
    std::wstring GenerateRandom12CharFolderName()
    {
        static const wchar_t charset[] = L"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        wchar_t randFolder[13]{};
        LARGE_INTEGER pc{};
        QueryPerformanceCounter(&pc);
        UINT seed = GetTickCount() ^ static_cast<UINT>(pc.QuadPart);
        for (int i = 0; i < 12; ++i)
        {
            seed = seed * 1103515245 + 12345;
            randFolder[i] = charset[(seed >> 16) % (sizeof(charset) / sizeof(wchar_t) - 1)];
        }
        randFolder[12] = L'\0';
        return randFolder;
    }
}

std::string ResolveDefaultModelPath()
{
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0)
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring dir(modulePath);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        dir.resize(slash + 1);

    const std::wstring candidates[] = {
        dir + L"assets\\esp_preview\\model.glb",
        dir + L"esp_preview\\model.glb",
        dir + L"nekomiya_mana_zzz.glb",
        L"nekomiya_mana_zzz.glb",
        L"D:\\Сlear\\nekomiya_mana_zzz.glb",
        L"assets\\esp_preview\\model.glb",
        L"project\\assets\\esp_preview\\model.glb",
        L"project\\build\\assets\\esp_preview\\model.glb",
        L"..\\..\\project\\assets\\esp_preview\\model.glb",
        L"D:\\Сlear\\working\\RAGEMP_SP\\project\\assets\\esp_preview\\model.glb",
        L"D:\\Сlear\\working\\RAGEMP_SP\\project\\build\\assets\\esp_preview\\model.glb",
    };
    for (const auto& c : candidates)
    {
        if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, c.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string out(n > 0 ? static_cast<size_t>(n - 1) : 0, '\0');
            if (n > 1)
                WideCharToMultiByte(CP_UTF8, 0, c.c_str(), -1, out.data(), n, nullptr, nullptr);
            return out;
        }
    }
    return "";
}

bool LoadDefaultAsset()
{
    if (!g_inited)
        return false;
    if (g_loaded)
        return true;
    if (g_load_failed || g_load_loading)
        return false;

    g_load_loading = true;
    g_load_attempted = true;

    // Timed here rather than at a call site: there are two of them, and the
    // one that actually triggers the load is the preview, not the renderer.
    // This runs on the render thread, so however long it takes is a frame
    // nobody gets, and a player reported the menu dropping to a handful of
    // frames on hardware that should never drop any.
    LARGE_INTEGER loadStart{};
    QueryPerformanceCounter(&loadStart);
    struct LoadTimer final
    {
        LARGE_INTEGER start;
        ~LoadTimer() noexcept
        {
            LARGE_INTEGER end{};
            LARGE_INTEGER freq{};
            QueryPerformanceCounter(&end);
            QueryPerformanceFrequency(&freq);
            if (!freq.QuadPart)
                return;
            char line[144];
            const double ms =
                static_cast<double>(end.QuadPart - start.QuadPart) *
                1000.0 / static_cast<double>(freq.QuadPart);
            sprintf_s(
                line,
                "ModelPreview: mesh load blocked the render thread "
                "for %.0f ms",
                ms);
            hooks::HLog(line);
        }
    } loadTimer{loadStart};

    // BMS2 blob lives in PE RCDATA (ModelPreviewMesh.rc) — avoids compiling
    // a multi-MB constexpr array that OOMs MSVC.
    constexpr uint32_t kMagic = embedded_model_preview_mesh::kMagic;

    // Header is wiped after DllMain, so the resource table is unreachable
    // through FindResourceW — use the pointer cached before the wipe.
    const uint8_t* bytes = g_baked_res_data;
    size_t size = g_baked_res_size;
    if (!bytes)
    {
        // Fallback for builds where the cache was never primed.
        const HMODULE self = reinterpret_cast<HMODULE>(&__ImageBase);
        const HRSRC res = FindResourceW(
            self, MAKEINTRESOURCEW(IDR_MODEL_PREVIEW_MESH), RT_RCDATA);
        if (res)
        {
            const HGLOBAL loaded = LoadResource(self, res);
            size = SizeofResource(self, res);
            bytes = static_cast<const uint8_t*>(LockResource(loaded));
        }
    }
    if (!bytes || size < 16)
    {
        hooks::HLog("ModelPreview: baked mesh resource unavailable");
        g_load_loading = false;
        g_load_failed = true;
        return false;
    }

    uint32_t magic = 0, vert_count = 0, prim_count = 0, tex_count = 0;
    uint32_t bone_count = 0;
    memcpy(&magic, bytes, 4);
    memcpy(&vert_count, bytes + 4, 4);
    memcpy(&prim_count, bytes + 8, 4);
    memcpy(&tex_count, bytes + 12, 4);
    const bool is_bms3 = (magic == kMagic); // 'BMS3'
    const bool is_bms2 = (magic == 0x32534D42u); // legacy 'BMS2'
    size_t header_bytes = 16u;
    if (is_bms3)
    {
        if (size < 20u)
        {
            hooks::HLog("ModelPreview: baked BMS3 header truncated");
            g_load_loading = false;
            g_load_failed = true;
            return false;
        }
        memcpy(&bone_count, bytes + 16, 4);
        header_bytes = 20u;
    }
    char head[192];
    _snprintf(head, sizeof(head),
        "ModelPreview: baked magic=0x%08X verts=%u prims=%u tex=%u bones=%u size=%zu",
        magic, vert_count, prim_count, tex_count, bone_count, size);
    hooks::HLog(head);

    if ((!is_bms3 && !is_bms2) || vert_count == 0 || vert_count > 500000u ||
        prim_count == 0 || prim_count > 256u || tex_count > 64u ||
        bone_count > 512u)
    {
        hooks::HLog("ModelPreview: baked header invalid");
        g_load_loading = false;
        g_load_failed = true;
        return false;
    }

    const size_t verts_bytes =
        static_cast<size_t>(vert_count) * sizeof(Vertex);
    const size_t prims_bytes =
        static_cast<size_t>(prim_count) * sizeof(uint32_t) * 3u;
    size_t cursor = header_bytes + verts_bytes + prims_bytes;
    if (cursor > size)
    {
        hooks::HLog("ModelPreview: baked size mismatch");
        g_load_loading = false;
        g_load_failed = true;
        return false;
    }

    const auto* verts =
        reinterpret_cast<const Vertex*>(bytes + header_bytes);
    const auto* prims =
        reinterpret_cast<const uint32_t*>(
            bytes + header_bytes + verts_bytes);

    release_primitives();
    if (g_vbuf)
    {
        g_vbuf->Release();
        g_vbuf = nullptr;
    }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = static_cast<UINT>(verts_bytes);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = verts;
    const HRESULT hr = g_device->CreateBuffer(&bd, &sd, &g_vbuf);
    if (FAILED(hr) || !g_vbuf)
    {
        char line[96];
        _snprintf(line, sizeof(line),
            "ModelPreview: baked CreateBuffer FAILED hr=0x%08X",
            (unsigned)hr);
        hooks::HLog(line);
        g_load_loading = false;
        g_load_failed = true;
        return false;
    }

    std::vector<ID3D11ShaderResourceView*> tex_srvs(tex_count, nullptr);
    for (uint32_t ti = 0; ti < tex_count; ++ti)
    {
        if (cursor + 12 > size)
            break;
        uint32_t w = 0, h = 0, nbytes = 0;
        memcpy(&w, bytes + cursor, 4);
        memcpy(&h, bytes + cursor + 4, 4);
        memcpy(&nbytes, bytes + cursor + 8, 4);
        cursor += 12;
        if (w == 0 || h == 0 || w > 4096 || h > 4096 ||
            nbytes != w * h * 4u || cursor + nbytes > size)
        {
            hooks::HLog("ModelPreview: baked tex header bad");
            break;
        }
        const auto* rgba = bytes + cursor;
        cursor += nbytes;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 0; // full chain
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_device->CreateTexture2D(&td, nullptr, &tex)) && tex)
        {
            g_ctx->UpdateSubresource(tex, 0, nullptr, rgba, w * 4u, 0);
            if (SUCCEEDED(g_device->CreateShaderResourceView(
                    tex, nullptr, &tex_srvs[ti])) &&
                tex_srvs[ti])
            {
                g_ctx->GenerateMips(tex_srvs[ti]);
            }
            tex->Release();
        }
    }

    g_primitives.reserve(prim_count);
    for (uint32_t i = 0; i < prim_count; ++i)
    {
        const UINT start = prims[i * 3u];
        const UINT count = prims[i * 3u + 1u];
        const uint32_t tex_i = prims[i * 3u + 2u];
        if (start + count > vert_count)
            continue;
        ID3D11ShaderResourceView* srv = nullptr;
        if (tex_i != 0xFFFFFFFFu && tex_i < tex_count)
            srv = tex_srvs[tex_i];
        // Primitives share textures — AddRef so release_primitives is safe.
        if (srv)
            srv->AddRef();
        g_primitives.push_back({ start, count, srv });
    }

    for (auto* srv : tex_srvs)
    {
        if (srv)
            srv->Release();
    }

    // BMS3: bones already normalized with the mesh (IBM bind space).
    g_bones.clear();
    if (is_bms3 && bone_count > 0)
    {
        const size_t bones_bytes =
            static_cast<size_t>(bone_count) * (sizeof(float) * 3u + sizeof(int32_t));
        if (cursor + bones_bytes <= size)
        {
            g_bones.reserve(bone_count);
            for (uint32_t i = 0; i < bone_count; ++i)
            {
                float x = 0.f, y = 0.f, z = 0.f;
                int32_t parent = -1;
                memcpy(&x, bytes + cursor, 4); cursor += 4;
                memcpy(&y, bytes + cursor, 4); cursor += 4;
                memcpy(&z, bytes + cursor, 4); cursor += 4;
                memcpy(&parent, bytes + cursor, 4); cursor += 4;
                if (parent < -1 ||
                    parent >= static_cast<int32_t>(bone_count))
                    parent = -1;
                g_bones.push_back({ XMFLOAT3{ x, y, z }, parent, 0 });
            }
            for (size_t i = 0; i < g_bones.size(); ++i)
            {
                int d = 0;
                int p = g_bones[i].parent;
                while (p >= 0 && d < 64)
                {
                    ++d;
                    p = g_bones[static_cast<size_t>(p)].parent;
                }
                g_bones[i].depth = d;
            }
        }
        else
        {
            hooks::HLog("ModelPreview: baked bones truncated");
        }
    }

    g_vertex_count = vert_count;
    g_loaded = !g_primitives.empty();
    g_load_loading = false;
    g_load_failed = !g_loaded;
    {
        char line[128];
        _snprintf(line, sizeof(line),
            "ModelPreview: baked mesh %s (texSRVs=%u bones=%zu)",
            g_loaded ? "loaded" : "FAILED",
            tex_count,
            g_bones.size());
        hooks::HLog(line);
    }
    return g_loaded;
}

void TickApplyCloudAsset() {}
void RetryCloudDownload()
{
    if (g_loaded || g_load_loading)
        return;
    g_load_failed = false;
    g_load_attempted = false;
}
bool IsLoading() { return g_load_loading; }
bool IsLoadFailed() { return g_load_failed; }
bool IsReady() { return g_inited && core_resources_ready(); }

void Render(float rotationY, float rotationX, float viewScale)
{
    const bool unchanged =
        !g_render_dirty &&
        g_loaded &&
        std::fabs(rotationY - g_last_rotation_y) < 0.0001f &&
        std::fabs(rotationX - g_last_rotation_x) < 0.0001f &&
        std::fabs(viewScale - g_last_view_scale) < 0.0001f;
    if (unchanged)
        return;
    RenderInternal(rotationY, rotationX, false, 0, viewScale);
    if (g_loaded)
    {
        g_last_rotation_y = rotationY;
        g_last_rotation_x = rotationX;
        g_last_view_scale = viewScale;
        g_render_dirty = false;
    }
}

}
