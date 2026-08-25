#include "ServerProfile.h"

#include "RageV8Runtime.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace
{
    // Everything the three old fingerprints looked at, in one pass. The order
    // matters: the pause header is the strongest signal and the only one that
    // tells the two roleplay servers we know apart from each other, so a header
    // naming one of them settles the question before anything else is read.
    constexpr char kProbe[] =
R"JS(return (function(){try{
var g=(typeof globalThis!=='undefined'&&globalThis)||
(typeof global!=='undefined'&&global)||this||{};
var ctx=g.__ctx__;
var rage=(ctx&&ctx.mp)||g.mp||(typeof mp!=='undefined'?mp:null);
if(!rage)return '@srv\tnone\tno runtime';
var live=false;
try{live=!!(rage.players&&rage.players.local);}catch(_){}
var hdr='';
try{hdr=String(rage.game.gxt.get('PM_PAUSE_HDR')||'').toLowerCase();}catch(_){}
var urls='';
try{if(rage.browsers&&rage.browsers.forEach)rage.browsers.forEach(function(b){
try{if(b&&b.url)urls+=' '+String(b.url).toLowerCase();}catch(_){}});}catch(_){}
var kind='';
if(hdr.indexOf('quant')>=0)kind='quant';
else if(hdr.indexOf('ukraine')>=0)kind='ukraine';
else if(hdr.indexOf('gta5rp')>=0||hdr.indexOf('5rp')>=0)kind='fiverp';
if(!kind){
if(urls.indexOf('quant')>=0)kind='quant';
else if(urls.indexOf('ukraine-gta5')>=0||urls.indexOf('ukrainegta')>=0)
kind='ukraine';
}
/* Server-specific globals, which survive a pause header we could not read. */
if(!kind){try{if(rage.bizStealing!=null)kind='ukraine';}catch(_){}}
if(!kind){try{if(ctx&&ctx.api)kind='fiverp';}catch(_){}}
if(!kind){try{if(g.__butterflyFiveRpApi)kind='fiverp';}catch(_){}}
if(!kind){try{var dk=g.__butterflyFiveRpDataKeys||rage.serverDataKeys;
if(dk&&dk.isAdmin)kind='fiverp';}catch(_){}}
/* Nothing matched. That is only worth reporting as "some other server" once
the session is far enough along for the evidence to have been there: at the
loading screen the header is empty and the browsers are not up, and calling
that generic would pin the wrong profile for the rest of the session. */
if(!kind)kind=(live&&hdr)?'generic':'none';
return '@srv\t'+kind+'\t'+hdr.replace(/[\t\r\n]/g,' ').slice(0,40);
}catch(e){return '@srv\tnone\tfailed '+String(e).slice(0,30);}})();)JS";

    ServerProfile::Settings g_Settings{};
    ServerProfile::Kind g_Probed = ServerProfile::Kind::Unknown;
    char g_Detail[64]{};
    int g_Attempts = 0;

    std::uint64_t g_PendingCommand = 0;
    std::uint64_t g_RuntimeGeneration = 0;
    ULONGLONG g_LastRequest = 0;
    // Which announce body the script side is currently holding, so the helper
    // is pushed once per change instead of every frame.
    ServerProfile::Announce g_PushedAnnounce =
        static_cast<ServerProfile::Announce>(-1);
    std::uint64_t g_PushedGeneration = 0;

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

    // What each server may be asked to do. One row per Kind, so adding a server
    // is one line here rather than a search for every place that tests a name.
    struct Capabilities final
    {
        const char* name;
        ServerProfile::Identity identity;
        ServerProfile::Announce announce;
        bool families;
        bool itemBridge;
        bool trunkInventory;
        bool greenzone;
        bool afkBridge;
        bool roleplayHealth;
        float maxWarpStep;
    };

    constexpr Capabilities kTable[
        static_cast<int>(ServerProfile::Kind::Count)] = {
        // Unknown: nothing server-specific until the probe answers. The health
        // scale is the exception, because an ESP that reads every player as
        // dead for the first seconds of a roleplay session is worse than a
        // reading that is briefly a hundred points off on a vanilla one.
        {"Unknown", ServerProfile::Identity::None,
         ServerProfile::Announce::None,
         false, false, false, false, false, true, 0.f},
        {"GTA5RP", ServerProfile::Identity::FiveRp,
         ServerProfile::Announce::AcCheck,
         true, true, true, true, true, true, 0.f},
        // Quant's check trips on a 650 m jump made less than two and a half
        // seconds after the last legitimate one, so warps are walked in steps
        // its own scripts would make.
        {"Quant RP", ServerProfile::Identity::Quant,
         ServerProfile::Announce::RefreshTp,
         false, false, false, false, false, true, 500.f},
        {"Ukraine GTA", ServerProfile::Identity::Ukraine,
         ServerProfile::Announce::RefreshTp,
         false, false, false, false, false, true, 500.f},
        {"Other server", ServerProfile::Identity::None,
         ServerProfile::Announce::None,
         false, false, false, false, false, false, 0.f}
    };

    const Capabilities& Row() noexcept
    {
        const int index = static_cast<int>(ServerProfile::Current());
        if (index < 0 ||
            index >= static_cast<int>(ServerProfile::Kind::Count))
            return kTable[0];
        return kTable[index];
    }

    ServerProfile::Kind ParseKind(const char* text) noexcept
    {
        if (!text)
            return ServerProfile::Kind::Unknown;
        if (!std::strncmp(text, "fiverp", 6))
            return ServerProfile::Kind::FiveRp;
        if (!std::strncmp(text, "quant", 5))
            return ServerProfile::Kind::Quant;
        if (!std::strncmp(text, "ukraine", 7))
            return ServerProfile::Kind::Ukraine;
        if (!std::strncmp(text, "generic", 7))
            return ServerProfile::Kind::Generic;
        return ServerProfile::Kind::Unknown;
    }

    void ParseResult(const char* text) noexcept
    {
        if (!text || std::strncmp(text, "@srv", 4) != 0 || text[4] != '\t')
            return;
        const char* cursor = text + 5;
        const ServerProfile::Kind kind = ParseKind(cursor);
        while (*cursor && *cursor != '\t')
            ++cursor;
        char detail[64]{};
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
        g_Probed = kind;
        lstrcpynA(g_Detail, detail, static_cast<int>(sizeof(g_Detail)));
        Unlock();
    }

    // Published as a function rather than substituted into each hook: the hooks
    // are static literals, and a server change has to reach the ones already
    // registered in the script context.
    void PushAnnounceHelper(
        ServerProfile::Announce channel,
        std::uint64_t generation) noexcept
    {
        char script[420]{};
        _snprintf_s(
            script,
            sizeof(script),
            _TRUNCATE,
            "try{var g=typeof globalThis!=='undefined'?globalThis:this;"
            "g.__bfAcPoke=function(ms){%s};"
            "g.__bfAcStep=%.1f;}catch(e){}",
            ServerProfile::AnnounceJs(),
            static_cast<double>(ServerProfile::MaxWarpStep()));
        if (RageV8Runtime::EnqueueFireAndForget(script))
        {
            g_PushedAnnounce = channel;
            g_PushedGeneration = generation;
        }
    }
}

namespace ServerProfile
{
    Settings& Access() noexcept
    {
        return g_Settings;
    }

    Kind Current() noexcept
    {
        if (g_Settings.force > 0 &&
            g_Settings.force < static_cast<int>(Kind::Count))
            return static_cast<Kind>(g_Settings.force);
        return g_Probed;
    }

    const char* NameOf(Kind kind) noexcept
    {
        const int index = static_cast<int>(kind);
        if (index < 0 || index >= static_cast<int>(Kind::Count))
            return kTable[0].name;
        return kTable[index].name;
    }

    const char* Name() noexcept
    {
        return NameOf(Current());
    }

    bool IsKnownRp() noexcept
    {
        const Kind kind = Current();
        return kind == Kind::FiveRp ||
            kind == Kind::Quant ||
            kind == Kind::Ukraine;
    }

    Identity IdentitySource() noexcept { return Row().identity; }
    Announce AnnounceChannel() noexcept { return Row().announce; }
    bool HasFamilies() noexcept { return Row().families; }
    bool HasItemBridge() noexcept { return Row().itemBridge; }
    bool HasTrunkInventory() noexcept { return Row().trunkInventory; }
    bool HasGreenzone() noexcept { return Row().greenzone; }
    bool HasAfkBridge() noexcept { return Row().afkBridge; }
    bool RoleplayHealth() noexcept { return Row().roleplayHealth; }
    float MaxWarpStep() noexcept { return Row().maxWarpStep; }

    int DownThreshold() noexcept
    {
        // GTA keeps ped health as 100 + value, and a roleplay server maps its
        // own bar onto exactly that, so an empty bar reads as a hundred. On a
        // server that does not, zero is zero.
        return RoleplayHealth() ? 100 : 0;
    }

    const char* AnnounceJs() noexcept
    {
        switch (AnnounceChannel())
        {
        case Announce::AcCheck:
            return "try{mp.events.call('c:ac:check',ms||3000);}catch(e){}";
        case Announce::RefreshTp:
            // The bound handler first, when something has captured it: calling
            // it directly refreshes the same timestamp without the event going
            // out, which is what this server's own tooling prefers.
            return "try{var g=typeof globalThis!=='undefined'?globalThis:this;"
                   "if(typeof g.__bfAcRefresh==='function'){g.__bfAcRefresh();}"
                   "else mp.events.call('ac:refreshtp');}catch(e){}";
        default:
            return "";
        }
    }

    void Reset() noexcept
    {
        g_PendingCommand = 0;
        g_LastRequest = 0;
        g_Attempts = 0;
        g_PushedAnnounce = static_cast<Announce>(-1);
        g_PushedGeneration = 0;
        Lock();
        g_Probed = Kind::Unknown;
        g_Detail[0] = 0;
        Unlock();
    }

    Status GetStatus() noexcept
    {
        Status status{};
        Lock();
        status.kind = Current();
        status.probed = g_Probed != Kind::Unknown;
        status.attempts = g_Attempts;
        lstrcpynA(
            status.detail,
            g_Detail,
            static_cast<int>(sizeof(status.detail)));
        Unlock();
        status.forced = g_Settings.force > 0;
        return status;
    }

    void Update() noexcept
    {
        const auto runtime = RageV8Runtime::GetStatus();
        const ULONGLONG now = GetTickCount64();

        // A new generation is a new script context, which means a reconnect and
        // possibly a different server. The answer and the helper both go with
        // it.
        if (runtime.generation != g_RuntimeGeneration)
        {
            g_RuntimeGeneration = runtime.generation;
            Reset();
        }

        if (g_PendingCommand)
        {
            if (RageV8Runtime::HasResultFor(g_PendingCommand))
            {
                RageV8Runtime::Result result{};
                if (RageV8Runtime::PollResultFor(g_PendingCommand, result) &&
                    result.success)
                    ParseResult(result.text);
                g_PendingCommand = 0;
            }
            else if (now - g_LastRequest >= 4000 ||
                     runtime.state != RageV8Runtime::State::Ready)
                g_PendingCommand = 0;
        }

        if (runtime.state != RageV8Runtime::State::Ready ||
            !runtime.rootCaptured)
            return;

        // The helper the movement hooks call. Pushed on a change of channel and
        // after every reconnect, because the context that held it is gone.
        if (g_PushedAnnounce != AnnounceChannel() ||
            g_PushedGeneration != runtime.generation)
            PushAnnounceHelper(AnnounceChannel(), runtime.generation);

        // Once the server is known there is nothing to learn, but the check is
        // cheap and a session can change servers without changing context.
        const ULONGLONG gap =
            g_Probed == Kind::Unknown ? 1000ull : 15000ull;
        if (g_PendingCommand ||
            runtime.queuedCommands > 3 ||
            now - g_LastRequest < gap)
            return;

        g_LastRequest = now;
        ++g_Attempts;
        if (!RageV8Runtime::Enqueue(kProbe, &g_PendingCommand))
            g_PendingCommand = 0;
    }

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept
    {
        if (!output)
            return false;
        const int written = _snprintf_s(
            output + used,
            capacity - used,
            _TRUNCATE,
            "srv_force=%d\n",
            g_Settings.force);
        if (written <= 0)
            return false;
        used += static_cast<std::size_t>(written);
        return true;
    }

    void Import(const char* content) noexcept
    {
        if (!content)
            return;
        const char* line = content;
        const char* value = nullptr;
        while (*line)
        {
            if (!std::strncmp(line, "srv_force", 9) && line[9] == '=')
            {
                value = line + 10;
                break;
            }
            const char* next = std::strchr(line, '\n');
            if (!next)
                break;
            line = next + 1;
        }
        if (!value)
            return;
        const int force = std::atoi(value);
        if (force >= 0 && force < static_cast<int>(Kind::Count))
            g_Settings.force = force;
    }
}
