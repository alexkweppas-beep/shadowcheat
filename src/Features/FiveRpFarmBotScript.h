#pragma once

namespace FiveRpFarmBotScript
{
    inline constexpr char kSource[] =
R"FIVEFARM0(
if (global.__fiveFarmUnhook) {
    try { global.__fiveFarmUnhook(); } catch (_) {}
}
const __ff = {
    active: false,
    invis: true,
    teleport: true,
    state: 'idle',
    message: 'idle',
    cow: -1,
    cycles: 0,
    lastInteract: 0,
    waitUntil: 0,
    lastInvis: 0,
    farm: null,
    listeners: [],
    timer: null
};
const __ffBuckets = [
    [2232.28, 4903.13, 40.07],
    [2232.57, 4903.47, 40.07],
    [2232.83, 4903.85, 40.07]
];
const __ffTanks = [
    [2212.38, 4934.21, 39.89],
    [2256.56, 4875.83, 39.88]
];
const __ffCows = [
    [2247.87, 4871.79, 40.89],
    [2245.65, 4874.52, 40.87],
    [2242.11, 4870.17, 40.79],
    [2232.51, 4889.99, 40.61],
    [2227.58, 4884.5, 40.64],
    [2224.77, 4887.28, 40.74],
    [2228.21, 4895.23, 40.59],
    [2220.03, 4902.43, 40.69],
    [2215.04, 4898.09, 40.92],
    [2207.94, 4906.76, 40.7],
    [2216.28, 4906.66, 40.74],
    [2203.08, 4918.76, 40.66],
    [2200.00, 4913.71, 40.63],
    [2219.73, 4934.89, 40.89],
    [2224.05, 4937.4, 40.95],
    [2229.43, 4934.61, 40.87],
    [2225.47, 4930.74, 40.8],
    [2235.83, 4928.62, 40.87],
    [2233.03, 4921.98, 40.79],
    [2231.06, 4924.27, 40.8],
    [2236.57, 4915.74, 40.67],
    [2236.66, 4919.33, 40.73],
    [2241.42, 4924.01, 40.78],
    [2245.38, 4919.58, 40.75],
    [2248.72, 4905.1, 40.72],
    [2251.51, 4906.41, 40.78],
    [2252.8, 4911.77, 40.75],
    [2252.84, 4902.72, 40.75],
    [2257.82, 4908.22, 40.8],
    [2260.45, 4895.13, 40.87],
    [2264.13, 4898.61, 40.87],
    [2266.94, 4893.75, 40.91]
];
function __ffNum(v) {
    const n = Number(v);
    return Number.isFinite(n) ? n : 0;
}
function __ffDist(a, b) {
    if (!a || !b) return 9999;
    const dx = __ffNum(a.x) - __ffNum(b.x);
    const dy = __ffNum(a.y) - __ffNum(b.y);
    const dz = __ffNum(a.z) - __ffNum(b.z);
    return Math.sqrt(dx * dx + dy * dy + dz * dz);
}
function __ffLocal() {
    try { return mp.players.local; } catch (_) { return null; }
}
function __ffPos() {
    try { return __ffLocal().position; } catch (_) { return null; }
}
function __ffLooksFarm(o) {
    if (!o || (typeof o !== 'object' && typeof o !== 'function'))
        return false;
    try {
        const barn = o.cows;
        const list = barn && barn.cows;
        return typeof o.isOnJob === 'boolean' ||
            typeof o.hasBucket === 'boolean' ||
            Array.isArray(list);
    } catch (_) { return false; }
}
function __ffFindFarm() {
    if (__ffLooksFarm(__ff.farm)) return __ff.farm;
    const roots = [];
    try { roots.push(global); } catch (_) {}
    try { roots.push(global.__ctx__); } catch (_) {}
    try { roots.push(global.__ctx__ && global.__ctx__.api); } catch (_) {}
    try { roots.push(mp); } catch (_) {}
    const keys = ['farm', '_farm', '__farm', 'farmSystem', 'farms'];
    for (let i = 0; i < roots.length; ++i) {
        const root = roots[i];
        if (!root) continue;
        if (__ffLooksFarm(root)) return root;
        for (let k = 0; k < keys.length; ++k) {
            try {
                const value = root[keys[k]];
                if (__ffLooksFarm(value)) return value;
                if (value && __ffLooksFarm(value._farm))
                    return value._farm;
            } catch (_) {}
        }
    }
    return null;
}
function __ffFarmFlag(key) {
    const farm = __ffFindFarm();
    if (!farm) return false;
    try { return farm[key] === true; } catch (_) { return false; }
}
function __ffCowList() {
    const out = [];
    const farm = __ffFindFarm();
    let live = null;
    try {
        live = farm && farm.cows && farm.cows.cows;
    } catch (_) {}
    const count = Array.isArray(live)
        ? Math.max(live.length, __ffCows.length)
        : __ffCows.length;
    for (let i = 0; i < count && i < 64; ++i) {
        const fallback = __ffCows[i] || __ffCows[0];
        let x = fallback[0], y = fallback[1], z = fallback[2];
        let angry = false;
        let index = i;
        const cow = live && live[i];
        if (cow) {
            try {
                const n = Number(cow.index);
                if (Number.isFinite(n)) index = Math.trunc(n);
                angry = cow.isAngry === true;
                let p = cow.position;
                if (!p && cow.offsets) p = cow.offsets.pickup;
                if (p && Number.isFinite(Number(p.x))) {
                    x = Number(p.x);
                    y = Number(p.y);
                    z = Number(p.z);
                }
            } catch (_) {}
        }
        out.push({ i: index, x: x, y: y, z: z, angry: angry });
    }
    return out;
}
function __ffOccupied(pt) {
    let hit = false;
    try {
        const local = __ffLocal();
        mp.players.forEach(function(player) {
            if (!player || player === local) return;
            try {
                if (__ffDist(player.position, pt) < 5)
                    hit = true;
            } catch (_) {}
        });
    } catch (_) {}
    return hit;
}
function __ffNearest(list) {
    const here = __ffPos();
    let best = null;
    let bestD = 1e9;
    for (let i = 0; i < list.length; ++i) {
        const item = list[i];
        const d = here ? __ffDist(here, item) : i;
        if (d < bestD) {
            bestD = d;
            best = item;
        }
    }
    return best;
}
function __ffPickCow() {
    const cows = __ffCowList();
    const here = __ffPos();
    let best = null;
    let bestD = 1e9;
    for (let i = 0; i < cows.length; ++i) {
        const cow = cows[i];
        if (cow.angry || __ffOccupied(cow)) continue;
        const d = here ? __ffDist(here, cow) : i;
        if (d < bestD) {
            bestD = d;
            best = cow;
        }
    }
    return best;
}

)FIVEFARM0"
R"FIVEFARM1(
function __ffRemote(name) {
    const args = Array.prototype.slice.call(arguments, 1);
    const events = (mp && mp.events) || null;
    if (!events || typeof events.callRemote !== 'function')
        return false;
    try {
        events.callRemote.apply(events, [name].concat(args));
        return true;
    } catch (_) {
        return false;
    }
}
function __ffTp(x, y, z) {
    if (!__ff.teleport) return false;
    const player = __ffLocal();
    if (!player) return false;
    try {
        player.setCoordsNoOffset(
            Number(x), Number(y), Number(z),
            false, false, false);
        return true;
    } catch (_) {}
    try {
        mp.game.ped.setCoordsNoOffset(
            player.handle,
            Number(x), Number(y), Number(z),
            false, false, false);
        return true;
    } catch (_) {}
    return false;
}
function __ffApplyInvis(on) {
    const player = __ffLocal();
    if (!player) return;
    const alpha = on ? 0 : 255;
    try { player.setVisible(!on); } catch (_) {}
    try { player.setAlpha(alpha); } catch (_) {}
    try {
        mp.game.entity.setEntityVisible(
            player.handle, !on, false);
    } catch (_) {}
    try {
        mp.game.entity.setEntityAlpha(
            player.handle, alpha, false);
    } catch (_) {}
    try {
        mp.game.player.setPlayerInvisibleLocally(
            player.handle, on);
    } catch (_) {}
    try { player.__disableNametag = !!on; } catch (_) {}
    try {
        if (player.vehicle) {
            player.vehicle.setVisible(!on);
            player.vehicle.setAlpha(alpha);
        }
    } catch (_) {}
    __ff.lastInvis = Date.now();
}
function __ffSetState(name, message) {
    __ff.state = name;
    if (message) __ff.message = String(message).slice(0, 80);
}
function __ffWait(ms) {
    __ff.waitUntil = Date.now() + ms;
}
function __ffListen(name, handler) {
    try {
        mp.events.add(name, handler);
        __ff.listeners.push([name, handler]);
    } catch (_) {}
}
function __ffOnWorker(value) {
    if (value) {
        if (__ff.state === 'idle' || __ff.state === 'start')
            __ffSetState('job', 'on job');
    } else if (__ff.active) {
        __ffSetState('idle', 'job ended');
        __ffWait(800);
    }
}
function __ffOnBucket(value) {
    if (value) __ffSetState('bucket', 'bucket ready');
    else if (__ff.state === 'bucket')
        __ffSetState('job', 'need bucket');
}
function __ffOnAnimStart(playerId, cowIndex) {
    const local = __ffLocal();
    if (local && Number(playerId) !== Number(local.remoteId))
        return;
    __ff.cow = __ffNum(cowIndex);
    __ffSetState('minigame', 'milk ' + __ff.cow);
    __ffWait(350);
}
function __ffOnAnimEnd(playerId, cowIndex, filled) {
    const local = __ffLocal();
    if (local && Number(playerId) !== Number(local.remoteId))
        return;
    if (filled) __ffSetState('filled', 'bucket filled');
    else {
        __ffSetState('bucket', 'milk failed');
        __ffWait(1200);
    }
}
function __ffOnDeliver(success) {
    if (success) {
        ++__ff.cycles;
        __ffSetState('job', 'delivered ' + __ff.cycles);
    } else __ffSetState('filled', 'deliver failed');
    __ffWait(700);
}
__ffListen('c:farm:worker_status', __ffOnWorker);
__ffListen('c:farm:bucket', __ffOnBucket);
__ffListen('c:farm:cow:anim:milk:start', __ffOnAnimStart);
__ffListen('c:farm:cow:anim:milk:end', __ffOnAnimEnd);
__ffListen('c:farm:milk:end', __ffOnDeliver);

function __ffTick() {
    const now = Date.now();
    if (__ff.invis && now - __ff.lastInvis > 2000)
        __ffApplyInvis(true);
    if (!__ff.active || now < __ff.waitUntil) return;
    __ff.farm = __ffFindFarm();
    const onJob = __ffFarmFlag('isOnJob');
    const hasBucket = __ffFarmFlag('hasBucket');
    const filled = __ffFarmFlag('isBucketFilled');
    if (__ff.state === 'idle' || __ff.state === 'start') {
        if (onJob) {
            __ffSetState('job', 'on job');
            return;
        }
        __ffSetState('start', 'starting job');
        const bucket = __ffNearest(__ffBuckets.map(function(p) {
            return { x: p[0], y: p[1], z: p[2] };
        }));
        if (bucket) __ffTp(bucket.x, bucket.y, bucket.z);
        __ffRemote('s:farm:enter_zone');
        __ffRemote('s:farm:start');
        __ffWait(1200);
        return;
    }
    if (__ff.state === 'job') {
        if (filled) {
            __ffSetState('filled', 'already filled');
            return;
        }
        if (hasBucket) {
            __ffSetState('bucket', 'has bucket');
            return;
        }
        const bucket = __ffNearest(__ffBuckets.map(function(p) {
            return { x: p[0], y: p[1], z: p[2] };
        }));
        if (bucket) __ffTp(bucket.x, bucket.y, bucket.z);
        __ffWait(500);
        __ffRemote('s:farm:bucket');
        __ffRemote('server_farm_milk_toggleBucket');
        __ffSetState('bucket', 'take bucket');
        __ffWait(900);
        return;
    }
    if (__ff.state === 'bucket') {
        if (filled) {
            __ffSetState('filled', 'filled');
            return;
        }
        if (now - __ff.lastInteract < 5500) {
            __ffSetState('bucket', 'antiflood');
            return;
        }
        const cow = __ffPickCow();
        if (!cow) {
            __ffSetState('bucket', 'no free cow');
            __ffWait(1500);
            return;
        }
        __ff.cow = cow.i;
        __ffTp(cow.x, cow.y, cow.z);
        __ffSetState('interact', 'cow ' + cow.i);
        __ffWait(650);
        return;
    }
    if (__ff.state === 'interact') {
        __ff.lastInteract = now;
        __ffRemote('s:farm:cowInteract', __ff.cow);
        __ffRemote('server_farm_milk_start', __ff.cow);
        __ffSetState('minigame', 'waiting milk');
        __ffWait(700);
        return;
    }
    if (__ff.state === 'minigame') {
        __ffRemote('s:farm:milk:game:end', __ff.cow, true);
        __ffRemote('s:farm:milk:game:end', __ff.cow, true, false);
        __ffWait(900);
        if (filled || __ffFarmFlag('isBucketFilled'))
            __ffSetState('filled', 'minigame done');
        return;
    }
    if (__ff.state === 'filled') {
        const tank = __ffNearest(__ffTanks.map(function(p) {
            return { x: p[0], y: p[1], z: p[2] };
        }));
        if (tank) __ffTp(tank.x, tank.y, tank.z);
        __ffWait(500);
        __ffRemote('s:farm:milk:end');
        __ffRemote('server_farm_milk_end');
        __ffRemote('server_farm_milk_take');
        __ffSetState('deliver', 'pour milk');
        __ffWait(1100);
        return;
    }
    if (__ff.state === 'deliver') {
        if (!filled && !__ffFarmFlag('isBucketFilled'))
            __ffSetState('job', 'cycle done');
        else __ffSetState('filled', 'still filled');
        __ffWait(400);
    }
}

)FIVEFARM1"
R"FIVEFARM2(
function __ffStart() {
    __ff.active = true;
    __ff.cycles = 0;
    __ff.cow = -1;
    __ffSetState('idle', 'starting');
    if (__ff.invis) __ffApplyInvis(true);
}
function __ffStop() {
    __ff.active = false;
    __ffSetState('idle', 'stopped');
    if (__ff.invis) __ffApplyInvis(false);
}
global.__fiveFarmCommand = function(command) {
    const name = String(command || '');
    if (name === 'start') __ffStart();
    else if (name === 'stop') __ffStop();
    else if (name === 'invis_on') {
        __ff.invis = true;
        __ffApplyInvis(true);
        __ff.message = 'invis on';
    } else if (name === 'invis_off') {
        __ff.invis = false;
        __ffApplyInvis(false);
        __ff.message = 'invis off';
    } else if (name === 'tp_on') {
        __ff.teleport = true;
        __ff.message = 'teleport on';
    } else if (name === 'tp_off') {
        __ff.teleport = false;
        __ff.message = 'teleport off';
    } else if (name === 'invis_pulse') {
        __ffApplyInvis(!!__ff.invis);
    }
    return global.__fiveFarmStatus();
};
global.__fiveFarmStatus = function() {
    const farm = __ffFindFarm();
    let cows = 0;
    try {
        cows = farm && farm.cows && farm.cows.cows
            ? farm.cows.cows.length : __ffCows.length;
    } catch (_) { cows = __ffCows.length; }
    return [
        'FARM',
        __ff.active ? 1 : 0,
        __ff.state,
        __ff.cow,
        __ff.cycles,
        __ff.invis ? 1 : 0,
        __ff.teleport ? 1 : 0,
        __ffFarmFlag('isOnJob') ? 1 : 0,
        __ffFarmFlag('hasBucket') ? 1 : 0,
        __ffFarmFlag('isBucketFilled') ? 1 : 0,
        farm ? 1 : 0,
        cows,
        String(__ff.message || '').replace(/[\t\r\n]/g, ' ')
    ].join('\t');
};
global.__fiveFarmUnhook = function() {
    try { if (__ff.timer) clearInterval(__ff.timer); } catch (_) {}
    __ff.timer = null;
    __ff.active = false;
    __ff.listeners.forEach(function(pair) {
        try { mp.events.remove(pair[0], pair[1]); } catch (_) {}
    });
    __ff.listeners.length = 0;
    try { __ffApplyInvis(false); } catch (_) {}
    try { delete global.__fiveFarmCommand; } catch (_) {}
    try { delete global.__fiveFarmStatus; } catch (_) {}
};
__ff.timer = setInterval(__ffTick, 250);
return 'five-farm-ready';
)FIVEFARM2";
}
