#include "security/xorstr.hpp"
#include "memory.hpp"
#include "../security/vmp_markers.hpp"
#include "../driver/driver_io.hpp"
#include "../core/globals.hpp"
#include "../core/platform_detect.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include "signature.hpp"
#include "../sdk/offsets.hpp"

namespace memory
{
    bool init()
    {
        SHADOW_PROTECT_MUT(xorstr_("memory_init"));
        while (true) {
            driver::process_id = driver::find_process(L"GTA5.exe");
            if (driver::process_id != 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        g.ImageBase = driver::find_image();
        if (g.ImageBase == 0) {
            SHADOW_PROTECT_END();
            return false;
        }

        // ── Определяем платформу по классу окна
        // RageMP создаёт окно позже, чем стартует процесс — ждём до 30 секунд
        Platform detected = Platform::Unknown;
        HWND game_hwnd = nullptr;

        for (int attempt = 0; attempt < 30 && detected == Platform::Unknown; ++attempt)
        {
            game_hwnd = platform::detect(detected, (DWORD)driver::process_id);
            if (detected == Platform::Unknown)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (game_hwnd) {
            g.GameHwnd = game_hwnd;
        }

        // ── Загрузка оффсетов в зависимости от платформы
        if (detected == Platform::RageMP)
        {
            // RageMP: статичные оффсеты — никакого паттерн-сканирования
            offsets::GameWorld        = offsets::ragemp_static::GameWorld;
            offsets::ReplayInterface  = offsets::ragemp_static::ReplayInterface;
            offsets::ViewPort         = offsets::ragemp_static::ViewPort;
            offsets::Camera           = offsets::ragemp_static::Camera;
            offsets::silent::silent   = offsets::ragemp_static::silent::silent;
            offsets::trigger::trigger = offsets::ragemp_static::trigger::trigger;

            // BlipList и AimPedPtr — паттерн одинаков для всех версий GTA5
            memory::signature scanner(g.ImageBase);

            uintptr_t blip = scanner.scan(xorstr_("4C 8D 05 ? ? ? ? 0F B7 C1")).add(3).rip().as<uintptr_t>();
            if (blip) offsets::BlipList = blip - g.ImageBase;

            uintptr_t aim = scanner.scan(xorstr_("E8 ? ? ? ? B1 01 48 81 C4")).add(1).rip().add(0x293).rip().as<uintptr_t>();
            if (aim) g.AimPedPtr = aim - g.ImageBase;
        }
        else
        {
            // Alt:V (или Unknown — пробуем паттерн-сканирование)
            memory::signature scanner(g.ImageBase);

            uintptr_t gw = scanner.scan(xorstr_("48 8B 05 ? ? ? ? 48 8B 58 08 48 85 DB 74 32")).add(3).rip().as<uintptr_t>();
            if (gw) offsets::GameWorld = gw - g.ImageBase;

            uintptr_t ri = scanner.scan(xorstr_("48 8D 0D ? ? ? ? 48 8B D7 E8 ? ? ? ? 48 8D 0D ? ? ? ? 8A D8 E8 ? ? ? ? 84 DB 75 13 48 8D 0D")).add(3).rip().as<uintptr_t>();
            if (ri) offsets::ReplayInterface = ri - g.ImageBase;

            uintptr_t vp = scanner.scan(xorstr_("48 8B 15 ? ? ? ? 48 8D 2D ? ? ? ? 48 8B CD")).add(3).rip().as<uintptr_t>();
            if (vp) offsets::ViewPort = vp - g.ImageBase;

            uintptr_t cam = scanner.scan(xorstr_("48 8B 05 ? ? ? ? 48 8B 98 ? ? ? ? EB")).add(3).rip().as<uintptr_t>();
            if (cam) offsets::Camera = cam - g.ImageBase;

            uintptr_t blip = scanner.scan(xorstr_("4C 8D 05 ? ? ? ? 0F B7 C1")).add(3).rip().as<uintptr_t>();
            if (blip) offsets::BlipList = blip - g.ImageBase;

            offsets::silent::silent   = offsets::altv_static::silent::silent;
            offsets::trigger::trigger = offsets::altv_static::trigger::trigger;

            // AimPedPtr — общий паттерн для валшота
            uintptr_t aim = scanner.scan(xorstr_("E8 ? ? ? ? B1 01 48 81 C4")).add(1).rip().add(0x293).rip().as<uintptr_t>();
            if (aim) g.AimPedPtr = aim - g.ImageBase;

            if (offsets::GameWorld == 0 || offsets::ReplayInterface == 0) {
                SHADOW_PROTECT_END();
                return false;
            }
        }
        SHADOW_PROTECT_END();
        return true;
    }
} // namespace memory








