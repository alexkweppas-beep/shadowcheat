#include "FiveRpAdminScan.h"

#include "RageV8Runtime.h"
#include "../vmp_markers.hpp"

#include <Windows.h>
#include <cstdlib>
#include <cstring>

namespace
{
    // Walks the whole player pool, not forEachInStreamRange, so off-stream
    // admins are included. The admin variable name is never hardcoded: the
    // server publishes it in mp.serverDataKeys.isAdmin and rotates it.
    constexpr char kScanScript[] =
R"FIVEADMIN(return (()=>{try{
const root=(typeof globalThis!=='undefined'&&globalThis)||(typeof global!=='undefined'&&global)||null;
const ctx=root&&root.__ctx__;
let bareMp=null,bareApi=null;
try{bareMp=(typeof mp!=='undefined'&&mp)||null;}catch(_){}
try{bareApi=(typeof api!=='undefined'&&api)||null;}catch(_){}
const rage=(ctx&&ctx.mp)||(root&&root.mp)||bareMp;
const apiRoot=(root&&root.__butterflyFiveRpApi)||(ctx&&ctx.api)||(root&&root.api)||bareApi;
const helpers=(root&&root.__butterflyFiveRpPlayers)||(apiRoot&&apiRoot.players);
const empty='@adm\t\t0\t0\t0\t0';
if(!rage||!rage.players||typeof rage.players.forEach!=='function')return empty;
/* Several mp instances coexist after a reconnect and the one our scripts run
in carries an empty serverDataKeys, so the native probe pins the live table on
the root as __butterflyFiveRpDataKeys. */
let key='';
try{const dk=(root&&root.__butterflyFiveRpDataKeys)||rage.serverDataKeys;
if(dk&&dk.isAdmin)key=String(dk.isAdmin).trim();}catch(_){}
if(!key)return empty;
let hash=null,charHash=null;
try{const m=rage.game&&rage.game.misc;
if(m&&typeof m.getVariableHashKey==='function'){
hash=m.getVariableHashKey(key);charHash=m.getVariableHashKey('characterId');}}catch(_){}
const safe=v=>String(v==null?'':v).replace(/[\t\r\n]/g,' ').trim();
const valid=v=>!!v&&Number.isFinite(Number(v.x))&&Number.isFinite(Number(v.y))&&Number.isFinite(Number(v.z))&&(Number(v.x)!==0||Number(v.y)!==0||Number(v.z)!==0);
/* errs and undef are reported back so a pass that finds nobody can be told
apart from a pass where the variable was simply unreadable. */
const rows=[];let scanned=0,errs=0,undef=0;
rage.players.forEach(p=>{
if(!p||scanned>=8192||rows.length>=128)return;
++scanned;
let raw;
try{raw=hash!=null?p.getVariable(hash):undefined;
if(raw===undefined||raw===null)raw=p.getVariable(key);}catch(_){++errs;return;}
if(raw===undefined||raw===null){++undef;return;}
if(!raw)return;
const id=Number(p.remoteId);
if(!Number.isFinite(id)||id<0||id>65535)return;
let cid=0;
try{cid=Number(charHash!=null?p.getVariable(charHash):0)||Number(p.getVariable('characterId'))||0;}catch(_){}
if(!Number.isFinite(cid)||cid<0)cid=0;
let name='';
try{if(helpers&&typeof helpers.getName==='function')name=safe(helpers.getName.call(helpers,p));}catch(_){}
if(!name){try{name=safe(p.name).replace(/_/g,' ');}catch(_){}}
let fac='';
try{if(helpers&&typeof helpers.getServerFactionId==='function')fac=safe(helpers.getServerFactionId.call(helpers,p));}catch(_){}
let streamed=0;
try{streamed=valid(p.position)?1:0;}catch(_){}
rows.push([id,cid,streamed,name.slice(0,56),fac.slice(0,24)].join('\t'));
});
return ['@adm',safe(key),scanned,rows.length,errs,undef].join('\t')+(rows.length?'\n'+rows.join('\n'):'');
}catch(_){return '@adm\t\t0\t0\t0\t0';}})())FIVEADMIN";

    // A single pass can come back empty while admins are still online, for
    // instance when getVariable throws mid-stream-change. Entries therefore
    // age out instead of being replaced wholesale, so one bad pass no longer
    // blanks the list.
    constexpr ULONGLONG kEntryTtlMs = 15000;

    volatile LONG g_Lock = 0;
    FiveRpAdminScan::Entry g_Entries[FiveRpAdminScan::kCapacity]{};
    ULONGLONG g_SeenAt[FiveRpAdminScan::kCapacity]{};
    int g_Count = 0;
    int g_Scanned = 0;
    int g_Errors = 0;
    int g_NoData = 0;
    ULONGLONG g_LastScanAt = 0;
    char g_KeyName[48]{};

    std::uint64_t g_PendingCommand = 0;
    std::uint64_t g_RuntimeGeneration = 0;
    ULONGLONG g_LastRequest = 0;

    void Lock() noexcept
    {
        while (InterlockedCompareExchange(&g_Lock, 1, 0) != 0)
            Sleep(0);
    }

    void Unlock() noexcept
    {
        InterlockedExchange(&g_Lock, 0);
    }

    const char* FieldEnd(const char* cursor) noexcept
    {
        while (*cursor && *cursor != '\t' && *cursor != '\n' &&
               *cursor != '\r')
            ++cursor;
        return cursor;
    }

    void CopyField(
        char* output,
        int capacity,
        const char* begin,
        const char* end) noexcept
    {
        int length = static_cast<int>(end - begin);
        if (length > capacity - 1)
            length = capacity - 1;
        if (length < 0)
            length = 0;
        for (int index = 0; index < length; ++index)
            output[index] = begin[index];
        output[length] = 0;
    }

    int ParseInt(const char* begin, const char* end) noexcept
    {
        char buffer[16]{};
        CopyField(buffer, static_cast<int>(sizeof(buffer)), begin, end);
        return std::atoi(buffer);
    }

    void ParseSnapshot(const char* text) noexcept
    {
        if (!text || !text[0])
            return;
        const char* cursor = text;
        FiveRpAdminScan::Entry parsed[FiveRpAdminScan::kCapacity]{};
        int count = 0;
        int scanned = 0;
        int errors = 0;
        int noData = 0;
        char keyName[48]{};
        bool haveHeader = false;

        while (*cursor)
        {
            const char* lineEnd = cursor;
            while (*lineEnd && *lineEnd != '\n')
                ++lineEnd;

            const char* fieldBegin[6]{};
            const char* fieldEnd[6]{};
            const char* field = cursor;
            int fields = 0;
            while (fields < 6 && field <= lineEnd)
            {
                const char* stop = FieldEnd(field);
                if (stop > lineEnd)
                    stop = lineEnd;
                fieldBegin[fields] = field;
                fieldEnd[fields] = stop;
                ++fields;
                if (stop >= lineEnd || *stop != '\t')
                    break;
                field = stop + 1;
            }

            if (!haveHeader)
            {
                if (fields >= 4 &&
                    fieldEnd[0] - fieldBegin[0] == 4 &&
                    strncmp(fieldBegin[0], "@adm", 4) == 0)
                {
                    CopyField(
                        keyName,
                        static_cast<int>(sizeof(keyName)),
                        fieldBegin[1],
                        fieldEnd[1]);
                    scanned = ParseInt(fieldBegin[2], fieldEnd[2]);
                    if (fields >= 5)
                        errors = ParseInt(fieldBegin[4], fieldEnd[4]);
                    if (fields >= 6)
                        noData = ParseInt(fieldBegin[5], fieldEnd[5]);
                    haveHeader = true;
                }
                else
                {
                    return;
                }
            }
            else if (fields >= 5 &&
                     count < FiveRpAdminScan::kCapacity)
            {
                auto& entry = parsed[count];
                const int remoteId =
                    ParseInt(fieldBegin[0], fieldEnd[0]);
                if (remoteId >= 0 && remoteId <= 65535)
                {
                    entry.remoteId =
                        static_cast<std::uint16_t>(remoteId);
                    entry.characterId =
                        ParseInt(fieldBegin[1], fieldEnd[1]);
                    entry.streamed =
                        ParseInt(fieldBegin[2], fieldEnd[2]) != 0;
                    CopyField(
                        entry.name,
                        static_cast<int>(sizeof(entry.name)),
                        fieldBegin[3],
                        fieldEnd[3]);
                    CopyField(
                        entry.factionTag,
                        static_cast<int>(sizeof(entry.factionTag)),
                        fieldBegin[4],
                        fieldEnd[4]);
                    ++count;
                }
            }

            cursor = *lineEnd ? lineEnd + 1 : lineEnd;
        }

        if (!haveHeader)
            return;

        const ULONGLONG now = GetTickCount64();
        Lock();
        // Refresh what this pass saw, keyed on remoteId.
        for (int index = 0; index < count; ++index)
        {
            int slot = -1;
            for (int probe = 0; probe < g_Count; ++probe)
            {
                if (g_Entries[probe].remoteId ==
                    parsed[index].remoteId)
                {
                    slot = probe;
                    break;
                }
            }
            if (slot < 0 && g_Count < FiveRpAdminScan::kCapacity)
                slot = g_Count++;
            if (slot < 0)
                continue;
            g_Entries[slot] = parsed[index];
            g_SeenAt[slot] = now;
        }
        // Drop only what has gone unconfirmed for a while, so a single empty
        // pass cannot wipe the list.
        int write = 0;
        for (int index = 0; index < g_Count; ++index)
        {
            if (g_SeenAt[index] &&
                now - g_SeenAt[index] > kEntryTtlMs)
                continue;
            if (write != index)
            {
                g_Entries[write] = g_Entries[index];
                g_SeenAt[write] = g_SeenAt[index];
            }
            ++write;
        }
        for (int index = write;
             index < FiveRpAdminScan::kCapacity;
             ++index)
        {
            g_Entries[index] = {};
            g_SeenAt[index] = 0;
        }
        g_Count = write;
        g_Scanned = scanned;
        g_Errors = errors;
        g_NoData = noData;
        g_LastScanAt = now;
        lstrcpynA(
            g_KeyName,
            keyName,
            static_cast<int>(sizeof(g_KeyName)));
        Unlock();
    }
}

namespace FiveRpAdminScan
{
    void Reset() noexcept
    {
        g_PendingCommand = 0;
        g_LastRequest = 0;
        Lock();
        g_Count = 0;
        g_Scanned = 0;
        g_Errors = 0;
        g_NoData = 0;
        g_LastScanAt = 0;
        g_KeyName[0] = 0;
        for (int index = 0; index < kCapacity; ++index)
        {
            g_Entries[index] = {};
            g_SeenAt[index] = 0;
        }
        Unlock();
    }

    void Update(bool active) noexcept
    {
        const auto status = RageV8Runtime::GetStatus();
        const ULONGLONG now = GetTickCount64();
        if (status.generation != g_RuntimeGeneration)
        {
            g_RuntimeGeneration = status.generation;
            g_PendingCommand = 0;
            g_LastRequest = 0;
            Lock();
            g_Count = 0;
            g_Scanned = 0;
            g_Errors = 0;
            g_NoData = 0;
            g_KeyName[0] = 0;
            for (int index = 0; index < kCapacity; ++index)
            {
                g_Entries[index] = {};
                g_SeenAt[index] = 0;
            }
            Unlock();
        }

        if (g_PendingCommand)
        {
            // Probe before building the 32 KB Result on the stack.
            if (RageV8Runtime::HasResultFor(g_PendingCommand))
            {
                RageV8Runtime::Result result{};
                if (RageV8Runtime::PollResultFor(
                        g_PendingCommand,
                        result) &&
                    result.success)
                    ParseSnapshot(result.text);
                g_PendingCommand = 0;
            }
            else if (now - g_LastRequest >= 6000 ||
                     status.state != RageV8Runtime::State::Ready)
            {
                g_PendingCommand = 0;
            }
        }

        // Sweeping ~1900 players is far too heavy for a per-frame job, so it
        // only runs while the admin list is on.
        if (!active)
        {
            if (g_Count)
            {
                Lock();
                g_Count = 0;
                for (int index = 0; index < kCapacity; ++index)
                {
                    g_Entries[index] = {};
                    g_SeenAt[index] = 0;
                }
                Unlock();
            }
            return;
        }

        // The pool sweep is the heaviest script we submit, so it waits for an
        // empty queue instead of landing right after the player snapshot.
        if (g_PendingCommand ||
            status.queuedCommands != 0 ||
            status.state != RageV8Runtime::State::Ready ||
            !status.rootCaptured ||
            now - g_LastRequest < 3000)
            return;

        g_LastRequest = now;
        SHADOW_PROTECT_MUT("FiveRP.AdminScan");
        const char* scanScript =
            SHADOW_DECRYPT_STRING_A(kScanScript);
        const bool queued =
            scanScript &&
            RageV8Runtime::Enqueue(
                scanScript,
                &g_PendingCommand);
        SHADOW_FREE_STRING(scanScript);
        SHADOW_PROTECT_END();
        if (!queued)
            g_PendingCommand = 0;
    }

    int CopyList(Entry* output, int capacity) noexcept
    {
        if (!output || capacity <= 0)
            return 0;
        Lock();
        int count = g_Count;
        if (count > capacity)
            count = capacity;
        for (int index = 0; index < count; ++index)
            output[index] = g_Entries[index];
        Unlock();
        return count;
    }

    int Count() noexcept
    {
        Lock();
        const int count = g_Count;
        Unlock();
        return count;
    }

    Status GetStatus() noexcept
    {
        Status status{};
        Lock();
        status.found = g_Count;
        status.scanned = g_Scanned;
        status.errors = g_Errors;
        status.noData = g_NoData;
        status.lastScanAt = g_LastScanAt;
        lstrcpynA(
            status.keyName,
            g_KeyName,
            static_cast<int>(sizeof(status.keyName)));
        Unlock();
        return status;
    }
}
