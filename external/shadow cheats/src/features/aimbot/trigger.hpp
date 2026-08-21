#include "security/xorstr.hpp"
#include "../../core/globals.hpp"
#include "../../memory/memory_access.hpp"
#include "../../config/settings.hpp"
#include "../../sdk/offsets.hpp"
#include "../../sdk/CPed.hpp"
#include "../../sdk/bones.hpp"
#include "../../sdk/math_utils.hpp"
#include "../../core/platform_detect.hpp"
#include "../../security/vmp_markers.hpp"
#include "../../sdk/world_to_screen.hpp"
#include <mutex>

inline bool isHoldingMouse = false;

namespace features
{
    static Vector3 GetBestBone(CPed* pEntity)
    {
        auto& acfg = cfg::aimbot_configs[cfg::current_weapon_category];
        int bone = acfg.aimbone;
        if (bone >= 0 && bone < bones::BONE_COUNT)
            return pEntity->BoneList[bone];

        Vector3 best = {0,0,0};
        float bestDist = FLT_MAX;
        Vector2 center(g.GameRect.right / 2.f, g.GameRect.bottom / 2.f);
        const Matrix view = read<Matrix>(g.Viewport + offsets::viewport::ViewMatrix);

        for (int i = 0; i < bones::BONE_COUNT; i++)
        {
            Vector3 pos = pEntity->BoneList[i];
            Vector2 screen;
            if (!WorldToScreen(view, pos, screen)) continue;
            float d = abs((center - screen).Length());
            if (d < bestDist) { bestDist = d; best = pos; }
        }
        return best;
    }

    // ── Проверяем, является ли ближайший к прицелу игрок другом ──
    // Вызывать только когда triggerState == 1 || 2 и trigger_ignore_friends включён
    static bool IsTriggerTargetFriend()
    {
        if (!cfg::trigger_ignore_friends) return false;
        if (!g.localplayer || !g.localplayer->IsValid()) return false;

        // Копируем список под мьютексом
        std::vector<CPed> local_list;
        {
            std::lock_guard<std::mutex> lock(g.EntityListMutex);
            local_list = g.EntityList;
        }

        if (local_list.empty()) return false;

        const Matrix view = read<Matrix>(g.Viewport + offsets::viewport::ViewMatrix);
        const Vector2 center(g.GameRect.right / 2.f, g.GameRect.bottom / 2.f);

        float bestDist = 200.f; // пиксели от прицела — порог
        CPed* bestPed = nullptr;

        for (auto& ped : local_list)
        {
            if (!ped.ptr) continue;
            if (ped.m_pVecPosition == Vector3(0, 0, 0)) continue;

            // Проецируем голову на экран
            Vector2 screen;
            Vector3 pos = ped.BoneList[bones::BONE_HEAD];
            if (pos == Vector3(0,0,0)) pos = ped.m_pVecPosition;
            if (!WorldToScreen(view, pos, screen)) continue;

            float dist = (center - screen).Length();
            if (dist < bestDist)
            {
                bestDist = dist;
                bestPed = &ped;
            }
        }

        if (!bestPed) return false;
        
        bool isIgnored = false;
        if (cfg::trigger_ignore_friends && bestPed->IsFriend()) isIgnored = true;
        if (cfg::trigger_ignore_admins && bestPed->IsAdmin()) isIgnored = true;
        
        return isIgnored;
    }

    // ── RageMP: тригер проверяет игрока под прицелом через WorldToScreen + triggerState ──
    inline void TriggerLoop_RageMP()
    {
        while (g.Run)
        {
            if (cfg::triggerbot)
            {
                bool keyPressed = false;
                if (cfg::triggerkey_mode == 0) // Toggle
                {
                    static bool toggled = false;
                    static bool lastState = false;
                    bool curState = (GetAsyncKeyState(cfg::triggerkey) & 0x8000) != 0;
                    if (curState && !lastState) toggled = !toggled;
                    lastState = curState;
                    keyPressed = toggled;
                }
                else // Hold
                {
                    keyPressed = (cfg::triggerkey == 0) ? true : ((GetAsyncKeyState(cfg::triggerkey) & 0x8000) != 0);
                }

                if (keyPressed)
                {
                    auto triggerState = read<BYTE>(g.ImageBase + offsets::trigger::trigger);

                    if (triggerState == 1 || triggerState == 2)
                    {
                        // Ignore friends: если цель ближайшая к прицелу — друг, не стреляем
                        if (cfg::trigger_ignore_friends && IsTriggerTargetFriend())
                        {
                            if (isHoldingMouse)
                            {
                                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                                isHoldingMouse = false;
                            }
                        }
                        else if (!isHoldingMouse)
                        {
                            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                            isHoldingMouse = true;
                        }
                    }
                    else if (isHoldingMouse)
                    {
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        isHoldingMouse = false;
                    }
                }
                else if (isHoldingMouse)
                {
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    isHoldingMouse = false;
                }

                Sleep(cfg::triggerdelay > 0 ? cfg::triggerdelay : 1);
            }
            else
            {
                if (isHoldingMouse)
                {
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    isHoldingMouse = false;
                }
                Sleep(10);
            }
        }

        if (isHoldingMouse)
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    }

    // ── AltV: тригер просто читает triggerState из памяти ──
    inline void TriggerLoop_AltV()
    {
        while (g.Run)
        {
            if (cfg::triggerbot)
            {
                bool keyPressed = false;
                if (cfg::triggerkey_mode == 0) // Toggle
                {
                    static bool toggled = false;
                    static bool lastState = false;
                    bool curState = (GetAsyncKeyState(cfg::triggerkey) & 0x8000) != 0;
                    if (curState && !lastState) toggled = !toggled;
                    lastState = curState;
                    keyPressed = toggled;
                }
                else // Hold
                {
                    keyPressed = (cfg::triggerkey == 0) ? true : ((GetAsyncKeyState(cfg::triggerkey) & 0x8000) != 0);
                }

                if (keyPressed)
                {
                    auto triggerState = read<BYTE>(g.ImageBase + offsets::trigger::trigger);

                    if (triggerState == 1 || triggerState == 2)
                    {
                        // Ignore friends: если цель ближайшая к прицелу — друг, не стреляем
                        if (cfg::trigger_ignore_friends && IsTriggerTargetFriend())
                        {
                            if (isHoldingMouse)
                            {
                                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                                isHoldingMouse = false;
                            }
                        }
                        else if (!isHoldingMouse)
                        {
                            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                            isHoldingMouse = true;
                        }
                    }
                    else if (isHoldingMouse)
                    {
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        isHoldingMouse = false;
                    }
                }
                else if (isHoldingMouse)
                {
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    isHoldingMouse = false;
                }

                Sleep(cfg::triggerdelay > 0 ? cfg::triggerdelay : 1);
            }
            else
            {
                if (isHoldingMouse)
                {
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    isHoldingMouse = false;
                }
                Sleep(10);
            }
        }

        if (isHoldingMouse)
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    }

    // ── Диспетчер: выбирает нужную реализацию по платформе ──
    inline void TriggerLoop()
    {
        if (platform::g_platform == Platform::RageMP)
            TriggerLoop_RageMP();
        else
            TriggerLoop_AltV();
    }
}
