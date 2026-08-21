#include "security/xorstr.hpp"
#include "../../core/globals.hpp"
#include "../../memory/memory_access.hpp"
#include "../../sdk/math_utils.hpp"
#include "../../sdk/world_to_screen.hpp"
#include "../../config/settings.hpp"
#include "../../sdk/bones.hpp"
#include "../../sdk/weapons.hpp"
#include "../../core/platform_detect.hpp"
#include "../../security/vmp_markers.hpp"

namespace magicbullet {
    inline std::vector<uint8_t> magicBulletfunc{
        0x4C, 0x8D, 0x0D, 0x9F,0xFF,0xFF,0xFF,
        0x4C, 0x8D, 0x05, 0xa8,0xFF,0xFF,0xFF,
        0xe9
    };
    inline std::vector<uint8_t> SilentFunc{
        0x4c,0x8d,0x45,0x17,
        0x4C, 0x8D, 0x0d, 0xab,0xFF,0xFF,0xFF,
        0xe9
    };
    inline std::vector<uint8_t> jmpMyMagic{ 0xe9 };
};

class ShellcodeHelper
{
public:
    ShellcodeHelper(std::vector<uint8_t>& vec) { this->shellCode = vec; }
    ShellcodeHelper() {};

    template<typename T>
    void addShiftedAddress(T address) {
        for (int i = 0; i < 8; i++)
            this->shellCode.push_back((uint8_t)(address >> i * 8) & 0xFF);
    }

    void addShiftedAddress(ULONGLONG origin, ULONGLONG dest);
    void add(std::vector<uint8_t> otherShell);
    void patch(unsigned long long address);
    void loadOriginalShell(ULONGLONG address, int sizeToRead);
    void restore(ULONGLONG address);
    size_t size();

private:
    std::vector<uint8_t> shellCode = {};
    std::vector<uint8_t> shellCodeOriginal = {};
};

inline auto mBullet = ShellcodeHelper(magicbullet::magicBulletfunc);
inline auto silent = ShellcodeHelper(magicbullet::SilentFunc);
inline auto mBulletpatch = ShellcodeHelper(magicbullet::jmpMyMagic);

inline void ShellcodeHelper::addShiftedAddress(ULONGLONG origin, ULONGLONG dest)
{
    auto addr = origin + 5;
    auto relative = dest - addr;
    for (int i = 0; i < 4; i++)
        shellCode.push_back((uint8_t)(relative >> i * 8) & 0xFF);
}

inline void ShellcodeHelper::add(std::vector<uint8_t> otherShell)
{
    for (size_t i = 0; i < otherShell.size(); i++)
        this->shellCode.push_back(otherShell.at(i));
}

inline void ShellcodeHelper::patch(unsigned long long address)
{
    for (size_t i = 0; i < shellCode.size(); i++)
        write<uint8_t>(address + i, shellCode.at(i));
}

inline void ShellcodeHelper::loadOriginalShell(ULONGLONG address, int sizeToRead)
{
    for (int i = 0; i < sizeToRead; i++) {
        auto byteCopy = read<uint8_t>(address + i);
        this->shellCodeOriginal.push_back(byteCopy);
    }
}

inline void ShellcodeHelper::restore(ULONGLONG address)
{
    if (this->shellCodeOriginal.size() > 0) {
        for (int i = 0; i < shellCodeOriginal.size(); i++)
            write<uint8_t>(address + i, shellCodeOriginal.at(i));
    }
}

inline size_t ShellcodeHelper::size() { return this->shellCode.size(); }

inline void silentinit()
{
    static bool init_done = false;
    if (init_done) return;
    init_done = true;

    auto writeOffset = g.ImageBase + offsets::silent::silent;
    auto myFuncOfsset = g.ImageBase + 0x55a;
    auto caller = g.ImageBase + offsets::silent::silent + 0x5;

    silent.addShiftedAddress(myFuncOfsset + silent.size() - 1, caller);
    mBullet.addShiftedAddress(myFuncOfsset + mBullet.size() - 1, caller);

    mBulletpatch.loadOriginalShell(writeOffset, 5);
    mBulletpatch.addShiftedAddress(writeOffset, myFuncOfsset);
}

inline Vector3 GetBestBone(CPed* pEntity, const Matrix& view)
{
    Vector3 best = pEntity->BoneList[0];
    float bestDist = FLT_MAX;
    Vector2 center(g.GameRect.right / 2.f, g.GameRect.bottom / 2.f);

    for (int i = 0; i < 4; i++) {
        Vector3 pos = pEntity->BoneList[i];
        Vector2 screen;
        if (!WorldToScreen(view, pos, screen)) continue;
        float d = (center - screen).Length();
        if (d < bestDist) { bestDist = d; best = pos; }
    }
    return best;
}

inline std::pair<CPed, Vector3> FindBestTarget()
{
    std::pair<CPed, Vector3> target{};
    const Matrix ViewMatrix = read<Matrix>(g.Viewport + offsets::viewport::ViewMatrix);

    float MinFov = FLT_MAX;
    Vector2 Center = Vector2(g.GameRect.right / 2.f, g.GameRect.bottom / 2.f);

    if (!g.localplayer || !g.localplayer->UpdateFast()) return target;

    auto& acfg = cfg::aimbot_configs[cfg::current_weapon_category];

    // Snapshot the entity list under mutex — then work on the copy (no lock held during heavy work)
    std::vector<CPed> local_list;
    {
        std::lock_guard<std::mutex> lock(g.EntityListMutex);
        local_list = g.EntityList;
    }

    for (auto& ped : local_list)
    {
        CPed* pEntity = &ped;

        if (pEntity->ptr == 0) continue;
        if (g.localplayer && pEntity->ptr == g.localplayer->ptr) continue;

        if (!pEntity->UpdateFast()) continue;
        if (acfg.ignore_friends && pEntity->IsFriend()) continue;
        if (acfg.ignore_admins && pEntity->IsAdmin()) continue;

        // Safe aimbone index — BoneList snapshot is already in the entity copy
        Vector3 bonePos;
        const int aimboneIdx = acfg.aimbone;
        if (aimboneIdx >= 0 && aimboneIdx < bones::BONE_COUNT)
            bonePos = pEntity->BoneList[aimboneIdx]; // safe: reading from local copy
        else
            bonePos = GetBestBone(pEntity, ViewMatrix);

        if (Vec3_Empty(bonePos)) continue;

        Vector2 pScreen;
        if (!WorldToScreen(ViewMatrix, bonePos, pScreen)) continue;

        float FOV = abs((Center - pScreen).Length());

        bool inFov = (acfg.fov == 0) ? true : (FOV < (float)acfg.fov);
        if (inFov && FOV < MinFov)
        {
            target.first = ped;
            target.second = bonePos;
            MinFov = FOV;
        }
    }

    return target;
}

namespace features
{
    inline void AimBot()
    {
        while (g.Run)
        {
            auto& acfg = cfg::aimbot_configs[cfg::current_weapon_category];
            if (acfg.aimbot && acfg.aimtype == 0)
            {
                bool keyPressed;
                if (cfg::aimkey == 0)
                {
                    keyPressed = true;
                }
                else
                {
                    keyPressed = (GetAsyncKeyState(cfg::aimkey) & 0x8000) != 0;
                    static bool toggleState = false;
                    static bool wasKeyPressed = false;
                    if (cfg::aimkey_mode == 0) // Toggle
                    {
                        if (keyPressed && !wasKeyPressed) toggleState = !toggleState;
                        wasKeyPressed = keyPressed;
                        keyPressed = toggleState;
                    }
                }

                if (keyPressed)
                {
                    std::pair<CPed, Vector3> target = FindBestTarget();

                if (!Vec3_Empty(target.second))
                {
                    if (offsets::Camera != 0) {
                        uintptr_t camera = read<uintptr_t>(g.ImageBase + offsets::Camera);
                        if (camera) {
                            Vector3 ViewAngle = read<Vector3>(camera + 0x3D0);
                            Vector3 CameraPos = read<Vector3>(camera + 0x60);

                            Vector3 TargetAngle = CalcAngle(CameraPos, target.second);
                            NormalizeAngles(TargetAngle);

                            if (acfg.smooth) {
                                if (acfg.smooth_type == 0) {
                                    TargetAngle = LerpDynamic(ViewAngle, TargetAngle, acfg.smooth_factor);
                                } else {
                                    TargetAngle = LerpConstant(ViewAngle, TargetAngle, acfg.smooth_factor);
                                }
                            }

                            if (!Vec3_Empty(TargetAngle))
                            {
                                write<Vector3>(camera + 0x40, TargetAngle);
                                write<Vector3>(camera + 0x3D0, TargetAngle);
                            }
                        }
                    }
                }
                }
            }
            Sleep(1);
        }
        
    }

    inline void Silent()
    {
        auto writeOffset = g.ImageBase + offsets::silent::silent;
        auto myFuncOfsset = g.ImageBase + 0x55a;
        silentinit();

        static bool isPatched = false;
        while (g.Run)
        {
            auto& acfg = cfg::aimbot_configs[cfg::current_weapon_category];
            if (acfg.aimbot && acfg.aimtype == 1)
            {
                // aimkey == 0 → сайлент всегда активен без клавиши
                bool keyPressed;
                if (cfg::aimkey == 0)
                {
                    keyPressed = true;
                }
                else
                {
                    keyPressed = (GetAsyncKeyState(cfg::aimkey) & 0x8000) != 0;
                    static bool toggleState = false;
                    static bool wasKeyPressed = false;
                    if (cfg::aimkey_mode == 0) // Toggle
                    {
                        if (keyPressed && !wasKeyPressed) toggleState = !toggleState;
                        wasKeyPressed = keyPressed;
                        keyPressed = toggleState;
                    }
                }

                if (keyPressed)
                {
                    std::pair<CPed, Vector3> target = FindBestTarget();

                    if (Vec3_Empty(target.second) || target.first.ptr == 0)
                    {
                        if (isPatched) {
                            mBulletpatch.restore(writeOffset);
                            isPatched = false;
                        }
                        Sleep(1);
                        continue;
                    }

                    Vector3 shoot_pos = target.second;
                    
                    static bool currently_wallshot = false;
                    bool should_repatch = false;
                    if (isPatched && currently_wallshot != cfg::enable_wallshot) {
                        should_repatch = true;
                    }

                    if (cfg::enable_wallshot) {
                        Vector3 magicStartPos = shoot_pos;
                        if (g.localplayer && g.localplayer->ptr) {
                            Vector3 localPos = g.localplayer->m_pVecPosition;
                            Vector3 dir(
                                shoot_pos.x - localPos.x,
                                shoot_pos.y - localPos.y,
                                shoot_pos.z - localPos.z
                            );

                            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                            if (len > 0.001f) {
                                dir.x /= len;
                                dir.y /= len;
                                dir.z /= len;
                            } else {
                                dir = Vector3(0.f, 1.f, 0.f);
                            }
                            magicStartPos = Vector3(
                                shoot_pos.x - dir.x * 0.20f,
                                shoot_pos.y - dir.y * 0.20f,
                                shoot_pos.z - dir.z * 0.20f + 0.05f
                            );

                        }

                        // mBullet shellcode: r9(vEnd)=0x500, r8(vStart)=0x510
                        write<Vector3>((ULONGLONG)g.ImageBase + 0x500, shoot_pos);
                        write<Vector3>((ULONGLONG)g.ImageBase + 0x510, magicStartPos);
                    } else {
                        // Silent shellcode: r9(vEnd)=0x510
                        write<Vector3>((ULONGLONG)g.ImageBase + 0x510, shoot_pos);
                    }

                    if (!isPatched || should_repatch)
                    {
                        if (cfg::enable_wallshot) {
                            mBullet.patch(myFuncOfsset);
                        } else {
                            silent.patch(myFuncOfsset);
                        }
                        mBulletpatch.patch(writeOffset);
                        isPatched = true;
                        currently_wallshot = cfg::enable_wallshot;
                    }
                    Sleep(5);
                }
                else if (isPatched)
                {
                    mBulletpatch.restore(writeOffset);
                    isPatched = false;
                }
            }
            else if (isPatched)
            {
                mBulletpatch.restore(writeOffset);
                isPatched = false;
            }
            Sleep(1);
        }
        
    }

    inline void aimbot_thread()
    {
        // Запускаем каждый тип аима в своем потоке, чтобы они не блокировали друг друга
        std::thread([] { features::AimBot(); }).detach();
        std::thread([] { features::Silent(); }).detach();

        while (g.Run)
        {
            if (g.localplayer && g.localplayer->ptr) {
                uint32_t weaponHash = g.localplayer->get_weaponhash();
                if (cfg::enable_weapon_configs) {
                    cfg::current_weapon_category = weapons::get_weapon_category(weaponHash);
                } else {
                    cfg::current_weapon_category = 0;
                }
            }
            Sleep(10);
        }
    }
}

