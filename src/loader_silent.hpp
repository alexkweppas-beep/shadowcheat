#pragma once

#include <windows.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <iostream>

// Из main.cpp
template<typename T> T TriggerReadVM(int32_t game_pid, uint64_t addr);
template<typename T> void TriggerWriteVM(int32_t game_pid, uint64_t addr, T val);

namespace loader_silent {

    // --- Математика ---
    struct Vector2 {
        float x, y;
        Vector2() : x(0), y(0) {}
        Vector2(float _x, float _y) : x(_x), y(_y) {}
        Vector2 operator-(const Vector2& v) const { return Vector2(x - v.x, y - v.y); }
        float Length() const { return std::sqrt(x * x + y * y); }
    };

    struct Vector3 {
        float x, y, z;
        Vector3() : x(0), y(0), z(0) {}
        Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
        Vector3 operator+(const Vector3& v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
        Vector3 operator-(const Vector3& v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
        Vector3 operator*(float f) const { return Vector3(x * f, y * f, z * f); }
        bool operator==(const Vector3& v) const { return x == v.x && y == v.y && z == v.z; }
        float Length() const { return std::sqrt(x * x + y * y + z * z); }
    };

    struct Vector4 {
        float x, y, z, w;
        Vector4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
    };

    struct Matrix {
        union {
            struct {
                float _11, _12, _13, _14;
                float _21, _22, _23, _24;
                float _31, _32, _33, _34;
                float _41, _42, _43, _44;
            };
            float m[4][4];
        };

        Matrix Transpose() const {
            Matrix out;
            out._11 = _11; out._12 = _21; out._13 = _31; out._14 = _41;
            out._21 = _12; out._22 = _22; out._23 = _32; out._24 = _42;
            out._31 = _13; out._32 = _23; out._33 = _33; out._34 = _43;
            out._41 = _14; out._42 = _24; out._43 = _34; out._44 = _44;
            return out;
        }
    };

    inline bool Vec3_Empty(const Vector3& v) {
        return v.x == 0.f && v.y == 0.f && v.z == 0.f;
    }

    // --- Оффсеты ---
    namespace offsets {
        constexpr uintptr_t GameWorld = 0x25B14B0;
        constexpr uintptr_t ReplayInterface = 0x1FBD4F0;
        constexpr uintptr_t ViewPort = 0x201DBA0;
        
        constexpr uintptr_t EntityListPtr = 0x18;
        constexpr uintptr_t EntityListBase = 0x100;
        
        constexpr uintptr_t silent = 0x103891E;

        namespace ped {
            constexpr uintptr_t Health = 0x280;
            constexpr uintptr_t MaxHealth = 0x284;
            constexpr uintptr_t BoneMatrix = 0x60;
            constexpr uintptr_t BoneList = 0x410; // CNetObjPlayer
        }
        namespace viewport {
            constexpr uintptr_t ViewMatrix = 0x24C;
        }
    }

    // --- WorldToScreen ---
    inline bool WorldToScreen(const Matrix& ViewMatrix, const Vector3& vWorld, Vector2& vOut, int screenWidth, int screenHeight) {
        const Matrix v = ViewMatrix.Transpose();
        const Vector4 row1(v._21, v._22, v._23, v._24);
        const Vector4 row2(v._31, v._32, v._33, v._34);
        const Vector4 row3(v._41, v._42, v._43, v._44);

        Vector3 vec_out(
            row1.x * vWorld.x + row1.y * vWorld.y + row1.z * vWorld.z + row1.w,
            row2.x * vWorld.x + row2.y * vWorld.y + row2.z * vWorld.z + row2.w,
            row3.x * vWorld.x + row3.y * vWorld.y + row3.z * vWorld.z + row3.w);

        if (vec_out.z <= 0.001f) return false;

        vec_out.z = 1.f / vec_out.z;
        vec_out.x *= vec_out.z;
        vec_out.y *= vec_out.z;

        const float w = static_cast<float>(screenWidth);
        const float h = static_cast<float>(screenHeight);

        vOut.x = w * 0.5f + (0.5f * vec_out.x * w + 0.5f);
        vOut.y = h * 0.5f - (0.5f * vec_out.y * h + 0.5f);
        return true;
    }

    // --- Shellcode Helper ---
    class ShellcodeHelper {
    public:
        std::vector<uint8_t> shellCode = {};
        std::vector<uint8_t> shellCodeOriginal = {};

        ShellcodeHelper(std::vector<uint8_t> vec) : shellCode(vec) {}
        ShellcodeHelper() {}

        void addShiftedAddress(ULONGLONG origin, ULONGLONG dest) {
            auto addr = origin + 5;
            auto relative = dest - addr;
            for (int i = 0; i < 4; i++) {
                shellCode.push_back((uint8_t)(relative >> i * 8) & 0xFF);
            }
        }

        void patch(int32_t game_pid, unsigned long long address) {
            for (size_t i = 0; i < shellCode.size(); i++) {
                TriggerWriteVM<uint8_t>(game_pid, address + i, shellCode[i]);
            }
        }

        void loadOriginalShell(int32_t game_pid, ULONGLONG address, int sizeToRead) {
            shellCodeOriginal.clear();
            for (int i = 0; i < sizeToRead; i++) {
                auto byteCopy = TriggerReadVM<uint8_t>(game_pid, address + i);
                shellCodeOriginal.push_back(byteCopy);
            }
        }

        void restore(int32_t game_pid, ULONGLONG address) {
            if (shellCodeOriginal.size() > 0) {
                for (size_t i = 0; i < shellCodeOriginal.size(); i++) {
                    TriggerWriteVM<uint8_t>(game_pid, address + i, shellCodeOriginal[i]);
                }
            }
        }

        size_t size() { return shellCode.size(); }
    };

    namespace magicbullet {
        inline std::vector<uint8_t> SilentFunc = {
            0x4c, 0x8d, 0x45, 0x17,
            0x4C, 0x8D, 0x0d, 0xab, 0xFF, 0xFF, 0xFF,
            0xe9
        };
        inline std::vector<uint8_t> magicBulletfunc = {
            0x4C, 0x8D, 0x0D, 0x9F, 0xFF, 0xFF, 0xFF,
            0x4C, 0x8D, 0x05, 0xa8, 0xFF, 0xFF, 0xFF,
            0xe9
        };
        inline std::vector<uint8_t> jmpMyMagic = { 0xe9 };
    }

    // Глобальные переменные сайлента
    struct SilentContext {
        ShellcodeHelper silent_sh;
        ShellcodeHelper mBulletpatch_sh;
        bool isPatched = false;
        bool initialized = false;
    };
    inline SilentContext ctx_silent;

    inline void SilentInit(int32_t game_pid, uint64_t ImageBase) {
        if (ctx_silent.initialized) return;

        ctx_silent.silent_sh = ShellcodeHelper(magicbullet::SilentFunc);
        ctx_silent.mBulletpatch_sh = ShellcodeHelper(magicbullet::jmpMyMagic);

        auto writeOffset = ImageBase + offsets::silent;
        auto myFuncOfsset = ImageBase + 0x55a;
        auto caller = ImageBase + offsets::silent + 0x5;

        ctx_silent.silent_sh.addShiftedAddress(myFuncOfsset + ctx_silent.silent_sh.size() - 1, caller);

        ctx_silent.mBulletpatch_sh.loadOriginalShell(game_pid, writeOffset, 5);
        ctx_silent.mBulletpatch_sh.addShiftedAddress(writeOffset, myFuncOfsset);
        
        ctx_silent.initialized = true;
    }

    // --- Логика Аима ---
    
    // Структура кэша, чтобы не читать матрицу костей каждый раз
    struct CachedPed {
        uintptr_t ptr;
        float health;
        float maxHealth;
        Vector3 headPos; // fallback если матрица костей сложна
    };

    inline Vector3 Vec3_Transform(const Vector3* v, const Matrix* m) {
        return Vector3(
            (v->x * m->_11) + (v->y * m->_21) + (v->z * m->_31) + m->_41,
            (v->x * m->_12) + (v->y * m->_22) + (v->z * m->_32) + m->_42,
            (v->x * m->_13) + (v->y * m->_23) + (v->z * m->_33) + m->_43
        );
    }

    inline Vector3 GetPedBonePos(int32_t pid, uintptr_t pedPtr, int boneIndex) {
        Matrix matrix = TriggerReadVM<Matrix>(pid, pedPtr + offsets::ped::BoneMatrix);

        // CNetObjPlayer starts at 0x410. Each CBoneFast element is 16 bytes.
        Vector3 pos = TriggerReadVM<Vector3>(pid, pedPtr + offsets::ped::BoneList + (boneIndex * 16));
        
        return Vec3_Transform(&pos, &matrix);
    }

    inline std::pair<uintptr_t, Vector3> FindBestTarget(int32_t game_pid, uint64_t ImageBase, uintptr_t localPlayerPtr, float fov_limit, int screenW, int screenH, int hitbox) {
        std::pair<uintptr_t, Vector3> target = {0, Vector3()};
        
        uintptr_t viewportPtr = TriggerReadVM<uintptr_t>(game_pid, ImageBase + offsets::ViewPort);
        Matrix ViewMatrix = TriggerReadVM<Matrix>(game_pid, viewportPtr + offsets::viewport::ViewMatrix);

        uintptr_t replayInterface = TriggerReadVM<uintptr_t>(game_pid, ImageBase + offsets::ReplayInterface);
        uintptr_t listPtr = TriggerReadVM<uintptr_t>(game_pid, replayInterface + offsets::EntityListPtr);
        uintptr_t entities = TriggerReadVM<uintptr_t>(game_pid, listPtr + offsets::EntityListBase);

        float MinFov = fov_limit * 10.0f; // Перевод fov в пиксели (приблизительно)
        Vector2 Center(screenW / 2.f, screenH / 2.f);

        // Определяем какие кости проверять (0: Голова, 1: Грудь, 2: Пах, 3: Nearest)
        std::vector<int> bones_to_check;
        if (hitbox == 0) bones_to_check = {0}; // Head
        else if (hitbox == 1) bones_to_check = {8}; // Chest (Spine3)
        else if (hitbox == 2) bones_to_check = {9}; // Pelvis (Spine2)
        else if (hitbox == 3) bones_to_check = {0, 7, 8, 9}; // Nearest: Head, Neck, Chest, Pelvis
        else bones_to_check = {0}; // Fallback

        // Читаем до 64 игроков для производительности (по сети редко больше)
        for (int i = 0; i < 64; ++i) {
            uintptr_t pedPtr = TriggerReadVM<uintptr_t>(game_pid, entities + (i * 0x10));
            if (pedPtr == 0 || pedPtr == localPlayerPtr) continue;

            float health = TriggerReadVM<float>(game_pid, pedPtr + offsets::ped::Health);
            float maxHealth = TriggerReadVM<float>(game_pid, pedPtr + offsets::ped::MaxHealth);
            
            if (health <= 0 || health > maxHealth + 100.0f) continue; // Мертв или невалиден

            for (int boneIndex : bones_to_check) {
                Vector3 bonePos = GetPedBonePos(game_pid, pedPtr, boneIndex); 
                if (Vec3_Empty(bonePos)) continue;

                Vector2 pScreen;
                if (!WorldToScreen(ViewMatrix, bonePos, pScreen, screenW, screenH)) continue;

                float FOV = (Center - pScreen).Length();

                if (FOV < MinFov) {
                    target.first = pedPtr;
                    target.second = bonePos;
                    MinFov = FOV;
                }
            }
        }
        return target;
    }

    inline void SilentAimTick(int32_t game_pid, uint64_t ImageBase, bool is_holding, float fov_limit, int hitbox) {
        uintptr_t gWorld = TriggerReadVM<uintptr_t>(game_pid, ImageBase + offsets::GameWorld);
        uintptr_t localPlayer = TriggerReadVM<uintptr_t>(game_pid, gWorld + 0x8);

        if (!localPlayer) return;

        SilentInit(game_pid, ImageBase);

        if (is_holding) {
            // Получаем разрешение экрана игры из Viewport
            uintptr_t viewportPtr = TriggerReadVM<uintptr_t>(game_pid, ImageBase + offsets::ViewPort);
            // Либо используем системное, так как лоадер работает поверх.
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            int screenH = GetSystemMetrics(SM_CYSCREEN);

            auto target = FindBestTarget(game_pid, ImageBase, localPlayer, fov_limit, screenW, screenH, hitbox);

            if (!Vec3_Empty(target.second) && target.first != 0) {
                // Пишем координаты в память
                TriggerWriteVM<Vector3>(game_pid, ImageBase + 0x500, target.second + Vector3(0, 0, 0.4f));
                TriggerWriteVM<Vector3>(game_pid, ImageBase + 0x510, target.second);

                if (!ctx_silent.isPatched) {
                    ctx_silent.silent_sh.patch(game_pid, ImageBase + 0x55a);
                    ctx_silent.mBulletpatch_sh.patch(game_pid, ImageBase + offsets::silent);
                    ctx_silent.isPatched = true;
                }
            } else if (ctx_silent.isPatched) {
                // Цель потеряна, отменяем патч
                ctx_silent.mBulletpatch_sh.restore(game_pid, ImageBase + offsets::silent);
                ctx_silent.isPatched = false;
            }
        } else if (ctx_silent.isPatched) {
            // Кнопка отпущена, отменяем патч
            ctx_silent.mBulletpatch_sh.restore(game_pid, ImageBase + offsets::silent);
            ctx_silent.isPatched = false;
        }
    }
    
    // Вызывается при выключении функции для гарантированной очистки
    inline void SilentCleanup(int32_t game_pid, uint64_t ImageBase) {
        if (ctx_silent.isPatched) {
            ctx_silent.mBulletpatch_sh.restore(game_pid, ImageBase + offsets::silent);
            ctx_silent.isPatched = false;
        }
        ctx_silent.initialized = false;
    }
}
