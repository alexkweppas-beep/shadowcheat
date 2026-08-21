#include "RageWorldEntities.h"

#include "RageV8Runtime.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr ULONGLONG kRefreshIntervalMs = 150;
    constexpr ULONGLONG kRequestTimeoutMs = 1500;
    constexpr int kFieldCount = 15;

    // Every payload field is an integer. Positions/headings/extents are scaled
    // by 100 and health values by 10, which keeps parsing locale-independent.
    constexpr char kSnapshotScript[] =
R"RAGEWORLD(return (()=>{try{
const MAX_ROWS=512;
const MAX_BYTES=32000;
const rows=[];
const seen=Object.create(null);
const dimensionCache=Object.create(null);
let bytes=0;
let full=false;
const number=value=>{const result=Number(value);return Number.isFinite(result)?result:NaN;};
const clamp=(value,minimum,maximum,fallback)=>{
const result=number(value);
return Number.isFinite(result)&&result>=minimum&&result<=maximum?result:fallback;};
const quantize=(value,scale)=>Math.round(value*scale);
const fallbackDimensions=(kind,vehicleClass)=>{
if(kind===0){
if(vehicleClass===1)return [0.45,1.10,0.75];
if(vehicleClass===2)return [1.50,4.00,1.20];
if(vehicleClass===3)return [6.00,6.00,2.00];
if(vehicleClass===4)return [1.20,2.50,1.00];
return [1.00,2.40,0.90];
}
if(kind===2)return [0.25,0.25,0.25];
return [0.50,0.50,0.50];
};
const dimensions=(model,kind,vehicleClass)=>{
const key=String(kind)+':'+String(vehicleClass)+':'+String(model);
if(dimensionCache[key])return dimensionCache[key];
const result=fallbackDimensions(kind,vehicleClass);
try{
const gameplay=mp&&mp.game&&mp.game.gameplay;
if(model&&gameplay&&typeof gameplay.getModelDimensions==='function'){
const value=gameplay.getModelDimensions(model);
const minimum=value&&(value.min||value.minimum);
const maximum=value&&(value.max||value.maximum);
if(minimum&&maximum){
const x=Math.abs(number(maximum.x)-number(minimum.x))*0.5;
const y=Math.abs(number(maximum.y)-number(minimum.y))*0.5;
const z=Math.abs(number(maximum.z)-number(minimum.z))*0.5;
if(Number.isFinite(x)&&x>=0.01&&x<=1000)result[0]=x;
if(Number.isFinite(y)&&y>=0.01&&y<=1000)result[1]=y;
if(Number.isFinite(z)&&z>=0.01&&z<=1000)result[2]=z;
}
}
}catch(_){}
dimensionCache[key]=result;
return result;
};
const classifyVehicle=entity=>{
let nativeClass=-1;
try{
if(typeof entity.getClass==='function')nativeClass=number(entity.getClass());
}catch(_){}
try{
if(!Number.isFinite(nativeClass)){
const vehicleApi=mp&&mp.game&&mp.game.vehicle;
if(vehicleApi&&typeof vehicleApi.getClass==='function')
nativeClass=number(vehicleApi.getClass(entity.handle));
}
}catch(_){}
if(nativeClass===8||nativeClass===13)return 1;
if(nativeClass===14)return 2;
if(nativeClass===15||nativeClass===16)return 3;
if((nativeClass>=0&&nativeClass<=7)||
(nativeClass>=9&&nativeClass<=12)||
(nativeClass>=17&&nativeClass<=20)||
nativeClass===22)return 0;
return 4;
};
const headingOf=entity=>{
let heading=NaN;
try{
if(typeof entity.getHeading==='function')heading=number(entity.getHeading());
}catch(_){}
try{
if(!Number.isFinite(heading))heading=number(entity.heading);
}catch(_){}
try{
if(!Number.isFinite(heading)){
const rotation=entity.rotation;
if(rotation)heading=number(rotation.z);
}
}catch(_){}
if(!Number.isFinite(heading))heading=0;
heading=((heading%360)+360)%360;
return heading;
};
const healthOf=(entity,handle)=>{
let health=NaN;
try{
if(typeof entity.getHealth==='function')health=number(entity.getHealth());
}catch(_){}
try{
if(!Number.isFinite(health)){
const entityApi=mp&&mp.game&&mp.game.entity;
if(entityApi&&typeof entityApi.getHealth==='function')
health=number(entityApi.getHealth(handle));
}
}catch(_){}
return clamp(health,-100000,100000,0);
};
const bodyHealthOf=(entity,handle,fallback)=>{
let health=NaN;
try{
if(typeof entity.getBodyHealth==='function')
health=number(entity.getBodyHealth());
}catch(_){}
try{
if(!Number.isFinite(health)){
const vehicleApi=mp&&mp.game&&mp.game.vehicle;
if(vehicleApi&&typeof vehicleApi.getBodyHealth==='function')
health=number(vehicleApi.getBodyHealth(handle));
}
}catch(_){}
return clamp(health,-100000,100000,fallback);
};
const isOccupied=(entity,handle)=>{
try{
if(typeof entity.getNumberOfPassengers==='function'&&
number(entity.getNumberOfPassengers())>0)return true;
}catch(_){}
try{
if(typeof entity.isSeatFree==='function'){
const free=entity.isSeatFree(-1);
if(free===false||free===0)return true;
}
}catch(_){}
try{
const vehicleApi=mp&&mp.game&&mp.game.vehicle;
if(vehicleApi&&typeof vehicleApi.getNumberOfPassengers==='function'&&
number(vehicleApi.getNumberOfPassengers(handle))>0)return true;
if(vehicleApi&&typeof vehicleApi.isSeatFree==='function'){
const free=vehicleApi.isSeatFree(handle,-1);
if(free===false||free===0)return true;
}
}catch(_){}
return false;
};
const append=values=>{
if(full)return;
const row=values.join('\t');
const additional=row.length+(rows.length?1:0);
if(rows.length>=MAX_ROWS||bytes+additional>MAX_BYTES){
full=true;
return;
}
rows.push(row);
bytes+=additional;
};
const emit=(entity,kind,pool)=>{
if(full||!entity)return;
try{
if(pool&&typeof pool.exists==='function'&&!pool.exists(entity))return;
}catch(_){}
let handleValue=NaN;
try{handleValue=number(entity.handle);}catch(_){}
if(!Number.isFinite(handleValue)||handleValue<=0||
handleValue>4294967295||Math.floor(handleValue)!==handleValue)return;
const handle=handleValue>>>0;
if(!handle||seen[String(handle)])return;
seen[String(handle)]=1;
let position=null;
try{position=entity.position;}catch(_){}
let x=position?number(position.x):NaN;
let y=position?number(position.y):NaN;
let z=position?number(position.z):NaN;
try{
if(!Number.isFinite(x)||!Number.isFinite(y)||!Number.isFinite(z)){
if(typeof entity.getCoords==='function'){
position=entity.getCoords(true);
x=position?number(position.x):NaN;
y=position?number(position.y):NaN;
z=position?number(position.z):NaN;
}
}
}catch(_){}
if(!Number.isFinite(x)||!Number.isFinite(y)||!Number.isFinite(z)||
Math.abs(x)>100000||Math.abs(y)>100000||Math.abs(z)>100000)return;
let remoteId=-1;
try{
const value=number(entity.remoteId);
if(Number.isFinite(value)&&value>=0&&value<=4294967295&&
Math.floor(value)===value)remoteId=value;
}catch(_){}
let model=0;
try{
let value=entity.model;
if((value===undefined||value===null)&&
typeof entity.getModel==='function')value=entity.getModel();
const numeric=number(value);
if(Number.isFinite(numeric))model=numeric>>>0;
}catch(_){}
const heading=headingOf(entity);
let health=healthOf(entity,handle);
let bodyHealth=0;
let occupied=false;
let vehicleClass=4;
if(kind===0){
vehicleClass=classifyVehicle(entity);
bodyHealth=bodyHealthOf(entity,handle,health);
if(!health&&bodyHealth)health=bodyHealth;
occupied=isOccupied(entity,handle);
}
const halfExtents=dimensions(model,kind,vehicleClass);
append([
kind,handle,remoteId,model,
quantize(x,100),quantize(y,100),quantize(z,100),
quantize(heading,100),quantize(health,10),
quantize(bodyHealth,10),occupied?1:0,vehicleClass,
quantize(halfExtents[0],100),
quantize(halfExtents[1],100),
quantize(halfExtents[2],100)
]);
};
const visit=(pool,kind)=>{
if(full||!pool||typeof pool.forEach!=='function')return;
try{pool.forEach(entity=>emit(entity,kind,pool));}catch(_){}
};
visit(mp&&mp.vehicles,0);
visit(mp&&mp.objects,1);
visit(mp&&mp.pickups,2);
const body=rows.join('\n');
return 'RWE1\t'+String(rows.length)+(body?'\n'+body:'');
}catch(_){return 'RWE1\t0';}})())RAGEWORLD";

    volatile LONG g_Lock = 0;
    RageWorldEntities::Entry
        g_Entries[RageWorldEntities::kMaxEntries]{};
    int g_Count = 0;
    std::uint64_t g_Generation = 0;
    std::uint64_t g_CapturedAtMs = 0;
    std::uint64_t g_RuntimeGeneration = 0;
    std::uint64_t g_PendingCommand = 0;
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

    void ClearPublishedSnapshot() noexcept
    {
        Lock();
        for (auto& entry : g_Entries)
            entry = {};
        g_Count = 0;
        g_CapturedAtMs = 0;
        ++g_Generation;
        Unlock();
    }

    void ResetSessionState() noexcept
    {
        g_PendingCommand = 0;
        g_LastRequest = 0;
        ClearPublishedSnapshot();
    }

    bool ParseInteger(
        const char* begin,
        const char* end,
        std::int64_t& output) noexcept
    {
        output = 0;
        if (!begin || !end || begin >= end)
            return false;

        bool negative = false;
        if (*begin == '-')
        {
            negative = true;
            ++begin;
        }
        if (begin >= end)
            return false;

        constexpr std::uint64_t kPositiveLimit =
            0x7FFFFFFFFFFFFFFFULL;
        constexpr std::uint64_t kNegativeLimit =
            0x8000000000000000ULL;
        const std::uint64_t limit =
            negative ? kNegativeLimit : kPositiveLimit;
        std::uint64_t magnitude = 0;
        while (begin < end)
        {
            if (*begin < '0' || *begin > '9')
                return false;
            const auto digit =
                static_cast<std::uint64_t>(*begin - '0');
            if (magnitude > (limit - digit) / 10)
                return false;
            magnitude = magnitude * 10 + digit;
            ++begin;
        }

        if (!negative)
        {
            output = static_cast<std::int64_t>(magnitude);
            return true;
        }
        if (magnitude == kNegativeLimit)
        {
            output = static_cast<std::int64_t>(
                0x8000000000000000ULL);
            return true;
        }
        output = -static_cast<std::int64_t>(magnitude);
        return true;
    }

    bool SplitFields(
        const char* begin,
        const char* end,
        const char** fieldBegin,
        const char** fieldEnd) noexcept
    {
        if (!begin || !end || begin >= end ||
            !fieldBegin || !fieldEnd)
            return false;
        const char* field = begin;
        for (int index = 0; index < kFieldCount; ++index)
        {
            fieldBegin[index] = field;
            const char* delimiter = field;
            while (delimiter < end && *delimiter != '\t')
                ++delimiter;
            fieldEnd[index] = delimiter;
            if (index == kFieldCount - 1)
                return delimiter == end;
            if (delimiter >= end)
                return false;
            field = delimiter + 1;
        }
        return false;
    }

    bool BuildEntry(
        const char** fieldBegin,
        const char** fieldEnd,
        RageWorldEntities::Entry& entry) noexcept
    {
        std::int64_t values[kFieldCount]{};
        for (int index = 0; index < kFieldCount; ++index)
        {
            if (!ParseInteger(
                    fieldBegin[index],
                    fieldEnd[index],
                    values[index]))
                return false;
        }

        if (values[0] < 0 || values[0] > 2 ||
            values[1] <= 0 ||
            values[1] > 0xFFFFFFFFLL ||
            values[2] < -1 ||
            values[2] > 0xFFFFFFFFLL ||
            values[3] < 0 ||
            values[3] > 0xFFFFFFFFLL ||
            values[4] < -10000000 ||
            values[4] > 10000000 ||
            values[5] < -10000000 ||
            values[5] > 10000000 ||
            values[6] < -10000000 ||
            values[6] > 10000000 ||
            values[7] < 0 || values[7] > 36000 ||
            values[8] < -1000000 ||
            values[8] > 1000000 ||
            values[9] < -1000000 ||
            values[9] > 1000000 ||
            values[10] < 0 || values[10] > 1 ||
            values[11] < 0 || values[11] > 4 ||
            values[12] < 1 || values[12] > 100000 ||
            values[13] < 1 || values[13] > 100000 ||
            values[14] < 1 || values[14] > 100000)
            return false;

        const auto kind = static_cast<
            RageWorldEntities::EntityKind>(values[0]);
        const auto vehicleClass = static_cast<
            RageWorldEntities::VehicleClass>(values[11]);
        if (kind != RageWorldEntities::EntityKind::Vehicle &&
            (values[10] != 0 ||
             vehicleClass !=
                RageWorldEntities::VehicleClass::Other))
            return false;

        entry = {};
        entry.valid = true;
        entry.kind = kind;
        entry.handle =
            static_cast<std::uint32_t>(values[1]);
        entry.remoteId = values[2] < 0
            ? RageWorldEntities::kInvalidRemoteId
            : static_cast<std::uint32_t>(values[2]);
        entry.modelHash =
            static_cast<std::uint32_t>(values[3]);
        entry.x = static_cast<float>(values[4]) / 100.0f;
        entry.y = static_cast<float>(values[5]) / 100.0f;
        entry.z = static_cast<float>(values[6]) / 100.0f;
        entry.heading =
            static_cast<float>(values[7]) / 100.0f;
        if (entry.heading >= 360.0f)
            entry.heading = 0.0f;
        entry.health =
            static_cast<float>(values[8]) / 10.0f;
        entry.bodyHealth =
            static_cast<float>(values[9]) / 10.0f;
        entry.occupied = values[10] != 0;
        entry.vehicleClass = vehicleClass;
        entry.halfExtentX =
            static_cast<float>(values[12]) / 100.0f;
        entry.halfExtentY =
            static_cast<float>(values[13]) / 100.0f;
        entry.halfExtentZ =
            static_cast<float>(values[14]) / 100.0f;
        return std::isfinite(entry.x) &&
            std::isfinite(entry.y) &&
            std::isfinite(entry.z) &&
            std::isfinite(entry.heading) &&
            std::isfinite(entry.health) &&
            std::isfinite(entry.bodyHealth) &&
            std::isfinite(entry.halfExtentX) &&
            std::isfinite(entry.halfExtentY) &&
            std::isfinite(entry.halfExtentZ);
    }

    void Publish(
        const RageWorldEntities::Entry* entries,
        int count,
        ULONGLONG capturedAt) noexcept
    {
        Lock();
        const int previousCount = g_Count;
        for (int index = 0; index < count; ++index)
            g_Entries[index] = entries[index];
        for (int index = count;
             index < previousCount;
             ++index)
            g_Entries[index] = {};
        g_Count = count;
        g_CapturedAtMs =
            static_cast<std::uint64_t>(capturedAt);
        ++g_Generation;
        Unlock();
    }

    bool ParseSnapshot(
        const char* text,
        ULONGLONG capturedAt) noexcept
    {
        if (!text || std::strncmp(text, "RWE1\t", 5) != 0)
            return false;

        const char* headerBegin = text + 5;
        const char* headerEnd = headerBegin;
        while (*headerEnd && *headerEnd != '\n')
            ++headerEnd;
        const char* numericHeaderEnd = headerEnd;
        if (numericHeaderEnd > headerBegin &&
            numericHeaderEnd[-1] == '\r')
            --numericHeaderEnd;

        std::int64_t declaredCount = 0;
        if (!ParseInteger(
                headerBegin,
                numericHeaderEnd,
                declaredCount) ||
            declaredCount < 0 ||
            declaredCount >
                RageWorldEntities::kMaxEntries)
            return false;

        const int expectedCount =
            static_cast<int>(declaredCount);
        const char* cursor =
            *headerEnd ? headerEnd + 1 : headerEnd;
        if (expectedCount > 0 && !*headerEnd)
            return false;

        RageWorldEntities::Entry
            parsed[RageWorldEntities::kMaxEntries]{};
        for (int index = 0;
             index < expectedCount;
             ++index)
        {
            if (!*cursor)
                return false;
            const char* lineEnd = cursor;
            while (*lineEnd && *lineEnd != '\n')
                ++lineEnd;
            const char* contentEnd = lineEnd;
            if (contentEnd > cursor &&
                contentEnd[-1] == '\r')
                --contentEnd;

            const char* fieldBegin[kFieldCount]{};
            const char* fieldEnd[kFieldCount]{};
            if (!SplitFields(
                    cursor,
                    contentEnd,
                    fieldBegin,
                    fieldEnd) ||
                !BuildEntry(
                    fieldBegin,
                    fieldEnd,
                    parsed[index]))
                return false;
            cursor = *lineEnd ? lineEnd + 1 : lineEnd;
        }
        if (*cursor)
            return false;

        Publish(parsed, expectedCount, capturedAt);
        return true;
    }

    bool SafeGetStatus(
        RageV8Runtime::StatusSnapshot& status) noexcept
    {
        __try
        {
            status = RageV8Runtime::GetStatus();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            status = {};
            return false;
        }
    }

    bool SafePollResult(
        std::uint64_t commandId,
        RageV8Runtime::Result& result) noexcept
    {
        __try
        {
            return RageV8Runtime::PollResultFor(
                commandId,
                result);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result = {};
            return false;
        }
    }

    bool SafeEnqueue(
        const char* script,
        std::uint64_t* commandId) noexcept
    {
        __try
        {
            return RageV8Runtime::Enqueue(
                script,
                commandId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (commandId)
                *commandId = 0;
            return false;
        }
    }

    void UpdateImpl() noexcept
    {
        RageV8Runtime::StatusSnapshot status{};
        if (!SafeGetStatus(status))
            return;

        const ULONGLONG now = GetTickCount64();
        if (status.generation != g_RuntimeGeneration)
        {
            g_RuntimeGeneration = status.generation;
            ResetSessionState();
        }

        if (g_PendingCommand)
        {
            // Probe before building the 32 KB Result on the stack.
            if (RageV8Runtime::HasResultFor(g_PendingCommand))
            {
                RageV8Runtime::Result result{};
                if (SafePollResult(g_PendingCommand, result) &&
                    result.success)
                    ParseSnapshot(result.text, now);
                g_PendingCommand = 0;
            }
            else if (now - g_LastRequest >=
                         kRequestTimeoutMs ||
                     status.state !=
                         RageV8Runtime::State::Ready ||
                     !status.rootCaptured)
            {
                g_PendingCommand = 0;
            }
        }

        // Same reasoning as the player snapshot: never stack two heavy scripts
        // into neighbouring V8 pumps.
        if (g_PendingCommand ||
            status.queuedCommands != 0 ||
            status.state != RageV8Runtime::State::Ready ||
            !status.rootCaptured ||
            (GetAsyncKeyState(VK_F1) & 0x8000) != 0 ||
            now - g_LastRequest < kRefreshIntervalMs)
            return;

        g_LastRequest = now;
        if (!SafeEnqueue(
                kSnapshotScript,
                &g_PendingCommand))
            g_PendingCommand = 0;
    }
}

namespace RageWorldEntities
{
    void Update() noexcept
    {
        UpdateImpl();
    }

    void Clear() noexcept
    {
        g_PendingCommand = 0;
        g_LastRequest = 0;
        ClearPublishedSnapshot();
    }

    int CopySnapshot(
        Entry* output,
        int capacity,
        std::uint64_t* generation,
        std::uint64_t* capturedAtMs) noexcept
    {
        if (!output || capacity <= 0)
            return 0;

        Lock();
        int count = g_Count;
        if (count > capacity)
            count = capacity;
        for (int index = 0; index < count; ++index)
            output[index] = g_Entries[index];
        if (generation)
            *generation = g_Generation;
        if (capturedAtMs)
            *capturedAtMs = g_CapturedAtMs;
        Unlock();
        return count;
    }

    int GetSnapshot(Snapshot& output) noexcept
    {
        Lock();
        const int count = g_Count;
        output.generation = g_Generation;
        output.capturedAtMs = g_CapturedAtMs;
        output.count = count;
        for (int index = 0; index < count; ++index)
            output.entries[index] = g_Entries[index];
        Unlock();

        for (int index = count; index < kMaxEntries; ++index)
            output.entries[index] = {};
        return count;
    }

    int Count() noexcept
    {
        Lock();
        const int count = g_Count;
        Unlock();
        return count;
    }
}
