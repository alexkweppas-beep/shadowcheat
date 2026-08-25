#include "RagePlayerNames.h"

#include "Aimbot.h"
#include "FiveRpMetadataResolver.h"
#include "RageV8Runtime.h"
#include "RuntimeProfiler.h"
#include "ServerProfile.h"
#include "Visuals.h"
#include "../Menu/Menu.h"
#include "../SDK/Game.h"
#include "../SDK/RageBuildFlavor.h"

#include <Windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hooks
{
    void HLog(const char* message);
}

namespace
{
    struct NameEntry final
    {
        RagePlayerNames::EntityKind kind{
            RagePlayerNames::EntityKind::Player};
        bool local{};
        std::uint16_t remoteId{};
        std::uint32_t handle{};
        float x{};
        float y{};
        float z{};
        float heading{};
        int adminLevel{};
        bool visible{};
        int factionId{};
        int factionType{};
        int familyId{};
        int sqlId{};
        bool masked{};
        bool displayAdmin{};
        bool nametagDisabled{};
        bool dead{};
        bool knock{};
        float hp{-1.f};
        float hpSync{-1.f};
        float hpRaw{-1.f};
        int dyingFlag{-1};
        std::uint32_t boneMask{};
        RagePlayerNames::BonePosition bones[
            static_cast<int>(
                RagePlayerNames::BoneSlot::Count)]{};
        char name[48]{};
        char factionKey[48]{};
        char factionTag[24]{};
        char familyName[48]{};
        char nickname[48]{};
    };

    struct ObservedPlayer final
    {
        bool valid{};
        std::uint16_t remoteId{};
        ULONGLONG updatedAt{};
        char name[48]{};
        char factionTag[24]{};
    };

    struct ObservedFamily final
    {
        bool valid{};
        int familyId{};
        ULONGLONG updatedAt{};
        char name[48]{};
    };

    // Sized well clear of the current script rather than snugly around it: this
    // is a static buffer, the spare kilobytes cost nothing, and _snprintf_s
    // truncation here would ship a syntactically broken script to V8.
    char g_SnapshotScript[24576]{};

    // The script text only changes when one of these inputs changes, so the
    // format runs on a toggle instead of four times a second.
    struct SnapshotScriptKey final
    {
        bool built{};
        bool needsVisibility{};
        bool includeNonPlayers{};
        bool familyLookup{};
        // Which server's identity layout the built script is asking for. A
        // reconnect can land on a different one, and the script has to be
        // rebuilt when it does.
        int identity{-1};
        char adminKey[sizeof(Menu::s_AdminVariableKey) * 2]{};
    };

    SnapshotScriptKey g_ScriptKey{};

    volatile LONG g_Lock = 0;
    NameEntry g_Entries[256]{};
    int g_Count = 0;
    std::uint64_t g_Generation = 0;
    std::uint64_t g_RuntimeGeneration = 0;
    std::uint64_t g_PendingCommand = 0;
    ULONGLONG g_LastRequest = 0;
    bool g_HelperReady = false;
    int g_CanonicalNames = 0;
    int g_FallbackNames = 0;
    int g_StringFactions = 0;
    int g_FamilyCacheCount = 0;
    int g_FamilyPendingCount = 0;
    int g_FamilyRepliesSeen = 0;
    int g_FamilyRepliesEmpty = 0;
    bool g_FamilyCanary = false;
    bool g_FamilyRpcDisabled = false;
    ULONGLONG g_NameGoodAt[256]{};
    ObservedPlayer g_ObservedPlayers[256]{};
    ObservedFamily g_ObservedFamilies[128]{};
    int g_ObservedPlayerCount = 0;
    int g_ObservedFamilyCount = 0;

    void Lock() noexcept
    {
        while (InterlockedCompareExchange(&g_Lock, 1, 0) != 0)
            Sleep(0);
    }

    void Unlock() noexcept
    {
        InterlockedExchange(&g_Lock, 0);
    }

    void ResetSessionState() noexcept
    {
        g_ScriptKey = {};
        g_PendingCommand = 0;
        g_LastRequest = 0;
        Lock();
        g_HelperReady = false;
        g_CanonicalNames = 0;
        g_FallbackNames = 0;
        g_StringFactions = 0;
        g_FamilyCacheCount = 0;
        g_FamilyPendingCount = 0;
        g_FamilyRepliesSeen = 0;
        g_FamilyRepliesEmpty = 0;
        g_FamilyCanary = false;
        g_FamilyRpcDisabled = false;
        g_ObservedPlayerCount = 0;
        g_ObservedFamilyCount = 0;
        g_Count = 0;
        for (int index = 0; index < 256; ++index)
        {
            g_Entries[index] = {};
            g_NameGoodAt[index] = 0;
            g_ObservedPlayers[index] = {};
        }
        for (auto& family : g_ObservedFamilies)
            family = {};
        ++g_Generation;
        Unlock();
    }

    bool BuildSnapshotScript(
        bool needsVisibility,
        bool includeNonPlayers) noexcept
    {
        char adminKey[sizeof(Menu::s_AdminVariableKey) * 2]{};
        int write = 0;
        for (int read = 0;
             Menu::s_AdminVariableKey[read] &&
             read < static_cast<int>(
                 sizeof(Menu::s_AdminVariableKey)) - 1;
             ++read)
        {
            const char value = Menu::s_AdminVariableKey[read];
            const bool safe =
                (value >= 'a' && value <= 'z') ||
                (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') ||
                value == '_' || value == '-' ||
                value == '.' || value == '$';
            if (!safe ||
                write >= static_cast<int>(
                    sizeof(adminKey)) - 1)
                continue;
            adminKey[write++] = value;
        }
        adminKey[write] = 0;
        bool familyLookupDemand = false;
        for (int index = 0;
             index < static_cast<int>(
                 Menu::EspProfileId::Count);
             ++index)
        {
            const auto& profile =
                Menu::s_EspProfiles[index];
            familyLookupDemand |=
                profile.enabled &&
                profile.familyTag;
        }

        // Where names, factions and the admin flag live differs per server, and
        // asking the wrong place returns nothing rather than failing loudly, so
        // the layout is chosen here instead of being tried blind.
        const int identity =
            static_cast<int>(ServerProfile::IdentitySource());
        const bool families = ServerProfile::HasFamilies();
        const bool roleplayHealth = ServerProfile::RoleplayHealth();
        // Their own admin ESP reads these, so the same names are what we ask
        // for. GTA5RP is absent on purpose: it rotates the name and publishes
        // the current one in mp.serverDataKeys, which the script reads live.
        const char* adminVars = "";
        switch (ServerProfile::IdentitySource())
        {
        case ServerProfile::Identity::Quant:
            adminVars = "'admin','adminLevel'";
            break;
        case ServerProfile::Identity::Ukraine:
            adminVars = "'redNick','adminLevel','admin'";
            break;
        default:
            break;
        }

        if (g_ScriptKey.built &&
            g_ScriptKey.needsVisibility == needsVisibility &&
            g_ScriptKey.includeNonPlayers == includeNonPlayers &&
            g_ScriptKey.familyLookup == familyLookupDemand &&
            g_ScriptKey.identity == identity &&
            lstrcmpA(g_ScriptKey.adminKey, adminKey) == 0)
            return g_SnapshotScript[0] != 0;

        const int length = std::snprintf(
            g_SnapshotScript,
            sizeof(g_SnapshotScript),
            "const rows=[],seen={};"
            "const adminKey='%s';"
            "const adminVars=[%s];"
            "const ident=%d,famSrc=%d,rpHp=%d;"
            "const needVis=%d,includeNonPlayers=%d,familyLookup=%d;"
            "const root=(typeof globalThis!=='undefined'&&globalThis)||(typeof global!=='undefined'&&global)||(typeof window!=='undefined'&&window)||{};"
            "const ctx=root.__ctx__;"
            "const rage=(ctx&&ctx.mp)||(root.mp)||(typeof window!=='undefined'&&window.mp)||(typeof globalThis!=='undefined'&&globalThis.mp)||(typeof mp!=='undefined'?mp:null);"
            "if(!rage||!rage.players)return '';"
            // 5RP keeps mp/api as script-context bindings, so they are not
            // own properties of the global object. The native probe pins
            // api.players onto the global as __butterflyFiveRpPlayers; the
            // bare identifier is a cheap fallback for the same object.
            "let bareApi=null;"
            "try{bareApi=(typeof api!=='undefined'&&api)||null;}catch(_){}"
            "const apiRoot=root.__butterflyFiveRpApi||(ctx&&ctx.api)||(root.api)||bareApi||null;"
            "const helpers=root.__butterflyFiveRpPlayers||(apiRoot&&apiRoot.players)||null;"
            // 5RP never hardcodes the admin variable: the server publishes its
            // name in mp.serverDataKeys.isAdmin (currently "152") and rotates
            // it. The menu field stays as a manual override.
            // Several mp instances coexist after a reconnect and the one our
            // scripts run in carries an empty serverDataKeys, so the native
            // probe pins the live table as __butterflyFiveRpDataKeys.
            // Several names, tried in order: the rotating one this server
            // publishes, then the manual override, then whatever its own admin
            // display reads. A server that uses none of them yields nothing,
            // which is the honest answer.
            "const adminList=[];"
            "if(ident===1){try{"
            "const dk=root.__butterflyFiveRpDataKeys||rage.serverDataKeys;"
            "if(dk&&dk.isAdmin)adminList.push(String(dk.isAdmin).trim());"
            "}catch(_){}}"
            "if(adminKey)adminList.push(adminKey);"
            "for(const v of adminVars)"
            "if(v&&adminList.indexOf(v)<0)adminList.push(v);"
            "const adminHashes=adminList.map(v=>{try{"
            "const am=rage.game&&rage.game.misc;"
            "return (am&&typeof am.getVariableHashKey==='function')"
            "?am.getVariableHashKey(v):null;}catch(_){return null;}});"
            "const local=rage.players.local;"
            "const valid=v=>v&&Number.isFinite(Number(v.x))&&"
            "Number.isFinite(Number(v.y))&&"
            "Number.isFinite(Number(v.z))&&"
            "(Number(v.x)!==0||Number(v.y)!==0||Number(v.z)!==0);"
            "const cache=root.__butterflyNameCache||"
            "(root.__butterflyNameCache={chars:{},families:{},pendingFam:{}});"
            // The cache lives on the game's global object, so it outlives the
            // payload: after a rebuild the new code kept reading identities the
            // old code had computed, which is why a corrected faction rule
            // appeared to change nothing. Stamped with the schema it was filled
            // under, and dropped when that no longer matches.
            "if(cache.rev!==4){cache.rev=4;cache.ident={};}"
            "const cleanLabel=(v,id)=>{let s=String(v==null?'':v).trim();"
            "if(!s)return '';if(id!=null&&id>=0){const q=String(id);"
            "if(s.endsWith('('+q+')'))s=s.slice(0,-q.length-2).trim();"
            "else if(s.endsWith('['+q+']'))s=s.slice(0,-q.length-2).trim();"
            "else if(s.endsWith('_'+q))s=s.slice(0,-q.length-1).trim();"
            "else if(s.endsWith('#'+q))s=s.slice(0,-q.length-1).trim();"
            "else if(s.endsWith(' '+q))s=s.slice(0,-q.length-1).trim();}"
            "s=s.replace(/_/g,' ').replace(/\\s+/g,' ').trim();"
            "if(s==='Player'||s==='Unknown'||s==='Citizen'||s==='NPC'||s==='Animal'||s==='undefined'||s==='null')return '';return s;};"
            // Donate and achievement titles ride the same charName channel as
            // real names. Not one of the 2424 character names in the live 5RP
            // roster is Cyrillic, so a lone Cyrillic word is always a title.
            "const isTitle=s=>/^\\S+$/.test(s)&&/[\\u0400-\\u04FF]/.test(s);"
            // Bare identifiers such as #777 or ID:10 are what the server shows
            // in place of a name, and the overlay already carries both ids in
            // their own fields.
            "const isIdLabel=s=>"
            "/^(?:#|ID\\s*:\\s*)?\\d+(?:\\s*\\(\\d+\\))?$/i.test(s);"
            "const isRealPerson=s=>{if(!s)return false;"
            "if(/^(?:\\u0413\\u0440\\u0430\\u0436\\u0434\\u0430\\u043d\\u0438\\u043d|\\u0413\\u0440\\u0430\\u0436\\u0434\\u0430\\u043d\\u043a\\u0430|Player|Citizen|Unknown|NPC|Animal|\\u041d\\u0435\\u0437\\u043d\\u0430\\u043a\\u043e\\u043c\\u0435\\u0446)(?:\\s|\\(|#|$)/i.test(s))return false;"
            "if(isIdLabel(s))return false;"
            "if(isTitle(s))return false;"
            "return true;};"
            // Sessions that ran an older build carry titles in the cache;
            // drop them once so a live reconnect is not needed.
            "if(!cache.__sanitized){cache.__sanitized=1;"
            "for(const k in cache.chars){"
            "if(!isRealPerson(cleanLabel(cache.chars[k])))delete cache.chars[k];}}"
            "if(!root.__butterflyDataHook&&rage&&rage.events&&typeof rage.events.addDataHandler==='function'){"
            "root.__butterflyDataHook=true;try{"
            "const onData=(ent,val)=>{if(ent&&val&&typeof val==='string'){"
            "const s=cleanLabel(val,ent.remoteId);"
            "if(isRealPerson(s)){"
            "const rid=ent.remoteId;if(rid!=null&&rid>=0)cache.chars['r_'+rid]=s;"
            "if(ent.handle!=null&&ent.handle>0)cache.chars['h_'+ent.handle]=s;"
            "const sid=ent.getVariable?(ent.getVariable('sqlId')||ent.getVariable('characterId')):0;"
            "if(sid>0)cache.chars[String(sid)]=s;}}};"
            "rage.events.addDataHandler('charName',onData);"
            "rage.events.addDataHandler('characterName',onData);"
            "if(rage.serverDataKeys){for(const k in rage.serverDataKeys){"
            "const sk=rage.serverDataKeys[k];if(sk)rage.events.addDataHandler(sk,onData);}}"
            "}catch(_){}}"
            "if(helpers&&typeof helpers.setName==='function'&&!helpers.__bfSetHook){"
            "helpers.__bfSetHook=true;try{"
            "const origSet=helpers.setName;"
            "helpers.setName=function(p,n){"
            "try{if(p&&n&&typeof n==='string'){"
            "const s=cleanLabel(n,p.remoteId);"
            "if(isRealPerson(s)){"
            "if(p.remoteId!=null&&p.remoteId>=0)cache.chars['r_'+p.remoteId]=s;"
            "if(p.handle!=null&&p.handle>0)cache.chars['h_'+p.handle]=s;"
            "const sid=p.getVariable?(p.getVariable('sqlId')||p.getVariable('characterId')):0;"
            "if(sid>0)cache.chars[String(sid)]=s;}}}"
            "catch(_){}"
            "return origSet.apply(this,arguments);};}"
            "catch(_){}}"
            // Visibility is gone from here entirely. It used to be a render-tick
            // worker firing ten asynchronous raycasts a frame, plus a read of
            // its results on every snapshot, and it was the most expensive thing
            // this script did. The profiler put V8 execution at a 33 ms
            // ninety-ninth percentile against a 1.1 ms median, and the frame
            // tail matched it almost exactly: average frame rate barely moved,
            // but the game hitched several times a second, which is what "the
            // FPS is close but it feels awful" actually measures.
            //
            // C++ now answers the same question by calling the game's own shape
            // test manager from the render thread, so there is nothing for JS to
            // do. Two systems answering one question was worse than one either
            // way: they disagreed, and the disagreement was the flicker.
            //
            // If the C++ side ever fails to resolve its entry points, visibility
            // degrades to "everyone visible" and the telemetry says so with
            // probe=0. That is a loud, diagnosable failure, which is worth more
            // than a silent fallback that costs a hitch every frame.
            //
            // Any worker left installed by an earlier build is removed, once.
            // The image can be injected over a live session, so a previous
            // version's render handler would otherwise keep firing raycasts
            // forever with nothing reading them.
            "try{const ov=root.__butterflyVis;"
            "if(ov&&ov.fn&&rage.events&&"
            "typeof rage.events.remove==='function'){"
            "rage.events.remove('render',ov.fn);ov.fn=null;ov.worker=0;}}"
            "catch(_){}"
            // Emitted as visible for every row. The field is kept so the wire
            // format and the parser do not have to change, and so the C++
            // fallback keeps its "unknown reads as visible" convention if the
            // probe ever goes away.
            "const clear=()=>true;"
            // Expensive identity resolutions allowed this pass. See the cache
            // below: this is what keeps the snapshot's cost flat regardless of
            // how many players are on the server.
            "let resolveBudget=3;"
            "const emit=(e,kind,isLocal)=>{if(!e)return;"
            "const id=Number(e.remoteId),h=Number(e.handle);"
            "if(!Number.isFinite(h)||h<=0||seen[h])return;"
            "seen[h]=1;const p=e.position;"
            "if(!valid(p))return;"
            // hideAdmintag is deliberately ignored: the game uses it to drop the
            // badge, but the flag itself keeps syncing, so hidden admins stay
            // visible to us.
            "let admin=0;"
            "if(kind===0)for(let ai=0;ai<adminList.length&&!admin;++ai){try{"
            "let raw=adminHashes[ai]!=null"
            "?e.getVariable(adminHashes[ai]):undefined;"
            "if(raw===undefined||raw===null)raw=e.getVariable(adminList[ai]);"
            "if(raw===true)admin=1;"
            "else{const n=Number(raw);"
            "admin=Number.isFinite(n)&&n>0?n:(raw?1:0);}}catch(_){}}"
            "const read=(o,k)=>{try{"
            "if(!o)return null;"
            "const d=o[k];if(d!==undefined&&d!==null)return d;"
            "if(typeof o.getVariable!=='function')return null;let v=o.getVariable(k);"
            "if(v!==undefined&&v!==null)return v;try{"
            "const misc=rage.game&&rage.game.misc;"
            "if(misc&&typeof misc.getVariableHashKey==='function'){"
            "const hk=misc.getVariableHashKey(k);v=o.getVariable(hk);"
            "if(v!==undefined&&v!==null)return v;}}catch(_){}"
            "return null;}catch(_){return null;}};"
            "const first=(o,keys)=>{for(const k of keys){const v=read(o,k);"
            "if(v!==undefined&&v!==null&&String(v).trim())return v;}return null;};"
            "const label=v=>{if(v===undefined||v===null)return '';"
            "let s=String(v).replace(/_/g,' ').trim();if(!s||/^\\d+$/.test(s)||"
            "/^(?:fam(?:ily)?|family)\\s*#?\\d+$/i.test(s))return '';return s;};"
            "const hiddenName=v=>/^(?:\\u0413\\u0440\\u0430\\u0436\\u0434"
            "\\u0430\\u043d\\u0438\\u043d|\\u0413\\u0440\\u0430\\u0436"
            "\\u0434\\u0430\\u043d\\u043a\\u0430|Player|Citizen|Unknown|"
            "\\u041d\\u0435\\u0437\\u043d\\u0430\\u043a\\u043e\\u043c"
            "\\u0435\\u0446)(?:\\s|\\(|#|$)/i.test(String(v||''));"
            "const familyLabel=id=>{if(!(id>0))return '';let n=null;"
            "if(cache.families&&cache.families[String(id)])return cache.families[String(id)];"
            "const roots=[];try{if(rage.familyNames)roots.push(rage.familyNames);}catch(_){}"
            "try{if(rage.modules&&rage.modules.familyNames)roots.push(rage.modules.familyNames);}catch(_){}"
            "for(const r of roots){if(!r)continue;try{"
            "if(typeof r.getName==='function')n=r.getName(id);"
            "if(!n&&r.actions&&typeof r.actions.getName==='function')"
            "n=r.actions.getName(id);"
            "const maps=[r.namesMap,r.names,r.state&&r.state.namesMap];"
            "for(const m of maps)if(!n&&m&&m[id]!=null)n=m[id];"
            "}catch(_){}if(label(n)){cache.families[String(id)]=label(n);return label(n);}}return '';};"
            // Keys and types below are the live server set read out of the
            // api.players roster map: 14 populated keys plus Marabunta and
            // Vagos, which exist but currently have no members.
            "const isFactionKey=k=>/^F_[A-Z0-9_]+$/.test(k);"
            "const factionLabel=(key,type)=>{"
            // Membership is decided by the key alone. The type used to be
            // enough by itself, which is what put a faction on players who
            // belong to none: the server leaves a type behind on characters
            // with no faction, so a citizen came out tagged Gang or Police. A
            // missing tag is a far smaller lie than a wrong one, and the type
            // is still used below once a key has established membership.
            "if(!key||/^0+$/.test(String(key)))return '';"
            "const m={"
            "F_LSPD:'LSPD',F_LSSD:'LSSD',F_FIB:'FIB',"
            "F_LSARMY:'Army',F_LSCITYHALL:'Gov',F_EMS:'EMS',F_NEWS:'News',F_PRISON:'Prison',"
            "F_GANG_BALLAS:'Ballas',F_GANG_BLOODS:'Bloods',"
            "F_GANG_GROVE:'Grove',F_GANG_MARABUNTA:'Marabunta',"
            "F_GANG_VAGOS:'Vagos',F_RUSSIANMAFIA:'RM',"
            "F_ARMENIAMAFIA:'Armenian',F_MEXICOMAFIA:'Mexican'};"
            "if(key&&m[key])return m[key];"
            "const legacy={1:'Gov',2:'LSPD',3:'BCSD',4:'FIB',"
            "5:'EMS',6:'Army',7:'News'};"
            "if(/^\\d+$/.test(key)&&legacy[key])return legacy[key];"
            // A faction the server added after this table was written still
            // gets a readable tag instead of the raw key.
            "if(isFactionKey(key)){"
            "const d=key.replace(/^F_(?:GANG_|MAFIA_)?/,'').toLowerCase();"
            "if(d)return d.charAt(0).toUpperCase()+d.slice(1);}"
            "const t={1:'Police',2:'Army',3:'Gang',4:'Mafia',5:'News',"
            "6:'Gov',7:'EMS',8:'FIB',9:'Prison'};"
            "return t[type]||'';};"
            "let factionId=0;let factionType=0;let factionKey='';"
            "let factionTag='';"
            "let familyId=0;let familyName='';let heading=0;"
            "let sqlId=0;let nickname='';realName='';let displayName='';let dead=0;let knock=0;"
            "let hp=NaN;let hpSync=NaN;let hpRaw=NaN;let dying=-1;"
            // Identity is resolved once per player and then reused.
            //
            // Everything in this block is a property of the character, not of
            // the moment: name, faction, family, database id. None of it changes
            // while a player is connected, and resolving it cost the snapshot
            // most of its runtime. Each `read` is up to three trips across the
            // bridge into the engine, `first` runs one per key until something
            // answers, and below that there is a walk over every member of the
            // helpers object plus one over every server data key. Tens of calls
            // per player, a hundred players, four times a second: that is where
            // the ninety-ninth percentile frame went.
            //
            // Cached entries are refreshed on a long timer, because a player can
            // change faction or name mid-session and a cache that never expired
            // would show the old one forever.
            //
            // The refresh is rationed rather than scheduled: a handful of
            // players are re-resolved per snapshot, oldest effectively first.
            // Expiring everyone on the same tick would simply move the spike
            // instead of removing it, since the whole roster would then resolve
            // together once per interval. At this budget a hundred-player
            // roster cycles through in roughly ten seconds, and a player whose
            // name has not arrived at all is retried far sooner than one that
            // merely might have changed.
            "if(kind===0){"
            "if(!cache.ident)cache.ident={};"
            // Keyed on the remote id but validated against the handle too: the
            // server hands remote ids out again after a player leaves, and a
            // reused one would otherwise inherit the previous occupant's name.
            "const ik='i'+id;const ic=cache.ident[ik];const tn=Date.now();"
            "const need=!ic||ic.hh!==h||"
            "(ic.n?tn-ic.at>=20000:tn-ic.at>=4000);"
            "if((!need||resolveBudget<=0)&&ic){"
            "factionId=ic.fi;factionType=ic.ft;factionKey=ic.fk;"
            "factionTag=ic.fg;familyId=ic.mi;familyName=ic.mn;"
            "sqlId=ic.s;realName=ic.n;displayName=ic.d;nickname=ic.n;"
            // Family names arrive later over an asynchronous call, so a cached
            // entry that had none is topped up from the local map. That lookup
            // touches no engine bridge and costs nothing.
            "if(!familyName&&familyId>0){const fl=familyLabel(familyId);"
            "if(fl){familyName=fl;ic.mn=fl;}}"
            "}else if(resolveBudget<=0){"
            // Nothing cached and no budget left this pass: the row goes out with
            // placeholders and is filled in on a following snapshot. One frame
            // of "Гражданин" for a player who just appeared beats dragging the
            // whole roster's resolution into this one.
            "}else{--resolveBudget;try{"
            // getServerFactionId is the roster lookup and therefore the only
            // authoritative source; entity variables are just a fallback.
            // groupId and clubs are deliberately absent: they carry biker
            // clubs such as REDNECKS, which are not factions.
            "if(ident===1){"
            "let fr=null;"
            "if(helpers&&typeof helpers.getServerFactionId==='function'){"
            "try{fr=helpers.getServerFactionId.call(helpers,e);}catch(_){}}"
            "if(fr==null||!String(fr).trim())"
            "fr=first(e,['serverFactionId','factionKey','factionId','faction']);"
            "factionKey=String(fr==null?'':fr).trim();"
            "if(!isFactionKey(factionKey)&&!/^\\d+$/.test(factionKey))factionKey='';"
            // Zero is how "no faction" arrives on the numeric channel, and it
            // passes the digit test above, so it read as a member of faction
            // zero and let the tag lookups below run on a non-member.
            "if(/^0+$/.test(factionKey))factionKey='';"
            "if(/^\\d+$/.test(factionKey))factionId=Number(factionKey);"
            "const fty=Number(first(e,['serverFactionType','factionType']));"
            "if(Number.isFinite(fty)&&fty>0)factionType=fty;"
            "else if(helpers&&typeof helpers.getServerFactionType==='function'){"
            "try{factionType=Number(helpers.getServerFactionType.call(helpers,e))||0;}catch(_){}}"
            // Only trust the entity's own tag once the key proves this really
            // is a faction, otherwise club names leak in through factionName.
            "if(factionKey)"
            "factionTag=label(first(e,['factionTag','factionName','factionTitle']));"
            "if(!factionTag)factionTag=factionLabel(factionKey,factionType);"
            "}else if(ident>1){"
            // These servers publish the tag itself and have no roster to ask,
            // so the tag is the whole answer: there is no key to validate it
            // against, and inventing one would only throw the tag away.
            "const ft=label(first(e,['factionTag','factionName','faction']));"
            "if(ft){factionTag=ft;factionKey=ft;}"
            "}"
            // Looked up through the roster as well as on the entity in hand.
            // Several mp instances coexist after a reconnect, and the one this
            // script runs in can carry entities whose variables are empty;
            // factions survive that because they come from a roster helper,
            // while the family is a plain variable and came back blank every
            // time. The server's own code reads it off atRemoteId, so that is
            // the path followed here too.
            // Families are a GTA5RP concept; the other servers have no
            // equivalent, so the lookup is not attempted there at all rather
            // than reading variables that will never exist.
            "if(famSrc){"
            "let famRaw=first(e,['familyId','famId']);"
            "if(famRaw==null&&rage.players&&"
            "typeof rage.players.atRemoteId==='function'){"
            "try{const re=rage.players.atRemoteId(id);"
            "if(re&&re!==e)famRaw=first(re,['familyId','famId']);}catch(_){}}"
            "familyId=Number(famRaw);"
            "if(!Number.isFinite(familyId)||familyId<0)familyId=0;"
            "familyName=label(first(e,['familyName','__familyName','familyTitle','yachtFamilyName']));"
            "if(!familyName&&familyId>0)familyName=familyLabel(familyId);"
            // No longer gated on the family tag being switched on. The lookup
            // is one request per family for the whole session, cached after
            // that, so making it conditional bought nothing and meant families
            // stayed blank for anyone who had the tag off when a player was
            // first seen and turned it on later.
            "if(!familyName&&familyId>0){"
            // The id, because the name is not obtainable here.
            //
            // Only the id is synced to clients; the game compares ids to colour
            // friend and foe in family wars and never needs a name for anyone
            // but you. The name lives behind a server call whose reply never
            // reaches this context: measured, with requests going out, the
            // listener attached, and not one event of any kind arriving. So the
            // request is not sent at all rather than pushing traffic at the
            // server for an answer that cannot be collected, and the tag shows
            // what is actually known.
            "familyName='#'+familyId;}"
            "}"
            "const sid=first(e,['sqlId','characterId']);"
            "sqlId=Number(sid);"
            "if(!Number.isFinite(sqlId)||sqlId<0)sqlId=0;"
            "const cands=[];"
            // The roster walk and the two page-scoped globals below belong to
            // GTA5RP's own client. Elsewhere they are absent, and walking a
            // helpers object that is not there cost a bridge trip per key for
            // nothing.
            "if(ident===1){"
            "try{if(helpers&&typeof helpers.getName==='function')cands.push(helpers.getName.call(helpers,e));}catch(_){}"
            "try{if(helpers&&typeof helpers.getRpPlayerName==='function')cands.push(helpers.getRpPlayerName.call(helpers,e));}catch(_){}"
            "try{if(helpers){for(const k in helpers){const m=helpers[k];if(m&&typeof m==='object'){"
            "try{if(m[id])cands.push(m[id]);if(m[h])cands.push(m[h]);if(sqlId>0&&m[sqlId])cands.push(m[sqlId]);"
            "if(typeof m.get==='function'){cands.push(m.get(e));cands.push(m.get(id));if(sqlId>0)cands.push(m.get(sqlId));}}"
            "catch(_){}}}}}catch(_){}"
            "}"
            "if(cache.chars['r_'+id])cands.push(cache.chars['r_'+id]);"
            "if(cache.chars['h_'+h])cands.push(cache.chars['h_'+h]);"
            "if(sqlId>0&&cache.chars[String(sqlId)])cands.push(cache.chars[String(sqlId)]);"
            "if(ident===1&&rage.serverDataKeys){for(const k in rage.serverDataKeys){"
            "const sk=rage.serverDataKeys[k];if(sk){try{cands.push(e.getVariable(sk));}catch(_){}}}}"
            "if(ident===1){"
            "try{if(root.playerFriendsReplaceString)cands.push(root.playerFriendsReplaceString(id));}catch(_){}"
            "try{if(root.getEntityVariable)cands.push(root.getEntityVariable(e,'charName',''));}catch(_){}"
            "}"
            "cands.push(read(e,'charName'));cands.push(read(e,'__displayCharName'));"
            "cands.push(read(e,'characterName'));cands.push(read(e,'realName'));"
            "cands.push(read(e,'playerName'));cands.push(read(e,'Nickname'));"
            "cands.push(read(e,'nickname'));cands.push(read(e,'__displayName'));"
            "cands.push(e.name);"
            "realName='';"
            "for(const c of cands){const cl=cleanLabel(c,id);if(isRealPerson(cl)){realName=cl;break;}}"
            "if(realName){if(sqlId>0)cache.chars[String(sqlId)]=realName;if(id>=0)cache.chars['r_'+id]=realName;if(h>0)cache.chars['h_'+h]=realName;}"
            "if(!realName&&sqlId>0&&cache.chars[String(sqlId)])realName=cache.chars[String(sqlId)];"
            "if(!realName&&id>=0&&cache.chars['r_'+id])realName=cache.chars['r_'+id];"
            "if(!realName&&h>0&&cache.chars['h_'+h])realName=cache.chars['h_'+h];"
            "displayName=realName;"
            "if(!displayName){for(const c of cands){const cl=cleanLabel(c,id);if(cl&&!isTitle(cl)&&!isIdLabel(cl)&&cl!=='Player'&&cl!=='Unknown'&&cl!=='Citizen'){displayName=cl;break;}}}"
            // The roleplay servers call an unresolved character a citizen,
            // which is what their own nametag shows. Anywhere else that word
            // would be an invention, so the id stands in for the name.
            "if(!displayName)displayName=ident"
            "?'\\u0413\\u0440\\u0430\\u0436\\u0434\\u0430\\u043d\\u0438\\u043d'"
            ":(id>=0?('#'+id):'');"
            "nickname=realName;"
            "}catch(_){}"
            "cache.ident[ik]={n:realName,d:displayName,fi:factionId,"
            "ft:factionType,fk:factionKey,fg:factionTag,mi:familyId,"
            "mn:familyName,s:sqlId,at:tn,hh:h};"
            "}}"
            // getHealth() used to be called here as well. It is a native call
            // across the bridge, once per player per snapshot, for a number C++
            // already reads straight out of the ped every frame: the ESP takes
            // the memory value as primary and only falls back to this snapshot
            // when that read is unusable. Paying for it here bought nothing.
            //
            // health is what RAGE syncs, and it stays in roleplay hit points, so
            // it is not derivable from memory and does stay. It is a property
            // read rather than a native call, which is the cheaper of the two.
            "try{const s=Number(e.health);"
            "if(Number.isFinite(s)&&s>=0){hpSync=s;hp=s;}}catch(_){}"
            "try{if(typeof e.isDeadOrDying==='function')"
            "dying=e.isDeadOrDying(true)?1:0;"
            "else if(typeof e.isDead==='function')dying=e.isDead()?1:0;}"
            "catch(_){}"
            // An empty bar is a hundred, not zero. GTA keeps ped health as
            // 100 + value and this server maps its own 0..100 hit points onto
            // exactly that range, which its client code states outright by
            // writing setHealth(100 + stored). Compared against zero this
            // never once fired, so nobody ever read as down: the whole death
            // and knock display was dead code against a threshold no live
            // player can reach.
            //
            // Reported in roleplay points, so a reader sees the same number
            // the server does. A server that does not do this mapping keeps its
            // health as the game reports it: subtracting a hundred there read
            // every healthy player as dead.
            "if(Number.isFinite(hp)){"
            "if(rpHp){hp=hp-100;if(hp<0)hp=0;}"
            "if(hp<=0)dead=1;}"
            "else if(dying===1)dead=1;"
            // Fatally injured with hit points left is the bleeding-out window,
            // which the overlay draws apart from a finished death.
            "if(dying===1&&!dead)knock=1;"
            "try{if(typeof e.getHeading==='function')heading=Number(e.getHeading());"
            "else if(typeof e.getRotation==='function'){const r=e.getRotation(2);"
            "heading=Number(r&&r.z);}"
            "if(!Number.isFinite(heading))heading=0;}catch(_){heading=0;}"
            "const head={x:Number(p.x),y:Number(p.y),z:Number(p.z)+0.8};"
            "const visible=clear(e,head);"
            "let name=kind===0?(realName||displayName):"
            "kind===2?String(e.name||'Animal'):String(e.name||'NPC');"
            "const safe=v=>String(v==null?'':v).replace(/[\\t\\r\\n]/g,' ');"
            // -1 keeps an unreadable value apart from a genuine zero.
            "const hpOut=v=>Number.isFinite(v)?Math.round(v*10)/10:-1;"
            "const row=[kind,isLocal?1:0,"
            "Number.isFinite(id)?id:-1,h,"
            "Number(p.x),Number(p.y),Number(p.z),admin,"
            "visible?1:0,factionId,"
            "safe(name),safe(factionTag),safe(familyName),"
            "sqlId,safe(nickname),dead,knock,familyId,factionType,"
            "safe(factionKey),heading,hpOut(hp),hpOut(hpSync),"
            "hpOut(hpRaw),dying];"
            "rows.push(row.join('\\t'));};"
            "emit(local,0,true);"
            // emit() discards anything without a handle, which is every player
            // the server is not streaming to us: on a full server that is two
            // thousand entries walked and thrown away, several times a second,
            // on the thread that draws the frame. The pool can hand over just
            // the streamed ones, so the whole roster is never touched.
            "if(rage.players){"
            "if(typeof rage.players.forEachInStreamRange==='function')"
            "rage.players.forEachInStreamRange(p=>emit(p,0,p===local));"
            "else if(rage.players.forEach)"
            "rage.players.forEach(p=>emit(p,0,p===local));"
            "}"
            "if(includeNonPlayers&&rage.peds&&rage.peds.forEach)"
            "rage.peds.forEach(p=>{let k=1;try{if(rage.game&&rage.game.ped&&rage.game.ped.isPedHuman){if(!rage.game.ped.isPedHuman(p.handle))k=2;}else if((rage.game&&rage.game.invoke&&rage.game.invoke('0xFF059E1E4C01E63C',p.handle)>>>0)===28)k=2;}catch(_){}emit(p,k,false);});"
            // The identity line the reader has always parsed and the script has
            // never sent. Every family number in the log was therefore a
            // constant zero, which is not "nothing is happening" but "nothing
            // is being measured", and several rounds of fixes were aimed at a
            // dead gauge before anyone checked the gauge itself.
            "let famCache=0,famPending=0;"
            "try{for(const k in cache.families)if(cache.families[k])++famCache;}catch(_){}"
            "try{for(const k in cache.pendingFam)++famPending;}catch(_){}"
            "rows.push(['@identity',helpers?1:0,0,0,0,famCache,famPending,"
            "0,0,0,0].join('\\t'));"
            "return rows.join('\\n');",
            adminKey,
            adminVars,
            identity,
            families ? 1 : 0,
            roleplayHealth ? 1 : 0,
            needsVisibility ? 1 : 0,
            includeNonPlayers ? 1 : 0,
            familyLookupDemand ? 1 : 0);
        const bool ok = length > 0 &&
            length < static_cast<int>(
                sizeof(g_SnapshotScript));
        if (ok)
        {
            g_ScriptKey.built = true;
            g_ScriptKey.needsVisibility = needsVisibility;
            g_ScriptKey.includeNonPlayers = includeNonPlayers;
            g_ScriptKey.familyLookup = familyLookupDemand;
            g_ScriptKey.identity = identity;
            lstrcpynA(
                g_ScriptKey.adminKey,
                adminKey,
                static_cast<int>(
                    sizeof(g_ScriptKey.adminKey)));
        }
        else
        {
            g_ScriptKey = {};
        }
        return ok;
    }

    bool ParseRemoteId(
        const char* begin,
        const char* end,
        std::uint16_t& remoteId) noexcept
    {
        if (!begin || !end || begin >= end)
            return false;
        bool negative = false;
        if (*begin == '-')
        {
            negative = true;
            ++begin;
        }
        std::uint32_t value = 0;
        int digits = 0;
        while (begin < end)
        {
            if (*begin < '0' || *begin > '9')
                return false;
            value = value * 10 +
                static_cast<std::uint32_t>(*begin - '0');
            if (value > 0xFFFF)
                return false;
            ++digits;
            ++begin;
        }
        if (!digits)
            return false;
        remoteId = negative
            ? static_cast<std::uint16_t>(0xFFFF)
            : static_cast<std::uint16_t>(value);
        return true;
    }

    bool ParseUnsigned(
        const char* begin,
        const char* end,
        std::uint32_t& value) noexcept
    {
        value = 0;
        if (!begin || !end || begin >= end)
            return false;
        int digits = 0;
        while (begin < end)
        {
            if (*begin < '0' || *begin > '9')
                return false;
            value = value * 10 +
                static_cast<std::uint32_t>(*begin - '0');
            ++digits;
            ++begin;
        }
        return digits != 0;
    }

    bool ParseFloat(
        const char* begin,
        const char* end,
        float& value) noexcept
    {
        value = 0.0f;
        if (!begin || !end || begin >= end)
            return false;
        char buffer[64]{};
        int length = static_cast<int>(end - begin);
        if (length >= static_cast<int>(sizeof(buffer)))
            return false;
        for (int index = 0; index < length; ++index)
            buffer[index] = begin[index];
        char* parseEnd = nullptr;
        value = std::strtof(buffer, &parseEnd);
        return parseEnd && parseEnd != buffer &&
            *parseEnd == 0;
    }

    bool IsUsefulPlayerName(const char* value) noexcept
    {
        if (!value || !value[0])
            return false;
        const unsigned char* cursor =
            reinterpret_cast<const unsigned char*>(value);
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;
        if (*cursor == '#')
            ++cursor;
        else if ((cursor[0] == 'I' || cursor[0] == 'i') &&
                 (cursor[1] == 'D' || cursor[1] == 'd'))
        {
            cursor += 2;
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            if (*cursor == ':')
                ++cursor;
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
        }
        const unsigned char* numeric = cursor;
        while (*cursor >= '0' && *cursor <= '9')
            ++cursor;
        if (cursor != numeric)
        {
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            if (*cursor == '(')
            {
                ++cursor;
                const unsigned char* suffix = cursor;
                while (*cursor >= '0' && *cursor <= '9')
                    ++cursor;
                if (cursor != suffix && *cursor == ')')
                    ++cursor;
                else
                    cursor = suffix;
            }
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            if (!*cursor)
                return false;
        }
        if (lstrcmpiA(value, "Player") == 0 ||
            lstrcmpiA(value, "Citizen") == 0 ||
            lstrcmpiA(value, "Unknown") == 0 ||
            _strnicmp(value, "Player", 6) == 0 ||
            _strnicmp(value, "Citizen", 7) == 0 ||
            _strnicmp(value, "Unknown", 7) == 0 ||
            strncmp(value, u8"Гражданин", 18) == 0 ||
            strncmp(value, u8"Гражданка", 18) == 0 ||
            strncmp(value, u8"Незнакомец", 20) == 0)
            return false;
        return _strnicmp(value, "Player #", 8) != 0;
    }

    bool IsStringFaction(
        const char* key,
        const char* tag) noexcept
    {
        auto useful = [](const char* value) -> bool {
            if (!value || !value[0])
                return false;
            for (const char* cursor = value; *cursor; ++cursor)
            {
                if (*cursor < '0' || *cursor > '9')
                    return true;
            }
            return false;
        };
        return useful(key) || useful(tag);
    }

    bool IsUsefulFamilyName(const char* value) noexcept
    {
        if (!value || !value[0])
            return false;
        bool digitsOnly = true;
        for (const unsigned char* cursor =
                 reinterpret_cast<
                     const unsigned char*>(value);
             *cursor;
             ++cursor)
        {
            if (*cursor < '0' || *cursor > '9')
            {
                digitsOnly = false;
                break;
            }
        }
        if (digitsOnly)
            return false;
        const char* suffix = nullptr;
        if (_strnicmp(value, "Family", 6) == 0)
            suffix = value + 6;
        else if (_strnicmp(value, "Fam", 3) == 0)
            suffix = value + 3;
        if (!suffix)
            return true;
        while (*suffix == ' ' || *suffix == '#')
            ++suffix;
        return *suffix < '0' || *suffix > '9';
    }

#if RAGEMP_DEV_BUILD
    // A death lasts seconds and is easy to miss on screen, so every fresh
    // sighting records the readings behind it. Players who stay down are not
    // repeated, and one who gets up is armed again for the next time.
    void LogDeathSightings(
        const NameEntry* entries,
        int count) noexcept
    {
        constexpr int kTracked = 64;
        static std::uint16_t reported[kTracked]{};
        static int reportedCount = 0;
        std::uint16_t active[kTracked]{};
        int activeCount = 0;
        for (int index = 0; index < count; ++index)
        {
            const NameEntry& entry = entries[index];
            // Anyone the state of whose health is worth a line, not only those
            // already classified: if the classification is the thing that is
            // wrong, reporting only its hits shows nothing at all. Roleplay
            // hit points run 0..100, so anything under full is an injury.
            const bool interesting =
                entry.dead ||
                entry.knock ||
                entry.dyingFlag == 1 ||
                (entry.hp >= 0.f && entry.hp < 100.f);
            if (entry.kind !=
                    RagePlayerNames::EntityKind::Player ||
                !interesting)
                continue;
            if (activeCount < kTracked)
                active[activeCount++] = entry.remoteId;
            bool known = false;
            for (int seen = 0;
                 seen < reportedCount && !known;
                 ++seen)
                known = reported[seen] == entry.remoteId;
            if (known)
                continue;
            char line[192]{};
            _snprintf(
                line,
                sizeof(line) - 1,
                "[DEATH] id=%u %s name=%s hp=%.0f raw=%.0f "
                "sync=%.0f dying=%d",
                static_cast<unsigned>(entry.remoteId),
                entry.dead ? "dead" : "knock",
                entry.name[0] ? entry.name : "?",
                entry.hp,
                entry.hpRaw,
                entry.hpSync,
                entry.dyingFlag);
            hooks::HLog(line);
        }
        reportedCount = 0;
        for (int index = 0; index < activeCount; ++index)
            reported[reportedCount++] = active[index];
    }
#endif

    void ParseSnapshot(const char* text) noexcept
    {
        NameEntry parsed[256]{};
        int parsedCount = 0;
        const char* cursor = text ? text : "";
        int helperReady = 0;
        int canonicalNames = 0;
        int fallbackNames = 0;
        int stringFactions = 0;
        int familyCacheCount = 0;
        int familyPendingCount = 0;
        int familyRepliesSeen = 0;
        int familyRepliesEmpty = 0;
        int familyCanary = 0;
        int familyRpcDisabled = 0;
            constexpr int kFieldCount = 25;
            while (*cursor && parsedCount < 256)
            {
                const char* lineEnd = cursor;
                while (*lineEnd && *lineEnd != '\n')
                    ++lineEnd;
                if (strncmp(
                        cursor,
                        "@identity\t",
                        10) == 0)
                {
                    std::sscanf(
                        cursor,
                        "@identity\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
                        &helperReady,
                        &canonicalNames,
                        &fallbackNames,
                        &stringFactions,
                        &familyCacheCount,
                        &familyPendingCount,
                        &familyCanary,
                        &familyRpcDisabled,
                        &familyRepliesSeen,
                        &familyRepliesEmpty);
                    cursor = *lineEnd
                        ? lineEnd + 1 : lineEnd;
                    continue;
                }

                const char* fieldBegin[kFieldCount]{};
                const char* fieldEnd[kFieldCount]{};
                const char* field = cursor;
                bool fieldsValid = true;
                for (int index = 0;
                     index < kFieldCount - 1;
                     ++index)
                {
                    fieldBegin[index] = field;
                    while (field < lineEnd && *field != '\t')
                        ++field;
                    if (field >= lineEnd)
                    {
                        fieldsValid = false;
                        break;
                    }
                    fieldEnd[index] = field;
                    ++field;
                }
                fieldBegin[kFieldCount - 1] = field;
                fieldEnd[kFieldCount - 1] = lineEnd;

                std::uint32_t kind = 0;
                std::uint32_t local = 0;
                std::uint16_t remoteId = 0;
                std::uint32_t handle = 0;
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                float adminLevel = 0.0f;
                std::uint32_t visible = 0;
                float factionId = 0.0f;
                if (fieldsValid &&
                    ParseUnsigned(
                        fieldBegin[0],
                        fieldEnd[0],
                        kind) &&
                    kind <= 2 &&
                    ParseUnsigned(
                        fieldBegin[1],
                        fieldEnd[1],
                        local) &&
                    local <= 1 &&
                    ParseRemoteId(
                        fieldBegin[2],
                        fieldEnd[2],
                        remoteId) &&
                    ParseUnsigned(
                        fieldBegin[3],
                        fieldEnd[3],
                        handle) &&
                    handle != 0 &&
                    ParseFloat(fieldBegin[4], fieldEnd[4], x) &&
                    ParseFloat(fieldBegin[5], fieldEnd[5], y) &&
                    ParseFloat(fieldBegin[6], fieldEnd[6], z) &&
                    std::isfinite(x) &&
                    std::isfinite(y) &&
                    std::isfinite(z) &&
                    ParseFloat(
                        fieldBegin[7],
                        fieldEnd[7],
                        adminLevel) &&
                    std::isfinite(adminLevel) &&
                    ParseUnsigned(
                        fieldBegin[8],
                        fieldEnd[8],
                        visible) &&
                    ParseFloat(
                        fieldBegin[9],
                        fieldEnd[9],
                        factionId) &&
                    std::isfinite(factionId))
                {
                    NameEntry& entry = parsed[parsedCount++];
                    entry.kind = static_cast<
                        RagePlayerNames::EntityKind>(kind);
                    entry.local = local != 0;
                    entry.remoteId = remoteId;
                    entry.handle = handle;
                    entry.x = x;
                    entry.y = y;
                    entry.z = z;
                    entry.adminLevel =
                        static_cast<int>(adminLevel);
                    entry.visible = visible != 0;
                    entry.factionId =
                        static_cast<int>(factionId);
                    if (entry.factionId < 0)
                        entry.factionId = 0;

                    auto copyField =
                        [](char* dest,
                           int destSize,
                           const char* begin,
                           const char* end) {
                            int length =
                                static_cast<int>(end - begin);
                            if (length > 0 &&
                                begin[length - 1] == '\r')
                                --length;
                            if (length > destSize - 1)
                                length = destSize - 1;
                            for (int i = 0; i < length; ++i)
                                dest[i] = begin[i];
                            dest[length] = 0;
                        };
                    copyField(
                        entry.name,
                        static_cast<int>(sizeof(entry.name)),
                        fieldBegin[10],
                        fieldEnd[10]);
                    if (!entry.name[0] && kind != 0)
                        lstrcpynA(
                            entry.name,
                            kind == 2 ? "Animal" : "NPC",
                            static_cast<int>(
                                sizeof(entry.name)));
                    copyField(
                        entry.factionTag,
                        static_cast<int>(
                            sizeof(entry.factionTag)),
                        fieldBegin[11],
                        fieldEnd[11]);
                    copyField(
                        entry.familyName,
                        static_cast<int>(
                            sizeof(entry.familyName)),
                        fieldBegin[12],
                        fieldEnd[12]);

                    float sqlIdF = 0.f;
                    std::uint32_t dead = 0;
                    std::uint32_t knock = 0;
                    float familyIdF = 0.f;
                    float factionTypeF = 0.f;
                    if (ParseFloat(
                            fieldBegin[13],
                            fieldEnd[13],
                            sqlIdF) &&
                        std::isfinite(sqlIdF))
                        entry.sqlId = static_cast<int>(sqlIdF);
                    copyField(
                        entry.nickname,
                        static_cast<int>(
                            sizeof(entry.nickname)),
                        fieldBegin[14],
                        fieldEnd[14]);
                    if (ParseUnsigned(
                            fieldBegin[15],
                            fieldEnd[15],
                            dead))
                        entry.dead = dead != 0;
                    if (ParseUnsigned(
                            fieldBegin[16],
                            fieldEnd[16],
                            knock))
                        entry.knock = knock != 0;
                    if (ParseFloat(
                            fieldBegin[17],
                            fieldEnd[17],
                            familyIdF) &&
                        std::isfinite(familyIdF) &&
                        familyIdF > 0.f)
                        entry.familyId =
                            static_cast<int>(familyIdF);
                    if (ParseFloat(
                            fieldBegin[18],
                            fieldEnd[18],
                            factionTypeF) &&
                        std::isfinite(factionTypeF) &&
                        factionTypeF > 0.f)
                        entry.factionType =
                            static_cast<int>(factionTypeF);
                    copyField(
                        entry.factionKey,
                        static_cast<int>(
                            sizeof(entry.factionKey)),
                        fieldBegin[19],
                        fieldEnd[19]);
                    float headingF = 0.f;
                    if (ParseFloat(
                            fieldBegin[20],
                            fieldEnd[20],
                            headingF) &&
                        std::isfinite(headingF))
                        entry.heading = headingF;
                    float hpF = -1.f;
                    if (ParseFloat(
                            fieldBegin[21],
                            fieldEnd[21],
                            hpF) &&
                        std::isfinite(hpF))
                        entry.hp = hpF;
                    float hpSyncF = -1.f;
                    if (ParseFloat(
                            fieldBegin[22],
                            fieldEnd[22],
                            hpSyncF) &&
                        std::isfinite(hpSyncF))
                        entry.hpSync = hpSyncF;
                    float hpRawF = -1.f;
                    if (ParseFloat(
                            fieldBegin[23],
                            fieldEnd[23],
                            hpRawF) &&
                        std::isfinite(hpRawF))
                        entry.hpRaw = hpRawF;
                    float dyingF = -1.f;
                    if (ParseFloat(
                            fieldBegin[24],
                            fieldEnd[24],
                            dyingF) &&
                        std::isfinite(dyingF))
                        entry.dyingFlag = static_cast<int>(dyingF);
                    if (entry.factionId <= 0 &&
                        !entry.factionKey[0])
                        entry.factionTag[0] = 0;

                    if (!IsUsefulPlayerName(entry.nickname))
                        entry.nickname[0] = 0;
                    if (!IsUsefulPlayerName(entry.name))
                    {
                        if (entry.kind != RagePlayerNames::EntityKind::Player)
                        {
                            lstrcpynA(
                                entry.name,
                                entry.kind ==
                                        RagePlayerNames::EntityKind::Animal
                                    ? "Animal" : "NPC",
                                static_cast<int>(
                                    sizeof(entry.name)));
                        }
                        else
                        {
                            entry.name[0] = 0;
                        }
                    }

                    if (entry.kind ==
                        RagePlayerNames::EntityKind::Player)
                    {
                        FiveRpMetadataResolver::Metadata
                            metadata{};
                        if (FiveRpMetadataResolver::TryGet(
                                entry.remoteId,
                                entry.handle,
                                metadata))
                        {
                            if (metadata.factionId > 0)
                                entry.factionId =
                                    metadata.factionId;
                            if (metadata.factionType > 0)
                                entry.factionType =
                                    metadata.factionType;
                            if (metadata.factionKey[0])
                                lstrcpynA(
                                    entry.factionKey,
                                    metadata.factionKey,
                                    static_cast<int>(
                                        sizeof(entry.factionKey)));
                            if (metadata.factionTag[0])
                                lstrcpynA(
                                    entry.factionTag,
                                    metadata.factionTag,
                                    static_cast<int>(
                                        sizeof(entry.factionTag)));
                            if (metadata.familyId > 0)
                                entry.familyId =
                                    metadata.familyId;
                            entry.masked =
                                metadata.masked;
                            entry.displayAdmin =
                                metadata.displayAdmin;
                            entry.nametagDisabled =
                                metadata.nametagDisabled;
                            if (metadata.characterId > 0)
                                entry.sqlId =
                                    metadata.characterId;
                            if (IsUsefulPlayerName(
                                    metadata.name))
                                lstrcpynA(
                                    entry.name,
                                    metadata.name,
                                    static_cast<int>(
                                        sizeof(entry.name)));
                            if (IsUsefulPlayerName(
                                    metadata.name))
                                lstrcpynA(
                                    entry.nickname,
                                    metadata.name,
                                    static_cast<int>(
                                        sizeof(entry.nickname)));
                            if (IsUsefulFamilyName(
                                    metadata.familyName))
                                lstrcpynA(
                                    entry.familyName,
                                    metadata.familyName,
                                    static_cast<int>(
                                        sizeof(entry.familyName)));
                            if (entry.displayAdmin &&
                                entry.adminLevel <= 0)
                                entry.adminLevel = 1;
                        }
                    }
                }
                cursor = *lineEnd ? lineEnd + 1 : lineEnd;
            }

#if RAGEMP_DEV_BUILD
        LogDeathSightings(parsed, parsedCount);
#endif

        const ULONGLONG now = GetTickCount64();
        ULONGLONG nameGoodAt[256]{};
        const auto metadataStatus =
            FiveRpMetadataResolver::GetStatus();
        Lock();
        for (int index = 0;
             index < parsedCount;
             ++index)
        {
            auto& current = parsed[index];
            if (current.kind !=
                RagePlayerNames::EntityKind::Player)
                continue;
            for (int observedIndex = 0;
                 observedIndex < g_ObservedPlayerCount;
                 ++observedIndex)
            {
                const auto& observed =
                    g_ObservedPlayers[observedIndex];
                if (!observed.valid ||
                    observed.remoteId != current.remoteId ||
                    now - observed.updatedAt > 180000)
                    continue;
                if (IsUsefulPlayerName(observed.name))
                {
                    lstrcpynA(
                        current.name,
                        observed.name,
                        static_cast<int>(
                            sizeof(current.name)));
                    lstrcpynA(
                        current.nickname,
                        observed.name,
                        static_cast<int>(
                            sizeof(current.nickname)));
                }
                if (observed.factionTag[0])
                {
                    lstrcpynA(
                        current.factionTag,
                        observed.factionTag,
                        static_cast<int>(
                            sizeof(current.factionTag)));
                }
                break;
            }
            if (current.familyId <= 0)
                continue;
            for (int familyIndex = 0;
                 familyIndex < g_ObservedFamilyCount;
                 ++familyIndex)
            {
                const auto& family =
                    g_ObservedFamilies[familyIndex];
                if (!family.valid ||
                    family.familyId != current.familyId ||
                    now - family.updatedAt > 300000 ||
                    !IsUsefulFamilyName(family.name))
                    continue;
                lstrcpynA(
                    current.familyName,
                    family.name,
                    static_cast<int>(
                        sizeof(current.familyName)));
                break;
            }
        }
        for (int index = 0;
             index < parsedCount;
             ++index)
        {
            auto& current = parsed[index];
            if (current.kind !=
                RagePlayerNames::EntityKind::Player)
                continue;
            if (IsUsefulPlayerName(current.name))
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
                    (cached.sqlId > 0 &&
                     current.sqlId > 0 &&
                     cached.sqlId != current.sqlId) ||
                    !IsUsefulPlayerName(cached.name) ||
                    !g_NameGoodAt[previous] ||
                    now - g_NameGoodAt[previous] > 5000)
                    continue;
                lstrcpynA(
                    current.name,
                    cached.name,
                    static_cast<int>(
                        sizeof(current.name)));
                lstrcpynA(
                    current.nickname,
                    cached.nickname,
                    static_cast<int>(
                        sizeof(current.nickname)));
                if (!current.factionKey[0])
                {
                    lstrcpynA(
                        current.factionKey,
                        cached.factionKey,
                        static_cast<int>(
                            sizeof(current.factionKey)));
                    lstrcpynA(
                        current.factionTag,
                        cached.factionTag,
                        static_cast<int>(
                            sizeof(current.factionTag)));
                }
                if (!current.familyName[0] &&
                    current.familyId ==
                        cached.familyId)
                {
                    lstrcpynA(
                        current.familyName,
                        cached.familyName,
                        static_cast<int>(
                            sizeof(current.familyName)));
                }
                nameGoodAt[index] =
                    g_NameGoodAt[previous];
                break;
            }
        }
        int overlayCanonical = 0;
        int overlayFallback = 0;
        int overlayStringFactions = 0;
        for (int index = 0;
             index < parsedCount;
             ++index)
        {
            const auto& current = parsed[index];
            if (current.kind !=
                RagePlayerNames::EntityKind::Player)
                continue;
            if (IsUsefulPlayerName(current.name))
                ++overlayCanonical;
            else
                ++overlayFallback;
            if (IsStringFaction(
                    current.factionKey,
                    current.factionTag))
                ++overlayStringFactions;
        }
        g_HelperReady =
            metadataStatus.helperVisible ||
            helperReady != 0 ||
            overlayCanonical > 0 ||
            g_ObservedPlayerCount > 0;
        g_CanonicalNames =
            overlayCanonical > 0
                ? overlayCanonical
                : canonicalNames;
        g_FallbackNames =
            overlayCanonical > 0 || overlayFallback > 0
                ? overlayFallback
                : fallbackNames;
        g_StringFactions =
            overlayStringFactions > 0
                ? overlayStringFactions
                : stringFactions;
        g_FamilyCacheCount =
            metadataStatus.familyCacheCount +
            g_ObservedFamilyCount;
        if (g_FamilyCacheCount <= 0)
            g_FamilyCacheCount = familyCacheCount;
        g_FamilyPendingCount =
            metadataStatus.familyPendingCount > 0
                ? metadataStatus.familyPendingCount
                : familyPendingCount;
        g_FamilyRepliesSeen = familyRepliesSeen;
        g_FamilyRepliesEmpty = familyRepliesEmpty;
        g_FamilyCanary =
            metadataStatus.familyCanary ||
            familyCanary != 0 ||
            g_ObservedFamilyCount > 0;
        g_FamilyRpcDisabled =
            metadataStatus.familyRpcDisabled ||
            familyRpcDisabled != 0;
        g_Count = parsedCount;
        for (int index = 0; index < parsedCount; ++index)
        {
            g_Entries[index] = parsed[index];
            g_NameGoodAt[index] = nameGoodAt[index];
        }
        for (int index = parsedCount; index < 256; ++index)
        {
            g_Entries[index] = {};
            g_NameGoodAt[index] = 0;
        }
        ++g_Generation;
        Unlock();
    }
}

namespace RagePlayerNames
{
    void ObserveIdentity(
        std::uint16_t remoteId,
        const char* name,
        const char* factionTag) noexcept
    {
        if (!IsUsefulPlayerName(name) &&
            (!factionTag || !factionTag[0]))
            return;
        const ULONGLONG now = GetTickCount64();
        Lock();
        int slot = -1;
        for (int index = 0;
             index < g_ObservedPlayerCount;
             ++index)
        {
            if (g_ObservedPlayers[index].valid &&
                g_ObservedPlayers[index].remoteId == remoteId)
            {
                slot = index;
                break;
            }
        }
        if (slot < 0)
        {
            if (g_ObservedPlayerCount < 256)
                slot = g_ObservedPlayerCount++;
            else
            {
                slot = 0;
                for (int index = 1; index < 256; ++index)
                {
                    if (g_ObservedPlayers[index].updatedAt <
                        g_ObservedPlayers[slot].updatedAt)
                        slot = index;
                }
            }
            g_ObservedPlayers[slot] = {};
            g_ObservedPlayers[slot].valid = true;
            g_ObservedPlayers[slot].remoteId = remoteId;
        }
        auto& observed = g_ObservedPlayers[slot];
        observed.updatedAt = now;
        if (IsUsefulPlayerName(name))
        {
            lstrcpynA(
                observed.name,
                name,
                static_cast<int>(sizeof(observed.name)));
        }
        if (factionTag && factionTag[0])
        {
            lstrcpynA(
                observed.factionTag,
                factionTag,
                static_cast<int>(
                    sizeof(observed.factionTag)));
        }
        for (int index = 0; index < g_Count; ++index)
        {
            auto& current = g_Entries[index];
            if (current.kind != EntityKind::Player ||
                current.remoteId != remoteId)
                continue;
            if (IsUsefulPlayerName(observed.name))
            {
                lstrcpynA(
                    current.name,
                    observed.name,
                    static_cast<int>(sizeof(current.name)));
                lstrcpynA(
                    current.nickname,
                    observed.name,
                    static_cast<int>(
                        sizeof(current.nickname)));
                g_NameGoodAt[index] = now;
            }
            if (observed.factionTag[0])
            {
                lstrcpynA(
                    current.factionTag,
                    observed.factionTag,
                    static_cast<int>(
                        sizeof(current.factionTag)));
            }
        }
        ++g_Generation;
        Unlock();
    }

    void ObserveFamily(
        int familyId,
        const char* name) noexcept
    {
        if (familyId <= 0 || !IsUsefulFamilyName(name))
            return;
        const ULONGLONG now = GetTickCount64();
        Lock();
        int slot = -1;
        for (int index = 0;
             index < g_ObservedFamilyCount;
             ++index)
        {
            if (g_ObservedFamilies[index].valid &&
                g_ObservedFamilies[index].familyId == familyId)
            {
                slot = index;
                break;
            }
        }
        if (slot < 0)
        {
            if (g_ObservedFamilyCount < 128)
                slot = g_ObservedFamilyCount++;
            else
            {
                slot = 0;
                for (int index = 1; index < 128; ++index)
                {
                    if (g_ObservedFamilies[index].updatedAt <
                        g_ObservedFamilies[slot].updatedAt)
                        slot = index;
                }
            }
            g_ObservedFamilies[slot] = {};
            g_ObservedFamilies[slot].valid = true;
            g_ObservedFamilies[slot].familyId = familyId;
        }
        auto& observed = g_ObservedFamilies[slot];
        observed.updatedAt = now;
        lstrcpynA(
            observed.name,
            name,
            static_cast<int>(sizeof(observed.name)));
        for (int index = 0; index < g_Count; ++index)
        {
            auto& current = g_Entries[index];
            if (current.kind != EntityKind::Player ||
                current.familyId != familyId)
                continue;
            lstrcpynA(
                current.familyName,
                observed.name,
                static_cast<int>(
                    sizeof(current.familyName)));
        }
        ++g_Generation;
        Unlock();
    }

    void Update() noexcept
    {
        const auto status = RageV8Runtime::GetStatus();
        const ULONGLONG now = GetTickCount64();
        if (status.generation != g_RuntimeGeneration)
        {
            g_RuntimeGeneration = status.generation;
            ResetSessionState();
        }

        if (g_PendingCommand)
        {
            // The readiness probe comes first: a Result is 32 KB and zeroing it
            // on the stack every frame while the script is still running is
            // pure waste.
            if (RageV8Runtime::HasResultFor(g_PendingCommand))
            {
                RageV8Runtime::Result result{};
                if (RageV8Runtime::PollResultFor(
                        g_PendingCommand,
                        result))
                {
                    if (result.success &&
                        std::strncmp(
                            result.text,
                            "BridgeError:",
                            12) != 0)
                    {
                        ParseSnapshot(result.text);
                    }
                    else if (result.text[0])
                    {
                        hooks::HLog(result.text);
                    }
                }
                g_PendingCommand = 0;
            }
            else if (now - g_LastRequest >= 1500 ||
                     status.state != RageV8Runtime::State::Ready ||
                     !status.rootCaptured)
            {
                g_PendingCommand = 0;
            }
        }

        const auto& npcProfile =
            Menu::s_EspProfiles[static_cast<int>(
                Menu::EspProfileId::Npc)];
        const auto& animalProfile =
            Menu::s_EspProfiles[static_cast<int>(
                Menu::EspProfileId::Animal)];
        const bool includeNonPlayers =
            npcProfile.enabled ||
            animalProfile.enabled ||
            (Menu::s_RadarSettings.enabled &&
             Menu::s_RadarSettings.showNpcs);
        bool profileDemand = false;
        bool visibilityDemand =
            Features::g_AimbotEnabled;
        for (int index = 0;
             index < static_cast<int>(
                 Menu::EspProfileId::Count);
             ++index)
        {
            const auto& profile =
                Menu::s_EspProfiles[index];
            profileDemand |= profile.enabled;
            visibilityDemand |=
                profile.enabled &&
                profile.visibleCheck;
        }
        const bool active =
            Features::g_EspEnabled ||
            Features::g_AimbotEnabled ||
            profileDemand ||
            Menu::s_RadarSettings.enabled ||
            Menu::s_VehicleVisualSettings.enabled ||
            Menu::s_ObjectVisualSettings.enabled ||
            Menu::NeedsPlayerCache();
        // What this carries is identity and roleplay state: names, factions,
        // families, admin level, health, downed. Positions and bones come from
        // the game itself, every frame. None of the rest changes four times a
        // second, and each run is about two milliseconds spent on the thread
        // that draws the frame, which is where the standing-still stutter came
        // from.
        const ULONGLONG refreshInterval =
            active ? 400ULL : 1000ULL;

        // One script runs per V8 pump, so queueing behind another heavy job
        // only stacks two long ticks back to back on the game's JS thread.
        // Waiting a few frames costs nothing: the data set is unchanged.
        if (!g_PendingCommand &&
            status.queuedCommands == 0 &&
            status.state == RageV8Runtime::State::Ready &&
            status.rootCaptured &&
            (GetAsyncKeyState(VK_F1) & 0x8000) == 0 &&
            now - g_LastRequest >= refreshInterval)
        {
            g_LastRequest = now;
            if (BuildSnapshotScript(
                    visibilityDemand,
                    includeNonPlayers))
            {
                if (RageV8Runtime::Enqueue(
                        g_SnapshotScript,
                        &g_PendingCommand))
                {
                    RuntimeProfiler::
                        RecordVisibilitySamples(
                            visibilityDemand
                                ? 24u : 0u);
                }
            }
        }
    }

    bool Get(
        std::uint16_t remoteId,
        char* output,
        int outputSize) noexcept
    {
        if (!output || outputSize < 2)
            return false;
        output[0] = 0;
        Lock();
        for (int index = 0; index < g_Count; ++index)
        {
            if (g_Entries[index].remoteId != remoteId)
                continue;
            lstrcpynA(
                output,
                g_Entries[index].name,
                outputSize);
            Unlock();
            return output[0] != 0;
        }
        Unlock();
        return false;
    }

    bool FindNearest(
        float x,
        float y,
        float z,
        float maxDistance,
        char* output,
        int outputSize) noexcept
    {
        if (!output || outputSize < 2 ||
            maxDistance <= 0.0f)
            return false;
        output[0] = 0;
        const float maxDistanceSquared =
            maxDistance * maxDistance;
        float bestDistanceSquared = maxDistanceSquared;
        int best = -1;

        Lock();
        for (int index = 0; index < g_Count; ++index)
        {
            // Only real mp.players — never fauna/NPC placeholders.
            if (g_Entries[index].kind != EntityKind::Player)
                continue;
            const float dx = g_Entries[index].x - x;
            const float dy = g_Entries[index].y - y;
            const float dz = g_Entries[index].z - z;
            const float distanceSquared =
                dx * dx + dy * dy + dz * dz;
            if (distanceSquared > bestDistanceSquared)
                continue;
            bestDistanceSquared = distanceSquared;
            best = index;
        }
        if (best >= 0)
        {
            lstrcpynA(
                output,
                g_Entries[best].name,
                outputSize);
        }
        Unlock();
        return output[0] != 0;
    }

    int CopySnapshot(
        SnapshotEntry* output,
        int capacity,
        std::uint64_t* generation) noexcept
    {
        if (!output || capacity <= 0)
            return 0;
        Lock();
        int count = g_Count;
        if (count > capacity)
            count = capacity;
        for (int index = 0; index < count; ++index)
        {
            output[index].kind = g_Entries[index].kind;
            output[index].local = g_Entries[index].local;
            output[index].remoteId = g_Entries[index].remoteId;
            output[index].handle = g_Entries[index].handle;
            output[index].x = g_Entries[index].x;
            output[index].y = g_Entries[index].y;
            output[index].z = g_Entries[index].z;
            output[index].heading = g_Entries[index].heading;
            output[index].adminLevel =
                g_Entries[index].adminLevel;
            output[index].visible =
                g_Entries[index].visible;
            output[index].factionId =
                g_Entries[index].factionId;
            output[index].factionType =
                g_Entries[index].factionType;
            output[index].familyId =
                g_Entries[index].familyId;
            output[index].sqlId = g_Entries[index].sqlId;
            output[index].masked =
                g_Entries[index].masked;
            output[index].displayAdmin =
                g_Entries[index].displayAdmin;
            output[index].nametagDisabled =
                g_Entries[index].nametagDisabled;
            output[index].dead = g_Entries[index].dead;
            output[index].knock = g_Entries[index].knock;
            output[index].hp = g_Entries[index].hp;
            output[index].hpSync = g_Entries[index].hpSync;
            output[index].hpRaw = g_Entries[index].hpRaw;
            output[index].dyingFlag =
                g_Entries[index].dyingFlag;
            output[index].boneMask =
                g_Entries[index].boneMask;
            for (int bone = 0;
                 bone < static_cast<int>(
                     BoneSlot::Count);
                 ++bone)
            {
                output[index].bones[bone] =
                    g_Entries[index].bones[bone];
            }
            lstrcpynA(
                output[index].name,
                g_Entries[index].name,
                static_cast<int>(sizeof(output[index].name)));
            lstrcpynA(
                output[index].factionKey,
                g_Entries[index].factionKey,
                static_cast<int>(
                    sizeof(output[index].factionKey)));
            lstrcpynA(
                output[index].factionTag,
                g_Entries[index].factionTag,
                static_cast<int>(
                    sizeof(output[index].factionTag)));
            lstrcpynA(
                output[index].familyName,
                g_Entries[index].familyName,
                static_cast<int>(
                    sizeof(output[index].familyName)));
            lstrcpynA(
                output[index].nickname,
                g_Entries[index].nickname,
                static_cast<int>(
                    sizeof(output[index].nickname)));
        }
        if (generation)
            *generation = g_Generation;
        Unlock();
        return count;
    }

    std::uint64_t SnapshotGeneration() noexcept
    {
        Lock();
        const std::uint64_t generation = g_Generation;
        Unlock();
        return generation;
    }

    LocalIdentity GetLocalIdentity() noexcept
    {
        LocalIdentity identity{};
        Lock();
        for (int index = 0; index < g_Count; ++index)
        {
            if (!g_Entries[index].local)
                continue;
            identity.valid = true;
            identity.factionId = g_Entries[index].factionId;
            identity.familyId = g_Entries[index].familyId;
            lstrcpynA(
                identity.factionKey,
                g_Entries[index].factionKey,
                static_cast<int>(
                    sizeof(identity.factionKey)));
            lstrcpynA(
                identity.familyName,
                g_Entries[index].familyName,
                static_cast<int>(
                    sizeof(identity.familyName)));
            break;
        }
        Unlock();
        return identity;
    }

    bool GetByHandle(
        std::uint32_t handle,
        SnapshotEntry& entry) noexcept
    {
        if (!handle)
            return false;
        Lock();
        for (int index = 0; index < g_Count; ++index)
        {
            if (g_Entries[index].handle != handle)
                continue;
            entry.kind = g_Entries[index].kind;
            entry.local = g_Entries[index].local;
            entry.remoteId = g_Entries[index].remoteId;
            entry.handle = g_Entries[index].handle;
            entry.x = g_Entries[index].x;
            entry.y = g_Entries[index].y;
            entry.z = g_Entries[index].z;
            entry.heading = g_Entries[index].heading;
            entry.adminLevel =
                g_Entries[index].adminLevel;
            entry.visible =
                g_Entries[index].visible;
            entry.factionId =
                g_Entries[index].factionId;
            entry.factionType =
                g_Entries[index].factionType;
            entry.familyId =
                g_Entries[index].familyId;
            entry.sqlId = g_Entries[index].sqlId;
            entry.masked =
                g_Entries[index].masked;
            entry.displayAdmin =
                g_Entries[index].displayAdmin;
            entry.nametagDisabled =
                g_Entries[index].nametagDisabled;
            entry.dead = g_Entries[index].dead;
            entry.knock = g_Entries[index].knock;
            entry.hp = g_Entries[index].hp;
            entry.hpSync = g_Entries[index].hpSync;
            entry.hpRaw = g_Entries[index].hpRaw;
            entry.dyingFlag = g_Entries[index].dyingFlag;
            entry.boneMask =
                g_Entries[index].boneMask;
            for (int bone = 0;
                 bone < static_cast<int>(
                     BoneSlot::Count);
                 ++bone)
            {
                entry.bones[bone] =
                    g_Entries[index].bones[bone];
            }
            lstrcpynA(
                entry.name,
                g_Entries[index].name,
                static_cast<int>(
                    sizeof(entry.name)));
            lstrcpynA(
                entry.factionKey,
                g_Entries[index].factionKey,
                static_cast<int>(
                    sizeof(entry.factionKey)));
            lstrcpynA(
                entry.factionTag,
                g_Entries[index].factionTag,
                static_cast<int>(
                    sizeof(entry.factionTag)));
            lstrcpynA(
                entry.familyName,
                g_Entries[index].familyName,
                static_cast<int>(
                    sizeof(entry.familyName)));
            lstrcpynA(
                entry.nickname,
                g_Entries[index].nickname,
                static_cast<int>(
                    sizeof(entry.nickname)));
            Unlock();
            return true;
        }
        Unlock();
        return false;
    }

    IdentityStatus GetIdentityStatus() noexcept
    {
        IdentityStatus status{};
        Lock();
        status.helperReady = g_HelperReady;
        status.familyCanary = g_FamilyCanary;
        status.familyRpcDisabled =
            g_FamilyRpcDisabled;
        status.canonicalNames = g_CanonicalNames;
        status.fallbackNames = g_FallbackNames;
        status.stringFactions =
            g_StringFactions;
        status.familyCacheCount =
            g_FamilyCacheCount;
        status.familyPendingCount =
            g_FamilyPendingCount;
        status.familyRepliesSeen = g_FamilyRepliesSeen;
        status.familyRepliesEmpty = g_FamilyRepliesEmpty;
        Unlock();
        return status;
    }

    int Count() noexcept
    {
        Lock();
        const int count = g_Count;
        Unlock();
        return count;
    }
}
