
#include "security/xorstr.hpp"
#include "overlay.hpp"
#include "../core/globals.hpp"
#include "../features/esp/esp.hpp"
#include "../render/debug_overlay.hpp"
#include "../driver/driver_io.hpp"
#include "../sdk/offsets.hpp"
#include "../memory/memory_access.hpp"

// settings.hpp must come BEFORE loveup headers (defines cfg:: namespace)
#include "../config/settings.hpp"
#include "../security/vmp_markers.hpp"

// Loveup menu system (defines menu_cfg pointer via config.h)
#include "../gui/loveup/headers/includes.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_freetype.h"

// NVIDIA Streamproof (FTS registry + WDA_EXCLUDEFROMCAPTURE + nvcontainer patch)
#include "../security/streamproof.hpp"

#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "freetype.lib")

static ID3D11Device*           g_pd3dDevice           = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext    = nullptr;
static IDXGISwapChain*         g_pSwapChain           = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static UINT                    g_ResizeWidth = 0, g_ResizeHeight = 0;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace overlay
{
    namespace
    {
        struct EnumCtx
        {
            DWORD pid;
            HWND  result;
        };

        BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
        {
            auto* ctx = reinterpret_cast<EnumCtx*>(lParam);

            DWORD wndPid = 0;
            GetWindowThreadProcessId(hwnd, &wndPid);
            if (wndPid != ctx->pid) return TRUE;

            if (!IsWindowVisible(hwnd))    return TRUE;
            if (GetWindow(hwnd, GW_OWNER)) return TRUE;
            if (GetParent(hwnd))           return TRUE;

            RECT rc{};
            if (!GetClientRect(hwnd, &rc)) return TRUE;
            if (rc.right <= 0 || rc.bottom <= 0) return TRUE;

            ctx->result = hwnd;
            return FALSE;
        }
    } 

    HWND FindGameWindow()
    {
        const DWORD pid = static_cast<DWORD>(driver::process_id);
        if (pid == 0) return nullptr;

        EnumCtx ctx{ pid, nullptr };
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
        return ctx.result;
    }

    bool IsGameWindowActive()
    {
        const HWND fg = GetForegroundWindow();
        if (!fg) return false;

        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        return fgPid == static_cast<DWORD>(driver::process_id);
    }

    void Shutdown()
    {
        static std::atomic<bool> is_shutdown(false);
        if (is_shutdown.exchange(true)) return;

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        CleanupDeviceD3D();
    }

    bool GetGameRect(HWND hwnd, RECT* out)
    {
        RECT client;
        if (!GetClientRect(hwnd, &client)) return false;
        POINT tl = { client.left, client.top };
        POINT br = { client.right, client.bottom };
        ClientToScreen(hwnd, &tl);
        ClientToScreen(hwnd, &br);
        out->left = tl.x;
        out->top = tl.y;
        out->right = br.x;
        out->bottom = br.y;
        return true;
    }

    void Render()
    {
        HWND hGame = nullptr;
        // Открываем handle процесса для проверки жив ли он (только по завершению)
        HANDLE hGameProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, (DWORD)driver::process_id);
        
        while (!hGame && g.Run) {
            hGame = FindGameWindow();
            if (!hGame) Sleep(500);
        }
        if (!hGame) return;

        GetGameRect(hGame, &g.GameRect);
        g.GameHwnd = hGame;
        overlay::width  = g.GameRect.right  - g.GameRect.left;
        overlay::height = g.GameRect.bottom - g.GameRect.top;

        WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                           GetModuleHandle(nullptr), nullptr, nullptr,
                           nullptr, nullptr, L"ShadowOverlay", nullptr };
        ::RegisterClassExW(&wc);

        HWND hwnd = ::CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
            wc.lpszClassName, L"ShadowOverlay", WS_POPUP,
            g.GameRect.left, g.GameRect.top, overlay::width, overlay::height,
            hGame, nullptr, wc.hInstance, nullptr);

        if (!hwnd)
        {
            CleanupDeviceD3D();
            ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return;
        }

        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
        const MARGINS margin{ -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margin);

        // ── NVIDIA Streamproof / OBS Bypass ──
        // Инициализируем фоновый сервис (он сам будет следить за галочкой cfg::bypass_obs
        // и на лету прятать/показывать окно, а также применять патчи)
        shadow_streamproof::EnableStreamproof(hwnd);

        if (!CreateDeviceD3D(hwnd))
        {
            CleanupDeviceD3D();
            ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return;
        }

        ::ShowWindow(hwnd, SW_SHOWDEFAULT);
        ::UpdateWindow(hwnd);

        // ---------- ImGui init ----------
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        // NOTE: NavEnableKeyboard intentionally disabled — VK_INSERT is ImGui's
        // default nav-activate key and conflicts with our ToggleMenuKey (Insert).
        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr; // Disable imgui.ini saving

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

        // ---------- Loveup system init ----------
        // Provide DX device pointers to loveup
        var->dx.device     = g_pd3dDevice;
        var->dx.ctx        = g_pd3dDeviceContext;
        var->dx.swap_chain = g_pSwapChain;
        var->winapi.hwnd   = hwnd;

        // Initialize loveup config (menu-internal settings: theme, DPI, etc.)
        menu_cfg->init_config();

        // Initialize fonts via loveup font system (uses FreeType)
        var->gui.dpi_changed = true; // triggers first font build in fonts.cpp

        // Load background / logo textures (optional — will silently fail if files missing)
        widgets->load_texture(xorstr_("bg.png"), &var->img.background);
        widgets->load_texture(xorstr_("logo.png"), &var->img.logo);

        // Notify on successful inject
        notify->add_notify(xorstr_("Successfully injected"), notify_type::success);

        const ImVec4 clear_color(0.f, 0.f, 0.f, 0.f);
        bool done = false;

        while (!done)
        {
            font->update();
            // Toggle menu key
            // Guard: don't toggle while ImGui has keyboard focus (e.g. keybind capture active)
            static bool menu_key_pressed = false;
            bool current_menu_key = (cfg::ToggleMenuKey != 0) &&
                                    ((GetAsyncKeyState(cfg::ToggleMenuKey) & 0x8000) != 0);
            bool imgui_wants_keyboard = ImGui::GetIO().WantCaptureKeyboard;
            if (current_menu_key && !menu_key_pressed && !imgui_wants_keyboard)
                overlay::menu_open = !overlay::menu_open;
            menu_key_pressed = current_menu_key;

            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT) done = true;
            }
            if (done) break;

            // ── Проверяем жив ли ПРОЦЕСС (не окно, не отклик) ───────────────────
            // Выходим ТОЛЬКО если GTA5.exe завершился (ExitCode != STILL_ACTIVE).
            // Зависшая игра — не повод закрываться.
            if (!g.Run)
            {
                break;
            }
            if (hGameProcess)
            {
                DWORD exitCode = STILL_ACTIVE;
                if (GetExitCodeProcess(hGameProcess, &exitCode) && exitCode != STILL_ACTIVE)
                {
                    // Процесс завершился
                    g.Run = false;
                    break;
                }
            }
            else
            {
                // Fallback: хэндл не открылся — проверяем по PID
                if (driver::find_process(L"GTA5.exe") == 0)
                {
                    g.Run = false;
                    break;
                }
            }

            RECT cur;
            if (GetGameRect(hGame, &cur))
            {
                g.GameRect = cur;
                static RECT last_rect = { 0, 0, 0, 0 };
                if (cur.left != last_rect.left || cur.top != last_rect.top ||
                    cur.right != last_rect.right || cur.bottom != last_rect.bottom)
                {
                    const int nw = cur.right - cur.left;
                    const int nh = cur.bottom - cur.top;
                    overlay::width = nw; 
                    overlay::height = nh;
                    SetWindowPos(hwnd, nullptr, cur.left, cur.top, nw, nh,
                                 SWP_NOACTIVATE | SWP_NOZORDER);
                    last_rect = cur;
                }
            }

            if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
            {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight,
                                            DXGI_FORMAT_UNKNOWN, 0);
                g_ResizeWidth = g_ResizeHeight = 0;
                CreateRenderTarget();
            }

            // Update fonts if DPI changed — must happen BEFORE NewFrame().
            // Atlas rebuild (io.Fonts->Clear + CreateDeviceObjects) must finish
            // before ImGui starts a new frame that references font pointers.
            // font->update(); // Called at the beginning of the loop now

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            
            // ---- FOV circle (using cfg:: directly) ----
            int _wcat = cfg::enable_weapon_configs ? cfg::active_weapon_category : 0;
            if (_wcat < 0 || _wcat >= 7) _wcat = 0;
            auto& acfg = cfg::aimbot_configs[_wcat];
            if (acfg.draw_fov) {
                bool shouldDraw = true;
                if (acfg.show_fov_only_aiming) {
                    shouldDraw = (GetAsyncKeyState(cfg::aimkey) & 0x8000) != 0 || (GetAsyncKeyState(cfg::triggerkey) & 0x8000) != 0;
                }
                if (shouldDraw) {
                    ImVec2 center(g.GameRect.right / 2.f, g.GameRect.bottom / 2.f);
                    ImGui::GetBackgroundDrawList()->AddCircle(center, (float)acfg.fov, acfg.col_Fov, 100, 1.0f);
                }
            }
            
            // ---- ESP features ----
            ImFont* esp_font = nullptr;
            if (cfg::ESP_Font == 0) esp_font = font->get(sf_pro, 14);
            else if (cfg::ESP_Font == 1) esp_font = font->get(syne, 14);
            else esp_font = font->get(sf_pro, 14);

            if (esp_font) ImGui::PushFont(esp_font);
            features::RenderESP();
            if (esp_font) ImGui::PopFont();

            // ---- OBS / NVIDIA bypass ----
            static bool last_bypass = false;
            bool current_bypass = cfg::bypass_obs;
            if (last_bypass != current_bypass)
            {
                last_bypass = current_bypass;
                SetWindowDisplayAffinity(hwnd, current_bypass ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
            }

            // ---- Window click-through toggle ----
            // Use full ExStyle replacement (not read-modify-write) — matches the working build.
            // The read-modify-write + HideWindowFromCapture combo was crashing when
            // WDA_EXCLUDEFROMCAPTURE was applied after WS_EX_LAYERED was already stripped.
            static bool last_menu_state = !overlay::menu_open;
            if (last_menu_state != overlay::menu_open)
            {
                if (overlay::menu_open)
                {
                    SetWindowLong(hwnd, GWL_EXSTYLE,
                                  WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
                }
                else
                {
                    SetWindowLong(hwnd, GWL_EXSTYLE,
                                  WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
                    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
                }
                last_menu_state = overlay::menu_open;
                // Re-apply WDA after SetWindowLong resets it
                if (cfg::bypass_obs)
                    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
            }

            // ---- Render menu or notify ----
            if (overlay::menu_open)
            {
                gui->render();
            }
            else
            {
                notify->setup_notify();
                widgets->init_keybinds();
            }

            ImGui::Render();
            const float clr[4] = { clear_color.x * clear_color.w,
                                   clear_color.y * clear_color.w,
                                   clear_color.z * clear_color.w,
                                   clear_color.w };
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            g_pSwapChain->Present(1, 0);
        }

        if (hGameProcess) CloseHandle(hGameProcess);
        Shutdown();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    }
}

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format            = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate       = { 60, 1 };
    sd.Flags                        = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                 = hWnd;
    sd.SampleDesc.Count             = 1;
    sd.Windowed                     = TRUE;
    sd.SwapEffect                   = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, _countof(levels), D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &got, &g_pd3dDeviceContext);

    if (hr == DXGI_ERROR_UNSUPPORTED)
    {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, _countof(levels), D3D11_SDK_VERSION,
            &sd, &g_pSwapChain, &g_pd3dDevice, &got, &g_pd3dDeviceContext);
    }
    if (FAILED(hr)) return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain        = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice        = nullptr; }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (!pBackBuffer) return;
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return TRUE;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth  = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
