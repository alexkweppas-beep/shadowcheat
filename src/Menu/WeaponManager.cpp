#include "WeaponManager.h"
#include "Menu.h"
#include "MenuState.h"
#include "MenuUiHelpers.h"
#include "MenuTexture.h"
#include "MenuType.h"
#include "IconsFontAwesome6.h"
#include "Leyawin/custom_widgets.hpp"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_safe.h"
#include "../SDK/WeaponCatalog.h"
#include "../SDK/WeaponAttachments.h"
#include "../SDK/Game.h"
#include "../Features/QuantTools.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace WeaponManagerUi
{
namespace
{
    int g_Category = 0;
    int g_SelectedIndex = -1;
    int g_DetailTab = 0;
    char g_Search[64]{};
    bool g_AddOpen = false;
    bool g_AddFocus = false;
    float g_AddAnim = 0.f;
    int g_AddCategory = 0;
    int g_AddSelected = -1;
    char g_AddSearch[64]{};
    // Right click builds a batch on top of the left click selection.
    bool g_AddMarked[WeaponCatalog::kWeaponCount]{};
    int g_AddMarkedCount = 0;

    // An aim preset is keyed by aim group, so melee, thrown and launchers have
    // nothing to configure — they never resolve to a group and are kept out of
    // both weapon pickers.
    bool AimableWeapon(const WeaponCatalog::Entry& weapon) noexcept
    {
        return Menu::AimGroupFromWeaponHash(weapon.hash) !=
            static_cast<int>(WeaponGroups::Group::General);
    }

    // Filter index 0 is "All"; the rest are WeaponGroups::Group values, which
    // is what the presets group by. Catalog categories cannot be used here
    // because a few guns are filed under Other.
    constexpr int kAimFilterCount =
        static_cast<int>(WeaponGroups::Group::Count);

    const char* AimFilterLabel(int filter, int lang) noexcept
    {
        if (filter <= 0)
            return lang == 1 ? "Все" : (lang == 2 ? "Усі" : "All");
        return WeaponGroups::GroupLabel(
            static_cast<WeaponGroups::Group>(filter), lang);
    }

    bool MatchesAimFilter(
        const WeaponCatalog::Entry& weapon,
        int filter) noexcept
    {
        return filter <= 0 ||
            Menu::AimGroupFromWeaponHash(weapon.hash) == filter;
    }

    int FirstAimableIndex() noexcept
    {
        for (int i = 0; i < WeaponCatalog::kWeaponCount; ++i)
        {
            if (AimableWeapon(WeaponCatalog::kWeapons[i]))
                return i;
        }
        return 0;
    }

    void ClearAddMarks() noexcept
    {
        if (g_AddMarkedCount <= 0)
            return;
        memset(g_AddMarked, 0, sizeof(g_AddMarked));
        g_AddMarkedCount = 0;
    }

    const char* T(
        const char* en,
        const char* ru,
        const char* ua) noexcept
    {
        return Menu::Translate(en, ru, ua);
    }

    using MenuUi::HelpIcon;
    using MenuUi::LabelWithHelp;
    using MenuUi::CheckWithHelp;
    using MenuUi::SliderWithHelp;
    using MenuUi::SectionTitle;

    void DrawCurveEditor(
        const char* id,
        Menu::AimCurve& curve,
        float ymin,
        float ymax) noexcept
    {
        ImGui::PushID(id);
        if (ymax <= ymin)
            ymax = ymin + 1.f;

        const float w = ImMax(ImGui::GetContentRegionAvail().x, 180.f);
        const float h = 176.f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##curve_pad", ImVec2(w, h));
        const bool itemHovered = ImGui::IsItemHovered();
        const bool itemActive = ImGui::IsItemActive();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float round = 10.f;
        const float leftGutter = 34.f;
        const float bottomGutter = 22.f;
        const float pad = 12.f;
        const ImVec2 cmin(pos.x + pad + leftGutter, pos.y + pad + 6.f);
        const ImVec2 cmax(
            pos.x + w - pad,
            pos.y + h - pad - bottomGutter);

        // Card
        dl->AddRectFilled(
            pos,
            ImVec2(pos.x + w, pos.y + h),
            gui->get_clr(clr->child_bottom_bar, 0.96f),
            round);
        dl->AddRectFilled(
            pos + ImVec2(1.f, 1.f),
            ImVec2(pos.x + w - 1.f, pos.y + h - 1.f),
            gui->get_clr(clr->element_background, 0.55f),
            round - 1.f);
        dl->AddRect(
            pos,
            ImVec2(pos.x + w, pos.y + h),
            gui->get_clr(clr->child_stroke_bar, 0.7f),
            round,
            0,
            1.15f);
        // Accent top edge
        dl->AddRectFilledMultiColor(
            pos + ImVec2(round, 0.f),
            ImVec2(pos.x + w - round, pos.y + 2.f),
            gui->get_clr(clr->accent_color, 0.0f),
            gui->get_clr(clr->accent_color, 0.55f),
            gui->get_clr(clr->accent_color, 0.55f),
            gui->get_clr(clr->accent_color, 0.0f));

        // Plot inset
        dl->AddRectFilled(
            cmin - ImVec2(4.f, 4.f),
            cmax + ImVec2(4.f, 4.f),
            gui->get_clr(clr->combo_color, 0.55f),
            8.f);

        auto YToScreen = [&](float yv) -> float {
            const float ny = ImClamp(
                (yv - ymin) / (ymax - ymin), 0.f, 1.f);
            return ImLerp(cmax.y, cmin.y, ny);
        };
        auto ToScreen = [&](int i, float yv) -> ImVec2 {
            const float nx =
                static_cast<float>(i) /
                static_cast<float>(Menu::kAimCurvePoints - 1);
            return ImVec2(
                ImLerp(cmin.x, cmax.x, nx),
                YToScreen(yv));
        };

        // Soft grid + value labels
        char yLabel[32]{};
        for (int g = 0; g <= 4; ++g)
        {
            const float t = static_cast<float>(g) / 4.f;
            const float y = ImLerp(cmax.y, cmin.y, t);
            const float x = ImLerp(cmin.x, cmax.x, t);
            const bool major = (g == 0 || g == 2 || g == 4);
            dl->AddLine(
                ImVec2(cmin.x, y),
                ImVec2(cmax.x, y),
                gui->get_clr(clr->text, major ? 0.16f : 0.08f),
                1.f);
            dl->AddLine(
                ImVec2(x, cmin.y),
                ImVec2(x, cmax.y),
                gui->get_clr(clr->text, major ? 0.14f : 0.07f),
                1.f);
            if (major)
            {
                const float val = ImLerp(ymin, ymax, t);
                _snprintf(yLabel, sizeof(yLabel) - 1, "%.2f", val);
                const ImVec2 sz = ImGui::CalcTextSize(yLabel);
                dl->AddText(
                    ImVec2(cmin.x - 6.f - sz.x, y - sz.y * 0.5f),
                    gui->get_clr(clr->text, 0.55f),
                    yLabel);
            }
        }

        // Baseline axis
        dl->AddLine(
            ImVec2(cmin.x, cmax.y),
            ImVec2(cmax.x, cmax.y),
            gui->get_clr(clr->text, 0.28f),
            1.25f);
        dl->AddLine(
            ImVec2(cmin.x, cmin.y),
            ImVec2(cmin.x, cmax.y),
            gui->get_clr(clr->text, 0.28f),
            1.25f);

        // Filled area under curve + glow stroke
        constexpr int kSamples = 64;
        ImVec2 samples[kSamples + 1]{};
        for (int s = 0; s <= kSamples; ++s)
        {
            const float x = static_cast<float>(s) / static_cast<float>(kSamples);
            const float yv = Menu::SampleAimCurve(curve, x);
            samples[s] = ImVec2(
                ImLerp(cmin.x, cmax.x, x),
                YToScreen(yv));
        }
        dl->PathClear();
        dl->PathLineTo(ImVec2(samples[0].x, cmax.y));
        for (int s = 0; s <= kSamples; ++s)
            dl->PathLineTo(samples[s]);
        dl->PathLineTo(ImVec2(samples[kSamples].x, cmax.y));
        dl->PathFillConvex(gui->get_clr(clr->accent_color, 0.14f));

        for (int pass = 0; pass < 3; ++pass)
        {
            const float thick = 5.5f - pass * 1.5f;
            const float alpha = 0.12f + pass * 0.28f;
            for (int s = 1; s <= kSamples; ++s)
            {
                dl->AddLine(
                    samples[s - 1],
                    samples[s],
                    gui->get_clr(clr->accent_color, alpha),
                    thick);
            }
        }

        struct CurveDragState final
        {
            int drag = -1;
            float hover[Menu::kAimCurvePoints]{};
        };
        CurveDragState& st =
            LeyawinAnim<CurveDragState, 9142>().get(ImGui::GetID("##curve_st"));

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int hoverIdx = -1;
        float bestDist = 16.f;
        for (int i = 0; i < Menu::kAimCurvePoints; ++i)
        {
            const ImVec2 pt = ToScreen(i, curve.y[i]);
            const float d = ImLengthSqr(ImVec2(mouse.x - pt.x, mouse.y - pt.y));
            if (d < bestDist * bestDist)
            {
                bestDist = sqrtf(d);
                hoverIdx = i;
            }
        }

        if (itemActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (hoverIdx >= 0)
                st.drag = hoverIdx;
            else
            {
                // Click empty plot → snap nearest point vertically
                float bestX = 1e9f;
                int nearest = 0;
                for (int i = 0; i < Menu::kAimCurvePoints; ++i)
                {
                    const ImVec2 pt = ToScreen(i, curve.y[i]);
                    const float dx = fabsf(mouse.x - pt.x);
                    if (dx < bestX)
                    {
                        bestX = dx;
                        nearest = i;
                    }
                }
                st.drag = nearest;
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            st.drag = -1;

        if (st.drag >= 0 && st.drag < Menu::kAimCurvePoints && itemActive)
        {
            float ny = (cmax.y - mouse.y) / ImMax(cmax.y - cmin.y, 1.f);
            ny = ImClamp(ny, 0.f, 1.f);
            curve.y[st.drag] = ymin + ny * (ymax - ymin);
        }

        for (int i = 0; i < Menu::kAimCurvePoints; ++i)
        {
            const bool hot = (i == hoverIdx) || (i == st.drag);
            st.hover[i] = ImLerp(
                st.hover[i],
                hot ? 1.f : 0.f,
                gui->fixed_speed(16.f));
            const ImVec2 pt = ToScreen(i, curve.y[i]);
            const float r = 5.f + st.hover[i] * 2.2f;
            if (hot)
            {
                dl->AddCircleFilled(
                    pt,
                    r + 6.f,
                    gui->get_clr(clr->accent_color, 0.18f + 0.12f * st.hover[i]),
                    24);
                // Value tooltip
                char tip[32]{};
                _snprintf(tip, sizeof(tip) - 1, "%.2f", curve.y[i]);
                const ImVec2 tipSz = ImGui::CalcTextSize(tip);
                const ImVec2 tipPos(
                    pt.x - tipSz.x * 0.5f,
                    pt.y - r - tipSz.y - 8.f);
                dl->AddRectFilled(
                    tipPos - ImVec2(6.f, 2.f),
                    tipPos + tipSz + ImVec2(6.f, 2.f),
                    gui->get_clr(clr->child_bottom_bar, 0.92f),
                    4.f);
                dl->AddText(
                    tipPos,
                    gui->get_clr(clr->text_active),
                    tip);
            }
            dl->AddCircleFilled(
                pt,
                r,
                gui->get_clr(clr->element_background),
                24);
            dl->AddCircle(
                pt,
                r,
                gui->get_clr(clr->accent_color, 0.95f),
                24,
                2.f);
            dl->AddCircleFilled(
                pt,
                2.2f + st.hover[i],
                gui->get_clr(clr->accent_color),
                16);
        }

        dl->AddText(
            ImVec2(cmin.x, pos.y + h - bottomGutter + 4.f),
            gui->get_clr(clr->text, 0.6f),
            "Near");
        const ImVec2 farSz = ImGui::CalcTextSize("Far");
        dl->AddText(
            ImVec2(cmax.x - farSz.x, pos.y + h - bottomGutter + 4.f),
            gui->get_clr(clr->text, 0.6f),
            "Far");

        (void)itemHovered;
        ImGui::PopID();
    }

    void CurveOrSlider(
        const char* title,
        bool* useCurve,
        float* simpleMul,
        Menu::AimCurve& curve,
        float ymin,
        float ymax,
        const char* tipEn,
        const char* tipRu,
        const char* tipUa) noexcept
    {
        LabelWithHelp(title, tipEn, tipRu, tipUa);
        const char* modes[] = {
            T("Slider", "Ползунок", "Повзунок"),
            T("Curve", "Кривая", "Крива")
        };
        int mode = *useCurve ? 1 : 0;
        char modeId[96]{};
        _snprintf(modeId, sizeof(modeId) - 1, "##mode_%s", title);
        if (gui->dropdown(modeId, &mode, modes, 2))
            *useCurve = (mode == 1);
        if (*useCurve)
            DrawCurveEditor(title, curve, ymin, ymax);
        else
        {
            char sliderId[96]{};
            _snprintf(sliderId, sizeof(sliderId) - 1, "%s##simple", title);
            gui->slider_float(sliderId, simpleMul, ymin, ymax, "%.2f");
        }
    }

    using MenuUi::BoneLabel;
    using MenuUi::DrawBoneDropdown;
    using MenuUi::DrawBoneOptions;

    bool AttachmentEnabled(
        const Menu::WeaponOverrideSlot& slot,
        std::uint32_t comp) noexcept
    {
        for (int i = 0; i < slot.attachmentCount; ++i)
            if (slot.attachments[i] == comp)
                return true;
        return false;
    }

    void SetAttachment(
        Menu::WeaponOverrideSlot& slot,
        std::uint32_t comp,
        bool on) noexcept
    {
        if (on)
        {
            if (AttachmentEnabled(slot, comp))
                return;
            if (slot.attachmentCount >= Menu::kMaxWeaponAttachments)
                return;
            slot.attachments[slot.attachmentCount++] = comp;
        }
        else
        {
            for (int i = 0; i < slot.attachmentCount; ++i)
            {
                if (slot.attachments[i] != comp)
                    continue;
                for (int j = i; j + 1 < slot.attachmentCount; ++j)
                    slot.attachments[j] = slot.attachments[j + 1];
                --slot.attachmentCount;
                break;
            }
        }
    }

    void PushAttachmentToggle(
        std::uint32_t weaponHash,
        std::uint32_t compHash,
        bool on) noexcept
    {
        char cmd[96]{};
        _snprintf(
            cmd,
            sizeof(cmd) - 1,
            "wpn_comp:%u:%u:%d",
            weaponHash,
            compHash,
            on ? 1 : 0);
        QuantTools::QueueCommand(cmd, "Weapon Attachment", true);
    }

    void PushAttachmentConfig(const Menu::WeaponOverrideSlot& slot) noexcept
    {
        char json[768]{};
        int n = _snprintf(
            json,
            sizeof(json) - 1,
            "wpn_cfg:{\"%u\":{",
            slot.hash);
        for (int i = 0; i < slot.attachmentCount && n > 0 && n < 700; ++i)
        {
            n += _snprintf(
                json + n,
                sizeof(json) - 1 - n,
                "%s\"%u\":true",
                i ? "," : "",
                slot.attachments[i]);
        }
        if (n > 0 && n < static_cast<int>(sizeof(json)) - 4)
        {
            json[n++] = '}';
            json[n++] = '}';
            json[n] = 0;
            QuantTools::QueueCommand(json, "Weapon Attachments", true);
        }
    }

    void RenderDetail(
        Menu::WeaponOverrideSlot& slot,
        bool advancedOnly) noexcept
    {
        Menu::AimWeaponProfile& p = slot.profile;
        CheckWithHelp(
            T("Use custom profile", "Свой профиль", "Власний профіль"),
            &slot.active,
            "When enabled, these settings replace the Aimbot group profile for this weapon only.",
            "Если включено — эти настройки заменяют групповой профиль Aimbot только для этого оружия.",
            "Якщо увімкнено — ці налаштування замінюють груповий профіль Aimbot лише для цієї зброї.");

        const char* tabs[] = {
            T("Attachments", "Обвесы", "Обвіси"),
            T("Combat", "Бой", "Бій")
        };
        if (g_DetailTab < 0 || g_DetailTab > 1)
            g_DetailTab = 0;
        ImGui::PushID("wm_detail_tabs");
        for (int i = 0; i < 2; ++i)
        {
            gui->sub_selection(tabs[i], i, g_DetailTab, ImVec2(108.f, 36.f));
            if (i < 1)
                gui->sameline();
        }
        ImGui::PopID();
        gui->separator();

        const int detailTab = advancedOnly ? g_DetailTab + 2 : g_DetailTab;
        if (detailTab == 0)
        {
            SectionTitle(T("Vector", "Вектор", "Вектор"));
            CheckWithHelp(
                T("Enable Vector", "Вектор", "Вектор"),
                &p.vecEnabled,
                "Smoothly pulls your camera toward the selected bone while the aim key is held.",
                "Плавно тянет камеру к выбранной кости, пока зажата клавиша аима.",
                "Плавно тягне камеру до обраної кістки, поки затиснута клавіша аіму.");
            CheckWithHelp(
                T("Draw FOV", "Показ FOV", "Показ FOV"),
                &p.vecDrawFov,
                "Draws the aim field-of-view circle on screen.",
                "Рисует круг FOV аима на экране.",
                "Малює коло FOV аіму на екрані.");
            SliderWithHelp(
                T("Base FOV", "Базовый FOV", "Базовий FOV"),
                &p.vecFov, 1.f, 300.f, "%.0f",
                "Max pixel radius from crosshair where targets are considered.",
                "Максимальный радиус в пикселях от прицела, в котором ищутся цели.",
                "Максимальний радіус у пікселях від прицілу, де шукаються цілі.");
            CheckWithHelp(
                T("Dynamic FOV", "Динамический FOV", "Динамічний FOV"),
                &p.dynamicFov,
                "Scales FOV by distance, ADS, and target speed.",
                "Масштабирует FOV по дистанции, прицеливанию (ADS) и скорости цели.",
                "Масштабує FOV за дистанцією, прицілюванням (ADS) і швидкістю цілі.");
            if (p.dynamicFov)
            {
                SliderWithHelp(
                    "ADS mul", &p.dynamicFovAdsMul, 0.3f, 1.2f, "%.2f",
                    "FOV multiplier while aiming down sights (usually < 1).",
                    "Множитель FOV при прицеливании (обычно < 1).",
                    "Множник FOV під час прицілювання (зазвичай < 1).");
                SliderWithHelp(
                    "Speed expand", &p.dynamicFovSpeedExpand, 0.f, 1.5f, "%.2f",
                    "How much FOV grows when the target is moving fast.",
                    "Насколько FOV растёт, когда цель быстро движется.",
                    "Наскільки FOV зростає, коли ціль швидко рухається.");
            }
            SliderWithHelp(
                T("Smoothness", "Плавность", "Плавність"),
                &p.vecSmoothness, 1.f, 100.f, "%.0f",
                "Higher = slower/smoother camera pull toward the target.",
                "Выше = медленнее/плавнее доводка камеры к цели.",
                "Вище = повільніше/плавніше доведення камери до цілі.");
            CheckWithHelp(
                "Random Smooth", &p.vecRandomSmooth,
                "Adds random variation to aim speed each frame.",
                "Случайный разброс скорости аима каждый кадр.",
                "Випадковий розкид швидкості аіму щокадру.");
            if (p.vecRandomSmooth)
            {
                SliderWithHelp(
                    "Random Amt", &p.vecRandomAmt, 0.f, 1.f, "%.2f",
                    "Strength of random smooth variation.",
                    "Сила случайного разброса плавности.",
                    "Сила випадкового розкиду плавності.");
            }
            CheckWithHelp(
                "Smart Smooth", &p.vecSmartSmooth,
                "Slows aim more when already close to the crosshair.",
                "Сильнее замедляет аим, когда цель уже близко к прицелу.",
                "Сильніше сповільнює аім, коли ціль уже близько до прицілу.");
            CheckWithHelp(
                "Camera Shake", &p.vecCamShake,
                "Adds tiny camera tremor so aim looks less robotic.",
                "Микро-дрожание камеры, чтобы аим выглядел менее роботным.",
                "Мікро-тремтіння камери, щоб аім виглядав менш роботним.");
            if (p.vecCamShake)
            {
                SliderWithHelp(
                    "Shake Amt", &p.vecShakeAmt, 0.f, 1.f, "%.2f",
                    "Camera shake intensity.",
                    "Интенсивность дрожания камеры.",
                    "Інтенсивність тремтіння камери.");
            }

            SectionTitle(T("Curves", "Кривые", "Криві"));
            if (p.dynamicFov)
            {
                CurveOrSlider(
                    T("FOV vs Distance", "FOV по дистанции", "FOV за дистанцією"),
                    &p.useFovCurve,
                    &p.fovDistanceMul,
                    p.fovDistanceCurve,
                    0.25f, 2.0f,
                    "Near→Far FOV scale. Curve = 5 control points; Slider = one multiplier for all ranges.",
                    "Near→Far масштаб FOV. Кривая = 5 точек; Ползунок = один множитель на все дистанции.",
                    "Near→Far масштаб FOV. Крива = 5 точок; Повзунок = один множник на всі дистанції.");
            }
            CurveOrSlider(
                T("Smooth vs Distance", "Плавность по дистанции", "Плавність за дистанцією"),
                &p.useSmoothCurve,
                &p.smoothDistanceMul,
                p.smoothDistanceCurve,
                0.25f, 3.0f,
                "Extra smoothness scale by distance. Curve or single slider.",
                "Доп. масштаб плавности по дистанции. Кривая или один ползунок.",
                "Додатковий масштаб плавності за дистанцією. Крива або один повзунок.");

            SectionTitle(T("Humanize", "Хуманайз", "Г'юманайз"));
            CheckWithHelp(
                T("Enable Humanize", "Включить", "Увімкнути"),
                &p.humanize,
                "Master switch for reaction delay, jitter, overshoot, fatigue.",
                "Главный переключатель задержки, джиттера, овершута, усталости.",
                "Головний перемикач затримки, джитеру, овершуту, втоми.");
            if (p.humanize)
            {
                SliderWithHelp(
                    "Reaction ms", &p.reactionDelayMs, 0.f, 250.f, "%.0f",
                    "Delay before aim starts after acquiring a new target.",
                    "Задержка перед началом аима после появления новой цели.",
                    "Затримка перед початком аіму після появи нової цілі.");
                SliderWithHelp(
                    "Micro Jitter", &p.microJitter, 0.f, 1.f, "%.2f",
                    "Small random aim offset while tracking.",
                    "Небольшое случайное смещение при слежении.",
                    "Невелике випадкове зміщення під час стеження.");
                SliderWithHelp(
                    "Overshoot", &p.overshoot, 0.f, 1.f, "%.2f",
                    "Occasionally aims slightly past the bone then settles.",
                    "Иногда целится чуть дальше кости и потом доводит.",
                    "Іноді цілиться трохи далі кістки і потім доводить.");
                SliderWithHelp(
                    "Switch Delay", &p.targetSwitchDelayMs, 0.f, 400.f, "%.0f",
                    "Pause when switching between targets.",
                    "Пауза при переключении между целями.",
                    "Пауза при перемиканні між цілями.");
                SliderWithHelp(
                    "Fatigue", &p.fatigue, 0.f, 1.f, "%.2f",
                    "Extra slowdown on long-range / prolonged tracking.",
                    "Доп. замедление на дальней дистанции / долгом трекинге.",
                    "Додаткове сповільнення на дальній дистанції / довгому трекінгу.");
            }

            SectionTitle(T("Bones", "Кости", "Кістки"));
            DrawBoneOptions(
                &p.vecNearest, &p.vecBoneNearestAny, &p.vecVisible, "vec");
            DrawBoneDropdown(p.vecBones, "vec", &p.vecBoneNearestAny);

            SectionTitle(T("Filters", "Фильтры", "Фільтри"));
            CheckWithHelp(
                "Ignore Dead", &p.ignoreDead,
                "Skip dead peds.",
                "Игнорировать мёртвых.",
                "Ігнорувати мертвих.");
            CheckWithHelp(
                "Ignore NPC", &p.ignoreNpc,
                "Skip non-player characters.",
                "Игнорировать NPC.",
                "Ігнорувати NPC.");
            CheckWithHelp(
                "Ignore Admins", &p.ignoreAdmins,
                "Skip players above admin level threshold.",
                "Игнорировать игроков выше порога админ-уровня.",
                "Ігнорувати гравців вище порогу адмін-рівня.");
            CheckWithHelp(
                "Ignore Friends", &p.ignoreFriends,
                "Skip players marked as friends.",
                "Игнорировать друзей из вкладки Friends.",
                "Ігнорувати друзів з вкладки Friends.");
        }
        else if (detailTab == 1)
        {
            SectionTitle(T("Silent", "Сайлент", "Сайлент"));
            CheckWithHelp(
                T("Enable Silent", "Сайлент", "Сайлент"),
                &p.silentEnabled,
                "Redirects bullet end-point to the selected bone without moving the camera.",
                "Перенаправляет конец пули в кость без движения камеры.",
                "Перенаправляє кінець кулі в кістку без руху камери.");
            CheckWithHelp(
                "Draw FOV", &p.silentDrawFov,
                "Draw silent FOV circle.",
                "Показать круг FOV сайлента.",
                "Показати коло FOV сайлента.");
            SliderWithHelp(
                "Silent FOV", &p.silentFov, 1.f, 300.f, "%.0f",
                "Pixel radius for silent target selection.",
                "Радиус в пикселях для выбора silent-цели.",
                "Радіус у пікселях для вибору silent-цілі.");
            CheckWithHelp(
                "Mark Target", &p.silentMark,
                "Highlight the current silent target on ESP/preview.",
                "Подсветить текущую silent-цель.",
                "Підсвітити поточну silent-ціль.");
            CheckWithHelp(
                "Lock Target", &p.silentLock,
                "Keep the same target until they leave FOV / die.",
                "Держать одну цель, пока не выйдет из FOV / не умрёт.",
                "Тримати одну ціль, поки не вийде з FOV / не помре.");
            CheckWithHelp(
                "Hitchance", &p.silentHitchance,
                "Randomly skip some shots based on hitchance %.",
                "Случайно пропускать часть выстрелов по % шанса.",
                "Випадково пропускати частину пострілів за % шансу.");
            if (p.silentHitchance)
            {
                SliderWithHelp(
                    "Hitchance %", &p.silentHitchancePct, 1.f, 100.f, "%.0f",
                    "Base percent chance a redirected bullet is applied.",
                    "Базовый шанс применения silent-редиректа.",
                    "Базовий шанс застосування silent-редиректу.");
                CurveOrSlider(
                    T("Hitchance vs Distance", "Шанс по дистанции", "Шанс за дистанцією"),
                    &p.useSilentHcCurve,
                    &p.silentHcDistanceMul,
                    p.silentHitchanceCurve,
                    0.1f, 1.5f,
                    "Scales hitchance by distance. Curve = 5 points; Slider = one multiplier.",
                    "Масштаб шанса по дистанции. Кривая = 5 точек; Ползунок = один множитель.",
                    "Масштаб шансу за дистанцією. Крива = 5 точок; Повзунок = один множник.");
            }

            SectionTitle(T("Silent suite", "Silent suite", "Silent suite"));
            SliderWithHelp(
                "Reaction ms", &p.silentReactionMs, 0.f, 250.f, "%.0f",
                "Delay before silent activates on a new target.",
                "Задержка перед активацией silent на новой цели.",
                "Затримка перед активацією silent на новій цілі.");
            SliderWithHelp(
                T("Miss Bias", "Смещение промаха", "Зміщення промаху"),
                &p.silentMissBias, 0.f, 1.f, "%.2f",
                "Soft random offset toward bone edge (looks less perfect).",
                "Мягкий случайный сдвиг к краю кости (менее идеальные попадания).",
                "М'який випадковий зсув до краю кістки (менш ідеальні влучання).");

            SectionTitle(T("Bones", "Кости", "Кістки"));
            DrawBoneOptions(
                &p.silentNearest,
                &p.silentBoneNearestAny,
                &p.silentVisible,
                "sil");
            DrawBoneDropdown(p.silentBones, "sil", &p.silentBoneNearestAny);
        }
        else if (detailTab == 2)
        {
            const auto* compat = WeaponAttachments::FindCompat(slot.hash);
            if (!compat || compat->count <= 0)
            {
                ImGui::TextDisabled(
                    "%s",
                    T("No known attachments for this weapon.",
                      "Нет известных обвесов для этого оружия.",
                      "Немає відомих обвісів для цієї зброї."));
            }
            else
            {
                LabelWithHelp(
                    T("Attachments", "Обвесы", "Обвіси"),
                    "Toggles sync into Quant Tools and re-apply in-game.",
                    "Переключатели синхронизируются в Quant Tools и применяются в игре.",
                    "Перемикачі синхронізуються в Quant Tools і застосовуються в грі.");
                                // Fixed storage — Leyawin FeatureBinds keeps bool*; unordered_map
                // rehash invalidated those pointers and crashed on Attachments tab.
                constexpr int kMaxAttUi = 16;
                static bool s_AttUi[kMaxAttUi]{};
                static std::uint32_t s_AttUiWeapon = 0;
                if (s_AttUiWeapon != slot.hash)
                {
                    s_AttUiWeapon = slot.hash;
                    for (int i = 0; i < kMaxAttUi; ++i)
                        s_AttUi[i] = false;
                }
                const int attCount =
                    compat->count < kMaxAttUi ? compat->count : kMaxAttUi;
                for (int i = 0; i < attCount; ++i)
                {
                    const std::uint32_t ch = compat->comps[i];
                    const auto* comp = WeaponAttachments::FindComponent(ch);
                    char label[96]{};
                    _snprintf(
                        label,
                        sizeof(label) - 1,
                        "%s##att%u",
                        comp ? comp->name : "Component",
                        ch);
                    bool& on = s_AttUi[i];
                    on = AttachmentEnabled(slot, ch);
                    if (gui->checkbox(label, &on))
                    {
                        SetAttachment(slot, ch, on);
                        PushAttachmentToggle(slot.hash, ch, on);
                    }
                }
                if (gui->button(
                        T("Apply All Off", "Снять всё", "Зняти все"),
                        ImVec2(ImGui::GetContentRegionAvail().x, 30.f)))
                {
                    for (int i = 0; i < attCount; ++i)
                        s_AttUi[i] = false;
                    slot.attachmentCount = 0;
                    QuantTools::QueueCommand("wpn_all_off", "Attachments Off", true);
                    PushAttachmentConfig(slot);
                }
            }
        }
        else
        {
            SectionTitle(T("Combat", "Бой", "Бій"));
            ImGui::TextDisabled(
                "%s",
                T("Adds to the global weapon settings for this weapon.",
                  "Дополняет глобальные настройки оружия для этого оружия.",
                  "Додає до глобальних налаштувань зброї для цієї зброї."));
            CheckWithHelp(
                T("No Spread", "Без разброса", "Без розкиду"),
                &p.noSpread,
                "Reduce weapon accuracy spread and sustained-fire bloom.",
                "Уменьшает разброс оружия и раскрытие при стрельбе.",
                "Зменшує розкид зброї та розкриття при стрільбі.");
            if (p.noSpread)
            {
                SliderWithHelp(
                    T("No Spread %", "Разброс %", "Розкид %"),
                    &p.noSpreadPct, 0.f, 100.f, "%.0f%%",
                    "How strongly to reduce spread (0–100%).",
                    "Насколько сильно убирать разброс (0–100%).",
                    "Наскільки сильно прибрати розкид (0–100%).");
            }
            CheckWithHelp(
                T("No Recoil", "Без отдачи", "Без віддачі"),
                &p.noRecoil,
                "Reduce recoil shake / accuracy error (when writer is enabled).",
                "Уменьшает отдачу (когда writer включён).",
                "Зменшує віддачу (коли writer увімкнено).");
            if (p.noRecoil)
            {
                SliderWithHelp(
                    T("No Recoil %", "Отдача %", "Віддача %"),
                    &p.noRecoilPct, 0.f, 100.f, "%.0f%%",
                    "How strongly to reduce recoil (0–100%).",
                    "Насколько сильно убирать отдачу (0–100%).",
                    "Наскільки сильно прибрати віддачу (0–100%).");
            }
            CheckWithHelp(
                T("Reload Speed", "Скорость перезарядки", "Швидкість перезарядки"),
                &p.reloadSpeed,
                "Faster reload times (when writer is enabled).",
                "Ускоряет перезарядку (когда writer включён).",
                "Прискорює перезарядку (коли writer увімкнено).");
            if (p.reloadSpeed)
            {
                SliderWithHelp(
                    T("Reload Speed %", "Перезарядка %", "Перезарядка %"),
                    &p.reloadSpeedPct, 0.f, 100.f, "%.0f%%",
                    "How strongly to speed up reload (0–100%).",
                    "Насколько ускорять перезарядку (0–100%).",
                    "Наскільки прискорювати перезарядку (0–100%).");
            }
        }
    }

bool MatchesSearch(const WeaponCatalog::Entry& w) noexcept
{
    if (!g_Search[0])
        return true;
    char needle[64]{};
    lstrcpynA(needle, g_Search, sizeof(needle));
    for (char* p = needle; *p; ++p)
        *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
    char hay[96]{};
    _snprintf(hay, sizeof(hay) - 1, "%s %s", w.spawn, w.name);
    for (char* p = hay; *p; ++p)
        *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
    return strstr(hay, needle) != nullptr;
}

void EnsureSelection(std::uint32_t equipped) noexcept
{
    if (g_SelectedIndex >= 0 &&
        g_SelectedIndex < WeaponCatalog::kWeaponCount &&
        AimableWeapon(WeaponCatalog::kWeapons[g_SelectedIndex]))
        return;
    if (equipped)
    {
        for (int i = 0; i < WeaponCatalog::kWeaponCount; ++i)
        {
            if (WeaponCatalog::kWeapons[i].hash == equipped &&
                AimableWeapon(WeaponCatalog::kWeapons[i]))
            {
                g_SelectedIndex = i;
                g_Category = Menu::AimGroupFromWeaponHash(
                    WeaponCatalog::kWeapons[i].hash);
                return;
            }
        }
    }
    g_SelectedIndex = FirstAimableIndex();
}

void RenderWeaponRow(
    int index,
    const WeaponCatalog::Entry& w,
    std::uint32_t equipped) noexcept
{
    const bool selected = (g_SelectedIndex == index);
    const bool isEquipped = (w.hash == equipped);
    const bool hasOverride =
        Menu::FindWeaponOverride(w.hash) &&
        Menu::FindWeaponOverride(w.hash)->active;

    float iconW = 56.f;
    float iconH = 28.f;
    MenuTexture::FitWeaponIconSize(w.iconFile, 64.f, 36.f, &iconW, &iconH);
    char meta[96]{};
    _snprintf(
        meta,
        sizeof(meta) - 1,
        "%s%s",
        w.spawn,
        isEquipped
            ? T("  · equipped", "  · в руках", "  · в руках")
            : "");
    char id[32]{};
    _snprintf(id, sizeof(id) - 1, "wpn_%d", index);
    MenuUi::ListRowDesc row{};
    row.title = w.name;
    row.meta = meta;
    row.selected = selected;
    row.accentDot = hasOverride;
    row.iconSlotW = 64.f;
    row.iconW = iconW;
    row.iconH = iconH;
    row.height = 48.f;
    if (void* tex = MenuTexture::GetWeaponIconImGui(w.iconFile))
        row.icon = reinterpret_cast<ImTextureID>(tex);
    if (MenuUi::DrawListRow(id, row))
        g_SelectedIndex = index;
}

void RenderImpl(bool advancedOnly) noexcept
{
    const int lang = static_cast<int>(Menu::s_Language);
    const std::uint32_t equipped = Game::GetLocalWeaponHash();
    EnsureSelection(equipped);

    // Compact category selector remains usable at 150–200% scale.
    const char* categories[kAimFilterCount]{};
    for (int c = 0; c < kAimFilterCount; ++c)
        categories[c] = AimFilterLabel(c, lang);
    gui->dropdown(
        T("Category###wm_category",
          "Категория###wm_category",
          "Категорія###wm_category"),
        &g_Category,
        categories,
        kAimFilterCount,
        7);
    gui->separator();

    const float availY = ImGui::GetContentRegionAvail().y;
    const float availX = ImGui::GetContentRegionAvail().x;
    const float gap = 10.f;
    const float listW = 310.f;
    const float profileW = ImMax(availX - listW - gap, 240.f);
    // Leyawin begin_child draws a 40px title bar above the child window.
    // Size the body so header + body fit in availY; otherwise the last
    // rows (e.g. Ignore Friends) stay clipped and cannot scroll fully in.
    const float childH = ImMax(availY - 40.f, 120.f);

    gui->begin_group();
    gui->begin_child(
        ICON_FA_GUN,
        T("Arsenal", "Арсенал", "Арсенал"),
        ImVec2(listW, childH));
    {
        gui->text_field(
            "##wm_search",
            T("Search weapon...", "Поиск оружия...", "Пошук зброї..."),
            ImVec2(ImGui::GetContentRegionAvail().x, 32.f),
            g_Search,
            sizeof(g_Search));

        if (equipped &&
            gui->button(
                T("Select equipped", "Выбрать в руках", "Обрати в руках"),
                ImVec2(ImGui::GetContentRegionAvail().x, 28.f)))
        {
            for (int i = 0; i < WeaponCatalog::kWeaponCount; ++i)
            {
                if (WeaponCatalog::kWeapons[i].hash == equipped &&
                    AimableWeapon(WeaponCatalog::kWeapons[i]))
                {
                    g_SelectedIndex = i;
                    g_Category = Menu::AimGroupFromWeaponHash(
                        WeaponCatalog::kWeapons[i].hash);
                    break;
                }
            }
        }

        ImGui::Spacing();
        int visible = 0;
        for (int i = 0; i < WeaponCatalog::kWeaponCount; ++i)
        {
            const auto& w = WeaponCatalog::kWeapons[i];
            if (!AimableWeapon(w))
                continue;
            if (!MatchesAimFilter(w, g_Category))
                continue;
            if (!MatchesSearch(w))
                continue;
            RenderWeaponRow(i, w, equipped);
            ++visible;
        }
        if (visible == 0)
        {
            ImGui::TextDisabled(
                "%s",
                T("No weapons match",
                  "Ничего не найдено",
                  "Нічого не знайдено"));
        }
    }
    gui->end_child();
    gui->end_group();

    gui->sameline();

    gui->begin_group();
    gui->begin_child(
        ICON_FA_SLIDERS,
        T("Weapon profile", "Профиль оружия", "Профіль зброї"),
        ImVec2(profileW, childH));
    {
        if (g_SelectedIndex < 0 ||
            g_SelectedIndex >= WeaponCatalog::kWeaponCount)
        {
            ImGui::TextDisabled(
                "%s",
                T("Select a weapon on the left",
                  "Выбери оружие слева",
                  "Обери зброю зліва"));
        }
        else
        {
            const auto& w = WeaponCatalog::kWeapons[g_SelectedIndex];
            ImGui::BeginGroup();
            if (void* tex = MenuTexture::GetWeaponIconImGui(w.iconFile))
            {
                float iw = 120.f;
                float ih = 60.f;
                MenuTexture::FitWeaponIconSize(
                    w.iconFile, 140.f, 70.f, &iw, &ih);
                ImGui::Image(
                    reinterpret_cast<ImTextureID>(tex),
                    ImVec2(iw, ih));
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(w.name);
            ImGui::TextDisabled("%s  ·  0x%08X", w.spawn, w.hash);
            if (w.hash == equipped)
            {
                ImGui::TextColored(
                    clr->accent_color,
                    "%s",
                    T("Currently equipped",
                      "Сейчас в руках",
                      "Зараз в руках"));
            }
            ImGui::EndGroup();
            ImGui::EndGroup();
            gui->separator();

            Menu::WeaponOverrideSlot* slot =
                Menu::FindWeaponOverride(w.hash);
            if (!slot)
            {
                ImGui::TextDisabled(
                    "%s",
                    T("No advanced profile for this weapon yet.",
                      "Для этого оружия ещё нет advanced-профиля.",
                      "Для цієї зброї ще немає advanced-профілю."));
                if (gui->button(
                        T("Create weapon profile###wm_create",
                          "Создать профиль оружия###wm_create",
                          "Створити профіль зброї###wm_create"),
                        ImVec2(
                            ImGui::GetContentRegionAvail().x,
                            32.f)))
                {
                    slot = Menu::GetOrCreateWeaponOverride(w.hash);
                }
                if (!slot &&
                    Menu::s_WeaponOverrideCount >=
                        Menu::kMaxWeaponOverrides)
                {
                    ImGui::TextDisabled(
                        "%s",
                        T("Weapon profile limit reached",
                          "Достигнут лимит профилей",
                          "Досягнуто ліміт профілів"));
                }
            }
            if (slot)
            {
                RenderDetail(*slot, advancedOnly);
            }
            // Extra scroll room so the last filter rows clear the clip edge.
            ImGui::Dummy(ImVec2(0.f, 18.f));
        }
    }
    gui->end_child();
    gui->end_group();
}
} // namespace

void Render() noexcept
{
    RenderImpl(true);
}

void RenderCurveEditor(
    const char* id,
    Menu::AimCurve& curve,
    float minimum,
    float maximum) noexcept
{
    DrawCurveEditor(id, curve, minimum, maximum);
}

void OpenAddWeapon() noexcept
{
    g_AddOpen = true;
    g_AddFocus = true;
    g_AddSelected = -1;
    g_AddSearch[0] = 0;
}

bool RenderAddWeaponWindow(std::uint32_t* addedHash) noexcept
{
    if (!g_AddOpen && g_AddAnim <= 0.001f)
        return false;

    bool added = false;
    const float dt = ImClamp(
        ImGui::GetIO().DeltaTime,
        0.f,
        0.05f);
    g_AddAnim = ImClamp(
        g_AddAnim +
            (g_AddOpen ? 1.f : -1.f) *
                dt * (g_AddOpen ? 5.f : 8.f),
        0.f,
        1.f);
    const float reveal =
        0.5f - 0.5f *
        cosf(g_AddAnim * 3.14159265358979323846f);
    const float scale = ImClamp(
        static_cast<float>(Menu::s_UIScalePercent) / 100.f,
        1.f,
        2.f);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 windowSize(
        ImMin(760.f * scale, viewport->WorkSize.x - 24.f),
        ImMin(560.f * scale, viewport->WorkSize.y - 24.f));

    ImGui::SetNextWindowPos(
        viewport->WorkPos,
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        viewport->WorkSize,
        ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::Begin(
        "##aim_add_backdrop",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav);
    ImGui::InvisibleButton(
        "##dismiss",
        viewport->WorkSize);
    if (g_AddOpen && ImGui::IsItemClicked())
        g_AddOpen = false;
    ImDrawList* backdropDraw =
        ImGui::GetWindowDrawList();
    const ImVec2 modalCenter =
        viewport->WorkPos + viewport->WorkSize * 0.5f +
        ImVec2(0.f, 14.f * (1.f - reveal) * scale);
    const ImVec2 shadowMin =
        modalCenter - windowSize * 0.5f - ImVec2(10.f, 10.f) * scale;
    const ImVec2 shadowMax =
        modalCenter + windowSize * 0.5f + ImVec2(10.f, 10.f) * scale;
    backdropDraw->AddRectFilled(
        shadowMin,
        shadowMax,
        IM_COL32(0, 0, 0, static_cast<int>(80.f * reveal)),
        18.f * scale);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImGui::SetNextWindowPos(
        modalCenter,
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    if (g_AddFocus)
    {
        ImGui::SetNextWindowFocus();
        g_AddFocus = false;
    }

    ImGui::PushStyleVar(
        ImGuiStyleVar_Alpha,
        ImGui::GetStyle().Alpha * reveal);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(16.f, 14.f) * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        gui->get_clr(clr->window_background, 0.99f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        gui->get_clr(clr->child_stroke_bar, 0.95f));

    bool open = g_AddOpen;
    ImGuiWindowFlags modalFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;
    if (!g_AddOpen)
        modalFlags |= ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin(
            "Add weapon###aim_add_weapon",
            nullptr,
            modalFlags))
    {
        ImDrawList* modalDraw =
            ImGui::GetWindowDrawList();
        const ImVec2 modalPos =
            ImGui::GetWindowPos();
        const ImVec2 modalMax =
            modalPos + ImGui::GetWindowSize();
        const float headerH = 58.f * scale;
        modalDraw->AddRectFilled(
            modalPos,
            ImVec2(modalMax.x, modalPos.y + headerH),
            gui->get_clr(clr->child_top_bar, 0.72f),
            12.f * scale,
            ImDrawFlags_RoundCornersTop);
        ImFont* iconFont =
            set->icon ? set->icon : ImGui::GetFont();
        modalDraw->AddText(
            iconFont,
            15.f * scale,
            modalPos + ImVec2(18.f, 20.f) * scale,
            gui->get_clr(clr->accent_color),
            ICON_FA_GUN);
        modalDraw->AddText(
            modalPos + ImVec2(49.f, 11.f) * scale,
            gui->get_clr(clr->text_active),
            T("Add weapon preset",
              "Добавить пресет оружия",
              "Додати пресет зброї"));
        modalDraw->AddText(
            modalPos + ImVec2(49.f, 31.f) * scale,
            gui->get_clr(clr->text, 0.78f),
            T("Left click picks one, right click marks several",
              "ЛКМ — одно, ПКМ — отметить несколько",
              "ЛКМ — одна, ПКМ — позначити кілька"));

        const ImVec2 closeMin(
            modalMax.x - 42.f * scale,
            modalPos.y + 12.f * scale);
        const ImVec2 closeMax =
            closeMin + ImVec2(30.f, 30.f) * scale;
        ImGui::SetCursorScreenPos(closeMin);
        ImGui::InvisibleButton(
            "##aim_add_close",
            closeMax - closeMin);
        const bool closeHovered =
            ImGui::IsItemHovered();
        if (g_AddOpen && ImGui::IsItemClicked())
        {
            g_AddOpen = false;
            open = false;
        }
        if (closeHovered)
        {
            modalDraw->AddRectFilled(
                closeMin,
                closeMax,
                gui->get_clr(clr->element_background, 0.72f),
                7.f * scale);
        }
        const ImVec2 xSize =
            iconFont->CalcTextSizeA(
                11.f * scale,
                FLT_MAX,
                0.f,
                ICON_FA_XMARK);
        modalDraw->AddText(
            iconFont,
            11.f * scale,
            closeMin +
                (closeMax - closeMin - xSize) * 0.5f,
            gui->get_clr(
                closeHovered
                    ? clr->text_active
                    : clr->text,
                closeHovered ? 1.f : 0.72f),
            ICON_FA_XMARK);
        ImGui::SetCursorScreenPos(
            ImVec2(
                modalPos.x + 16.f * scale,
                modalPos.y + headerH + 12.f * scale));

        MenuUi::SearchRow(
            "aim_add_search",
            T("Search weapon...",
              "Поиск оружия...",
              "Пошук зброї..."),
            g_AddSearch,
            sizeof(g_AddSearch));

        const char* categories[kAimFilterCount]{};
        for (int i = 0; i < kAimFilterCount; ++i)
        {
            categories[i] = AimFilterLabel(
                i,
                static_cast<int>(Menu::s_Language));
        }
        gui->dropdown(
            T("Category###aim_add_category",
              "Категория###aim_add_category",
              "Категорія###aim_add_category"),
            &g_AddCategory,
            categories,
            kAimFilterCount,
            7);

        const std::uint32_t equipped = Game::GetLocalWeaponHash();
        if (equipped &&
            MenuUi::ActionButton(
                "aim_add_equipped",
                T("Select equipped",
                  "Выбрать оружие в руках",
                  "Обрати зброю в руках"),
                ImVec2(ImGui::GetContentRegionAvail().x, 30.f * scale),
                false,
                ICON_FA_CROSSHAIRS))
        {
            for (int i = 0; i < WeaponCatalog::kWeaponCount; ++i)
            {
                if (WeaponCatalog::kWeapons[i].hash == equipped &&
                    AimableWeapon(WeaponCatalog::kWeapons[i]))
                {
                    g_AddSelected = i;
                    g_AddCategory = Menu::AimGroupFromWeaponHash(
                        WeaponCatalog::kWeapons[i].hash);
                    break;
                }
            }
        }

        const float footerH = 42.f * scale;
        ImGui::BeginChild(
            "##aim_add_list",
            ImVec2(0.f, ImMax(120.f, ImGui::GetContentRegionAvail().y - footerH)),
            ImGuiChildFlags_None,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);
        int filtered[WeaponCatalog::kWeaponCount]{};
        int filteredCount = 0;
        for (int i = 0; i < WeaponCatalog::kWeaponCount; ++i)
        {
            const WeaponCatalog::Entry& weapon =
                WeaponCatalog::kWeapons[i];
            if (!AimableWeapon(weapon))
                continue;
            if (!MatchesAimFilter(weapon, g_AddCategory))
                continue;

            if (g_AddSearch[0])
            {
                char needle[64]{};
                char haystack[128]{};
                lstrcpynA(needle, g_AddSearch, sizeof(needle));
                _snprintf(
                    haystack,
                    sizeof(haystack) - 1,
                    "%s %s",
                    weapon.spawn,
                    weapon.name);
                for (char* p = needle; *p; ++p)
                    *p = static_cast<char>(
                        tolower(static_cast<unsigned char>(*p)));
                for (char* p = haystack; *p; ++p)
                    *p = static_cast<char>(
                        tolower(static_cast<unsigned char>(*p)));
                if (!strstr(haystack, needle))
                    continue;
            }
            filtered[filteredCount++] = i;
        }

        const float cardSpacing = 8.f * scale;
        const float cardH = 96.f * scale;
        const float gridWidth = ImGui::GetContentRegionAvail().x;
        const int columns = ImClamp(
            static_cast<int>(
                (gridWidth + cardSpacing) / (172.f * scale + cardSpacing)),
            1,
            6);
        const float cardW =
            (gridWidth -
             cardSpacing * static_cast<float>(columns - 1)) /
            static_cast<float>(columns);
        const int gridRows =
            (filteredCount + columns - 1) / columns;
        ImDrawList* gridDraw = ImGui::GetWindowDrawList();
        ImFont* cardFont = set->poppins_widget
            ? set->poppins_widget
            : ImGui::GetFont();
        ImFont* cardIcon = set->icon ? set->icon : ImGui::GetFont();

        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            ImVec2(cardSpacing, cardSpacing));
        ImGuiListClipper clipper;
        clipper.Begin(gridRows, cardH + cardSpacing);
        while (clipper.Step())
        {
            for (int gridRow = clipper.DisplayStart;
                 gridRow < clipper.DisplayEnd;
                 ++gridRow)
            {
            const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
            for (int column = 0; column < columns; ++column)
            {
                const int visibleIndex = gridRow * columns + column;
                if (visibleIndex >= filteredCount)
                    break;
                const int i = filtered[visibleIndex];
                const WeaponCatalog::Entry& weapon =
                    WeaponCatalog::kWeapons[i];
                const ImVec2 cardMin(
                    rowOrigin.x +
                        static_cast<float>(column) * (cardW + cardSpacing),
                    rowOrigin.y);
                const ImVec2 cardMax = cardMin + ImVec2(cardW, cardH);

                char cardId[40]{};
                _snprintf(
                    cardId, sizeof(cardId) - 1, "##aim_add_%u", weapon.hash);
                ImGui::SetCursorScreenPos(cardMin);
                ImGui::InvisibleButton(
                    cardId,
                    ImVec2(cardW, cardH),
                    ImGuiButtonFlags_MouseButtonLeft |
                        ImGuiButtonFlags_MouseButtonRight);
                const bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    ClearAddMarks();
                    g_AddSelected = i;
                }
                else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                {
                    if (g_AddMarked[i])
                    {
                        g_AddMarked[i] = false;
                        --g_AddMarkedCount;
                    }
                    else
                    {
                        g_AddMarked[i] = true;
                        ++g_AddMarkedCount;
                        g_AddSelected = i;
                    }
                }

                const bool marked = g_AddMarked[i];
                const bool picked =
                    marked ||
                    (g_AddMarkedCount == 0 && g_AddSelected == i);
                const bool exists =
                    Menu::FindWeaponOverride(weapon.hash) != nullptr;

                gridDraw->AddRectFilled(
                    cardMin,
                    cardMax,
                    gui->get_clr(
                        picked
                            ? clr->accent_color
                            : clr->element_background,
                        picked ? 0.20f : (hovered ? 0.90f : 0.64f)),
                    10.f * scale);
                gridDraw->AddRect(
                    cardMin,
                    cardMax,
                    gui->get_clr(
                        picked ? clr->accent_color : clr->child_stroke_bar,
                        picked ? 0.95f : 0.55f),
                    10.f * scale,
                    0,
                    picked ? 1.4f : 1.f);

                float iconW = 78.f * scale;
                float iconH = 34.f * scale;
                MenuTexture::FitWeaponIconSize(
                    weapon.iconFile,
                    cardW - 26.f * scale,
                    38.f * scale,
                    &iconW,
                    &iconH);
                if (void* texture =
                        MenuTexture::GetWeaponIconImGui(weapon.iconFile))
                {
                    const ImVec2 iconMin(
                        cardMin.x + (cardW - iconW) * 0.5f,
                        cardMin.y + 12.f * scale +
                            (38.f * scale - iconH) * 0.5f);
                    gridDraw->AddImage(
                        reinterpret_cast<ImTextureID>(texture),
                        iconMin,
                        iconMin + ImVec2(iconW, iconH));
                }

                const float labelSize = MenuType::BodySize();
                const ImVec2 nameSize = cardFont->CalcTextSizeA(
                    labelSize, cardW - 16.f * scale, 0.f, weapon.name);
                gridDraw->PushClipRect(
                    ImVec2(cardMin.x + 8.f * scale, cardMin.y),
                    ImVec2(cardMax.x - 8.f * scale, cardMax.y),
                    true);
                gridDraw->AddText(
                    cardFont,
                    labelSize,
                    ImVec2(
                        cardMin.x + (cardW - nameSize.x) * 0.5f,
                        cardMin.y + 58.f * scale),
                    gui->get_clr(
                        picked ? clr->text_active : clr->text,
                        picked ? 1.f : 0.92f),
                    weapon.name);
                const float metaSize = MenuType::MicroSize();
                const ImVec2 metaTextSize = cardFont->CalcTextSizeA(
                    metaSize, FLT_MAX, 0.f, weapon.spawn);
                gridDraw->AddText(
                    cardFont,
                    metaSize,
                    ImVec2(
                        cardMin.x + (cardW - metaTextSize.x) * 0.5f,
                        cardMin.y + 76.f * scale),
                    gui->get_clr(clr->text, 0.58f),
                    weapon.spawn);
                gridDraw->PopClipRect();

                if (marked || exists)
                {
                    const char* badge = marked
                        ? ICON_FA_CHECK
                        : ICON_FA_CIRCLE;
                    const float badgeSize =
                        (marked ? 10.f : 6.f) * scale;
                    const ImVec2 badgeExtent =
                        cardIcon->CalcTextSizeA(
                            badgeSize, FLT_MAX, 0.f, badge);
                    gridDraw->AddText(
                        cardIcon,
                        badgeSize,
                        ImVec2(
                            cardMax.x - 10.f * scale - badgeExtent.x,
                            cardMin.y + 10.f * scale),
                        gui->get_clr(clr->accent_color, marked ? 1.f : 0.8f),
                        badge);
                }
            }
            ImGui::SetCursorScreenPos(rowOrigin);
            ImGui::Dummy(ImVec2(gridWidth, cardH));
            }
        }
        ImGui::PopStyleVar();
        if (filteredCount == 0)
        {
            MenuUi::EmptyState(
                "aim_add_empty",
                ICON_FA_MAGNIFYING_GLASS,
                T("No weapons found",
                  "Оружие не найдено",
                  "Зброю не знайдено"),
                T("Change the category or search query.",
                  "Измени категорию или строку поиска.",
                  "Зміни категорію або пошуковий запит."),
                110.f * scale);
        }
        ImGui::EndChild();

        const float buttonW =
            (ImGui::GetContentRegionAvail().x - 8.f * scale) * 0.5f;
        if (MenuUi::ActionButton(
                "aim_add_cancel",
                T("Cancel", "Отмена", "Скасувати"),
                ImVec2(buttonW, 32.f * scale)))
        {
            ClearAddMarks();
            open = false;
        }
        ImGui::SameLine(0.f, 8.f * scale);
        const bool validSelection =
            g_AddMarkedCount > 0 ||
            (g_AddSelected >= 0 &&
             g_AddSelected < WeaponCatalog::kWeaponCount);
        char confirmLabel[64]{};
        if (g_AddMarkedCount > 1)
        {
            _snprintf(
                confirmLabel,
                sizeof(confirmLabel) - 1,
                T("Add %d weapons", "Добавить %d оружия", "Додати %d зброї"),
                g_AddMarkedCount);
        }
        else
        {
            lstrcpynA(
                confirmLabel,
                T("Add weapon", "Добавить оружие", "Додати зброю"),
                sizeof(confirmLabel));
        }
        if (!validSelection)
            ImGui::BeginDisabled();
        if (MenuUi::ActionButton(
                "aim_add_confirm",
                confirmLabel,
                ImVec2(buttonW, 32.f * scale),
                true,
                ICON_FA_PLUS))
        {
            for (int i = 0; i < WeaponCatalog::kWeaponCount; ++i)
            {
                const bool wanted = g_AddMarkedCount > 0
                    ? g_AddMarked[i]
                    : (i == g_AddSelected);
                if (!wanted)
                    continue;
                const WeaponCatalog::Entry& weapon =
                    WeaponCatalog::kWeapons[i];
                if (Menu::WeaponOverrideSlot* slot =
                        Menu::GetOrCreateWeaponOverride(weapon.hash))
                {
                    slot->active = true;
                    if (addedHash)
                        *addedHash = weapon.hash;
                    added = true;
                }
            }
            if (added)
            {
                ClearAddMarks();
                open = false;
            }
        }
        if (!validSelection)
            ImGui::EndDisabled();
    }
    ImGui::End();

    ImDrawList* foreground =
        ImGui::GetForegroundDrawList();
    const ImVec2 workMin = viewport->WorkPos;
    const ImVec2 workMax =
        viewport->WorkPos + viewport->WorkSize;
    const ImVec2 modalMin =
        modalCenter - windowSize * 0.5f;
    const ImVec2 modalMaxRect =
        modalCenter + windowSize * 0.5f;
    const ImU32 dimColor = IM_COL32(
        0,
        0,
        0,
        static_cast<int>(158.f * reveal));
    foreground->AddRectFilled(
        workMin,
        ImVec2(modalMin.x, workMax.y),
        dimColor);
    foreground->AddRectFilled(
        ImVec2(modalMaxRect.x, workMin.y),
        workMax,
        dimColor);
    foreground->AddRectFilled(
        ImVec2(modalMin.x, workMin.y),
        ImVec2(modalMaxRect.x, modalMin.y),
        dimColor);
    foreground->AddRectFilled(
        ImVec2(modalMin.x, modalMaxRect.y),
        ImVec2(modalMaxRect.x, workMax.y),
        dimColor);

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);

    g_AddOpen = open;
    return added;
}
}
