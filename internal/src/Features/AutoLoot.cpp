#include "AutoLoot.h"

#include "RageV8Runtime.h"
#include "ServerProfile.h"
#include "../vmp_markers.hpp"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace
{
    // One pass does the whole job: it reads the ground items, picks the nearest
    // one that passes the filter, finds a free inventory slot and raises their
    // own bridge event. Splitting it would cost a script round trip per step,
    // and the script thread is the one thing here that must stay cheap.
    constexpr char kBody[] =
R"JS(return (function(){try{
var g=(typeof globalThis!=='undefined'&&globalThis)||
(typeof global!=='undefined'&&global)||this||{};
var ctx=g.__ctx__;
var rage=(ctx&&ctx.mp)||g.mp||(typeof mp!=='undefined'?mp:null);
var st=g.__bfAutoLoot;
if(!st||typeof st!=='object'){
st={slots:null,cool:{},miss:{},taken:0,refused:0,seq:-20000,pendId:0,pendAt:0,
pendSlot:-1,pendItem:0,pendName:'',lastFire:0,guess:19,known:0,censusAt:0,
note:'',armed:false};
try{g.__bfAutoLoot=st;}catch(_){}}
if(!st.cool)st.cool={};
if(!st.miss)st.miss={};
var line=function(reach,known,free,note){
var text=String(note||st.note||'');
if(st.slotSrc)text+=' ['+st.slotSrc+']';
return ['@loot',reach,known,st.taken,st.refused,st.slots?1:0,free,
text.replace(/[\t\r\n]/g,' ').slice(0,90)].join('\t');};
if(!rage||!rage.objects)return line(0,0,0,'no runtime');
/* Registered once and kept: these fire when the inventory changes, so a
listener that came and went with each pass would miss the one that matters.
Opening the window is not required — a change is enough, and our own first
pickup is a change — but the shape of each payload is not documented anywhere,
so several are accepted and the one that answered is reported. */
var absorb=function(src,payload){try{
if(!payload||typeof payload==='string')return;
if(typeof payload.length==='number'&&payload.length>0){
var arr=[].slice.call(payload,0,60);
var looksLikeSlots=false;
for(var i=0;i<arr.length;i++){
var cell=String(arr[i]);
if(cell==='E'||cell.indexOf('I_')===0){looksLikeSlots=true;break;}}
if(looksLikeSlots){st.slots=arr;st.slotSrc=src;return;}}
/* The inventory payload carries the same thing as a run of 0 and 1. */
var bits=payload._c;
if(typeof bits==='string'&&/^[01]+$/.test(bits)&&bits.length>3){
var list=[];
for(var b=0;b<bits.length;b++)list.push(bits.charAt(b)==='0'?'E':'X');
st.slots=list;st.slotSrc=src;return;}
if(payload.slots)absorb(src,payload.slots);
if(payload.items)absorb(src,payload.items);
}catch(_){}};
if(!st.armed){st.armed=true;
try{rage.events.add('__client_inventory_setLastItems',function(list){
absorb('lastItems',list);});}catch(_){}
/* Fires on a slot change rather than on the window opening, which is what
makes a map available without the player ever opening the inventory. */
try{rage.events.add('client_inventory_updateSlots',function(a,b){
absorb('updateSlots',a);absorb('updateSlots2',b);});}catch(_){}
try{['cr:ok','cr:error','cr:err'].forEach(function(name){
rage.events.add(name,function(reqId,payload){try{
if(!st.pendId||String(reqId)!==String(st.pendId))return;
/* Their handler answers on cr:ok either way: a take that was refused comes
back as a plain false, so the payload is the verdict, not the event name. */
var ok=name==='cr:ok'&&payload!==false&&payload!=='false'&&
payload!==null&&payload!==undefined;
st.pendId=0;
if(ok){st.taken++;st.note='took '+st.pendName;
if(st.miss)delete st.miss[st.pendItem];
/* The server pushes a fresh slot map after the change, but not before the next
item is considered, so the slot just filled is marked here as well. */
if(st.slots&&st.pendSlot>=0&&st.pendSlot<st.slots.length)
st.slots[st.pendSlot]='X';}
else{st.refused++;st.note='refused '+st.pendName;
/* A refusal is usually the server's distance rule, and that will not change
while the player stands still, so the same item backs off further each time
instead of being asked again every second. */
if(!st.miss)st.miss={};
var seen=(st.miss[st.pendItem]||0)+1;
st.miss[st.pendItem]=seen;
var wait=seen>=4?300000:(seen>=3?120000:(seen>=2?30000:8000));
st.cool[st.pendItem]=Date.now()+wait;
/* Without a real slot map the index is a guess, and a refusal is one of the
few signals that it was wrong; walking down finds a free one in a few tries. */
if(!st.slots&&st.guess>4)st.guess--;}
}catch(_){}});});}catch(_){}}
var now=Date.now();
var local=rage.players&&rage.players.local;
var me=local&&local.position;
if(!me)return line(0,0,0,'no player');
var lower=[];
for(var n=0;n<CFG.names.length;n++)lower.push(String(CFG.names[n]).toLowerCase());
var wanted=function(name){
if(!CFG.only)return true;
var low=String(name).toLowerCase();
for(var i=0;i<lower.length;i++)if(low.indexOf(lower[i])!==-1)return true;
return false;};
var reach=0,bestD=1e9,best=null;
var consider=function(o){try{
var raw=o.__objectItem;
if(raw===undefined||raw===null)return;
var p=o.position;
if(!p)return;
var dx=p.x-me.x,dy=p.y-me.y,dz=p.z-me.z;
var d=Math.sqrt(dx*dx+dy*dy+dz*dz);
if(d>CFG.r)return;
var parts=String(raw).split('$#$');
var id=parseInt(parts[0],10);
if(!id||id<=0)return;
if(!wanted(parts[1]||''))return;
++reach;
if(st.cool[id]&&st.cool[id]>now)return;
if(d<bestD){bestD=d;
best={id:id,name:String(parts[1]||''),count:parseInt(parts[2],10)||1};}
}catch(_){}};
/* Only what the game has streamed in. Walking every object the client knows
about means several thousand position reads per pass, which is both slow enough
to be noticed and pointless: an item outside stream range is far past the
distance the server will accept anyway. */
try{
if(typeof rage.objects.forEachInStreamRange==='function')
rage.objects.forEachInStreamRange(consider);
else rage.objects.forEach(consider);
}catch(_){}
/* The map-wide count is for the readout only, so it is sampled rarely rather
than on the path that has to stay quick. */
if(!st.censusAt||now-st.censusAt>5000){st.censusAt=now;
var total=0;
try{rage.objects.forEach(function(o){try{
if(o.__objectItem!==undefined&&o.__objectItem!==null)++total;}catch(_){}});}
catch(_){}
st.known=total;}
var known=st.known||0;
var free=0,slot=-1;
if(st.slots){for(var s=0;s<st.slots.length;s++){
if(String(st.slots[s])==='E'){++free;if(slot<0)slot=s;}}}
else slot=st.guess;
/* Cooldowns are only useful while they are in the future; left alone the map
would grow for as long as the session lasts. */
if(!st.swept||now-st.swept>20000){st.swept=now;
for(var key in st.cool){if(st.cool[key]<=now)delete st.cool[key];}}
if(st.pendId){
if(now-st.pendAt<3000)return line(reach,known,free,'waiting for server');
st.pendId=0;st.refused++;st.note='no answer';}
if(!CFG.on)return line(reach,known,free,'off');
if(!best)return line(reach,known,free,reach?'all on cooldown':'nothing in range');
if(slot<0)return line(reach,known,free,'inventory full');
if(now-st.lastFire<CFG.gap)return line(reach,known,free,'pacing');
st.lastFire=now;
/* Held off briefly either way: the object stays in the client's list for a
moment after a successful take, and retrying it would spend a round trip to be
told what we already know. A refusal replaces this with a longer wait. */
st.cool[best.id]=now+4000;
st.pendId=st.seq--;st.pendAt=now;st.pendSlot=slot;
st.pendItem=best.id;
st.pendName=best.name.slice(0,24);
try{rage.events.call('cr:call:id',st.pendId,
'inv:takeWorldItemToSlot','inv',-1,slot,best.id,best.count);}
catch(e){st.pendId=0;st.note='call failed';}
return line(reach,known,free,'taking '+st.pendName);
}catch(e){return '@loot\t0\t0\t0\t0\t0\t0\tfailed '+String(e).slice(0,40);}})();)JS";

    AutoLoot::Settings g_Settings{};
    AutoLoot::Status g_Status{};

    char g_Filters[AutoLoot::kMaxFilters][AutoLoot::kFilterLength]{};
    int g_FilterCount = 0;
    // Seeded once and remembered, so a list the user has pruned is not
    // refilled behind their back on the next launch.
    bool g_Seeded = false;

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

    // Names arrive from the server in Cyrillic, so they are passed through as
    // UTF-8 and folded to lower case inside the script: a byte-wise fold here
    // would only ever match the ASCII half.
    int AppendJsString(
        char* output,
        int capacity,
        const char* text) noexcept
    {
        int written = 0;
        const auto put = [&](char value) {
            if (written < capacity - 1)
                output[written++] = value;
        };
        put('"');
        for (const char* cursor = text; *cursor; ++cursor)
        {
            const char value = *cursor;
            if (value == '"' || value == '\\')
            {
                put('\\');
                put(value);
            }
            else if (static_cast<unsigned char>(value) >= 0x20)
                put(value);
        }
        put('"');
        output[written] = 0;
        return written;
    }

    void ParseStatus(const char* text) noexcept
    {
        if (!text || std::strncmp(text, "@loot", 5) != 0)
            return;
        int reach = 0;
        int known = 0;
        int taken = 0;
        int refused = 0;
        int slotsKnown = 0;
        int free = 0;
        const char* cursor = text + 5;
        int* const fields[] = {
            &reach, &known, &taken, &refused, &slotsKnown, &free};
        for (int index = 0; index < 6; ++index)
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
        g_Status.reachable = reach;
        g_Status.knownItems = known;
        g_Status.taken = taken;
        g_Status.refused = refused;
        g_Status.slotsKnown = slotsKnown != 0;
        g_Status.freeSlots = free;
        lstrcpynA(
            g_Status.detail,
            detail,
            static_cast<int>(sizeof(g_Status.detail)));
        Unlock();
    }
}

namespace AutoLoot
{
    Settings& Access() noexcept
    {
        return g_Settings;
    }

    int FilterCount() noexcept
    {
        return g_FilterCount;
    }

    const char* FilterAt(int index) noexcept
    {
        if (index < 0 || index >= g_FilterCount)
            return "";
        return g_Filters[index];
    }

    bool AddFilter(const char* name) noexcept
    {
        if (!name || !name[0] || g_FilterCount >= kMaxFilters)
            return false;
        for (int index = 0; index < g_FilterCount; ++index)
            if (lstrcmpiA(g_Filters[index], name) == 0)
                return false;
        lstrcpynA(g_Filters[g_FilterCount], name, kFilterLength);
        ++g_FilterCount;
        return true;
    }

    void RemoveFilter(int index) noexcept
    {
        if (index < 0 || index >= g_FilterCount)
            return;
        for (int slot = index; slot + 1 < g_FilterCount; ++slot)
            lstrcpynA(g_Filters[slot], g_Filters[slot + 1], kFilterLength);
        --g_FilterCount;
        g_Filters[g_FilterCount][0] = 0;
    }

    // Read out of the server's own name table in the interface bundle rather
    // than invented, so they match what actually lands on the ground. Matching
    // is by substring because the server appends state to the name, as in
    // "Carbine Rifle (96%)".
    void SeedDefaultFilters() noexcept
    {
        static const char* const defaults[] = {
            "Advanced Rifle",
            "Assault Rifle",
            "Assault Shotgun",
            "Assault SMG",
            "Carbine Rifle",
            "Compact Rifle",
            "Gusenberg Sweeper",
            "Heavy Shotgun",
            "Micro SMG",
            "Musket",
            "Navy Revolver",
            "Pump Shotgun",
            "Service Carbine",
            "SMG",
            "Sniper Rifle",
            "Special Rifle",
            "Автоматический пистолет",
            "Боевой пистолет",
            "Дробовик булл-пап",
            "Кольт",
            "Малокалиберная винтовка",
            "Мини-СМГ",
            "Обрез",
            "Пистолет",
            "Револьвер",
            "Сигнальная ракетница",
            "Старинный пистолет",
            "Тазер",
            "Тяжелый пистолет",
            "Бита",
            "Гаечный ключ",
            "Клюшка для гольфа",
            "Лом",
            "Молоток",
            "Нож",
            "Резиновая дубинка",
            "Топор",
            "Фонарик",
            // Ammunition is named by calibre alone on the ground.
            "5.56mm",
            "7.62mm",
            "9mm",
            "12mm",
            "SPANK",
            "Броня",
            "Аптечка"};
        for (const char* name : defaults)
            AddFilter(name);
    }

    void Reset() noexcept
    {
        g_PendingCommand = 0;
        g_LastRequest = 0;
        Lock();
        g_Status = {};
        Unlock();
    }

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept
    {
        if (!output)
            return false;
        int written = _snprintf_s(
            output + used,
            capacity - used,
            _TRUNCATE,
            "loot_on=%d\nloot_take_all=%d\nloot_notify=%d\nloot_ring=%d\n"
            "loot_radius=%.3f\nloot_delay=%d\nloot_seeded=%d\nloot_names=%d\n",
            g_Settings.enabled ? 1 : 0,
            g_Settings.takeAll ? 1 : 0,
            g_Settings.notify ? 1 : 0,
            g_Settings.showRadius ? 1 : 0,
            static_cast<double>(g_Settings.radius),
            g_Settings.intervalMs,
            g_Seeded ? 1 : 0,
            g_FilterCount);
        if (written <= 0)
            return false;
        used += static_cast<std::size_t>(written);
        // One key per entry rather than a delimited blob: item names carry
        // spaces and brackets, and a separator would have to be escaped.
        for (int index = 0; index < g_FilterCount; ++index)
        {
            written = _snprintf_s(
                output + used,
                capacity - used,
                _TRUNCATE,
                "loot_name%d=%s\n",
                index,
                g_Filters[index]);
            if (written <= 0)
                return false;
            used += static_cast<std::size_t>(written);
        }
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
        if (const char* value = find("loot_on"))
            g_Settings.enabled = std::atoi(value) != 0;
        if (const char* value = find("loot_take_all"))
            g_Settings.takeAll = std::atoi(value) != 0;
        if (const char* value = find("loot_notify"))
            g_Settings.notify = std::atoi(value) != 0;
        if (const char* value = find("loot_ring"))
            g_Settings.showRadius = std::atoi(value) != 0;
        if (const char* value = find("loot_radius"))
        {
            const float radius = static_cast<float>(std::atof(value));
            if (radius >= kMinRadius && radius <= kMaxRadius)
                g_Settings.radius = radius;
        }
        if (const char* value = find("loot_delay"))
        {
            const int delay = std::atoi(value);
            if (delay >= 150 && delay <= 5000)
                g_Settings.intervalMs = delay;
        }
        if (const char* value = find("loot_seeded"))
            g_Seeded = std::atoi(value) != 0;
        const char* countText = find("loot_names");
        if (!countText)
        {
            if (!g_Seeded)
            {
                SeedDefaultFilters();
                g_Seeded = true;
            }
            return;
        }
        int count = std::atoi(countText);
        if (count < 0)
            count = 0;
        if (count > kMaxFilters)
            count = kMaxFilters;
        g_FilterCount = 0;
        for (int index = 0; index < count; ++index)
        {
            char key[32]{};
            _snprintf_s(key, sizeof(key), _TRUNCATE, "loot_name%d", index);
            const char* value = find(key);
            if (!value)
                continue;
            char name[kFilterLength]{};
            int length = 0;
            while (*value && *value != '\n' && *value != '\r' &&
                   length < kFilterLength - 1)
                name[length++] = *value++;
            name[length] = 0;
            AddFilter(name);
        }
        if (!g_Seeded)
        {
            SeedDefaultFilters();
            g_Seeded = true;
        }
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
        // Covers a first run with no config to import from, where nothing else
        // would ever put the starting list in place.
        if (!g_Seeded)
        {
            SeedDefaultFilters();
            g_Seeded = true;
        }

        const auto runtime = RageV8Runtime::GetStatus();
        const ULONGLONG now = GetTickCount64();
        if (runtime.generation != g_RuntimeGeneration)
        {
            g_RuntimeGeneration = runtime.generation;
            g_PendingCommand = 0;
            g_LastRequest = 0;
            Lock();
            g_Status = {};
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
            else if (now - g_LastRequest >= 4000 ||
                     runtime.state != RageV8Runtime::State::Ready)
                g_PendingCommand = 0;
        }

        // The whole feature is their own item bridge: the objects carry their
        // own payload property and the take is their own local call. On a
        // server without it there is nothing to scan and nothing to raise.
        if (!g_Settings.enabled || !ServerProfile::HasItemBridge())
        {
            if (g_Status.reachable || g_Status.detail[0])
            {
                Lock();
                g_Status.reachable = 0;
                g_Status.detail[0] = 0;
                Unlock();
            }
            return;
        }

        // Waiting for a completely empty queue made pickups take seconds: the
        // player snapshot alone keeps one script in flight most of the time. A
        // short queue is fine to sit behind, since the scan itself now only
        // touches streamed objects.
        const ULONGLONG gap =
            g_Settings.intervalMs > 150
                ? static_cast<ULONGLONG>(g_Settings.intervalMs)
                : 150ull;
        if (g_PendingCommand ||
            runtime.queuedCommands > 2 ||
            runtime.state != RageV8Runtime::State::Ready ||
            !runtime.rootCaptured ||
            now - g_LastRequest < gap)
            return;

        float radius = g_Settings.radius;
        if (radius < kMinRadius)
            radius = kMinRadius;
        if (radius > kMaxRadius)
            radius = kMaxRadius;

        char names[1400]{};
        int used = 0;
        for (int index = 0; index < g_FilterCount; ++index)
        {
            if (used && used < static_cast<int>(sizeof(names)) - 2)
                names[used++] = ',';
            used += AppendJsString(
                names + used,
                static_cast<int>(sizeof(names)) - used,
                g_Filters[index]);
        }
        names[used] = 0;

        char script[sizeof(kBody) + 1700]{};
        const int header = _snprintf_s(
            script,
            sizeof(script),
            _TRUNCATE,
            "var CFG={on:1,only:%d,r:%.2f,gap:%d,names:[%s]};\n",
            g_Settings.takeAll ? 0 : 1,
            static_cast<double>(radius),
            g_Settings.intervalMs,
            names);
        if (header <= 0)
            return;
        SHADOW_PROTECT_MUT("AutoLoot.Load");
        const char* body = SHADOW_DECRYPT_STRING_A(kBody);
        if (!body)
        {
            SHADOW_PROTECT_END();
            SecureZeroMemory(names, sizeof(names));
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
        SecureZeroMemory(names, sizeof(names));
        SecureZeroMemory(script, sizeof(script));
        if (!queued)
            g_PendingCommand = 0;
    }
}
