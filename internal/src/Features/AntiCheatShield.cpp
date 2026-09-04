#include "AntiCheatShield.h"

#include "RageV8Runtime.h"
#include "ServerProfile.h"

#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hooks
{
    void HLog(const char* message);
}

namespace
{
    // The wrapper is installed on every runtime host that exposes an outbound
    // call, and it is idempotent: a marker on the function keeps a second pass
    // from wrapping the wrapper. Reports are recorded either way, so the names
    // become visible even with blocking off.
    constexpr char kInstallScript[] =
R"ACSHIELD(return (()=>{try{
const root=(typeof globalThis!=='undefined'&&globalThis)||(typeof global!=='undefined'&&global)||null;
if(!root)return 'ERR\tno root';
const ctx=root.__ctx__;
let bareMp=null;
try{bareMp=(typeof mp!=='undefined'&&mp)||null;}catch(_){}
const hosts=[];
const push=o=>{if(o&&hosts.indexOf(o)===-1)hosts.push(o);};
push((ctx&&ctx.mp&&ctx.mp.events)||null);
push((root.mp&&root.mp.events)||null);
push((bareMp&&bareMp.events)||null);
const state=root.__acShield||(root.__acShield={
blocked:0,observed:0,hooks:0,names:[],block:true});
state.block=BLOCK_FLAG;
const isReport=name=>{try{return String(name).indexOf('s_ac_')===0;}catch(_){return false;}};
const note=name=>{try{
const text=String(name);
++state.observed;
if(state.names.indexOf(text)===-1){
state.names.push(text);
if(state.names.length>16)state.names.shift();}
}catch(_){}};
const wrap=(host,key)=>{try{
if(!host||typeof host[key]!=='function')return false;
const original=host[key];
if(original.__acShield)return true;
const wrapped=function(){
try{
if(arguments.length>0&&isReport(arguments[0])){
note(arguments[0]);
if(state.block){
++state.blocked;
/* Returning undefined matches what the runtime hands back for a
one-way call; the reporter ignores the result. */
return undefined;}}
}catch(_){}
return original.apply(this,arguments);};
try{Object.defineProperty(wrapped,'__acShield',{value:true});}
catch(_){wrapped.__acShield=true;}
host[key]=wrapped;
return true;}catch(_){return false;}};
let hooked=0;
hosts.forEach(host=>{
['callRemote','callRemoteUnreliable','callRemoteProc','callRemoteAsync',
'_callRemoteProc','trigger','triggerServer','callServer'].forEach(key=>{
if(wrap(host,key))++hooked;});});
state.hooks=hooked;
return 'OK\t'+hooked+'\t'+state.blocked+'\t'+state.observed+'\t'+
state.names.join('|');
}catch(e){return 'ERR\t'+String(e&&e.message?e.message:e);}})();
)ACSHIELD";

    constexpr char kStatusScript[] =
R"ACSTATUS(return (()=>{try{
const root=(typeof globalThis!=='undefined'&&globalThis)||(typeof global!=='undefined'&&global)||null;
const state=root&&root.__acShield;
if(!state)return 'OFF\t0\t0\t0\t';
return 'OK\t'+state.hooks+'\t'+state.blocked+'\t'+state.observed+'\t'+
state.names.join('|');
}catch(e){return 'ERR\t'+String(e&&e.message?e.message:e);}})();
)ACSTATUS";

    bool g_Enabled = false;
    bool g_Initialized = false;
    AntiCheatShield::Status g_Status{};
    ULONGLONG g_NextInstall = 0;
    ULONGLONG g_NextStatus = 0;
    std::uint64_t g_PendingInstall = 0;
    std::uint64_t g_PendingStatus = 0;

    void ParseStatus(const char* text) noexcept
    {
        if (!text || !text[0])
            return;
        char buffer[RageV8Runtime::kMaxResultLength]{};
        lstrcpynA(buffer, text, static_cast<int>(sizeof(buffer)));

        char* fields[5]{};
        int count = 0;
        char* cursor = buffer;
        fields[count++] = cursor;
        while (*cursor && count < 5)
        {
            if (*cursor == '\t')
            {
                *cursor = 0;
                fields[count++] = cursor + 1;
            }
            ++cursor;
        }
        if (count < 4)
            return;
        if (lstrcmpA(fields[0], "OK") != 0)
        {
            g_Status.installed = false;
            lstrcpynA(
                g_Status.message,
                count > 1 ? fields[1] : fields[0],
                static_cast<int>(sizeof(g_Status.message)));
            return;
        }

        g_Status.installed = true;
        g_Status.hooks = static_cast<int>(strtol(fields[1], nullptr, 10));
        g_Status.blocked = static_cast<std::uint32_t>(
            strtoul(fields[2], nullptr, 10));
        g_Status.observed = static_cast<std::uint32_t>(
            strtoul(fields[3], nullptr, 10));
        g_Status.message[0] = 0;

        g_Status.nameCount = 0;
        if (count < 5 || !fields[4])
            return;
        // The reporter builds each name at runtime, so this list is the only
        // place the complete names show up.
        char* name = fields[4];
        while (*name && g_Status.nameCount < AntiCheatShield::kObservedNameCapacity)
        {
            char* separator = name;
            while (*separator && *separator != '|')
                ++separator;
            const bool last = (*separator == 0);
            *separator = 0;
            if (name[0])
            {
                lstrcpynA(
                    g_Status.names[g_Status.nameCount],
                    name,
                    AntiCheatShield::kNameLength);
                ++g_Status.nameCount;
            }
            if (last)
                break;
            name = separator + 1;
        }
    }

    void EnqueueInstall() noexcept
    {
        char script[sizeof(kInstallScript) + 16]{};
        const char* marker = strstr(kInstallScript, "BLOCK_FLAG");
        if (!marker)
            return;
        const std::size_t prefix =
            static_cast<std::size_t>(marker - kInstallScript);
        memcpy(script, kInstallScript, prefix);
        const char* value = g_Enabled ? "true" : "false";
        lstrcpynA(
            script + prefix,
            value,
            static_cast<int>(sizeof(script) - prefix));
        lstrcatA(script, marker + strlen("BLOCK_FLAG"));
        RageV8Runtime::Enqueue(script, &g_PendingInstall);
    }
}

namespace AntiCheatShield
{
bool& Enabled() noexcept
{
    return g_Enabled;
}

void Initialize() noexcept
{
    g_Initialized = true;
    g_Status = Status{};
    g_NextInstall = 0;
    g_NextStatus = 0;
    g_PendingInstall = 0;
    g_PendingStatus = 0;
}

void Update() noexcept
{
    if (!g_Initialized)
        return;

    const ULONGLONG now = GetTickCount64();

    if (g_PendingInstall)
    {
        RageV8Runtime::Result result{};
        if (RageV8Runtime::PollResultFor(g_PendingInstall, result))
        {
            g_PendingInstall = 0;
            ParseStatus(result.text);
            if (!g_Status.installed && g_Status.message[0])
            {
                char line[160]{};
                sprintf_s(
                    line,
                    "[AntiCheatShield] install failed: %.100s",
                    g_Status.message);
                hooks::HLog(line);
            }
        }
    }
    if (g_PendingStatus)
    {
        RageV8Runtime::Result result{};
        if (RageV8Runtime::PollResultFor(g_PendingStatus, result))
        {
            g_PendingStatus = 0;
            ParseStatus(result.text);
        }
    }

    // Reinstalled on a cadence rather than once: a reconnect brings up a new
    // runtime whose call function is unwrapped again.
    if (now >= g_NextInstall && !g_PendingInstall)
    {
        g_NextInstall = now + 5000;
        EnqueueInstall();
        return;
    }
    if (now >= g_NextStatus && !g_PendingStatus && !g_PendingInstall)
    {
        g_NextStatus = now + 2000;
        RageV8Runtime::Enqueue(kStatusScript, &g_PendingStatus);
    }
}

Status GetStatus() noexcept
{
    return g_Status;
}

void DeclarePositionJump(int milliseconds) noexcept
{
    // Nothing to announce to a server that has no such channel, and a made-up
    // event name is one more thing for it to notice.
    if (ServerProfile::AnnounceChannel() ==
        ServerProfile::Announce::None)
        return;
    if (milliseconds < 500)
        milliseconds = 500;
    if (milliseconds > 15000)
        milliseconds = 15000;
    // Routed through the helper ServerProfile publishes, which holds whichever
    // call this server's own spawn scripts make before they move the player.
    char script[224]{};
    sprintf_s(
        script,
        "try{const g=(typeof globalThis!=='undefined'&&globalThis)||this;"
        "if(typeof g.__bfAcPoke==='function')g.__bfAcPoke(%d);}catch(_){}",
        milliseconds);
    RageV8Runtime::EnqueueFireAndForget(script);
}
}
