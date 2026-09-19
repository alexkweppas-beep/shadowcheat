#pragma once

namespace QuantToolsScript
{
    inline constexpr char kSource[] =
        R"QTJSX91(// ==========================================================
// QUANT TOOLS v1.0 — Unified: FakeDeath + Aquanaut Bot
// ==========================================================
if (global._qt_unload) { try { global._qt_unload(); } catch(e) {} }

(function () {
    const SCRIPT_NAME = '[QT]';
    let qtAlive = true;
    const qtTimeouts = [];
    const qtIntervals = [];
    function qtSetTimeout(fn, ms) {
        const id = setTimeout(function() {
            const idx = qtTimeouts.indexOf(id);
            if (idx !== -1) qtTimeouts.splice(idx, 1);
            if (!qtAlive) return;
            fn();
        }, ms);
        qtTimeouts.push(id);
        return id;
    }
    function qtDelay(ms) {
        return new Promise(resolve => qtSetTimeout(resolve, ms));
    }
    function qtSetInterval(fn, ms) {
        const id = setInterval(function() {
            if (!qtAlive) return;
            fn();
        }, ms);
        qtIntervals.push(id);
        return id;
    }
    function qtClearTimeout(id) {
        clearTimeout(id);
        const idx = qtTimeouts.indexOf(id);
        if (idx !== -1) qtTimeouts.splice(idx, 1);
    }
    function qtClearInterval(id) {
        clearInterval(id);
        const idx = qtIntervals.indexOf(id);
        if (idx !== -1) qtIntervals.splice(idx, 1);
    }
    function canUseBrowserMenuExecute() {
        return qtAlive && typeof browserMenu !== 'undefined' && browserMenu && typeof browserMenu.execute === 'function';
    }
    function safeBrowserExecute(code) {
        if (!canUseBrowserMenuExecute()) return false;
        try { browserMenu.execute(code); return true; } catch(e) { return false; }
    }

    // ---- Aquanaut Config ----
    const DELAY = {
        AFTER_TAKE: 200,
        BETWEEN_BOXES: 50,       // was 200
        AFTER_ALL_COLLECTED: 200,
        LOAD_VEHICLE: 3000,
        ORDER_WAIT: 100,
        COLSHAPE_WAIT: 5000,
        FREEZE_BEFORE: 200,
        UNFREEZE_AFTER: 200,
        TP_SETTLE: 200,
        BOX_PRE_COLLECT: 350,    // wait after TP before takeObject (was 400)
        BOX_CONFIRM_MAX: 1500,   // max wait for collect confirmation (was 3000)
        BOX_POLL: 50,            // polling step (was 100)
        BOX_RETRY_WAIT: 400,     // wait before retry
        BOX_MAX_RETRIES: 2,      // max attempts per box (was infinite)
        ROUND_PAUSE: 500,        // pause between rounds (was 2000)
    };

    const STORAGE_POINTS = [
        { x: -79.79, y: -2771.72, z: -0.3 },
        { x: -1914.715, y: -2701.158, z: -0.271 },
        { x: 1714.785, y: -2846.981, z: -0.168 },
    ];

    // ---- Local Quant Tools integration ----

    // ---- Shared State ----
    let sessionToken = null;

    // ---- FakeDeath State ----
    let crawlActive = false;
    let renderEvent = null;
    let crawlTimeout = null;
    let lastAnimSent = 0;
    let crawlKey = 0; // None — webapp syncs saved key on connect
    let crawlMousePoll = false;
    let crawlMouseWasDown = false;
    let standupKey = 0; // None — webapp syncs saved key on connect
    let standupMousePoll = false;
    let standupMouseWasDown = false;
    let cheatSheetKey = 0;
    let csMousePoll = false;
    let csMouseWasDown = false;
    let marijuanaKey = 0;
    let marijuanaMousePoll = false;
    let marijuanaMouseWasDown = false;
    let cheatSheetOpen = false;
    let cheatSheetBrowser = null;
    let csScale = 100;
    let csOpacity = 90;
    let csDim = 85;
    let csVariant = 2; // 1=PNG image, 2=Dark Table, 3=Neon Cards
    let heliStabEnabled = false;
    let heliStabDamping = 0.92; // roll damping factor per frame (lower = stronger stabilization)
    let autoBuyMarket = false;
    let ampBotActive = false;
    let ampBotTimer = null;
    let matBotActive = false;
    let matBotTimer = null;
    let muteHitSound = false;
    let hitSoundVolume = 0.06;
    let customHitSound = '';

    // ---- Custom HitFX ----
    let hitfx = {
        enabled: false,         // master switch
        showNumbers: true,      // show floating damage numbers
        color: [255, 255, 255, 255],   // RGBA for all hits (same for head/body)
        originalColors: false,  // true = Quant RP green-to-red gradient based on damage
        fontSize: 0.55,         // GTA text scale
        fontId: 4,              // GTA font: 0=Chalet London, 1=House Script, 2=Monospace, 4=Chalet Comprime, 7=Pricedown
        stretchX: 1.0,          // horizontal text stretch
        stretchY: 1.0,          // vertical text stretch
        outline: true,          // outline around numbers
        outlineWidth: 2,        // outline thickness (1-5)
        outlineColor: [0, 0, 0, 255], // outline RGBA
        shadow: true,           // drop shadow under numbers
        duration: 1500,         // ms to float before disappearing
        floatDist: 1.2,         // world units to float upward
        spread: 0.3,            // random XY spread in world units
        heightOffset: 0.85,     // height above target (world units)
        distScale: true,        // scale font size by distance (closer = bigger)
        sound: true,            // play hit sound
        soundUrl: '',           // custom sound URL (empty = built-in hitmarker.mp3)
        soundVol: 0.5,          // 0..1
        headSound: true,        // play sound on headshot (false = only body hit sound)
    };
    let hitfxMarkers = [];      // [{ts, damage, wx, wy, wz, head}]
    let hitfxPendingHits = [];  // outgoingDamage is latency-sensitive; process visuals outside the event
    let hitfxAudioReady = false;
    let hitfxSoundQueue = 0;
    let hitfxBuiltinSoundQueue = 0;
    let hitfxLastSoundFlush = 0;

    // ---- Server UI Theme (Majestic look from CEF-dump-main) ----
    let qtUiTheme = { enabled: false, opacity: 1.0, editHud: false };
    let qtUiThemeTimer = null;
    let qtUiThemeObserver = null;

    // ---- Damage Counter (crosshair total) ----
    let dmgCounterEnabled = false;
    let dmgCounterTarget = null;      // remoteId текущей цели
    let dmgCounterTotal = 0;          // суммарный урон по текущей цели
    let dmgCounterLastHit = 0;        // timestamp последнего попадания
    let dmgCounterTargetName = '';    // имя цели для отображения
    const DMG_COUNTER_RESET_MS = 3000; // сброс, если не стреляли 3с

    function hitfxGetPlayerDistance(pos) {
        try {
            const p = mp.players.local.position;
            return Math.sqrt((pos.x-p.x)*(pos.x-p.x) + (pos.y-p.y)*(pos.y-p.y) + (pos.z-p.z)*(pos.z-p.z));
        } catch(e) {
            return 0;
        }
    }

    // ---- Poker Transfer Bot ----
    let pokerBotActive = false;
    let pokerMyCards = [];
    let pokerCommunity = [];
    let pokerStage = '';
    let pokerMyTurn = false;
    let pokerMySeat = -1;
    let pokerCanPass = false;
    let pokerCanCall = false;
    let pokerCanFold = false;
    let pokerCanAllIn = false;
    let pokerCanBet = false;
    let pokerAutoDecideTimer = null; // fallback timer cancelled when server sends poker_action:
    let pokerCallAmt = 0;
    let pokerBetAmt = 0;
    let pokerActionPending = false;
    let pokerAutoMode = null; // 'dump' (farm lose) | 'catch' (main win) | null
    const POKER_CHIP_USD = 90; // server ratio: 1 chip = $90
    let pokerDumpAmount = 1000; // raise amount in CHIPS for dump on 1st turn (1000 chips = $90,000)

    // ---- Aquanaut State (v4 = single v2-style surface bot; v1/v3 removed) ----
    let botVersion = 2; // fixed: food/water + invis + air (no parking)
    let botActive = false;
    let botStartTime = 0;
    let currentStatus = 'Idle';
    let boxesCollected = 0;
    let totalBoxes = 0;
    let totalRounds = 0;
    let totalEarned = 0;
    let bonusEarned = 0;
    // Live junk-seller prices (Bradley / Seller type=4) for aquanaut loot.
    const LOOT_PRICE_FALLBACK = { pearl: 10038, starfish: 750, rapana: 350 };
    let lootPrices = { pearl: null, starfish: null, rapana: null };
    let lootPriceExpiryMs = 0;
    let lootPricesStale = false;
    let lootPricesLoggedStale = false;
    let aquanautPoints = [];
    let colshapesReady = false;
    let gotNinfo = false;
    let gotEarned = false;
    let gotAirLoaded = false;
    let gotBoxCollected = false;
    let gotOrderError = false;
    let lastNinfoText = '';
    let airMinutes = null;
    let airDeadlineMs = 0; // Date.now() + remaining ms from CEF/native air HUD
    let airDumpUntil = 0;
    let airDumpLines = [];
    let airDumpBuffer = '';
    let airDumpSeen = {};
    let airDumpCefTimer = null;
    let logLines = [];

    // ---- v2: Food/Water ----
    let foodSqlId = null;
    let waterSqlId = null;
    let eatInterval = null;
    let listeningForFood = false;
    let listeningForWater = false;
    let airReloadSent = false; // legacy flag unused; see airReloadPending
    let airReloadPending = false;
    let airReloadTimer = null;
    let airReloadAttempt = 0;
    const AIR_AUTO_RELOAD_MIN = 5;      // refill when <= 5 min left
    const AIR_RELOAD_WAIT_MS = 60000;   // wait 1 min for refill, then retry
    const STUCK_WATCHDOG_MS = 45000;    // no box progress → abort round
    const ADMIN_SPECT_PAUSE_M = 150;    // pause if spectating admin within range
    const INVIS_REASSERT_MS = 60000;    // re-send setInvisible periodically
    let safeTpMode = true; // ON = no ac:refreshtp event; step <650m + silent lastLegitTp
    let _qtAcRefreshTp = null; // captured bound AC.refreshTp (not via events.call)
    let invisEnabled = true;
    let invisActivated = false;
    let lastInvisAssertMs = 0;
    let skipOccupiedBoxes = true;
    const BOX_SKIP_RADIUS_XY = 25;
    let orderMode = 'last';            // default last order; price/zone = fallback
    let zoneFilter = '';
    let zoneMaxDist = 1500;
    let pinnedOrderId = null;
    let zoneAnchor = null;
    let botPausedAdmin = false;
    let lastBoxProgressMs = 0;
    let roundAbortStuck = false;

    // ---- Vehicle State ----
    let myVehicleRef = null; // cached own submarine (survives eject — used for WARP_PED_INTO_VEHICLE)
    let myVehicleModel = 0;

    // ---- Weapon Attachments ----
    let weaponAttachments = {}; // { weaponHash: { compHash: true/false } }
    let lastDetectedWeapon = 0;
    let weaponCheckTimer = null;
    const WPN_COMPS = [
        // Muzzle brakes
        {h:0x6927E1A1,n:'Bell-End Muzzle Brake',t:'m'},
        // Suppressors
        {h:0xC304849A,n:'Глушитель',t:'s'},{h:0x65EA7EBB,n:'Глушитель v2',t:'s'},
        {h:0x837445AA,n:'Глушитель (AR)',t:'s'},{h:0xA73D4664,n:'Глушитель (AR v2)',t:'s'},
        {h:0xE608B35E,n:'Глушитель (SR)',t:'s'},{h:0xAC42DF71,n:'Глушитель (SR v3)',t:'s'},
        // Grips
        {h:0xC164F53,n:'Рукоятка',t:'g'},{h:0x9D65907A,n:'Рукоятка v2',t:'g'},
        // Holographic sights
        {h:0x420FD713,n:'Гол. прицел',t:'o'},{h:0x9FDB5652,n:'Гол. прицел (SMG)',t:'o'},
        // Extended clips (per-weapon)
        {h:0xED265A1C,n:'Расш. магазин',t:'c'},{h:0xD67B4F2D,n:'Расш. магазин',t:'c'},
)QTJSX91"
        R"QTJSX91(        {h:0x249A17D5,n:'Расш. магазин',t:'c'},{h:0xD9D3AC92,n:'Расш. магазин',t:'c'},
        {h:0x64F9C62B,n:'Расш. магазин',t:'c'},{h:0x5ED6C128,n:'Расш. магазин',t:'c'},
        {h:0xCE8C0772,n:'Расш. магазин',t:'c'},{h:0x10E6BA2B,n:'Расш. магазин',t:'c'},
        {h:0x350966FB,n:'Расш. магазин',t:'c'},{h:0xBB46E417,n:'Расш. магазин',t:'c'},
        {h:0x937ED0B7,n:'Расш. магазин',t:'c'},{h:0xB9835B2E,n:'Расш. магазин',t:'c'},
        {h:0xB92C6979,n:'Расш. магазин',t:'c'},{h:0x334A5203,n:'Расш. магазин',t:'c'},
        {h:0x86BD7F72,n:'Расш. магазин',t:'c'},{h:0x971CF6FD,n:'Расш. магазин',t:'c'},
        {h:0xB1214F9B,n:'Расш. магазин',t:'c'},{h:0xD12ACA6F,n:'Расш. магазин',t:'c'},
        {h:0x91109691,n:'Расш. магазин',t:'c'},{h:0x5DD5DBD5,n:'Расш. магазин',t:'c'},
        {h:0x8EC1C979,n:'Расш. магазин',t:'c'},{h:0x7C8BD10E,n:'Расш. магазин',t:'c'},
        {h:0xDE1FA12C,n:'Расш. магазин',t:'c'},{h:0xB3688B0F,n:'Расш. магазин',t:'c'},
        {h:0x59FF9BF8,n:'Расш. магазин',t:'c'},{h:0x684ACE42,n:'Расш. магазин',t:'c'},
        {h:0x82158B47,n:'Расш. магазин',t:'c'},{h:0xD6C59CD6,n:'Расш. магазин',t:'c'},
        {h:0x17DF42E9,n:'Расш. магазин',t:'c'},{h:0xEFB00628,n:'Расш. магазин',t:'c'},
        {h:0xEAC8C270,n:'Расш. магазин',t:'c'},{h:0xCCFD2AC5,n:'Расш. магазин',t:'c'},
        {h:0xE6CFD1AA,n:'Расш. магазин',t:'c'},{h:0x2CD8FF9D,n:'Расш. магазин',t:'c'},
    ];
    // Hardcoded weapon→component compatibility (bypasses buggy DOES_WEAPON_TAKE_WEAPON_COMPONENT native)
    const WPN_COMPAT = {
        0x1B06D571:[0x65EA7EBB,0xED265A1C],
        0xBFE256D4:[0x65EA7EBB,0x5ED6C128],
        0x5EF9FEC4:[0xC304849A,0xD67B4F2D],
        0x22D8FE39:[0xC304849A,0x249A17D5],
        0x99AEEB3B:[0xA73D4664,0xD9D3AC92],
        0xD205520E:[0xC304849A,0x64F9C62B],
        0x13532244:[0xA73D4664,0x10E6BA2B],
        0x2BE6766B:[0xC304849A,0x350966FB],
        0x78A97CD0:[0xC304849A,0x9FDB5652,0xB9835B2E],
        0xEFE7E2DF:[0xA73D4664,0xBB46E417],
        0x0A3D4D34:[0xC164F53,0x334A5203],
        0xBD248B55:[0x937ED0B7],
        0xDB1AA450:[0xC304849A,0xB92C6979],
        0x1D073A89:[0xE608B35E],
        0x555AF99A:[0xAC42DF71,0x420FD713],
        0x7846A318:[],
        0xE284C527:[0x837445AA,0xC164F53,0x86BD7F72],
        0x9D61E50F:[0xA73D4664,0xC164F53],
        0x3AABBBAA:[0xA73D4664,0xC164F53,0x971CF6FD],
        0xBFEFFF6D:[0xA73D4664,0xC164F53,0xB1214F9B],
        0x394F415C:[0xA73D4664,0x9D65907A,0x420FD713,0xD12ACA6F],
        0x83BF0278:[0x837445AA,0xC164F53,0x91109691],
        0xFAD1F1C9:[0x837445AA,0x9D65907A,0x420FD713,0x5DD5DBD5],
        0xAF113F99:[0x837445AA,0x8EC1C979],
        0xC0A3098D:[0xA73D4664,0xC164F53,0x7C8BD10E],
        0x969C3D67:[0xA73D4664,0x9D65907A,0x420FD713,0xDE1FA12C],
        0x7F229F94:[0x837445AA,0xC164F53,0xB3688B0F],
        0x84D6FAFD:[0x837445AA,0x9D65907A,0x420FD713,0xEFB00628],
        0x624FE830:[0x59FF9BF8],
        0x9D1F17E6:[0x837445AA,0x684ACE42],
        0x9D07F764:[0x82158B47],
        0x7FD62962:[0xC164F53,0xD6C59CD6],
        0xDBBD7280:[0x9D65907A,0x420FD713,0x17DF42E9],
        0x61012683:[0xEAC8C270],
        0x05FC3C11:[0xA73D4664],
        0x0C472FE2:[],
        0x0A914799:[0xAC42DF71,0x2CD8FF9D,0x6927E1A1],
        0xC734385A:[0x837445AA,0xC164F53,0xCCFD2AC5],
        0x6A6C02E0:[0x837445AA,0x9D65907A,0x420FD713,0xE6CFD1AA],
    };
    const WPN_NAMES = {
        0x1B06D571:'Pistol',0xBFE256D4:'Pistol Mk2',0x5EF9FEC4:'Combat Pistol',
        0x22D8FE39:'AP Pistol',0x99AEEB3B:'Pistol .50',0xD205520E:'Heavy Pistol',
        0x13532244:'Micro SMG',0x2BE6766B:'SMG',0x78A97CD0:'SMG Mk2',
        0xEFE7E2DF:'Assault SMG',0x0A3D4D34:'Combat PDW',0xBD248B55:'Mini SMG',
        0xDB1AA450:'Machine Pistol',
        0x1D073A89:'Pump Shotgun',0x555AF99A:'Pump Shotgun Mk2',
        0x7846A318:'Sawed-Off',0xE284C527:'Assault Shotgun',
        0x9D61E50F:'Bullpup Shotgun',0x3AABBBAA:'Heavy Shotgun',
        0xBFEFFF6D:'Assault Rifle',0x394F415C:'Assault Rifle Mk2',
        0x83BF0278:'Carbine Rifle',0xFAD1F1C9:'Carbine Mk2',
        0xAF113F99:'Advanced Rifle',
        0xC0A3098D:'Special Carbine',0x969C3D67:'Special Carbine Mk2',
        0x7F229F94:'Bullpup Rifle',0x84D6FAFD:'Bullpup Rifle Mk2',
        0x624FE830:'Compact Rifle',0xC78D71B4:'Heavy Rifle',0x9D1F17E6:'Military Rifle',
        0x9D07F764:'MG',0x7FD62962:'Combat MG',0xDBBD7280:'Combat MG Mk2',
        0x61012683:'Gusenberg',
        0x05FC3C11:'Sniper',0x0C472FE2:'Heavy Sniper',0x0A914799:'Heavy Sniper Mk2',
        0xC734385A:'Marksman Rifle',0x6A6C02E0:'Marksman Rifle Mk2',
        0xA2719263:'Unarmed'
    };

    const EAT_DRINK_INTERVAL = 10 * 60 * 1000; // 10 min

    // ---- GUI removed - managed via external webapp ----


    // ---- Log ----
    // Never write to F8 / mp.console — only internal status + ring buffer.
    function log(msg) {
        currentStatus = msg;
        const t = new Date();
        const ts = t.getHours().toString().padStart(2, '0') + ':' + t.getMinutes().toString().padStart(2, '0') + ':' + t.getSeconds().toString().padStart(2, '0');
        logLines.push(ts + ' ' + msg);
        if (logLines.length > 20) logLines.shift();
        try { qtUiLog(msg); } catch (e) {}
    }

    // Bot GUI update removed (was for F2 menu)
    function updateBotGui() {}

    // ---- Junk seller loot prices (pearl / starfish / rapana) ----
    function lootPriceFor(key) {
        const v = lootPrices[key];
        if (v != null && isFinite(v) && v > 0) return Number(v);
        return LOOT_PRICE_FALLBACK[key] || 0;
    }

    function lootPriceSource(key) {
        return lootPrices[key] != null ? '' : ' (fallback)';
    }

    function tickLootPriceExpiry() {
        if (!lootPriceExpiryMs || lootPricesStale) return;
        if (Date.now() < lootPriceExpiryMs) return;
        lootPricesStale = true;
        if (!lootPricesLoggedStale) {
            lootPricesLoggedStale = true;
            log('Junk prices expired — reopen seller (Bradley)');
        }
    }

    function applyLootSellerPayload(type, data) {
        if (Number(type) !== 4) return;
        let payload = data;
        if (typeof payload === 'string') {
            try { payload = JSON.parse(payload); } catch (e) { return; }
        }
        if (!payload || !Array.isArray(payload.items)) return;

        const next = {
            pearl: lootPrices.pearl,
            starfish: lootPrices.starfish,
            rapana: lootPrices.rapana
        };
        const prev = {
            pearl: lootPrices.pearl,
            starfish: lootPrices.starfish,
            rapana: lootPrices.rapana
        };

        for (let i = 0; i < payload.items.length; i++) {
            const it = payload.items[i];
            if (!it || typeof it !== 'object') continue;
            const name = String(it.text || '');
            if (!name || /продати\s*все/i.test(name)) continue;
            const price = Number(it.price);
            if (!isFinite(price) || price <= 0) continue;
            if (/перлин/i.test(name)) next.pearl = price;
            else if (/зірк|звезд/i.test(name)) next.starfish = price;
            else if (/рапан/i.test(name)) next.rapana = price;
        }

        let expiry = 0;
        if (payload.time != null) {
            const t = new Date(payload.time).getTime();
            if (isFinite(t) && t > 0) expiry = t;
        }

        const diffs = [];
        ['pearl', 'starfish', 'rapana'].forEach(function(k) {
            if (next[k] != null && prev[k] != null && next[k] !== prev[k])
                diffs.push(k + ' ' + prev[k] + '→' + next[k]);
        });

        lootPrices = next;
        lootPriceExpiryMs = expiry;
        lootPricesStale = false;
        lootPricesLoggedStale = false;

        const rem = expiry > Date.now()
            ? Math.round((expiry - Date.now()) / 1000)
            : 0;
        log(
            'Loot prices: pearl=' + (next.pearl != null ? next.pearl : '?') +
            ' starfish=' + (next.starfish != null ? next.starfish : '?') +
            ' rapana=' + (next.rapana != null ? next.rapana : '?') +
            (expiry ? (' expiry=' + rem + 's') : '') +
            (diffs.length ? (' | ' + diffs.join(', ')) : '')
        );
    }

    try {
        mp.events.add('setSeller', function(type, data) {
            try { applyLootSellerPayload(type, data); } catch (e) {}
        });
    } catch (e) {}

    // ---- Shared callRemote ----
    function callRemote(event, param) {
        if (!sessionToken) { log('No token!'); return false; }
        try {
            browserMenu.call("socket", "callRemote", [event, sessionToken, param != null ? param : null]);
            return true;
        } catch (e) {
            log('callRemote error: ' + e);
            return false;
        }
    }

    function callRemoteMulti() {
        if (!sessionToken) { log('No token!'); return false; }
        const args = Array.from(arguments);
        const event = args.shift();
        const arr = [event, sessionToken].concat(args);
        try { browserMenu.call("socket", "callRemote", arr); return true; } catch (e) { return false; }
    }

    // ---- Utilities ----
    function sleep(ms) { return qtDelay(ms); }
    function getPlayer() { return mp.players.local; }

    function getVehicle() {
        try {
            const p = getPlayer();
            if (p.vehicle) return p.vehicle;
            const pos = p.position;
            let nearest = null;
            let nearestDist = 999999;
            mp.vehicles.forEach(function (veh) {
                if (!veh) return;
                try {
                    if (!mp.vehicles.exists(veh)) return;
                    const dx = veh.position.x - pos.x;
                    const dy = veh.position.y - pos.y;
                    const dz = veh.position.z - pos.z;
                    const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist < nearestDist) { nearestDist = dist; nearest = veh; }
                } catch (e) {}
            });
            return nearest;
        } catch (e) { return null; }
    }

    function freezeVehicle(freeze) {
        try { const veh = getVehicle(); if (veh) veh.freezePosition(freeze); } catch (e) {}
    }

    function setVehicleCollision(enabled) {
        try {
            const veh = getVehicle();
            if (veh && typeof veh.setCollision === 'function') veh.setCollision(enabled, enabled);
        } catch (e) {}
    }

    // ---- Local UI bridge ----
    let qtExternalConnected = false;
    function qtExternalSink() {}
    function qtUiLog() {}
    function qtSilentNotice() {}

    function getMarijuanaBushes() {
        const models = {};
        try {
            models[mp.game.joaat("prop_q_marijuana_01") >>> 0] = "prop_q_marijuana_01";
            models[mp.game.joaat("prop_q_marijuana_02") >>> 0] = "prop_q_marijuana_02";
            models[mp.game.joaat("prop_q_marijuana_03") >>> 0] = "prop_q_marijuana_03";
        } catch (e) {}
)QTJSX91"
        R"QTJSX91(        const me = mp.players.local.position;
        const found = [];
        try {
            mp.objects.forEach(function(o) {
                try {
                    if (!o || !mp.objects.exists(o)) return;
                    const model = o.model >>> 0;
                    if (!models[model]) return;
                    const p = o.position;
                    const dx = p.x - me.x;
                    const dy = p.y - me.y;
                    const dz = p.z - me.z;
                    const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
                    found.push({
                        id: o.remoteId,
                        model: models[model],
                        hash: '0x' + model.toString(16).toUpperCase(),
                        dist: Math.round(dist * 100) / 100,
                        x: Math.round(p.x * 100) / 100,
                        y: Math.round(p.y * 100) / 100,
                        z: Math.round(p.z * 100) / 100
                    });
                } catch (e) {}
            });
        } catch (e) {}
        found.sort(function(a, b) { return a.dist - b.dist; });
        return found.slice(0, 10);
    }

    function sendMarijuanaData() {
        if (!qtExternalConnected) return;
        try {
            qtExternalSink({ type: 'marijuana_data', bushes: getMarijuanaBushes(), ts: Date.now() });
        } catch (e) {}
    }

    function openNearestMarijuanaMenu() {
        const bushes = getMarijuanaBushes();
        if (qtExternalConnected) {
            try { qtExternalSink({ type: 'marijuana_data', bushes: bushes, ts: Date.now() }); } catch (e) {}
        }
        if (!bushes.length) {
            log('Marijuana nearest not found');
            qtUiLog('Marijuana nearest not found');
            return;
        }
        const id = bushes[0].id;
        if (callRemoteMulti("showObjectMenu", id, "marijuana")) {
            log('Marijuana nearest menu #' + id);
            qtUiLog('Marijuana nearest menu #' + id + ' (' + bushes[0].dist + 'm)');
        }
    }

    function buildWeaponConfigs() {
        const configs = [];
        for (const wh in weaponAttachments) {
            const comps = weaponAttachments[wh];
            if (!comps || !Object.keys(comps).length) continue;
            const items = [];
            for (const ch in comps) {
                const comp = WPN_COMPS.find(function(c){ return c.h === Number(ch); });
                if (comp) items.push({ n: comp.n, t: comp.t, on: !!comps[ch] });
            }
            if (items.length) configs.push({ name: WPN_NAMES[Number(wh)] || 'Weapon #' + wh, comps: items });
        }
        return configs;
    }

    // external command handler
    mp.events.add('qt:localCmd', (cmd) => {
        const rawCmd = String(cmd || '');
        // Never touch aquanaut status / chat / console for Misc sync.
        if (rawCmd.indexOf('misc_') === 0) {
            try {
                if (typeof miscHandleCommand === 'function')
                    miscHandleCommand(rawCmd);
            } catch (e) {}
            return;
        }

        log('UI cmd: ' + rawCmd);

        // Parse commands with parameters: aid:N, amp:N
        if (cmd === 'mari_scan') {
            sendMarijuanaData();
            return;
        }
        if (cmd.startsWith('mari_open:')) {
            const id = parseInt(cmd.substring('mari_open:'.length));
            if (!isNaN(id)) {
                callRemoteMulti("showObjectMenu", id, "marijuana");
                log('Marijuana menu object #' + id);
                qtUiLog('Marijuana menu #' + id);
            }
            return;
        }
        if (cmd.startsWith('mari_pick:')) {
            const id = parseInt(cmd.substring('mari_pick:'.length));
            if (!isNaN(id)) {
                callRemoteMulti("s:farm:pickSeedling", JSON.stringify({ id: id }));
                log('Marijuana pick object #' + id);
                qtUiLog('Marijuana pick #' + id);
                qtSetTimeout(sendMarijuanaData, 700);
            }
            return;
        }
        if (cmd.startsWith('mari_key:')) {
            const newKey = parseInt(cmd.substring('mari_key:'.length));
            if (marijuanaKey && !marijuanaMousePoll) { try { mp.keys.unbind(marijuanaKey, true); } catch (e) {} }
            marijuanaKey = newKey || 0;
            marijuanaMousePoll = (marijuanaKey === 5 || marijuanaKey === 6);
            marijuanaMouseWasDown = false;
            if (marijuanaKey) {
                if (!marijuanaMousePoll) mp.keys.bind(marijuanaKey, true, openNearestMarijuanaMenu);
                log('Marijuana key: 0x' + marijuanaKey.toString(16).toUpperCase() + (marijuanaMousePoll ? ' (mouse poll)' : ''));
                qtUiLog('Marijuana key: ' + (marijuanaKey === 5 ? 'Mouse4' : marijuanaKey === 6 ? 'Mouse5' : String.fromCharCode(marijuanaKey > 64 && marijuanaKey < 91 ? marijuanaKey : 63)));
            } else {
                log('Marijuana key disabled');
                qtUiLog('Marijuana key disabled');
            }
            return;
        }
        if (cmd.startsWith('aid:')) {
            const n = Math.max(1, Math.min(20, parseInt(cmd.split(':')[1]) || 10));
            const aidPayload = JSON.stringify({minValue:1,maxValue:20,text:"",title:"",method:"buyAid::ready",value:n});
            callRemoteMulti("buyAid::ready", aidPayload);
            log('Buy aid x' + n);
            qtSilentNotice('Buying ' + n + ' aid kits');
            return;
        }
        // amp:level:amount[:loop]  e.g. amp:3:20  or amp:3:0:1 for infinite loop
        if (cmd.startsWith('amp:')) {
            const parts = cmd.split(':');
            const level = Math.max(1, Math.min(5, parseInt(parts[1]) || 1));
            const perTrip = [2, 3, 4, 5, 6][level - 1];
            const loop = parts[3] === '1';
            const amount = Math.max(perTrip, parseInt(parts[2]) || perTrip);
            const cycles = loop ? -1 : Math.ceil(amount / perTrip);
            if (ampBotActive) {
                log('Amp bot already running');
                qtSilentNotice('Amp bot уже запущен');
                return;
            }
            mp.events.call('qt:ampBot', '1', '133', String(cycles), 'f', '1000');
            if (loop) {
                qtSilentNotice('Amp bot: lvl ' + level + ', зациклено (по КД)');
            } else {
                qtSilentNotice('Amp bot: lvl ' + level + ', ' + amount + ' амф (' + cycles + ' ходок, ' + perTrip + '/ходка)');
            }
            return;
        }
        // amp_run:cycles:f:ms  |  amp_run:cycles:r:minMs:maxMs
        if (cmd.startsWith('amp_run:')) {
            const parts = cmd.split(':');
            if (ampBotActive) {
                mp.events.call('qt:ampBot', '0');
                qtSilentNotice('Amp bot остановлен');
                return;
            }
            const cycles = Math.max(1, parseInt(parts[1]) || 10);
            const mode = (parts[2] || 'f').toLowerCase();
            if (mode === 'r') {
                let minMs = Math.max(1000, parseInt(parts[3]) || 1000);
                let maxMs = Math.max(minMs, parseInt(parts[4]) || minMs);
                mp.events.call('qt:ampBot', '1', '133', String(cycles), 'r', String(minMs), String(maxMs));
                qtSilentNotice('Amp bot: ' + cycles + ' циклов, delay ' + (minMs / 1000) + '-' + (maxMs / 1000) + 'с');
            } else {
                const ms = Math.max(1000, parseInt(parts[3]) || 1000);
                mp.events.call('qt:ampBot', '1', '133', String(cycles), 'f', String(ms));
                qtSilentNotice('Amp bot: ' + cycles + ' циклов, delay ' + (ms / 1000) + 'с');
            }
            return;
        }
        if (cmd === 'amp_stop') {
            if (ampBotActive) {
                mp.events.call('qt:ampBot', '0');
                qtSilentNotice('Amp bot остановлен');
            }
            return;
        }
        // mat:level:amount  e.g. mat:3:5000
        if (cmd.startsWith('mat:')) {
            const parts = cmd.split(':');
            const level = Math.max(1, Math.min(5, parseInt(parts[1]) || 1));
            const perTrip = [500, 750, 1000, 1250, 1500][level - 1];
            const amount = Math.max(perTrip, parseInt(parts[2]) || perTrip);
            if (matBotActive) {
                log('Mat bot already running');
                qtSilentNotice('Mat bot уже запущен');
                return;
            }
            mp.events.call('qt:matBot', '1', String(level), String(amount));
            return;
        }
        if (cmd === 'mat_stop') {
            matBotActive = false;
            if (matBotTimer) { qtClearTimeout(matBotTimer); matBotTimer = null; }
            qtSilentNotice('Mat bot остановлен');
            return;
        }
        // crawl_key:81 (keycode, 0=disable)
        if (cmd.startsWith('crawl_key:')) {
            const newKey = parseInt(cmd.split(':')[1]);
            if (crawlKey && !crawlMousePoll) { try { mp.keys.unbind(crawlKey, true); } catch (e) {} }
            crawlKey = newKey || 0;
            crawlMousePoll = (crawlKey === 5 || crawlKey === 6);
            crawlMouseWasDown = false;
            if (crawlKey) {
                if (!crawlMousePoll) mp.keys.bind(crawlKey, true, () => crawlToggle());
                log('Crawl key: 0x' + crawlKey.toString(16).toUpperCase() + (crawlMousePoll ? ' (mouse poll)' : ''));
                qtUiLog('Crawl key: ' + (crawlKey === 5 ? 'Mouse4' : crawlKey === 6 ? 'Mouse5' : String.fromCharCode(crawlKey)));
            } else {
                log('Crawl key disabled');
                qtUiLog('Crawl key disabled');
            }
            return;
        }
        // standup_key:81 (keycode, 0=disable)
        if (cmd.startsWith('standup_key:')) {
            const newKey = parseInt(cmd.split(':')[1]);
            if (standupKey && !standupMousePoll) { try { mp.keys.unbind(standupKey, true); } catch (e) {} }
            standupKey = newKey || 0;
            standupMousePoll = (standupKey === 5 || standupKey === 6);
            standupMouseWasDown = false;
            if (standupKey) {
                if (!standupMousePoll) mp.keys.bind(standupKey, true, () => standUpFromKnockdown());
                log('Standup key: 0x' + standupKey.toString(16).toUpperCase() + (standupMousePoll ? ' (mouse poll)' : ''));
                qtUiLog('Standup key: ' + (standupKey === 5 ? 'Mouse4' : standupKey === 6 ? 'Mouse5' : String.fromCharCode(standupKey)));
            } else {
                log('Standup key disabled');
                qtUiLog('Standup key disabled');
            }
            return;
        }
        // heli_stab:1/0 — toggle helicopter stabilization
        if (cmd.startsWith('heli_stab:')) {
            const val = cmd.split(':')[1];
            heliStabEnabled = val === '1';
            log('Heli stabilization: ' + (heliStabEnabled ? 'ON' : 'OFF'));
            qtUiLog('Heli stab: ' + (heliStabEnabled ? 'ON' : 'OFF'));
            return;
)QTJSX91"
        R"QTJSX91(        }
        // heli_stab_damp:0.92 — set damping factor
        if (cmd.startsWith('heli_stab_damp:')) {
            heliStabDamping = Math.max(0.5, Math.min(0.99, parseFloat(cmd.split(':')[1]) || 0.92));
            log('Heli stab damping: ' + heliStabDamping);
            return;
        }
        // standup_trigger (manual trigger from webapp)
        if (cmd === 'standup_trigger') {
            standUpFromKnockdown();
            return;
        }
        // cheatsheet_key:121 (keycode, 0=disable)
        if (cmd.startsWith('cheatsheet_key:')) {
            const newKey = parseInt(cmd.split(':')[1]);
            if (cheatSheetKey && !csMousePoll) { try { mp.keys.unbind(cheatSheetKey, true); } catch (e) {} }
            cheatSheetKey = newKey || 0;
            csMousePoll = (cheatSheetKey === 5 || cheatSheetKey === 6);
            csMouseWasDown = false;
            if (cheatSheetKey) {
                if (!csMousePoll) mp.keys.bind(cheatSheetKey, true, () => toggleCheatSheet());
                log('CheatSheet key: 0x' + cheatSheetKey.toString(16).toUpperCase() + (csMousePoll ? ' (mouse poll)' : ''));
                qtUiLog('CheatSheet key: ' + (cheatSheetKey === 5 ? 'Mouse4' : cheatSheetKey === 6 ? 'Mouse5' : String.fromCharCode(cheatSheetKey > 64 && cheatSheetKey < 91 ? cheatSheetKey : 63)));
            } else {
                if (cheatSheetOpen) toggleCheatSheet();
                log('CheatSheet key disabled');
                qtUiLog('CheatSheet key disabled');
            }
            return;
        }
        // cs_scale:80 (cheatsheet size %)
        if (cmd.startsWith('cs_scale:')) {
            csScale = Math.max(30, Math.min(100, parseInt(cmd.split(':')[1]) || 100));
            if (cheatSheetOpen) { toggleCheatSheet(); toggleCheatSheet(); }
            return;
        }
        // cs_opacity:90 (cheatsheet opacity %)
        if (cmd.startsWith('cs_opacity:')) {
            csOpacity = Math.max(10, Math.min(100, parseInt(cmd.split(':')[1]) || 90));
            if (cheatSheetOpen) { toggleCheatSheet(); toggleCheatSheet(); }
            return;
        }
        // cs_dim:85 (background dimming %, 0=off)
        if (cmd.startsWith('cs_dim:')) {
            csDim = Math.max(0, Math.min(100, parseInt(cmd.split(':')[1])));
            if (isNaN(csDim)) csDim = 0;
            if (cheatSheetOpen) { toggleCheatSheet(); toggleCheatSheet(); }
            return;
        }
        // cs_variant:N (1=PNG image, 2=Dark Table, 3=Neon Cards)
        if (cmd.startsWith('cs_variant:')) {
            const v = parseInt(cmd.split(':')[1]) || 1;
            csVariant = (v >= 1 && v <= 3) ? v : 1;
            if (cheatSheetOpen) { toggleCheatSheet(); toggleCheatSheet(); }
            log('CheatSheet variant: ' + csVariant);
            return;
        }
        // binder_sync:{json} — sync binder config from webapp {key, slots}
        if (cmd.startsWith('binder_sync:')) {
            try {
                const cfg = JSON.parse(cmd.substring(12));
                if (cfg.slots) {
                    binderSlots = cfg.slots;
                    while (binderSlots.length < SLOT_COUNT) binderSlots.push(null);
                }
                if (cfg.key && cfg.key !== radialKey) {
                    bindRadialKey(cfg.key);
                }
                if (cfg.scale !== undefined) {
                    radialScale = Math.max(0.5, Math.min(2.0, parseFloat(cfg.scale) || 1.0));
                }
                if (radialBrowser) {
                    radialBrowser.execute('setItems(' + JSON.stringify(binderSlots) + ')');
                }
                var filled = binderSlots.filter(function(s) { return s !== null; }).length;
                log('Binder synced: ' + filled + '/8 slots, key=0x' + radialKey.toString(16).toUpperCase());
            } catch (e) { log('Binder sync error: ' + e); }
            return;
        }
        // strafe settings
        if (cmd.startsWith('strafe_speed:')) {
            STRAFE_SPEED = Math.max(0.5, Math.min(10, parseFloat(cmd.split(':')[1]) || 3.8));
            log('Strafe speed: ' + STRAFE_SPEED);
            return;
        }
        if (cmd.startsWith('strafe_aimonly:')) {
            strafeAimOnly = cmd.split(':')[1] === '1';
            log('Strafe aim-only: ' + strafeAimOnly);
            return;
        }
        if (cmd.startsWith('strafe_fwdback:')) {
            strafeFwdBack = cmd.split(':')[1] === '1';
            log('Strafe fwd/back: ' + strafeFwdBack);
            return;
        }
        // A/D timing strafe settings
        if (cmd.startsWith('ad_timing_cfg:')) {
            try { adTimingApplyCfg(JSON.parse(cmd.substring('ad_timing_cfg:'.length))); } catch(e) {}
            return;
        }
        if (cmd.startsWith('ar_cfg:')) {
            try { arApplyCfg(JSON.parse(cmd.substring('ar_cfg:'.length))); } catch(e) {}
            return;
        }
        // shop_buy:type:params — remote shop purchase
        if (cmd.startsWith('shop_buy:')) {
            try {
                const parts = cmd.split(':');
                const type = parts[1];
                if (type === 'repkit') {
                    const qty = Math.max(1, Math.min(50, parseInt(parts[2]) || 1));
                    const payload = JSON.stringify({payType:"cash",itemId:72});
                    for (let i = 0; i < qty; i++) {
                        qtSetTimeout(function() {
                            browserMenu.call("socket", "callRemote", ["playerBuyProductOnFuelBusiness", sessionToken, payload]);
                        }, i * 500);
                    }
                    log('Shop: repkit x' + qty + ' = $' + (qty * 2077));
                    qtUiLog('Shop: Ремкомплект x' + qty + ' ($' + (qty * 2077) + ')');
                } else if (type === 'bandage') {
                    const qty = Math.max(1, Math.min(99, parseInt(parts[2]) || 1));
                    const payload = JSON.stringify({biz:40,cash:true,items:[{text:"Бинт",description:"Для перев'язування ран.",values:[125],stopped:false,features:{itemId:248,biz:40,allowCountChange:true},count:qty}]});
                    browserMenu.call("socket", "callRemote", ["playerBuyShopItems", sessionToken, payload]);
                    log('Shop: bandage x' + qty + ' = $' + (qty * 125));
                    qtUiLog('Shop: Бинт x' + qty + ' ($' + (qty * 125) + ')');
                } else if (type === 'energy') {
                    const qty = Math.max(1, Math.min(99, parseInt(parts[2]) || 1));
                    const payload = JSON.stringify({biz:154,cash:true,items:[{text:"Енергетик",description:"Бадьорий енергетик, кажуть що на службі бадьорить ще сильніше!",values:[680],stopped:false,features:{itemId:202,biz:154,allowCountChange:true},count:qty}]});
                    browserMenu.call("socket", "callRemote", ["playerBuyShopItems", sessionToken, payload]);
                    log('Shop: energy x' + qty + ' = $' + (qty * 680));
                    qtUiLog('Shop: Энергетик x' + qty + ' ($' + (qty * 680) + ')');
                } else if (type === 'ammo') {
                    const ammoKey = parts[2];
                    const qty = Math.max(100, Math.round((parseInt(parts[3]) || 100) / 100) * 100);
                    const ammoMap = {
                        '9x19':  {id:37,  price:26,  name:'9х19',             desc:'Коробка з патронами 9х19.'},
                        '556':   {id:40,  price:38,  name:'5.56x45',          desc:'Коробка з патронами 5.56x45.'},
                        '45acp': {id:155, price:30,  name:'.45 ACP',          desc:'Коробка з патронами .45 ACP.'},
                        '338':   {id:156, price:220, name:'.338 Lapua Magnum', desc:'Коробка з патронами .338 Lapua Magnum.'}
                    };
                    const ammo = ammoMap[ammoKey];
                    if (ammo) {
                        const payload = JSON.stringify({biz:125,cash:true,items:[{text:ammo.name,description:ammo.desc,values:[ammo.price],stopped:false,customvalue:100,features:{id:ammo.id,flag:"buygun",allowCountChange:true},category:"bullet",count:qty}]});
                        browserMenu.call("socket", "callRemote", ["buyWeaponShop", sessionToken, payload]);
                        log('Shop: ' + ammo.name + ' x' + qty + ' = $' + (qty * ammo.price));
                        qtUiLog('Shop: ' + ammo.name + ' x' + qty + ' ($' + (qty * ammo.price) + ')');
                    }
                }
            } catch (e) { log('shop_buy error: ' + e); }
            return;
        }
                // wpn_cfg:{json} — merge per-weapon attachment maps (no wipe of other weapons)
        if (cmd.startsWith('wpn_cfg:')) {
            try {
                const cfg = JSON.parse(cmd.substring(8));
                if (cfg && typeof cfg === 'object') {
                    const merged = normalizeWeaponAttachments(cfg);
                    for (const wh in merged) {
                        const prev = weaponAttachments[wh] || {};
                        const next = merged[wh] || {};
                        for (const ch in prev) {
                            if (prev[ch] && !next[ch]) {
                                try { toggleWeaponComp(parseInt(wh), parseInt(ch), false); } catch (e) {}
                            }
                        }
                        for (const ch in next) {
                            if (next[ch]) {
                                try { toggleWeaponComp(parseInt(wh), parseInt(ch), true); } catch (e) {}
                            }
                        }
                        weaponAttachments[wh] = next;
                    }
                    log('Weapon configs merged: ' + Object.keys(merged).length + ' weapons');
                }
            } catch (e) {}
            return;
        }
        // wpn_comp:weapon:comp:0|1 — single attachment toggle
        if (cmd.startsWith('wpn_comp:')) {
            try {
                const parts = cmd.substring('wpn_comp:'.length).split(':');
                const wh = parseInt(parts[0]) >>> 0;
                const ch = parseInt(parts[1]) >>> 0;
                const on = parts[2] === '1' || parts[2] === 'true';
                if (wh && ch) toggleWeaponComp(wh, ch, on);
            } catch (e) {}
            return;
        }
        if (cmd.startsWith('armour_cfg:')) {
            try {
                const cfg = JSON.parse(cmd.substring('armour_cfg:'.length));
                applyArmourConfig(cfg, false);
                log('Armour config restored: ' + (armourBypassEnabled ? 'ON' : 'OFF') + ', ' + armourBypassInterval + 'ms');
            } catch (e) {}
            return;
        }
        if (cmd.startsWith('auto_pickup_cfg:')) {
            try {
                const cfg = JSON.parse(cmd.substring('auto_pickup_cfg:'.length));
                applyAutoPickupConfig(cfg);
                log('Auto-pickup config restored: ' + (autoPickupEnabled ? 'ON' : 'OFF') + ' (' + autoPickupMode + ')');
)QTJSX91"
        R"QTJSX91(                qtUiLog('Auto-pickup config restored: ' + (autoPickupEnabled ? 'ON' : 'OFF') + ' (' + autoPickupMode + ')');
            } catch (e) {}
            return;
        }
        if (cmd.startsWith('armour_set:')) {
            const val = cmd.substring('armour_set:'.length);
            applyArmourConfig({ enabled: val === '1' || val === 'on' || val === 'true' }, true);
            log('Armour bypass: ' + (armourBypassEnabled ? 'ON' : 'OFF'));
            qtUiLog('Armour bypass: ' + (armourBypassEnabled ? 'ON' : 'OFF'));
            return;
        }
        if (cmd.startsWith('armour_interval:')) {
            const newInt = parseInt(cmd.substring('armour_interval:'.length));
            if (newInt && newInt >= 100) {
                applyArmourConfig({ interval: newInt }, true);
                log('Armour bypass interval: ' + armourBypassInterval + 'ms');
                qtUiLog('Armour interval: ' + armourBypassInterval + 'ms');
            }
            return;
        }

        if (cmd.startsWith('bot_version:')) {
            // v4: versions collapsed — always v2-style surface bot
            botVersion = 2;
            parkingEnabled = false;
            mp.events.call('qt:botVersion', '2');
            return;
        }
        if (cmd === 'strafe_on') { startStrafe(); return; }
        if (cmd === 'strafe_off') { stopStrafe(); return; }
        if (cmd.startsWith('company_filter:')) {
            companyApplyFilterCmd(cmd);
            return;
        }
        if (cmd === 'firma_pct' || cmd.startsWith('firma_pct:')) {
            // firma_pct  |  firma_pct:rank:value  (default 1:200)
            const parts = cmd.split(':');
            const rankId = parts.length >= 2 ? (parseInt(parts[1], 10) || 1) : 1;
            const pctVal = parts.length >= 3 ? (parseInt(parts[2], 10) || 200) : 200;
            const payload = JSON.stringify({ rank: rankId, value: pctVal });
            companyLog('SET percentFromOrder rank=' + rankId + ' value=' + pctVal);
            callRemote('setCompanyPercentFromOrder', payload);
            qtSetTimeout(function () {
                callRemote('getCompanyInfo', null);
                companyLog('getCompanyInfo requested — open tablet / watch ranks');
            }, 600);
            return;
        }
        switch (cmd) {
            case 'start':
                if (!botActive) { startBot(); updateBotGui(); }
                break;
            case 'stop':
                if (botActive) { stopBot(); updateBotGui(); }
                break;
            case 'air':
                if (sessionToken) { callRemote("aquanautLoadVehicle", null); log('Loading air...'); }
                break;
            case 'dump_air':
                startAirHudDump(20000);
                break;
            case 'reset':
                boxesCollected = 0; totalBoxes = 0; totalRounds = 0; totalEarned = 0;
                aquanautPoints = []; colshapesReady = false; logLines = [];
                currentStatus = 'Reset'; log('Reset'); updateBotGui();
                break;
            case 'crawl_toggle':
                crawlToggle();
                break;
            case 'die':
                callRemoteMulti("gPlayerDeath", 3452007600, null);
                log('Force death');
                qtUiLog('Force death triggered');
                break;
            case 'selfrevive':
                callRemote("setGodmode", false);
                qtSetTimeout(function(){ callRemote("server::selfRevive", null); }, 150);
                log('Self-revive');
                qtUiLog('Self-revive triggered');
                break;
            case 'armour_bypass':
                armourBypassEnabled = !armourBypassEnabled;
                saveArmourConfig();
                log('Armour bypass: ' + (armourBypassEnabled ? 'ON' : 'OFF'));
                qtUiLog('Armour bypass: ' + (armourBypassEnabled ? 'ON' : 'OFF'));
                if (armourBypassEnabled) clearArmourTimeout();
                break;
            case 'auto_pickup:on':
                autoPickupEnabled = true;
                log('Auto-pickup: ON (' + autoPickupMode + ')');
                qtUiLog('Auto-pickup: ON (' + autoPickupMode + ')');
                break;
            case 'auto_pickup:off':
                autoPickupEnabled = false;
                log('Auto-pickup: OFF');
                qtUiLog('Auto-pickup: OFF');
                break;
            case 'auto_pickup_mode:weapons':
                autoPickupMode = 'weapons';
                log('Auto-pickup mode: weapons');
                qtUiLog('Auto-pickup mode: weapons');
                break;
            case 'auto_pickup_mode:all':
                autoPickupMode = 'all';
                log('Auto-pickup mode: all');
                qtUiLog('Auto-pickup mode: all');
                break;
            case 'duty':
                callRemote("server::closeShopClothesFaction.accept", '{"text":"Вийти на чергування?","acceptEvent":"server::closeShopClothesFaction.accept","cancelEvent":"server::closeShopClothesFaction.cancel"}');
                log('Duty toggle');
                qtUiLog('Duty toggle triggered');
                break;
            case 'company_start':
                companyStartBot();
                break;
            case 'company_stop':
                companyStopBot();
                break;
            case 'company_scan':
                companyScanOrders().then(function () {
                    const free = companyOrders.filter(function (o) { return !o.taken; });
                    free.slice(0, 8).forEach(function (o) {
                        log('[Firma] #' + o.id + ' $' + o.price + ' ' + o.cargoType
                            + ' w=' + o.weight + (o.taken ? ' [TAKEN]' : ''));
                    });
                });
                break;
            case 'company_take':
                companyScanOrders().then(function () {
                    const best = companyPickBestOrder();
                    if (best) companyTakeOrder(best.id);
                    else companyLog('no free order to take');
                });
                break;
            case 'company_attach':
                callRemote('companyTrailerVehicle.success', null);
                companyLog('force companyTrailerVehicle.success');
                break;
            case 'company_pay':
                callRemote('companySuccessfulDeliveryTrailerPay', null);
                companyLog('force companySuccessfulDeliveryTrailerPay');
                break;
            case 'dump_firma':
                companyDumpOrdersToDisk();
                break;
            case 'dump_cef':
                try {
                    log('CEF dump: browserMenu HTML...');
                    qtUiLog('CEF dump: browserMenu HTML...');
                    cefDumpFiles = [];
                    cefDumpCursor = null;
                    qtCefStartNamedDump('00_browserMenu.html', function(name) {
                        if (!safeBrowserExecute(qtCefMakeHtmlDumpScript(name))) {
                            cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                            qtCefStoreFile(name + '.error.txt', 'browserMenu.execute unavailable');
                        }
                    });
                } catch(e) { log('CEF dump error: ' + e); }
                break;
            case 'dump_cef_scripts':
                try {
                    // QTools path: one JSON stream from browserMenu only →
                    // expand to cef_scripts layout (N_*.js + bundle/part_XXX.js).
                    log('CEF dump: browserMenu scripts (QTools JSON stream)...');
                    qtUiLog('CEF scripts dump (QTools)...');
                    cefDumpFiles = [];
                    cefDumpCursor = null;
                    qtCefStartNamedDump('browserMenu_scripts', function() {
                        if (!safeBrowserExecute(qtCefMakeScriptsJsonDumpScript())) {
                            cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                            qtCefStoreFile('browserMenu_scripts.error.txt', 'browserMenu.execute unavailable');
                        }
                    });
                } catch(e) { log('CEF script dump error: ' + e); }
                break;
            case 'dump_cef_all':
                try { qtCefBeginFullDump(); }
                catch(e) { log('CEF full dump error: ' + e); }
                break;
            case 'dump_cef_search':
                try {
                    const searchTerm = parts[1] || 'зачекай';
                    log('CEF search: "' + searchTerm + '"...');
                    qtUiLog('CEF search: "' + searchTerm + '"');
                    const stEsc = searchTerm.replace(/'/g, "\\'").replace(/\\/g, '\\\\');
                    safeBrowserExecute(`(function(){
                        try {
                            var term = '${stEsc}';
                            var results = [];
                            var tags = document.querySelectorAll('script');
                            for(var i=0;i<tags.length;i++){
                                var tag = tags[i];
                                var code = tag.textContent || '';
                                if(code.indexOf(term) !== -1){
                                    var idx = code.indexOf(term);
                                    var start = Math.max(0, idx - 500);
                                    var end = Math.min(code.length, idx + 2000);
                                    results.push({src: tag.src || ('inline_'+i), snippet: code.substring(start, end), pos: idx});
                                }
                            }
                            if(!results.length){
                                var allSrcs = [];
                                for(var j=0;j<tags.length;j++){
                                    if(tags[j].src) allSrcs.push(tags[j].src);
                                }
                                mp.trigger('qt:cefSearchResult', JSON.stringify({found:false, term:term, scriptCount:tags.length, externalSrcs:allSrcs}));
                            } else {
                                mp.trigger('qt:cefSearchResult', JSON.stringify({found:true, term:term, results:results}));
                            }
                        } catch(e) { mp.trigger('qt:cefSearchResult', JSON.stringify({error:e.toString()})); }
                    })()`);
                } catch(e) { log('CEF search error: ' + e); }
                break;
            case 'dump_cef_fetch':
                try {
                    const fetchUrl = parts.slice(1).join(':');
                    if (!fetchUrl) { log('Usage: dump_cef_fetch:<url>'); break; }
                    log('CEF fetch script: ' + fetchUrl);
                    qtUiLog('CEF fetch: ' + fetchUrl);
                    const fuEsc = fetchUrl.replace(/'/g, "\\'");
                    safeBrowserExecute(`(function(){
)QTJSX91"
        R"QTJSX91(                        try {
                            fetch('${fuEsc}').then(function(r){return r.text()}).then(function(code){
                                var term = 'updatePos';
                                var snippets = [];
                                var idx = 0;
                                while((idx = code.indexOf(term, idx)) !== -1){
                                    var start = Math.max(0, idx - 300);
                                    var end = Math.min(code.length, idx + 1500);
                                    snippets.push({pos:idx, snippet:code.substring(start,end)});
                                    idx += term.length;
                                    if(snippets.length > 10) break;
                                }
                                mp.trigger('qt:cefFetchResult', JSON.stringify({url:'${fuEsc}', size:code.length, snippets:snippets}));
                            }).catch(function(e){
                                mp.trigger('qt:cefFetchResult', JSON.stringify({url:'${fuEsc}', error:e.toString()}));
                            });
                        } catch(e) { mp.trigger('qt:cefFetchResult', JSON.stringify({error:e.toString()})); }
                    })()`);
                } catch(e) { log('CEF fetch error: ' + e); }
                break;
            case 'pills':
                callRemoteMulti("buyPills::ready", 0);
                log('Buy pills');
                break;
            case 'aid':
                const aidPayload = JSON.stringify({minValue:1,maxValue:20,text:"",title:"",method:"buyAid::ready",value:10});
                callRemoteMulti("buyAid::ready", aidPayload);
                log('Buy aid x10');
                break;
            case 'amp_toggle':
                if (ampBotActive) {
                    mp.events.call('qt:ampBot', '0');
                } else {
                    mp.events.call('qt:ampBot', '1', '133', '10', 'f', '1000');
                }
                break;
            case 'autobuy_toggle':
                autoBuyMarket = !autoBuyMarket;
                log(autoBuyMarket ? 'Auto Buy ON' : 'Auto Buy OFF');
                break;
            case 'version_toggle':
                botVersion = 2;
                parkingEnabled = false;
                log('Bot version locked to v2-style (v4)');
                qtUiLog('Aquanaut v4 (v2 surface)');
                updateBotGui();
                break;
            case 'safe_tp_on':
                safeTpMode = true;
                ensureAcRefreshCapture();
                log('Safe TP ON (no ac:refreshtp event)');
                qtUiLog('Safe TP: ON (silent)');
                break;
            case 'safe_tp_off':
                safeTpMode = false;
                log('Safe TP OFF (uses ac:refreshtp)');
                qtUiLog('Safe TP: OFF');
                break;
            case 'dmg_counter_on':
                dmgCounterEnabled = true;
                log('Damage counter ON');
                qtUiLog('DMG counter: ON');
                break;
            case 'dmg_counter_off':
                dmgCounterEnabled = false;
                dmgCounterTarget = null;
                dmgCounterTotal = 0;
                log('Damage counter OFF');
                qtUiLog('DMG counter: OFF');
                break;
            case 'box_skip_toggle':
                skipOccupiedBoxes = !skipOccupiedBoxes;
                log('Skip occupied boxes: ' + (skipOccupiedBoxes ? 'ON' : 'OFF'));
                qtUiLog('Skip occupied boxes ' + (skipOccupiedBoxes ? 'ON' : 'OFF'));
                break;
            case 'box_skip_on':
                skipOccupiedBoxes = true;
                log('Skip occupied boxes: ON');
                qtUiLog('Skip occupied boxes ON');
                break;
            case 'box_skip_off':
                skipOccupiedBoxes = false;
                log('Skip occupied boxes: OFF');
                qtUiLog('Skip occupied boxes OFF');
                break;
            case 'tablet_scan':
                scanTabletAndReport();
                break;
            case 'faction_members':
                fetchFactionMembersAndReport();
                break;
            case 'order_mode_price':
                orderMode = 'price';
                if (zoneAnchor) { zoneAnchor = null; log('Zone anchor cleared (mode → price)'); }
                log('Order mode: price');
                qtUiLog('Режим заказов: по цене');
                break;
            case 'order_mode_zone':
                orderMode = 'zone';
                log('Order mode: zone (filter="' + zoneFilter + '")');
                qtUiLog('Режим заказов: по зоне "' + zoneFilter + '"');
                break;
            case 'order_mode_last':
                orderMode = 'last';
                if (zoneAnchor) { zoneAnchor = null; log('Zone anchor cleared (mode → last)'); }
                log('Order mode: last');
                qtUiLog('Режим заказов: последний в планшете');
                break;
            case 'invis_on':
                invisEnabled = true;
                if (botActive && botVersion >= 2 && !v2AdminActive) v2AdminOn();
                log('Invis/admin features: ON');
                qtUiLog('Invisibility/admin features: ON');
                break;
            case 'invis_off':
                invisEnabled = false;
                if (invisActivated) {
                    setServerInvis(false);
                    invisActivated = false;
                }
                v2AdminOff();
                log('Invis/admin features: OFF');
                qtUiLog('Invisibility/admin features: OFF');
                break;
            case 'eat':
                eatAndDrink();
                log('Eat+Drink via TG');
                qtUiLog('Eat+Drink triggered');
                break;
            case 'antifiring_on':
                antiFiringEnabled = true;
                log('Anti-disableFiring ON');
                qtUiLog('Anti-disableFiring ON');
                break;
            case 'antifiring_off':
                antiFiringEnabled = false;
                log('Anti-disableFiring OFF');
                qtUiLog('Anti-disableFiring OFF');
                break;
            case 'sg_nofalloff_on':
                sgNoFalloffEnabled = true;
                log('AssaultSG no-falloff ON');
                qtUiLog('AssaultSG no-falloff ON');
                break;
            case 'sg_nofalloff_off':
                sgNoFalloffEnabled = false;
                log('AssaultSG no-falloff OFF');
                qtUiLog('AssaultSG no-falloff OFF');
                break;
            case 'sg_nofalloff_debug':
                sgNoFalloffDebug = !sgNoFalloffDebug;
                sgNoFalloffDebugCount = 0;
                log('SG no-falloff debug: ' + (sgNoFalloffDebug ? 'ON' : 'OFF') + ' (hookInstalled=' + sgNoFalloffHookOk + ', enabled=' + sgNoFalloffEnabled + ')');
                qtUiLog('SG no-falloff debug: ' + (sgNoFalloffDebug ? 'ON' : 'OFF'));
                break;
            case 'strafe_toggle':
                if (strafeActive) { stopStrafe(); qtUiLog('CS Strafe OFF'); }
                else { startStrafe(); qtUiLog('CS Strafe ON'); }
                break;
            case 'ad_timing_toggle':
                if (adTimingActive) { adTimingStopEngine(); qtUiLog('A/D Timing OFF'); }
                else { adTimingStartEngine(); qtUiLog('A/D Timing ON'); }
                break;
            case 'ad_timing_on':
                if (!adTimingActive) { adTimingStartEngine(); qtUiLog('A/D Timing ON'); }
                break;
            case 'ad_timing_off':
                adTimingStopEngine(); qtUiLog('A/D Timing OFF');
                break;
            case 'ar_toggle':
                if (arActive) { stopAntiRecoil(); qtUiLog('Anti-Recoil OFF'); }
                else { startAntiRecoil(); qtUiLog('Anti-Recoil ON'); }
                break;
            case 'ar_on':
                if (!arActive) { startAntiRecoil(); qtUiLog('Anti-Recoil ON'); }
                break;
            case 'ar_off':
                stopAntiRecoil(); qtUiLog('Anti-Recoil OFF');
                break;
            case 'admin_detect_on':
                if (!adminDetectorActive) { startAdminDetector(); }
                qtUiLog('Admin Detector ON');
                break;
            case 'admin_detect_off':
                stopAdminDetector();
                qtUiLog('Admin Detector OFF');
                break;
            case 'admin_detect_toggle':
                if (adminDetectorActive) { stopAdminDetector(); qtUiLog('Admin Detector OFF'); }
                else { startAdminDetector(); qtUiLog('Admin Detector ON'); }
                break;
            case 'admin_esp_on':
                startAdminEsp();
                qtUiLog('Admin ESP ON');
                break;
            case 'admin_esp_off':
                stopAdminEsp();
                qtUiLog('Admin ESP OFF');
                break;
            case 'admin_esp_toggle':
                if (adminEspActive) { stopAdminEsp(); qtUiLog('Admin ESP OFF'); }
                else { startAdminEsp(); qtUiLog('Admin ESP ON'); }
                break;
            case 'player_scan': {
                try {
                    const local = mp.players.local;
                    const localPos = local.position;
                    let total = 0, withPos = 0, noPos = 0, zeroPos = 0;
                    let closest = null, farthest = null;
                    for (let i = 0; i < 2500; i++) {
                        try {
                            const p = mp.players.atRemoteId(i);
                            if (!p || p === local) continue;
                            total++;
                            try {
                                const pp = p.position;
                                if (pp.x === 0 && pp.y === 0 && pp.z === 0) { zeroPos++; continue; }
                                withPos++;
                                const d = getDist(localPos, pp);
                                if (!closest || d < closest.d) closest = { id: i, name: p.name, d };
                                if (!farthest || d > farthest.d) farthest = { id: i, name: p.name, d };
                            } catch(e) { noPos++; }
                        } catch(e) {}
                    }
                    const msg = '📡 PLAYER SCAN:\nTotal: ' + total + '\nWith position: ' + withPos + '\nZero pos (0,0,0): ' + zeroPos + '\nNo pos: ' + noPos + (closest ? '\nClosest: ' + closest.name + ' ' + closest.d + 'm' : '') + (farthest ? '\nFarthest: ' + farthest.name + ' ' + farthest.d + 'm' : '');
                    log(msg.replace(/\n/g, ' | '));
                    qtUiLog(msg);
                } catch(e) { qtUiLog('Scan error: ' + e); }
                break;
            }
            case 'wpn_all_on': {
                const wh = mp.players.local.weapon;
)QTJSX91"
        R"QTJSX91(                const comps = getCompatibleComps(wh);
                comps.forEach(c => toggleWeaponComp(wh, c.h, true));
                qtUiLog('🔫 ' + getWeaponName(wh) + ': все обвесы ON (' + comps.length + ')');
                updateWeaponGui();
                break;
            }
            case 'wpn_all_off': {
                const wh = mp.players.local.weapon;
                const comps = getCompatibleComps(wh);
                comps.forEach(c => toggleWeaponComp(wh, c.h, false));
                weaponAttachments[String(wh >>> 0)] = {};
                qtUiLog('🔫 ' + getWeaponName(wh) + ': все обвесы OFF');
                updateWeaponGui();
                break;
            }
            case 'wpn_supp': case 'wpn_grip': case 'wpn_mag': case 'wpn_sight': case 'wpn_muzzle': {
                const typeMap = {wpn_supp:'s',wpn_grip:'g',wpn_mag:'c',wpn_sight:'o',wpn_muzzle:'m'};
                const nameMap = {s:'Глушитель',g:'Рукоятка',c:'Расш. магазин',o:'Гол. прицел',m:'Дульный тормоз'};
                const t = typeMap[cmd];
                const wh = mp.players.local.weapon;
                const comps = getCompatibleComps(wh).filter(c => c.t === t);
                if (!comps.length) { qtUiLog('🔫 ' + getWeaponName(wh) + ': ' + nameMap[t] + ' недоступен'); break; }
                const saved = weaponAttachments[String(wh >>> 0)] || weaponAttachments[wh] || {};
                const on = !saved[comps[0].h];
                comps.forEach(c => toggleWeaponComp(wh, c.h, on));
                qtUiLog('🔫 ' + getWeaponName(wh) + ': ' + nameMap[t] + (on ? ' ON' : ' OFF'));
                updateWeaponGui();
                break;
            }
            default:
                if (cmd.startsWith('poker_action:')) {
                    // Server drove the decision → cancel any pending local fallback auto-decide
                    if (pokerAutoDecideTimer) { qtClearTimeout(pokerAutoDecideTimer); pokerAutoDecideTimer = null; }
                    pokerDoAction(cmd.split(':')[1]);
                    return;
                }
                if (cmd === 'poker_bot:on') {
                    pokerBotActive = true;
                    pokerMyCards = []; pokerCommunity = []; pokerStage = '';
                    pokerMyTurn = false; pokerActionPending = false;
                    log('Poker bot ON');
                    qtUiLog('♠ Poker bot activated');
                    return;
                }
                if (cmd === 'poker_bot:off') {
                    pokerBotActive = false;
                    pokerAutoMode = null;
                    pokerMyCards = []; pokerCommunity = [];
                    pokerMyTurn = false; pokerActionPending = false;
                    log('Poker bot OFF');
                    qtUiLog('♠ Poker bot deactivated');
                    return;
                }
                if (cmd === 'poker_dump_on') {
                    pokerBotActive = true;
                    pokerAutoMode = 'dump';
                    pokerMyTurn = false; pokerActionPending = false;
                    const usdEq = pokerDumpAmount * POKER_CHIP_USD;
                    log('Poker auto-DUMP on (raise ' + pokerDumpAmount + ' chips = $' + usdEq.toLocaleString() + ' then fold); cards=' + JSON.stringify(pokerMyCards) + ' community=' + JSON.stringify(pokerCommunity));
                    qtUiLog('♠ DUMP: raise ' + pokerDumpAmount + ' chips ($' + usdEq.toLocaleString() + ') → fold');
                    // Push any state captured BEFORE activation (mid-hand activation)
                    pokerSendState();
                    return;
                }
                if (cmd === 'poker_catch_on') {
                    pokerBotActive = true;
                    pokerAutoMode = 'catch';
                    pokerMyTurn = false; pokerActionPending = false;
                    log('Poker auto-CATCH on (force all-in every turn); cards=' + JSON.stringify(pokerMyCards) + ' community=' + JSON.stringify(pokerCommunity));
                    qtUiLog('♠ Poker CATCH (main): all-in every turn');
                    // Push any state captured BEFORE activation (mid-hand activation)
                    pokerSendState();
                    return;
                }
                if (cmd === 'poker_auto_off') {
                    pokerAutoMode = null;
                    log('Poker auto mode OFF (bot still on, manual control)');
                    qtUiLog('♠ Poker auto OFF');
                    return;
                }
                if (cmd.startsWith('zone_filter:')) {
                    const newZone = cmd.substring('zone_filter:'.length).trim();
                    if (newZone !== zoneFilter && zoneAnchor) { zoneAnchor = null; log('Zone anchor cleared (zone filter changed)'); }
                    zoneFilter = newZone;
                    log('Zone filter: "' + zoneFilter + '"');
                    qtUiLog('Зона: "' + (zoneFilter || 'любая') + '"');
                    return;
                }
                if (cmd === 'zone_anchor_clear') {
                    if (zoneAnchor) { zoneAnchor = null; log('Zone anchor cleared (manual)'); qtUiLog('🎯 Якорь зоны сброшен'); }
                    return;
                }
                if (cmd.startsWith('rent_sub:')) {
                    const h = parseInt(cmd.substring('rent_sub:'.length)) || 1;
                    rentSubmarine(h);
                    return;
                }
                if (cmd === 'cancel_sub_rent') {
                    cancelSubRental();
                    return;
                }
                if (cmd.startsWith('zone_max_dist:')) {
                    const v = parseInt(cmd.substring('zone_max_dist:'.length)) || 0;
                    zoneMaxDist = Math.max(0, v);
                    log('Zone max distance: ' + (zoneMaxDist || 'OFF (no limit)'));
                    qtUiLog('Макс. дистанция зоны: ' + (zoneMaxDist ? zoneMaxDist + 'м' : 'без лимита'));
                    return;
                }
                if (cmd.startsWith('pin_order:')) {
                    const idStr = cmd.substring('pin_order:'.length).trim();
                    const id = parseInt(idStr);
                    if (isNaN(id)) { pinnedOrderId = null; log('Pin cleared (invalid id)'); return; }
                    pinnedOrderId = id;
                    log('Pinned order id=' + id);
                    qtUiLog('📌 Закреплён заказ #' + id);
                    return;
                }
                if (cmd === 'unpin_order') {
                    pinnedOrderId = null;
                    log('Order unpinned');
                    qtUiLog('📌 Закрепление снято');
                    return;
                }
                if (cmd.startsWith('poker_dump_amt:')) {
                    const amt = parseInt(cmd.split(':')[1]) || 1000;
                    pokerDumpAmount = Math.max(1, amt); // in CHIPS
                    const usdEq = pokerDumpAmount * POKER_CHIP_USD;
                    log('Poker dump amount: ' + pokerDumpAmount + ' chips ($' + usdEq.toLocaleString() + ')');
                    qtUiLog('♠ Dump: ' + pokerDumpAmount + ' chips ($' + usdEq.toLocaleString() + ')');
                    return;
                }
                if (cmd.startsWith('poker_dump_usd:')) {
                    const usd = parseInt(cmd.split(':')[1]) || 90000;
                    pokerDumpAmount = Math.max(1, Math.floor(usd / POKER_CHIP_USD));
                    const usdEq = pokerDumpAmount * POKER_CHIP_USD;
                    log('Poker dump amount (from $' + usd.toLocaleString() + '): ' + pokerDumpAmount + ' chips ($' + usdEq.toLocaleString() + ' actual)');
                    qtUiLog('♠ Dump: ' + pokerDumpAmount + ' chips ($' + usdEq.toLocaleString() + ')');
                    return;
                }
                if (cmd === 'poker_leave') {
                    try { callRemote('PokerLeave', null); log('Poker: leave table'); qtUiLog('♠ Left table'); } catch(e) { log('PokerLeave err: ' + e); }
                    return;
                }
                if (cmd.startsWith('hitsound_url:')) {
                    try {
                        const url = cmd.substring('hitsound_url:'.length);
                        customHitSound = url || '';
                        if (url) {
                            log('Hit sound: ' + url.split('/').pop());
                            qtUiLog('Hit sound set: ' + url.split('/').pop());
                        } else {
                            log('Hit sound: default');
                            qtUiLog('Hit sound reset to default');
                        }
                    } catch(e) { log('hitsound_url error: ' + e); }
                } else if (cmd.startsWith('hitsound_vol:')) {
                    try {
                        const raw = parseInt(cmd.substring('hitsound_vol:'.length));
                        const pct = isNaN(raw) ? 50 : raw;
                        hitSoundVolume = pct <= 0 ? 0 : (pct / 100) * 0.06;
                        muteHitSound = pct <= 0;
                        log('Hit volume: ' + pct + '%' + (pct <= 0 ? ' (MUTED)' : ''));
                        qtUiLog('Hit volume: ' + pct + '%' + (pct <= 0 ? ' (MUTED)' : ''));
                    } catch(e) { log('hitsound_vol error: ' + e); }
                }
                // ---- Custom HitFX commands ----
                else if (cmd === 'hitfx:on') {
                    hitfx.enabled = true; hitfxInit(); hitfxSaveConfig();
                    log('Custom HitFX ON'); qtUiLog('🎯 Custom HitFX ON');
                } else if (cmd === 'hitfx:off') {
                    hitfx.enabled = false; hitfxSaveConfig();
                    try { mp.events.call('setHitmarker', true, true); } catch(e) {}
                    log('Custom HitFX OFF'); qtUiLog('🎯 Custom HitFX OFF');
                } else if (cmd === 'hitfx_nums:on') { hitfx.showNumbers = true; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_nums:off') { hitfx.showNumbers = false; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_sound:on') { hitfx.sound = true; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_sound:off') { hitfx.sound = false; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_hsound:on') { hitfx.headSound = true; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_hsound:off') { hitfx.headSound = false; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_outline:on') { hitfx.outline = true; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_outline:off') { hitfx.outline = false; hitfxSaveConfig(); }
                else if (cmd.startsWith('hitfx_owidth:')) {
                    hitfx.outlineWidth = Math.max(1, Math.min(5, parseInt(cmd.split(':')[1]) || 2)); hitfxSaveConfig();
                } else if (cmd.startsWith('hitfx_ocolor:')) {
                    try { hitfx.outlineColor = JSON.parse(cmd.substring('hitfx_ocolor:'.length)); hitfxSaveConfig(); } catch(e) {}
)QTJSX91"
        R"QTJSX91(                }
                else if (cmd === 'hitfx_shadow:on') { hitfx.shadow = true; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_shadow:off') { hitfx.shadow = false; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_origclr:on') { hitfx.originalColors = true; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_origclr:off') { hitfx.originalColors = false; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_dscale:on') { hitfx.distScale = true; hitfxSaveConfig(); }
                else if (cmd === 'hitfx_dscale:off') { hitfx.distScale = false; hitfxSaveConfig(); }
                else if (cmd.startsWith('hitfx_color:')) {
                    try { hitfx.color = JSON.parse(cmd.substring('hitfx_color:'.length)); hitfxSaveConfig(); } catch(e) {}
                } else if (cmd.startsWith('hitfx_fsize:')) {
                    hitfx.fontSize = Math.max(0.15, Math.min(2.0, parseFloat(cmd.split(':')[1]) || 0.55)); hitfxSaveConfig();
                } else if (cmd.startsWith('hitfx_font:')) {
                    const fid = parseInt(cmd.split(':')[1]);
                    hitfx.fontId = (fid === 100 || fid === 101) ? fid : Math.max(0, Math.min(7, fid || 0)); hitfxSaveConfig();
                    if (!hitfxIsCefFont()) safeBrowserExecute(`(function(){var r=document.getElementById('qt-hitfx-overlay');if(r)r.innerHTML=''})()`);
                } else if (cmd.startsWith('hitfx_stretchx:')) {
                    hitfx.stretchX = Math.max(0.3, Math.min(3.0, parseFloat(cmd.split(':')[1]) || 1.0)); hitfxSaveConfig();
                } else if (cmd.startsWith('hitfx_stretchy:')) {
                    hitfx.stretchY = Math.max(0.3, Math.min(3.0, parseFloat(cmd.split(':')[1]) || 1.0)); hitfxSaveConfig();
                } else if (cmd.startsWith('hitfx_dur:')) {
                    hitfx.duration = Math.max(200, Math.min(5000, parseInt(cmd.split(':')[1]) || 1500)); hitfxSaveConfig();
                } else if (cmd.startsWith('hitfx_float:')) {
                    hitfx.floatDist = Math.max(0.2, Math.min(5.0, parseFloat(cmd.split(':')[1]) || 1.2)); hitfxSaveConfig();
                } else if (cmd.startsWith('hitfx_spread:')) {
                    hitfx.spread = Math.max(0, Math.min(2.0, parseFloat(cmd.split(':')[1]) || 0.3)); hitfxSaveConfig();
                } else if (cmd.startsWith('hitfx_height:')) {
                    hitfx.heightOffset = Math.max(0, Math.min(3.0, parseFloat(cmd.split(':')[1]) || 0.85)); hitfxSaveConfig();
                } else if (cmd.startsWith('hitfx_svol:')) {
                    hitfx.soundVol = Math.max(0, Math.min(1, parseFloat(cmd.split(':')[1]) || 0.5)); hitfxSaveConfig();
                    if (hitfx.soundUrl) safeBrowserExecute(`(function(){if(window._hitfxPreload)window._hitfxPreload('${hitfx.soundUrl.replace(/'/g, "\\'")}',${hitfx.soundVol})})()`);
                } else if (cmd.startsWith('hitfx_surl:')) {
                    hitfx.soundUrl = cmd.substring('hitfx_surl:'.length) || ''; hitfxSaveConfig();
                    if (hitfx.soundUrl) { hitfxInitAudio(); safeBrowserExecute(`(function(){if(window._hitfxPreload)window._hitfxPreload('${hitfx.soundUrl.replace(/'/g, "\\'")}',${hitfx.soundVol})})()`); }
                    qtUiLog('🎯 HitFX sound: ' + (hitfx.soundUrl ? hitfx.soundUrl.split('/').pop() : 'built-in'));
                } else if (cmd.startsWith('hitfx_cfg:')) {
                    try { hitfxApplyConfig(JSON.parse(cmd.substring('hitfx_cfg:'.length))); hitfxSaveConfig(); } catch(e) {}
                } else if (cmd === 'uitheme:on' || cmd === 'uitheme:off' ||
                           cmd.startsWith('uitheme_opacity:') || cmd.startsWith('uitheme_cfg:') ||
                           cmd === 'uitheme_edit:on' || cmd === 'uitheme_edit:off' ||
                           cmd === 'uitheme_hud_reset') {
                    // Server UI Theme redesign temporarily disabled
                    qtUiTheme.enabled = false; qtUiTheme.editHud = false;
                    qtUiThemeApply();
                }
                // ---- No-Drop on Death ----
                else if (cmd === 'nodrop:on') {
                    noDropEnabled = true; noDropInit();
                    log('NoDrop ON'); qtUiLog('🛡️ NoDrop ON — weapon won\'t drop on death');
                } else if (cmd === 'nodrop:off') {
                    noDropEnabled = false; noDropSetBlock(false); noDropSetBypassLock(false);
                    log('NoDrop OFF'); qtUiLog('🛡️ NoDrop OFF');
                } else if (cmd === 'nodrop:bypass') {
                    noDropInit(); noDropSetBypassLock(true);
                    log('NoDrop: 20s weapon lock bypassed'); qtUiLog('🛡️ NoDrop: 20s weapon lock BYPASSED');
                } else if (cmd === 'nodrop:clear') {
                    noDropClearWeaponLock();
                    log('NoDrop: weapon lock cleared'); qtUiLog('🛡️ NoDrop: weapon lock cleared');
                }
                break;
        }
    });

    // ============================================================
    // FAKE DEATH MODULE
    // ============================================================

    // ---- Auto-pickup ground items ----
    let autoPickupEnabled = false;
    let autoPickupMode = 'weapons'; // 'all' or 'weapons'
    let autoPickupDelay = 300;
    let autoPickupBusy = false;
    let autoPickupUnfreezeUntil = 0;
    let autoPickupQueueTimer = null;
    let autoPickupQueue = [];
    let autoPickupWeaponAllow = null;
    let autoPickupWeaponPriority = [];
    // Server sends "setFreezePosition" true → blocks all movement in render loop via isFreezeMoving.
    // Force-clear it every frame during auto-pickup window.
    mp.events.add('render', () => {
        if (Date.now() < autoPickupUnfreezeUntil) {
            try {
                mp.players.local.isFreezeMoving = false;
                mp.players.local.freezePosition(false);
            } catch(e) {}
        }
    });
    // Weapon itemIds from weaponsStrength
    const WEAPON_ITEM_IDS = new Set([19,20,44,45,46,47,48,49,51,52,53,102,103,104,106,107,108,109,110,111,112,113,114,115,116,117,118,120,121,122,123,124,125,126,137,142,143,144,145,157,181,182,183,184,185,260,261,342]);
    function autoPickupUnsigned(v) {
        const n = Number(v);
        return isFinite(n) ? String(n >>> 0) : String(v);
    }
    function getAutoPickupWeaponKeys(data) {
        const keys = [];
        if (data && data.params && data.params.weaponHash !== undefined && data.params.weaponHash !== null) keys.push('h:' + autoPickupUnsigned(data.params.weaponHash));
        if (data && data.itemId) keys.push('i:' + Number(data.itemId));
        return keys;
    }
    function getAutoPickupPrimaryWeaponKey(data) {
        if (data && data.params && data.params.weaponHash !== undefined && data.params.weaponHash !== null) return 'h:' + autoPickupUnsigned(data.params.weaponHash);
        if (data && data.itemId) return 'i:' + Number(data.itemId);
        return null;
    }
    function isWeaponItem(data) {
        if (!data) return false;
        if (data.itemId && WEAPON_ITEM_IDS.has(data.itemId)) return true;
        if (data.params && (data.params.weaponHash || data.params.ammo !== undefined || data.params.fakeAmmo !== undefined)) return true;
        return false;
    }
    function isAutoPickupAllowed(data) {
        if (autoPickupMode !== 'weapons') return true;
        if (!isWeaponItem(data)) return false;
        if (autoPickupWeaponAllow === null) return true;
        const key = getAutoPickupPrimaryWeaponKey(data);
        return key ? autoPickupWeaponAllow.has(key) : false;
    }
    function getAutoPickupPriority(data) {
        const keys = getAutoPickupWeaponKeys(data);
        let best = 999999;
        const primary = getAutoPickupPrimaryWeaponKey(data);
        if (primary) {
            const idx = autoPickupWeaponPriority.indexOf(primary);
            if (idx !== -1) return idx;
        }
        for (const k of keys) {
            const idx = autoPickupWeaponPriority.indexOf(k);
            if (idx !== -1 && idx < best) best = idx;
        }
        return best;
    }
    function applyAutoPickupConfig(cfg) {
        if (!cfg || typeof cfg !== 'object') return;
        if (typeof cfg.enabled === 'boolean') autoPickupEnabled = cfg.enabled;
        if (cfg.mode === 'weapons' || cfg.mode === 'all') autoPickupMode = cfg.mode;
        if (Array.isArray(cfg.allowed)) autoPickupWeaponAllow = new Set(cfg.allowed.map(String));
        if (Array.isArray(cfg.priority)) autoPickupWeaponPriority = cfg.priority.map(String);
        if (cfg.delay !== undefined) {
            const d = parseInt(cfg.delay);
            if (d && d >= 50) autoPickupDelay = Math.max(50, Math.min(2000, d));
        }
    }
    function makeAutoPickupCandidate(data) {
        const cs = mp.players.local.colshape;
        let params = null;
        try {
            if (cs && cs.catchEventParams) params = JSON.parse(JSON.stringify(cs.catchEventParams));
        } catch(e) {
            params = cs && cs.catchEventParams ? cs.catchEventParams : null;
        }
        return {
            data,
            itemId: data && data.itemId,
            keys: getAutoPickupWeaponKeys(data),
            priority: getAutoPickupPriority(data),
            catchEvent: cs && cs.catchEvent ? cs.catchEvent : null,
            params,
            ts: Date.now()
        };
    }
    function getAutoPickupCandidateKey(c) {
        let p = '';
        try { p = JSON.stringify(c.params || {}); } catch(e) {}
        return (c.catchEvent || 'EventKeyDownE') + '|' + (c.itemId || '') + '|' + c.keys.join(',') + '|' + p;
    }
    function processAutoPickupQueue() {
        autoPickupQueueTimer = null;
        if (!autoPickupEnabled || !qtAlive || autoPickupBusy) { autoPickupQueue = []; return; }
        const list = autoPickupQueue.splice(0).filter(c => c && isAutoPickupAllowed(c.data));
        if (!list.length) return;
        list.sort((a, b) => (a.priority - b.priority) || (a.ts - b.ts));
        const best = list[0];
        autoPickupBusy = true;
        try {
            if (!sessionToken) { autoPickupBusy = false; return; }
            if (best.catchEvent) {
                const pStr = (best.params && Object.keys(best.params).length > 0) ? JSON.stringify(best.params) : undefined;
                callRemote(best.catchEvent, pStr);
                log('~g~Auto-pickup: ' + best.catchEvent + ' [' + best.keys.join('/') + ']');
                qtUiLog('Auto-pickup: ' + best.catchEvent + ' [' + best.keys.join('/') + ']');
            } else {
                callRemote('EventKeyDownE');
                log('~g~Auto-pickup: EventKeyDownE (no colshape)');
            }
            // Force-unfreeze for 3 seconds (render loop clears isFreezeMoving every frame)
            autoPickupUnfreezeUntil = Date.now() + 3000;
        } catch(e) { log('Auto-pickup err: ' + e); }
        autoPickupBusy = false;
    }
)QTJSX91"
        R"QTJSX91(    function scheduleAutoPickupQueue() {
        if (autoPickupQueueTimer) return;
        autoPickupQueueTimer = qtSetTimeout(processAutoPickupQueue, autoPickupDelay);
    }
    mp.events.add('promptFourth.show', (data) => {
        if (!autoPickupEnabled || !qtAlive || autoPickupBusy) return;
        if (!isAutoPickupAllowed(data)) return;
        const candidate = makeAutoPickupCandidate(data);
        const key = getAutoPickupCandidateKey(candidate);
        if (!autoPickupQueue.some(c => getAutoPickupCandidateKey(c) === key)) autoPickupQueue.push(candidate);
        scheduleAutoPickupQueue();
    });

    // ---- Block setArmourTimeout + clear CEF armourTimeout flag ----
    let armourBypassEnabled = true;
    let armourBypassInterval = 2000;
    let armourBypassTimerId = null;
    let armourHooked = false;
    function hookArmourTimeout() {
        if (!qtAlive) return;
        if (armourHooked) return;
        if (typeof browserMenu === 'undefined' || !browserMenu || !browserMenu.call) {
            qtSetTimeout(hookArmourTimeout, 1000); return;
        }
        if (browserMenu.call.__qtArmourHooked) return;
        const origBMCall = browserMenu.call.bind(browserMenu);
        browserMenu.call = function() {
            try {
                const args = Array.prototype.slice.call(arguments);
                if (armourBypassEnabled && args[0] === 'socket' && args[1] === 'callRemote' && Array.isArray(args[2]) && args[2][0] === 'setArmourTimeout') {
                    return;
                }
            } catch (e) {}
            return origBMCall.apply(browserMenu, arguments);
        };
        browserMenu.call.__qtArmourHooked = true;
        armourHooked = true;
        // Clear CEF-side armourTimeout flag so double-click equip works
        clearArmourTimeout();
    }
    function clearArmourTimeout() {
        if (!armourBypassEnabled) return;
        try {
            safeBrowserExecute('if(window.clientStorage){window.clientStorage.armourTimeout=false}');
        } catch(e) {}
    }
    hookArmourTimeout();
    // Periodically clear armourTimeout in CEF
    function startArmourBypassTimer() {
        if (armourBypassTimerId) { try { clearInterval(armourBypassTimerId); } catch(e) {} }
        armourBypassTimerId = setInterval(clearArmourTimeout, armourBypassInterval);
    }
    function getArmourConfig() {
        return { enabled: !!armourBypassEnabled, interval: armourBypassInterval };
    }
    function sendArmourConfigState() {
        if (qtExternalConnected) {
            qtExternalSink({ type: 'armour_config_state', data: getArmourConfig() });
        }
    }
    function saveArmourConfig() {
        const cfg = getArmourConfig();
        if (qtExternalConnected) {
            qtExternalSink({ type: 'armour_config', data: cfg });
        } else {
            qtExternalSink('/armour-config', cfg);
        }
        sendArmourConfigState();
    }
    function applyArmourConfig(cfg, save) {
        if (!cfg || typeof cfg !== 'object') return;
        if (typeof cfg.enabled === 'boolean') armourBypassEnabled = cfg.enabled;
        if (cfg.interval !== undefined) {
            const interval = parseInt(cfg.interval);
            if (interval && interval >= 100) armourBypassInterval = Math.max(100, Math.min(5000, interval));
        }
        startArmourBypassTimer();
        if (armourBypassEnabled) clearArmourTimeout();
        if (save) saveArmourConfig();
        else sendArmourConfigState();
    }
    startArmourBypassTimer();

    // ---- Anti disableFiring (toggleable) ----
    let firingHooked = false;
    let antiFiringEnabled = false;

    // ---- Assault Shotgun no-falloff (distance spoof) ----
    // Server's outgoingDamage handler (QU_formatted.js around line 49158) computes:
    //   h = mp.game.system.vdist(target.pos, source.pos)
    //   _ = Math.max(5, _ - Math.max(0, h - 1))    // for GROUP_SHOTGUN
    // We hook vdist so that during that exact call it returns 1 → falloff term becomes 0.
    // No additional applyDamageTo / setCurrentDamageEventAmount from our side: the QU script
    // applies the full base damage by itself, exactly as at point-blank range.
    // Accept both signed and unsigned int32 forms of the hash because RAGE:MP
    // can return mp.players.local.weapon either way depending on platform/build.
    const ASSAULT_SHOTGUN_HASH_U = 3800352039;
    const ASSAULT_SHOTGUN_HASH_S = ASSAULT_SHOTGUN_HASH_U | 0; // -494615257
    let sgNoFalloffEnabled = false;
    let sgNoFalloffDebug = false;       // toggle via 'sg_nofalloff_debug'
    let sgNoFalloffDebugCount = 0;      // limit log spam to first 20 events per debug session
    let sgNoFalloffHookOk = false;

    (function hookVdistForAssaultSG() {
        if (!mp.game || !mp.game.system || typeof mp.game.system.vdist !== 'function') {
            log('SG no-falloff: vdist API not available, hook NOT installed');
            return;
        }
        const origVdist = mp.game.system.vdist.bind(mp.game.system);
        // Generous tolerance: player coords are unique to several meters around them,
        // and QU may sample a.position at a slightly different float than ours.
        const POS_EPS = 0.5;
        const dbg = function(msg) {
            if (!sgNoFalloffDebug || sgNoFalloffDebugCount >= 20) return;
            sgNoFalloffDebugCount++;
            try { log('[SG-NF] ' + msg); } catch(_) {}
        };
        const hookFn = function(x1, y1, z1, x2, y2, z2) {
            try {
                if (sgNoFalloffEnabled) {
                    const w = mp.players.local.weapon;
                    const isAssaultSG = (w === ASSAULT_SHOTGUN_HASH_U) || (w === ASSAULT_SHOTGUN_HASH_S);
                    if (isAssaultSG) {
                        const lp = mp.players.local.position;
                        const m1 = Math.abs(x1 - lp.x) < POS_EPS && Math.abs(y1 - lp.y) < POS_EPS && Math.abs(z1 - lp.z) < POS_EPS;
                        const m2 = Math.abs(x2 - lp.x) < POS_EPS && Math.abs(y2 - lp.y) < POS_EPS && Math.abs(z2 - lp.z) < POS_EPS;
                        if (m1 || m2) {
                            if (sgNoFalloffDebug) {
                                const real = origVdist(x1, y1, z1, x2, y2, z2);
                                dbg('HIT spoof: real=' + real.toFixed(2) + 'm -> 1m');
                            }
                            return 1;
                        } else if (sgNoFalloffDebug) {
                            dbg('vdist no-match (probably not damage call). p1=(' + x1.toFixed(1) + ',' + y1.toFixed(1) + ',' + z1.toFixed(1) + ') p2=(' + x2.toFixed(1) + ',' + y2.toFixed(1) + ',' + z2.toFixed(1) + ') me=(' + lp.x.toFixed(1) + ',' + lp.y.toFixed(1) + ',' + lp.z.toFixed(1) + ')');
                        }
                    } else if (sgNoFalloffDebug) {
                        dbg('skipped: weapon=' + w + ' (expected ' + ASSAULT_SHOTGUN_HASH_U + ' or ' + ASSAULT_SHOTGUN_HASH_S + ')');
                    }
                }
            } catch(e) { try { log('[SG-NF] hook err: ' + e.message); } catch(_){} }
            return origVdist(x1, y1, z1, x2, y2, z2);
        };
        try {
            mp.game.system.vdist = hookFn;
            sgNoFalloffHookOk = (mp.game.system.vdist === hookFn);
            if (sgNoFalloffHookOk) log('SG no-falloff: vdist hook installed OK');
            else log('SG no-falloff: vdist assignment silently rejected (readonly?)');
        } catch(e) {
            log('SG no-falloff: hook install error: ' + e.message);
        }
    })();

    function hookDisableFiring() {
        if (firingHooked) return;
        const origDisableFiring = mp.game.player.disableFiring.bind(mp.game.player);
        mp.game.player.disableFiring = function(toggle) {
            if (toggle && antiFiringEnabled) return;
            return origDisableFiring(toggle);
        };
        firingHooked = true;
    }
    hookDisableFiring();

    // ---- Hitmarker sound hook ----
    let soundHooked = false;
    function patchHitSoundArgs(args) {
        if ((args[0] === 'enableSound' && (args[1] === 'hitmarker.mp3' || args[1] === 'hitsound.mp3')) || (args[0] === 'enableSoundStart' && (args[1] === 'hitmarker.mp3' || args[1] === 'hitsound.mp3'))) {
            if (muteHitSound) return false;
            if (customHitSound) {
                args[1] = customHitSound;
                if (/^https?:\/\//i.test(customHitSound)) args[3] = true;
            }
            args[2] = hitSoundVolume;
        }
        return true;
    }
    function hookHitSound() {
        if (soundHooked) return;
        const origCall = mp.events.call.bind(mp.events);
        mp.events.call = function() {
            const args = Array.prototype.slice.call(arguments);
            if (!patchHitSoundArgs(args)) return;
            return origCall.apply(mp.events, args);
        };
        soundHooked = true;
    }
    hookHitSound();
    function hookBrowserHitSound() {
        if (!qtAlive) return;
        try {
            if (typeof browserMenu === 'undefined' || !browserMenu || !browserMenu.call) {
                qtSetTimeout(hookBrowserHitSound, 1000); return;
            }
            if (browserMenu.call.__qtHitSoundHooked) return;
            const origBMCall = browserMenu.call.bind(browserMenu);
            browserMenu.call = function() {
                const args = Array.prototype.slice.call(arguments);
                if (!patchHitSoundArgs(args)) return;
                return origBMCall.apply(browserMenu, args);
            };
            browserMenu.call.__qtHitSoundHooked = true;
        } catch(e) { qtSetTimeout(hookBrowserHitSound, 1000); }
    }
    hookBrowserHitSound();

    // ---- Token intercept (shared: works for both FD and Aquanaut) ----
    const tokenInjectCode = `(function(){
        if(window.__qtTokenHook) return;
        window.__qtTokenHook = true;
        var origStringify = JSON.stringify;
        JSON.stringify = function(value) {
            var result = origStringify.apply(this, arguments);
            try {
                if (result && result.indexOf('remoteEvent') !== -1) {
                    var parsed = typeof value === 'string' ? JSON.parse(value) : value;
                    if (Array.isArray(parsed) && parsed[0] === 'remoteEvent' && Array.isArray(parsed[1])) {
                        var evName = parsed[1][0];
                        if (evName === 'EventKeyDown' && parsed[1][1] && typeof parsed[1][1] === 'string' && parsed[1][1].length > 5) {
                            mp.trigger('qt:setToken', parsed[1][1]);
                        }
                        if (evName === 'createMarketDrugPointBuyer' && parsed[1][2]) {
                            mp.trigger('qt:marketListing', parsed[1][2]);
                        }
                        if (evName === 'useItem' && parsed[1][2]) {
                            try {
                                var itemData = JSON.parse(parsed[1][2]);
)QTJSX91"
        R"QTJSX91(                                if (itemData && itemData.sqlId) mp.trigger('qt:detectedUseItem', String(itemData.sqlId));
                            } catch(ee) {}
                        }
                    }
                }
            } catch(e) {}
            return result;
        };
    })();`;

    function injectTokenHook() {
        try {
            mp.browsers.forEach(b => {
                try { b.execute(tokenInjectCode); } catch (e) {}
            });
        } catch (e) {}
        try { qtUiThemeApply(); } catch (e) {}
    }

    function hookNewBrowsers() {
        if (mp.browsers.new.__qtTokenHooked) return;
        const origNew = mp.browsers.new;
        mp.browsers.new = function () {
            const b = origNew.apply(this, arguments);
            qtSetTimeout(() => {
                try { b.execute(tokenInjectCode); } catch (e) {}
                try { qtUiThemeApply(); } catch (e) {}
            }, 500);
            return b;
        };
        mp.browsers.new.__qtTokenHooked = true;
    }

    mp.events.add('qt:setToken', (token) => {
        if (token && token.length > 5) {
            if (!sessionToken) log('Token: ' + token.substring(0, 6) + '...');
            sessionToken = token;
        }
    });

    injectTokenHook();
    hookNewBrowsers();

    // ---- CS Counter-Strafe ----
    let strafeActive = false;
    let STRAFE_SPEED = 3.8;
    let strafeAimOnly = true;
    let strafeFwdBack = true;
    const CTRL_LEFT = 34, CTRL_RIGHT = 35, CTRL_FWD = 32, CTRL_BACK = 33, CTRL_AIM = 25;

    function strafeRender() {
        if (!strafeActive) return;
        const p = mp.players.local;
        if (!p || !p.handle || p.isInAnyVehicle(false) || p.isDead()) return;
        if (strafeAimOnly && !mp.game.controls.isControlPressed(0, CTRL_AIM)) return;

        const left  = mp.game.controls.isControlPressed(0, CTRL_LEFT);
        const right = mp.game.controls.isControlPressed(0, CTRL_RIGHT);
        const fwd   = mp.game.controls.isControlPressed(0, CTRL_FWD);
        const back  = mp.game.controls.isControlPressed(0, CTRL_BACK);

        let dir = 0;
        if (right && !left) dir = 1;
        else if (left && !right) dir = -1;
        if (dir === 0) return;

        const rad = mp.game.cam.getGameplayCamRot(2).z * (Math.PI / 180);
        const fX = -Math.sin(rad), fY = Math.cos(rad);
        const rX = fY, rY = -fX;
        let vx = rX * dir, vy = rY * dir;
        if (strafeFwdBack) {
            if (fwd && !back) { vx += fX; vy += fY; }
            if (back && !fwd) { vx -= fX; vy -= fY; }
        }
        const len = Math.sqrt(vx * vx + vy * vy);
        if (len > 0.01) { vx = (vx / len) * STRAFE_SPEED; vy = (vy / len) * STRAFE_SPEED; }
        const cv = p.getVelocity();
        p.setVelocity(vx, vy, cv.z);
    }

    function startStrafe() {
        if (strafeActive) return;
        strafeActive = true;
        mp.events.add('render', strafeRender);
        log('CS Strafe ON');
    }

    function stopStrafe() {
        if (!strafeActive) return;
        strafeActive = false;
        try { mp.events.remove('render', strafeRender); } catch (e) {}
        log('CS Strafe OFF');
    }

    // ---- A/D Timing Strafe (proxy to Python SendInput helper) ----
    let adTimingActive = false;
    let adTimingCfg = {
        targetA: 228, targetD: 228,
        jitter: 8,
        usefulDelay: 0,
        aimOnly: false,
        holdKey: 0  // 0 = toggle mode, >0 = hold-key VK code
    };
    let adTimingHoldDown = false;

    // A/D Timing — proxy to Python SendInput helper on localhost:7233
    var AD_HELPER_URL = 'http://127.0.0.1:7233';

    function adTimingHttpGet(path) {
        try {
            mp.browsers.forEach(function(b) {
                b.execute('fetch("' + AD_HELPER_URL + path + '").catch(function(){})');
            });
        } catch (e) {
            log('A/D helper unreachable: ' + e);
        }
    }

    let adTimingHelperRunning = false; // is Python helper currently pressing keys

    function adTimingStartEngine() {
        if (adTimingActive) return;
        adTimingActive = true;
        adTimingHttpGet('/config?aimOnly=' + (adTimingCfg.aimOnly ? '1' : '0'));
        adTimingHttpGet('/arm');
        if (!adTimingCfg.aimOnly) adTimingHelperRunning = true;
        log('A/D Timing ON (A=' + adTimingCfg.targetA + ' D=' + adTimingCfg.targetD + ' j=' + adTimingCfg.jitter + (adTimingCfg.aimOnly ? ' AIM' : '') + ')');
    }

    function adTimingStopEngine() {
        if (!adTimingActive) return;
        adTimingActive = false;
        adTimingHttpGet('/disarm');
        adTimingHelperRunning = false;
        log('A/D Timing OFF');
    }

    function adTimingApplyCfg(cfg) {
        if (!cfg || typeof cfg !== 'object') return;
        if (cfg.targetA !== undefined) adTimingCfg.targetA = Math.max(50, Math.min(1000, Number(cfg.targetA) || 228));
        if (cfg.targetD !== undefined) adTimingCfg.targetD = Math.max(50, Math.min(1000, Number(cfg.targetD) || 228));
        if (cfg.jitter !== undefined) adTimingCfg.jitter = Math.max(0, Math.min(50, Number(cfg.jitter) || 0));
        if (cfg.usefulDelay !== undefined) adTimingCfg.usefulDelay = Math.max(0, Math.min(100, Number(cfg.usefulDelay) || 0));
        if (cfg.aimOnly !== undefined) adTimingCfg.aimOnly = !!cfg.aimOnly;
        if (cfg.holdKey !== undefined) adTimingCfg.holdKey = Number(cfg.holdKey) || 0;
        // Sync config to Python helper
        adTimingHttpGet('/config?targetA=' + adTimingCfg.targetA + '&targetD=' + adTimingCfg.targetD + '&jitter=' + adTimingCfg.jitter + '&aimOnly=' + (adTimingCfg.aimOnly ? '1' : '0'));
        log('A/D cfg: A=' + adTimingCfg.targetA + ' D=' + adTimingCfg.targetD + ' j=' + adTimingCfg.jitter + ' aim=' + adTimingCfg.aimOnly);
    }

    function adTimingSaveCfg() {
        if (qtExternalConnected) {
            qtExternalSink({ type: 'ad_timing_config', data: adTimingCfg });
        }
    }

    // F6 keybind for A/D timing toggle
    mp.keys.bind(0x75, false, function() {
        if (adTimingActive) {
            adTimingStopEngine();
        } else {
            adTimingStartEngine();
        }
    });

    // Aim-only mode + hold-key polling for A/D timing
    mp.events.add('render', function() {
        // Hold-key mode
        if (adTimingCfg.holdKey > 0) {
            var down = mp.keys.isDown(adTimingCfg.holdKey);
            if (down && !adTimingHoldDown) {
                adTimingHoldDown = true;
                adTimingStartEngine();
            } else if (!down && adTimingHoldDown) {
                adTimingHoldDown = false;
                adTimingStopEngine();
            }
        }

        // Shift + aim check: A/D only when shift is held (and aim if aimOnly)
        if (!adTimingActive) return;
        var shift = mp.keys.isDown(0x10); // VK_SHIFT
        var shouldRun = shift;
        if (adTimingCfg.aimOnly) {
            shouldRun = shouldRun && mp.game.controls.isControlPressed(0, CTRL_AIM);
        }
        if (shouldRun && !adTimingHelperRunning) {
            adTimingHttpGet('/on');
            adTimingHelperRunning = true;
        } else if (!shouldRun && adTimingHelperRunning) {
            adTimingHttpGet('/off');
            adTimingHelperRunning = false;
        }
    });

    // ---- Anti-Recoil (native camera pitch compensation) ----
    let arActive = false;
    let arCfg = {
        strength: 0.65,     // 0.0-1.0 how much recoil to compensate (< 1.0 = legit)
        smoothing: 0.4,     // smoothing factor (higher = smoother but slower)
        jitter: 0.15,       // random variation to look human
        aimOnly: true       // only when aiming
    };
    let arPrevPitch = null;
    let arCompensation = 0;

    function arGetPitch() {
        return mp.game.invoke('0x3A6867B4845BEDA2'); // GET_GAMEPLAY_CAM_RELATIVE_PITCH
    }
    function arSetPitch(angle) {
        mp.game.invoke('0x6D0858B8EDFD2B7D', angle, 1.0); // SET_GAMEPLAY_CAM_RELATIVE_PITCH
    }

    function arRender() {
        try {
            if (!arActive) return;
            var p = mp.players.local;
            if (!p || !p.handle || p.isInAnyVehicle(false) || p.isDead()) {
                arPrevPitch = null;
                return;
            }

            var aiming = mp.game.controls.isControlPressed(0, CTRL_AIM);
            if (arCfg.aimOnly && !aiming) {
                arPrevPitch = null;
                arCompensation = 0;
                return;
            }

            var shooting = mp.game.controls.isControlPressed(0, 24); // INPUT_ATTACK
            var pitch = arGetPitch();

            if (arPrevPitch !== null && shooting) {
                var delta = pitch - arPrevPitch;
                if (delta > 0.05) {
                    var jit = 1.0 + (Math.random() * 2 - 1) * arCfg.jitter;
                    var compensation = delta * arCfg.strength * jit;
                    arCompensation = arCompensation * arCfg.smoothing + compensation * (1 - arCfg.smoothing);
                    var newPitch = pitch - arCompensation;
                    arSetPitch(newPitch);
                }
            } else if (!shooting) {
                arCompensation = arCompensation * 0.8;
            }

            arPrevPitch = arGetPitch();
        } catch (e) {
            log('AR error: ' + e);
        }
    }

    function startAntiRecoil() {
        if (arActive) return;
        arActive = true;
        arPrevPitch = null;
        arCompensation = 0;
        mp.events.add('render', arRender);
        log('Anti-Recoil ON (str=' + arCfg.strength + ' sm=' + arCfg.smoothing + ' j=' + arCfg.jitter + ')');
    }

    function stopAntiRecoil() {
        if (!arActive) return;
        arActive = false;
        try { mp.events.remove('render', arRender); } catch (e) {}
        arPrevPitch = null;
        arCompensation = 0;
        log('Anti-Recoil OFF');
    }

    function arApplyCfg(cfg) {
        if (!cfg || typeof cfg !== 'object') return;
        if (cfg.strength !== undefined) arCfg.strength = Math.max(0, Math.min(1, Number(cfg.strength) || 0.65));
        if (cfg.smoothing !== undefined) arCfg.smoothing = Math.max(0, Math.min(0.95, Number(cfg.smoothing) || 0.4));
        if (cfg.jitter !== undefined) arCfg.jitter = Math.max(0, Math.min(0.5, Number(cfg.jitter) || 0.15));
        if (cfg.aimOnly !== undefined) arCfg.aimOnly = !!cfg.aimOnly;
        log('AR cfg: str=' + arCfg.strength + ' sm=' + arCfg.smoothing + ' j=' + arCfg.jitter + ' aim=' + arCfg.aimOnly);
    }

    function arSaveCfg() {
        if (qtExternalConnected) {
            qtExternalSink({ type: 'ar_config', data: arCfg });
        }
    }

    // ---- Poker Transfer Bot ----
    function pokerSendState(newHand) {
        if (!pokerBotActive || pokerActionPending) return;
        qtExternalSink({
            type: 'poker_state', cards: pokerMyCards, community: pokerCommunity,
            stage: pokerStage, myTurn: pokerMyTurn, seat: pokerMySeat,
)QTJSX91"
        R"QTJSX91(            canPass: pokerCanPass, canCall: pokerCanCall, canFold: pokerCanFold,
            canAllIn: pokerCanAllIn, canBet: pokerCanBet,
            callAmount: pokerCallAmt, betAmount: pokerBetAmt,
            newHand: !!newHand
        });
    }
    function pokerDoAction(action, betAmt) {
        if (pokerActionPending) return;
        pokerActionPending = true;
        const delay = 1500 + Math.random() * 2000;
        qtSetTimeout(function() {
            try {
                switch (action) {
                    case 'fold':
                        if (pokerCanFold) callRemote('PokerFold', null);
                        else if (pokerCanPass) callRemote('PokerPass', null);
                        break;
                    case 'call':
                        if (pokerCanCall) callRemote('PokerCall', null);
                        else if (pokerCanPass) callRemote('PokerPass', null);
                        break;
                    case 'allin':
                        if (pokerCanAllIn) callRemote('PokerAllIn', null);
                        else if (pokerCanCall) callRemote('PokerCall', null);
                        else if (pokerCanPass) callRemote('PokerPass', null);
                        break;
                    case 'check':
                        if (pokerCanPass) callRemote('PokerPass', null);
                        else if (pokerCanCall) callRemote('PokerCall', null);
                        break;
                    case 'bet':
                        if (pokerCanBet && betAmt > 0) callRemote('PokerBet', String(betAmt));
                        else if (pokerCanCall) callRemote('PokerCall', null);
                        else if (pokerCanPass) callRemote('PokerPass', null);
                        break;
                    case 'betmax': {
                        // Inflate pot when farm has strong hand (will fold before showdown)
                        const amt = pokerDumpAmount || 1000;
                        if (pokerCanBet) { callRemote('PokerBet', String(amt)); betAmt = amt; }
                        else if (pokerCanCall) callRemote('PokerCall', null);
                        else if (pokerCanPass) callRemote('PokerPass', null);
                        break;
                    }
                }
                log('Poker action: ' + action + (betAmt ? ' $' + betAmt : ''));
                qtUiLog('♠ Poker: ' + action + (betAmt ? ' $' + betAmt : ''));
            } catch(e) { log('Poker action error: ' + e); }
            pokerActionPending = false;
            pokerMyTurn = false;
        }, delay);
    }

    // Evaluate 2-card hand strength: 'weak' | 'medium' | 'strong'
    // Card format: "2h", "Th", "Ah" etc. First char = rank, second = suit.
    function pokerEvaluateHand(c1, c2) {
        if (!c1 || !c2 || c1.length < 2 || c2.length < 2) return 'unknown';
        const VALS = {'2':2,'3':3,'4':4,'5':5,'6':6,'7':7,'8':8,'9':9,'T':10,'J':11,'Q':12,'K':13,'A':14};
        const v1 = VALS[c1[0].toUpperCase()] || 0;
        const v2 = VALS[c2[0].toUpperCase()] || 0;
        if (!v1 || !v2) return 'unknown';
        const hi = Math.max(v1, v2), lo = Math.min(v1, v2);
        const isPair = v1 === v2;
        const isSuited = c1[1].toLowerCase() === c2[1].toLowerCase();
        // Strong: any pair OR both cards ≥ T (10) OR suited Ax/Kx
        if (isPair) return 'strong';
        if (hi >= 13 && lo >= 10) return 'strong';    // Ax/Kx with T+
        if (hi >= 11 && lo >= 11) return 'strong';    // JJ+/JQ/JK etc. (no pair variant)
        // Weak: highest card < J (11), no pair, no broadway
        if (hi < 11) return 'weak';
        // Medium: Ax low, Kx low, Jx low (coin flip-ish)
        return 'medium';
    }

    // Auto-decide action based on mode (dump = farm loses, catch = main wins)
    function pokerAutoDecide() {
        if (!pokerBotActive || !pokerAutoMode || !pokerMyTurn || pokerActionPending) return;
        const callNeeded = pokerCallAmt > 0; // opponent bet/raised
        if (pokerAutoMode === 'dump') {
            // Smart dump: evaluate own hand. Weak → all-in (lose stack). Strong → fold (save stack).
            const hand = pokerEvaluateHand(pokerMyCards[0], pokerMyCards[1]);
            log('[DUMP] cards=' + pokerMyCards.join(' ') + ' strength=' + hand);
            if (hand === 'weak') {
                // Weak cards → all-in. Main will call, dump loses stack.
                if (pokerCanAllIn) {
                    log('[DUMP] WEAK hand → all-in (dump stack to main)');
                    pokerDoAction('allin');
                    return;
                }
                if (pokerCanBet) {
                    log('[DUMP] WEAK no all-in → bet ' + pokerDumpAmount + ' chips');
                    pokerDoAction('bet', pokerDumpAmount);
                    return;
                }
                if (pokerCanCall) { pokerDoAction('call'); return; }
                if (pokerCanPass) { pokerDoAction('check'); return; }
            }
            // Strong/medium/unknown → fold if someone bet, else check (don't commit chips)
            if (pokerCanFold && callNeeded) {
                log('[DUMP] ' + hand.toUpperCase() + ' hand, opp raised → fold (save stack)');
                pokerDoAction('fold');
            } else if (pokerCanPass) {
                log('[DUMP] ' + hand.toUpperCase() + ' hand → check');
                pokerDoAction('check');
            } else if (pokerCanCall) {
                pokerDoAction('call'); // forced blind call
            }
        } else if (pokerAutoMode === 'catch') {
            // Strategy: always all-in / call to catch dump. Never fold.
            if (pokerCanAllIn) {
                log('[CATCH] → all-in');
                pokerDoAction('allin');
            } else if (pokerCanCall) {
                log('[CATCH] → call');
                pokerDoAction('call');
            } else if (pokerCanPass) {
                pokerDoAction('check');
            }
        }
    }
    // State-tracking handlers: ALWAYS update (no pokerBotActive guard) so activating
    // the bot mid-hand still has fresh cards/community/seat. Only logging is gated.
    // update_mycards is the AUTHORITATIVE new-hand trigger (fires once per hand when
    // hole cards are dealt) — regardless of what game's stage numbering uses.
    let pokerLastCardsKey = '';
    mp.events.add('POKER::update_mycards', function(e, a) {
        pokerMyCards = [e, a];
        const cardsKey = e + '|' + a;
        const isNewHand = cardsKey !== pokerLastCardsKey;
        pokerLastCardsKey = cardsKey;
        if (isNewHand) {
            pokerCommunity = [];
            pokerStage = 'preflop'; // normalize — don't trust game's stage numbering
            pokerActionPending = false;
        }
        if (pokerBotActive) {
            log('Poker cards: ' + e + ' ' + a + (isNewHand ? ' (NEW HAND)' : ''));
            pokerSendState(isNewHand);
        }
    });
    mp.events.add('POKER::dealercards', function(e) {
        try { pokerCommunity = typeof e === 'string' ? JSON.parse(e) : (Array.isArray(e) ? e : []); } catch(ex) { pokerCommunity = []; }
        if (pokerBotActive) { log('Poker community: ' + JSON.stringify(pokerCommunity)); pokerSendState(); }
    });
    mp.events.add('POKER::gamestage', function(e) {
        pokerStage = e;
        if (pokerBotActive) log('Poker stage: ' + e);
    });
    mp.events.add('POKER::myPlayerSrc', function(e) {
        pokerMySeat = e;
    });
    function schedulePokerFallback() {
        if (!pokerAutoMode) return;
        if (pokerAutoDecideTimer) qtClearTimeout(pokerAutoDecideTimer);
        // 3500ms — server's poker_action: normally arrives in <300ms, so fallback only fires if external is unreachable/slow
        pokerAutoDecideTimer = qtSetTimeout(function() {
            pokerAutoDecideTimer = null;
            if (pokerBotActive && pokerMyTurn && !pokerActionPending) {
                log('[FALLBACK] Server did not respond in 3.5s → local auto-decide');
                pokerAutoDecide();
            }
        }, 3500);
    }
    mp.events.add('POKER::currentplayer', function(e) {
        if (!pokerBotActive) return;
        pokerMyTurn = (e === pokerMySeat);
        if (pokerMyTurn) { log('Poker: my turn'); pokerSendState(); schedulePokerFallback(); }
    });
    mp.events.add('POKER::updateButtons', function(canpass, cancall, canfold, canallin, canbet) {
        if (!pokerBotActive) return;
        pokerCanPass = canpass; pokerCanCall = cancall; pokerCanFold = canfold;
        pokerCanAllIn = canallin; pokerCanBet = canbet;
        if (pokerMyTurn) { pokerSendState(); schedulePokerFallback(); }
    });
    mp.events.add('POKER::callAmount', function(callAmt, betAmt) {
        if (!pokerBotActive) return;
        pokerCallAmt = callAmt; pokerBetAmt = betAmt;
    });
    mp.events.add('POKER::disableallbuttons', function() {
        if (!pokerBotActive) return;
        pokerMyTurn = false;
        if (pokerAutoDecideTimer) { qtClearTimeout(pokerAutoDecideTimer); pokerAutoDecideTimer = null; }
    });

    // ---- Admin Proximity Detector ----
    let adminDetectorActive = true;
    let detectedAdmins = new Map(); // remoteId → { name, alerted, spectTicks, lastPos }
    let adminScanInterval = null;
    let adminEspActive = false; // render boxes + lines
    let activeAdminList = []; // [{id, pos, name, invis, spectating}] for render
    let knownServerAdmins = new Map(); // remoteId → { name, lvl } — all admins on server
    let firstAdminScan = true; // skip join alerts on first scan (initial population)
    let criticalAdminJoinSerial = 0;
    let lastCriticalAdminJoin = null;

    function resetAdminJoinBaseline() {
        detectedAdmins.clear();
        knownServerAdmins.clear();
        activeAdminList = [];
        firstAdminScan = true;
        lastCriticalAdminJoin = null;
    }
    mp.events.add('playerReady', resetAdminJoinBaseline);

    function getDist(a, b) {
        return Math.round(Math.sqrt((a.x-b.x)**2 + (a.y-b.y)**2 + (a.z-b.z)**2));
    }

    function startAdminDetector() {
        if (adminScanInterval) return;
        adminDetectorActive = true;
        adminScanInterval = qtSetInterval(adminScanTick, 3000);
        log('Admin detector ON');
    }

    function adminScanTick() {
        if (!adminDetectorActive) return;
        try {
            const local = mp.players.local;
            const localPos = local.position;
            const foundIds = new Set();
            activeAdminList = [];

            mp.players.forEach((p) => {
                try {
                    const i = Number(p.remoteId);
                    if (!p || p === local) return;
                    const adminVar = p.getVariable('admin');
                    const adminLvl = adminVar || 0;
                    const isAdmin = adminLvl > 0 || adminVar === null; // null = off-duty admin, undefined = never admin
                    if (!isAdmin) {
                        detectedAdmins.delete(i);
)QTJSX91"
        R"QTJSX91(                        return;
                    }
                    // confirmed admin (on-duty or off-duty)
                    foundIds.add(i);
                    const offDuty = adminVar === null;
                    const isInvis = !!(p.variable_invisible || p.getVariable('invisible'));
                    let dist = '?';
                    let pPos = null;
                    try {
                        pPos = p.position;
                        dist = getDist(localPos, pPos);
                    } catch(e) {}

                    let entry = detectedAdmins.get(i);
                    if (!entry) {
                        entry = { name: p.name||'ID:'+i, alerted: false, spectTicks: 0, spectAlerted: false, lastPos: null };
                        detectedAdmins.set(i, entry);
                    }
                    entry.name = p.name || 'ID:'+i;

                    // --- Spectate detection ---
                    if (isInvis && pPos && typeof dist === 'number' && dist < 50) {
                        entry.spectTicks++;
                    } else {
                        entry.spectTicks = 0;
                        entry.spectAlerted = false;
                    }
                    const isSpectating = entry.spectTicks >= 2; // 2 ticks (6 sec) close + invisible = spectating

                    // --- Alert (once per admin) ---
                    if (!entry.alerted) {
                        entry.alerted = true;
                        const tag = p.getVariable('factionTag') || '';
                        qtUiLog('⚠️ ADMIN: ' + entry.name + (offDuty ? ' [OFF-DUTY]' : '') + (isInvis ? ' [INVISIBLE]' : '') + ' lvl:' + adminLvl + ' dist:' + dist + 'm' + (tag ? ' ['+tag+']' : ''));
                    }

                    // --- Spectate alert (once per spectate session) ---
                    if (isSpectating && !entry.spectAlerted) {
                        entry.spectAlerted = true;
                        qtSilentNotice('🔴 SPECTATING YOU: ' + entry.name + ' lvl:' + adminLvl + ' dist:' + dist + 'm (following ' + (entry.spectTicks*3) + 's)');
                    }

                    // --- ESP data (only within stream range ~300m) ---
                    if (pPos && dist <= 300) {
                        activeAdminList.push({ id: i, pos: pPos, name: entry.name, lvl: adminLvl, offDuty: offDuty, invis: isInvis, spectating: isSpectating, dist: dist });
                    }

                    entry.lastPos = pPos;
                } catch(e) {}
            });

            // clean up admins who left
            for (const [id] of detectedAdmins) {
                if (!foundIds.has(id)) detectedAdmins.delete(id);
            }

            // --- Server-wide admin join/leave tracking ---
            // Detect new admins that joined the server
            for (const id of foundIds) {
                if (!knownServerAdmins.has(id)) {
                    try {
                        const p = mp.players.atRemoteId(id);
                        const name = p ? (p.name || 'ID:'+id) : 'ID:'+id;
                        const lvl = p ? (p.getVariable('admin') || 0) : 0;
                        const tag = p ? (p.getVariable('factionTag') || '') : '';
                        knownServerAdmins.set(id, { name, lvl });
                        if (!firstAdminScan) {
                            qtUiLog('🟢 ADMIN JOINED: ' + name + ' (ID:' + id + ' lvl:' + lvl + ')' + (tag ? ' ['+tag+']' : ''));
                            if (lvl >= 5) {
                                criticalAdminJoinSerial++;
                                lastCriticalAdminJoin = {
                                    serial: criticalAdminJoinSerial,
                                    id,
                                    name,
                                    lvl
                                };
                            }
                        }
                    } catch(e) {}
                }
            }
            // Detect admins that left the server
            const adminsToRemove = [];
            for (const [id, info] of knownServerAdmins) {
                if (!foundIds.has(id)) {
                    adminsToRemove.push(id);
                    if (!firstAdminScan) {
                        qtUiLog('🔴 ADMIN LEFT: ' + info.name + ' (ID:' + id + ' lvl:' + info.lvl + ')');
                    }
                }
            }
            adminsToRemove.forEach(id => knownServerAdmins.delete(id));
            if (firstAdminScan) {
                firstAdminScan = false;
                if (knownServerAdmins.size > 0) {
                    log('Admin tracker: ' + knownServerAdmins.size + ' admins online');
                }
            }

            // push admin list to external for webapp
            const adminListPayload = [];
            for (const [id, entry] of detectedAdmins) {
                try {
                    const p = mp.players.atRemoteId(id);
                    const adminVar = p ? p.getVariable('admin') : undefined;
                    const adminLvl = adminVar || 0;
                    const od = adminVar === null;
                    const tag = p ? (p.getVariable('factionTag') || '') : '';
                    const isInvis = p ? !!(p.variable_invisible || p.getVariable('invisible')) : false;
                    adminListPayload.push({ id, name: entry.name, lvl: adminLvl, tag, invis: isInvis, spect: entry.spectTicks >= 2, offDuty: od });
                } catch(e) {
                    adminListPayload.push({ id, name: entry.name, lvl: 0, tag: '', invis: false, spect: false });
                }
            }
            if (qtExternalConnected) {
                qtExternalSink({ type: 'admin_list', data: adminListPayload });
            } else {
                qtExternalSink('/admin-list', adminListPayload);
            }
        } catch(e) {}
    }

    function stopAdminDetector() {
        adminDetectorActive = false;
        if (adminScanInterval) { qtClearInterval(adminScanInterval); adminScanInterval = null; }
        detectedAdmins.clear();
        activeAdminList = [];
        log('Admin detector OFF');
    }

    // ---- Admin ESP (render boxes + lines) ----
    function adminEspRender() {
        if (!adminEspActive || !activeAdminList.length) return;
        try {
            const local = mp.players.local;
            const localPos = local.position;
            for (const adm of activeAdminList) {
                const scr = mp.game.graphics.world3dToScreen2d(adm.pos.x, adm.pos.y, adm.pos.z + 1.0);
                if (!scr) continue;

                // line from player to admin
                const lScr = mp.game.graphics.world3dToScreen2d(localPos.x, localPos.y, localPos.z + 0.5);
                if (lScr) {
                    mp.game.graphics.drawLine(localPos.x, localPos.y, localPos.z + 0.5, adm.pos.x, adm.pos.y, adm.pos.z + 0.5,
                        adm.spectating ? 255 : 255, adm.spectating ? 0 : 165, 0, 200);
                }

                // 3D box around admin
                const bx = 0.3, bz = 0.9;
                const corners = [
                    [adm.pos.x-bx, adm.pos.y-bx, adm.pos.z-bz], [adm.pos.x+bx, adm.pos.y-bx, adm.pos.z-bz],
                    [adm.pos.x+bx, adm.pos.y+bx, adm.pos.z-bz], [adm.pos.x-bx, adm.pos.y+bx, adm.pos.z-bz],
                    [adm.pos.x-bx, adm.pos.y-bx, adm.pos.z+bz], [adm.pos.x+bx, adm.pos.y-bx, adm.pos.z+bz],
                    [adm.pos.x+bx, adm.pos.y+bx, adm.pos.z+bz], [adm.pos.x-bx, adm.pos.y+bx, adm.pos.z+bz]
                ];
                const r = adm.spectating ? 255 : 255, g = adm.spectating ? 0 : 165, b = 0, a = 180;
                const edges = [[0,1],[1,2],[2,3],[3,0],[4,5],[5,6],[6,7],[7,4],[0,4],[1,5],[2,6],[3,7]];
                for (const [ea, eb] of edges) {
                    mp.game.graphics.drawLine(corners[ea][0],corners[ea][1],corners[ea][2],
                        corners[eb][0],corners[eb][1],corners[eb][2], r, g, b, a);
                }

                // text label
                const label = (adm.spectating ? '~r~SPECTATING ' : (adm.invis ? '~o~INVIS ' : '')) + '~w~' + adm.name + ' [' + adm.dist + 'm]';
                mp.game.graphics.drawText(label, [scr.x, scr.y], {
                    font: 0, color: [255, 255, 255, 230], scale: [0.3, 0.3], outline: true, centre: true
                });
            }
        } catch(e) {}
    }

    function startAdminEsp() {
        if (adminEspActive) return;
        adminEspActive = true;
        mp.events.add('render', adminEspRender);
        log('Admin ESP ON');
    }

    function stopAdminEsp() {
        adminEspActive = false;
        try { mp.events.remove('render', adminEspRender); } catch(e) {}
        log('Admin ESP OFF');
    }

    // Auto-start admin detector
    qtSetTimeout(startAdminDetector, 1000);

    // ---- Weapon Attachments Logic ----
    function getWeaponName(hash) {
        const n = Number(hash);
        return WPN_NAMES[n] || WPN_NAMES[n >>> 0] || ('Weapon #' + (n >>> 0));
    }

    function sh(hash) { return hash >> 0; }

    function normalizeWeaponAttachments(cfg) {
        const out = {};
        try {
            for (const wh in (cfg || {})) {
                const key = String(Number(wh) >>> 0);
                out[key] = Object.assign(out[key] || {}, cfg[wh] || {});
            }
        } catch(e) {}
        return out;
    }

    function getCompatibleComps(weaponHash) {
        const result = [];
        if (!weaponHash || weaponHash === 0xA2719263) return result;
        const ped = mp.players.local.handle;
        const compat = WPN_COMPAT[weaponHash] || WPN_COMPAT[weaponHash >>> 0];
        if (compat) {
            for (let i = 0; i < WPN_COMPS.length; i++) {
                if (compat.indexOf(WPN_COMPS[i].h) !== -1) {
                    let on = false;
                    try { on = !!mp.game.invoke('0xC593212475FAE340', ped, sh(weaponHash), sh(WPN_COMPS[i].h)); } catch(e) {}
                    result.push({ h: WPN_COMPS[i].h, n: WPN_COMPS[i].n, t: WPN_COMPS[i].t, on: on });
                }
            }
        } else {
            for (let i = 0; i < WPN_COMPS.length; i++) {
                try {
                    if (mp.game.invoke('0x5CEE3DF569CECAB0', sh(weaponHash), sh(WPN_COMPS[i].h))) {
                        let on = false;
                        try { on = !!mp.game.invoke('0xC593212475FAE340', ped, sh(weaponHash), sh(WPN_COMPS[i].h)); } catch(e) {}
                        result.push({ h: WPN_COMPS[i].h, n: WPN_COMPS[i].n, t: WPN_COMPS[i].t, on: on });
                    }
                } catch(e) {}
            }
        }
        return result;
    }

    function ensureWeapon(ped, wh) {
        try {
            if (!mp.game.invoke('0x8DECB02F88F428BC', ped, sh(wh), false)) {
                mp.game.invoke('0xBF0FD6E56C964FCB', ped, sh(wh), 9999, false, true);
            }
        } catch(e) {}
    }

)QTJSX91"
        R"QTJSX91(    let hasWeaponAttachments = false;
    mp.events.add('render', () => {
        if (!hasWeaponAttachments) return;
        try {
            const wh = mp.players.local.weapon;
            if (!wh || wh === 0xA2719263) return;
            const saved = weaponAttachments[String(wh >>> 0)] || weaponAttachments[wh] || weaponAttachments[wh >>> 0];
            if (!saved) return;
            const ped = mp.players.local.handle;
            ensureWeapon(ped, wh);
            for (const ch in saved) {
                if (saved[ch]) mp.game.invoke('0xD966D51AA5B28BB9', ped, sh(wh), sh(parseInt(ch)));
            }
        } catch(e) {}
    });

    function saveWpnConfig() {
        if (qtExternalConnected) {
            qtExternalSink({ type: 'wpn_config', data: weaponAttachments });
        } else {
            qtExternalSink('/wpn-config', weaponAttachments);
        }
    }

    function toggleWeaponComp(weaponHash, compHash, on) {
        const key = String(weaponHash >>> 0);
        if (!weaponAttachments[key]) weaponAttachments[key] = {};
        weaponAttachments[key][compHash] = on;
        hasWeaponAttachments = true;
        const ped = mp.players.local.handle;
        ensureWeapon(ped, weaponHash);
        try {
            if (on) mp.game.invoke('0xD966D51AA5B28BB9', ped, sh(weaponHash), sh(compHash));
            else mp.game.invoke('0x1E8BE90C74FB4C09', ped, sh(weaponHash), sh(compHash));
        } catch(e) {}
        saveWpnConfig();
    }

    function updateWeaponGui() {
        return; // GUI removed
    }

    function startWeaponCheck() {
        if (weaponCheckTimer) return;
        weaponCheckTimer = qtSetInterval(() => {
            try {
                const wh = mp.players.local.weapon;
                if (wh !== lastDetectedWeapon) {
                    lastDetectedWeapon = wh;
                }
            } catch(e) {}
        }, 500);
    }

    function stopWeaponCheck() {
        if (weaponCheckTimer) { qtClearInterval(weaponCheckTimer); weaponCheckTimer = null; }
    }

    // ---- Custom HitFX Module ----
    const HITFX_HEAD_BONE = 20;
    // Quant RP weapon damage table (hash -> base damage)
    const HITFX_WPN_DMG = {94989220:15,100416529:45,126349499:1,137902532:12,171789620:14,177293209:110,205991906:90,317205821:4,324215364:14,419712736:15,453432689:12,487013001:15,584646201:14,727643628:11,736523883:14,961495388:14,984333226:40,1119849093:10,1141786504:10,1198256469:15,1317494643:15,1432025498:15,1470379660:65,1593441988:16,1627465347:14,1649403952:14,1703483498:15,1737195953:10,1785463520:25,1853742572:25,2017895192:15,2024373456:14,2132975508:14,2144741730:14,2210333304:14,2227010557:13,2228681469:11,2285322324:10,2343591895:10,2441047180:53,2460120199:18,2484171525:10,2508868239:15,2526821735:14,2548703416:20,2578377531:14,2578778090:20,2634544996:14,2636060646:14,2640438543:15,2725352035:10,2828843422:30,2937143193:14,2939590305:24,3056410471:10,3173288789:14,3218215474:10,3219281620:16,3220176749:14,3231910285:14,3249783761:45,3342088282:21,3347935668:14,3415619887:45,3441901897:25,3520460075:14,3523564046:16,3638508604:10,3675956304:14,3686625920:11,3696079510:73,3713923289:25,3756226112:15,3800352039:15,4019527611:15,4024951519:14,4191993645:23,4192643659:15,4208062921:14};
    // Quant RP bone multipliers (boneIdx -> multiplier)
    const HITFX_BONE_MULT = {0:1,1:.75,2:.75,3:.75,4:.75,5:.75,6:.75,7:1,8:1,9:1,10:1,11:1,12:.75,13:.75,14:.75,15:1,16:.75,17:.75,18:.75,19:1,20:1.5};
    // Shotgun group hash
    const HITFX_SHOTGUN_GROUP = 860033945;
    const HITFX_SMG_GROUP = 3337201093;

    function hitfxInit() {
        if (!hitfx.enabled) return;
        try { mp.events.call('setHitmarker', false, true); } catch(e) {}
        hitfxInitAudio();
    }

    function hitfxInitAudio() {
        try {
            safeBrowserExecute(`(function(){
                if(window._hitfxReadyV2) return;
                window._hitfxReadyV2 = true;
                window._hitfxPool = [];
                window._hitfxPoolSize = 24;
                window._hitfxLastUrl = '';
                window._hitfxCtx = null;
                window._hitfxBuffer = null;
                window._hitfxBufferUrl = '';
                for(var i=0;i<window._hitfxPoolSize;i++){
                    var a=new Audio();a.preload='auto';window._hitfxPool.push(a);
                }
                window._hitfxIdx = 0;
                window._hitfxEnsureCtx = function(){
                    try{
                        var C = window.AudioContext || window.webkitAudioContext;
                        if(C && !window._hitfxCtx) window._hitfxCtx = new C();
                        if(window._hitfxCtx && window._hitfxCtx.state === 'suspended') window._hitfxCtx.resume().catch(function(){});
                        return window._hitfxCtx;
                    }catch(e){return null}
                };
                window._hitfxPreload = function(url, vol) {
                    try {
                        if(!url || url === window._hitfxLastUrl) return;
                        window._hitfxLastUrl = url;
                        window._hitfxBuffer = null;
                        window._hitfxBufferUrl = url;
                        for(var j=0;j<window._hitfxPool.length;j++){
                            var a = window._hitfxPool[j];
                            a.src=url;
                            a.volume=Math.max(0,Math.min(1,vol||0.5));
                            a.load();
                        }
                        var ctx = window._hitfxEnsureCtx();
                        if(ctx && window.fetch){
                            fetch(url,{cache:'force-cache'}).then(function(r){return r.arrayBuffer()}).then(function(buf){
                                try{
                                    var decoded = ctx.decodeAudioData(buf.slice(0));
                                    if(decoded && decoded.then) decoded.then(function(b){if(window._hitfxBufferUrl===url)window._hitfxBuffer=b}).catch(function(){});
                                    else ctx.decodeAudioData(buf,function(b){if(window._hitfxBufferUrl===url)window._hitfxBuffer=b},function(){});
                                }catch(e){
                                    try{ctx.decodeAudioData(buf,function(b){if(window._hitfxBufferUrl===url)window._hitfxBuffer=b},function(){})}catch(x){}
                                }
                            }).catch(function(){});
                        }
                    } catch(e) {}
                };
                window._hitfxPlay = function(url, vol) {
                    try {
                        if(url !== window._hitfxLastUrl){
                            window._hitfxPreload(url, vol);
                        }
                        var ctx = window._hitfxEnsureCtx();
                        if(ctx && window._hitfxBuffer && window._hitfxBufferUrl === url){
                            var src = ctx.createBufferSource();
                            var gain = ctx.createGain();
                            src.buffer = window._hitfxBuffer;
                            gain.gain.value = Math.max(0, Math.min(1, vol));
                            src.connect(gain);
                            gain.connect(ctx.destination);
                            src.start(0);
                            return;
                        }
                        var a = window._hitfxPool[window._hitfxIdx];
                        window._hitfxIdx = (window._hitfxIdx + 1) % window._hitfxPoolSize;
                        a.volume = Math.max(0, Math.min(1, vol));
                        a.currentTime = 0;
                        a.play().catch(function(){});
                    } catch(e) {}
                };
            })()`);
            hitfxAudioReady = true;
        } catch(e) {}
    }

    function hitfxPlaySound(isHead) {
        if (!hitfx.sound || !hitfx.enabled) return;
        if (isHead && !hitfx.headSound) return;
        const url = hitfx.soundUrl;
        if (!url) {
            hitfxBuiltinSoundQueue = Math.min(16, hitfxBuiltinSoundQueue + 1);
            return;
        }
        hitfxSoundQueue = Math.min(16, hitfxSoundQueue + 1);
    }

    function hitfxFlushSoundQueue() {
        if (!hitfx.sound || !hitfx.enabled) {
            hitfxSoundQueue = 0;
            hitfxBuiltinSoundQueue = 0;
            return;
        }
        const now = Date.now();
        if (now - hitfxLastSoundFlush < 30) return;
        if (!hitfx.soundUrl && hitfxBuiltinSoundQueue) {
            const count = Math.min(4, hitfxBuiltinSoundQueue);
            hitfxBuiltinSoundQueue -= count;
            hitfxLastSoundFlush = now;
            for (let i = 0; i < count; i++) {
                try { mp.events.call('enableSound', 'hitmarker.mp3', hitfx.soundVol); } catch(e) {}
            }
            return;
        }
        if (!hitfxSoundQueue || !hitfx.soundUrl) return;
        const count = Math.min(8, hitfxSoundQueue);
        hitfxSoundQueue -= count;
        hitfxLastSoundFlush = now;
        const vol = hitfx.soundVol;
        const urlEsc = hitfx.soundUrl.replace(/'/g, "\\'");
        safeBrowserExecute(`(function(){if(window._hitfxPlay){for(var i=0;i<${count};i++)window._hitfxPlay('${urlEsc}',${vol})}})()`);
    }

    function hitfxAddMarker(targetEntity, damage, isHead, maxDmg) {
        if (!hitfx.showNumbers || !hitfx.enabled) return;
        try {
            const pos = targetEntity.position;
            const sp = hitfx.spread;
            const dist = hitfxGetPlayerDistance(pos);
            const dynHeight = (hitfx.heightOffset || 0.85) + Math.min(0.75, Math.max(0, dist - 12) * 0.018);
            hitfxMarkers.push({
                ts: Date.now(),
                damage: Math.round(damage),
                maxDmg: maxDmg || Math.round(damage),
                head: isHead,
                wx: pos.x + (Math.random() - 0.5) * sp,
                wy: pos.y + (Math.random() - 0.5) * sp,
                wz: pos.z + dynHeight + Math.random() * 0.15,
            });
        } catch(e) {}
    }

    function hitfxLerpColor(amount, maxAmount) {
        const ratio = Math.max(0, Math.min(1, amount / (maxAmount || 1)));
        return [Math.round(255 * ratio), Math.round(255 * (1 - ratio)), 0];
    }

    function hitfxDrawText(text, sx, sy, alpha, dmg, maxDmg, scale) {
        const c = (hitfx.originalColors && dmg !== undefined) ? hitfxLerpColor(dmg, maxDmg) : hitfx.color;
        const s = (scale || 1.0) * hitfx.fontSize;
        mp.game.invoke('0x66E0276CC5F6B9DA', hitfx.fontId);
        mp.game.invoke('0x07C837F9A01C34C9', s * (hitfx.stretchX || 1), s * (hitfx.stretchY || 1));
        mp.game.invoke('0xBE6B23FFA53FB442', c[0], c[1], c[2], alpha);
        if (hitfx.outline) {
            mp.game.invoke('0x2513DFB0FB8400FE');
            const oc = hitfx.outlineColor || [0,0,0,255];
            mp.game.invoke('0x441603240D202FA6', hitfx.outlineWidth || 2, oc[0], oc[1], oc[2], Math.round(alpha * 0.9));
        }
)QTJSX91"
        R"QTJSX91(        if (hitfx.shadow) mp.game.invoke('0x1CA3E9EAC9D93E5E', 2, 0, 0, 0, Math.round(alpha * 0.7));
        mp.game.invoke('0xC02F4DBFB51D988B', true);
        mp.game.invoke('0x25FBB336DF1804CB', 'STRING');
        mp.game.invoke('0x6C188BE134E074AA', text);
        mp.game.invoke('0xCD015E5BB0D96A57', sx, sy);
    }

    function hitfxDrawTextWorld(text, wx, wy, wz, alpha, dmg, maxDmg) {
        const c = (hitfx.originalColors && dmg !== undefined) ? hitfxLerpColor(dmg, maxDmg) : hitfx.color;
        const s = Math.max(0.1, hitfx.fontSize * 0.73);
        try {
            mp.game.graphics.drawText(text, [wx, wy, wz], {
                font: hitfx.fontId,
                centre: true,
                color: [c[0], c[1], c[2], alpha],
                scale: [s * (hitfx.stretchX || 1), s * (hitfx.stretchY || 1)],
                outline: !!hitfx.outline,
                shadow: !!hitfx.shadow
            });
        } catch(e) {
            const screen = mp.game.graphics.world3dToScreen2d(new mp.Vector3(wx, wy, wz));
            if (screen) hitfxDrawText(text, screen.x, screen.y, alpha, dmg, maxDmg, 1.0);
        }
    }

    function hitfxIsCefFont() {
        return hitfx.fontId === 100 || hitfx.fontId === 101;
    }

    function hitfxDrawCefMarkers(items) {
        if (!items.length) return;
        const payload = JSON.stringify(items);
        safeBrowserExecute(`(function(){
            try{
                var root=document.getElementById('qt-hitfx-overlay');
                if(!root){
                    root=document.createElement('div');
                    root.id='qt-hitfx-overlay';
                    root.style.cssText='position:fixed;left:0;top:0;width:100vw;height:100vh;pointer-events:none;z-index:2147483647;overflow:hidden';
                    document.body.appendChild(root);
                }
                var items=${payload};
                root.innerHTML='';
                for(var i=0;i<items.length;i++){
                    var it=items[i], el=document.createElement('div');
                    el.textContent=it.text;
                    el.style.cssText='position:absolute;left:'+(it.x*100)+'vw;top:'+(it.y*100)+'vh;transform:translate(-50%,-50%) scale('+it.sx+','+it.sy+');transform-origin:center center;font-family:'+it.font+',Arial,sans-serif;font-size:'+it.size+'px;font-weight:700;line-height:1;color:rgba('+it.color[0]+','+it.color[1]+','+it.color[2]+','+(it.alpha/255)+');white-space:nowrap;';
                    if(it.outline) el.style.webkitTextStroke=it.strokeW+'px rgba('+it.strokeColor[0]+','+it.strokeColor[1]+','+it.strokeColor[2]+','+(it.alpha/255)+')';
                    if(it.shadow) el.style.textShadow='0 2px 4px rgba(0,0,0,'+(it.alpha/255*0.7)+')';
                    root.appendChild(el);
                }
            }catch(e){}
        })()`);
    }

    function hitfxDrawMarkers() {
        if (!hitfx.enabled || !hitfx.showNumbers || !hitfxMarkers.length) {
            if (hitfxIsCefFont()) safeBrowserExecute(`(function(){var r=document.getElementById('qt-hitfx-overlay');if(r)r.innerHTML=''})()`);
            return;
        }
        const now = Date.now();
        while (hitfxMarkers.length && now - hitfxMarkers[0].ts > hitfx.duration) hitfxMarkers.shift();
        if (!hitfxMarkers.length) {
            if (hitfxIsCefFont()) safeBrowserExecute(`(function(){var r=document.getElementById('qt-hitfx-overlay');if(r)r.innerHTML=''})()`);
            return;
        }

        const cefItems = [];
        const cefFont = hitfxIsCefFont();
        const ppos = mp.players.local.position;
        for (let i = 0; i < hitfxMarkers.length; i++) {
            const m = hitfxMarkers[i];
            const t = (now - m.ts) / hitfx.duration;
            const z = m.wz + t * hitfx.floatDist;
            const alpha = Math.max(0, Math.round(255 * (1 - t * t)));
            if (cefFont) {
                const screen = mp.game.graphics.world3dToScreen2d(new mp.Vector3(m.wx, m.wy, z));
                if (!screen) continue;
                let scale = 1.0;
                if (hitfx.distScale) {
                    const dist = Math.sqrt((m.wx-ppos.x)**2 + (m.wy-ppos.y)**2 + (m.wz-ppos.z)**2);
                    scale = Math.max(0.75, Math.min(1.35, 1.15 - Math.max(0, dist - 8) * 0.012));
                }
                const c = (hitfx.originalColors && m.damage !== undefined) ? hitfxLerpColor(m.damage, m.maxDmg) : hitfx.color;
                cefItems.push({
                    text: String(m.damage), x: screen.x, y: screen.y, alpha: alpha,
                    size: Math.round(36 * hitfx.fontSize), sx: (hitfx.stretchX || 1) * scale, sy: (hitfx.stretchY || 1) * scale,
                    font: hitfx.fontId === 100 ? 'Verdana' : 'Tahoma',
                    color: c, outline: !!hitfx.outline, shadow: !!hitfx.shadow,
                    strokeW: hitfx.outline ? Math.max(1, Math.round((hitfx.outlineWidth || 2) * 0.6)) : 0,
                    strokeColor: hitfx.outlineColor || [0,0,0,255]
                });
                continue;
            }
            if (hitfx.distScale) {
                hitfxDrawTextWorld(String(m.damage), m.wx, m.wy, z, alpha, m.damage, m.maxDmg);
                continue;
            }
            const screen = mp.game.graphics.world3dToScreen2d(new mp.Vector3(m.wx, m.wy, z));
            if (!screen) continue;
            hitfxDrawText(String(m.damage), screen.x, screen.y, alpha, m.damage, m.maxDmg, 1.0);
        }
        if (cefFont) hitfxDrawCefMarkers(cefItems);
    }

    function hitfxCalcDamage(sourcePlayer, targetEntity, weaponHash, boneIdx) {
        const baseDmg = HITFX_WPN_DMG[weaponHash >>> 0];
        if (!baseDmg) return 0;
        let d = baseDmg;
        let boneMult = HITFX_BONE_MULT[boneIdx] ?? 1;
        const wpnGroup = mp.game.weapon.getWeapontypeGroup(weaponHash);
        if (boneIdx === 20 && (weaponHash === 3249783761 || weaponHash === 2548703416)) boneMult = 2;
        else if (boneIdx === 20 && wpnGroup === HITFX_SHOTGUN_GROUP) boneMult = 1;
        d *= boneMult;
        try {
            const dist = mp.game.system.vdist(
                targetEntity.position.x, targetEntity.position.y, targetEntity.position.z,
                sourcePlayer.position.x, sourcePlayer.position.y, sourcePlayer.position.z
            );
            if (wpnGroup === HITFX_SHOTGUN_GROUP || weaponHash === 2828843422) {
                d = Math.max(5, d - Math.max(0, dist - 1));
            } else if (wpnGroup === HITFX_SMG_GROUP || weaponHash === 584646201) {
                if (dist > 10) d -= (dist - 10) * 0.1;
                if (d < 10) d = 10;
            }
        } catch(e) {}
        if (wpnGroup !== 3082541095 && d > 90) d = 90;
        // Drug effects: reduce HP damage only (armour portion unchanged)
        try {
            const drugFx = targetEntity.variable_drugEffects || (targetEntity.getVariable && targetEntity.getVariable('drugEffects'));
            if (drugFx && typeof drugFx === 'object') {
                let totalReduce = 0;
                for (const key in drugFx) {
                    const eff = drugFx[key];
                    if (eff && typeof eff.reduce === 'number') totalReduce += eff.reduce;
                }
                if (totalReduce > 0) {
                    const armour = targetEntity.getArmour ? targetEntity.getArmour() : 0;
                    const armourAbsorb = Math.min(armour, d);
                    const hpDmg = Math.max(0, d - armour);
                    d = armourAbsorb + hpDmg * Math.max(0, 1 - totalReduce);
                }
            }
        } catch(e) {}
        if (sourcePlayer.vehicle && sourcePlayer.vehicle.getPedInSeat(-1) === sourcePlayer.handle) d = 1;
        return Math.round(d);
    }

    function hitfxQueueDamage(sourcePlayer, targetEntity, weapon, boneIdx) {
        if (hitfxPendingHits.length > 48) hitfxPendingHits.shift();
        hitfxPendingHits.push({
            sourcePlayer: sourcePlayer,
            targetEntity: targetEntity,
            weapon: weapon,
            boneIdx: boneIdx
        });
    }

    function hitfxProcessPendingHits() {
        if (!hitfx.enabled) {
            hitfxPendingHits.length = 0;
            return;
        }
        let processed = 0;
        while (hitfxPendingHits.length && processed < 8) {
            const h = hitfxPendingHits.shift();
            processed++;
            try {
                const targetEntity = h.targetEntity;
                if (!targetEntity || targetEntity.type !== 'player') continue;
                const isHead = h.boneIdx === HITFX_HEAD_BONE;
                if (hitfx.showNumbers) {
                    const realDmg = hitfxCalcDamage(h.sourcePlayer, targetEntity, h.weapon, h.boneIdx);
                    const baseDmg = HITFX_WPN_DMG[h.weapon >>> 0] || 0;
                    const maxDmg = Math.floor(baseDmg * 1.5);
                    if (realDmg > 0) hitfxAddMarker(targetEntity, realDmg, isHead, maxDmg);
                }
                hitfxPlaySound(isHead);
            } catch(e) {}
        }
    }

    mp.events.add('outgoingDamage', (sourcePlayer, targetEntity, sourceEntity, weapon, boneIdx, damage) => {
        try {
            if (!hitfx.enabled) return;
            if (!targetEntity || targetEntity.type !== 'player') return;
            hitfxQueueDamage(sourcePlayer, targetEntity, weapon, boneIdx);
        } catch(e) {}
    });

    mp.events.add('render', () => {
        try { hitfxProcessPendingHits(); } catch(e) {}
        try { hitfxFlushSoundQueue(); } catch(e) {}
        try { hitfxDrawMarkers(); } catch(e) {}
    });

    function hitfxSaveConfig() {
        if (qtExternalConnected) {
            qtExternalSink({ type: 'hitfx_config', data: hitfx });
        }
    }

    function hitfxApplyConfig(cfg) {
        if (!cfg || typeof cfg !== 'object') return;
        const keys = ['enabled','showNumbers','color','originalColors','fontSize','fontId','stretchX','stretchY','outline','outlineWidth','outlineColor','shadow','duration','floatDist','spread','heightOffset','distScale','sound','soundUrl','soundVol','headSound'];
        keys.forEach(k => { if (cfg[k] !== undefined) hitfx[k] = cfg[k]; });
        if (hitfx.enabled) hitfxInit();
        if (hitfx.soundUrl) { hitfxInitAudio(); safeBrowserExecute(`(function(){if(window._hitfxPreload)window._hitfxPreload('${hitfx.soundUrl.replace(/'/g, "\\'")}',${hitfx.soundVol})})()`); }
    }

    // ---- Server UI Theme Module (identical classic Majestic HUD for Quant) ----
    function qtUiThemeSave() {
        if (qtExternalConnected) qtExternalSink({ type: 'uitheme_config', data: qtUiTheme });
    }
    function qtUiThemeApplyConfig(cfg) {
        // Redesign temporarily disabled — always force off
        qtUiTheme.enabled = false;
        qtUiTheme.editHud = false;
        if (cfg && typeof cfg === 'object' && cfg.opacity !== undefined)
            qtUiTheme.opacity = Math.max(0.35, Math.min(1, Number(cfg.opacity) || 1));
)QTJSX91"
        R"QTJSX91(        qtUiThemeApply();
    }
    function qtUiThemeShellCss() {
        return ":root{--IntegratedFont:\"ProximaNova\",\"Proxima Nova\",\"Segoe UI\",Arial,sans-serif}html.qt-mj-on #hud-container,html.qt-mj-on #hud-container.hud,html.qt-mj-on section.hud,html.qt-mj-on .hud,html.qt-mj-on .topright-wrapper,html.qt-mj-on .info-wrapper,html.qt-mj-on .bank-wrap,html.qt-mj-on .server-logo-wrap,html.qt-mj-on .hudpanel,html.qt-mj-on .hudpanel-wrapper,html.qt-mj-on .main-chat-wrapper,html.qt-mj-on .chat-bg,html.qt-mj-on .chat-slide,html.qt-mj-on .main-chat-input,html.qt-mj-on .main-chat-list,html.qt-mj-on [class*=\"killList\"],html.qt-mj-on .kill-message{opacity:0!important;visibility:hidden!important;pointer-events:none!important}html.qt-mj-on .inventory-overflow,html.qt-mj-on .inventory-wrapper,html.qt-mj-on .inventory-main-wrapper{position:fixed!important;left:-10000px!important;top:0!important;opacity:0!important;pointer-events:none!important;z-index:-1!important}#qt-mj-root{position:fixed;inset:0;z-index:2147482000;pointer-events:none;font-family:var(--IntegratedFont),sans-serif;color:#fff}#qt-mj-root *{box-sizing:border-box}.row-block{display:flex;flex-direction:row}.column-block{display:flex;flex-direction:column}.align-center{display:flex;align-items:center}.align-end{display:flex;align-items:flex-end}.full-width{width:100%}.full-height{height:100%}.main-cover{position:absolute;left:0;top:0;width:100%;height:100%;overflow:hidden;pointer-events:none}.hud-shadow{position:absolute;right:0;top:0;width:37.037vh;height:37.037vh;background-image:radial-gradient(circle at 100% 0,rgba(0,0,0,.45),transparent 70%);pointer-events:none}.online{position:absolute;right:1.0185vh;margin-top:1.9444vh;pointer-events:auto}.online__logo{max-height:3.1019vh}.online__logo img,.online__logo__picture{width:11.5741vh;height:auto;display:block;filter:drop-shadow(0 .093vh .185vh rgba(0,0,0,.32))}.online .server-info{margin-top:.9722vh}.online .server-info .online-text{gap:.3704vh}.online .server-info .online-text__title{font-weight:400;font-size:1.2963vh;color:#f8f8f8;text-shadow:0 0 .093vh rgba(0,0,0,.75)}.online .server-info .online-text__text{font-weight:500;font-size:1.2963vh;color:#f8f8f8;text-shadow:0 0 .093vh rgba(0,0,0,.25)}.online .server-info .server-id{padding:.3704vh .7407vh;border-radius:.6481vh;background-color:rgba(30,33,36,.25);margin-left:.5556vh}.online .server-info .server-id.no-flag{padding:.3704vh .7407vh}.online .server-info .server-id__text{font-weight:500;font-size:.9259vh;color:#f8f8f8}.online .player-id{margin-top:.7407vh}.online .player-id__text-1{font-weight:500;font-size:1.2963vh;color:rgba(255,255,255,.75);text-shadow:0 0 .093vh rgba(0,0,0,.25)}.online .player-id__text-2{font-weight:500;font-size:1.2963vh;color:#fff;margin-left:.3704vh;text-shadow:0 0 .093vh rgba(0,0,0,.25)}.online .bullets-block{margin-top:.7407vh}.online .bullets-block__picture{width:1.6667vh;height:1.6667vh}.online .bullets-block__text{margin-right:.3704vh}.online .bullets-block__text span:first-child{font-weight:500;font-size:1.4815vh;color:#fff}.online .bullets-block__text span:last-child{font-weight:500;font-size:1.4815vh;color:rgba(255,255,255,.75)}.online .wanted{margin-top:.8333vh}.online .wanted-star{opacity:.1;width:1.9444vh;height:1.9444vh;margin-right:.463vh}.online .wanted-star:last-child{margin-right:0}.online .wanted-star.active{opacity:1}.user-container{display:inline-flex;flex-direction:column;position:absolute;left:0;bottom:1.5741vh;margin-left:18vh;text-shadow:0 .093vh .185vh rgba(0,0,0,.32);pointer-events:auto}.money{display:flex;flex-direction:column;margin-top:1.2963vh}.money .money-container{display:flex;flex-direction:column;gap:.5556vh}.money .money-container .money-block{display:flex;flex-direction:row;align-items:center;gap:.8333vh}.money .money-container .money-block__logo{display:flex;justify-content:center;align-items:center;filter:drop-shadow(0 .093vh .185vh rgba(0,0,0,.32))}.money .money-container .money-block__logo svg,.money .money-container .money-block__logo img{width:1.4815vh;height:1.4815vh;display:block}.money .money-container ";
    }
    function qtUiThemeShellCss2() {
        return ".money-block.cash_hud,.money .money-container .money-block.bank_hud{font-weight:500;font-size:1.6667vh;color:#fff}.money .money-container .money-block.positive{color:#a7fa53!important}.money .money-container .money-block.negative{color:#fa8072!important}.money .money-container .money-block__value{transition:color ease .5s}.mj-hud-item.mj-editable{outline:1px dashed rgba(232,28,90,.35);cursor:grab}.mj-hud-item.mj-editable:hover{outline-color:rgba(232,28,90,.75)}.mj-hud-item.dragging{cursor:grabbing;outline:1px solid #e81c5a;z-index:30}html.qt-mj-edit #qt-mj-root::after{content:\"HUD EDIT\";position:fixed;top:1.2vh;left:50%;transform:translateX(-50%);z-index:2147483600;pointer-events:none;background:rgba(232,28,90,.9);color:#fff;font-weight:700;letter-spacing:.14em;font-size:1.2vh;padding:.45vh 1.2vh;border-radius:.45vh}#qt-mj-chat{position:absolute;left:2.2vh;bottom:22vh;width:42vh;max-height:32vh;pointer-events:none;display:flex;flex-direction:column}#qt-mj-chat .mj-chat-list{display:flex;flex-direction:column;gap:.2vh;overflow:hidden}#qt-mj-chat .mj-line{font-size:1.35vh;line-height:1.35;color:#fff;text-shadow:0 0 .2vh #000,-1px 0 #000,0 1px #000;paint-order:stroke fill;word-break:break-word}#qt-mj-chat .mj-chat-input{pointer-events:auto;margin-top:.55vh;height:4.167vh;background:rgba(24,24,24,.85);border:.185vh solid rgba(46,46,46,.85);border-radius:.556vh;color:#fff;padding:0 1vh;font-size:1.481vh;outline:0;width:100%;font-family:var(--IntegratedFont),sans-serif}#qt-mj-inv{position:absolute;inset:0;display:none;align-items:center;justify-content:center;background:rgba(37,37,37,.75);pointer-events:auto;z-index:5}#qt-mj-inv.open{display:flex}#qt-mj-inv .mj-inv{width:min(128vh,96vw);max-height:90vh;background:#111;border-radius:1.11vh;border:1px solid rgba(255,255,255,.05);box-shadow:0 1.2vh 3.2vh rgba(0,0,0,.6);padding:2.6vh 3vh;display:flex;flex-direction:column;gap:2vh;overflow:auto;color:#f5f5f5}#qt-mj-inv .mj-inv-h{display:flex;justify-content:space-between;align-items:center}#qt-mj-inv .mj-inv-h h2{margin:0;font-size:1.9vh;font-weight:900;text-transform:uppercase;letter-spacing:.05em}#qt-mj-inv .mj-close{width:3.2vh;height:3.2vh;border:0;border-radius:.55vh;background:rgba(255,255,255,.08);color:#fff;cursor:pointer}#qt-mj-inv .mj-inv-body{display:grid;grid-template-columns:24vh 1fr 22vh;gap:2vh;min-height:52vh}#qt-mj-inv .mj-col{background:rgba(255,255,255,.02);border:1px solid rgba(255,255,255,.04);border-radius:.9vh;padding:1.2vh}#qt-mj-inv .mj-col h3{margin:0 0 1vh;font-size:1.3vh;font-weight:900;text-transform:uppercase;color:#f9bbce}#qt-mj-inv .mj-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:.55vh}#qt-mj-inv .mj-grid.q{grid-template-columns:repeat(3,1fr)}#qt-mj-inv .mj-slot{aspect-ratio:1;background:#1a1a1a;border:1px solid #2a2a2a;border-radius:.55vh;position:relative;cursor:grab;overflow:hidden;user-select:none}#qt-mj-inv .mj-slot:hover{border-color:rgba(232,28,90,.55)}#qt-mj-inv .mj-slot.mj-dragging{opacity:.35}#qt-mj-inv .mj-slot img{width:70%;height:70%;object-fit:contain;position:absolute;left:15%;top:10%;pointer-events:none}#qt-mj-inv .mj-slot .c{position:absolute;right:.35vh;bottom:.25vh;font-size:1.1vh;font-weight:800;pointer-events:none}#qt-mj-inv .mj-slot .n{position:absolute;left:.35vh;top:.25vh;font-size:1vh;color:#a8a8a8;pointer-events:none}#qt-mj-inv .mj-slot.mj-empty{opacity:.45;cursor:default}#qt-mj-inv .mj-drop{margin-top:1.2vh;height:6vh;border:1px dashed rgba(232,28,90,.45);border-radius:.7vh;display:flex;align-items:center;justify-content:center;color:#f9bbce;font-weight:900;letter-spacing:.12em;font-size:1.2vh}#qt-mj-inv .mj-drop.hot{background:rgba(232,28,90,.22);border-style:solid}#qt-mj-ghost{position:fixed;z-index:2147483000;width:7vh;height:7vh;pointer-events:none;display:none;background:rgba(0,0,0,.64);border:1px solid rgba(249,187,206,.35);border-radius:.55vh;overflow:hidden}#qt-mj-ghost.show{display:block}#qt-mj-ghost img{width:70%;height:70%;object-fit:contain;position:absolute;left:15%;top:12%}";
    }
    function qtUiThemeShellBootCode() {
        return [
            "(function(){\nif(window._qtMjShellV6){window._qtMjShellV6.refresh&&window._qtMjShellV6.refresh();return;}\nvar CDN='https://cdn.majestic-files.net/public/master/static/img/main/hud/';\nvar IDX={clothes:[6,1,10,5,3,12,7,2,4,11,14,13,8,9],quick:[88,89,90,91,92],main:[]};\nfor(var mi=15;mi<=74;mi++)IDX.main.push(mi);\nvar root=null,chatList=null,chatInput=null,invRoot=null,invClothes=null,invMain=null,invQuick=null;\nvar drag={active:false,from:null,moved:false,sx:0,sy:0};\nvar panelDrag=null;\nvar HUD_DEFAULTS={\n  online:{right:'1.0185vh',top:'1.9444vh'},\n  money:{left:'18vh',bottom:'1.5741vh'},\n  chat:{left:'2.2vh',bottom:'22vh',width:'42vh'}\n};\nwindow._qtMjHudData=window._qtMjHudData||{money:null,bank:null,online:null,id:null,staticId:null,wanted:0,clip:null,ammo:null};\nfunction el(t,c,p){var n=document.createElement(t);if(c)n.className=c;if(p)p.appendChild(n);return n;}\nfunction esc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}\nfunction fmtMoney(v){\n  if(v==null||v==='')return '—';\n  v=Math.floor(Number(v)||0);\n  var neg=v<0;v=Math.abs(v);\n  var s=String(v).replace(/\\B(?=(\\d{3})+(?!\\d))/g,' ');\n  return (neg?'-':'')+s;\n}\nfunction svgCash(){return '<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1.41 16.09V20h-2.67v-1.93c-1.71-.36-3.16-1.46-3.27-3.4h1.96c.1.93.84 1.62 2.24 1.62 1.54 0 2.3-.7 2.3-1.69 0-.96-.68-1.5-2.37-1.93l-.9-.23c-2.24-.57-3.4-1.7-3.4-3.56 0-1.95 1.5-3.27 3.44-3.6V4h2.67v1.95c1.86.45 2.79 1.86 2.85 3.39H14.3c-.05-1.01-.76-1.7-2.1-1.7-1.3 0-2.1.6-2.1 1.54 0 .84.62 1.3 2.22 1.72l.9.24c2.45.64 3.54 1.7 3.54 3.62 0 2.05-1.53 3.4-3.35 3.73z\" fill=\"#a7fa53\"/></svg>';}\nfunction svgBank(){return '<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M4 10v7h3v-7H4zm6.5 0v7h3v-7h-3zM2 22h19v-3H2v3zm14-12v7h3v-7h-3zm-4.5-9L2 6v2h19V6l-9.5-5z\" fill=\"#fff\"/></svg>';}\nfu",
)QTJSX91"
        R"QTJSX91(            "nction svgStar(){return '<svg class=\"wanted-star\" viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\"><path fill=\"#fff\" d=\"M12 17.27L18.18 21l-1.64-7.03L22 9.24l-7.19-.61L12 2 9.19 8.63 2 9.24l5.46 4.73L5.82 21z\"/></svg>';}\nfunction findInv(){\n  try{\n    if(window._noDropInvInstance&&typeof window._noDropInvInstance.updateIndex==='function')return window._noDropInvInstance;\n    if(typeof window._noDropFindInv==='function'){var i=window._noDropFindInv();if(i)return i;}\n  }catch(e){}\n  return null;\n}\nfunction hookHudControl(){\n  try{\n    if(!window.hudControl||window.hudControl.__qtMjHooked)return;\n    var orig=window.hudControl.changeOptions;\n    if(typeof orig!=='function')return;\n    window.hudControl.__qtMjHooked=true;\n    window.hudControl.changeOptions=function(A,t){\n      try{\n        var d=window._qtMjHudData;\n        if(A==='updateMoney')d.money=t;\n        else if(A==='updateBank')d.bank=t;\n        else if(A==='updateWanted')d.wanted=Number(t)||0;\n        else if(A==='setData'&&t&&typeof t==='object'){\n          if(t.money!=null)d.money=t.money;\n          if(t.bank!=null)d.bank=t.bank;\n          if(t.online!=null)d.online=t.online;\n          if(t.id!=null)d.id=t.id;\n          if(t.sqlId!=null)d.staticId=t.sqlId;\n          if(t.wanted!=null)d.wanted=t.wanted;\n          if(t.ammo!=null)d.ammo=t.ammo;\n          if(t.clip!=null)d.clip=t.clip;\n          if(t.weaponAmmo!=null)d.ammo=t.weaponAmmo;\n        }else if(A==='setDataWeapon'&&t&&typeof t==='object'){\n          if(t.ammo!=null)d.ammo=t.ammo;\n          if(t.clip!=null)d.clip=t.clip;\n          if(t.bullets!=null)d.ammo=t.bullets;\n        }\n      }catch(e){}\n      return orig.apply(this,arguments);\n    };\n  }catch(e){}\n}\nfunction scrapeQuantHud(){\n  var d=window._qtMjHudData;\n  try{\n    var bankWrap=document.querySelector('#hud-container .bank-wrap, .topright-wrapper .bank-wrap');\n    if(bankWrap){\n      var ps=bankWrap.querySelectorAll('p.posrev, p');\n      var nums=[];\n      for(var i=0;i<ps.length;i++){\n ",
            "       var raw=(ps[i].textContent||'').replace(/[^\\d\\-]/g,'');\n        if(raw!=='')nums.push(Number(raw));\n      }\n      if(nums.length>=1)d.money=nums[0];\n      if(nums.length>=2)d.bank=nums[1];\n    }\n    var tops=document.querySelectorAll('#hud-container .topline-item, .topright-wrapper .topline-item');\n    for(var j=0;j<tops.length;j++){\n      var tx=(tops[j].textContent||'').trim();\n      var om=tx.match(/online\\s*[:=]?\\s*(\\d+)/i); if(om)d.online=Number(om[1]);\n      var im=tx.match(/id\\s*[:=]?\\s*(\\d+)/i); if(im)d.id=Number(im[1]);\n    }\n  }catch(e){}\n}\nfunction ensure(){\n  if(root&&document.body&&document.body.contains(root))return;\n  root=document.getElementById('qt-mj-root');\n  if(!root){root=el('div');root.id='qt-mj-root';(document.body||document.documentElement).appendChild(root);}\n  var stars=''; for(var s=0;s<5;s++)stars+=svgStar();\n  root.innerHTML=''\n    +'<div class=\"main-cover full-width full-height\">'\n    +'<div class=\"hud-shadow\"></div>'\n    +'<div class=\"online column-block align-end mj-hud-item\" data-hud=\"online\">'\n    +'<div class=\"online__logo row-block\"><img class=\"online__logo__picture\" id=\"mjLogoImg\" src=\"'+CDN+'logotype.svg\" alt=\"\"/></div>'\n    +'<div class=\"server-info row-block align-center\">'\n    +'<div class=\"online-text row-block align-center\"><span class=\"online-text__title\">Online:</span><span class=\"online-text__text\" id=\"mjOnline\">0</span></div>'\n    +'<div class=\"server-id align-center row-block no-flag\"><span class=\"server-id__text\">QUANT</span></div>'\n    +'</div>'\n    +'<div class=\"player-id\"><span class=\"player-id__text-1\">ID: <span id=\"mjId\">0</span> |</span><span class=\"player-id__text-2\">#<span id=\"mjStatic\">0</span></span></div>'\n    +'<div class=\"bullets-block row-block align-center\" id=\"mjAmmo\" style=\"display:none\">'\n    +'<div class=\"bullets-block__text\"><span id=\"mjClip\">0</span><span> / </span><span id=\"mjAmmoVal\">0</span></div>'\n    +'<img class=\"bullets-block__picture\" src=\"'+CDN+'bullets.svg\" alt=\"\"/>'\n    +'</div>'",
            "\n    +'<div class=\"wanted row-block\" id=\"mjWanted\">'+stars+'</div>'\n    +'</div>'\n    +'<div class=\"user-container column-block mj-hud-item\" data-hud=\"money\">'\n    +'<div class=\"money\"><div class=\"money-container\">'\n    +'<div class=\"money-block cash_hud\"><span class=\"money-block__logo cash_hud\" id=\"mjCashIcon\">'+svgCash()+'</span><span class=\"money-block__value cash_hud\" id=\"mjMoney\">0</span></div>'\n    +'<div class=\"money-block bank_hud\"><span class=\"money-block__logo bank_hud\" id=\"mjBankIcon\">'+svgBank()+'</span><span class=\"money-block__value\" id=\"mjBank\">0</span></div>'\n    +'</div></div></div>'\n    +'</div>'\n    +'<div id=\"qt-mj-chat\" class=\"mj-hud-item\" data-hud=\"chat\"><div class=\"mj-chat-list\" id=\"mjChatList\"></div>'\n    +'<input class=\"mj-chat-input\" id=\"mjChatInput\" placeholder=\"\" maxlength=\"200\"/></div>'\n    +'<div id=\"qt-mj-inv\"><div class=\"mj-inv\"><div class=\"mj-inv-h\"><h2>ИНВЕНТАРЬ</h2><button class=\"mj-close\" id=\"mjInvClose\" type=\"button\">X</button></div>'\n    +'<div class=\"mj-inv-body\"><div class=\"mj-col\"><h3>Экипировка</h3><div class=\"mj-grid q\" id=\"mjClothes\"></div></div>'\n    +'<div class=\"mj-col wide\"><h3>Инвентарь</h3><div class=\"mj-grid\" id=\"mjMain\"></div></div>'\n    +'<div class=\"mj-col\"><h3>Быстрый доступ</h3><div class=\"mj-grid q\" id=\"mjQuick\"></div><div class=\"mj-drop\" id=\"mjDropZone\">ВЫКИНУТЬ</div></div></div></div></div>'\n    +'<div id=\"qt-mj-ghost\"></div>';\n  chatList=document.getElementById('mjChatList');\n  chatInput=document.getElementById('mjChatInput');\n  invRoot=document.getElementById('qt-mj-inv');\n  invClothes=document.getElementById('mjClothes');\n  invMain=document.getElementById('mjMain');\n  invQuick=document.getElementById('mjQuick');\n  document.getElementById('mjInvClose').onclick=function(){\n    try{if(window.inventoryAPI&&inventoryAPI.show)inventoryAPI.show(false);}catch(e){}\n    invRoot.classList.remove('open');\n  };\n  chatInput.onkeydown=function(ev){\n    if(ev.key!=='Enter'&&ev.keyCode!==13)return;\n    ev.preventDefault()",
            ";\n    var msg=(chatInput.value||'').trim(); if(!msg)return; chatInput.value='';\n    try{if(typeof mp!=='undefined'&&mp.invoke)mp.invoke('chatMessage',msg);}catch(e){}\n    try{if(typeof mp!=='undefined'&&mp.trigger)mp.trigger('chatMessage',msg);}catch(e){}\n    try{\n      var q=document.querySelector('.main-chat-input, .chat-enter, input.main-chat-input, textarea.main-chat-input');\n      if(q){q.focus();q.value=msg;try{q.dispatchEvent(new Event('input',{bubbles:true}));}catch(e){}\n        try{q.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',code:'Enter',keyCode:13,which:13,bubbles:true}));}catch(e){}}\n    }catch(e){}\n  };\n  var logo=document.getElementById('mjLogoImg');\n  if(logo)logo.onerror=function(){\n    try{var q=document.querySelector('.server-logo-wrap img'); if(q&&q.src)logo.src=q.src;}catch(e){}\n  };\n  bindHudDrag(); bindInvDnD(); applyHudDefaults(); restoreHudPos(); applyEditMode(); hookHudControl();\n}\nfunction isEditMode(){return !!window._qtMjEditHud || document.documentElement.classList.contains('qt-mj-edit');}\nfunction applyEditMode(){\n  var on=isEditMode();\n  document.documentElement.classList.toggle('qt-mj-edit',on);\n  document.querySelectorAll('.mj-hud-item').forEach(function(n){n.classList.toggle('mj-editable',on);});\n}\nfunction applyPos(node,pos){\n  if(!node||!pos)return;\n  node.style.left=pos.left||''; node.style.top=pos.top||'';\n  node.style.right=pos.right||''; node.style.bottom=pos.bottom||'';\n  if(pos.width)node.style.width=pos.width;\n  if(pos.left)node.style.right=pos.right||'auto';\n  if(pos.top)node.style.bottom=pos.bottom||'auto';\n  if(pos.right&&!pos.left)node.style.left='auto';\n  if(pos.bottom&&!pos.top)node.style.top='auto';\n}\nfunction applyHudDefaults(){\n  Object.keys(HUD_DEFAULTS).forEach(function(id){\n    var n=document.querySelector('.mj-hud-item[data-hud=\"'+id+'\"]');\n    applyPos(n,HUD_DEFAULTS[id]);\n  });\n}\nfunction saveHudPos(){\n  try{\n    var o={};\n    document.querySelectorAll('.mj-hud-item[data-hud]').forEach(function(n)",
            "{\n      var id=n.getAttribute('data-hud'); if(!id)return;\n      o[id]={left:n.style.left||'',top:n.style.top||'',right:n.style.right||'',bottom:n.style.bottom||'',width:n.style.width||''};\n    });\n    localStorage.setItem('qtMjHudLayoutV6',JSON.stringify(o));\n  }catch(e){}\n}\nfunction restoreHudPos(){\n  try{\n    var o=JSON.parse(localStorage.getItem('qtMjHudLayoutV6')||localStorage.getItem('qtMjHudLayoutV5')||'{}');\n    Object.keys(o||{}).forEach(function(id){\n      var n=document.querySelector('.mj-hud-item[data-hud=\"'+id+'\"]');\n      if(n)applyPos(n,o[id]);\n    });\n  }catch(e){}\n}\nfunction resetHudPos(id){\n  if(id){var n=document.querySelector('.mj-hud-item[data-hud=\"'+id+'\"]'); if(n&&HUD_DEFAULTS[id])applyPos(n,HUD_DEFAULTS[id]);}\n  else applyHudDefaults();\n  saveHudPos();\n}\nfunction bindHudDrag(){\n  if(!root||root._mjHudDrag)return; root._mjHudDrag=true;\n  root.addEventListener('mousedown',function(ev){\n    if(ev.button!==0||!isEditMode())return;\n    if(ev.target.closest('.mj-slot')||ev.target.closest('.mj-chat-input')||ev.target.closest('.mj-close')||ev.target.closest('#qt-mj-inv'))return;\n    var p=ev.target.closest('.mj-hud-item[data-hud]'); if(!p)return;\n    var r=p.getBoundingClientRect();\n    panelDrag={el:p,ox:ev.clientX-r.left,oy:ev.clientY-r.top};\n    p.classList.add('dragging'); ev.preventDefault();\n  });\n  root.addEventListener('dblclick',function(ev){\n    if(!isEditMode())return;\n    var p=ev.target.closest('.mj-hud-item[data-hud]'); if(!p)return;\n    resetHudPos(p.getAttribute('data-hud')); ev.preventDefault();\n  });\n  window.addEventListener('mousemove',function(ev){\n    if(!panelDrag)return;\n    var p=panelDrag.el;\n    var x=Math.max(0,Math.min(window.innerWidth-p.offsetWidth,ev.clientX-panelDrag.ox));\n    var y=Math.max(0,Math.min(window.innerHeight-p.offsetHeight,ev.clientY-panelDrag.oy));\n    p.style.left=x+'px'; p.style.top=y+'px'; p.style.right='auto'; p.style.bottom='auto';\n  },true);\n  window.addEventListener('mouseup',function(){\n    if(!pa",
)QTJSX91"
        R"QTJSX91(            "nelDrag)return;\n    panelDrag.el.classList.remove('dragging'); panelDrag=null; saveHudPos();\n  },true);\n}\nfunction syncHud(){\n  hookHudControl(); scrapeQuantHud();\n  var d=window._qtMjHudData; var cs=window.clientStorage||{};\n  var money=d.money!=null?d.money:(cs.money!=null?cs.money:null);\n  var bank=d.bank!=null?d.bank:(cs.bank!=null?cs.bank:null);\n  var online=d.online!=null?d.online:(cs.online!=null?cs.online:null);\n  var id=d.id!=null?d.id:(cs.id!=null?cs.id:(cs.sqlId!=null?cs.sqlId:0));\n  var st=d.staticId!=null?d.staticId:(cs.sqlId!=null?cs.sqlId:id);\n  var mEl=document.getElementById('mjMoney'); if(mEl)mEl.textContent=fmtMoney(money);\n  var bEl=document.getElementById('mjBank'); if(bEl)bEl.textContent=fmtMoney(bank);\n  var oEl=document.getElementById('mjOnline'); if(oEl)oEl.textContent=online!=null?String(online):'—';\n  var iEl=document.getElementById('mjId'); if(iEl)iEl.textContent=String(id);\n  var sEl=document.getElementById('mjStatic'); if(sEl)sEl.textContent=String(st);\n  var wanted=Number(d.wanted)||0;\n  var stars=document.querySelectorAll('#mjWanted .wanted-star');\n  for(var i=0;i<stars.length;i++)stars[i].classList.toggle('active', i>=(5-wanted));\n  var clip=d.clip, ammo=d.ammo;\n  var ammoBox=document.getElementById('mjAmmo');\n  if(ammoBox){\n    if(clip!=null||ammo!=null){\n      ammoBox.style.display='flex';\n      var c=document.getElementById('mjClip'); if(c)c.textContent=clip!=null?String(clip):'0';\n      var a=document.getElementById('mjAmmoVal'); if(a)a.textContent=ammo!=null?String(ammo):'0';\n    }else ammoBox.style.display='none';\n  }\n  applyEditMode();\n}\nfunction syncChat(){\n  if(!chatList)return;\n  var src=document.querySelector('.main-chat-list'); if(!src)return;\n  var nodes=src.querySelectorAll('.main-chat-post, .chatMessage, li');\n  if(!nodes.length)nodes=src.children;\n  var lines=[];\n  for(var i=0;i<nodes.length;i++){\n    var t=(nodes[i].innerText||nodes[i].textContent||'').replace(/\\s+/g,' ').trim();\n    if(t&&t.length<=240)lines.push(t)",
            ";\n  }\n  if(lines.length>16)lines=lines.slice(-16);\n  var html=''; for(var j=0;j<lines.length;j++)html+='<div class=\"mj-line\">'+esc(lines[j])+'</div>';\n  if(chatList._mjLast!==html){chatList._mjLast=html;chatList.innerHTML=html;}\n}\nfunction itemName(it){\n  try{if(window.inventoryAPI&&inventoryAPI.getByItemId&&it&&it.itemId){var meta=inventoryAPI.getByItemId(it.itemId);if(meta&&meta.name)return meta.name;}}catch(e){}\n  return it&&it.itemId?('ITEM '+it.itemId):'';\n}\nfunction itemImg(it){\n  try{if(window.inventoryAPI&&inventoryAPI.getItemImg){var u=inventoryAPI.getItemImg(it);if(u)return u;}}catch(e){}\n  try{var node=document.querySelector('.in-grid-inventory-item[index=\"'+it.index+'\"] img');if(node&&node.getAttribute('src'))return node.getAttribute('src');}catch(e){}\n  return '';\n}\nfunction itemCount(it){\n  if(!it||!it.params)return '';\n  var p=it.params;\n  if(p.fakeAmmo!=null)return String(p.fakeAmmo);\n  if(p.ammo!=null)return String(p.ammo);\n  if(p.count!=null&&p.count!==1)return String(p.count);\n  return '';\n}\nfunction slotHtml(it,index,label){\n  if(!it)return '<div class=\"mj-slot mj-empty\" data-index=\"'+esc(index)+'\"><span class=\"n\">'+esc(label)+'</span></div>';\n  var img=itemImg(it),c=itemCount(it),title=esc(itemName(it));\n  return '<div class=\"mj-slot\" data-id=\"'+esc(it.id)+'\" data-index=\"'+esc(index)+'\" title=\"'+title+'\">'\n    +(img?'<img src=\"'+esc(img)+'\" alt=\"\"/>':'')+'<span class=\"n\">'+esc(label)+'</span>'+(c?'<span class=\"c\">'+esc(c)+'</span>':'')+'</div>';\n}\nfunction isInvOpen(){\n  try{if(window.inventoryAPI&&inventoryAPI.status&&inventoryAPI.status())return true;}catch(e){}\n  return false;\n}\nfunction syncInv(){\n  if(!invRoot)return;\n  if(!isInvOpen()){invRoot.classList.remove('open');return;}\n  invRoot.classList.add('open');\n  if(drag.active)return;\n  var items=[]; try{if(window.inventoryAPI&&inventoryAPI.getItems)items=inventoryAPI.getItems()||[];}catch(e){}\n  if(!Array.isArray(items))items=[];\n  var byIndex={}; for(var i=0;i<items.length;i++){var it=ite",
            "ms[i];if(it&&it.index!=null)byIndex[it.index]=it;}\n  var cHtml='',mHtml='',qHtml='',n;\n  for(n=0;n<IDX.clothes.length;n++)cHtml+=slotHtml(byIndex[IDX.clothes[n]],IDX.clothes[n],String(n+1));\n  for(n=0;n<IDX.main.length;n++)mHtml+=slotHtml(byIndex[IDX.main[n]],IDX.main[n],String(n+1));\n  for(n=0;n<IDX.quick.length;n++)qHtml+=slotHtml(byIndex[IDX.quick[n]],IDX.quick[n],'F'+(n+1));\n  if(invClothes._mjLast!==cHtml){invClothes._mjLast=cHtml;invClothes.innerHTML=cHtml;}\n  if(invMain._mjLast!==mHtml){invMain._mjLast=mHtml;invMain.innerHTML=mHtml;}\n  if(invQuick._mjLast!==qHtml){invQuick._mjLast=qHtml;invQuick.innerHTML=qHtml;}\n}\nfunction clearGhost(){\n  var g=document.getElementById('qt-mj-ghost'); if(g){g.className='';g.innerHTML='';g.style.display='none';}\n  drag.active=false;drag.from=null;drag.moved=false;\n  document.querySelectorAll('.mj-slot.mj-dragging').forEach(function(n){n.classList.remove('mj-dragging');});\n  var dz=document.getElementById('mjDropZone'); if(dz)dz.classList.remove('hot');\n}\nfunction startItemDrag(slot,ev){\n  if(!slot||slot.classList.contains('mj-empty'))return;\n  var idx=slot.getAttribute('data-index'); if(idx==null)return;\n  drag.active=true;drag.from=Number(idx);drag.moved=false;drag.sx=ev.clientX;drag.sy=ev.clientY;\n  slot.classList.add('mj-dragging');\n  var g=document.getElementById('qt-mj-ghost');\n  if(g){g.style.display='block';g.className='show';g.innerHTML=slot.innerHTML;g.style.left=(ev.clientX+12)+'px';g.style.top=(ev.clientY+12)+'px';}\n}\nfunction dropOnSlot(targetIndex){\n  var from=drag.from; if(from==null||targetIndex==null||Number(from)===Number(targetIndex))return;\n  var inst=findInv();\n  if(inst){\n    try{\n      var src=inst.getItemByIdx(Number(from)); if(!src)return;\n      var dst=inst.getItemByIdx(Number(targetIndex));\n      if(dst){if(typeof inst.tryToMerge==='function')inst.tryToMerge(src,dst);else inst.updateIndex(src,Number(targetIndex));}\n      else inst.updateIndex(src,Number(targetIndex));\n    }catch(e){}\n  }\n}\nfunction th",
            "rowFromSlot(){\n  var from=drag.from; if(from==null)return;\n  try{\n    var item=null,inst=findInv();\n    if(inst&&inst.getItemByIdx)item=inst.getItemByIdx(Number(from));\n    if(!item&&window.inventoryAPI&&inventoryAPI.getItems){\n      var items=inventoryAPI.getItems()||[];\n      for(var i=0;i<items.length;i++)if(items[i]&&Number(items[i].index)===Number(from)){item=items[i];break;}\n    }\n    if(!item||item.id==null)return;\n    mp.trigger('cefProc','item.throw',item.id);\n  }catch(e){}\n}\nfunction bindInvDnD(){\n  if(!invRoot||invRoot._mjDnd)return; invRoot._mjDnd=true;\n  invRoot.addEventListener('mousedown',function(ev){\n    if(ev.button!==0)return;\n    if(ev.target.closest('.mj-close'))return;\n    var slot=ev.target.closest('.mj-slot'); if(!slot||slot.classList.contains('mj-empty'))return;\n    if(ev.shiftKey){\n      var idx=Number(slot.getAttribute('data-index')); var inst=findInv();\n      if(inst&&inst.getItemByIdx&&inst.findFreeSlot&&inst.updateIndex){\n        try{var it=inst.getItemByIdx(idx); if(!it)return;\n          var typ=inst.getItemInventoryType?inst.getItemInventoryType(idx):null;\n          var free=inst.findFreeSlot(typ==='quick'?'main':'quick'); if(free!=null)inst.updateIndex(it,free);\n        }catch(e){}\n      }\n      ev.preventDefault(); return;\n    }\n    startItemDrag(slot,ev); ev.preventDefault();\n  });\n  window.addEventListener('mousemove',function(ev){\n    if(!drag.active)return;\n    var g=document.getElementById('qt-mj-ghost'); if(g){g.style.left=(ev.clientX+12)+'px';g.style.top=(ev.clientY+12)+'px';}\n    var dz=document.getElementById('mjDropZone');\n    if(dz){var r=dz.getBoundingClientRect(); dz.classList.toggle('hot',ev.clientX>=r.left&&ev.clientX<=r.right&&ev.clientY>=r.top&&ev.clientY<=r.bottom);}\n  },true);\n  window.addEventListener('mouseup',function(ev){\n    if(!drag.active)return;\n    var under=document.elementFromPoint(ev.clientX,ev.clientY);\n    var dz=under&&under.closest?under.closest('#mjDropZone'):null;\n    var slot=under&&under.closes",
            "t?under.closest('.mj-slot'):null;\n    if(dz)throwFromSlot();\n    else if(slot&&slot.getAttribute('data-index')!=null)dropOnSlot(Number(slot.getAttribute('data-index')));\n    clearGhost(); setTimeout(function(){try{syncInv();}catch(e){}},80);\n  },true);\n}\nfunction tick(){\n  if(!window._qtMjShellV6||!window._qtMjShellV6.enabled)return;\n  try{ensure();syncHud();syncChat();syncInv();}catch(e){}\n}\nwindow._qtMjShellV6={\n  enabled:true,\n  refresh:function(){\n    this.enabled=true;\n    document.documentElement.classList.add('qt-mj-on');\n    ensure(); applyEditMode(); tick();\n  },\n  setEdit:function(on){window._qtMjEditHud=!!on;applyEditMode();},\n  resetLayout:function(id){ensure();resetHudPos(id||null);},\n  disable:function(){\n    this.enabled=false;\n    document.documentElement.classList.remove('qt-mj-on');\n    document.documentElement.classList.remove('qt-mj-edit');\n    clearGhost();\n    var r=document.getElementById('qt-mj-root'); if(r)r.remove();\n    root=null;\n    try{delete window._qtMjShellV6;}catch(e){window._qtMjShellV6=null;}\n  }\n};\nif(typeof window._qtMjEditHud==='undefined')window._qtMjEditHud=false;\nif(!window._qtMjShellTimer)window._qtMjShellTimer=setInterval(function(){try{tick();}catch(e){}},200);\nwindow._qtMjShellV6.refresh();\n})();"
        ].join('');
    }
    function qtUiThemeInjectCode(enabled) {
        // Cleanup must NEVER append boot — that recreated logo/stars after off.
        var cleanup = "(function(){try{"
            +"document.documentElement.classList.remove('qt-mj-on');"
            +"document.documentElement.classList.remove('qt-mj-edit');"
            +"window._qtMjEditHud=false;"
            +"try{if(window._qtMjShellTimer){clearInterval(window._qtMjShellTimer);window._qtMjShellTimer=null;}}catch(e){}"
            +"try{if(window._qtMjShellV6){window._qtMjShellV6.enabled=false;if(window._qtMjShellV6.disable)window._qtMjShellV6.disable();}}catch(e){}"
            +"try{if(window._qtMjShellV5){window._qtMjShellV5.enabled=false;if(window._qtMjShellV5.disable)window._qtMjShellV5.disable();}}catch(e){}"
            +"try{if(window._qtMjShellV4){window._qtMjShellV4.enabled=false;if(window._qtMjShellV4.disable)window._qtMjShellV4.disable();}}catch(e){}"
            +"var ids=['qt-mj-theme','qt-mj-root','qt-mj-ghost','qt-mj-style','qt-ui-theme'];"
            +"for(var i=0;i<ids.length;i++){var n=document.getElementById(ids[i]);if(n)n.remove();}"
            +"document.querySelectorAll('[id^=\"qt-mj\"]').forEach(function(n){try{n.remove();}catch(e){}});"
            +"try{delete window._qtMjShellV6;delete window._qtMjShellV5;delete window._qtMjShellV4;}catch(e){}"
            +"}catch(e){}})();";
        if (!enabled) return cleanup;
)QTJSX91"
        R"QTJSX91(        var css = JSON.stringify(qtUiThemeShellCss() + qtUiThemeShellCss2());
        var boot = qtUiThemeShellBootCode();
        var edit = qtUiTheme.editHud ? "true" : "false";
        return "(function(){try{"
            +"window._qtMjEditHud="+edit+";"
            +"document.documentElement.classList.add('qt-mj-on');"
            +"document.documentElement.classList.toggle('qt-mj-edit',!!window._qtMjEditHud);"
            +"var style=document.getElementById('qt-mj-theme');"
            +"if(!style){style=document.createElement('style');style.id='qt-mj-theme';(document.head||document.documentElement).appendChild(style);}"
            +"style.textContent="+css+";"
            +"if(window._qtMjShellV6){window._qtMjShellV6.enabled=true;if(window._qtMjShellV6.setEdit)window._qtMjShellV6.setEdit(!!window._qtMjEditHud);if(window._qtMjShellV6.refresh)window._qtMjShellV6.refresh();}"
            +"}catch(e){}})();" + boot;
    }
    function qtUiThemeApply() {
        // Redesign temporarily disabled — cleanup only (never boot shell)
        try { if (qtUiThemeTimer) { qtClearTimeout(qtUiThemeTimer); qtUiThemeTimer = null; } } catch (e) {}
        qtUiTheme.enabled = false;
        qtUiTheme.editHud = false;
        safeBrowserExecute(qtUiThemeInjectCode(false));
    }

    // ---- No-Drop on Death ----
    let noDropEnabled = false;
    let isKnockedDown = false;

    function noDropInit() {
        safeBrowserExecute(`(function(){
            if(window._noDropPatched) return;
            window._noDropPatched = true;
            window._blockItemThrow = false;
            window._noDropBypassLock = false;
            window._noDropInvInstance = null;
            var _origTrigger = mp.trigger.bind(mp);
            mp.trigger = function() {
                if(arguments[0]==='cefProc' && arguments[1]==='item.throw' && window._blockItemThrow) {
                    return;
                }
                return _origTrigger.apply(mp, arguments);
            };
            window._noDropFindInv = function(){
                try {
                    var all = document.querySelectorAll('*');
                    for(var i=0;i<all.length;i++){
                        var el = all[i];
                        var fkey = null;
                        for(var k in el){ if(k.indexOf('__reactFiber')===0){fkey=k;break;} }
                        if(!fkey) continue;
                        var fiber = el[fkey];
                        while(fiber){
                            var inst = fiber.stateNode;
                            if(inst && typeof inst.canMoveItem==='function' && Array.isArray(inst.lastWeapons)){
                                return inst;
                            }
                            fiber = fiber.return;
                        }
                    }
                } catch(e){}
                return null;
            };
            window._noDropPatchInv = function(){
                if(window._noDropInvInstance && window._noDropInvInstance._noDropPatched) return true;
                var inst = window._noDropFindInv();
                if(!inst) return false;
                if(inst._noDropPatched) return true;
                inst._noDropPatched = true;
                inst._origCanMoveItem = inst.canMoveItem.bind(inst);
                inst.canMoveItem = function(id){
                    if(window._noDropBypassLock) return true;
                    return inst._origCanMoveItem(id);
                };
                inst._origAddLastWeapon = inst.addLastWeapon.bind(inst);
                inst.addLastWeapon = function(id){
                    if(window._noDropBypassLock){ return; }
                    return inst._origAddLastWeapon(id);
                };
                window._noDropInvInstance = inst;
                return true;
            };
            if(!window._noDropPatchInterval){
                window._noDropPatchInterval = setInterval(function(){
                    try { window._noDropPatchInv(); } catch(e){}
                }, 2000);
            }
            window._noDropPatchInv();
        })()`);
    }

    function noDropSetBlock(block) {
        safeBrowserExecute(`window._blockItemThrow = ${block ? 'true' : 'false'}`);
    }

    function noDropClearWeaponLock() {
        safeBrowserExecute(`(function(){
            try { if(typeof window._noDropPatchInv==='function') window._noDropPatchInv(); } catch(e){}
            if(window._noDropInvInstance && Array.isArray(window._noDropInvInstance.lastWeapons)){
                window._noDropInvInstance.lastWeapons.length = 0;
            }
        })()`);
    }

    function noDropSetBypassLock(val) {
        safeBrowserExecute(`(function(){
            window._noDropBypassLock = ${val ? 'true' : 'false'};
            try { if(typeof window._noDropPatchInv==='function') window._noDropPatchInv(); } catch(e){}
            if(${val ? 'true' : 'false'} && window._noDropInvInstance && Array.isArray(window._noDropInvInstance.lastWeapons)){
                window._noDropInvInstance.lastWeapons.length = 0;
            }
        })()`);
    }

    mp.events.add('setRadiusVariable', (entity, varName, value) => {
        try {
            if (!noDropEnabled) return;
            if (varName !== 'knockDown') return;
            if (entity.type !== 'player' || entity.remoteId !== mp.players.local.remoteId) return;
            isKnockedDown = !!value;
            noDropSetBlock(isKnockedDown);
            if (isKnockedDown) {
                noDropClearWeaponLock();
                log('NoDrop: blocked (knockDown)');
                qtUiLog('🛡️ NoDrop: weapon throw blocked');
            } else {
                log('NoDrop: unblocked');
            }
        } catch(e) {}
    });

    // ---- Crawl (FakeDeath) ----
    let currentAnim = undefined;
    let animLock = undefined;
    let lastRotSync = Date.now();
    let lastCrawlDir = "onfront_fwd";

    function crawlStart() {
        if (crawlActive) return;
        crawlActive = true;
        lastRotSync = Date.now();
        log('Crawl ON');
        callRemoteMulti("knockDownCrawlAnim", "onfront_fwd");

        renderEvent = new mp.Event("render", () => {
            if (!crawlActive) return;
            const ctrl = mp.game.controls;
            ctrl.disableControlAction(0, 21, true);
            ctrl.disableControlAction(0, 24, true);
            ctrl.disableControlAction(0, 25, true);
            ctrl.disableControlAction(0, 22, true);
            ctrl.disableControlAction(0, 44, true);

            if (ctrl.isDisabledControlPressed(0, 34) || ctrl.isDisabledControlPressed(0, 35)) {
                let rot = mp.players.local.getRotation(2);
                ctrl.isDisabledControlPressed(0, 34)
                    ? mp.players.local.setRotation(rot.x, rot.y, rot.z + 0.2, 2, true)
                    : mp.players.local.setRotation(rot.x, rot.y, rot.z - 0.2, 2, true);
                let now = Date.now();
                if (now - lastRotSync > 1000) {
                    callRemoteMulti("knockDownSetRotation", JSON.stringify(mp.players.local.getRotation(2)));
                    lastRotSync = now;
                }
            }

            if (ctrl.isDisabledControlPressed(0, 32)) {
                if (currentAnim === "onfront_fwd" || animLock) return;
                currentAnim = "onfront_fwd";
                lastCrawlDir = "onfront_fwd";
                let s = mp.game.entity.getEntityAnimDuration("move_crawl", currentAnim);
                callRemoteMulti("knockDownCrawlAnim", currentAnim);
                animLock = qtSetTimeout(() => { currentAnim = undefined; animLock = undefined; }, (s - 0.1) * 1000);
            }

            if (ctrl.isDisabledControlPressed(0, 33)) {
                if (currentAnim === "onfront_fwd" || animLock) return;
                currentAnim = "onfront_bwd";
                lastCrawlDir = "onfront_bwd";
                let s = mp.game.entity.getEntityAnimDuration("move_crawl", currentAnim);
                callRemoteMulti("knockDownCrawlAnim", currentAnim);
                animLock = qtSetTimeout(() => { currentAnim = undefined; animLock = undefined; }, (s - 0.1) * 1000);
            }
        });
    }

    let crawlRecovery = 0;
    function crawlStop() {
        crawlActive = false;
        if (renderEvent) { try { renderEvent.destroy(); } catch(e) {} renderEvent = null; }
        if (animLock) { qtClearTimeout(animLock); animLock = undefined; }
        callRemoteMulti("animation.set", "move_crawl", lastCrawlDir);
        currentAnim = undefined;
        crawlRecovery = 30;
        try { mp.game.invoke('0xE1EF3C1216AFF2CD', mp.players.local.handle); } catch(e) {}
        log('Crawl OFF');
    }

    function crawlToggle() {
        if (crawlActive) crawlStop(); else crawlStart();
    }

    // ---- Stand up from knockdown ----
    function standUpFromKnockdown() {
        if (!sessionToken) {
            log('No token for stand up');
            return;
        }
        try {
            browserMenu.call("socket", "callRemote", [
                "knockDown",
                sessionToken,
                false
            ]);
            browserMenu.call("socket", "callRemote", [
                "setGodmode",
                sessionToken,
                false
            ]);
            log('Stand up from knockdown');
        } catch (e) {
            log('Error standing up: ' + e);
        }
    }

    // ---- CheatSheet Data (transcribed from image, best-effort) ----
    const CHEATSHEET_DATA = {
        groups: {
            murder:   { name: 'Вбивство',     color: '#d63447' },
            harm:     { name: 'Шкода',        color: '#e85d75' },
            theft:    { name: 'Викрадення',   color: '#8e9b4e' },
            rob:      { name: 'Пограбування', color: '#b8864c' },
            weapon:   { name: 'Зброя',        color: '#c0392b' },
            drugs:    { name: 'Наркотики',    color: '#a83f5a' },
            minor:    { name: 'Адм.',          color: '#7d8591' },
            evade:    { name: 'Ухилення',     color: '#c9a13b' },
            hool:     { name: 'Хуліганство',  color: '#9c6f3e' }
        },
        penalties: [
            ['4.1',   'Ф/Р', 'Вбивство, тобто умисне заподіяння смерті іншої особи', 5, 40, 'murder'],
            ['4.2',   'Р',   'Тяжке вбивство, тобто вбивство двох або більше осіб', 6, 50, 'murder'],
            ['4.3',   'Р',   'Групове вбивство особою з особливою жорстокістю', 5, 50, 'murder'],
            ['4.4',   'Ф',   'Заподіяння шкоди здоров\u2019ю з нанесенням фізичних ушкоджень', 3, 30, 'harm'],
            ['4.5',   'Р',   'Загроза вбивством або нанесення тяжких тілесних ушкоджень', 3, 40, 'harm'],
            ['4.6',   'Ф',   'Послання на ч. Дарк (близьким, з використанням зброї)', 3, 40, 'harm'],
            ['10.7',  'Р',   'Загроза вбивством посадовій особі', 5, 50, 'harm'],
            ['5.1',   'Ф',   'Викрадення майна', 3, 30, 'theft'],
)QTJSX91"
        R"QTJSX91(            ['6.1',   'Ф',   'Крадіжка, тобто таємне викрадення чужого майна (Будинок)', 5, 40, 'theft'],
            ['5.3',   'Р',   'Грабіж, тобто відкрите викрадення чужого майна (Банкомат)', 3, 30, 'theft'],
            ['5.4',   'Р',   'Розбій, напад з метою викрадення чужого майна (Магазини)', 3, 30, 'theft'],
            ['5.5',   'Р',   'Викрадення майна під загрозою насильства (Гросс)', 3, 30, 'rob'],
            ['6.1/5', 'Р',   'Викрадення транспортного засобу (Угон)', 3, 30, 'rob'],
            ['9.6',   'Р',   'Незаконне придбання, зберігання, збут зброї та боєприпасів', 5, 40, 'weapon'],
            ['9.7',   'Ф',   'Незаконне зберігання наркотичних речовин (до 25 г)', 3, 40, 'drugs'],
            ['9.8',   'Р',   'Незаконне зберігання наркотичних речовин (від 25 г)', 4, 50, 'drugs'],
            ['9.9',   'Р',   'Незаконне виробництво та перевезення наркотичних речовин', 5, 50, 'drugs'],
            ['13.6',  'Р',   'Придбання, зберігання або збут форми держструктури, ідент. знаків', 6, 50, 'drugs'],
            ['5.4m',  'Ф',   'Нецензурна лексика у публічному виступі', 2, 20, 'minor'],
            ['5.10',  'Ф',   'Провокація, дії у бік правоохоронного представника', 4, 50, 'minor'],
            ['5.15',  'Ф',   'Дрібне хуліганство у громадських місцях, нецензурні вирази', 3, 30, 'minor'],
            ['10.11', 'Р',   'Перешкоджання правоохоронним співробітникам при затриманні', 3, 30, 'minor'],
            ['10.13', 'Р',   'Перешкоджання роботі уповн. співробітника при затриманні', 4, 50, 'minor'],
            ['7.4',   'Р',   'Несплата штрафу або ухилення від платні штрафу', 2, 50, 'evade'],
            ['10.12', 'Р',   'Втеча з місця злочину або затримання, приховування злочину', 4, 50, 'evade'],
            ['10.14', 'Р',   'Повторне вчинене правопорушення або вчинене при виконанні обов\u2019язків', 4, 50, 'evade'],
            ['11.13', 'Р',   'Ухилення від реального кримінального строку', 6, 70, 'evade'],
            ['6.6',   'Р',   'Хуліганство, умисні дії, що грубо порушують громадський порядок', 3, 30, 'hool'],
            ['8.1',   'Ф',   'Вандалізм, знищення або пошкодження майна, підпал, вибух', 4, 40, 'hool'],
            ['8.3',   'Ф',   'Терористичний акт, напад з використанням вибухових пристроїв', 6, 60, 'hool'],
            ['8.6',   'Р',   'Незаконне проникнення на закритий об\u2019єкт (LSPD, FIB)', 4, 40, 'hool']
        ],
        teaCodes: [
            ['10-3',  'Зустрічі усіх учасників на чергуванні'],
            ['10-4',  'Coffee Time'],
            ['10-5',  'Повторити останнє повідомлення'],
            ['10-7',  'Не приймаю / Ні'],
            ['10-8',  'Чекаємо'],
            ['10-9',  'В даний час відсутній'],
            ['10-10', 'Повторна підтримка'],
            ['10-11', 'Чергування у КПП'],
            ['10-12', 'На КПП перекрито'],
            ['10-13', 'Відроджу на КПП FIB'],
            ['10-14', 'Запит транспорту'],
            ['10-15', 'Пограбування'],
            ['10-16', 'Операції, що потребують додаткових засобів'],
            ['10-17', 'Нелегальна активність'],
            ['10-57', 'Поспіх за автомобілем']
        ],
        codes: [
            ['CODE-0', 'Офіцер поранений у тяжкому стані'],
            ['CODE-1', 'Відступ з озброєним злочинцем'],
            ['CODE-2', 'BigSig (сигнал про напад)'],
            ['CODE-3', 'BigSig (сигнал завершено)'],
            ['CODE-4', 'Потрібна допомога']
        ],
        procedure: [
            ['1',  'Присутність офіцера'],
            ['2',  'Представитись'],
            ['3',  'Назначити радіопізнавання'],
            ['4',  'Сховатися зі зброєю'],
            ['5',  'Обшук та вилучення'],
            ['6',  'Посадка в авто'],
            ['7',  'Заходити в порядку'],
            ['8',  'Доповісти FIB'],
            ['9',  'Реалізація прав'],
            ['10', 'Прочитати про права'],
            ['11', 'Заповнити протокол арешту'],
            ['12', 'Відправити в СІЗО']
        ],
        immune: [
            ['Губернатор Quant, Віце-губернатор', 5, '5.000 − 20.000'],
            ['Аудитор USSS',                     2, '25.000 − 40.000'],
            ['Голова держ. служб та заступники', 3, '30.000 − 140.000'],
            ['Міністри та їх заступники',        4, '100.000 − 140.000'],
            ['Судді',                            5, '150.000 − 200.000']
        ]
    };

    // ---- CheatSheet CEF Variant 2: Dark Table ----
    function buildCheatsheetV2(sz, op, bgA) {
        const data = CHEATSHEET_DATA;
        const stars = n => '★'.repeat(n) + '☆'.repeat(Math.max(0, 5 - n));
        const penRows = data.penalties.map(p => {
            const g = data.groups[p[5]] || { name: '', color: '#666' };
            return '<tr data-s="' + (p[0] + ' ' + p[2] + ' ' + g.name).toLowerCase() + '" style="border-left:3px solid ' + g.color + '">'
                + '<td class="art">' + p[0] + '</td>'
                + '<td class="fr">' + p[1] + '</td>'
                + '<td class="desc">' + p[2] + '</td>'
                + '<td class="st"><span title="' + p[3] + ' зірок">' + stars(p[3]) + '</span></td>'
                + '<td class="hr">' + p[4] + '<small>г</small></td>'
                + '<td><span class="gchip" style="background:' + g.color + '22;color:' + g.color + ';border-color:' + g.color + '66">' + g.name + '</span></td>'
                + '</tr>';
        }).join('');
        const teaRows = data.teaCodes.map(c => '<div class="kv"><code>' + c[0] + '</code><span>' + c[1] + '</span></div>').join('');
        const codeRows = data.codes.map(c => '<div class="kv code"><code>' + c[0] + '</code><span>' + c[1] + '</span></div>').join('');
        const procRows = data.procedure.map(c => '<div class="kv proc"><code>' + c[0] + '</code><span>' + c[1] + '</span></div>').join('');
        const immRows = data.immune.map(i => '<tr><td>' + i[0] + '</td><td class="st">' + stars(i[1]) + '</td><td class="price">' + i[2] + '</td></tr>').join('');

        return '<!DOCTYPE html><html><head><meta charset="utf-8"><style>'
            + '*{margin:0;padding:0;box-sizing:border-box;font-family:\"Segoe UI\",system-ui,sans-serif}'
            + 'html,body{background:rgba(0,0,0,' + bgA + ');min-height:100vh;color:#d8dee9;font-size:11px;overflow:auto}'
            + '.wrap{transform:scale(' + (sz/100) + ');transform-origin:top center;opacity:' + op + ';padding:14px;max-width:1680px;margin:0 auto}'
            + '.hd{display:flex;align-items:center;gap:12px;margin-bottom:10px;padding:10px 14px;background:linear-gradient(90deg,#1a1d24,#0f1115);border:1px solid #2a2f3a;border-radius:10px}'
            + '.hd h1{font-size:14px;color:#e5c07b;letter-spacing:1px;text-transform:uppercase;font-weight:700}'
            + '.hd .spacer{flex:1}'
            + '.hd input{background:#0b0d12;border:1px solid #2a2f3a;color:#d8dee9;padding:6px 10px;border-radius:6px;font-size:11px;width:280px;outline:none;font-family:inherit}'
            + '.hd input:focus{border-color:#5e81ac}'
            + '.hd .meta{color:#5c6370;font-size:10px}'
            + '.grid{display:grid;grid-template-columns:1.6fr 1fr;gap:10px}'
            + '.panel{background:#0f1115;border:1px solid #2a2f3a;border-radius:10px;overflow:hidden}'
            + '.panel>.title{padding:7px 12px;background:#14171e;border-bottom:1px solid #2a2f3a;color:#e5c07b;font-size:10px;letter-spacing:1.5px;text-transform:uppercase;font-weight:700}'
            + 'table{width:100%;border-collapse:collapse;font-size:10.5px}'
            + 'thead th{padding:6px 8px;background:#14171e;color:#8b95a7;font-weight:600;text-align:left;font-size:9px;letter-spacing:1px;text-transform:uppercase;border-bottom:1px solid #2a2f3a;position:sticky;top:0}'
            + 'tbody td{padding:6px 8px;border-bottom:1px solid #1a1d24;vertical-align:top;line-height:1.35}'
            + 'tbody tr:hover{background:#15191f}'
            + '.art{color:#61afef;font-family:\"Consolas\",monospace;font-weight:600;white-space:nowrap;width:44px}'
            + '.fr{color:#c678dd;font-family:\"Consolas\",monospace;width:28px;text-align:center}'
            + '.desc{color:#d8dee9}'
            + '.st{color:#e5c07b;width:68px;font-size:10px;letter-spacing:1px;white-space:nowrap}'
            + '.hr{color:#98c379;font-family:\"Consolas\",monospace;width:42px;text-align:right;white-space:nowrap}'
            + '.hr small{color:#5c6370;margin-left:2px}'
            + '.gchip{display:inline-block;padding:2px 8px;border-radius:10px;font-size:9px;font-weight:600;letter-spacing:0.5px;border:1px solid;white-space:nowrap}'
            + '.kv{display:flex;align-items:baseline;gap:10px;padding:5px 12px;border-bottom:1px solid #1a1d24;font-size:10.5px}'
            + '.kv code{color:#61afef;font-family:\"Consolas\",monospace;font-weight:700;min-width:50px;font-size:10px}'
            + '.kv.code code{color:#e06c75}'
            + '.kv.proc code{color:#98c379}'
            + '.kv span{color:#c8ccd4;flex:1}'
            + '.right-col{display:flex;flex-direction:column;gap:10px}'
            + '.panel.imm table td{padding:7px 10px;font-size:10.5px}'
            + '.panel.imm .price{color:#98c379;font-family:\"Consolas\",monospace;text-align:right;white-space:nowrap}'
            + '.hidden{display:none}'
            + '::-webkit-scrollbar{width:6px;height:6px}'
            + '::-webkit-scrollbar-track{background:#0b0d12}'
            + '::-webkit-scrollbar-thumb{background:#2a2f3a;border-radius:3px}'
            + '::-webkit-scrollbar-thumb:hover{background:#3e4454}'
            + '</style></head><body><div class="wrap">'
            + '<div class="hd"><h1>Quant RP — Cheatsheet</h1>'
            + '<div class="meta">' + data.penalties.length + ' статей · ' + data.teaCodes.length + ' кодів</div>'
            + '<div class="spacer"></div>'
            + '<input id="q" placeholder="Пошук по статті, групі, тексту..." oninput="f()"></div>'
            + '<div class="grid">'
            + '<div class="panel"><div class="title">Кримінальний кодекс</div>'
            + '<table><thead><tr><th>Ст.</th><th>Ф/Р</th><th>Опис</th><th>Зірки</th><th>Год</th><th>Група</th></tr></thead>'
            + '<tbody id="pen">' + penRows + '</tbody></table></div>'
            + '<div class="right-col">'
            + '<div class="panel"><div class="title">Радіо-коди (Tea Codes)</div>' + teaRows + '</div>'
            + '<div class="panel"><div class="title">Сигнальні коди</div>' + codeRows + '</div>'
            + '<div class="panel"><div class="title">Порядок затримання</div>' + procRows + '</div>'
            + '<div class="panel imm"><div class="title">Недоторкані особи</div>'
            + '<table><thead><tr><th>Посада</th><th>Зірки</th><th>Відкуп</th></tr></thead>'
            + '<tbody>' + immRows + '</tbody></table></div>'
            + '</div></div></div>'
)QTJSX91"
        R"QTJSX91(            + '<script>function f(){var q=document.getElementById("q").value.toLowerCase().trim();var rows=document.querySelectorAll("#pen tr");rows.forEach(function(r){r.classList.toggle("hidden",q&&r.dataset.s.indexOf(q)<0)})}</script>'
            + '</body></html>';
    }

    // ---- CheatSheet CEF Variant 3: Neon Cards ----
    function buildCheatsheetV3(sz, op, bgA) {
        const data = CHEATSHEET_DATA;
        const stars = n => '★'.repeat(n);
        const byGroup = {};
        data.penalties.forEach(p => {
            if (!byGroup[p[5]]) byGroup[p[5]] = [];
            byGroup[p[5]].push(p);
        });
        const groupCards = Object.keys(byGroup).map(gk => {
            const g = data.groups[gk] || { name: gk, color: '#888' };
            const rows = byGroup[gk].map(p =>
                '<div class="row"><div class="art">' + p[0] + '<small>' + p[1] + '</small></div>'
                + '<div class="desc">' + p[2] + '</div>'
                + '<div class="meta"><span class="st">' + stars(p[3]) + '</span><span class="hr">' + p[4] + 'г</span></div></div>'
            ).join('');
            return '<div class="card" style="--accent:' + g.color + '"><div class="chd"><span class="dot"></span>' + g.name
                + '<span class="cnt">' + byGroup[gk].length + '</span></div>' + rows + '</div>';
        }).join('');
        const teaCard = '<div class="card small" style="--accent:#5fd7ff"><div class="chd"><span class="dot"></span>Радіо-коди<span class="cnt">' + data.teaCodes.length + '</span></div>'
            + data.teaCodes.map(c => '<div class="krow"><code>' + c[0] + '</code><span>' + c[1] + '</span></div>').join('') + '</div>';
        const codesCard = '<div class="card small" style="--accent:#ff4f7b"><div class="chd"><span class="dot"></span>Сигнальні коди<span class="cnt">' + data.codes.length + '</span></div>'
            + data.codes.map(c => '<div class="krow"><code>' + c[0] + '</code><span>' + c[1] + '</span></div>').join('') + '</div>';
        const procCard = '<div class="card small" style="--accent:#b5ff6a"><div class="chd"><span class="dot"></span>Затримання<span class="cnt">' + data.procedure.length + '</span></div>'
            + data.procedure.map(c => '<div class="krow"><code>' + c[0] + '</code><span>' + c[1] + '</span></div>').join('') + '</div>';
        const immCard = '<div class="card small" style="--accent:#ffb84d"><div class="chd"><span class="dot"></span>Недоторкані<span class="cnt">' + data.immune.length + '</span></div>'
            + data.immune.map(i => '<div class="irow"><div class="ititle">' + i[0] + '</div><div class="iprice">' + i[2] + ' <span>' + stars(i[1]) + '</span></div></div>').join('') + '</div>';

        return '<!DOCTYPE html><html><head><meta charset="utf-8"><style>'
            + '*{margin:0;padding:0;box-sizing:border-box;font-family:\"Segoe UI\",system-ui,sans-serif}'
            + 'html,body{background:rgba(0,0,0,' + bgA + ');min-height:100vh;color:#e8eaed;overflow:auto}'
            + 'body::before{content:\"\";position:fixed;inset:0;background:radial-gradient(ellipse at top,rgba(95,215,255,0.08),transparent 60%),radial-gradient(ellipse at bottom,rgba(255,79,123,0.05),transparent 60%);pointer-events:none}'
            + '.wrap{transform:scale(' + (sz/100) + ');transform-origin:top center;opacity:' + op + ';padding:16px;max-width:1680px;margin:0 auto;position:relative}'
            + '.topbar{display:flex;align-items:center;gap:14px;margin-bottom:14px;padding:12px 18px;background:rgba(20,22,28,0.75);border:1px solid rgba(95,215,255,0.25);border-radius:12px;backdrop-filter:blur(10px);box-shadow:0 0 20px rgba(95,215,255,0.08)}'
            + '.topbar h1{font-size:15px;color:#5fd7ff;letter-spacing:3px;text-transform:uppercase;font-weight:800;text-shadow:0 0 10px rgba(95,215,255,0.4)}'
            + '.topbar .badge{background:rgba(95,215,255,0.15);color:#5fd7ff;padding:3px 10px;border-radius:10px;font-size:9px;font-weight:700;letter-spacing:1px;border:1px solid rgba(95,215,255,0.4)}'
            + '.topbar .spacer{flex:1}'
            + '.topbar input{background:rgba(0,0,0,0.4);border:1px solid rgba(95,215,255,0.3);color:#e8eaed;padding:7px 14px;border-radius:20px;font-size:12px;width:260px;outline:none;font-family:inherit}'
            + '.topbar input:focus{border-color:#5fd7ff;box-shadow:0 0 12px rgba(95,215,255,0.3)}'
            + '.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(310px,1fr));gap:12px}'
            + '.card{background:linear-gradient(145deg,rgba(25,27,34,0.85),rgba(15,17,22,0.85));border:1px solid rgba(255,255,255,0.06);border-left:3px solid var(--accent);border-radius:10px;padding:0;overflow:hidden;backdrop-filter:blur(8px);transition:all .2s}'
            + '.card:hover{border-color:var(--accent);box-shadow:0 0 24px rgba(0,0,0,0.6),0 0 16px var(--accent)33;transform:translateY(-2px)}'
            + '.chd{display:flex;align-items:center;gap:9px;padding:11px 14px;background:rgba(0,0,0,0.3);border-bottom:1px solid rgba(255,255,255,0.05);font-size:11px;font-weight:700;letter-spacing:2px;text-transform:uppercase;color:var(--accent)}'
            + '.chd .dot{width:7px;height:7px;border-radius:50%;background:var(--accent);box-shadow:0 0 8px var(--accent)}'
            + '.chd .cnt{margin-left:auto;font-size:9px;color:#6a7280;letter-spacing:1px;background:rgba(0,0,0,0.4);padding:2px 8px;border-radius:10px}'
            + '.row{padding:9px 14px;border-bottom:1px solid rgba(255,255,255,0.04);display:grid;grid-template-columns:60px 1fr auto;gap:10px;align-items:center;font-size:11px}'
            + '.row:last-child{border:none}'
            + '.row:hover{background:rgba(255,255,255,0.03)}'
            + '.row .art{color:var(--accent);font-family:\"Consolas\",monospace;font-weight:700;font-size:11.5px;display:flex;flex-direction:column}'
            + '.row .art small{color:#5a6375;font-size:9px;font-weight:500;margin-top:1px}'
            + '.row .desc{color:#c8cdd6;line-height:1.35;font-size:10.5px}'
            + '.row .meta{display:flex;flex-direction:column;align-items:flex-end;gap:2px;white-space:nowrap}'
            + '.row .st{color:#ffd166;font-size:10px;letter-spacing:1px}'
            + '.row .hr{color:#8ed081;font-family:\"Consolas\",monospace;font-size:10px;font-weight:600}'
            + '.small .krow{display:flex;gap:10px;padding:7px 14px;border-bottom:1px solid rgba(255,255,255,0.04);align-items:baseline;font-size:10.5px}'
            + '.small .krow:last-child{border:none}'
            + '.small .krow code{color:var(--accent);font-family:\"Consolas\",monospace;font-weight:700;font-size:10px;min-width:60px}'
            + '.small .krow span{color:#c8cdd6;flex:1}'
            + '.irow{padding:9px 14px;border-bottom:1px solid rgba(255,255,255,0.04)}'
            + '.irow:last-child{border:none}'
            + '.irow .ititle{color:#d8dde6;font-size:10.5px;margin-bottom:3px}'
            + '.irow .iprice{color:var(--accent);font-family:\"Consolas\",monospace;font-size:10px;font-weight:600;display:flex;justify-content:space-between;align-items:center}'
            + '.irow .iprice span{color:#ffd166;font-family:inherit;letter-spacing:1px}'
            + '.hidden{display:none}'
            + '::-webkit-scrollbar{width:8px;height:8px}'
            + '::-webkit-scrollbar-track{background:rgba(0,0,0,0.3)}'
            + '::-webkit-scrollbar-thumb{background:rgba(95,215,255,0.25);border-radius:4px}'
            + '::-webkit-scrollbar-thumb:hover{background:rgba(95,215,255,0.5)}'
            + '</style></head><body><div class="wrap">'
            + '<div class="topbar"><h1>Quant RP</h1><div class="badge">Cheatsheet</div>'
            + '<div class="spacer"></div>'
            + '<input id="q" placeholder="Пошук..." oninput="f()"></div>'
            + '<div class="grid">' + groupCards + teaCard + codesCard + procCard + immCard + '</div></div>'
            + '<script>function f(){var q=document.getElementById("q").value.toLowerCase().trim();document.querySelectorAll(".card").forEach(function(c){var txt=c.textContent.toLowerCase();c.classList.toggle("hidden",q&&txt.indexOf(q)<0)})}</script>'
            + '</body></html>';
    }

    // ---- CheatSheet Overlay ----
    function toggleCheatSheet() {
        if (cheatSheetOpen && cheatSheetBrowser) {
            cheatSheetBrowser.destroy();
            cheatSheetBrowser = null;
            cheatSheetOpen = false;
            log('CheatSheet OFF');
        } else {
            const op = csOpacity / 100;
            const bgA = csDim > 0 ? (csDim / 100) : 0;
            const sz = csScale;
            let html;
            if (csVariant === 2) {
                html = buildCheatsheetV2(sz, op, bgA);
            } else if (csVariant === 3) {
                html = buildCheatsheetV3(sz, op, bgA);
            } else {
                html = '<!DOCTYPE html><html><head><style>*{margin:0;padding:0;overflow:auto;background:rgba(0,0,0,' + bgA + ')}img{display:block;width:' + sz + 'vw;max-height:' + sz + 'vh;margin:' + ((100 - sz) / 2) + 'vh auto;object-fit:contain;opacity:' + op + '}</style></head><body><img src="' + '' + '/cheatsheet.png"></body></html>';
            }
            cheatSheetBrowser = mp.browsers.new('data:text/html,' + encodeURIComponent(html));
            cheatSheetOpen = true;
            log('CheatSheet ON v' + csVariant + ' (scale:' + sz + '% opacity:' + csOpacity + '% dim:' + csDim + '%)');
        }
    }

    // ============================================================
    // AQUANAUT BOT MODULE
    // ============================================================

    // Air HUD: native drawText "Повітря: XX хв." (trailing dot) and/or
    // CEF timerAPI via client::setTimebar. dump_air captures both live.
    function clearAirReloadTimer() {
        if (airReloadTimer) {
            try { qtClearTimeout(airReloadTimer); } catch (e) {}
            airReloadTimer = null;
        }
    }

    function resetAirReloadState() {
        airReloadPending = false;
        airReloadAttempt = 0;
        airReloadSent = false;
        clearAirReloadTimer();
    }

    function airReloadSucceeded(mins) {
        return mins != null && mins > AIR_AUTO_RELOAD_MIN;
    }

    function requestAirReload(reason) {
        if (!botActive || botVersion < 2 || !sessionToken) return;
        gotAirLoaded = false;
        airReloadPending = true;
        airReloadAttempt++;
        airReloadSent = true;
        log('Air ' + reason + ' — reload #' + airReloadAttempt + '...');
        qtSilentNotice('Air reload #' + airReloadAttempt);
        callRemote("aquanautLoadVehicle", null);
        clearAirReloadTimer();
        airReloadTimer = qtSetTimeout(function () {
            airReloadTimer = null;
            if (!botActive) {
                resetAirReloadState();
                return;
            }
            try { refreshAirMinutes(); } catch (e) {}
            const mins = airMinutes == null ? 0 : airMinutes;
)QTJSX91"
        R"QTJSX91(            if (airReloadSucceeded(mins)) {
                log('Air reload OK: ' + mins + ' min');
                qtSilentNotice('Air OK: ' + mins + ' min');
                resetAirReloadState();
                return;
            }
            log('Air still ' + mins + ' min after 60s — retry...');
            qtSilentNotice('Air not refilled, retry...');
            requestAirReload(mins + ' min left');
        }, AIR_RELOAD_WAIT_MS);
    }

    function checkAirAutoReload(mins) {
        if (botVersion < 2 || !botActive) return;
        if (mins == null || isNaN(mins)) return;
        // Refilled while waiting — cancel retry timer early.
        if (airReloadPending && airReloadSucceeded(mins)) {
            log('Air refilled: ' + mins + ' min');
            qtSilentNotice('Air OK: ' + mins + ' min');
            resetAirReloadState();
            return;
        }
        if (mins <= AIR_AUTO_RELOAD_MIN && !airReloadPending)
            requestAirReload(mins + ' min left');
    }

    function stripGtaTextCodes(text) {
        return String(text == null ? '' : text).replace(/~[^~]*~/g, '');
    }

    function matchAirMinutesFromText(text) {
        const t = stripGtaTextCodes(text);
        if (!t || /поц[іиi]лунок/i.test(t)) return null;
        // Live HUD uses Latin "i" inside "Повiтря" (not Ukrainian і).
        // Exact: "~y~Повiтря: 30 хв." at drawText [0.5, 0.92]
        let m = t.match(/Пов[іиi]тря\s*:\s*(\d+)\s*хв\.?/i);
        if (m) return parseInt(m[1], 10);
        m = t.match(/Пов[іиi]тря\s*:\s*(\d+)/i);
        if (m) return parseInt(m[1], 10);
        return null;
    }

    function matchAirMinutesFromDrawText(text, args) {
        let mins = matchAirMinutesFromText(text);
        if (mins != null) return mins;
        try {
            const p = args && args[1];
            if (!Array.isArray(p) || typeof p[0] !== 'number' || typeof p[1] !== 'number')
                return null;
            // Bottom-center native HUD only (dump confirmed 0.5 / 0.92).
            if (Math.abs(p[0] - 0.5) > 0.05 || Math.abs(p[1] - 0.92) > 0.05)
                return null;
            const t = stripGtaTextCodes(text);
            const m = t.match(/:\s*(\d+)\s*хв\.?/i) || t.match(/:\s*(\d+)/);
            if (m) return parseInt(m[1], 10);
        } catch (e) {}
        return null;
    }

    function noteAirSeconds(seconds, source) {
        const secs = parseInt(seconds, 10);
        if (isNaN(secs) || secs < 0) return;
        airDeadlineMs = Date.now() + secs * 1000;
        airMinutes = Math.ceil(secs / 60);
        if (source) log('Air HUD (' + source + '): ' + airMinutes + ' min (' + secs + 's)');
        checkAirAutoReload(airMinutes);
    }

    function noteAirMinutes(mins, source) {
        const m = parseInt(mins, 10);
        if (isNaN(m) || m < 0) return;
        airMinutes = m;
        airDeadlineMs = Date.now() + m * 60 * 1000;
        if (source) log('Air HUD (' + source + '): ' + m + ' min');
        checkAirAutoReload(m);
    }

    function refreshAirMinutes() {
        if (airDeadlineMs > 0) {
            const leftMs = airDeadlineMs - Date.now();
            if (leftMs <= 0) {
                airDeadlineMs = 0;
                airMinutes = 0;
            } else {
                airMinutes = Math.max(0, Math.ceil(leftMs / 60000));
            }
            checkAirAutoReload(airMinutes == null ? 0 : airMinutes);
        } else if (mp.timebar && mp.timebar.title &&
            /пов[іиi]тря/i.test(String(mp.timebar.title)) &&
            !/поц[іиi]лунок/i.test(String(mp.timebar.title))) {
            const secs = parseInt(mp.timebar.seconds, 10);
            if (!isNaN(secs) && secs >= 0 && airMinutes == null)
                noteAirSeconds(secs, 'mp.timebar');
        }
        return airMinutes;
    }

    function isAirHudTitle(text) {
        if (!text) return false;
        const t = stripGtaTextCodes(text);
        if (/поц[іиi]лунок/i.test(t)) return false;
        return /пов[іиi]тря\s*:/i.test(t) || /^пов[іиi]тря$/i.test(t.trim());
    }

    function airDumpActive() {
        return airDumpUntil > 0 && Date.now() < airDumpUntil;
    }

    function airDumpPush(line) {
        if (!airDumpActive()) return;
        const s = String(line);
        if (airDumpSeen[s]) return;
        airDumpSeen[s] = 1;
        if (airDumpLines.length >= 400) return;
        airDumpLines.push(s);
    }

    function airDumpSnapshotStatic(tag) {
        try {
            let tb = 'null';
            if (mp.timebar) {
                tb = JSON.stringify({
                    title: mp.timebar.title,
                    seconds: mp.timebar.seconds,
                    type: mp.timebar.type,
                    image: mp.timebar.image
                });
            }
            airDumpPush('[' + tag + '] mp.timebar=' + tb);
        } catch (e) {
            airDumpPush('[' + tag + '] mp.timebar err=' + e);
        }
        try {
            airDumpPush('[' + tag + '] airMinutes=' + airMinutes +
                ' deadlineMs=' + airDeadlineMs +
                ' leftMs=' + (airDeadlineMs > 0 ? (airDeadlineMs - Date.now()) : -1));
        } catch (e) {}
    }

    function airDumpScrapeCef() {
        try {
            safeBrowserExecute(`(function(){
                try {
                    var out = [];
                    var timers = document.querySelectorAll('#timer, #newTimer, [class*="timer"]');
                    for (var i = 0; i < timers.length && i < 12; i++) {
                        var el = timers[i];
                        var st = window.getComputedStyle ? getComputedStyle(el) : null;
                        out.push({
                            id: el.id || '',
                            cls: el.className || '',
                            display: st ? st.display : '',
                            text: (el.innerText || '').replace(/\\s+/g, ' ').trim().slice(0, 160)
                        });
                    }
                    var titles = document.querySelectorAll('.timer__title, .timer__time');
                    for (var j = 0; j < titles.length && j < 8; j++) {
                        out.push({
                            id: 'node',
                            cls: titles[j].className || '',
                            display: '',
                            text: (titles[j].innerText || '').trim().slice(0, 120)
                        });
                    }
                    var body = (document.body && document.body.innerText || '').replace(/\\s+/g, ' ');
                    var hit = '';
                    var m = body.match(/Повітря[^\\n]{0,40}/i);
                    if (m) hit = m[0];
                    mp.trigger('qt:airDumpCef', JSON.stringify({hit: hit, nodes: out}).slice(0, 3500));
                } catch (e) {
                    mp.trigger('qt:airDumpCef', JSON.stringify({error: String(e)}));
                }
            })()`);
        } catch (e) {
            airDumpPush('[cef] scrape err=' + e);
        }
    }

    function finishAirHudDump() {
        if (airDumpCefTimer) {
            try { qtClearTimeout(airDumpCefTimer); } catch (e) {}
            airDumpCefTimer = null;
        }
        airDumpUntil = 0;
        airDumpSnapshotStatic('end');
        airDumpBuffer = airDumpLines.join('\n');
        log('Air HUD dump ready: ' + airDumpLines.length + ' lines');
        qtSilentNotice('Air dump ready (' + airDumpLines.length + ' lines)');
    }

    function startAirHudDump(ms) {
        const dur = Math.max(5000, Math.min(60000, parseInt(ms, 10) || 20000));
        airDumpLines = [];
        airDumpSeen = {};
        airDumpBuffer = '';
        airDumpUntil = Date.now() + dur;
        airDumpPush('=== AIR HUD DUMP start ' + new Date().toISOString() + ' dur=' + dur + 'ms ===');
        airDumpSnapshotStatic('start');
        airDumpScrapeCef();
        if (airDumpCefTimer) {
            try { qtClearTimeout(airDumpCefTimer); } catch (e) {}
        }
        let ticks = 0;
        function tick() {
            if (!airDumpActive()) {
                finishAirHudDump();
                return;
            }
            ticks++;
            if (ticks % 2 === 0) airDumpScrapeCef();
            if (ticks % 4 === 0) airDumpSnapshotStatic('tick' + ticks);
            airDumpCefTimer = qtSetTimeout(tick, 1000);
        }
        airDumpCefTimer = qtSetTimeout(tick, 1000);
        qtSetTimeout(function () {
            if (airDumpActive()) finishAirHudDump();
        }, dur + 50);
        log('Air HUD dump: capturing ' + (dur / 1000) + 's — keep HUD visible');
        qtSilentNotice('Air dump ' + (dur / 1000) + 's — HUD on screen');
    }

    try {
        mp.events.add('qt:airDumpCef', function (json) {
            try { airDumpPush('[cef] ' + String(json).slice(0, 3500)); } catch (e) {}
        });
    } catch (e) {}

    try {
        mp.events.add('client::setTimebar', function (title, seconds) {
            try {
                airDumpPush('[setTimebar] title=' + JSON.stringify(title) +
                    ' seconds=' + seconds + ' args=' + JSON.stringify([].slice.call(arguments).slice(0, 6)));
                if (isAirHudTitle(title) ||
                    (/пов[іиi]тря/i.test(String(title || '')) && !/поц[іиi]лунок/i.test(String(title || '')))) {
                    noteAirSeconds(seconds, 'setTimebar');
                }
            } catch (e) {}
        });
        mp.events.add('client::clearTimebar', function () {
            try {
                airDumpPush('[clearTimebar] args=' + JSON.stringify([].slice.call(arguments).slice(0, 4)));
            } catch (e) {}
        });
    } catch (e) {}

    function considerDrawTextDump(api, text, args) {
        try {
            if (typeof text !== 'string' && typeof text !== 'number') return;
            const raw = String(text);
            const clean = stripGtaTextCodes(raw);
            const mins = matchAirMinutesFromDrawText(raw, args);
            if (mins != null) noteAirMinutes(mins, api);
            if (!airDumpActive()) return;
            // Always dump air-ish / bottom-screen / хв texts; sample others lightly.
            let pos = '';
            try {
                const p = args && args[1];
                if (Array.isArray(p)) {
                    pos = ' pos=[' + p.map(function (v) {
                        return (typeof v === 'number') ? (Math.round(v * 1000) / 1000) : v;
                    }).join(',') + ']';
                }
            } catch (e2) {}
            const interesting = /пов[іиi]тря|хв|air|oxygen|timer/i.test(clean) ||
                (pos.indexOf('0.8') !== -1 || pos.indexOf('0.9') !== -1);
            if (interesting || Object.keys(airDumpSeen).length < 80)
                airDumpPush('[' + api + ']' + pos + ' text=' + JSON.stringify(raw).slice(0, 200));
        } catch (e) {}
    }

    const origDrawText = mp.game.graphics.drawText;
    mp.game.graphics.drawText = function (text) {
)QTJSX91"
        R"QTJSX91(        considerDrawTextDump('graphics.drawText', text, arguments);
        return origDrawText.apply(mp.game.graphics, arguments);
    };

    try {
        if (mp.game.ui && typeof mp.game.ui.drawText === 'function' && !mp.game.ui.drawText.__qtAirHooked) {
            const origUiDrawText = mp.game.ui.drawText.bind(mp.game.ui);
            mp.game.ui.drawText = function (text) {
                considerDrawTextDump('ui.drawText', text, arguments);
                return origUiDrawText.apply(mp.game.ui, arguments);
            };
            mp.game.ui.drawText.__qtAirHooked = true;
        }
    } catch (e) {}

    // ---- Safe TP ----
    // AC teleport: ΔXY >= 650 and lastLegitTpTime older than 2.5s.
    // Safe mode: never mp.events.call('ac:refreshtp'); steps stay <650m and we
    // silently bump lastLegitTp via captured AC.refreshTp (bound handler).
    const TP_STEP_DIST = 500;
    const TP_STEP_DELAY = 250;
    const TP_STEP_DELAY_SAFE = 300; // >= AC 250ms tick so lastPosition can catch up

    function ensureAcRefreshCapture() {
        if (mp.events.add.__qtAcRefreshHooked) return;
        try {
            const origAdd = mp.events.add.bind(mp.events);
            mp.events.add = function (name, handler) {
                try {
                    if (name === 'ac:refreshtp' && typeof handler === 'function') {
                        _qtAcRefreshTp = handler;
                    }
                } catch (e) {}
                return origAdd.apply(this, arguments);
            };
            mp.events.add.__qtAcRefreshHooked = true;
        } catch (e) {}
    }
    ensureAcRefreshCapture();

    function markLegitTp() {
        // Prefer silent bound refreshTp — does NOT fire mp.events.call('ac:refreshtp')
        if (_qtAcRefreshTp) {
            try { _qtAcRefreshTp(); return true; } catch (e) {}
        }
        if (!safeTpMode) {
            try { mp.events.call('ac:refreshtp'); return true; } catch (e) {}
        }
        return false;
    }

    function buildPath(fromX, fromY, toX, toY) {
        const dx = toX - fromX;
        const dy = toY - fromY;
        const totalDist = Math.sqrt(dx * dx + dy * dy);
        if (totalDist <= TP_STEP_DIST) return [{ x: toX, y: toY }];
        const steps = Math.ceil(totalDist / TP_STEP_DIST);
        const stepX = dx / steps;
        const stepY = dy / steps;
        const path = [];
        for (let i = 1; i <= steps; i++) {
            path.push({ x: fromX + stepX * i, y: fromY + stepY * i });
        }
        return path;
    }

    // Distance FROM BOX TO PLAYER. XY within `radius`, |dZ| ≤ 30m (filter flying/diving players).
    // Scans both streamed players AND all remoteIds (dedupe via Set) to catch edge-of-stream players.
    // Returns { name, dist, dz } of the nearest occupying player, or null if clear.
    function boxOccupiedBy(point, radius) {
        const r2 = radius * radius;
        const MAX_DZ = 30;
        let closest = null, closestD2 = Infinity;
        try {
            const local = mp.players.local;
            const seen = new Set();
            const check = (p) => {
                try {
                    if (!p || p === local || !p.handle || p.handle === 0) return;
                    if (seen.has(p.remoteId)) return;
                    seen.add(p.remoteId);
                    const pos = p.position;
                    if (!pos || (pos.x === 0 && pos.y === 0 && pos.z === 0)) return;
                    const dx = pos.x - point.x;
                    const dy = pos.y - point.y;
                    const dz = pos.z - point.z;
                    if (Math.abs(dz) > MAX_DZ) return; // player way above/below — not on box
                    const d2 = dx * dx + dy * dy;
                    if (d2 < r2 && d2 < closestD2) {
                        closestD2 = d2;
                        closest = { name: p.name || ('ID:' + p.remoteId), dist: Math.sqrt(d2), dz: dz };
                    }
                } catch (e) {}
            };
            // 1) Streamed players (reliable live positions)
            mp.players.forEachInStreamRange(check);
            // 2) All known remoteIds (catches just-streamed-out players with last-known position)
            for (let i = 0; i < 2500; i++) {
                try { const p = mp.players.atRemoteId(i); if (p) check(p); } catch (e) {}
            }
        } catch (e) { log('  Occupancy scan ERROR: ' + e.message); }
        return closest;
    }

    async function teleportTo(x, y, z) {
        const _tpStart = Date.now();
        try {
            const player = getPlayer();
            if (!player || !mp.players.exists(player)) { log('  TP: no player, abort'); return; }
            const veh = getVehicle();
            const startPos = player.position;
            if (!startPos || startPos.x == null) { log('  TP: no startPos, abort'); return; }
            const hasVeh = veh && mp.vehicles.exists(veh);
            const inVehicle = !!player.vehicle;

            try { player.setCollision(false, false); } catch (e) {}
            if (hasVeh) {
                try { veh.setCollision(false, false); } catch (e) {}
                veh.freezePosition(true);
                try { veh.setRotation(0, 0, veh.rotation.z, 2, true); } catch (e) {}
            }

            const path = buildPath(startPos.x, startPos.y, x, y);
            const totalDist = Math.round(Math.sqrt((x-startPos.x)**2 + (y-startPos.y)**2));
            const stepDelay = safeTpMode ? TP_STEP_DELAY_SAFE : TP_STEP_DELAY;
            log('  TP: dist=' + totalDist + 'm, steps=' + path.length + ', veh=' + (hasVeh?'yes':'no') + ', inVeh=' + (inVehicle?'yes':'no') + ', safeTp=' + !!safeTpMode);

            for (let i = 0; i < path.length - 1; i++) {
                const pt = path[i];
                markLegitTp();
                await sleep(50);
                if (hasVeh) {
                    veh.freezePosition(false);
                    veh.setCoordsNoOffset(pt.x, pt.y, -50, false, false, false);
                    veh.setRotation(0, 0, veh.rotation.z, 2, true);
                    veh.freezePosition(true);
                }
                if (!inVehicle) player.setCoordsNoOffset(pt.x, pt.y, -50, false, false, false);
                await sleep(stepDelay);
            }

            markLegitTp();
            await sleep(50);

            if (hasVeh) {
                veh.freezePosition(false);
                veh.setCoordsNoOffset(x, y, z, false, false, false);
                veh.setRotation(0, 0, veh.rotation.z, 2, true);
                veh.freezePosition(true);
                if (!inVehicle) {
                    player.setCoordsNoOffset(x, y, z, false, false, false);
                    qtSetTimeout(() => { try { mp.game.invoke("0xF75B0D629E1C063D", player.handle, veh.handle, -1); } catch (e) {} }, 200);
                }
            } else {
                player.setCoordsNoOffset(x, y, z, false, false, false);
            }
            log('  TP done: ' + path.length + ' steps in ' + (Date.now()-_tpStart) + 'ms');
        } catch (e) { log('  TP ERROR: ' + e.message); }
    }

    function loadVehicleRemote() {
        if (!sessionToken) { log('No token!'); return; }
        callRemote("aquanautLoadVehicle", null);
        log('aquanautLoadVehicle called');
    }

    // ---- Colshape intercept ----
    mp.events.add("createDynamicColshapesByParams", (e) => {
        try {
            const items = Array.isArray(e) ? e : [e];
            items.forEach(item => {
                if (!item || !item.uniqName) return;
                if (item.uniqName.indexOf('aquanaut') !== -1 && item.params && item.params.catchEvent === 'takeAquanautObject') {
                    const id = item.params.catchEventParams ? item.params.catchEventParams.id : null;
                    if (id != null && item.position) {
                        const point = { id: id, x: item.position.x, y: item.position.y, z: item.position.z, scale: item.scale || 4, uniqName: item.uniqName };
                        const exists = aquanautPoints.find(p => p.id === id);
                        if (!exists) {
                            aquanautPoints.push(point);
                            log('Point #' + id + ' (' + point.x.toFixed(0) + ', ' + point.y.toFixed(0) + ')');
                        }
                    }
                }
            });
            if (aquanautPoints.length > 0) colshapesReady = true;
        } catch (ex) {}
    });

    // ---- Notify parser (browserMenu.execute hook) ----
    function parseNotify(code) {
        try {
            if (typeof code !== 'string') return;
            if (code.indexOf('__rpc:process') === -1) return;
            const m = code.match(/process\("(.+?)"\);?\s*\}/);
            if (!m) return;
            const json = m[1].replace(/\\"/g, '"').replace(/\\\\/g, '\\');
            const obj = JSON.parse(json);
            if (!obj || !obj.name || !obj.args) return;
            const fnName = obj.name;
            const text = Array.isArray(obj.args) ? obj.args[0] : '';
            if (!text || typeof text !== 'string') return;

            if (fnName === 'nInfo') {
                gotNinfo = true;
                lastNinfoText = text;
                if (/залишил(ось|ась|ись)|схованк/i.test(text)) {
                    gotBoxCollected = true;
                    const rm = text.match(/(\d+)/);
                    if (rm) log('nInfo: залишилось ' + rm[1] + ' схованок');
                    else log('nInfo: ' + text);
                } else {
                    log('nInfo: ' + text);
                }
                if (/знайдено.*перлин/i.test(text)) {
                    const p = lootPriceFor('pearl');
                    bonusEarned += p;
                    log('PEARL +' + p + '$ (bonus)' + lootPriceSource('pearl'));
                } else if (/знайдено.*зірк/i.test(text) || /знайдено.*звезд/i.test(text)) {
                    const p = lootPriceFor('starfish');
                    bonusEarned += p;
                    log('STARFISH +' + p + '$ (bonus)' + lootPriceSource('starfish'));
                } else if (/знайдено.*рапан/i.test(text)) {
                    const p = lootPriceFor('rapana');
                    bonusEarned += p;
                    log('RAPANA +' + p + '$ (bonus)' + lootPriceSource('rapana'));
                }
            }
            if (fnName === 'nSuccess' && /пов[іиi]тря/i.test(text)) {
                gotAirLoaded = true;
                log('Air loaded!');
            }
            if (fnName === 'nSuccess' && /зароб/i.test(text)) {
                let base = 0, skill = 0, vip = 0;
                const mBase = text.match(/зароблено\s+(\d+)/i);
                if (mBase) base = parseInt(mBase[1]);
                const mSkill = text.match(/навичк[иі]\s+(\d+)/i);
                if (mSkill) skill = parseInt(mSkill[1]);
                const mVip = text.match(/VIP\s+(\d+)/i);
                if (mVip) vip = parseInt(mVip[1]);
)QTJSX91"
        R"QTJSX91(                const earned = base + skill + vip;
                if (earned > 0) {
                    totalEarned += earned;
                    gotEarned = true;
                    gotBoxCollected = true;
                    log('Earned: ' + earned + '$');
                }
            }
            if (fnName === 'nWarning') log('Warning: ' + text);
            if (fnName === 'nError') {
                log('Error: ' + text);
                if (/замовлення.*хтось.*прийняв|спробуй інше/i.test(text)) gotOrderError = true;
            }
        } catch (e) {}
    }

    // ---- browserMenu hooks ----
    (function hookExec() {
        if (!qtAlive) return;
        try {
            if (typeof browserMenu === 'undefined' || !browserMenu) { qtSetTimeout(hookExec, 1000); return; }
            if (browserMenu.execute.__qtHooked) return;
            const origExec = browserMenu.execute.bind(browserMenu);
            browserMenu.execute = function (code) {
                parseNotify(code);
                return origExec.apply(browserMenu, arguments);
            };
            browserMenu.execute.__qtHooked = true;
            log('browserMenu.execute hooked');
        } catch (e) { qtSetTimeout(hookExec, 1000); }
    })();


    (function hookBM() {
        if (!qtAlive) return;
        try {
            if (typeof browserMenu === 'undefined' || !browserMenu) { qtSetTimeout(hookBM, 1000); return; }
            if (browserMenu.call.__qtBMHooked) return;
            const origCall = browserMenu.call.bind(browserMenu);
            browserMenu.call = function (api, method) {
                try {
                    if (api === 'timerAPI') {
                        airDumpPush('[timerAPI] ' + method + ' ' +
                            JSON.stringify([].slice.call(arguments, 2, 8)));
                    }
                    if (api === 'timerAPI' && method === 'setTimer') {
                        // browserMenu.call("timerAPI", "setTimer", title, seconds, ...)
                        const title = arguments[2];
                        const seconds = arguments[3];
                        if (isAirHudTitle(title) ||
                            (/пов[іиi]тря/i.test(String(title || '')) &&
                             !/поц[іиi]лунок/i.test(String(title || '')))) {
                            noteAirSeconds(seconds, 'timerAPI');
                        }
                    }
                    if (api === 'socket' && method === 'callRemote' && Array.isArray(arguments[2])) {
                        const arr = arguments[2];
                        // Auto-token from any callRemote via M menu
                        if (arr[1] && typeof arr[1] === 'string' && arr[1].length > 5) {
                            if (!sessionToken || sessionToken !== arr[1]) {
                                sessionToken = arr[1];
                                log('Token auto (M): ' + sessionToken.substring(0, 8));
                            }
                        }
                        // Block armour timeout
                        if (armourBypassEnabled && arr[0] === 'setArmourTimeout') return;
                        // Block admin log
                        if (arr[0] === 'logTxt') { log('~r~BLOCKED logTxt: ' + (arr[2] || arr[1] || '')); return; }
                    }
                } catch (e) {}
                return origCall.apply(browserMenu, arguments);
            };
            browserMenu.call.__qtBMHooked = true;
        } catch (e) { qtSetTimeout(hookBM, 1000); }
    })();

    // ---- v2: Admin HUD + Server Invis ----
    let origAdminVal = 0;
    let origWhVal = 0;
    let origWhcVal = 0;
    let origWhfVal = 0;
    let v2AdminActive = false;

    function v2AdminOn() {
        if (v2AdminActive) return;
        if (!invisEnabled) {
            log('Admin HUD skipped (invis OFF)');
            return;
        }
        origAdminVal = mp.clientStorage.admin || 0;
        origWhVal = mp.clientStorage.wh || 0;
        origWhcVal = mp.clientStorage.whc || 0;
        origWhfVal = mp.clientStorage.whf || 0;
        mp.clientStorage.admin = 7;
        mp.clientStorage.wh = 1;   // player WH
        mp.clientStorage.whc = 1;  // vehicle WH
        mp.clientStorage.whf = 1;  // family WH
        v2AdminActive = true;
        log('Admin HUD ON (invis/admin features)');
    }

    function v2AdminOff() {
        if (!v2AdminActive) return;
        mp.clientStorage.admin = origAdminVal;
        mp.clientStorage.wh = origWhVal;
        mp.clientStorage.whc = origWhcVal;
        mp.clientStorage.whf = origWhfVal;
        v2AdminActive = false;
        log('Admin HUD OFF');
    }

    // ---- Submarine rental ----
    const SUB_RENTAL_POINT = { x: 20.3, y: -2733.8, z: 6.0 };

    function rentSubmarine(hours) {
        if (!sessionToken) { log('Rent sub: no token'); qtUiLog('⚠ Нет session token'); return; }
        const h = Math.max(1, Math.min(48, parseInt(hours) || 1));
        const payload = JSON.stringify({ model: 'submersible', time: h, color: [213, 60, 39] });
        try {
            browserMenu.call('socket', 'callRemote', ['giveVehicleFromPickupPoint', sessionToken, payload]);
            log('🚢 Rent sub: ' + h + 'h');
            qtUiLog('🚢 Аренда субмарины: ' + h + 'ч');
        } catch (e) {
            log('Rent sub error: ' + e.message);
            qtUiLog('⚠ Ошибка аренды: ' + e.message);
        }
    }

    function cancelSubRental() {
        if (!sessionToken) { log('Cancel rent: no token'); return; }
        const payload = JSON.stringify({ text: 'Ви вже маєте в оренді транспортний засіб. Відмовитись від оренди?', acceptEvent: 'cancelJobRental' });
        try {
            browserMenu.call('socket', 'callRemote', ['cancelJobRental', sessionToken, payload]);
            log('🚢 Cancel sub rental');
            qtUiLog('🚢 Отмена аренды');
        } catch (e) {
            log('Cancel sub rental error: ' + e.message);
        }
    }

    function teleportToRentalPoint() {
        try {
            try { mp.events.call('ac:refreshtp'); } catch (e) {}
            const p = getPlayer();
            if (!p) return;
            p.setCoordsNoOffset(SUB_RENTAL_POINT.x, SUB_RENTAL_POINT.y, SUB_RENTAL_POINT.z, false, false, false);
            log('🚢 TP to submarine rental point (' + SUB_RENTAL_POINT.x + ',' + SUB_RENTAL_POINT.y + ',' + SUB_RENTAL_POINT.z + ')');
        } catch (e) { log('TP rental err: ' + e.message); }
    }

    function setServerInvis(on) {
        try {
            if (!sessionToken) return;
            browserMenu.call('socket', 'callRemote', ['setInvisible', sessionToken, on ? true : false]);
            mp.players.local.setAlpha(on ? 0 : 255);
            if (mp.players.local.vehicle) mp.players.local.vehicle.setAlpha(on ? 0 : 255);
            log('Server invis: ' + (on ? 'ON' : 'OFF'));
        } catch(e) { log('Invis error: ' + e); }
    }

    // ---- v2: Food/Water functions ----
    function useItem(sqlId) {
        if (!sessionToken || !sqlId) return;
        try {
            browserMenu.call("socket", "callRemote", [
                "useItem",
                sessionToken,
                JSON.stringify({ type: "use", sqlId: sqlId })
            ]);
        } catch (e) { log('useItem error: ' + e); }
    }

    function eatAndDrink() {
        if (foodSqlId) {
            useItem(foodSqlId);
            log('Auto EAT (sqlId: ' + foodSqlId + ')');
        }
        qtSetTimeout(() => {
            if (waterSqlId) {
                useItem(waterSqlId);
                log('Auto DRINK (sqlId: ' + waterSqlId + ')');
            }
        }, 7000);
    }

    function startEatInterval() {
        if (eatInterval) qtClearInterval(eatInterval);
        eatInterval = qtSetInterval(() => {
            if ((botActive || companyBotActive) && (foodSqlId || waterSqlId)) {
                eatAndDrink();
            }
        }, EAT_DRINK_INTERVAL);
    }

    function stopEatInterval() {
        if (eatInterval) { qtClearInterval(eatInterval); eatInterval = null; }
    }

    // useItem hook for food/water detection
    function injectUseItemHook() {
        injectTokenHook();
    }

    // ---- Invincible helpers (protect from terrain damage underground) ----
    function setPlayerInvincible(on) {
        try {
            const p = getPlayer();
            if (p) p.setInvincible(on);
            const v = getVehicle();
            if (v) {
                if (on) {
                    v.setFixed();
                    v.setEngineHealth(1000);
                    v.setBodyHealth(1000);
                }
                v.setInvincible(on);
            }
        } catch (e) {}
    }

    // ---- HP monitoring: detect damage → invincible + air reload (once per 60s) ----
    let lastKnownHp = null;
    let hpMonitorInterval = null;
    let lastAirReloadTime = 0;
    const AIR_RELOAD_COOLDOWN = 60000;

    function startHpMonitor() {
        if (hpMonitorInterval) return;
        lastKnownHp = null;
        lastAirReloadTime = 0;
        hpMonitorInterval = qtSetInterval(() => {
            if (!botActive) return;
            try {
                const p = getPlayer();
                if (!p) return;
                const hp = p.getHealth();
                if (lastKnownHp !== null && hp < lastKnownHp && hp > 0) {
                    setPlayerInvincible(true);
                    const now = Date.now();
                    if (now - lastAirReloadTime > AIR_RELOAD_COOLDOWN) {
                        lastAirReloadTime = now;
                        log('HP drop: ' + lastKnownHp + ' → ' + hp + '. Air reload sent');
                        if (sessionToken) callRemote("aquanautLoadVehicle", null);
                    }
                }
                lastKnownHp = hp;
            } catch (e) {}
        }, 500);
    }

    function stopHpMonitor() {
        if (hpMonitorInterval) { qtClearInterval(hpMonitorInterval); hpMonitorInterval = null; }
        lastKnownHp = null;
        lastAirReloadTime = 0;
    }

    // ---- Collect one box (v3 flash mode: travel underground, FLASH up to box, collect, flash back down) ----
    // Server requires player INSIDE submarine to collect, so we keep player in vehicle throughout.
    // Trick: travel is hidden (underground 50m), only flash-visit box for ~200-500ms during collect.
    async function collectOneBoxParking(point) {
        log('── Box #' + point.id + ' START (v3 flash) @(' + point.x.toFixed(1) + ',' + point.y.toFixed(1) + ',' + point.z.toFixed(1) + ') scale=' + point.scale);
        for (let attempt = 1; ; attempt++) {
            if (!botActive) { log('  Bot stopped mid-collect, abort'); return false; }
            const _attemptStart = Date.now();
            log('  → Try ' + attempt + ' (flash)');

            const player = getPlayer();
            let veh = (myVehicleRef && mp.vehicles.exists(myVehicleRef)) ? myVehicleRef : getVehicle();
            if (!player) { log('  No player!'); await sleep(500); continue; }
)QTJSX91"
        R"QTJSX91(            if (!veh || !mp.vehicles.exists(veh)) {
                log('  ⚠ No valid submarine reference! Re-caching from player.vehicle');
                if (player.vehicle) { myVehicleRef = player.vehicle; myVehicleModel = player.vehicle.model; veh = myVehicleRef; }
            }

            setPlayerInvincible(true);
            const boxZ = point.z - (point.scale - 1);    // land inside colshape (radius = scale)
            const undergroundZ = point.z - 50;           // 50m below box — hidden parking/travel z
            log('  boxZ=' + boxZ.toFixed(2) + ', undergroundZ=' + undergroundZ.toFixed(2) + ', subHandle=' + (veh ? veh.handle : 'none'));

            // Step 1: Stepped TP vehicle+player to underground near box (hidden travel, AC-safe via ac:refreshtp per step)
            log('  [1] Travel underground → (' + point.x.toFixed(0) + ',' + point.y.toFixed(0) + ',' + undergroundZ.toFixed(0) + ')');
            await teleportTo(point.x, point.y, undergroundZ);
            freezeVehicle(true);

            // Step 1.5: check if another player is on the box — skip if occupied (no wait, immediate scan)
            if (skipOccupiedBoxes) {
                const occ = boxOccupiedBy(point, BOX_SKIP_RADIUS_XY);
                if (occ) {
                    log('  ⚠ Box #' + point.id + ' OCCUPIED by ' + occ.name + ' @' + occ.dist.toFixed(1) + 'm — staying underground, SKIP (will retry)');
                    qtUiLog('⏭ Box #' + point.id + ' skip: ' + occ.name + ' @' + occ.dist.toFixed(0) + 'm');
                    freezeVehicle(true);
                    setPlayerInvincible(false);
                    return 'occupied';
                }
            }

            // Step 2: FLASH both vehicle AND player 50m UP to box (short TP — AC friendly, small delta)
            log('  [2] FLASH veh+ply UP 50m → box z=' + boxZ.toFixed(1));
            try { mp.events.call('ac:refreshtp'); } catch (e) {}
            if (veh && mp.vehicles.exists(veh)) {
                veh.freezePosition(false);
                veh.setCoordsNoOffset(point.x, point.y, boxZ, false, false, false);
                veh.freezePosition(true);
            }
            player.setCoordsNoOffset(point.x, point.y, boxZ, false, false, false);

            // Step 3: Short server sync + call collect
            try { mp.events.call('ac:refreshtp'); } catch (e) {}
            log('  [3] Pre-collect sleep ' + DELAY.BOX_PRE_COLLECT + 'ms...');
            await sleep(DELAY.BOX_PRE_COLLECT);

            gotBoxCollected = false;
            log('  [4] → callRemote takeAquanautObject id=' + point.id);
            callRemote("takeAquanautObject", JSON.stringify({ id: point.id }));

            // Step 5: Poll for confirmation
            let waited = 0;
            while (botActive && waited < DELAY.BOX_CONFIRM_MAX) {
                await sleep(DELAY.BOX_POLL);
                waited += DELAY.BOX_POLL;
                if (gotBoxCollected) break;
            }
            log('  [5] Poll end: gotBox=' + gotBoxCollected + ', waited=' + waited + 'ms');

            // Step 6: FLASH back down 50m (hide vehicle again — regardless of success)
            try { mp.events.call('ac:refreshtp'); } catch (e) {}
            if (veh && mp.vehicles.exists(veh)) {
                veh.freezePosition(false);
                veh.setCoordsNoOffset(point.x, point.y, undergroundZ, false, false, false);
                veh.freezePosition(true);
            }
            player.setCoordsNoOffset(point.x, point.y, undergroundZ, false, false, false);
            log('  [6] FLASH veh+ply DOWN 50m → underground');

            if (gotBoxCollected) {
                log('✓ Box #' + point.id + ' OK (' + waited + 'ms, try ' + attempt + ', total ' + (Date.now()-_attemptStart) + 'ms)');
                boxesCollected++;
                totalBoxes++;
                gotBoxCollected = false;
                setPlayerInvincible(false);
                return true;
            }

            log('  ✗ Try ' + attempt + ' FAIL, retry in ' + DELAY.BOX_RETRY_WAIT + 'ms');
            setPlayerInvincible(false);
            await sleep(DELAY.BOX_RETRY_WAIT);
        }
    }

    // ---- Collect one box (standard v1/v2 mode: vehicle teleports with player) ----
    async function collectOneBox(point) {
        // v4: parking/v3 path removed — surface collect only
        parkingEnabled = false;
        log('── Box #' + point.id + ' START @(' + point.x.toFixed(1) + ',' + point.y.toFixed(1) + ',' + point.z.toFixed(1) + ') scale=' + point.scale);
        for (let attempt = 1; ; attempt++) {
            if (!botActive) { log('  Bot stopped mid-collect, abort'); return false; }
            const _attemptStart = Date.now();
            log('  → Try ' + attempt);

            setPlayerInvincible(true);
            const tpZ = point.z - (point.scale - 1); // land inside colshape (radius = scale)
            log('  tpZ=' + tpZ.toFixed(2) + ' (z=' + point.z + ', scale=' + point.scale + ')');

            // Single TP directly to landing coord (teleportTo handles stepped if far)
            await teleportTo(point.x, point.y, tpZ);
            freezeVehicle(true);

            // Position verification — re-TP if drifted (rare but happens)
            const player = getPlayer();
            const veh = getVehicle();
            try {
                const pos = player.position;
                const dx = pos.x - point.x, dy = pos.y - point.y, dz = pos.z - tpZ;
                const offset = Math.sqrt(dx*dx + dy*dy + dz*dz);
                log('  Post-TP pos=(' + pos.x.toFixed(1) + ',' + pos.y.toFixed(1) + ',' + pos.z.toFixed(1) + ') drift=' + offset.toFixed(2) + 'm');
                if (offset > 2) {
                    log('  ⚠ Drift >2m, re-TP');
                    try { mp.events.call('ac:refreshtp'); } catch (e) {}
                    if (veh && player.vehicle) {
                        veh.setCoordsNoOffset(point.x, point.y, tpZ, false, false, false);
                    } else if (player) {
                        player.setCoordsNoOffset(point.x, point.y, tpZ, false, false, false);
                    }
                    freezeVehicle(true);
                }
            } catch (e) { log('  Drift check ERROR: ' + e.message); }

            // Immediate occupancy check (no wait for streaming)
            if (skipOccupiedBoxes) {
                const occ = boxOccupiedBy(point, BOX_SKIP_RADIUS_XY);
                if (occ) {
                    log('  ⚠ Box #' + point.id + ' OCCUPIED by ' + occ.name + ' @' + occ.dist.toFixed(1) + 'm — hide & SKIP (will retry)');
                    qtUiLog('⏭ Box #' + point.id + ' skip: ' + occ.name + ' @' + occ.dist.toFixed(0) + 'm');
                    try { mp.events.call('ac:refreshtp'); } catch (e) {}
                    const vehHide = getVehicle();
                    const playerHide = getPlayer();
                    if (vehHide && playerHide && playerHide.vehicle) {
                        vehHide.setCoordsNoOffset(point.x, point.y, point.z - 50, false, false, false);
                    } else if (playerHide) {
                        playerHide.setCoordsNoOffset(point.x, point.y, point.z - 50, false, false, false);
                    }
                    freezeVehicle(false);
                    setPlayerInvincible(false);
                    return 'occupied';
                }
            }

            // AC refresh + pre-collect server sync wait (only when we actually collect)
            try { mp.events.call('ac:refreshtp'); } catch (e) {}
            log('  Pre-collect sleep ' + DELAY.BOX_PRE_COLLECT + 'ms...');
            await sleep(DELAY.BOX_PRE_COLLECT);

            // Request collection
            gotBoxCollected = false;
            log('  → callRemote takeAquanautObject id=' + point.id);
            callRemote("takeAquanautObject", JSON.stringify({ id: point.id }));

            // Poll for confirmation (tight 50ms polling up to BOX_CONFIRM_MAX)
            let waited = 0;
            while (botActive && waited < DELAY.BOX_CONFIRM_MAX) {
                await sleep(DELAY.BOX_POLL);
                waited += DELAY.BOX_POLL;
                if (gotBoxCollected) break;
            }
            log('  Poll end: gotBox=' + gotBoxCollected + ', waited=' + waited + 'ms');

            // Hide underground (stealth between boxes)
            try { mp.events.call('ac:refreshtp'); } catch (e) {}
            const vehHide = getVehicle();
            const playerHide = getPlayer();
            if (vehHide && playerHide && playerHide.vehicle) {
                vehHide.setCoordsNoOffset(point.x, point.y, point.z - 50, false, false, false);
            } else if (playerHide) {
                playerHide.setCoordsNoOffset(point.x, point.y, point.z - 50, false, false, false);
            }
            freezeVehicle(true);

            if (gotBoxCollected) {
                log('✓ Box #' + point.id + ' OK (' + waited + 'ms, try ' + attempt + ', total ' + (Date.now()-_attemptStart) + 'ms)');
                boxesCollected++;
                totalBoxes++;
                gotBoxCollected = false;
                setPlayerInvincible(false);
                freezeVehicle(false);
                return true;
            }

            log('  ✗ Try ' + attempt + ' FAIL, retry in ' + DELAY.BOX_RETRY_WAIT + 'ms');
            setPlayerInvincible(false);
            await sleep(DELAY.BOX_RETRY_WAIT);
        }
    }

    // ---- Tablet orders ----
    let tabletOrders = [];
    let tabletDataReady = false;
    let tabletLastSetTs = 0;  // ms timestamp of last client::TabletPrimarySetData receive
    let fastLastOrderReady = false;
    const FAST_LAST_ORDER_ID = 39;

    function sortPointsNearestNeighbor(points, startPos) {
        const remaining = (points || []).slice();
        const ordered = [];
        let cx = startPos && startPos.x != null ? startPos.x : 0;
        let cy = startPos && startPos.y != null ? startPos.y : 0;
        while (remaining.length) {
            let bi = 0, bd = Infinity;
            for (let i = 0; i < remaining.length; i++) {
                const dx = remaining[i].x - cx, dy = remaining[i].y - cy;
                const d = dx * dx + dy * dy;
                if (d < bd) { bd = d; bi = i; }
            }
            const next = remaining.splice(bi, 1)[0];
            ordered.push(next);
            cx = next.x; cy = next.y;
        }
        return ordered;
    }

    async function waitForAirReady(timeoutMs) {
        const limit = timeoutMs == null ? AIR_RELOAD_WAIT_MS : timeoutMs;
        gotAirLoaded = false;
        log('Loading air...');
        if (!sessionToken) { log('Air: no token'); return false; }
        callRemote("aquanautLoadVehicle", null);
        airReloadPending = true;
        airReloadSent = true;
        airReloadAttempt = Math.max(1, airReloadAttempt || 0);
        let airWait = 0;
        while (botActive && airWait < limit) {
            await sleep(500);
            airWait += 500;
)QTJSX91"
        R"QTJSX91(            try { refreshAirMinutes(); } catch (e) {}
            const mins = airMinutes == null ? 0 : airMinutes;
            if (airReloadSucceeded(mins)) {
                log('Air OK: ' + mins + ' min');
                qtSilentNotice('Air OK: ' + mins + ' min');
                resetAirReloadState();
                gotAirLoaded = true;
                return true;
            }
        }
        log('Air: no response after ' + Math.round(limit / 1000) + 's');
        qtSilentNotice('Air: no response after ' + Math.round(limit / 1000) + 's');
        resetAirReloadState();
        return false;
    }

    function assertInvisIfNeeded(force) {
        if (!botActive || !invisEnabled) return;
        const now = Date.now();
        if (!force && lastInvisAssertMs && (now - lastInvisAssertMs) < INVIS_REASSERT_MS) return;
        setServerInvis(true);
        invisActivated = true;
        lastInvisAssertMs = now;
    }

    function spectatingAdminNearby() {
        try {
            const me = getPlayer();
            if (!me) return null;
            const mp_ = me.position;
            for (let i = 0; i < activeAdminList.length; i++) {
                const a = activeAdminList[i];
                if (!a || !a.spectating || !a.pos) continue;
                const dx = a.pos.x - mp_.x, dy = a.pos.y - mp_.y, dz = (a.pos.z || 0) - (mp_.z || 0);
                const d = Math.sqrt(dx * dx + dy * dy + dz * dz);
                if (d <= ADMIN_SPECT_PAUSE_M) return { name: a.name || 'admin', dist: d };
            }
        } catch (e) {}
        return null;
    }

    async function pauseIfAdminSpectating(nearPoint) {
        for (;;) {
            if (!botActive) return false;
            const hit = spectatingAdminNearby();
            if (!hit) {
                if (botPausedAdmin) {
                    botPausedAdmin = false;
                    log('Admin spect clear — resume');
                    qtUiLog('Admin spect clear — resume');
                }
                return true;
            }
            if (!botPausedAdmin) {
                botPausedAdmin = true;
                log('⏸ Admin spect nearby: ' + hit.name + ' @' + hit.dist.toFixed(0) + 'm — pause');
                qtUiLog('⏸ Admin spect: ' + hit.name + ' — pause');
                try {
                    if (nearPoint) {
                        try { mp.events.call('ac:refreshtp'); } catch (e) {}
                        const pl = getPlayer();
                        const veh = getVehicle();
                        const z = (nearPoint.z || 0) - 80;
                        if (veh && mp.vehicles.exists(veh)) {
                            veh.setCoordsNoOffset(nearPoint.x, nearPoint.y, z, false, false, false);
                            veh.freezePosition(true);
                        }
                        if (pl) pl.setCoordsNoOffset(nearPoint.x, nearPoint.y, z, false, false, false);
                    }
                } catch (e) {}
            }
            await sleep(2000);
        }
    }

    async function recoverIfKnocked() {
        if (!botActive) return false;
        if (!isKnockedDown) return true;
        log('Recover: knockDown detected — selfRevive');
        qtUiLog('Recover: knockDown → selfRevive');
        try { callRemote("server::selfRevive", null); } catch (e) {}
        let waited = 0;
        while (botActive && isKnockedDown && waited < 15000) {
            await sleep(500);
            waited += 500;
        }
        try {
            const pl = getPlayer();
            const veh = (myVehicleRef && mp.vehicles.exists(myVehicleRef)) ? myVehicleRef : getVehicle();
            if (pl && veh && mp.vehicles.exists(veh)) {
                mp.game.invoke("0xF75B0D629E1C063D", pl.handle, veh.handle, -1);
            }
        } catch (e) {}
        await sleep(500);
        return !isKnockedDown;
    }



    // Heuristic: detect if an order already has an executor ("Виконавця знайдено")
    function detectOrderTaken(v) {
        if (!v || typeof v !== 'object') return false;
        try {
            // Direct boolean/id fields commonly used by servers
            if (v.taken === true || v.isTaken === true || v.busy === true || v.accepted === true || v.active === true) return true;
            if (v.executor || v.performer || v.executorId || v.performerId || v.acceptedBy || v.takenBy) return true;
            // Status text field, if any
            if (typeof v.status === 'string' && /знайдено|taken|busy/i.test(v.status)) return true;
            if (typeof v.buttonText === 'string' && /знайдено/i.test(v.buttonText)) return true;
            // Fallback: scan all string fields for the Ukrainian marker
            for (const k in v) {
                const val = v[k];
                if (typeof val === 'string' && /знайдено/i.test(val) && !/схован/i.test(val)) return true;
            }
        } catch (e) {}
        return false;
    }

    function orderDistFromPlayer(o) {
        try {
            const sp = o && o.spawnPoint;
            if (!sp) return Infinity;
            const p = mp.players.local.position;
            const x = sp.x != null ? sp.x : (Array.isArray(sp) ? sp[0] : 0);
            const y = sp.y != null ? sp.y : (Array.isArray(sp) ? sp[1] : 0);
            const z = sp.z != null ? sp.z : (Array.isArray(sp) ? sp[2] : 0);
            const dx = x - p.x, dy = y - p.y, dz = z - p.z;
            return Math.sqrt(dx*dx + dy*dy + dz*dz);
        } catch (e) { return Infinity; }
    }

    // Distance used for zone filter/sort: anchor if set, else player position.
    function orderDistForZoneFilter(o) {
        if (!zoneAnchor) return orderDistFromPlayer(o);
        try {
            const sp = o && o.spawnPoint;
            if (!sp) return Infinity;
            const x = sp.x != null ? sp.x : (Array.isArray(sp) ? sp[0] : 0);
            const y = sp.y != null ? sp.y : (Array.isArray(sp) ? sp[1] : 0);
            const z = sp.z != null ? sp.z : (Array.isArray(sp) ? sp[2] : 0);
            const dx = x - zoneAnchor.x, dy = y - zoneAnchor.y, dz = z - zoneAnchor.z;
            return Math.sqrt(dx*dx + dy*dy + dz*dz);
        } catch (e) { return Infinity; }
    }

    mp.events.add("client::TabletPrimarySetData", (e) => {
        try {
            if (!e || !e.values || !Array.isArray(e.values)) return;
            tabletOrders = e.values.map((v, i) => ({
                id: v.id != null ? v.id : i,
                name: v.name || '',
                price: v.price || 0,
                spawnPoint: v.spawnPoint || null,
                taken: detectOrderTaken(v),
                tabletIndex: i,
                _raw: v,
            }));
            tabletDataReady = true;
            tabletLastSetTs = Date.now();
            const takenCnt = tabletOrders.filter(o => o.taken).length;
            log('Tablet: ' + tabletOrders.length + ' orders (' + takenCnt + ' taken)');
        } catch (ex) {}
    });

    function buildTabletSummary() {
        const summary = tabletOrders.map(o => ({
            id: o.id,
            name: o.name,
            price: o.price,
            distance: isFinite(orderDistFromPlayer(o)) ? Math.round(orderDistFromPlayer(o)) : null,
            taken: !!o.taken,
            tabletIndex: o.tabletIndex,
        }));
        if (orderMode === 'last') summary.sort((a, b) => a.tabletIndex - b.tabletIndex);
        else summary.sort((a, b) => b.price - a.price);
        return summary;
    }

    // Pre-scan tablet: open it, parse orders, send summary with distances to webapp
    async function scanTabletAndReport() {
        log('== Tablet pre-scan ==');
        qtUiLog('📋 Сканирую планшет...');

        // Preserve prior data as fallback; re-request fresh
        const hadCache = tabletOrders.length > 0;
        const cacheAgeMs = hadCache ? (Date.now() - tabletLastSetTs) : Infinity;
        tabletDataReady = false;
        try { callRemote('server::activateTabletPrimary', null); } catch (e) { log('scan: callRemote err ' + e); }

        // Wait up to 12s for fresh data
        let waited = 0;
        while (!tabletDataReady && waited < 12000) { await sleep(100); waited += 100; }

        if (tabletDataReady) {
            const summary = buildTabletSummary();
            log('Tablet scan: fresh data, ' + summary.length + ' orders');
            qtUiLog('📋 Найдено ' + summary.length + ' заказов (взято: ' + summary.filter(o => o.taken).length + ')');
            if (qtExternalConnected) qtExternalSink({ type: 'tablet_scan', orders: summary, ok: true });
            return;
        }

        // Timeout: use cached data if present (may happen if tablet was already open — server doesn't re-push)
        if (hadCache) {
            const summary = buildTabletSummary();
            const ageS = Math.round(cacheAgeMs / 1000);
            log('Tablet scan: timeout, using cached data (' + ageS + 's old), ' + summary.length + ' orders');
            qtUiLog('⚠ Новых данных нет, показываю кэш (' + ageS + 'с): ' + summary.length + ' заказов');
            if (qtExternalConnected) qtExternalSink({ type: 'tablet_scan', orders: summary, ok: true, cached: true });
            return;
        }

        log('Tablet scan: no data after ' + waited + 'ms and no cache');
        qtUiLog('⚠ Планшет не ответил. Закрой его в игре и попробуй снова.');
        if (qtExternalConnected) qtExternalSink({ type: 'tablet_scan', orders: [], ok: false });
    }

    function findBestOrder() {
        if (tabletOrders.length === 0) return 0;
        let best = tabletOrders[0];
        for (let i = 1; i < tabletOrders.length; i++) {
            if (tabletOrders[i].price > best.price) best = tabletOrders[i];
        }
        return best.id;
    }

    async function acceptOrder() {
        log('══ acceptOrder START');
        if (fastLastOrderReady) {
            if (aquanautPoints.length > 0) {
                fastLastOrderReady = false;
                colshapesReady = true;
                log('⚡ Last order preloaded: using cached points (' + aquanautPoints.length + ')');
                return true;
            }
            fastLastOrderReady = false;
        }
        // v4 primary: fast last order (even if UI mode drifted)
        if (orderMode === 'last' || orderMode === 'price' || orderMode === 'zone') {
            const preferLast = (orderMode === 'last');
            if (preferLast) {
                log('  → primary fast last order #' + FAST_LAST_ORDER_ID);
                const lastOk = await preloadFastLastOrder();
                if (lastOk) return true;
                log('  ↳ last order failed — tablet fallback');
            }
        }
        tabletDataReady = false;
        tabletOrders = [];
        log('  → callRemote server::activateTabletPrimary');
        callRemote("server::activateTabletPrimary", null);

        let waited = 0;
        while (botActive && !tabletDataReady && waited < 8000) {
            await sleep(100);
            waited += 100;
)QTJSX91"
        R"QTJSX91(        }
        log('  Tablet wait end: ready=' + tabletDataReady + ', orders=' + tabletOrders.length + ', waited=' + waited + 'ms');

        if (!tabletDataReady || tabletOrders.length === 0) {
            log('✗ No tablet data!');
            return false;
        }

        // 1) Skip orders already taken (Виконавця знайдено)
        let candidates = tabletOrders.filter(o => !o.taken);
        const skippedTaken = tabletOrders.length - candidates.length;
        if (skippedTaken) log('  ↳ Skipped ' + skippedTaken + ' taken orders');

        // 1.5) Pinned order override — if user pinned one in WebApp, use it and bypass zone/distance filters
        let pinUsed = false;
        if (pinnedOrderId != null) {
            const pinned = candidates.find(o => o.id === pinnedOrderId);
            if (pinned) {
                log('  📌 Pinned order #' + pinnedOrderId + ' found — using it, bypassing filters');
                qtUiLog('📌 Беру закреплённый заказ #' + pinnedOrderId);
                candidates = [pinned];
                pinnedOrderId = null; // one-shot: auto-clear
                pinUsed = true;
            } else {
                log('  ⚠ Pinned order #' + pinnedOrderId + ' not found or taken — clearing pin, using normal logic');
                qtUiLog('⚠ Закреплённый #' + pinnedOrderId + ' недоступен, сбрасываю');
                pinnedOrderId = null;
            }
        }

        // 2) Optional zone filter (name substring) — skipped if pin was used
        if (!pinUsed && orderMode === 'zone' && zoneFilter) {
            const q = zoneFilter.toLowerCase();
            const matched = candidates.filter(o => (o.name || '').toLowerCase().indexOf(q) !== -1);
            if (matched.length > 0) {
                log('  ↳ Zone "' + zoneFilter + '" matched ' + matched.length + '/' + candidates.length);
                candidates = matched;
            } else {
                log('  ⚠ Zone "' + zoneFilter + '" matched 0, falling back to all');
                qtUiLog('⚠ Зона "' + zoneFilter + '" не найдена, идём по цене');
            }
        }

        // 3) Zone distance filter — only orders with spawnPoint within zoneMaxDist from anchor (or player if no anchor yet). Skipped if pin was used.
        if (!pinUsed && orderMode === 'zone' && zoneMaxDist > 0 && zoneFilter) {
            const refLabel = zoneAnchor ? 'anchor' : 'player';
            const nearBy = candidates.filter(o => {
                const d = orderDistForZoneFilter(o);
                return isFinite(d) && d <= zoneMaxDist;
            });
            if (nearBy.length > 0) {
                log('  ↳ Distance filter ≤' + zoneMaxDist + 'm from ' + refLabel + ': ' + nearBy.length + '/' + candidates.length);
                candidates = nearBy;
            } else {
                let cl = null, clD = Infinity;
                for (const o of candidates) {
                    const d = orderDistForZoneFilter(o);
                    if (d < clD) { clD = d; cl = o; }
                }
                log('  ⚠ No zone orders within ' + zoneMaxDist + 'm from ' + refLabel + ' (closest: ' + (isFinite(clD) ? Math.round(clD) + 'm' : '?') + ')');
                qtUiLog('⚠ В зоне нет заказов в радиусе ' + zoneMaxDist + 'м (от ' + (zoneAnchor ? 'якоря' : 'игрока') + ', ближайший: ' + (isFinite(clD) ? Math.round(clD) + 'м' : '?') + ')');
                return false;
            }
        }

        if (candidates.length === 0) {
            log('✗ No free orders after filters');
            qtUiLog('⚠ Все заказы взяты');
            return false;
        }

        // Sort: price desc (primary). In zone mode, nearer first within zone matches.
        const sorted = candidates.slice();
        if (orderMode === 'zone') {
            sorted.sort((a, b) => {
                const da = orderDistForZoneFilter(a), db = orderDistForZoneFilter(b);
                return (isFinite(da) ? da : 1e9) - (isFinite(db) ? db : 1e9);
            });
        } else if (orderMode === 'last') {
            sorted.sort((a, b) => b.tabletIndex - a.tabletIndex);
        } else {
            sorted.sort((a, b) => b.price - a.price);
        }
        log('  Sorted ' + sorted.length + ' orders by ' + (orderMode === 'zone' ? 'distance' : (orderMode === 'last' ? 'tablet last' : 'price')));

        for (let oi = 0; oi < sorted.length; oi++) {
            if (!botActive) { log('  Bot stopped, abort'); return false; }
            const order = sorted[oi];
            aquanautPoints = [];
            colshapesReady = false;
            gotOrderError = false;
            log('  [' + (oi+1) + '/' + sorted.length + '] → getAquanautOrder #' + order.id + ' ($' + order.price + ')');
            callRemote("getAquanautOrder", order.id);

            waited = 0;
            while (botActive && !colshapesReady && !gotOrderError && waited < 5000) {
                await sleep(100);
                waited += 100;
            }
            log('  Wait end: colshapes=' + colshapesReady + ', err=' + gotOrderError + ', waited=' + waited + 'ms');

            if (gotOrderError) {
                log('  ✗ Order #' + order.id + ' taken, next...');
                continue;
            }

            if (colshapesReady) {
                log('✓ Got ' + aquanautPoints.length + ' points for #' + order.id);
                // Set zone anchor on first successful TP in zone mode (remembers the physical zone bot went to)
                if (orderMode === 'zone' && !zoneAnchor && order.spawnPoint) {
                    zoneAnchor = { x: order.spawnPoint.x, y: order.spawnPoint.y, z: order.spawnPoint.z || 0 };
                    log('🎯 Zone anchor set: (' + zoneAnchor.x.toFixed(0) + ',' + zoneAnchor.y.toFixed(0) + ')');
                    qtUiLog('🎯 Якорь зоны: (' + zoneAnchor.x.toFixed(0) + ',' + zoneAnchor.y.toFixed(0) + ')');
                }
                return true;
            }
            log('  ⚠ Order #' + order.id + ' timeout (no colshapes, no error)');
        }

        log('✗ All orders taken!');
        return false;
    }

    async function preloadFastLastOrder() {
        if (!botActive || orderMode !== 'last') return false;
        if (!sessionToken) { log('⚡ Fast last order: no token'); return false; }
        aquanautPoints = [];
        colshapesReady = false;
        gotOrderError = false;
        fastLastOrderReady = false;
        log('⚡ Fast last order: getAquanautOrder #' + FAST_LAST_ORDER_ID);
        try {
            browserMenu.call("socket", "callRemote", [
                "getAquanautOrder",
                sessionToken,
                FAST_LAST_ORDER_ID
            ]);
        } catch (e) {
            log('⚡ Fast last order error: ' + e.message);
            return false;
        }
        let waited = 0;
        while (botActive && orderMode === 'last' && !colshapesReady && !gotOrderError && waited < 5000) {
            await sleep(50);
            waited += 50;
        }
        if (gotOrderError) {
            log('⚡ Fast last order #' + FAST_LAST_ORDER_ID + ' taken/error, fallback to tablet next round');
            return false;
        }
        if (colshapesReady && aquanautPoints.length > 0) {
            fastLastOrderReady = true;
            log('⚡ Fast last order ready: ' + aquanautPoints.length + ' points in ' + waited + 'ms');
            qtUiLog('⚡ Последний заказ #' + FAST_LAST_ORDER_ID + ' взят без планшета');
            return true;
        }
        log('⚡ Fast last order timeout, fallback to tablet next round');
        return false;
    }

    // ---- Main bot loop ----
    async function botLoop() {
        log('Bot started');
        if (!sessionToken) {
            log('No token!');
            stopBot();
            return;
        }

        // v4: single surface bot (v2 features always on). Parking/v3 disabled.
        invisActivated = false;
        parkingEnabled = false;
        botPausedAdmin = false;
        roundAbortStuck = false;
        lastBoxProgressMs = Date.now();
        if (invisEnabled) v2AdminOn();

        // auto-detect food/water if not set
        if (true) {
            injectUseItemHook();
            if (!foodSqlId) {
                listeningForFood = true;
                listeningForWater = false;
                log('~o~Use FOOD in inventory to set...');
                qtSilentNotice('Waiting for FOOD item...');
                let foodWait = 0;
                while (botActive && !foodSqlId && foodWait < 60000) {
                    await sleep(500);
                    foodWait += 500;
                }
                if (!foodSqlId) {
                    log('~r~No food set, skipping auto-eat');
                    qtSilentNotice('No food set, skipping');
                } else {
                    log('~g~Food set: ' + foodSqlId);
                    qtSilentNotice('Food set: ' + foodSqlId);
                }
            }
            if (!waterSqlId && botActive) {
                listeningForWater = true;
                listeningForFood = false;
                log('~o~Use WATER in inventory to set...');
                qtSilentNotice('Waiting for WATER item...');
                let waterWait = 0;
                while (botActive && !waterSqlId && waterWait < 60000) {
                    await sleep(500);
                    waterWait += 500;
                }
                if (!waterSqlId) {
                    log('~r~No water set, skipping auto-drink');
                    qtSilentNotice('No water set, skipping');
                } else {
                    log('~g~Water set: ' + waterSqlId);
                    qtSilentNotice('Water set: ' + waterSqlId);
                }
            }
            if (!botActive) return;
            if (foodSqlId || waterSqlId) {
                eatAndDrink();
                startEatInterval();
            }
        }

        await waitForAirReady(AIR_RELOAD_WAIT_MS);

        setVehicleCollision(false);

        while (botActive) {
            log('▶▶ ROUND START (total so far: ' + totalRounds + ')');
            const _roundStart = Date.now();
            roundAbortStuck = false;
            lastBoxProgressMs = Date.now();

            // Air gate before each round
            try { refreshAirMinutes(); } catch (e) {}
            if (!airReloadSucceeded(airMinutes == null ? 0 : airMinutes)) {
                await waitForAirReady(AIR_RELOAD_WAIT_MS);
            }
            if (!botActive) break;

            const orderOk = await acceptOrder();
            if (!orderOk) {
                if (!botActive) break;
                log('Order error, wait 5s...');
                qtSilentNotice('Order error, retrying in 5s...');
                await sleep(5000);
                continue;
            }

            await sleep(DELAY.ORDER_WAIT);
            if (!botActive) break;

            let startPos = { x: 0, y: 0 };
            try {
                const pl = getPlayer();
)QTJSX91"
        R"QTJSX91(                if (pl && pl.position) startPos = { x: pl.position.x, y: pl.position.y };
            } catch (e) {}
            const points = sortPointsNearestNeighbor(aquanautPoints.slice(), startPos);
            let roundBoxes = 0;
            log('  Points to collect (NN): ' + points.length);

            if (invisEnabled && points.length > 0) {
                assertInvisIfNeeded(true);
                log('Invis: ON (first TP incoming)');
                qtUiLog('Invis ON');
            }

            // ── Pre-scan: TP 50m below box #1, then check all boxes for nearby players ──
            const preOccupiedIds = new Set();
            if (skipOccupiedBoxes && points.length > 0) {
                const scanBox = points[0];
                const scanZ = scanBox.z - 50;
                log('🔍 Pre-scan: TP 50m below box #' + scanBox.id + ' → scan ' + points.length + ' boxes');
                qtUiLog('🔍 Предсканирование: под ящик #' + scanBox.id);
                try {
                    await teleportTo(scanBox.x, scanBox.y, scanZ);
                    await sleep(1500); // wait for player streaming
                    const hits = [];
                    for (const pt of points) {
                        const occ = boxOccupiedBy(pt, BOX_SKIP_RADIUS_XY);
                        if (occ) {
                            preOccupiedIds.add(pt.id);
                            hits.push('#' + pt.id + '(' + occ.name + ' @' + occ.dist.toFixed(0) + 'm)');
                            log('  ⚠ Pre-scan: box #' + pt.id + ' occupied by ' + occ.name + ' @' + occ.dist.toFixed(1) + 'm');
                        }
                    }
                    log('🔍 Pre-scan done: ' + preOccupiedIds.size + '/' + points.length + ' occupied');
                    if (preOccupiedIds.size > 0) {
                        qtUiLog('🔍 Занято: ' + preOccupiedIds.size + '/' + points.length + ' — ' + hits.join(', '));
                    } else {
                        qtUiLog('🔍 Все ' + points.length + ' ящиков свободны');
                    }
                } catch (e) { log('Pre-scan err: ' + e.message); }
                if (!botActive) break;
            }

            const pendingOccupied = [];
            for (let i = 0; i < points.length; i++) {
                if (!botActive) break;
                if (Date.now() - lastBoxProgressMs > STUCK_WATCHDOG_MS) {
                    log('Watchdog: stuck ' + Math.round(STUCK_WATCHDOG_MS/1000) + 's — abort round');
                    qtUiLog('Watchdog: stuck — abort round');
                    roundAbortStuck = true;
                    break;
                }
                if (!(await pauseIfAdminSpectating(points[i]))) break;
                if (!(await recoverIfKnocked())) {
                    log('Recover failed — abort round');
                    roundAbortStuck = true;
                    break;
                }
                assertInvisIfNeeded(false);
                if (preOccupiedIds.has(points[i].id)) {
                    log('  [' + (i+1) + '/' + points.length + '] box #' + points[i].id + ' pre-occupied → defer to retry');
                    pendingOccupied.push(points[i]);
                    continue;
                }
                log('  [' + (i+1) + '/' + points.length + '] collecting...');
                const ok = await collectOneBox(points[i]);
                if (ok === true) { roundBoxes++; lastBoxProgressMs = Date.now(); }
                else if (ok === 'occupied') pendingOccupied.push(points[i]);
                updateBotGui();
                // Prefetch next last-order while finishing
                if (orderMode === 'last' && i >= points.length - 2 && !fastLastOrderReady) {
                    try { preloadFastLastOrder(); } catch (e) {}
                }
                if (i < points.length - 1) {
                    log('  Between-boxes sleep ' + DELAY.BETWEEN_BOXES + 'ms');
                    await sleep(DELAY.BETWEEN_BOXES);
                }
            }

            if (!botActive) break;

            // ── Retry phase: re-visit boxes skipped due to player occupancy ──
            if (pendingOccupied.length > 0 && skipOccupiedBoxes) {
                log('↺ RETRY phase: ' + pendingOccupied.length + ' occupied box(es) queued');
                qtUiLog('↺ Возврат к ' + pendingOccupied.length + ' пропущенным ящикам');
                const MAX_WAIT_ATTEMPTS = 60;    // per box: 60 × 4s = 4 minutes max wait
                const WAIT_BETWEEN = 4000;
                for (let pi = 0; pi < pendingOccupied.length; pi++) {
                    if (!botActive || roundAbortStuck) break;
                    const pt = pendingOccupied[pi];
                    log('  retry [' + (pi+1) + '/' + pendingOccupied.length + '] box #' + pt.id);
                    let done = false;
                    for (let attempt = 1; attempt <= MAX_WAIT_ATTEMPTS && botActive; attempt++) {
                        if (!(await pauseIfAdminSpectating(pt))) break;
                        if (!(await recoverIfKnocked())) break;
                        const res = await collectOneBox(pt);
                        if (res === true) {
                            roundBoxes++;
                            lastBoxProgressMs = Date.now();
                            log('    ✓ retry #' + attempt + ' collected box #' + pt.id);
                            qtUiLog('Box #' + pt.id + ' collected after wait');
                            done = true;
                            break;
                        }
                        if (res === 'occupied') {
                            log('    ⏳ still occupied (attempt ' + attempt + '/' + MAX_WAIT_ATTEMPTS + '), wait ' + (WAIT_BETWEEN/1000) + 's');
                            if (attempt === 1 || attempt % 10 === 0) qtUiLog('⏳ Ящик #' + pt.id + ' всё ещё занят (' + attempt + '/' + MAX_WAIT_ATTEMPTS + ')');
                            await sleep(WAIT_BETWEEN);
                            continue;
                        }
                        // Other failure — move to next pending box
                        log('    ✗ retry failed with non-occupied result, moving on');
                        break;
                    }
                    if (!done && botActive) {
                        log('  ⚠ Gave up on box #' + pt.id + ' after ' + MAX_WAIT_ATTEMPTS + ' attempts');
                        qtUiLog('⚠ Ящик #' + pt.id + ' пропущен окончательно');
                    }
                    updateBotGui();
                }
            }

            if (!botActive) break;

            log('◀◀ ROUND END in ' + ((Date.now()-_roundStart)/1000).toFixed(1) + 's');
            totalRounds++;
            log('Round #' + totalRounds + ' done! Boxes: ' + roundBoxes);
            qtSilentNotice('Round #' + totalRounds + ' done\nBoxes: ' + roundBoxes + '\nTotal: ' + totalBoxes + '\nEarned: $' + totalEarned);
            updateBotGui();

            setPlayerInvincible(true);
            if (points.length > 0) {
                const lastPt = points[points.length - 1];
                const player = getPlayer();
                const veh = getVehicle();
                try { mp.events.call('ac:refreshtp'); } catch (e) {}
                if (veh && player && player.vehicle) {
                    veh.setCoordsNoOffset(lastPt.x, lastPt.y, lastPt.z - 50, false, false, false);
                } else if (player) {
                    player.setCoordsNoOffset(lastPt.x, lastPt.y, lastPt.z - 50, false, false, false);
                }
                freezeVehicle(true);
            }

            if (orderMode === 'last' && botActive) {
                await preloadFastLastOrder();
            }

            await sleep(DELAY.AFTER_ALL_COLLECTED);
            await sleep(DELAY.ROUND_PAUSE);
            setPlayerInvincible(false);
        }
    }

    function startBot() {
        if (botActive) return;
        botActive = true;
        botStartTime = Date.now();
        boxesCollected = 0;
        fastLastOrderReady = false;
        tickLootPriceExpiry();
        const haveLoot =
            lootPrices.pearl != null ||
            lootPrices.starfish != null ||
            lootPrices.rapana != null;
        if (!haveLoot) {
            log('No junk seller prices — open Bradley (мотлох) before start for live pearl/star/rapana');
        } else if (lootPricesStale) {
            log('Junk seller prices stale — reopen seller to refresh');
        } else {
            log(
                'Loot prices OK: P=' + lootPriceFor('pearl') +
                ' S=' + lootPriceFor('starfish') +
                ' R=' + lootPriceFor('rapana')
            );
        }
        // Capture own submarine reference — survives eject, used for warp-back
        try {
            const p = getPlayer();
            const v = p && p.vehicle;
            if (v && mp.vehicles.exists(v)) {
                myVehicleRef = v;
                myVehicleModel = v.model;
                log('My submarine cached: handle=' + v.handle + ' model=0x' + v.model.toString(16) + ' remoteId=' + (v.remoteId || '?'));
            } else {
                myVehicleRef = null;
                myVehicleModel = 0;
                log('No vehicle at bot start — get into submarine first');
            }
        } catch (e) { log('Cache veh err: ' + e.message); }
        orderMode = orderMode || 'last';
        botVersion = 2;
        parkingEnabled = false;
        log('Starting Aquanaut v4 (last-order, surface)');
        qtSilentNotice('Bot STARTED v4\nOrder: last');
        startHpMonitor();
        botLoop().catch(e => { log('Error: ' + e); qtSilentNotice('ERROR: ' + e); stopBot(); });
    }

    function stopBot() {
        botActive = false;
        resetAirReloadState();
        fastLastOrderReady = false;
        if (zoneAnchor) { zoneAnchor = null; log('Zone anchor cleared (bot stopped)'); }
        stopHpMonitor();
        setPlayerInvincible(false);
        freezeVehicle(false);
        setVehicleCollision(true);
        stopEatInterval();
        // Eject player, TP to sub rental point, cancel rental
        try {
            teleportToRentalPoint();
            qtSetTimeout(() => { try { cancelSubRental(); } catch (e) {} }, 800);
        } catch (e) { log('stopBot rental cleanup err: ' + e.message); }
        if (invisActivated) {
            setServerInvis(false);
            invisActivated = false;
        }
        v2AdminOff();
        parkingEnabled = false;
        botPausedAdmin = false;
        const elapsed = botStartTime > 0 ? ((Date.now() - botStartTime) / 60000).toFixed(1) : '0';
        const earned = totalEarned + bonusEarned;
        log('Stopped | Boxes: ' + totalBoxes + ' | Earned: $' + earned);
        qtSilentNotice('Bot STOPPED v4\nBoxes: ' + totalBoxes + '\nEarned: $' + earned + '\nTime: ' + elapsed + ' min');
    }

    // ============================================================
    // EVENT HANDLERS
)QTJSX91"
        R"QTJSX91(    // ============================================================

    // -- GUI removed (external webapp only) --

    // -- Main tab --
    mp.events.add('qt:crawl', (val) => {
        if (val === '1') crawlStart(); else crawlStop();
    });
    mp.events.add('qt:setCrawlKey', (val) => {
        const newKey = parseInt(val);
        if (newKey === crawlKey) return;
        if (crawlKey) { try { mp.keys.unbind(crawlKey, true); } catch (e) {} }
        crawlKey = newKey;
        if (crawlKey) {
            mp.keys.bind(crawlKey, true, () => crawlToggle());
            log('Crawl key: 0x' + crawlKey.toString(16).toUpperCase());
        } else {
            log('Crawl key disabled');
        }
    });
    mp.events.add('qt:hitMute', (val) => {
        muteHitSound = val === '1';
        log(muteHitSound ? 'Hit sound MUTED' : 'Hit sound ON');
    });
    mp.events.add('qt:hitVol', (val) => {
        const pct = parseInt(val) || 0;
        hitSoundVolume = pct <= 0 ? 0 : (pct / 100) * 0.06;
        muteHitSound = pct <= 0;
    });
    mp.events.add('qt:hitUrl', (url) => {
        customHitSound = url || '';
    });
    mp.events.add('qt:autoBuy', (val) => {
        autoBuyMarket = val === '1';
        log(autoBuyMarket ? 'Auto Buy ON' : 'Auto Buy OFF');
    });
    mp.events.add('qt:marketListing', (jsonStr) => {
        if (!autoBuyMarket) return;
        try {
            const data = JSON.parse(jsonStr);
            const buyPayload = JSON.stringify({id:data.id,owner:data.owner,count:data.count,payType:data.payType,isDarknet:true});
            log('Auto buying: id=' + data.id);
            qtSetTimeout(() => { callRemote('buyMarketItems', buyPayload); }, 200);
        } catch (e) {}
    });

    // -- Bot tab --
    mp.events.add('qt:botToggle', () => {
        if (botActive) stopBot(); else startBot();
        updateBotGui();
    });
    mp.events.add('qt:loadAir', () => {
        if (!sessionToken) { log('No token!'); return; }
        callRemote("aquanautLoadVehicle", null);
        log('Loading air...');
    });
    mp.events.add('qt:tpStorage', () => {
        loadVehicleRemote();
    });
    mp.events.add('qt:botReset', () => {
        boxesCollected = 0;
        totalBoxes = 0;
        totalRounds = 0;
        totalEarned = 0;
        aquanautPoints = [];
        colshapesReady = false;
        logLines = [];
        currentStatus = 'Reset';
        log('Reset');
        updateBotGui();
    });
    mp.events.add('qt:autoToken', () => {
        log('Auto-token: intercepting M menu...');
        injectTokenHook();
    });
    mp.events.add('qt:botVersion', (ver) => {
        botVersion = 2;
        parkingEnabled = false;
        log('Bot version locked v2-style (v4)');
        updateBotGui();
    });
    mp.events.add('qt:listenFood', () => {
        listeningForFood = true;
        listeningForWater = false;
        log('Eat food in inventory to set...');
        injectUseItemHook();
    });
    mp.events.add('qt:listenWater', () => {
        listeningForWater = true;
        listeningForFood = false;
        log('Drink water in inventory to set...');
        injectUseItemHook();
    });
    mp.events.add('qt:detectedUseItem', (sqlId) => {
        if (listeningForFood) {
            foodSqlId = parseInt(sqlId) || sqlId;
            listeningForFood = false;
            log('Food sqlId: ' + foodSqlId);
        } else if (listeningForWater) {
            waterSqlId = parseInt(sqlId) || sqlId;
            listeningForWater = false;
            log('Water sqlId: ' + waterSqlId);
        }
        updateBotGui();
    });
    mp.events.add('qt:eat', () => {
        eatAndDrink();
    });

    // -- Weapons tab --
    mp.events.add('qt:wpnToggle', (compHash, val) => {
        const wh = mp.players.local.weapon;
        const ch = parseInt(compHash);
        const on = val === '1';
        toggleWeaponComp(wh, ch, on);
        const comp = WPN_COMPS.find(c => c.h === ch);
        log((on ? '+' : '-') + ' ' + (comp ? comp.n : ch));
    });
    mp.events.add('qt:wpnApplyAll', () => {
        const wh = mp.players.local.weapon;
        const comps = getCompatibleComps(wh);
        comps.forEach(c => toggleWeaponComp(wh, c.h, true));
        log('All attachments ON (' + comps.length + ')');
        updateWeaponGui();
    });
    mp.events.add('qt:wpnRemoveAll', () => {
        const wh = mp.players.local.weapon;
        const comps = getCompatibleComps(wh);
        comps.forEach(c => toggleWeaponComp(wh, c.h, false));
        weaponAttachments[String(wh >>> 0)] = {};
        log('All attachments OFF');
        updateWeaponGui();
    });
    mp.events.add('qt:wpnRefresh', () => {
        lastDetectedWeapon = 0;
        updateWeaponGui();
    });

    // -- Misc tab --
    mp.events.add('qt:buyPills', () => {
        callRemoteMulti("buyPills::ready", 0);
    });
    mp.events.add('qt:buyAid', (qty) => {
        const n = Math.max(1, Math.min(20, parseInt(qty) || 10));
        const payload = JSON.stringify({minValue:1,maxValue:20,text:"Вкажи кількість необхідних аптечок (1шт. - 10.000$)",title:"Покупка",method:"buyAid::ready",value:n});
        callRemoteMulti("buyAid::ready", payload);
    });
    mp.events.add('qt:die', () => {
        callRemoteMulti("gPlayerDeath", 3452007600, null);
    });
    // Amp bot: cycles by cooldown. count = -1 → infinite loop (works until stopped).
    // Pauses when player is in any vehicle and reports it to webapp.
    // delayMode: 'f' fixed ms (delayA), 'r' random delayA..delayB (ms, min 1000).
    const AMP_CYCLE_DELAY_MS = 1000;       // default between selectInBrothelDialogBox → win minigame
    const AMP_NEXT_CYCLE_DELAY_MS = 1000;  // default between minigame finish → next cycle
    const AMP_VEH_RECHECK_MS = 2000;       // re-check delay when blocked by vehicle
    mp.events.add('qt:ampBot', (val, sqlId, count, delayMode, delayA, delayB) => {
        if (val === '1') {
            const id = parseInt(sqlId) || 133;
            const total = parseInt(count);
            const loop = total === -1;
            let remaining = loop ? Infinity : (total || 10);
            let done = 0;
            let vehBlockedNotified = false;
            const mode = String(delayMode || 'f').toLowerCase();
            const fixedMs = Math.max(1000, parseInt(delayA) || AMP_NEXT_CYCLE_DELAY_MS);
            let randMin = Math.max(1000, parseInt(delayA) || 1000);
            let randMax = Math.max(randMin, parseInt(delayB) || randMin);
            function ampDelayMs() {
                if (mode === 'r') {
                    return randMin + Math.floor(Math.random() * (randMax - randMin + 1));
                }
                return fixedMs;
            }
            ampBotActive = true;
            log('Amp bot ON — sqlId=' + id + (loop ? ' loop' : ' count=' + remaining) +
                (mode === 'r' ? (' delay=' + randMin + '-' + randMax + 'ms') : (' delay=' + fixedMs + 'ms')));
            qtUiLog('Amp bot ON' + (loop ? ' (зациклено)' : ' x' + remaining));

            function isPlayerInVehicle() {
                try {
                    const p = mp.players.local;
                    if (!p || !p.handle) return false;
                    return !!p.vehicle || p.isInAnyVehicle(false);
                } catch (e) { return false; }
            }

            function doCycle() {
                if (!ampBotActive) { ampBotTimer = null; return; }
                if (remaining <= 0) {
                    ampBotActive = false;
                    ampBotTimer = null;
                    log('Amp bot finished — ' + done + ' cycles');
                    qtUiLog('Amp bot готов: ' + done + ' циклов');
                    return;
                }
                if (isPlayerInVehicle()) {
                    if (!vehBlockedNotified) {
                        log('Amp bot paused: выйди из авто');
                        qtUiLog('⚠ Amp bot: выйди из авто чтобы продолжить');
                        vehBlockedNotified = true;
                    }
                    ampBotTimer = qtSetTimeout(doCycle, AMP_VEH_RECHECK_MS);
                    return;
                }
                if (vehBlockedNotified) {
                    qtUiLog('Amp bot: продолжаю крафт');
                    vehBlockedNotified = false;
                }
                callRemote('selectInBrothelDialogBox', JSON.stringify({sqlId: id}));
                const stepDelay = ampDelayMs();
                ampBotTimer = qtSetTimeout(() => {
                    if (!ampBotActive) return;
                    callRemote('playerOnWinDrugsMinigame', true);
                    if (!loop) remaining--;
                    done++;
                    ampBotTimer = qtSetTimeout(doCycle, ampDelayMs());
                }, stepDelay);
            }
            doCycle();
        } else {
            ampBotActive = false;
            if (ampBotTimer) { qtClearTimeout(ampBotTimer); ampBotTimer = null; }
            log('Amp bot OFF');
            qtUiLog('Amp bot остановлен');
        }
    });

    mp.events.add('qt:matBot', (val, levelStr, amountStr) => {
        if (val === '1') {
            const level = Math.max(1, Math.min(5, parseInt(levelStr) || 1));
            const perTrip = [500, 750, 1000, 1250, 1500][level - 1];
            const totalWant = Math.max(perTrip, parseInt(amountStr) || perTrip);
            let tripsLeft = Math.ceil(totalWant / perTrip);
            let crafted = 0;
            matBotActive = true;
            log('Mat bot ON — lvl=' + level + ' perTrip=' + perTrip + ' total=' + totalWant + ' trips=' + tripsLeft);
            qtSilentNotice('Mat bot: lvl ' + level + ', ' + totalWant + ' мат (' + tripsLeft + ' ходок)');
            function doTrip() {
                if (!matBotActive || tripsLeft <= 0) {
                    matBotActive = false;
                    matBotTimer = null;
                    log('Mat bot finished — crafted ' + crafted);
                    qtSilentNotice('Mat bot готов: ' + crafted + ' материалов');
                    return;
                }
                qtSilentNotice('Mat: ходка ' + (Math.ceil(totalWant / perTrip) - tripsLeft + 1) + '/' + Math.ceil(totalWant / perTrip));
                // Step 1: criminalBuyComponents
                callRemote('criminalBuyComponents', null);
                matBotTimer = qtSetTimeout(() => {
                    if (!matBotActive) return;
                    // Step 2: criminalRecyclingFinishedComponents
                    callRemote('criminalRecyclingFinishedComponents', true);
                    matBotTimer = qtSetTimeout(() => {
                        if (!matBotActive) return;
                        // Step 3: criminalReceivingFinishedComponents::ready
                        callRemoteMulti('criminalReceivingFinishedComponents::ready', 0);
                        tripsLeft--;
                        crafted += perTrip;
                        // Step 4: wait 57s then playerOnBasementAccess → next trip
)QTJSX91"
        R"QTJSX91(                        matBotTimer = qtSetTimeout(() => {
                            if (!matBotActive) return;
                            callRemote('playerOnBasementAccess', null);
                            matBotTimer = qtSetTimeout(doTrip, 1000);
                        }, 57000);
                    }, 1000);
                }, 1000);
            }
            doTrip();
        } else {
            matBotActive = false;
            if (matBotTimer) { qtClearTimeout(matBotTimer); matBotTimer = null; }
            log('Mat bot OFF');
            qtSilentNotice('Mat bot остановлен');
        }
    });

    // ============================================================
    // CHAT BINDER — Radial Wheel Menu (8 sectors)
    // ============================================================
    let chatSending = false;
    let binderSlots = [null,null,null,null,null,null,null,null]; // 8 slots, each {label,messages:[{text,delay}]}
    let radialBrowser = null;
    let radialActive = false;
    let radialSelected = -1;
    let radialKey = 0x14; // CapsLock default
    let radialScale = 1.0;
    let radialCenterFrames = 0;
    let chatBlockCursor = false;
    const SLOT_COUNT = 8;

    var _origCursorShow = mp.gui.cursor.show.bind(mp.gui.cursor);
    function sendChat(msg, doSend, callback) {
        // Back-compat: if 2nd arg is a function, treat as (msg, callback) with doSend=true
        if (typeof doSend === 'function') { callback = doSend; doSend = true; }
        if (doSend === undefined) doSend = true;
        chatSending = true;
        chatBlockCursor = true;
        mp.events.call("displayChatHandler", true);
        qtSetTimeout(function() {
            try {
                mp.browsers.forEach(function(b) {
                    b.execute(
                        '(function(){' +
                        'var input = document.querySelector("input.chat-enter") || document.querySelector("#Chat input");' +
                        'if(!input) return;' +
                        'var ns = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set;' +
                        'ns.call(input, ' + JSON.stringify(msg) + ');' +
                        'input.dispatchEvent(new Event("input", {bubbles:true}));' +
                        (doSend ? 'input.dispatchEvent(new KeyboardEvent("keydown",{key:"Enter",code:"Enter",keyCode:13,which:13,bubbles:true,cancelable:true}));' : '') +
                        '})()'  
                    );
                });
            } catch(e) {}
            qtSetTimeout(function() {
                if (doSend) mp.events.call("displayChatHandler", false);
                chatBlockCursor = false;
                _origCursorShow(false, false);
                chatSending = false;
                if (callback) callback();
            }, 100);
        }, 150);
    }

    function sendChatSequence(messages) {
        if (!messages || !messages.length || chatSending) return;
        var idx = 0;
        function next() {
            if (idx >= messages.length) return;
            var m = messages[idx];
            var delay = idx === 0 ? 0 : Math.max(350, m.delay || 500);
            idx++;
            qtSetTimeout(function() { sendChat(m.text, next); }, delay);
        }
        next();
    }

    function executeActions(actions) {
        if (!actions || !actions.length || chatSending) return;
        var idx = 0;
        function next() {
            if (idx >= actions.length) return;
            var a = actions[idx];
            idx++;
            if (a.type === 'pause') {
                qtSetTimeout(next, Math.max(0, parseInt(a.value) || 500));
            } else if (a.type === 'text') {
                // autoSend per-action: default true (press Enter). If false — just paste without Enter
                var doSend = a.autoSend !== false;
                sendChat(a.value, doSend, next);
            } else {
                next();
            }
        }
        next();
    }

    const RADIAL_HTML = '<!DOCTYPE html><html><head><style>' +
        '*{margin:0;padding:0}' +
        'body{background:rgba(0,0,0,0.35);overflow:hidden;width:100vw;height:100vh;' +
        'font-family:Segoe UI,sans-serif}' +
        'svg{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%)}' +
        '#desc{position:fixed;left:50%;top:calc(50% + 75px);transform:translateX(-50%);' +
        'text-align:center;pointer-events:none;z-index:10}' +
        '#dlbl{color:#fff;font-size:16px;font-weight:700;text-transform:uppercase;' +
        'letter-spacing:1.5px;text-shadow:0 2px 8px rgba(0,0,0,0.9)}' +
        '#dmsg{color:rgba(255,255,255,0.45);font-size:12px;margin-top:4px;' +
        'text-shadow:0 1px 4px rgba(0,0,0,0.9);max-width:250px}' +
        '</style></head><body>' +
        '<svg id="w" width="700" height="700" viewBox="0 0 700 700"></svg>' +
        '<div id="desc"><div id="dlbl"></div><div id="dmsg"></div></div>' +
        '<script>' +
        'var items=[],sel=-1,N=8,CX=350,CY=350,RO=300,RI=115,RC=58,sc=1;' +
        'function setScale(s){sc=s;var svg=document.getElementById("w");svg.style.transform="translate(-50%,-50%) scale("+sc+")"}' +
        'function setItems(a){items=a;while(items.length<N)items.push(null);render()}' +
        'function sp(a1,a2,r1,r2){' +
        'var c=Math.cos,s=Math.sin;' +
        'return"M"+(CX+r1*c(a1))+","+(CY+r1*s(a1))' +
        '+" L"+(CX+r2*c(a1))+","+(CY+r2*s(a1))' +
        '+" A"+r2+","+r2+" 0 0,1 "+(CX+r2*c(a2))+","+(CY+r2*s(a2))' +
        '+" L"+(CX+r1*c(a2))+","+(CY+r1*s(a2))' +
        '+" A"+r1+","+r1+" 0 0,0 "+(CX+r1*c(a1))+","+(CY+r1*s(a1))+"Z"}' +
        'function render(){' +
        'var svg=document.getElementById("w");svg.innerHTML="";' +
        'var st=Math.PI*2/N,off=-Math.PI/2-st/2;' +
        'for(var i=0;i<N;i++){' +
        'var a1=off+i*st,a2=a1+st;' +
        'var p=document.createElementNS("http://www.w3.org/2000/svg","path");' +
        'p.setAttribute("d",sp(a1,a2,RI+2,RO));' +
        'p.setAttribute("fill",items[i]?"rgba(45,45,60,0.6)":"rgba(25,25,35,0.45)");' +
        'p.setAttribute("stroke","rgba(255,255,255,0.08)");p.setAttribute("stroke-width","1.5");' +
        'p.id="s"+i;svg.appendChild(p);' +
        'var ma=(a1+a2)/2,lr=(RI+RO)/2;' +
        'var t=document.createElementNS("http://www.w3.org/2000/svg","text");' +
        't.setAttribute("x",CX+lr*Math.cos(ma));' +
        't.setAttribute("y",CY+lr*Math.sin(ma));' +
        't.setAttribute("fill",items[i]?"rgba(255,255,255,0.85)":"rgba(255,255,255,0.15)");' +
        't.setAttribute("font-size","14");' +
        't.setAttribute("font-weight","700");t.setAttribute("font-family","Segoe UI,sans-serif");' +
        't.setAttribute("text-anchor","middle");t.setAttribute("dominant-baseline","middle");' +
        't.setAttribute("pointer-events","none");t.id="t"+i;' +
        't.textContent=items[i]?items[i].label:"";svg.appendChild(t)}' +
        'var ir=document.createElementNS("http://www.w3.org/2000/svg","circle");' +
        'ir.setAttribute("cx",CX);ir.setAttribute("cy",CY);ir.setAttribute("r",RI);' +
        'ir.setAttribute("fill","rgba(20,20,30,0.7)");' +
        'ir.setAttribute("stroke","rgba(255,255,255,0.06)");ir.setAttribute("stroke-width","1");' +
        'svg.appendChild(ir);' +
        'var cc=document.createElementNS("http://www.w3.org/2000/svg","circle");' +
        'cc.setAttribute("cx",CX);cc.setAttribute("cy",CY);cc.setAttribute("r",RC);' +
        'cc.setAttribute("fill","rgba(12,12,18,0.92)");' +
        'cc.setAttribute("stroke","rgba(255,255,255,0.18)");cc.setAttribute("stroke-width","2.5");' +
        'svg.appendChild(cc);' +
        'var ct=document.createElementNS("http://www.w3.org/2000/svg","text");' +
        'ct.setAttribute("x",CX);ct.setAttribute("y",CY);' +
        'ct.setAttribute("fill","rgba(255,255,255,0.6)");ct.setAttribute("font-size","13");' +
        'ct.setAttribute("font-weight","700");ct.setAttribute("font-family","Segoe UI,sans-serif");' +
        'ct.setAttribute("text-anchor","middle");ct.setAttribute("dominant-baseline","middle");' +
        'ct.setAttribute("letter-spacing","2");' +
        'ct.textContent="STOP";ct.id="ctxt";svg.appendChild(ct)}' +
        'document.addEventListener("mousemove",function(e){' +
        'var cx=innerWidth/2,cy=innerHeight/2,dx=e.clientX-cx,dy=e.clientY-cy;' +
        'var dist=Math.sqrt(dx*dx+dy*dy);' +
        'var svg=document.getElementById("w"),rect=svg.getBoundingClientRect(),sc=rect.width/700;' +
        'var prev=sel;' +
        'if(dist<RC*sc){sel=-2}' +
        'else if(dist<RI*sc){sel=-1}' +
        'else{var st=Math.PI*2/N,off=-Math.PI/2-st/2;' +
        'var ang=Math.atan2(dy,dx),rel=ang-off;if(rel<0)rel+=Math.PI*2;' +
        'sel=Math.floor(rel/st)%N;if(!items[sel])sel=-1}' +
        'if(sel!==prev){' +
        'for(var i=0;i<N;i++){' +
        'var p=document.getElementById("s"+i),t=document.getElementById("t"+i);if(!p)continue;' +
        'if(i===sel){' +
        'p.setAttribute("fill","rgba(220,45,75,0.35)");' +
        'p.setAttribute("stroke","rgba(220,45,75,0.6)");' +
        'p.setAttribute("stroke-width","2.5");' +
        'if(t)t.setAttribute("fill","#fff")}' +
        'else{' +
        'p.setAttribute("fill",items[i]?"rgba(45,45,60,0.6)":"rgba(25,25,35,0.45)");' +
        'p.setAttribute("stroke","rgba(255,255,255,0.08)");' +
        'p.setAttribute("stroke-width","1.5");' +
        'if(t)t.setAttribute("fill",items[i]?"rgba(255,255,255,0.85)":"rgba(255,255,255,0.15)")}}' +
        'var dl=document.getElementById("dlbl"),dm=document.getElementById("dmsg");' +
        'if(sel>=0&&items[sel]){dl.textContent=items[sel].label;' +
        'dm.textContent=items[sel].messages&&items[sel].messages[0]?items[sel].messages[0].text:""}' +
        'else{dl.textContent="";dm.textContent=""}' +
        'try{mp.trigger("qt:radialHover",String(sel))}catch(x){}}});' +
        'render()' +
        '<\/script></body></html>';

    function createRadialMenu() {
        if (radialBrowser) return;
        radialBrowser = mp.browsers.new('data:text/html,' + encodeURIComponent(RADIAL_HTML));
        radialBrowser.active = false;
        log('Radial menu created');
    }

    function showRadialMenu() {
        if (!radialBrowser || radialActive) return;
        var hasAny = binderSlots.some(function(s) { return s !== null; });
        if (!hasAny) return;
        radialActive = true;
        radialSelected = -1;
        radialBrowser.execute('setItems(' + JSON.stringify(binderSlots) + ');setScale(' + radialScale + ')');
        radialBrowser.active = true;
        _origCursorShow(false, true);
        radialCenterFrames = 10;
    }

    function hideRadialMenu() {
        if (!radialActive) return;
        radialActive = false;
        radialCenterFrames = 0;
        if (radialBrowser) radialBrowser.active = false;
        _origCursorShow(false, false);
)QTJSX91"
        R"QTJSX91(        if (radialSelected >= 0 && radialSelected < SLOT_COUNT && binderSlots[radialSelected]) {
            var slot = binderSlots[radialSelected];
            log('Binder[' + radialSelected + ']: ' + slot.label);
            if (slot.actions) {
                executeActions(slot.actions);
            } else if (slot.messages) {
                sendChatSequence(slot.messages);
            }
        }
        radialSelected = -1;
    }

    function destroyRadialMenu() {
        if (radialBrowser) {
            try { radialBrowser.active = false; radialBrowser.destroy(); } catch(e) {}
            radialBrowser = null;
        }
        radialActive = false;
    }

    var radialMousePoll = false;
    var radialMouseWasDown = false;

    function bindRadialKey(newKey) {
        if (!radialMousePoll) {
            try { mp.keys.unbind(radialKey, true); } catch(e) {}
            try { mp.keys.unbind(radialKey, false); } catch(e) {}
        }
        radialKey = newKey;
        radialMousePoll = (radialKey === 5 || radialKey === 6);
        radialMouseWasDown = false;
        if (!radialMousePoll) {
            mp.keys.bind(radialKey, true, function() { showRadialMenu(); });
            mp.keys.bind(radialKey, false, function() { hideRadialMenu(); });
        }
        log('Radial key: 0x' + radialKey.toString(16).toUpperCase() + (radialMousePoll ? ' (mouse poll)' : ''));
    }

    mp.events.add('qt:radialHover', function(idx) { radialSelected = parseInt(idx); });
    mp.events.add('qt:chatTest', function(msg) { sendChat(msg); });

    var _screenCX = 0, _screenCY = 0;
    try { var _sr = mp.game.graphics.getScreenActiveResolution(0, 0); _screenCX = _sr.x / 2; _screenCY = _sr.y / 2; } catch(e) { _screenCX = 960; _screenCY = 540; }

    mp.events.add('render', function() {
        // Force-hide cursor every frame during chat send
        if (chatBlockCursor) {
            _origCursorShow(false, false);
        }
        // Force cursor to center for N frames after radial menu opens
        if (radialCenterFrames > 0) {
            radialCenterFrames--;
            try { mp.game.invoke('0xFC695459D4D0E219', 0.5, 0.5); } catch(e) {}
        }
        // Mouse side button polling for radial menu
        if (radialMousePoll) {
            var down = mp.keys.isDown(radialKey);
            if (down && !radialMouseWasDown) { showRadialMenu(); }
            if (!down && radialMouseWasDown) { hideRadialMenu(); }
            radialMouseWasDown = down;
        }
        // Crawl recovery: force-enable controls for N frames after crawl ends
        if (crawlRecovery > 0) {
            crawlRecovery--;
            var ctrl = mp.game.controls;
            ctrl.enableControlAction(0, 21, true);
            ctrl.enableControlAction(0, 24, true);
            ctrl.enableControlAction(0, 25, true);
            ctrl.enableControlAction(0, 22, true);
            ctrl.enableControlAction(0, 44, true);
        }
        // Mouse side button polling for crawl
        if (crawlMousePoll && crawlKey) {
            var cd = mp.keys.isDown(crawlKey);
            if (cd && !crawlMouseWasDown) { crawlToggle(); }
            crawlMouseWasDown = cd;
        }
        // Helicopter stabilization
        if (heliStabEnabled) {
            try {
                var veh = mp.players.local.vehicle;
                if (veh && veh.getClass() === 15) {
                    // Remove turbulence (always — doesn't interfere with input)
                    mp.game.invoke('0xE6F13851780394DA', veh.handle, 0.0);
                    // Check if player is actively steering (LMB held = mouse flight control)
                    var steering = mp.keys.isDown(0x01); // VK_LBUTTON
                    if (steering) {
                        // Turn BOOST: read raw mouse movement and apply torque directly
                        // Controls 1/2 = mouse X/Y (-1..1 per frame based on mouse delta)
                        // Use GET_DISABLED_CONTROL_NORMAL so it works even if game consumes the input
                        var lookX = mp.game.invoke('0x11E65974A982637C', 0, 1); // INPUT_LOOK_LR
                        var lookY = mp.game.invoke('0x11E65974A982637C', 0, 2); // INPUT_LOOK_UD
                        if (Math.abs(lookX) > 0.005 || Math.abs(lookY) > 0.005) {
                            var angVel = mp.game.invoke('0x213B91045D09B983', veh.handle); // GET_ENTITY_ROTATION_VELOCITY
                            if (angVel) {
                                var strength = 0.45, cap = 5.0;
                                // Mouse Y → pitch (nose up/down)
                                // Mouse X → yaw (turn) + gentle roll (bank into turn)
                                var nx = Math.max(-cap, Math.min(cap, angVel.x - lookY * strength));
                                var ny = Math.max(-cap, Math.min(cap, angVel.y - lookX * strength * 0.6));
                                var nz = Math.max(-cap, Math.min(cap, angVel.z - lookX * strength));
                                mp.game.invoke('0x8339643499D1222E', veh.handle, nx, ny, nz); // SET_ENTITY_ANGULAR_VELOCITY
                            }
                        }
                    } else {
                        var rot = veh.getRotation(2);
                        var newPitch = rot.x, newRoll = rot.y, needUpdate = false;
                        // Landing assist: if low altitude + slow, aggressively level pitch+roll
                        var alt = mp.game.invoke('0x1DD55701034110E5', veh.handle); // GET_ENTITY_HEIGHT_ABOVE_GROUND
                        var spd = mp.game.invoke('0xD5037BA82E12416F', veh.handle); // GET_ENTITY_SPEED
                        var landing = (alt > 0 && alt < 15 && spd < 8);
                        if (landing) {
                            if (Math.abs(rot.x) > 0.3) { newPitch = rot.x * 0.80; needUpdate = true; }
                            if (Math.abs(rot.y) > 0.3) { newRoll = rot.y * 0.80; needUpdate = true; }
                        } else if (Math.abs(rot.y) > 0.5) {
                            newRoll = rot.y * heliStabDamping; needUpdate = true;
                        }
                        if (needUpdate) veh.setRotation(newPitch, newRoll, rot.z, 2, true);
                    }
                }
            } catch(e) {}
        }
        // Mouse side button polling for standup
        if (standupMousePoll && standupKey) {
            var sd = mp.keys.isDown(standupKey);
            if (sd && !standupMouseWasDown) { standUpFromKnockdown(); }
            standupMouseWasDown = sd;
        }
        // Mouse side button polling for cheatsheet
        if (csMousePoll && cheatSheetKey) {
            var csd = mp.keys.isDown(cheatSheetKey);
            if (csd && !csMouseWasDown) { toggleCheatSheet(); }
            csMouseWasDown = csd;
        }
        if (marijuanaMousePoll && marijuanaKey) {
            var md = mp.keys.isDown(marijuanaKey);
            if (md && !marijuanaMouseWasDown) { openNearestMarijuanaMenu(); }
            marijuanaMouseWasDown = md;
        }
    });

    // Initial key bind
    if (radialKey === 5 || radialKey === 6) {
        radialMousePoll = true;
    } else {
        mp.keys.bind(radialKey, true, function() { showRadialMenu(); });
        mp.keys.bind(radialKey, false, function() { hideRadialMenu(); });
    }

    // Create radial browser after a short delay
    qtSetTimeout(createRadialMenu, 3000);

    // ============================================================
    // KEY BINDINGS
    // ============================================================
    mp.keys.bind(crawlKey, true, () => crawlToggle()); // Q = crawl
    // F2 menu removed - use external webapp
    startWeaponCheck();

    // ============================================================
    // MISC FEATURES (native bridge from C++ MiscFeatures)
    // ============================================================
    let miscState = {
        god: false,
        noSpread: 0, noRecoil: 0, reload: 0,
        noclip: false, noclipSpeed: 1.5,
        freecam: false, freecamSpeed: 1.5, freecamId: -1,
        freecamYaw: 0, freecamPitch: 0,
        fovOn: false, fov: 50,
        aspectOn: false, aspect: 1.777,
        seatbelt: false,
        carCol: false, pedCol: false,
        fixedHp: false, fixedHpVal: 100,
        fixedAp: false, fixedApVal: 100
    };

    function miscDestroyFreecam() {
        try {
            if (miscState._cam) {
                try { miscState._cam.setActive(false); } catch (e) {}
                try { miscState._cam.destroy(true); } catch (e) {
                    try { miscState._cam.destroy(); } catch (e2) {}
                }
            }
        } catch (e) {}
        miscState._cam = null;
        miscState.freecamId = -1;
        try { mp.game.cam.renderScriptCams(false, false, 0, true, false, 0); } catch (e) {}
        try {
            const p = mp.players.local;
            if (p) {
                p.freezePosition(false);
                try { p.setCollision(true, true); } catch (e) {}
            }
        } catch (e) {}
    }

    function miscCreateFreecam() {
        miscDestroyFreecam();
        try {
            const p = mp.players.local;
            if (!p) return;
            p.freezePosition(true);
            try { p.setVelocity(0, 0, 0); } catch (e) {}
            const pos = p.position;
            let rot = { x: 0, y: 0, z: 0 };
            try { rot = mp.game.cam.getGameplayCamRot(2); } catch (e) {}
            miscState.freecamPitch = rot.x || 0;
            miscState.freecamYaw = rot.z || 0;
            const c = mp.cameras.new(
                'default',
                new mp.Vector3(pos.x, pos.y, pos.z + 0.6),
                new mp.Vector3(miscState.freecamPitch, 0, miscState.freecamYaw),
                50);
            c.setActive(true);
            mp.game.cam.renderScriptCams(true, false, 0, true, false, 0);
            miscState._cam = c;
            miscState.freecamId = 1;
        } catch (e) {
            miscDestroyFreecam();
        }
    }

    function miscParseTriplet(cmd, prefix) {
        const rest = cmd.substring(prefix.length);
        const parts = rest.split(':');
        const on = parseInt(parts[0], 10) === 1;
        const val = parseFloat(parts[1]);
        return { on: on, val: isNaN(val) ? 0 : val };
    }

    function miscHandleCommand(cmd) {
        if (cmd === 'misc_suicide') {
            try {
                const ped = mp.players.local.handle;
                mp.game.invoke('0x6B76DC1F3AE6E6A3', ped, 0, 0);
                mp.game.invoke('0x697157CED63F18D4', ped, 10000, true, true);
            } catch (e) {}
            return true;
        }
        if (cmd === 'misc_reset_anim') {
            try {
                const ped = mp.players.local.handle;
                mp.game.invoke('0xE1EF3C1216AFF2CD', ped);
                mp.game.invoke('0xAAA34F8A7CB32098', ped);
            } catch (e) {}
)QTJSX91"
        R"QTJSX91(            return true;
        }
        if (cmd === 'misc_heal') {
            try { mp.game.invoke('0x6B76DC1F3AE6E6A3', mp.players.local.handle, 200, 0); } catch (e) {}
            return true;
        }
        if (cmd === 'misc_armor') {
            try { mp.players.local.setArmour(100); } catch (e) {
                try { mp.game.invoke('0xCEA04D83135264CC', mp.players.local.handle, 100); } catch (e2) {}
            }
            return true;
        }
        if (cmd === 'misc_respawn') {
            try {
                callRemote('setGodmode', false);
                qtSetTimeout(function(){ callRemote('server::selfRevive', null); }, 150);
            } catch (e) {}
            return true;
        }
        if (cmd === 'misc_repair') {
            try {
                const p = mp.players && mp.players.local;
                const v = p && p.vehicle;
                if (!v || !v.handle) return true;
                if (typeof v.repair === 'function') {
                    try {
                        v.repair();
                        return true;
                    } catch (e) {}
                }
                // Guarded fallback for RAGE builds without vehicle.repair().
                try {
                    mp.game.invoke('0x115722B1B9C14C1C', v.handle);
                } catch (e) {}
            } catch (e) {}
            return true;
        }
        if (cmd === 'misc_god:on') { miscState.god = true; setPlayerInvincible(true); return true; }
        if (cmd === 'misc_god:off') { miscState.god = false; if (!botActive) setPlayerInvincible(false); return true; }
        if (cmd === 'misc_seatbelt:on') { miscState.seatbelt = true; return true; }
        if (cmd === 'misc_seatbelt:off') { miscState.seatbelt = false; return true; }
        if (cmd === 'misc_carcol:on') { miscState.carCol = true; return true; }
        if (cmd === 'misc_carcol:off') { miscState.carCol = false; return true; }
        if (cmd === 'misc_pedcol:on') { miscState.pedCol = true; return true; }
        if (cmd === 'misc_pedcol:off') { miscState.pedCol = false; return true; }
        if (cmd.startsWith('misc_nospread:')) {
            const p = miscParseTriplet(cmd, 'misc_nospread:');
            miscState.noSpread = p.on ? Math.max(0, Math.min(100, p.val)) : 0;
            return true;
        }
        if (cmd.startsWith('misc_norecoil:')) {
            const p = miscParseTriplet(cmd, 'misc_norecoil:');
            miscState.noRecoil = p.on ? Math.max(0, Math.min(100, p.val)) : 0;
            return true;
        }
        if (cmd.startsWith('misc_reload:')) {
            const p = miscParseTriplet(cmd, 'misc_reload:');
            miscState.reload = p.on ? Math.max(0, Math.min(100, p.val)) : 0;
            return true;
        }
        if (cmd.startsWith('misc_noclip:')) {
            const p = miscParseTriplet(cmd, 'misc_noclip:');
            miscState.noclip = p.on;
            miscState.noclipSpeed = Math.max(0.2, Math.min(20, p.val || 1.5));
            try { mp.players.local.setCollision(!p.on, !p.on); } catch (e) {}
            try { mp.players.local.freezePosition(p.on); } catch (e) {}
            return true;
        }
        if (cmd.startsWith('misc_freecam:')) {
            const p = miscParseTriplet(cmd, 'misc_freecam:');
            miscState.freecamSpeed = Math.max(0.2, Math.min(20, p.val || 1.5));
            if (p.on && !miscState.freecam) {
                miscState.freecam = true;
                miscCreateFreecam();
            } else if (!p.on && miscState.freecam) {
                miscState.freecam = false;
                miscDestroyFreecam();
            } else {
                miscState.freecam = p.on;
            }
            return true;
        }
        if (cmd.startsWith('misc_gravity:')) {
            // Retired compatibility command. C++ VehicleMovement is the sole
            // owner of vehicle power, torque and downforce.
            return true;
        }
        // FOV / aspect are applied in C++ via GTA5.exe memory — ignore JS cmds.
        if (cmd.startsWith('misc_fov:') || cmd.startsWith('misc_aspect:')) {
            return true;
        }
        if (cmd.startsWith('misc_fixedhp:')) {
            const p = miscParseTriplet(cmd, 'misc_fixedhp:');
            miscState.fixedHp = p.on;
            miscState.fixedHpVal = Math.max(1, Math.min(100, p.val || 100));
            return true;
        }
        if (cmd.startsWith('misc_fixedap:')) {
            const p = miscParseTriplet(cmd, 'misc_fixedap:');
            miscState.fixedAp = p.on;
            miscState.fixedApVal = Math.max(0, Math.min(100, p.val || 100));
            return true;
        }
        return false;
    }

    function miscDirsFromYawPitch(yawDeg, pitchDeg) {
        const pitch = pitchDeg * Math.PI / 180;
        const yaw = yawDeg * Math.PI / 180;
        const forward = new mp.Vector3(
            -Math.sin(yaw) * Math.cos(pitch),
            Math.cos(yaw) * Math.cos(pitch),
            Math.sin(pitch));
        const right = new mp.Vector3(Math.cos(yaw), Math.sin(yaw), 0);
        return { forward: forward, right: right };
    }

    mp.events.add('render', function miscRenderTick() {
        if (!qtAlive) return;
        if (!miscState.god && !miscState.seatbelt && !miscState.carCol && !miscState.pedCol && !miscState.fixedHp && !miscState.fixedAp && !miscState.noclip && !miscState.freecam) return;
        try {
            const p = mp.players.local;
            if (!p) return;
            if (miscState.god) { try { p.setInvincible(true); } catch (e) {} }
            if (miscState.seatbelt && p.vehicle) {
                try { mp.game.invoke('0x1913FE4CBF41C463', p.handle, 32, true); } catch (e) {}
                try { mp.game.invoke('0x75D3F1A73B0E4D9C', p.handle, false); } catch (e) {}
                try { p.setConfigFlag(32, true); } catch (e) {}
            }
            if (miscState.carCol) {
                try {
                    const v = p.vehicle;
                    if (v) {
                        v.setCollision(false, false);
                        try { mp.game.invoke('0x1A9205C1B9EE827F', v.handle, false, false); } catch (e) {}
                    }
                } catch (e) {}
            }
            if (miscState.pedCol) {
                try { p.setCollision(false, false); } catch (e) {}
                try { mp.game.invoke('0x1A9205C1B9EE827F', p.handle, false, false); } catch (e) {}
            }
            if (miscState.fixedHp) {
                try { mp.game.invoke('0x6B76DC1F3AE6E6A3', p.handle, 100 + miscState.fixedHpVal, 0); } catch (e) {}
            }
            if (miscState.fixedAp) { try { p.setArmour(miscState.fixedApVal); } catch (e) {} }

            if (miscState.noclip || miscState.freecam) {
                try {
                    p.freezePosition(true);
                    p.setVelocity(0, 0, 0);
                    // Block movement / attack while freecam/noclip.
                    for (let i = 0; i < 350; i++) {
                        try { mp.game.controls.disableControlAction(0, i, true); } catch (e) {}
                    }
                } catch (e) {}
            }

            const fast = mp.keys.isDown(0x10) ? 2.5 : 1.0;

            if (miscState.noclip && !miscState.freecam) {
                try {
                    p.setCollision(false, false);
                    let rot = { x: 0, y: 0, z: 0 };
                    try { rot = mp.game.cam.getGameplayCamRot(2); } catch (e) {}
                    const dirs = miscDirsFromYawPitch(rot.z || 0, rot.x || 0);
                    const spd = miscState.noclipSpeed * fast * 0.35;
                    let pos = p.position;
                    if (mp.keys.isDown(0x57)) pos = new mp.Vector3(pos.x + dirs.forward.x * spd, pos.y + dirs.forward.y * spd, pos.z + dirs.forward.z * spd);
                    if (mp.keys.isDown(0x53)) pos = new mp.Vector3(pos.x - dirs.forward.x * spd, pos.y - dirs.forward.y * spd, pos.z - dirs.forward.z * spd);
                    if (mp.keys.isDown(0x41)) pos = new mp.Vector3(pos.x - dirs.right.x * spd, pos.y - dirs.right.y * spd, pos.z);
                    if (mp.keys.isDown(0x44)) pos = new mp.Vector3(pos.x + dirs.right.x * spd, pos.y + dirs.right.y * spd, pos.z);
                    if (mp.keys.isDown(0x20)) pos = new mp.Vector3(pos.x, pos.y, pos.z + spd);
                    if (mp.keys.isDown(0x11)) pos = new mp.Vector3(pos.x, pos.y, pos.z - spd);
                    p.setCoordsNoOffset(pos.x, pos.y, pos.z, false, false, false);
                } catch (e) {}
            }

            if (miscState.freecam) {
                try {
                    if (!miscState._cam) miscCreateFreecam();
                    if (!miscState._cam) return;
                    // Mouse look via look axes (works with disabled move controls).
                    let lookX = 0, lookY = 0;
                    try { lookX = mp.game.controls.getDisabledControlNormal(0, 1); } catch (e) {}
                    try { lookY = mp.game.controls.getDisabledControlNormal(0, 2); } catch (e) {}
                    miscState.freecamYaw -= lookX * 8.0;
                    miscState.freecamPitch -= lookY * 8.0;
                    if (miscState.freecamPitch > 89) miscState.freecamPitch = 89;
                    if (miscState.freecamPitch < -89) miscState.freecamPitch = -89;
                    const dirs = miscDirsFromYawPitch(miscState.freecamYaw, miscState.freecamPitch);
                    const spd = miscState.freecamSpeed * fast * 0.45;
                    let pos = miscState._cam.getCoord();
                    if (mp.keys.isDown(0x57)) pos = new mp.Vector3(pos.x + dirs.forward.x * spd, pos.y + dirs.forward.y * spd, pos.z + dirs.forward.z * spd);
                    if (mp.keys.isDown(0x53)) pos = new mp.Vector3(pos.x - dirs.forward.x * spd, pos.y - dirs.forward.y * spd, pos.z - dirs.forward.z * spd);
                    if (mp.keys.isDown(0x41)) pos = new mp.Vector3(pos.x - dirs.right.x * spd, pos.y - dirs.right.y * spd, pos.z);
                    if (mp.keys.isDown(0x44)) pos = new mp.Vector3(pos.x + dirs.right.x * spd, pos.y + dirs.right.y * spd, pos.z);
                    if (mp.keys.isDown(0x20)) pos = new mp.Vector3(pos.x, pos.y, pos.z + spd);
                    if (mp.keys.isDown(0x11)) pos = new mp.Vector3(pos.x, pos.y, pos.z - spd);
                    miscState._cam.setCoord(pos.x, pos.y, pos.z);
                    miscState._cam.setRot(miscState.freecamPitch, 0, miscState.freecamYaw, 2);
                    // Keep ped frozen at last spot — no walking.
                    p.freezePosition(true);
                    try { p.setVelocity(0, 0, 0); } catch (e) {}
                } catch (e) {}
            }
        } catch (e) {}
    });

    // ============================================================
    // COMPANY TRUCKER BOT (trackTablet CEF API)
    // Filters: weightTab / selectedTypes / sortedBy / onlyFree
    // Flow: eat+drink → getCompanyOrders → companyGetOrder
    //   → TP z-50 under trailer → companyTrailerVehicle.success
)QTJSX91"
        R"QTJSX91(    //   → TP z-50 under delivery → companySuccessfulDeliveryTrailerPay
    // ============================================================
    let companyBotActive = false;
    let companyOrders = [];
    let companyStatus = 'Idle';
    let companyTrailerPoints = [];
    let companyDeliveryPlace = null;
    let companyDeliveries = 0;
    let companyLoopRunning = false;
    try {
        mp.events.add('setCompanyInfo', function (raw) {
            try {
                const obj = typeof raw === 'string' ? JSON.parse(raw) : raw;
                const ranks = (obj && (obj.ranks || obj.ranksData)) || null;
                if (!Array.isArray(ranks)) return;
                ranks.forEach(function (r) {
                    companyLog(
                        'rank#' + r.id + ' ' + (r.name || '')
                        + ' percentFromOrder=' + r.percentFromOrder);
                });
            } catch (e) {}
        });
    } catch (e) {}
    let companyFilterWeight = 'all'; // all|light|medium|heavy
    let companyFilterSort = 'price'; // price|distance|weight
    let companyFilterOnlyFree = true;
    let companyFilterTypes = [
        'tools', 'dangerous', 'fuel', 'food', 'household', 'recycling',
        'explosive', 'radioactive', 'corrosive', 'gas', 'wood', 'cars'
    ];
    const COMPANY_ALL_TYPES = [
        'tools', 'dangerous', 'fuel', 'food', 'household', 'recycling',
        'explosive', 'radioactive', 'corrosive', 'gas', 'wood', 'cars'
    ];
    const COMPANY_WEIGHT_TABS = ['light', 'medium', 'heavy'];
    const COMPANY_UNDER_Z = 50;

    function companyLog(msg) {
        log('[Firma] ' + msg);
        qtUiLog('Firma: ' + msg);
        companyStatus = msg;
    }

    function companyPosOf(obj) {
        if (!obj) return null;
        if (obj.position && obj.position.x != null) return obj.position;
        if (obj.x != null && obj.y != null) return obj;
        return null;
    }

    function companyNormalizeOrders(data) {
        const list = (data && data.list) ? data.list
            : (Array.isArray(data) ? data : []);
        return list.map(function (o, idx) {
            return {
                id: o.id != null ? o.id : idx,
                price: o.price || 0,
                executor: o.executor || null,
                taken: !!(o.executor),
                cargoType: o.cargoType || '',
                weight: o.weight || 0,
                companyExp: o.companyExp || 0,
                trailerBase: companyPosOf(o.trailerBase) || companyPosOf(o.from) || null,
                deliveryPlace: companyPosOf(o.deliveryPlace) || companyPosOf(o.to) || null,
                distance: o.distance || null,
                raw: o
            };
        });
    }

    function companyActiveWeightTabs() {
        if (companyFilterWeight === 'all') return COMPANY_WEIGHT_TABS.slice();
        return [companyFilterWeight];
    }

    function companyActiveTypes() {
        if (!companyFilterTypes || !companyFilterTypes.length)
            return COMPANY_ALL_TYPES.slice();
        return companyFilterTypes.slice();
    }

    async function companyFetchOrdersPage(weightTab, pageParam) {
        const payload = JSON.stringify({
            pageParam: pageParam || 0,
            weightTab: weightTab || 'light',
            selectedTypes: companyActiveTypes(),
            sortedBy: companyFilterSort || 'price',
            sortedOrder: 'desc',
            onlyFree: !!companyFilterOnlyFree
        });
        // Executor/V8 often has no mp.events.callProc (CEF bridge only).
        // Server RPC is callRemoteProc; companyQuests.addProc just wraps it.
        let raw;
        if (typeof mp.events.callRemoteProc === 'function') {
            raw = await mp.events.callRemoteProc('getCompanyOrders', payload);
        } else if (typeof mp.events.callProc === 'function') {
            raw = await mp.events.callProc(
                'companyQuests.callRemoteProc',
                'getCompanyOrders',
                payload
            );
        } else {
            throw new Error('no callRemoteProc/callProc');
        }
        let data = raw;
        if (typeof raw === 'string') {
            try { data = JSON.parse(raw); } catch (e) { data = null; }
        }
        return data;
    }

    async function companyDumpOrdersToDisk() {
        // Reuse the CEF drain path to collect the orders.
        cefDumpBuffers = {};
        cefDumpFiles = [];
        cefDumpCursor = null;
        cefDumpInFlight = 1;
        cefDumpForceReady = false;
        try {
            const meta = {
                ts: Date.now(),
                iso: new Date().toISOString(),
                company: !!mp.players.local.getVariable('company'),
                inVeh: !!mp.players.local.vehicle,
                companyVeh: !!mp.players.local.getVariable('variable_company'),
                api: {
                    callProc: typeof (mp.events && mp.events.callProc),
                    callRemoteProc: typeof (mp.events && mp.events.callRemoteProc)
                },
                filter: {
                    weight: companyFilterWeight,
                    sort: companyFilterSort,
                    onlyFree: !!companyFilterOnlyFree,
                    types: companyActiveTypes()
                }
            };
            const tabs = ['light', 'medium', 'heavy'];
            const all = { meta: meta, tabs: {} };
            for (let i = 0; i < tabs.length; i++) {
                const tab = tabs[i];
                try {
                    const data = await companyFetchOrdersPage(tab, 0);
                    all.tabs[tab] = data;
                    const list = (data && data.list) || [];
                    qtCefStoreFile(
                        'firma/orders_' + tab + '.json',
                        JSON.stringify(data, null, 2));
                    companyLog(
                        'dump ' + tab + ': list=' + list.length
                        + ' keys0=' + (list[0] ? Object.keys(list[0]).join(',') : '-'));
                } catch (e) {
                    all.tabs[tab] = { error: String(e) };
                    qtCefStoreFile(
                        'firma/orders_' + tab + '.error.txt',
                        String(e));
                    companyLog('dump ' + tab + ' err: ' + e);
                }
            }
            qtCefStoreFile(
                'firma/orders_all.json',
                JSON.stringify(all, null, 2));
            companyLog('dump ready files=' + cefDumpFiles.length);
            qtSilentNotice('Firma dump ready');
        } catch (e) {
            qtCefStoreFile('firma/dump.error.txt', String(e));
            companyLog('dump fatal: ' + e);
        } finally {
            cefDumpInFlight = 0;
            cefDumpForceReady = true;
        }
    }

    async function companyScanOrders() {
        companyOrders = [];
        const tabs = companyActiveWeightTabs();
        for (let wi = 0; wi < tabs.length; wi++) {
            const tab = tabs[wi];
            try {
                const data = await companyFetchOrdersPage(tab, 0);
                const chunk = companyNormalizeOrders(data);
                companyLog('scan ' + tab + ': ' + chunk.length);
                for (let i = 0; i < chunk.length; i++) {
                    const o = chunk[i];
                    if (!companyOrders.find(function (x) { return x.id === o.id; }))
                        companyOrders.push(o);
                }
            } catch (e) {
                companyLog('scan ' + tab + ' err: ' + e);
            }
        }
        const types = companyActiveTypes();
        companyOrders = companyOrders.filter(function (o) {
            if (companyFilterOnlyFree && o.taken) return false;
            if (o.cargoType && types.indexOf(String(o.cargoType)) < 0) return false;
            return true;
        });
        if (companyFilterSort === 'weight')
            companyOrders.sort(function (a, b) { return (b.weight || 0) - (a.weight || 0); });
        else if (companyFilterSort === 'distance')
            companyOrders.sort(function (a, b) {
                const da = (a.distance && a.distance.from) || a.distance || 0;
                const db = (b.distance && b.distance.from) || b.distance || 0;
                return (da || 0) - (db || 0);
            });
        else
            companyOrders.sort(function (a, b) { return (b.price || 0) - (a.price || 0); });
        companyLog('filtered: ' + companyOrders.length
            + ' types=' + companyActiveTypes().length
            + ' weight=' + companyFilterWeight
            + ' sort=' + companyFilterSort);
        return companyOrders;
    }

    function companyTakeOrder(orderId) {
        if (!sessionToken) {
            companyLog('no token — open M / any shop once');
            return false;
        }
        companyLog('companyGetOrder #' + orderId);
        return callRemote('companyGetOrder', orderId);
    }

    function companyPickBestOrder() {
        const free = companyOrders.filter(function (o) { return !o.taken && o.id != null; });
        return free.length ? free[0] : null;
    }

    async function companyTpUnderground(pos, label) {
        if (!pos || pos.x == null || pos.y == null) {
            companyLog('TP fail: bad pos (' + label + ')');
            return false;
        }
        const z = (pos.z != null ? pos.z : 0) - COMPANY_UNDER_Z;
        companyLog('TP under ' + label + ' (' + Math.round(pos.x) + ',' + Math.round(pos.y) + ',' + Math.round(z) + ')');
        try { mp.events.call('ac:refreshtp'); } catch (e) {}
        setPlayerInvincible(true);
        const p = getPlayer();
        const v = getVehicle();
        try {
            if (v && p && p.vehicle) {
                v.setCoordsNoOffset(pos.x, pos.y, z, false, false, false);
            } else if (p) {
                p.setCoordsNoOffset(pos.x, pos.y, z, false, false, false);
            }
        } catch (e) {
            companyLog('TP err: ' + e);
            return false;
        }
        freezeVehicle(true);
        await sleep(500);
        try { mp.events.call('ac:refreshtp'); } catch (e) {}
        await sleep(300);
        return true;
    }

    async function companyWaitFoodWater() {
        injectUseItemHook();
        if (!foodSqlId) {
            listeningForFood = true;
            listeningForWater = false;
            companyLog('use FOOD in inventory...');
            let foodWait = 0;
            while (companyBotActive && !foodSqlId && foodWait < 60000) {
                await sleep(500);
                foodWait += 500;
            }
            if (!foodSqlId) companyLog('no food set — skip');
            else companyLog('food sqlId=' + foodSqlId);
        }
        if (!waterSqlId && companyBotActive) {
            listeningForWater = true;
            listeningForFood = false;
            companyLog('use WATER in inventory...');
            let waterWait = 0;
)QTJSX91"
        R"QTJSX91(            while (companyBotActive && !waterSqlId && waterWait < 60000) {
                await sleep(500);
                waterWait += 500;
            }
            if (!waterSqlId) companyLog('no water set — skip');
            else companyLog('water sqlId=' + waterSqlId);
        }
        listeningForFood = false;
        listeningForWater = false;
        if (!companyBotActive) return;
        if (foodSqlId || waterSqlId) {
            eatAndDrink();
            startEatInterval();
        }
    }
    async function companyBotLoop() {
        if (companyLoopRunning) return;
        companyLoopRunning = true;
        companyLog('bot loop start');
        try {
            await companyWaitFoodWater();
            while (companyBotActive) {
                if (!sessionToken) {
                    companyLog('waiting token...');
                    await sleep(1500);
                    continue;
                }
                await companyScanOrders();
                const best = companyPickBestOrder();
                if (!best) {
                    companyLog('no free orders — retry 8s');
                    await sleep(8000);
                    continue;
                }
                companyTrailerPoints = [];
                companyDeliveryPlace = null;
                companyTakeOrder(best.id);

                let trailerPos = best.trailerBase;
                let waited = 0;
                while (companyBotActive && !trailerPos && waited < 12000) {
                    if (companyTrailerPoints.length && companyTrailerPoints[0].position)
                        trailerPos = companyTrailerPoints[0].position;
                    await sleep(250);
                    waited += 250;
                }
                if (companyTrailerPoints.length && companyTrailerPoints[0].position)
                    trailerPos = companyTrailerPoints[0].position;
                if (!trailerPos) {
                    companyLog('no trailer coords — next');
                    await sleep(3000);
                    continue;
                }

                await companyTpUnderground(trailerPos, 'trailer');
                companyLog('companyTrailerVehicle.success');
                callRemote('companyTrailerVehicle.success', null);
                await sleep(1500);

                let deliverPos = best.deliveryPlace || companyDeliveryPlace;
                waited = 0;
                while (companyBotActive && !deliverPos && waited < 20000) {
                    deliverPos = companyDeliveryPlace || deliverPos;
                    await sleep(250);
                    waited += 250;
                }
                deliverPos = companyDeliveryPlace || deliverPos;
                if (!deliverPos) {
                    companyLog('no delivery coords — next');
                    setPlayerInvincible(false);
                    freezeVehicle(false);
                    await sleep(3000);
                    continue;
                }

                await companyTpUnderground(deliverPos, 'delivery');
                companyLog('companySuccessfulDeliveryTrailerPay');
                callRemote('companySuccessfulDeliveryTrailerPay', null);
                await sleep(2000);
                companyDeliveries++;
                companyLog('delivered #' + best.id + ' total=' + companyDeliveries);
                setPlayerInvincible(false);
                freezeVehicle(false);
                await sleep(2500);
            }
        } catch (e) {
            companyLog('loop err: ' + e);
        }
        companyLoopRunning = false;
        companyStatus = 'Idle';
        if (!botActive) stopEatInterval();
        setPlayerInvincible(false);
        freezeVehicle(false);
    }

    function companyStartBot() {
        if (companyBotActive) return;
        companyBotActive = true;
        companyLog('START');
        companyBotLoop();
    }

    function companyStopBot() {
        companyBotActive = false;
        companyTrailerPoints = [];
        companyDeliveryPlace = null;
        if (!botActive) stopEatInterval();
        setPlayerInvincible(false);
        freezeVehicle(false);
        companyLog('STOP');
    }

    function companyApplyFilterCmd(cmd) {
        // company_filter:weight=light;sort=price;free=1;types=tools,fuel,food
        const body = cmd.indexOf(':') >= 0 ? cmd.substring(cmd.indexOf(':') + 1) : '';
        const parts = body.split(';');
        for (let i = 0; i < parts.length; i++) {
            const kv = parts[i].split('=');
            if (kv.length < 2) continue;
            const k = kv[0].trim();
            const v = kv.slice(1).join('=').trim();
            if (k === 'weight') companyFilterWeight = v || 'all';
            else if (k === 'sort') companyFilterSort = v || 'price';
            else if (k === 'free') companyFilterOnlyFree = !(v === '0' || v === 'false');
            else if (k === 'types') {
                if (!v || v === 'all') companyFilterTypes = COMPANY_ALL_TYPES.slice();
                else companyFilterTypes = v.split(',').map(function (s) { return s.trim(); }).filter(Boolean);
            }
        }
        companyLog('filter weight=' + companyFilterWeight
            + ' sort=' + companyFilterSort
            + ' free=' + companyFilterOnlyFree
            + ' types=' + companyFilterTypes.join(','));
    }

    mp.events.add('createTrailerPoint', function (t) {
        if (!companyBotActive) return;
        const arr = (Array.isArray(t) ? t : [t]).filter(Boolean);
        companyTrailerPoints = arr.map(function (a) {
            return {
                position: companyPosOf(a.position) || companyPosOf(a),
                heading: a.heading != null ? a.heading : 0
            };
        });
        companyLog('createTrailerPoint x' + companyTrailerPoints.length);
    });
    mp.events.add('removeTrailerPoint', function () {
        if (!companyBotActive) return;
        companyTrailerPoints = [];
        companyLog('removeTrailerPoint');
    });
    mp.events.add('client::truckerCreateDeliveryPlace', function (t) {
        if (!companyBotActive) return;
        companyDeliveryPlace = companyPosOf(t);
        companyLog('delivery place received');
    });
    mp.events.add('client::truckerDeleteDeliveryPlace', function () {
        if (!companyBotActive) return;
        companyDeliveryPlace = null;
    });

    // ============================================================
    // UNLOAD
    // ============================================================
    function unloadScript() {
        if (!qtAlive) return;
        qtAlive = false;
        try { miscDestroyFreecam(); } catch (e) {}
        miscState.freecam = false;
        miscState.noclip = false;
        stopAntiRecoil();
        botActive = false;
        companyBotActive = false;
        if (crawlActive) crawlStop();
        ampBotActive = false;
        if (ampBotTimer) { qtClearTimeout(ampBotTimer); ampBotTimer = null; }
        matBotActive = false;
        if (matBotTimer) { qtClearTimeout(matBotTimer); matBotTimer = null; }
        // Cleanup radial menu
        destroyRadialMenu();
        mp.gui.cursor.show = _origCursorShow;
        try { mp.keys.unbind(radialKey, true); } catch(e) {}
        try { mp.keys.unbind(radialKey, false); } catch(e) {}
        stopEatInterval();
        stopHpMonitor();
        stopStrafe();
        antiFiringEnabled = false;
        sgNoFalloffEnabled = false;
        stopWeaponCheck();
        freezeVehicle(false);
        setVehicleCollision(true);
        qtTimeouts.slice().forEach(function(id) { clearTimeout(id); });
        qtTimeouts.length = 0;
        qtIntervals.slice().forEach(function(id) { clearInterval(id); });
        qtIntervals.length = 0;
        try { mp.keys.unbind(crawlKey, true); } catch (e) {}
        try { mp.keys.unbind(0x70, true); } catch (e) {}
        try { mp.events.remove('playerReady', resetAdminJoinBaseline); } catch (e) {}
        const evts = ['qt:crawl', 'qt:setCrawlKey', 'qt:hitMute', 'qt:hitVol', 'qt:hitUrl', 'qt:autoBuy', 'qt:marketListing', 'qt:botToggle', 'qt:loadAir', 'qt:tpStorage', 'qt:botReset', 'qt:autoToken', 'qt:buyPills', 'qt:buyAid', 'qt:die', 'qt:ampBot', 'qt:matBot', 'qt:setToken', 'qt:localCmd', 'qt:botVersion', 'qt:listenFood', 'qt:listenWater', 'qt:detectedUseItem', 'qt:eat', 'qt:wpnToggle', 'qt:wpnApplyAll', 'qt:wpnRemoveAll', 'qt:wpnRefresh', 'qt:radialHover', 'qt:chatTest', 'qt:airDumpCef', 'qt:cefDumpBatch', 'qt:cefDumpStart', 'qt:cefDumpChunk', 'qt:cefDumpDone', 'qt:cefSearchResult', 'qt:cefFetchResult'];
        evts.forEach(e => { try { mp.events.remove(e); } catch (ex) {} });
        delete global.__qtCommand;
        delete global.__qtGetStatus;
        delete global.__qtGetLog;
        delete global.__qtGetAirDump;
        delete global.__qtCefDumpPull;
        delete global.__qtCefDumpForce;
        delete global._qt_unload;
        log('Unloaded.');
    }


    function qtBridgeSafe(value) {
        return String(value == null ? '' : value).replace(/[\t\r\n]/g, ' ');
    }

    global.__qtCommand = function(command) {
        const cmd = String(command || '').trim();
        if (!cmd) return 'empty command';
        mp.events.call('qt:localCmd', cmd);
        return 'accepted: ' + cmd;
    };


    // ===== Local CEF dump (chunk assemble + C++ drain) =====
    let cefDumpBuffers = {};
    let cefDumpFiles = [];
    let cefDumpInFlight = 0;
    let cefDumpCursor = null; // { file:0, offset:0 }
    let cefDumpUrls = {};
    let cefDumpForceReady = false;

    function qtCefSanitizeName(name) {
        // Keep path separators so bundle/part_000.js matches old layout.
        return String(name || 'cef')
            .replace(/[<>:"|?*\x00-\x1f]/g, '_')
            .replace(/\\/g, '/')
            .replace(/\/+/g, '/')
            .replace(/^\/+/, '')
            .slice(0, 120);
    }

    function qtCefStoreFile(name, data) {
        const n = qtCefSanitizeName(name);
        cefDumpFiles.push({
            name: n,
            data: String(data == null ? '' : data)
        });
        log('CEF dump stored: ' + n + ' (' + String(data || '').length + ' chars)');
        qtUiLog('CEF saved: ' + n);
    }

    function qtCefMakeHtmlDumpScript(name) {
        const n = String(name).replace(/\\/g, '\\\\').replace(/'/g, "\\'");
        // Same pacing as old QTools (25KB @ 100ms) — proven stable.
        return "(function(){try{"
            + "var data=document.documentElement?document.documentElement.outerHTML:String(document.body&&document.body.innerHTML||'');"
            + "var cs=25000;var total=Math.max(1,Math.ceil(data.length/cs));"
            + "mp.trigger('qt:cefDumpStart','" + n + "',String(total),String(data.length));"
            + "for(var i=0;i<total;i++){(function(idx){setTimeout(function(){"
            + "mp.trigger('qt:cefDumpChunk','" + n + "',String(idx),data.substring(idx*cs,(idx+1)*cs));"
            + "},idx*100);})(i);}"
)QTJSX91"
        R"QTJSX91(            + "}catch(e){mp.trigger('qt:cefDumpDone','" + n + ":error:'+e);}})()";
    }

    // QTools dump_cef_scripts: fetch all <script> tags → ONE JSON array →
    // one chunked mp.trigger stream. Client expands to cef_scripts layout.
    function qtCefMakeScriptsJsonDumpScript() {
        return "(function(){try{"
            + "var result=[];var tags=document.querySelectorAll('script');"
            + "var pending=0,done=0;"
            + "function sendChunked(){"
            + "var data=JSON.stringify(result);"
            + "var cs=25000;var total=Math.max(1,Math.ceil(data.length/cs));"
            + "mp.trigger('qt:cefDumpStart','browserMenu_scripts',String(total),String(data.length));"
            + "for(var i=0;i<total;i++){(function(idx){setTimeout(function(){"
            + "mp.trigger('qt:cefDumpChunk','browserMenu_scripts',String(idx),data.substring(idx*cs,(idx+1)*cs));"
            + "},idx*100);})(i);}"
            + "}"
            + "for(var i=0;i<tags.length;i++){var tag=tags[i];"
            + "if(tag.src){pending++;(function(url){fetch(url).then(function(r){return r.text()}).then(function(t){"
            + "result.push({src:url,code:t});done++;if(done>=pending)sendChunked();"
            + "}).catch(function(){result.push({src:url,code:'FETCH_ERROR'});done++;if(done>=pending)sendChunked();});})(tag.src);}"
            + "else if(tag.textContent&&tag.textContent.trim()){result.push({src:'inline_'+i,code:tag.textContent});}}"
            + "if(!pending)sendChunked();"
            + "}catch(e){mp.trigger('qt:cefDumpDone','scripts:error:'+e);}})()";
    }

    // Expand QTools JSON [{src,code},...] into cef_scripts layout:
    //   N_name.js for small tags, bundle/part_XXX.js (~480KB) for Vite.
    function qtCefExpandScriptsJson(jsonText) {
        let arr;
        try { arr = JSON.parse(jsonText); }
        catch (e) {
            qtCefStoreFile('browserMenu_scripts.raw.json', String(jsonText || ''));
            qtCefStoreFile('scripts_parse.error.txt', String(e));
            return;
        }
        if (!Array.isArray(arr)) {
            qtCefStoreFile('browserMenu_scripts.raw.json', String(jsonText || ''));
            return;
        }
        const PART = 480000;
        let partIdx = 0;
        const indexLines = ['# CEF Bundle Index', ''];
        for (let i = 0; i < arr.length; i++) {
            const src = (arr[i] && arr[i].src != null) ? String(arr[i].src) : ('inline_' + i);
            const code = String((arr[i] && arr[i].code != null) ? arr[i].code : '');
            if (!code.length) continue;
            let base = 'script.js';
            try {
                const u = src.split('?')[0];
                const slash = u.lastIndexOf('/');
                base = (slash >= 0 ? u.substring(slash + 1) : u)
                    .replace(/[<>:"/\\|?*\x00-\x1f]/g, '_')
                    .slice(0, 72) || 'script.js';
            } catch (e) {}
            if (code.length > PART) {
                const parts = Math.ceil(code.length / PART);
                for (let p = 0; p < parts; p++) {
                    const name = 'bundle/part_' + ('000' + partIdx).slice(-3) + '.js';
                    const slice = code.substring(p * PART, (p + 1) * PART);
                    qtCefStoreFile(name, slice);
                    indexLines.push(
                        name + ' (' + Math.round(slice.length / 1024) + 'KB)');
                    partIdx++;
                }
            } else {
                let nm;
                if (src.indexOf('inline_') === 0)
                    nm = i + '_' + src;
                else {
                    nm = i + '_' + base;
                    if (!/\.js$/i.test(nm)) nm += '.js';
                }
                qtCefStoreFile(nm, code);
            }
        }
        if (partIdx > 0)
            qtCefStoreFile('bundle/INDEX.md', indexLines.join('\n'));
        log('CEF scripts expanded files=' + cefDumpFiles.length + ' parts=' + partIdx);
        qtUiLog('CEF scripts → ' + cefDumpFiles.length + ' files');
    }

    function qtCefStartNamedDump(name, starter) {
        const n = qtCefSanitizeName(name);
        cefDumpInFlight++;
        cefDumpForceReady = false;
        try { starter(n); }
        catch (e) {
            cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
            qtCefStoreFile(n + '.error.txt', String(e));
        }
    }

    function qtCefBeginFullDump() {
        cefDumpBuffers = {};
        cefDumpFiles = [];
        cefDumpCursor = null;
        cefDumpUrls = {};
        cefDumpInFlight = 0;
        cefDumpForceReady = false;
        // HTML for every browser (serialized) + browserMenu scripts via the
        // original QTools single-JSON stream (NOT per-file mp.trigger flood).
        log('CEF FULL dump starting (HTML + QTools scripts)...');
        qtUiLog('CEF FULL dump...');

        const jobs = [];
        jobs.push(function() {
            qtCefStartNamedDump('00_browserMenu.html', function(name) {
                if (!safeBrowserExecute(qtCefMakeHtmlDumpScript(name))) {
                    cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                    qtCefStoreFile(name + '.error.txt', 'browserMenu.execute unavailable');
                }
            });
        });

        let browserIndex = 0;
        try {
            if (typeof mp !== 'undefined' && mp.browsers && mp.browsers.forEach) {
                mp.browsers.forEach(function(b) {
                    const idx = browserIndex++;
                    let url = '';
                    try { url = String(b.url || b._url || ''); } catch (e) {}
                    const base = 'browser_' + (idx < 10 ? '0' : '') + idx;
                    cefDumpUrls[base + '.html'] = url;
                    (function(browser, baseName, browserUrl) {
                        jobs.push(function() {
                            qtCefStartNamedDump(baseName + '.html', function(name) {
                                try {
                                    if (!browser || typeof browser.execute !== 'function')
                                        throw new Error('browser.execute missing');
                                    browser.execute(qtCefMakeHtmlDumpScript(name));
                                } catch (e) {
                                    cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                                    qtCefStoreFile(name + '.error.txt', String(e) + '\nurl=' + browserUrl);
                                }
                            });
                        });
                    })(b, base, url);
                });
            }
        } catch (e) {
            qtCefStoreFile('browsers_enum.error.txt', String(e));
        }

        // Last: QTools scripts dump from browserMenu only.
        jobs.push(function() {
            qtCefStartNamedDump('browserMenu_scripts', function() {
                if (!safeBrowserExecute(qtCefMakeScriptsJsonDumpScript())) {
                    cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                    qtCefStoreFile('browserMenu_scripts.error.txt', 'browserMenu.execute unavailable');
                }
            });
        });

        let jobIndex = 0;
        function runNextJob() {
            if (jobIndex >= jobs.length) {
                log('CEF FULL dump queue drained jobs=' + jobs.length);
                return;
            }
            if (cefDumpInFlight > 0) {
                setTimeout(runNextJob, 750);
                return;
            }
            try { jobs[jobIndex++](); }
            catch (e) {
                qtCefStoreFile('job_' + jobIndex + '.error.txt', String(e));
                jobIndex++;
            }
            setTimeout(runNextJob, 1000);
        }
        setTimeout(runNextJob, 200);

        const manifest = {
            startedAt: new Date().toISOString(),
            browserCount: browserIndex,
            urls: cefDumpUrls,
            note: 'HTML per browser + browserMenu scripts as QTools cef_scripts layout'
        };
        qtCefStoreFile('00_manifest.json', JSON.stringify(manifest, null, 2));
        log('CEF FULL dump queued browsers=' + browserIndex + ' jobs=' + jobs.length);
    }

    try {
        mp.events.add('qt:cefDumpBatch', function(countStr) {
            const n = Math.max(1, parseInt(countStr, 10) || 1);
            // StartNamedDump already counted +1 for the orchestrator.
            if (n > 1)
                cefDumpInFlight += (n - 1);
            log('CEF dump batch size=' + n + ' inFlight=' + cefDumpInFlight);
            qtUiLog('CEF dump files: ' + n);
        });
        mp.events.add('qt:cefDumpStart', function(name, totalStr, sizeStr) {
            const total = parseInt(totalStr, 10) || 0;
            const size = parseInt(sizeStr, 10) || 0;
            cefDumpBuffers[String(name)] = {
                chunks: new Array(Math.max(0, total)),
                total: Math.max(0, total),
                received: 0,
                size: size
            };
            log('CEF dump start ' + name + ' size=' + size + ' chunks=' + total);
        });
        mp.events.add('qt:cefDumpChunk', function(name, idxStr, chunk) {
            const key = String(name);
            const buf = cefDumpBuffers[key];
            if (!buf) return;
            const idx = parseInt(idxStr, 10) || 0;
            if (idx < 0 || idx >= buf.total) return;
            if (buf.chunks[idx] == null) {
                buf.chunks[idx] = String(chunk == null ? '' : chunk);
                buf.received++;
            }
            if (buf.received >= buf.total) {
                const full = buf.chunks.join('');
                delete cefDumpBuffers[key];
                // QTools scripts stream is one JSON array — expand to cef_scripts
                // layout instead of writing a single giant .json file.
                if (key === 'browserMenu_scripts' ||
                    (key.indexOf('scripts') >= 0 && full.charAt(0) === '[')) {
                    qtCefExpandScriptsJson(full);
                } else {
                    qtCefStoreFile(key, full);
                }
                cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                if (cefDumpInFlight === 0) {
                    log('CEF dump all in-flight complete, files=' + cefDumpFiles.length);
                    qtUiLog('CEF dump ready (' + cefDumpFiles.length + ' files)');
                }
            }
        });
        mp.events.add('qt:cefDumpDone', function(info) {
            log('CEF dump done signal: ' + info);
            // Soft-complete stuck in-flight on explicit error signals.
            if (String(info || '').indexOf('error') >= 0) {
                cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
            }
        });
        mp.events.add('qt:cefSearchResult', function(json) {
            try {
                qtCefStoreFile('search_result.json', String(json));
)QTJSX91"
        R"QTJSX91(                log('CEF search result stored');
            } catch (e) {}
        });
        mp.events.add('qt:cefFetchResult', function(json) {
            try {
                qtCefStoreFile('fetch_result.json', String(json));
                log('CEF fetch result stored');
            } catch (e) {}
        });
    } catch (e) {
        log('CEF dump handlers install error: ' + e);
    }

    global.__qtCefDumpPull = function() {
        // Stream to disk while CEF is still capturing — do not wait for all
        // ~30MB bundle parts to sit in memory first (that stalled the old drain).
        if (!cefDumpCursor) {
            cefDumpCursor = { file: 0, offset: 0 };
        }
        if (cefDumpCursor.file >= cefDumpFiles.length) {
            if (cefDumpInFlight > 0 && !cefDumpForceReady) {
                return 'CEF_BUSY:' + cefDumpInFlight + ':' + cefDumpFiles.length;
            }
            const total = cefDumpFiles.length;
            cefDumpCursor = null;
            cefDumpForceReady = false;
            return 'CEF_IDLE:' + total;
        }
        const entry = cefDumpFiles[cefDumpCursor.file];
        const start = cefDumpCursor.offset;
        // Prefer raw ASCII chunks (prefix R) to avoid encodeURIComponent 3x blowup.
        // Fall back to E+encodeURIComponent when tabs/newlines appear.
        // Keep under RageV8Runtime::kMaxResultLength (32KB).
        // Larger chunks = fewer Present-thread V8 pulls for ~9MB scripts.
        const rawBudget = 20000;
        let chunk = entry.data.slice(start, start + rawBudget);
        let packed;
        if (/[\t\r\n\x00]/.test(chunk)) {
            chunk = entry.data.slice(start, start + 4000);
            packed = 'E' + encodeURIComponent(chunk);
        } else {
            packed = 'R' + chunk;
        }
        cefDumpCursor.offset += chunk.length;
        const complete = cefDumpCursor.offset >= entry.data.length ? 1 : 0;
        const safeName = encodeURIComponent(entry.name);
        const result = [
            'CEF_CHUNK',
            cefDumpCursor.file,
            safeName,
            start,
            entry.data.length,
            complete,
            packed
        ].join('\t');
        if (complete) {
            // Free RAM as soon as a file is fully drained.
            try { cefDumpFiles[cefDumpCursor.file].data = ''; } catch (e) {}
            cefDumpCursor.file++;
            cefDumpCursor.offset = 0;
        }
        return result;
    };

    global.__qtCefDumpForce = function() {
        cefDumpForceReady = true;
        return 'CEF_FORCE:' + cefDumpInFlight + ':' + cefDumpFiles.length;
    };

    global.__qtGetAirDump = function() {
        if (airDumpActive())
            return 'AIRDUMP_BUSY\n' + airDumpLines.join('\n');
        return airDumpBuffer
            ? ('AIRDUMP\n' + airDumpBuffer)
            : 'AIRDUMP_EMPTY';
    };

    let lastFarmModule = 1; // 1=aquanaut, 2=firma (for idle widget layout)

    global.__qtGetStatus = function() {
        refreshAirMinutes();
        if (botActive) tickLootPriceExpiry();
        if (companyBotActive) lastFarmModule = 2;
        else if (botActive) lastFarmModule = 1;
        const farmModule = companyBotActive ? 2 : (botActive ? 1 : lastFarmModule);
        const anyActive = !!(botActive || companyBotActive);
        const elapsed = botActive && botStartTime > 0
            ? (Date.now() - botStartTime) / 3600000
            : 0;
        const perHour = elapsed > 0.01
            ? Math.round((totalEarned + bonusEarned) / elapsed)
            : 0;
        const statusText = companyBotActive
            ? companyStatus
            : currentStatus;
        const rows = [
            [
                'S',
                anyActive ? 1 : 0,
                qtBridgeSafe(statusText),
                sessionToken ? 1 : 0,
                botVersion,
                companyBotActive ? companyDeliveries : boxesCollected,
                companyBotActive ? companyDeliveries : totalBoxes,
                companyBotActive ? companyDeliveries : totalRounds,
                totalEarned + bonusEarned,
                perHour,
                airMinutes == null ? -1 : airMinutes,
                adminDetectorActive ? 1 : 0,
                adminEspActive ? 1 : 0,
                ampBotActive ? 1 : 0,
                farmModule
            ].join('\t')
        ];
        rows.push([
            'P',
            lootPrices.pearl != null ? lootPrices.pearl : -1,
            lootPrices.starfish != null ? lootPrices.starfish : -1,
            lootPrices.rapana != null ? lootPrices.rapana : -1,
            lootPriceExpiryMs || 0,
            lootPricesStale ? 1 : 0
        ].join('\t'));
        for (const [id, admin] of knownServerAdmins) {
            rows.push([
                'O',
                id,
                qtBridgeSafe(admin.name),
                admin.lvl || 0
            ].join('\t'));
        }
        for (const admin of activeAdminList) {
            rows.push([
                'A',
                admin.id,
                qtBridgeSafe(admin.name),
                admin.lvl || 0,
                admin.dist == null ? -1 : admin.dist,
                admin.invis ? 1 : 0,
                admin.spectating ? 1 : 0,
                admin.offDuty ? 1 : 0
            ].join('\t'));
        }
        if (lastCriticalAdminJoin) {
            rows.push([
                'J',
                lastCriticalAdminJoin.serial,
                lastCriticalAdminJoin.id,
                qtBridgeSafe(lastCriticalAdminJoin.name),
                lastCriticalAdminJoin.lvl
            ].join('\t'));
        }
        return rows.join('\n');
    };

    global.__qtGetLog = function() {
        return logLines.slice(-20).join('\n');
    };

    mp.events.add('qt:unload', () => unloadScript());
    global._qt_unload = () => unloadScript();

    log('Loaded. Local menu integration ready.');
})();
)QTJSX91";
}
