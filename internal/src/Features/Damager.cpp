#include "Damager.h"

#include "RageV8Runtime.h"
#include "ServerProfile.h"
#include "../vmp_markers.hpp"
#include "../Menu/Menu.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace
{
    // One pass is one round trip, so the whole burst is fired from inside the
    // script rather than one hit per call: the difference between a kill and a
    // tickle is how many damage events land before the victim's client next
    // reports its health.
    constexpr char kBody[] =
R"JS(return (function(){try{
var g=(typeof globalThis!=='undefined'&&globalThis)||
(typeof global!=='undefined'&&global)||this||{};
var ctx=g.__ctx__;
var rage=(ctx&&ctx.mp)||g.mp||(typeof mp!=='undefined'?mp:null);
var st=g.__bfDmg1;
if(!st||typeof st!=='object'){
st={hits:0,passes:0,hash:0,hashOf:''};
try{g.__bfDmg1=st;}catch(_){}}
var out=function(hp,note){
return ['@dmg',st.hits,st.passes,hp,
String(note||'').replace(/[\t\r\n]/g,' ').slice(0,80)].join('\t');};
if(!rage||!rage.players||!rage.game)return out(-1,'no runtime');
var me=rage.players.local;
if(!me)return out(-1,'no local player');
var mine=0;try{mine=me.handle;}catch(_){}
if(!mine)return out(-1,'no local handle');
/* Resolved once and remembered: the name never changes within a session and
the hash lookup is not free at this rate. */
if(st.hashOf!==CFG.wn){
var h=0;
try{h=rage.game.joaat(CFG.wn);}catch(_){}
if(!h){try{h=rage.game.gameplay.getHashKey(CFG.wn);}catch(_){}}
st.hash=h||0;st.hashOf=CFG.wn;}
var wp=st.hash;
if(CFG.cur){
var cw=0;
try{cw=rage.game.weapon.getSelectedPedWeapon(mine);}catch(_){}
if(cw)wp=cw;}
if(!wp)return out(-1,'no weapon hash');
var tgt=null;
try{if(CFG.rid>=0&&rage.players.atRemoteId)
tgt=rage.players.atRemoteId(CFG.rid);}catch(_){}
/* Health is read from the victim's own entity, which is the one number that
says whether the server accepted anything: it is theirs to publish, and a value
that never moves means every hit was dropped. */
var health=function(){
try{if(tgt&&tgt.getHealth)return tgt.getHealth();}catch(_){}
try{if(tgt&&tgt.handle)
return rage.game.entity.getEntityHealth(tgt.handle);}catch(_){}
return -1;};
var hp=health();
/* A roleplay server maps its own 0..100 bar onto the game's 100..200, so there
an empty bar reads as a hundred; elsewhere zero means zero. The threshold comes
from the server profile, because hits landed on a body are noise on the wire. */
if(CFG.stop&&hp>=0&&hp<=CFG.down)return out(hp,'target down');
/* Three spellings of the same call. The first is what this runtime exposes,
the second is the variant that takes an entity to pass through, and the last
goes straight at the native for a build where neither wrapper is present. */
var fire=function(){
try{rage.game.weapon.shootSingleBulletBetweenCoords(
CFG.sx,CFG.sy,CFG.sz,CFG.tx,CFG.ty,CFG.tz,CFG.d,true,wp,mine,false,true,1500.0);
return 1;}catch(e){}
try{rage.game.weapon.shootSingleBulletBetweenCoordsIgnoreEntity(
CFG.sx,CFG.sy,CFG.sz,CFG.tx,CFG.ty,CFG.tz,CFG.d,true,wp,mine,false,true,
1500.0,0,0);
return 1;}catch(e){}
try{rage.game.invoke('0x867654CBC7606F2C',
CFG.sx,CFG.sy,CFG.sz,CFG.tx,CFG.ty,CFG.tz,CFG.d,true,wp,mine,false,true,1500.0);
return 1;}catch(e){}
return 0;};
var fired=0;
for(var i=0;i<CFG.n;++i)fired+=fire();
st.hits+=fired;++st.passes;
return out(health(),fired?'':'call refused');
}catch(e){return '@dmg\t0\t0\t-1\tfailed '+String(e).slice(0,40);}})();)JS";

    Damager::Settings g_Settings{};
    Damager::Status g_Status{};
    Damager::Target g_Target{};
    ULONGLONG g_TargetAt = 0;

    std::uint64_t g_PendingCommand = 0;
    std::uint64_t g_RuntimeGeneration = 0;
    ULONGLONG g_LastRequest = 0;
    volatile LONG g_Lock = 0;

    void Lock() noexcept
    {
        while (InterlockedCompareExchange(&g_Lock, 1, 0) != 0)
            Sleep(0);
    }

    void Unlock() noexcept
    {
        InterlockedExchange(&g_Lock, 0);
    }

    const char* WeaponName(int weapon) noexcept
    {
        switch (static_cast<Damager::WeaponKind>(weapon))
        {
        case Damager::WeaponKind::Knife:
            return "weapon_knife";
        default:
            return "weapon_unarmed";
        }
    }

    void ParseStatus(const char* text) noexcept
    {
        if (!text || std::strncmp(text, "@dmg", 4) != 0)
            return;
        int hits = 0;
        int passes = 0;
        int health = -1;
        const char* cursor = text + 4;
        int* const fields[] = {&hits, &passes, &health};
        for (int index = 0; index < 3; ++index)
        {
            if (*cursor != '\t')
                return;
            ++cursor;
            *fields[index] = std::atoi(cursor);
            while (*cursor && *cursor != '\t')
                ++cursor;
        }
        char detail[96]{};
        if (*cursor == '\t')
        {
            ++cursor;
            int length = 0;
            while (*cursor && *cursor != '\n' &&
                   length < static_cast<int>(sizeof(detail)) - 1)
                detail[length++] = *cursor++;
            detail[length] = 0;
        }
        Lock();
        g_Status.hits = hits;
        g_Status.passes = passes;
        g_Status.health = health;
        lstrcpynA(
            g_Status.detail,
            detail,
            static_cast<int>(sizeof(g_Status.detail)));
        Unlock();
    }
}

namespace Damager
{
    Settings& Access() noexcept
    {
        return g_Settings;
    }

    bool Armed() noexcept
    {
        if (!g_Settings.enabled)
            return false;
        if (g_Settings.holdKey <= 0)
            return true;
        return (GetAsyncKeyState(g_Settings.holdKey) & 0x8000) != 0;
    }

    void PublishTarget(const Target& target) noexcept
    {
        Lock();
        g_Target = target;
        g_TargetAt = target.valid ? GetTickCount64() : 0;
        Unlock();
    }

    void Reset() noexcept
    {
        g_PendingCommand = 0;
        g_LastRequest = 0;
        Lock();
        g_Status = {};
        g_Status.remoteId = -1;
        g_Status.health = -1;
        g_Target = {};
        g_TargetAt = 0;
        Unlock();
    }

    Status GetStatus() noexcept
    {
        Lock();
        const Status copy = g_Status;
        Unlock();
        return copy;
    }

    void Update() noexcept
    {
        const auto runtime = RageV8Runtime::GetStatus();
        const ULONGLONG now = GetTickCount64();
        if (runtime.generation != g_RuntimeGeneration)
        {
            g_RuntimeGeneration = runtime.generation;
            g_PendingCommand = 0;
            g_LastRequest = 0;
            Lock();
            g_Status = {};
            g_Status.remoteId = -1;
            g_Status.health = -1;
            Unlock();
        }

        if (g_PendingCommand)
        {
            if (RageV8Runtime::HasResultFor(g_PendingCommand))
            {
                RageV8Runtime::Result result{};
                if (RageV8Runtime::PollResultFor(g_PendingCommand, result) &&
                    result.success)
                    ParseStatus(result.text);
                g_PendingCommand = 0;
            }
            else if (now - g_LastRequest >= 2000 ||
                     runtime.state != RageV8Runtime::State::Ready)
                g_PendingCommand = 0;
        }

        const bool armed = Armed();
        Target target{};
        Lock();
        // The aim pass runs on the same thread as this, one call earlier, so a
        // target older than a couple of frames means it published nothing this
        // frame and there is nobody in the FOV any more.
        if (g_TargetAt && now - g_TargetAt <= 120)
            target = g_Target;
        g_Status.armed = armed;
        g_Status.hasTarget = target.valid;
        g_Status.remoteId = target.valid ? target.remoteId : -1;
        g_Status.distance = target.valid ? target.distance : 0.f;
        if (!armed)
        {
            g_Status.health = -1;
            g_Status.detail[0] = 0;
        }
        Unlock();

        if (!armed || !target.valid)
            return;

        const ULONGLONG gap =
            g_Settings.intervalMs > 30
                ? static_cast<ULONGLONG>(g_Settings.intervalMs)
                : 30ull;
        if (g_PendingCommand ||
            runtime.queuedCommands > 2 ||
            runtime.state != RageV8Runtime::State::Ready ||
            !runtime.rootCaptured ||
            now - g_LastRequest < gap)
            return;

        int damage = g_Settings.damage;
        if (damage < 1)
            damage = 1;
        if (damage > 2000)
            damage = 2000;
        int hits = g_Settings.hits;
        if (hits < 1)
            hits = 1;
        if (hits > 40)
            hits = 40;

        const bool current =
            g_Settings.weapon == static_cast<int>(WeaponKind::Current);

        char script[sizeof(kBody) + 700]{};
        const int header = _snprintf_s(
            script,
            sizeof(script),
            _TRUNCATE,
            "var CFG={rid:%d,d:%d,n:%d,stop:%d,down:%d,cur:%d,wn:'%s',"
            "sx:%.3f,sy:%.3f,sz:%.3f,tx:%.3f,ty:%.3f,tz:%.3f};\n",
            target.remoteId,
            damage,
            hits,
            g_Settings.stopWhenDown ? 1 : 0,
            ServerProfile::DownThreshold(),
            current ? 1 : 0,
            WeaponName(g_Settings.weapon),
            static_cast<double>(target.originX),
            static_cast<double>(target.originY),
            static_cast<double>(target.originZ),
            static_cast<double>(target.aimX),
            static_cast<double>(target.aimY),
            static_cast<double>(target.aimZ));
        if (header <= 0)
            return;
        SHADOW_PROTECT_MUT("Damager.Load");
        const char* body = SHADOW_DECRYPT_STRING_A(kBody);
        if (!body)
        {
            SHADOW_PROTECT_END();
            SecureZeroMemory(script, sizeof(script));
            return;
        }
        lstrcpynA(
            script + header,
            body,
            static_cast<int>(sizeof(script)) - header);
        SHADOW_FREE_STRING(body);
        SHADOW_PROTECT_END();

        g_LastRequest = now;
        const bool queued =
            RageV8Runtime::Enqueue(script, &g_PendingCommand);
        SecureZeroMemory(script, sizeof(script));
        if (!queued)
        {
            g_PendingCommand = 0;
            return;
        }
        if (g_Settings.notify)
        {
            static ULONGLONG s_LastNotify = 0;
            if (now - s_LastNotify > 1500)
            {
                s_LastNotify = now;
                char message[96]{};
                _snprintf_s(
                    message,
                    sizeof(message),
                    _TRUNCATE,
                    "%d x %d, %.1f m",
                    hits,
                    damage,
                    static_cast<double>(target.distance));
                Menu::PushNotification("Damager", message, true, 1200);
            }
        }
    }

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept
    {
        if (!output)
            return false;
        std::uint32_t bones = 0;
        for (int index = 0; index < kBoneCount; ++index)
            if (g_Settings.bones[index])
                bones |= 1u << index;
        const int written = _snprintf_s(
            output + used,
            capacity - used,
            _TRUNCATE,
            "dmg_on=%d\ndmg_key=%d\ndmg_fov=%.2f\ndmg_dist=%.2f\n"
            "dmg_damage=%d\ndmg_hits=%d\ndmg_delay=%d\ndmg_visible=%d\n"
            "dmg_stop=%d\ndmg_notify=%d\ndmg_weapon=%d\ndmg_bones=%d\n",
            g_Settings.enabled ? 1 : 0,
            g_Settings.holdKey,
            static_cast<double>(g_Settings.fov),
            static_cast<double>(g_Settings.maxDistance),
            g_Settings.damage,
            g_Settings.hits,
            g_Settings.intervalMs,
            g_Settings.visibleCheck ? 1 : 0,
            g_Settings.stopWhenDown ? 1 : 0,
            g_Settings.notify ? 1 : 0,
            g_Settings.weapon,
            static_cast<int>(bones));
        if (written <= 0)
            return false;
        used += static_cast<std::size_t>(written);
        return true;
    }

    void Import(const char* content) noexcept
    {
        if (!content)
            return;
        const auto find = [content](const char* key) -> const char* {
            const std::size_t length = std::strlen(key);
            const char* line = content;
            while (*line)
            {
                if (!std::strncmp(line, key, length) && line[length] == '=')
                    return line + length + 1;
                const char* next = std::strchr(line, '\n');
                if (!next)
                    break;
                line = next + 1;
            }
            return nullptr;
        };
        if (const char* value = find("dmg_on"))
            g_Settings.enabled = std::atoi(value) != 0;
        if (const char* value = find("dmg_key"))
            g_Settings.holdKey = std::atoi(value);
        if (const char* value = find("dmg_fov"))
        {
            const float fov = static_cast<float>(std::atof(value));
            if (fov >= kMinFov && fov <= kMaxFov)
                g_Settings.fov = fov;
        }
        if (const char* value = find("dmg_dist"))
        {
            const float distance = static_cast<float>(std::atof(value));
            if (distance >= kMinDistance && distance <= kMaxDistance)
                g_Settings.maxDistance = distance;
        }
        if (const char* value = find("dmg_damage"))
        {
            const int damage = std::atoi(value);
            if (damage >= 1 && damage <= 2000)
                g_Settings.damage = damage;
        }
        if (const char* value = find("dmg_hits"))
        {
            const int hits = std::atoi(value);
            if (hits >= 1 && hits <= 40)
                g_Settings.hits = hits;
        }
        if (const char* value = find("dmg_delay"))
        {
            const int delay = std::atoi(value);
            if (delay >= 30 && delay <= 2000)
                g_Settings.intervalMs = delay;
        }
        if (const char* value = find("dmg_visible"))
            g_Settings.visibleCheck = std::atoi(value) != 0;
        if (const char* value = find("dmg_stop"))
            g_Settings.stopWhenDown = std::atoi(value) != 0;
        if (const char* value = find("dmg_notify"))
            g_Settings.notify = std::atoi(value) != 0;
        if (const char* value = find("dmg_weapon"))
        {
            const int weapon = std::atoi(value);
            if (weapon >= 0 && weapon < static_cast<int>(WeaponKind::Count))
                g_Settings.weapon = weapon;
        }
        if (const char* value = find("dmg_bones"))
        {
            const std::uint32_t bones =
                static_cast<std::uint32_t>(std::atoi(value));
            for (int index = 0; index < kBoneCount; ++index)
                g_Settings.bones[index] = (bones & (1u << index)) != 0;
        }
    }
}
