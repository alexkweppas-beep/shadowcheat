#include "FiveRpMetadataResolver.h"

#include "RageV8Runtime.h"

#include <Windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace hooks
{
    void HLog(const char* message);
}

namespace
{
    constexpr int kCapacity = 256;
    constexpr std::uint32_t kNegativeConfirmations = 5;
    constexpr ULONGLONG kNegativeGraceMs = 10000;
    constexpr ULONGLONG kNameGraceMs = 5000;
    constexpr char kSafeSnapshotScript[] =
R"FIVEIDENTITY(return (()=>{try{
const root=(typeof globalThis!=='undefined'&&globalThis)||(typeof global!=='undefined'&&global)||null;
const context=root&&root.__ctx__;
let bareMp=null,bareApi=null;
try{bareMp=(typeof mp!=='undefined'&&mp)||null;}catch(_){}
try{bareApi=(typeof api!=='undefined'&&api)||null;}catch(_){}
const runtimeMp=(context&&context.mp)||(root&&root.mp)||bareMp;
const apiRoot=(root&&root.__butterflyFiveRpApi)||(context&&context.api)||(root&&root.api)||bareApi;
const helpers=(root&&root.__butterflyFiveRpPlayers)||(apiRoot&&apiRoot.players);
const players=runtimeMp&&runtimeMp.players;
const safe=v=>String(v==null?'':v).replace(/[\t\r\n]/g,' ').trim();
const num=v=>{const n=Number(v);return Number.isFinite(n)?n:0;};
const validHelpers=!!helpers&&typeof helpers.getName==='function';
const variable=(player,key)=>{try{
if(!player)return null;
const direct=player[key];
if(direct!==undefined&&direct!==null)return direct;
if(typeof player.getVariable!=='function')return null;
let value=player.getVariable(key);
if(value!==undefined&&value!==null)return value;
try{
const misc=runtimeMp&&runtimeMp.game&&runtimeMp.game.misc;
if(misc&&typeof misc.getVariableHashKey==='function'){
value=player.getVariable(misc.getVariableHashKey(key));
if(value!==undefined&&value!==null)return value;
}}catch(_){}
return null;
}catch(_){return null;}};
const hiddenName=v=>{const s=String(v==null?'':v).trim();
if(!s||s==='undefined'||s==='null')return true;
if(/^(?:Гражданин|Гражданка|Player|Citizen|Unknown|Незнакомец)(?:\s|#|\(|$)/i.test(s))return true;
/* Donate and achievement titles arrive on the same charName channel. Every
character name in the live 5RP roster is Latin, so a lone Cyrillic word is a
title, never a name. */
if(/^\S+$/.test(s)&&/[\u0400-\u04FF]/.test(s))return true;
return false;};
const label=(value,id)=>{let text=safe(value);if(!text)return '';
if(id!=null&&id>=0){const suffix=String(id);
if(text.endsWith('('+suffix+')'))text=text.slice(0,-suffix.length-2).trim();
else if(text.endsWith('['+suffix+']'))text=text.slice(0,-suffix.length-2).trim();
else if(text.endsWith('_'+suffix))text=text.slice(0,-suffix.length-1).trim();
else if(text.endsWith('#'+suffix))text=text.slice(0,-suffix.length-1).trim();
else if(text.endsWith(' '+suffix))text=text.slice(0,-suffix.length-1).trim();}
text=text.replace(/_/g,' ').replace(/\s+/g,' ').trim();
if(!text||/^(?:#|ID\s*:\s*)?\d+(?:\s*\(\d+\))?$/i.test(text)||hiddenName(text))return '';
return text;};
/* The admin variable name is published by the server in
mp.serverDataKeys.isAdmin (currently "152") and rotates, so it must never be
hardcoded. variable() already tries both the raw name and its hash. */
let adminVar='';
try{const dk=(root&&root.__butterflyFiveRpDataKeys)||(runtimeMp&&runtimeMp.serverDataKeys);
if(dk&&dk.isAdmin)adminVar=safe(dk.isAdmin);}catch(_){}
const local=players&&players.local;
const localHandle=num(local&&local.handle);
const localCharacter=num(variable(local,'characterId')||variable(local,'sqlId'));
const apiReady=!!players&&typeof players.forEach==='function';
let state=root&&root.__butterflyIdentityCollector;
if(!state||typeof state!=='object'){
state={gateHandle:0,gateCharacter:0,gatePolls:0,passes:0,names:{},chars:{},pending:{},negative:{},lastRequest:0,failures:0,familyCanary:false,familyRpcDisabled:false};
try{Object.defineProperty(root,'__butterflyIdentityCollector',{value:state,writable:true,configurable:true});}
catch(_){if(root)root.__butterflyIdentityCollector=state;}
}
if(!state.chars)state.chars={};
const header=(ready)=>['@5rp',ready?1:0,Object.keys(state.names||{}).length,Object.keys(state.pending||{}).length,validHelpers?1:0,apiRoot?1:0,state.familyCanary?1:0,state.familyRpcDisabled?1:0].join('\t');
if(apiReady&&localHandle>0)state.gatePolls++;
else{
if(state.gateHandle!==localHandle){
state.names={};state.pending={};state.negative={};
state.failures=0;state.familyCanary=false;state.familyRpcDisabled=false;
state.passes=0;state.lastRequest=0;
}
state.gateHandle=localHandle;
state.gateCharacter=localCharacter;
state.gatePolls=apiReady&&localHandle>0?1:0;
}
if(!apiReady)
return header(0);
state.passes++;
/* Keys and types below are the live server set read out of the api.players
roster map: 14 populated keys plus Marabunta and Vagos, which exist but
currently have no members. */
const isFactionKey=k=>/^F_[A-Z0-9_]+$/.test(k);
const factionLabel=(keyValue,typeValue)=>{const key=safe(keyValue);
const named={F_LSPD:'LSPD',F_LSSD:'LSSD',F_FIB:'FIB',F_LSARMY:'Army',F_LSCITYHALL:'Gov',F_EMS:'EMS',F_NEWS:'News',F_PRISON:'Prison',F_GANG_BALLAS:'Ballas',F_GANG_BLOODS:'Bloods',F_GANG_GROVE:'Grove',F_GANG_MARABUNTA:'Marabunta',F_GANG_VAGOS:'Vagos',F_RUSSIANMAFIA:'RM',F_ARMENIAMAFIA:'Armenian',F_MEXICOMAFIA:'Mexican'};
if(named[key])return named[key];
const numeric={1:'Gov',2:'LSPD',3:'BCSD',4:'FIB',5:'EMS',6:'Army',7:'News'};
if(/^\d+$/.test(key)&&numeric[Number(key)])return numeric[Number(key)];
if(isFactionKey(key)){const derived=key.replace(/^F_(?:GANG_|MAFIA_)?/,'').toLowerCase();
if(derived)return derived.charAt(0).toUpperCase()+derived.slice(1);}
const byType={1:'Police',2:'Army',3:'Gang',4:'Mafia',5:'News',6:'Gov',7:'EMS',8:'FIB',9:'Prison'};
return byType[Number(typeValue)]||'';};
const familyIds=[];
const rows=[];
players.forEach(player=>{if(!player)return;
const id=num(player.remoteId),handle=num(player.handle);
if(id<0||id>65535||handle<=0)return;
const characterId=num(variable(player,'characterId')||variable(player,'sqlId'));
let name='';
let factionKey='';
let factionType=0;
try{if(helpers&&typeof helpers.getName==='function')name=label(helpers.getName.call(helpers,player),id);}catch(_){}
if(!name){try{if(helpers&&typeof helpers.getRpPlayerName==='function')name=label(helpers.getRpPlayerName.call(helpers,player),id);}catch(_){}}
if(!name)name=label(variable(player,'charName'),id)||
label(variable(player,'__displayCharName'),id)||
label(variable(player,'characterName'),id)||
label(variable(player,'realName'),id)||
label(variable(player,'Nickname'),id)||
label(variable(player,'nickname'),id)||
label(variable(player,'__displayName'),id)||
label(player.name,id);
if(name&&characterId>0)state.chars[String(characterId)]=name;
if(!name&&characterId>0&&state.chars[String(characterId)])name=state.chars[String(characterId)];
if(!name&&player.name&&!hiddenName(player.name))name=label(player.name,id);
try{if(helpers&&typeof helpers.getServerFactionId==='function')factionKey=safe(helpers.getServerFactionId.call(helpers,player));}catch(_){}
/* groupId and clubs are deliberately not consulted: they carry biker clubs
such as REDNECKS, which are not factions. */
if(!factionKey)factionKey=safe(variable(player,'serverFactionId')||variable(player,'factionKey'));
if(!isFactionKey(factionKey)&&!/^\d+$/.test(factionKey))factionKey='';
try{if(helpers&&typeof helpers.getServerFactionType==='function')factionType=num(helpers.getServerFactionType.call(helpers,player));}catch(_){}
const familyId=num(variable(player,'familyId')||variable(player,'famId'));
if(familyId>0&&familyIds.indexOf(familyId)===-1)familyIds.push(familyId);
const factionId=/^\d+$/.test(factionKey)?num(factionKey):0;
const rawDisplay=safe(variable(player,'__displayName'));
const masked=!name&&/^(?:Гражданин|Гражданка|Citizen|Unknown|Незнакомец)(?:\s|$)/i.test(rawDisplay);
const admin=adminVar?!!variable(player,adminVar):!!variable(player,'__displayIsAdmin');
const disabled=!!variable(player,'__disableNametag');
const directFamily=label(variable(player,'familyName')||variable(player,'__familyName')||variable(player,'familyTitle')||variable(player,'yachtFamilyName'),familyId);
const familyName=directFamily||safe(state.names[String(familyId)]||'');
rows.push([id,handle,characterId,familyId,factionId,factionType,masked?1:0,admin?1:0,disabled?1:0,safe(name),safe(familyName),safe(factionKey),safe(factionLabel(factionKey,factionType))].join('\t'));
});
const now=Date.now();
if(state.passes>=2&&!state.familyRpcDisabled&&now-state.lastRequest>=1000&&Object.keys(state.pending).length===0){
const server=apiRoot&&apiRoot.server;
const rpc=server&&server.rpcCall;
if(typeof rpc==='function'){
let familyId=0;
for(let index=0;index<familyIds.length;index++){
const candidate=familyIds[index];
const key=String(candidate);
if(!state.names[key]&&!state.pending[key]&&(!state.negative[key]||now-state.negative[key]>=30000)){familyId=candidate;break;}
}
if(familyId>0){
const key=String(familyId);
state.pending[key]=now;
state.lastRequest=now;
try{
Promise.resolve(rpc.call(server,'s:familyPointsWar:getFamilyName',familyId)).then(value=>{
const familyName=label(value,familyId);
if(familyName){state.names[key]=familyName;state.familyCanary=true;state.failures=0;}
else state.negative[key]=Date.now();
delete state.pending[key];
},()=>{state.negative[key]=Date.now();state.failures++;if(!state.familyCanary&&state.failures>=3)state.familyRpcDisabled=true;delete state.pending[key];});
}catch(_){
delete state.pending[key];
state.negative[key]=now;
state.failures++;
if(!state.familyCanary&&state.failures>=3)state.familyRpcDisabled=true;
}
}
}
}
return header(1)+(rows.length?'\n'+rows.join('\n'):'');
}catch(_){return '@5rp\t0\t0\t0\t0\t0\t0\t0';}})())FIVEIDENTITY";


    FiveRpMetadataResolver::Metadata g_Entries[kCapacity]{};
    ULONGLONG g_NameGoodAt[kCapacity]{};
    int g_Count = 0;
    bool g_Detected = false;
    FiveRpMetadataResolver::State g_State =
        FiveRpMetadataResolver::State::Unknown;
    std::uint32_t g_PositiveStreak = 0;
    std::uint32_t g_NegativeStreak = 0;
    ULONGLONG g_LastSnapshotAt = 0;
    ULONGLONG g_LastPositiveAt = 0;
    ULONGLONG g_LastNegativeAt = 0;
        int g_FamilyCacheCount = 0;
        int g_FamilyPendingCount = 0;
        bool g_HelperVisible = false;
        bool g_ApiVisible = false;
        bool g_FamilyCanary = false;
        bool g_FamilyRpcDisabled = false;
    volatile LONG64 g_EnqueueFailures = 0;
    char g_Message[128] = "Waiting for 5RP identity";
    volatile LONG g_Lock = 0;
    std::uint64_t g_PendingCommand = 0;
    std::uint64_t g_RuntimeGeneration = 0;
    ULONGLONG g_LastRequest = 0;
    ULONGLONG g_ReadyNotBefore = 0;

    void Lock() noexcept
    {
        while (InterlockedCompareExchange(
                   &g_Lock, 1, 0) != 0)
            YieldProcessor();
    }

    void Unlock() noexcept
    {
        InterlockedExchange(&g_Lock, 0);
    }

    bool ParseInteger(
        const char* begin,
        const char* end,
        int& output) noexcept
    {
        output = 0;
        if (!begin || !end || begin >= end)
            return false;
        char value[32]{};
        const auto length = static_cast<int>(
            end - begin);
        if (length <= 0 ||
            length >= static_cast<int>(
                sizeof(value)))
            return false;
        for (int index = 0;
             index < length;
             ++index)
            value[index] = begin[index];
        char* parsed = nullptr;
        const long result =
            std::strtol(value, &parsed, 10);
        if (!parsed || parsed == value ||
            *parsed != 0)
            return false;
        output = static_cast<int>(result);
        return true;
    }

    void CopyField(
        char* output,
        int capacity,
        const char* begin,
        const char* end) noexcept
    {
        if (!output || capacity < 1)
            return;
        int length =
            static_cast<int>(end - begin);
        if (length < 0)
            length = 0;
        if (length >= capacity)
            length = capacity - 1;
        for (int index = 0;
             index < length;
             ++index)
            output[index] = begin[index];
        output[length] = 0;
    }

    void ParseSnapshot(const char* text) noexcept
    {
        FiveRpMetadataResolver::Metadata
            parsed[kCapacity]{};
        int count = 0;
        const char* cursor = text ? text : "";
        bool detected = false;
        int familyCacheCount = 0;
        int familyPendingCount = 0;
        int helperVisible = 0;
        int apiVisible = 0;
        int familyCanary = 0;
        int familyRpcDisabled = 0;
        if (strncmp(cursor, "@5rp\t", 5) == 0)
        {
            detected = cursor[5] == '1';
            int detectedValue = detected ? 1 : 0;
            std::sscanf(
                cursor,
                "@5rp\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
                &detectedValue,
                &familyCacheCount,
                &familyPendingCount,
                &helperVisible,
                &apiVisible,
                &familyCanary,
                &familyRpcDisabled);
            detected = detectedValue != 0;
            while (*cursor && *cursor != '\n')
                ++cursor;
            if (*cursor == '\n')
                ++cursor;
        }
        constexpr int kFieldCount = 13;
        while (*cursor && count < kCapacity)
        {
            const char* lineEnd = cursor;
            while (*lineEnd && *lineEnd != '\n')
                ++lineEnd;
            const char* begins[kFieldCount]{};
            const char* ends[kFieldCount]{};
            const char* field = cursor;
            bool valid = true;
            for (int index = 0;
                 index < kFieldCount - 1;
                 ++index)
            {
                begins[index] = field;
                while (field < lineEnd &&
                    *field != '\t')
                    ++field;
                if (field >= lineEnd)
                {
                    valid = false;
                    break;
                }
                ends[index] = field++;
            }
            begins[kFieldCount - 1] = field;
            ends[kFieldCount - 1] = lineEnd;

            int values[9]{};
            for (int index = 0;
                 valid && index < 9;
                 ++index)
            {
                valid = ParseInteger(
                    begins[index],
                    ends[index],
                    values[index]);
            }
            if (valid && values[0] >= 0 &&
                values[0] <= 65535 &&
                values[1] > 0)
            {
                auto& entry = parsed[count++];
                entry.remoteId =
                    static_cast<std::uint16_t>(
                        values[0]);
                entry.handle =
                    static_cast<std::uint32_t>(
                        values[1]);
                entry.characterId = values[2];
                entry.familyId = values[3];
                entry.factionId = values[4];
                entry.factionType = values[5];
                entry.masked = values[6] != 0;
                entry.displayAdmin =
                    values[7] != 0;
                entry.nametagDisabled =
                    values[8] != 0;
                CopyField(
                    entry.name,
                    static_cast<int>(
                        sizeof(entry.name)),
                    begins[9],
                    ends[9]);
                CopyField(
                    entry.familyName,
                    static_cast<int>(
                        sizeof(entry.familyName)),
                    begins[10],
                    ends[10]);
                CopyField(
                    entry.factionKey,
                    static_cast<int>(
                        sizeof(entry.factionKey)),
                    begins[11],
                    ends[11]);
                CopyField(
                    entry.factionTag,
                    static_cast<int>(
                        sizeof(entry.factionTag)),
                    begins[12],
                    ends[12]);
            }
            cursor = *lineEnd
                ? lineEnd + 1 : lineEnd;
        }

        const ULONGLONG now = GetTickCount64();
        Lock();
        const bool previousDetected = g_Detected;
        const auto previousState = g_State;
        g_LastSnapshotAt = now;
        g_FamilyCacheCount = familyCacheCount;
        g_FamilyPendingCount = familyPendingCount;
        g_HelperVisible = helperVisible != 0;
        g_ApiVisible = apiVisible != 0;
        g_FamilyCanary = familyCanary != 0;
        g_FamilyRpcDisabled = familyRpcDisabled != 0;
        if (detected)
        {
            ++g_PositiveStreak;
            g_NegativeStreak = 0;
            g_LastPositiveAt = now;
            g_Detected = true;
            g_State =
                FiveRpMetadataResolver::State::Confirmed;
            lstrcpynA(
                g_Message,
                "5RP identity confirmed",
                static_cast<int>(sizeof(g_Message)));
        }
        else
        {
            g_PositiveStreak = 0;
            ++g_NegativeStreak;
            g_LastNegativeAt = now;
            if (previousDetected &&
                (g_NegativeStreak < kNegativeConfirmations ||
                 !g_LastPositiveAt ||
                 now - g_LastPositiveAt < kNegativeGraceMs))
            {
                g_Detected = true;
                g_State =
                    FiveRpMetadataResolver::State::LostPending;
                lstrcpynA(
                    g_Message,
                    "5RP identity temporarily missing",
                    static_cast<int>(sizeof(g_Message)));
            }
            else if (
                previousState ==
                    FiveRpMetadataResolver::State::Unknown &&
                g_NegativeStreak < 3)
            {
                g_Detected = false;
                g_State =
                    FiveRpMetadataResolver::State::Unknown;
                lstrcpynA(
                    g_Message,
                    "Collecting 5RP identity signals",
                    static_cast<int>(sizeof(g_Message)));
            }
            else
            {
                g_Detected = false;
                g_State =
                    FiveRpMetadataResolver::State::NotFiveRp;
                lstrcpynA(
                    g_Message,
                    "No sustained 5RP identity signals",
                    static_cast<int>(sizeof(g_Message)));
            }
        }
        ULONGLONG nameGoodAt[kCapacity]{};
        for (int index = 0; index < count; ++index)
        {
            auto& current = parsed[index];
            if (current.name[0])
            {
                nameGoodAt[index] = now;
                continue;
            }
            for (int previous = 0;
                 previous < g_Count;
                 ++previous)
            {
                const auto& cached = g_Entries[previous];
                if (cached.remoteId != current.remoteId ||
                    cached.handle != current.handle ||
                    !cached.name[0] ||
                    !g_NameGoodAt[previous] ||
                    now - g_NameGoodAt[previous] >
                        kNameGraceMs)
                    continue;
                if (cached.characterId > 0 &&
                    current.characterId > 0 &&
                    cached.characterId !=
                        current.characterId)
                    continue;
                lstrcpynA(
                    current.name,
                    cached.name,
                    static_cast<int>(
                        sizeof(current.name)));
                nameGoodAt[index] =
                    g_NameGoodAt[previous];
                break;
            }
        }
        g_Count = count;
        for (int index = 0;
             index < count;
             ++index)
        {
            g_Entries[index] = parsed[index];
            g_NameGoodAt[index] = nameGoodAt[index];
        }
        for (int index = count;
             index < kCapacity;
             ++index)
        {
            g_Entries[index] = {};
            g_NameGoodAt[index] = 0;
        }
        Unlock();
        if (previousDetected != g_Detected ||
            previousState != g_State)
        {
            const bool active = g_Detected;
            const auto state = g_State;
            hooks::HLog(
                active &&
                    state ==
                        FiveRpMetadataResolver::State::Confirmed
                    ? "[FiveRpMetadata] 5RP confirmed"
                    : (active
                        ? "[FiveRpMetadata] identity lost-pending"
                        : "[FiveRpMetadata] not 5RP"));
        }
    }
}

namespace FiveRpMetadataResolver
{
    void Reset() noexcept
    {
        g_PendingCommand = 0;
        g_LastRequest = 0;
        g_ReadyNotBefore = 0;
        Lock();
        g_Detected = false;
        g_State = State::Unknown;
        g_PositiveStreak = 0;
        g_NegativeStreak = 0;
        g_LastSnapshotAt = 0;
        g_LastPositiveAt = 0;
        g_LastNegativeAt = 0;
        g_FamilyCacheCount = 0;
        g_FamilyPendingCount = 0;
        g_HelperVisible = false;
        g_ApiVisible = false;
        g_FamilyCanary = false;
        g_FamilyRpcDisabled = false;
        lstrcpynA(
            g_Message,
            "Waiting for 5RP identity",
            static_cast<int>(sizeof(g_Message)));
        g_Count = 0;
        for (int index = 0;
             index < kCapacity;
             ++index)
        {
            g_Entries[index] = {};
            g_NameGoodAt[index] = 0;
        }
        Unlock();
    }

    void Update(bool active) noexcept
    {
        const auto status =
            RageV8Runtime::GetStatus();
        const ULONGLONG now = GetTickCount64();
        if (status.generation !=
            g_RuntimeGeneration)
        {
            g_RuntimeGeneration =
                status.generation;
            g_PendingCommand = 0;
            g_LastRequest = 0;
            g_ReadyNotBefore = now + 10000;
            Lock();
            g_Count = 0;
            g_FamilyCacheCount = 0;
            g_FamilyPendingCount = 0;
            g_HelperVisible = false;
            g_ApiVisible = false;
            g_FamilyCanary = false;
            g_FamilyRpcDisabled = false;
            for (int index = 0;
                 index < kCapacity;
                 ++index)
            {
                g_Entries[index] = {};
                g_NameGoodAt[index] = 0;
            }
            if (g_Detected)
            {
                g_State = State::LostPending;
                lstrcpynA(
                    g_Message,
                    "Reconnect: keeping confirmed 5RP identity",
                    static_cast<int>(sizeof(g_Message)));
            }
            Unlock();
        }

        if (g_PendingCommand)
        {
            RageV8Runtime::Result result{};
            if (RageV8Runtime::PollResultFor(
                    g_PendingCommand,
                    result))
            {
                if (result.success)
                    ParseSnapshot(result.text);
                g_PendingCommand = 0;
            }
            else if (now - g_LastRequest >= 4500 ||
                status.state !=
                    RageV8Runtime::State::Ready)
            {
                g_PendingCommand = 0;
            }
        }

        Lock();
        const State identityState = g_State;
        Unlock();
        const ULONGLONG interval =
            !active
                ? 5000ULL
                : (identityState == State::Confirmed
                    ? 2000ULL
                    : 1000ULL);
        if (g_PendingCommand ||
            status.state !=
                RageV8Runtime::State::Ready ||
            !status.rootCaptured ||
            now < g_ReadyNotBefore ||
            now - g_LastRequest < interval)
            return;
        g_LastRequest = now;
        constexpr bool kEnqueueIdentityEval = false;
        if (!kEnqueueIdentityEval)
            return;
        if (!RageV8Runtime::Enqueue(
                kSafeSnapshotScript,
                &g_PendingCommand))
        {
            InterlockedIncrement64(&g_EnqueueFailures);
            g_PendingCommand = 0;
        }
    }

    bool TryGet(
        std::uint16_t remoteId,
        std::uint32_t handle,
        Metadata& output) noexcept
    {
        output = {};
        Lock();
        for (int index = 0;
             index < g_Count;
             ++index)
        {
            if (g_Entries[index].remoteId !=
                    remoteId ||
                (handle &&
                 g_Entries[index].handle != handle))
                continue;
            output = g_Entries[index];
            Unlock();
            return true;
        }
        Unlock();
        return false;
    }

    int Count() noexcept
    {
        Lock();
        const int count = g_Count;
        Unlock();
        return count;
    }

    bool IsDetected() noexcept
    {
        Lock();
        const bool detected = g_Detected;
        Unlock();
        return detected;
    }

    StatusSnapshot GetStatus() noexcept
    {
        StatusSnapshot status{};
        Lock();
        status.state = g_State;
        status.detected = g_Detected;
        status.positiveStreak = g_PositiveStreak;
        status.negativeStreak = g_NegativeStreak;
        status.generation = g_RuntimeGeneration;
        status.lastSnapshotAt = g_LastSnapshotAt;
        status.lastPositiveAt = g_LastPositiveAt;
        status.lastNegativeAt = g_LastNegativeAt;
        status.metadataCount = g_Count;
        status.familyCacheCount = g_FamilyCacheCount;
        status.familyPendingCount = g_FamilyPendingCount;
        status.helperVisible = g_HelperVisible;
        status.apiVisible = g_ApiVisible;
        status.familyCanary = g_FamilyCanary;
        status.familyRpcDisabled = g_FamilyRpcDisabled;
        lstrcpynA(
            status.message,
            g_Message,
            static_cast<int>(sizeof(status.message)));
        Unlock();
        status.pending = g_PendingCommand != 0;
        status.enqueueFailures =
            static_cast<std::uint64_t>(
                InterlockedCompareExchange64(
                    &g_EnqueueFailures, 0, 0));
        return status;
    }

    const char* StateName(State state) noexcept
    {
        switch (state)
        {
        case State::Confirmed: return "CONFIRMED";
        case State::LostPending: return "LOST-PENDING";
        case State::NotFiveRp: return "NOT-5RP";
        default: return "UNKNOWN";
        }
    }
}
