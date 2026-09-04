#define IMGUI_DEFINE_MATH_OPERATORS

#include <Windows.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "leyawin_globals.h"
#include "leyawin_safe.h"
#include <cmath>
#include <algorithm>

#include "../MenuMotion.h"
#include "../MenuTheme.h"
#include "../MenuType.h"
#include "../IconsFontAwesome6.h"

class c_widgets
{
public:
    // Legacy call sites pass this straight into ImLerp. It used to be
    // speed / io.Framerate, which made every animation run at a different
    // speed per framerate and overshoot below ~30 fps. Same perceived speed,
    // now frame-rate independent and clamped.
    float                   fixed_speed(float speed) {
        if (MenuMotion::ReducedMotion())
            return 1.f;
        const float dt = MenuMotion::Delta();
        if (!(dt > 0.f) || !(speed > 0.f))
            return 0.f;
        return 1.f - std::exp(-speed * dt);
    };

    ImU32                   get_clr(const ImVec4& col, float alpha = 1.f);

    void                    text_colored(const ImU32 col, const char* text);
    void                    set_cursor_pos(const ImVec2& local_pos);
    void                    begin_group();
    void                    end_group();
    void                    push_style_var(ImGuiStyleVar idx, const ImVec2& val);
    void                    push_style_var(ImGuiStyleVar idx, const float val);
    void                    pop_style_var(int num = 1);

    void                    sameline();
    void                    render_text_clipped(ImFont* font, const ImVec2& p_min, const ImVec2& p_max, ImU32 col, const char* text, const ImVec2& align);
    void                    end();
    void                    end_child();
    void                    begin_content(const ImVec2& size_arg = ImVec2(0, 0), ImGuiWindowFlags extra_flags = 0);
    void                    end_content();
    void					add_rect_filled_multi_color(ImDrawList* draw, const ImVec2& p_min, const ImVec2& p_max, ImU32 col_upr_left, ImU32 col_upr_right, ImU32 col_bot_right, ImU32 col_bot_left, float rounding = 0.f, ImDrawFlags flags = 0);

    bool                    keybind(const char* label, int* key, const char* icon = nullptr);

    // Printable name of a virtual key, for callers that show a bind next to the
    // control it belongs to. Returns nullptr when there is nothing to show.
    const char*             key_name(int vk);

    bool                    dropdown(const char* label, int* current_item, const char* const items[], int items_count, int max_count = 5);
    void                    multi_dropdown(const char* label, bool variable[], const char* labels[], int count, int max_count = 5);

    void                    separator();

    bool                    begin_popup_w(const char* name, float size_x, int open_mouse_button = 1);
    bool                    is_popup_w_open(const char* name);
    void                    open_popup_w(const char* name);
    void                    open_popup_w(const char* name, const ImVec2& tl, const ImVec2& br);
    void                    close_popup_w(const char* name);
    void                    dismiss_popup_w(const char* name);
    void                    end_popup_w();
    void                    bring_popups_to_front();

    bool                    selectable_ex(const char* label, bool active);
    bool                    sub_selection(const char* label, int selection_id, int& selection_variable, const ImVec2& size);
    bool                    sub_selection(const char* icon_label, const char* label, int selection_id, int& selection_variable, const ImVec2& size);
    bool                    button(const char* label, const ImVec2& size_arg);
    bool                    checkbox(const char* label, bool* callback, bool popup = false, int* key = nullptr, int* mode = nullptr, bool* show_in_binds = nullptr, bool bindable = true);
    bool                    checkbox_with_picker(const char* label, bool* callback, float col[3], ImGuiColorEditFlags flags, bool bindable = true);
    bool                    text_field(const char* label, const char* hint, const ImVec2& size, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = 0, void* user_data = 0);
    bool                    color_edit(const char* label, float col[4], ImGuiColorEditFlags flags = 0, bool popup = false);
    void                    multi_dropdown_icon(const char* icon, const char* label, bool variable[], const char* labels[], int count, int max_count);
    bool                    begin(const char* name, bool* open, ImGuiWindowFlags flags = ImGuiWindowFlags_None);
    bool                    begin_child(const char* icon, const char* name, const ImVec2& size_arg = ImVec2(0, 0));
    bool                    slider_float(const char* label, float* v, float v_min, float v_max, const char* format = "%.2f", ImGuiSliderFlags flags = 0);
    bool                    slider_float_icon(const char* icon, const char* label, float* v, float v_min, float v_max, const char* format = "%.2f", ImGuiSliderFlags flags = 0);
    bool                    slider_int(const char* label, int* v, int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0);
    bool                    selection(const char* icon_label, const char* label, int selection_id, int& selection_variable, const ImVec2& size);
    bool                    nav_rail_item(const char* id, const char* icon_label, const char* tooltip, int selection_id, int& selection_variable, const ImVec2& size);
    bool                    color_picker(const char* label, float col[4], ImGuiColorEditFlags flags, const float* ref_col);
    int                     config_panel(const char* name, const char* modified, const char* author, int config_id, int& config_variable, const ImVec2& size);
};

inline c_widgets g_LeyawinWidgets;
inline c_widgets* gui = &g_LeyawinWidgets;

struct notify_struct {
    int id = 0;
    char message[512] = {};
    char information[64] = {};
    ULONGLONG startTick = 0;
    ULONGLONG endTick = 0;
    ImU32 color = 0;
    bool active = false;
    float alpha = 0.f;
    float slide = 72.f;
};

class c_notify {
public:
    static constexpr int kMaxNotifications = 8;

    void AddNotification(const char* information, const char* message, int durationMs, ImU32 color)
    {
        if (!information || !message)
            return;
        if (count >= kMaxNotifications) {
            for (int i = 1; i < count; ++i)
                items[i - 1] = items[i];
            --count;
        }
        notify_struct& n = items[count++];
        n = {};
        n.id = notificationIdCounter++;
        lstrcpynA(
            n.information,
            information,
            static_cast<int>(sizeof(n.information)));
        lstrcpynA(
            n.message,
            message,
            static_cast<int>(sizeof(n.message)));
        n.startTick = GetTickCount64();
        n.endTick = n.startTick +
            static_cast<ULONGLONG>(
                durationMs > 500 ? durationMs : 500);
        n.color = color;
        n.active = true;
        n.alpha = 0.f;
        n.slide = 72.f;
    }

    void DrawNotifications()
    {
        const ULONGLONG now = GetTickCount64();
        float dt = ImGui::GetIO().DeltaTime;
        if (dt <= 0.f || dt > 0.25f)
            dt = 1.f / 60.f;
        const float blend = 1.f - std::exp(-14.f * dt);
        const float scale = MenuTheme::Scaled(1.f);
        // Toasts size themselves to their text, so the stack has to accumulate
        // real heights instead of assuming one fixed row.
        float stackY = 16.f * scale;
        int write = 0;
        for (int i = 0; i < count; ++i) {
            notify_struct& n = items[i];
            if (now >= n.endTick)
                n.active = false;
            const float targetAlpha = n.active ? 1.f : 0.f;
            const float targetSlide = n.active ? 0.f : 72.f;
            n.alpha += (targetAlpha - n.alpha) * blend;
            n.slide += (targetSlide - n.slide) * blend;
            const ULONGLONG duration = n.endTick - n.startTick;
            const ULONGLONG elapsed =
                now > n.startTick ? now - n.startTick : 0;
            float percentage = duration > 0 && elapsed < duration
                ? static_cast<float>(duration - elapsed) /
                    static_cast<float>(duration) * 100.f
                : 0.f;
            if (n.alpha > 0.01f) {
                stackY +=
                    ShowNotification(stackY, n, percentage) +
                    10.f * scale;
            }
            if (n.active || n.alpha > 0.015f)
                items[write++] = n;
        }
        count = write;
    }

private:
    notify_struct items[kMaxNotifications] = {};
    int count = 0;
    int notificationIdCounter = 0;

    // Returns the height it occupied so the caller can stack the next one.
    float ShowNotification(
        float stackY,
        const notify_struct& notification,
        float percentage) {
        const MenuTheme::Palette& tokens = MenuTheme::Colors();
        const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
        // Scaled(1) is the plain UI scale; the toast lives outside the shell so
        // it cannot read the shell's own scale variable.
        const float scale = MenuTheme::Scaled(1.f);
        const float alpha = ImClamp(notification.alpha, 0.f, 1.f);
        const float remaining = ImClamp(percentage / 100.f, 0.f, 1.f);

        ImFont* titleFont = MenuType::Title()
            ? MenuType::Title()
            : ImGui::GetFont();
        ImFont* bodyFont = MenuType::Body()
            ? MenuType::Body()
            : ImGui::GetFont();
        ImFont* iconFont = set->icon ? set->icon : ImGui::GetFont();
        const float titleSize = MenuType::TitleSize();
        const float bodySize = MenuType::BodySize();

        const float pad = 13.f * scale;
        const float badge = 30.f * scale;
        const float width = 336.f * scale;
        const float textX = pad + badge + 11.f * scale;
        const float wrapWidth = ImMax(60.f * scale, width - textX - pad);

        const ImVec2 titleExtent = titleFont->CalcTextSizeA(
            titleSize, FLT_MAX, 0.f, notification.information);
        const bool hasMessage = notification.message[0] != '\0';
        const ImVec2 messageExtent = hasMessage
            ? bodyFont->CalcTextSizeA(
                bodySize, FLT_MAX, wrapWidth, notification.message)
            : ImVec2(0.f, 0.f);
        const float textBlock = titleExtent.y +
            (hasMessage ? 4.f * scale + messageExtent.y : 0.f);
        const float height =
            ImMax(textBlock + pad * 2.f, badge + pad * 2.f) + 5.f * scale;

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(
            ImVec2(
                display.x - 18.f * scale + notification.slide,
                stackY),
            ImGuiCond_Always,
            ImVec2(1.f, 0.f));
        ImGui::SetNextWindowSize(
            ImVec2(width, height),
            ImGuiCond_Always);

        char notify_id[32];
        wsprintfA(
            notify_id,
            "##NOTIFY%d",
            notification.id);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::Begin(
            notify_id,
            nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs);
        {
            // Toasts are built before the shell, so without this the shell's
            // own window painted over them.
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            const ImVec2 min = ImGui::GetWindowPos();
            const ImVec2 max = min + ImVec2(width, height);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float radius = metrics.radiusPanel * scale;

            // Toasts float over the game, so they carry their own lift instead
            // of relying on the shell behind them.
            for (int layer = 3; layer >= 1; --layer)
            {
                const float spread = static_cast<float>(layer) * 3.f * scale;
                draw->AddRectFilled(
                    min + ImVec2(-spread * 0.35f, spread * 0.45f),
                    max + ImVec2(spread * 0.35f, spread),
                    ImGui::GetColorU32(
                        IM_COL32(0, 0, 0, static_cast<int>(16.f * alpha))),
                    radius + spread * 0.5f);
            }
            draw->AddRectFilled(
                min,
                max,
                gui->get_clr(tokens.panel, 0.99f),
                radius);
            draw->AddRect(
                min,
                max,
                gui->get_clr(tokens.borderStrong, 1.f),
                radius,
                0,
                metrics.stroke);

            const ImVec4 statusRgba =
                ImGui::ColorConvertU32ToFloat4(notification.color);
            const auto matches =
                [&statusRgba](const ImVec4& other) noexcept {
                    return fabsf(statusRgba.x - other.x) < 0.10f &&
                        fabsf(statusRgba.y - other.y) < 0.10f &&
                        fabsf(statusRgba.z - other.z) < 0.10f;
                };
            const char* glyph = ICON_FA_CIRCLE_INFO;
            if (matches(tokens.error))
                glyph = ICON_FA_CIRCLE_EXCLAMATION;
            else if (matches(tokens.warning))
                glyph = ICON_FA_TRIANGLE_EXCLAMATION;
            else if (matches(tokens.success))
                glyph = ICON_FA_CIRCLE_CHECK;

            const ImVec2 badgeCenter(
                min.x + pad + badge * 0.5f,
                min.y + (height - 5.f * scale) * 0.5f);
            draw->AddCircleFilled(
                badgeCenter,
                badge * 0.5f,
                gui->get_clr(statusRgba, 0.16f),
                24);
            draw->AddCircle(
                badgeCenter,
                badge * 0.5f,
                gui->get_clr(statusRgba, 0.42f),
                24,
                metrics.stroke);
            const float glyphSize = 13.f * scale;
            const ImVec2 glyphExtent = iconFont->CalcTextSizeA(
                glyphSize, FLT_MAX, 0.f, glyph);
            draw->AddText(
                iconFont,
                glyphSize,
                badgeCenter - glyphExtent * 0.5f,
                gui->get_clr(statusRgba, 1.f),
                glyph);

            float textY = min.y +
                ImMax(pad, (height - 5.f * scale - textBlock) * 0.5f);
            draw->AddText(
                titleFont,
                titleSize,
                ImVec2(min.x + textX, textY),
                gui->get_clr(tokens.textPrimary, 1.f),
                notification.information);
            if (hasMessage)
            {
                textY += titleExtent.y + 4.f * scale;
                draw->AddText(
                    bodyFont,
                    bodySize,
                    ImVec2(min.x + textX, textY),
                    gui->get_clr(tokens.textMuted, 1.f),
                    notification.message,
                    nullptr,
                    wrapWidth);
            }

            // Time left reads as a track that empties, inset like a real
            // control instead of a full-bleed hairline on the window edge.
            const float trackY = max.y - 4.f * scale;
            const float trackH = 2.5f * scale;
            draw->AddRectFilled(
                ImVec2(min.x + pad, trackY),
                ImVec2(max.x - pad, trackY + trackH),
                gui->get_clr(tokens.raised, 1.f),
                trackH * 0.5f);
            if (remaining > 0.001f)
            {
                draw->AddRectFilled(
                    ImVec2(min.x + pad, trackY),
                    ImVec2(
                        min.x + pad +
                            (width - pad * 2.f) * remaining,
                        trackY + trackH),
                    gui->get_clr(statusRgba, 0.95f),
                    trackH * 0.5f);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        return height;
    }
};

inline c_notify g_LeyawinNotify;
inline c_notify* notify = &g_LeyawinNotify;

class c_particle {
public:
    struct Particle
    {
        float position[2] = {};
        float velocity[2] = {};
        float lifetime = 0.f;
        bool active = false;
    };

    static constexpr int kMaxParticles = 128;
    static constexpr float M_PI = 3.14159265359f;

    Particle particles[kMaxParticles] = {};
    int particleCount = 0;
    unsigned randSeed = 0xC0FFEEu;

    float NextFloat(float minV, float maxV)
    {
        randSeed = randSeed * 1664525u + 1013904223u;
        const float t = (float)((randSeed >> 8) & 0xFFFF) / 65535.f;
        return minV + (maxV - minV) * t;
    }

    void EmitParticles(const ImVec2& cursorPos)
    {
        const int emitCount = set->particle_num > 0 ? set->particle_num : 1;
        for (int i = 0; i < emitCount; ++i) {
            if (particleCount >= kMaxParticles)
                break;
            Particle& p = particles[particleCount++];
            p.position[0] = cursorPos.x;
            p.position[1] = cursorPos.y;
            const float angle = 2.f * M_PI * ((float)i / (float)emitCount);
            const float speed = NextFloat(100.f, 150.f);
            p.velocity[0] = cosf(angle) * speed;
            p.velocity[1] = sinf(angle) * speed;
            p.lifetime = NextFloat(1.f, 2.f);
            p.active = true;
        }
    }

    void UpdateParticles(float deltaTime)
    {
        const float damping = 0.98f;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();

        for (int i = 0; i < particleCount; ++i) {
            Particle& p = particles[i];
            if (!p.active)
                continue;

            p.velocity[1] += set->particle_gravity * deltaTime;
            p.velocity[0] *= damping;
            p.velocity[1] *= damping;
            p.position[0] += p.velocity[0] * deltaTime;
            p.position[1] += p.velocity[1] * deltaTime;
            p.lifetime -= deltaTime;

            if (p.lifetime <= 0.f) {
                p.active = false;
                continue;
            }

            const float alpha = p.lifetime * 0.5f;
            dl->AddCircleFilled(
                ImVec2(p.position[0], p.position[1]), 1.0f,
                gui->get_clr(ImColor(set->particle_color[0], set->particle_color[1], set->particle_color[2], alpha)));
        }

        int write = 0;
        for (int i = 0; i < particleCount; ++i) {
            if (particles[i].active)
                particles[write++] = particles[i];
        }
        particleCount = write;
    }
};

inline c_particle g_LeyawinParticle;
inline c_particle* particle = &g_LeyawinParticle;
