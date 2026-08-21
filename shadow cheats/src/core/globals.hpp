#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>
#include "../sdk/CPed.hpp"

struct Object
{
    uintptr_t ptr;
    Vector3   position;
    uint32_t  hash;
    uintptr_t attachmentExtension;
    char      name[32] = {0};
    bool      should_draw = false;
    float     dist_to_local = 0.f;
};

#include <mutex>

struct Global
{
    uintptr_t ImageBase = 0;
    uintptr_t GWorld = 0;
    uintptr_t Replayinterface = 0;
    uintptr_t Viewport = 0;
    uintptr_t Camera = 0;
    uintptr_t AimPedPtr = 0;  // указатель на структуру CPed аим-цели (валшот)

    CPed* localplayer = nullptr;
    std::mutex EntityListMutex;
    std::vector<CPed>   EntityList;
    std::vector<Object> ObjectList;

    RECT GameRect{};
    HWND GameHwnd = nullptr;

    bool Run = false;
};

inline Global g;

// Глобальная функция логгирования удалена
