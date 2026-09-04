#pragma once

namespace UkraineToolsScript
{
    // Embedded Ukraine GTA client bridge. Uses plain mp.events.callRemote (no token).
    // Split raw strings — MSVC C2026 limit on a single literal.
    inline constexpr const char* kSource =
        R"UAJS((function () {
    if (global.__uaLoaded) return 'UA_ALREADY_LOADED';
    global.__uaLoaded = true;
    global.__uaStatus = 'ready';
    global.__uaLast = '';

    function log(msg) {
        try { global.__uaLast = String(msg || ''); } catch (e) {}
    }

    function rem() {
        var args = Array.prototype.slice.call(arguments);
        try {
            mp.events.callRemote.apply(mp.events, args);
            log('sent: ' + args[0]);
            return 'ok:' + args[0];
        } catch (e) {
            log('err: ' + e);
            return 'err:' + e;
        }
    }

    function localPos() {
        try {
            var p = mp.players.local.position;
            return { x: p.x, y: p.y, z: p.z };
        } catch (e) {
            return { x: 0, y: 0, z: 0 };
        }
    }

    function nearestPlayerId() {
        try {
            var me = mp.players.local;
            var best = null, bestD = 8;
            mp.players.forEach(function (p) {
                if (!p || p === me) return;
                try {
                    var d = mp.game.system.vdist(
                        me.position.x, me.position.y, me.position.z,
                        p.position.x, p.position.y, p.position.z);
                    if (d < bestD) { bestD = d; best = p; }
                } catch (e) {}
            });
            return best ? best.remoteId : -1;
        } catch (e) { return -1; }
    }

    function parseArgsJson(raw) {
        if (!raw) return [];
        try {
            var v = JSON.parse(raw);
            if (Array.isArray(v)) return v;
            return [v];
        } catch (e) {
            return [raw];
        }
    }

    // ---------------- CEF dump (Ukraine) ----------------
    var cefDumpBuffers = {};
    var cefDumpFiles = [];
    var cefDumpInFlight = 0;
    var cefDumpCursor = null;
    var cefDumpUrls = {};
    var cefDumpForceReady = false;

    function uaCefSanitizeName(name) {
        return String(name || 'file')
            .replace(/[<>:"|?*\x00-\x1f]/g, '_')
            .replace(/\\/g, '/')
            .slice(0, 140) || 'file';
    }

    function uaCefStoreFile(name, data) {
        var n = uaCefSanitizeName(name);
        cefDumpFiles.push({ name: n, data: String(data == null ? '' : data) });
    }

    function uaCefMakeHtmlDumpScript(name) {
        var n = String(name).replace(/\\/g, '\\\\').replace(/'/g, "\\'");
        return "(function(){try{"
            + "var data=document.documentElement?document.documentElement.outerHTML:String(document.body&&document.body.innerHTML||'');"
            + "var cs=25000;var total=Math.max(1,Math.ceil(data.length/cs));"
            + "mp.trigger('ua:cefDumpStart','" + n + "',String(total),String(data.length));"
            + "for(var i=0;i<total;i++){(function(idx){setTimeout(function(){"
            + "mp.trigger('ua:cefDumpChunk','" + n + "',String(idx),data.substring(idx*cs,(idx+1)*cs));"
            + "},idx*100);})(i);}"
            + "}catch(e){mp.trigger('ua:cefDumpDone','" + n + ":error:'+e);}})()";
    }

    function uaCefMakeScriptsJsonDumpScript(streamName) {
        var n = String(streamName || 'scripts')
            .replace(/\\/g, '\\\\').replace(/'/g, "\\'");
        return "(function(){try{"
            + "var result=[];var tags=document.querySelectorAll('script');"
            + "var pending=0,done=0;"
            + "function sendChunked(){"
            + "var data=JSON.stringify(result);"
            + "var cs=25000;var total=Math.max(1,Math.ceil(data.length/cs));"
            + "mp.trigger('ua:cefDumpStart','" + n + "',String(total),String(data.length));"
            + "for(var i=0;i<total;i++){(function(idx){setTimeout(function(){"
            + "mp.trigger('ua:cefDumpChunk','" + n + "',String(idx),data.substring(idx*cs,(idx+1)*cs));"
            + "},idx*100);})(i);}"
            + "}"
            + "for(var i=0;i<tags.length;i++){var tag=tags[i];"
            + "if(tag.src){pending++;(function(url){fetch(url).then(function(r){return r.text()}).then(function(t){"
            + "result.push({src:url,code:t});done++;if(done>=pending)sendChunked();"
            + "}).catch(function(){result.push({src:url,code:'FETCH_ERROR'});done++;if(done>=pending)sendChunked();});})(tag.src);}"
            + "else if(tag.textContent&&tag.textContent.trim()){result.push({src:'inline_'+i,code:tag.textContent});}}"
            + "if(!pending)sendChunked();"
            + "}catch(e){mp.trigger('ua:cefDumpDone','scripts:error:'+e);}})()";
    }

    function uaCefMakeLogicProbeScript() {
        return "(function(){try{"
            + "var info={href:String(location&&location.href||''),title:String(document.title||'')};"
            + "var names=['selectMenu','inventory','inventoryState','playerMenu','modal','speedometer',"
            + "'handlingEditor','vue','Vue','app','store','__vue__','plantsExchangeState','mushroomExchangeState',"
            + "'treasureExchangeState','fishing','browserMenu'];"
            + "info.globals={};"
            + "for(var i=0;i<names.length;i++){var k=names[i];try{info.globals[k]=typeof window[k];}catch(e){info.globals[k]='err';}}"
            + "try{if(window.store&&store.state){info.vuexKeys=Object.keys(store.state);} }catch(e){}"
            + "try{if(window.__VUE__){}}catch(e){}"
            + "var scripts=[];var tags=document.querySelectorAll('script');"
            + "for(var s=0;s<tags.length;s++){scripts.push({src:tags[s].src||('inline_'+s),len:(tags[s].textContent||'').length});}"
            + "info.scripts=scripts;"
            + "var hay='';"
            + "try{hay=document.documentElement?document.documentElement.outerHTML:'';}catch(e){}"
            + "var needles=['item.add','item.ground','callRemote','inventory.','mp.trigger','plantsExchange','mushroomGrower','treasureHunter','fishing.'];"
            + "info.hits={};"
            + "for(var n=0;n<needles.length;n++){var q=needles[n];var idx=hay.indexOf(q);info.hits[q]=idx>=0?idx:-1;}"
            + "var data=JSON.stringify(info,null,2);"
            + "var cs=25000;var total=Math.max(1,Math.ceil(data.length/cs));"
            + "mp.trigger('ua:cefDumpStart','logic_probe.json',String(total),String(data.length));"
            + "for(var i=0;i<total;i++){(function(idx){setTimeout(function(){"
            + "mp.trigger('ua:cefDumpChunk','logic_probe.json',String(idx),data.substring(idx*cs,(idx+1)*cs));"
            + "},idx*80);})(i);}"
            + "}catch(e){mp.trigger('ua:cefDumpDone','logic:error:'+e);}})()";
    }

    function uaCefMakeSearchScript(term) {
        var t = String(term || '').replace(/\\/g, '\\\\').replace(/'/g, "\\'");
        return "(function(){try{"
            + "var term='" + t + "';"
            + "var results=[];var tags=document.querySelectorAll('script');"
            + "var pending=0,done=0;"
            + "function finish(){"
            + "var data=JSON.stringify({term:term,results:results});"
            + "var cs=25000;var total=Math.max(1,Math.ceil(data.length/cs));"
            + "mp.trigger('ua:cefDumpStart','search_result.json',String(total),String(data.length));"
            + "for(var i=0;i<total;i++){(function(idx){setTimeout(function(){"
            + "mp.trigger('ua:cefDumpChunk','search_result.json',String(idx),data.substring(idx*cs,(idx+1)*cs));"
            + "},idx*80);})(i);}"
            + "}"
            + "function scan(src,code){"
            + "var c=String(code||'');var pos=0,hits=0,snips=[];"
            + "while(hits<8){var i=c.indexOf(term,pos);if(i<0)break;"
            + "snips.push(c.substring(Math.max(0,i-80),Math.min(c.length,i+120)));"
            + "pos=i+term.length;hits++;}"
            + "if(hits)results.push({src:src,hits:hits,snippets:snips});"
            + "}"
            + "for(var i=0;i<tags.length;i++){var tag=tags[i];"
            + "if(tag.src){pending++;(function(url){fetch(url).then(function(r){return r.text()}).then(function(t){"
            + "scan(url,t);done++;if(done>=pending)finish();"
            + "}).catch(function(){done++;if(done>=pending)finish();});})(tag.src);}"
            + "else scan('inline_'+i,tag.textContent||'');}"
            + "if(!pending)finish();"
            + "}catch(e){mp.trigger('ua:cefDumpDone','search:error:'+e);}})()";
    }

    function uaCefExpandScriptsJson(jsonText, prefix) {
        var arr;
        var pre = prefix ? (String(prefix).replace(/\/$/, '') + '/') : '';
        try { arr = JSON.parse(jsonText); }
        catch (e) {
            uaCefStoreFile(pre + 'scripts.raw.json', String(jsonText || ''));
            uaCefStoreFile(pre + 'scripts_parse.error.txt', String(e));
            return;
        }
        if (!Array.isArray(arr)) {
            uaCefStoreFile(pre + 'scripts.raw.json', String(jsonText || ''));
            return;
        }
        var PART = 480000;
        var partIdx = 0;
        var indexLines = ['# CEF Bundle Index', ''];
        for (var i = 0; i < arr.length; i++) {
            var src = (arr[i] && arr[i].src != null) ? String(arr[i].src) : ('inline_' + i);
            var code = String((arr[i] && arr[i].code != null) ? arr[i].code : '');
            if (!code.length) continue;
            var base = 'script.js';
            try {
                var u = src.split('?')[0];
                var slash = u.lastIndexOf('/');
                base = (slash >= 0 ? u.substring(slash + 1) : u)
                    .replace(/[<>:"/\\|?*\x00-\x1f]/g, '_')
                    .slice(0, 72) || 'script.js';
            } catch (e) {}
            if (code.length > PART) {
                var parts = Math.ceil(code.length / PART);
                for (var p = 0; p < parts; p++) {
                    var name = pre + 'bundle/part_' + ('000' + partIdx).slice(-3) + '.js';
                    var slice = code.substring(p * PART, (p + 1) * PART);
                    uaCefStoreFile(name, slice);
                    indexLines.push(name + ' (' + Math.round(slice.length / 1024) + 'KB)');
                    partIdx++;
                }
            } else {
                var nm;
                if (src.indexOf('inline_') === 0) nm = pre + i + '_' + src + '.js';
                else {
                    nm = pre + i + '_' + base;
                    if (!/\.js$/i.test(nm)) nm += '.js';
                }
                uaCefStoreFile(nm, code);
            }
        }
        if (partIdx > 0)
            uaCefStoreFile(pre + 'bundle/INDEX.md', indexLines.join('\n'));
        log('CEF scripts expanded files=' + cefDumpFiles.length);
    }

    function uaCefStartNamedDump(name, starter) {
        var n = uaCefSanitizeName(name);
        cefDumpInFlight++;
        cefDumpForceReady = false;
        try { starter(n); }
        catch (e) {
            cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
            uaCefStoreFile(n + '.error.txt', String(e));
        }
    }

    function uaCefExecuteOnBrowser(browser, code) {
        if (!browser || typeof browser.execute !== 'function') return false;
        try { browser.execute(code); return true; } catch (e) { return false; }
    }

    function uaCefExecuteCallCefv(code) {
        try {
            if (typeof mp.callCEFV === 'function') {
                mp.callCEFV(code);
                return true;
            }
        } catch (e) {}
        return false;
    }

    function uaCefListBrowsers() {
        var list = [];
        try {
            if (typeof mp !== 'undefined' && mp.browsers && mp.browsers.forEach) {
                var idx = 0;
                mp.browsers.forEach(function (b) {
                    var url = '';
                    try { url = String(b.url || b._url || ''); } catch (e) {}
                    list.push({ index: idx++, url: url, browser: b });
                });
            }
        } catch (e) {}
        return list;
    }

    function uaCefBeginDump(mode) {
        cefDumpBuffers = {};
        cefDumpFiles = [];
        cefDumpCursor = null;
        cefDumpUrls = {};
        cefDumpInFlight = 0;
        cefDumpForceReady = false;

        var wantHtml = mode === 'html' || mode === 'all';
        var wantScripts = mode === 'scripts' || mode === 'all';
        var wantLogic = mode === 'logic' || mode === 'all';

        var browsers = uaCefListBrowsers();
        var jobs = [];

        uaCefStoreFile('00_manifest.json', JSON.stringify({
            startedAt: new Date().toISOString(),
            mode: mode,
            browserCount: browsers.length,
            urls: browsers.map(function (b) { return { index: b.index, url: b.url }; }),
            note: 'Ukraine GTA CEF dump via mp.browsers + callCEFV'
        }, null, 2));

        if (wantHtml || wantScripts) {
            browsers.forEach(function (entry) {
                var base = 'browser_' + (entry.index < 10 ? '0' : '') + entry.index;
                cefDumpUrls[base + '.html'] = entry.url;
                if (wantHtml) {
                    (function (browser, baseName, browserUrl) {
                        jobs.push(function () {
                            uaCefStartNamedDump(baseName + '.html', function (name) {
                                if (!uaCefExecuteOnBrowser(browser, uaCefMakeHtmlDumpScript(name))) {
                                    cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
)UAJS"
        R"UAJS(                                    uaCefStoreFile(name + '.error.txt', 'browser.execute missing\nurl=' + browserUrl);
                                }
                            });
                        });
                    })(entry.browser, base, entry.url);
                }
                if (wantScripts) {
                    (function (browser, baseName, browserUrl) {
                        jobs.push(function () {
                            var stream = baseName + '_scripts';
                            uaCefStartNamedDump(stream, function () {
                                if (!uaCefExecuteOnBrowser(browser, uaCefMakeScriptsJsonDumpScript(stream))) {
                                    cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                                    uaCefStoreFile(stream + '.error.txt', 'browser.execute missing\nurl=' + browserUrl);
                                }
                            });
                        });
                    })(entry.browser, base, entry.url);
                }
            });
        }

        // Main CEF path used by mp.callCEFV (inventory/selectMenu/etc).
        if (wantHtml) {
            jobs.push(function () {
                uaCefStartNamedDump('00_callCEFV.html', function (name) {
                    if (!uaCefExecuteCallCefv(uaCefMakeHtmlDumpScript(name))) {
                        cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                        uaCefStoreFile(name + '.error.txt', 'mp.callCEFV unavailable');
                    }
                });
            });
        }
        if (wantScripts) {
            jobs.push(function () {
                uaCefStartNamedDump('00_callCEFV_scripts', function () {
                    if (!uaCefExecuteCallCefv(uaCefMakeScriptsJsonDumpScript('00_callCEFV_scripts'))) {
                        cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                        uaCefStoreFile('00_callCEFV_scripts.error.txt', 'mp.callCEFV unavailable');
                    }
                });
            });
        }
        if (wantLogic) {
            jobs.push(function () {
                uaCefStartNamedDump('logic_probe.json', function () {
                    var ok = uaCefExecuteCallCefv(uaCefMakeLogicProbeScript());
                    if (!ok && browsers[0])
                        ok = uaCefExecuteOnBrowser(browsers[0].browser, uaCefMakeLogicProbeScript());
                    if (!ok) {
                        cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                        uaCefStoreFile('logic_probe.json.error.txt', 'no CEF execute path');
                    }
                });
            });
        }

        if (!jobs.length) {
            uaCefStoreFile('empty.txt', 'no browsers / nothing to dump');
            cefDumpForceReady = true;
            log('CEF dump empty');
            return 'ok:dump_empty';
        }

        var jobIndex = 0;
        function runNextJob() {
            if (jobIndex >= jobs.length) {
                log('CEF dump queue drained jobs=' + jobs.length);
                return;
            }
            if (cefDumpInFlight > 0) {
                setTimeout(runNextJob, 750);
                return;
            }
            try { jobs[jobIndex++](); }
            catch (e) {
                uaCefStoreFile('job_' + jobIndex + '.error.txt', String(e));
                jobIndex++;
            }
            setTimeout(runNextJob, 1000);
        }
        setTimeout(runNextJob, 200);
        log('CEF dump queued mode=' + mode + ' jobs=' + jobs.length + ' browsers=' + browsers.length);
        return 'ok:dump_' + mode + ':' + jobs.length;
    }

    try {
        mp.events.add('ua:cefDumpStart', function (name, totalStr, sizeStr) {
            var total = parseInt(totalStr, 10) || 0;
            var size = parseInt(sizeStr, 10) || 0;
            cefDumpBuffers[String(name)] = {
                chunks: new Array(Math.max(0, total)),
                total: Math.max(0, total),
                received: 0,
                size: size
            };
            log('CEF dump start ' + name + ' size=' + size);
        });
        mp.events.add('ua:cefDumpChunk', function (name, idxStr, chunk) {
            var key = String(name);
            var buf = cefDumpBuffers[key];
            if (!buf) return;
            var idx = parseInt(idxStr, 10) || 0;
            if (idx < 0 || idx >= buf.total) return;
            if (buf.chunks[idx] == null) {
                buf.chunks[idx] = String(chunk == null ? '' : chunk);
                buf.received++;
            }
            if (buf.received >= buf.total) {
                var full = buf.chunks.join('');
                delete cefDumpBuffers[key];
                if (key.indexOf('_scripts') >= 0 || (key.indexOf('scripts') >= 0 && full.charAt(0) === '[')) {
                    var prefix = key.replace(/_scripts$/, '').replace(/\.json$/, '');
                    if (prefix === key) prefix = key;
                    uaCefExpandScriptsJson(full, prefix === '00_callCEFV_scripts' ? 'callCEFV' : prefix);
                } else {
                    uaCefStoreFile(key, full);
                }
                cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                if (cefDumpInFlight === 0)
                    log('CEF dump complete files=' + cefDumpFiles.length);
            }
        });
        mp.events.add('ua:cefDumpDone', function (info) {
            log('CEF dump done: ' + info);
            if (String(info || '').indexOf('error') >= 0)
                cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
        });
    } catch (e) {
        log('CEF dump handlers error: ' + e);
    }

    global.__uaCefDumpForce = function () {
        cefDumpForceReady = true;
    };

    global.__uaCefDumpPull = function () {
        if (!cefDumpCursor) cefDumpCursor = { file: 0, offset: 0 };
        if (cefDumpCursor.file >= cefDumpFiles.length) {
            if (cefDumpInFlight > 0 && !cefDumpForceReady)
                return 'CEF_BUSY:' + cefDumpInFlight + ':' + cefDumpFiles.length;
            var total = cefDumpFiles.length;
            cefDumpCursor = null;
            cefDumpForceReady = false;
            return 'CEF_IDLE:' + total;
        }
        var entry = cefDumpFiles[cefDumpCursor.file];
        var start = cefDumpCursor.offset;
        var rawBudget = 20000;
        var chunk = entry.data.slice(start, start + rawBudget);
        var packed;
        if (/[\t\r\n\x00]/.test(chunk)) {
            chunk = entry.data.slice(start, start + 4000);
            packed = 'E' + encodeURIComponent(chunk);
        } else {
            packed = 'R' + chunk;
        }
        cefDumpCursor.offset += chunk.length;
        var complete = cefDumpCursor.offset >= entry.data.length ? 1 : 0;
        var safeName = encodeURIComponent(entry.name);
        var result = [
            'CEF_CHUNK',
            cefDumpCursor.file,
            safeName,
            start,
            entry.data.length,
            complete,
            packed
        ].join('\t');
        if (complete) {
            try { cefDumpFiles[cefDumpCursor.file].data = ''; } catch (e) {}
            cefDumpCursor.file++;
            cefDumpCursor.offset = 0;
        }
        return result;
    };
)UAJS"
        R"UAJS(
    // ---------------- Woodman bot ----------------
    var woodmanBot = {
        on: false,
        phase: 'idle',
        logsAddJson: null,
        hitSlot: 0,
        hitCountOnSlot: 0,
        // Log slot HP tracking (mp.woodman.logSquats): retry if hit missed.
        awaitHitSlot: -1,
        lastHitHealth: -1,
        missStreak: 0,
        lastWood: -1,
        lastWeight: -1,
        busyUntil: 0,
        timerId: null,
        probePending: false,
        probeAt: 0,
        lootHint: false,
        waitLootSince: 0,
        buyAwaitUntil: 0,
        sellCooldown: 0,
        buyCooldown: 0,
        overweight: false,
        lastMsg: '',
        returnPos: null,
        resumePhase: 'hitting',
        millAction: null,
        millActionDone: false,
        tpPath: null,
        tpIndex: 0,
        workPos: null,
        workUnderPos: null,
        millUnderPos: null,
        // Server checks 3D dist to tree/log ("Ви не біля дерева").
        // Keep under offset small so freeze-hide still passes the check.
        underZ: 1.6,
        // Shallow sink under mill marker for sell/buy (not deep -50).
        millUnderZ: 1.2,
        dropCooldown: 0
    };
    global.__uaInv = null;
    global.__uaWoodman = woodmanBot;

    function woodmanStatus(msg) {
        woodmanBot.lastMsg = String(msg || '');
        log('woodman: ' + woodmanBot.lastMsg);
    }

    // mp.callCEFV strips mp.trigger(...) via regex before browser.execute.
    // Use mp['trigger'] + direct browser.execute so the reply reaches client.
    function uaInvSendResult(payload) {
        var s = '';
        try { s = JSON.stringify(payload); } catch (e) {
            s = '{"error":"stringify"}';
        }
        try { mp.events.call('ua:invProbe', s); } catch (e0) {}
    }

    function uaInvProbe() {
        woodmanBot.probePending = true;
        woodmanBot.probeAt = Date.now();
        // Live inventory is Vuex inventoryState (window.app.$store),
        // not the legacy Vue `inventory` object (usually empty → 0/20).
        // mp.callCEFV strips mp.trigger(...); use mp['trigger'].
        var code = "(function(){try{"
            + "var send=function(p){var s;try{s=JSON.stringify(p);}catch(e){s='{\"error\":\"stringify\"}';}"
            + "try{mp['trigger']('ua:invProbe',s);}catch(e1){}};"
            + "var store=null;"
            + "try{store=window.app&&window.app.config&&window.app.config.globalProperties"
            + "&&window.app.config.globalProperties.$store;}catch(eS){}"
            + "var st=store&&store.state&&store.state.inventoryState;"
            + "if(!st){send({error:'no inventoryState',via:'vuex'});return;}"
            + "var weight=0;"
            + "try{var gw=store.getters['inventoryState/mainEquippedWeight'];"
            + "weight=Number(typeof gw==='function'?gw():gw);if(isNaN(weight))weight=0;}catch(eW){}"
            + "var maxW=20;try{maxW=Number(st.config&&st.config.maxWeight)||20;}catch(eM){}"
            + "function collectMap(map,ids,out){"
            + "if(!map)return;"
            + "for(var k in map){var it=map[k];if(!it)continue;"
            + "if(ids.indexOf(Number(it.itemId))>=0)out.push(it);}}"
            + "var axeIds=[70,76],axes=[];"
            + "collectMap(st.mainItems,axeIds,axes);"
            + "collectMap(st.bodyItems,axeIds,axes);"
            + "if(st.bag)collectMap(st.bag.items,axeIds,axes);"
            + "var best=null,bestH=-1;"
            + "for(var i=0;i<axes.length;i++){var a=axes[i];if(!a)continue;"
            + "var h=100;try{if(a.params&&a.params.health!=null)h=Number(a.params.health);}catch(eH){}"
            + "if(isNaN(h))h=100;if(h>bestH){bestH=h;best=a;}}"
            + "try{var hands=st.bodyItems&&st.bodyItems[12];"
            + "if(hands&&axeIds.indexOf(Number(hands.itemId))>=0){"
            + "var hh=100;if(hands.params&&hands.params.health!=null)hh=Number(hands.params.health);"
            + "if(!isNaN(hh)){best=hands;bestH=hh;}"
            + "}}catch(e5){}"
            + "var woods=[];collectMap(st.mainItems,[131],woods);"
            + "collectMap(st.bodyItems,[131],woods);"
            + "if(st.bag)collectMap(st.bag.items,[131],woods);"
            + "var woodCount=0;"
            + "for(var j=0;j<woods.length;j++){var w=woods[j];if(!w)continue;"
            + "woodCount+=(w.params&&w.params.count!=null)?Number(w.params.count):1;}"
            + "if(isNaN(woodCount))woodCount=woods.length;"
            + "var mainN=0;try{for(var mk in st.mainItems)if(st.mainItems[mk])mainN++;}catch(eN){}"
            + "send({weight:weight,maxWeight:maxW,"
            + "axeSqlId:best?best.sqlId:null,"
            + "axeHealth:best?bestH:null,"
            + "axeItemId:best?best.itemId:null,"
            + "woodCount:woodCount,axeCount:axes.length,"
            + "mainCount:mainN,via:'vuex'});"
            + "}catch(e){try{mp['trigger']('ua:invProbe','{\"error\":\"'+String(e).replace(/\"/g,'')+'\"}');}catch(x){}}})()";

        var sent = uaCefExecuteCallCefv(code);
        if (!sent) {
            try {
                var browsers = uaCefListBrowsers();
                for (var bi = 0; bi < browsers.length; bi++) {
                    if (uaCefExecuteOnBrowser(browsers[bi].browser, code))
                        sent = true;
                }
            } catch (eB) {}
        }
        if (!sent) {
            woodmanBot.probePending = false;
            global.__uaInv = { error: 'cef execute unavailable' };
            uaInvSendResult(global.__uaInv);
        }
        try {
            setTimeout(function () {
                if (woodmanBot.probePending &&
                    Date.now() - woodmanBot.probeAt >= 2500) {
                    woodmanBot.probePending = false;
                    if (!global.__uaInv || global.__uaInv.error) {
                        global.__uaInv = global.__uaInv || {
                            error: 'inv probe timeout'
                        };
                        log('woodman inv: timeout (no CEF reply)');
                    }
                }
            }, 2600);
        } catch (eT) {}
    }

    try {
        mp.events.add('ua:invProbe', function (json) {
            try {
                var parsed =
                    typeof json === 'string' ? JSON.parse(json) : json;
                // Ignore late errors from a non-main browser.
                if (parsed &&
                    parsed.error &&
                    global.__uaInv &&
                    !global.__uaInv.error &&
                    global.__uaInv.via === 'vuex') {
                    woodmanBot.probePending = false;
                    return;
                }
                global.__uaInv = parsed;
                if (parsed && !parsed.error) {
                    log('woodman inv: w=' + parsed.weight + '/' +
                        parsed.maxWeight + ' axe=' + parsed.axeSqlId +
                        ' hp=' + parsed.axeHealth +
                        ' wood=' + parsed.woodCount +
                        ' main=' + parsed.mainCount +
                        ' via=' + parsed.via);
                } else if (parsed && parsed.error) {
                    log('woodman inv error: ' + parsed.error);
                }
            } catch (e) {
                global.__uaInv = { error: String(json) };
            }
            woodmanBot.probePending = false;
        });
    } catch (e) {}

    try {
        mp.events.add('inventory.setOverweight', function (flag) {
            woodmanBot.overweight = !!flag;
        });
    } catch (e) {}

    try {
        mp.events.add('inventory.items.addAnswer', function () {
            if (woodmanBot.on && woodmanBot.phase === 'wait_loot')
                woodmanBot.lootHint = true;
        });
    } catch (e) {}

    function woodmanHookCallRemote() {
        if (mp.events.callRemote.__uaWoodHooked) return;
        var orig = mp.events.callRemote.bind(mp.events);
        mp.events.callRemote = function () {
            try {
                if (woodmanBot.on &&
                    woodmanBot.phase === 'wait_capture' &&
                    arguments[0] === 'woodman.logs.add' &&
                    arguments[1]) {
                    woodmanBot.logsAddJson = String(arguments[1]);
                    woodmanBot.hitSlot = 0;
                    woodmanBot.hitCountOnSlot = 0;
                    woodmanBot.awaitHitSlot = -1;
                    woodmanBot.lastHitHealth = -1;
                    woodmanBot.missStreak = 0;
                    try {
                        var cap = woodmanGetPos();
                        var anchor = cap;
                        // Prefer log spawn from logs.add payload / live log obj.
                        try {
                            var slot = JSON.parse(String(arguments[1]));
                            if (slot && slot.pos) {
                                anchor = {
                                    x: Number(slot.pos.x),
                                    y: Number(slot.pos.y),
                                    z: Number(slot.pos.z)
                                };
                            }
                        } catch (eSlot) {}
                        try {
                            if (mp.woodman && mp.woodman.logObj &&
                                mp.objects.exists(mp.woodman.logObj)) {
                                var lp = mp.woodman.logObj.position;
                                anchor = { x: lp.x, y: lp.y, z: lp.z };
                            }
                        } catch (eLog) {}
                        if (cap) {
                            woodmanBot.workPos = {
                                x: cap.x, y: cap.y, z: cap.z
                            };
                            var uz = woodmanBot.underZ || 1.6;
                            // Stay on player XY (where you chopped), only sink a bit.
                            // Anchoring too far under the log center fails server dist.
                            woodmanBot.workUnderPos = {
                                x: cap.x,
                                y: cap.y,
                                z: (anchor && !isNaN(anchor.z)
                                    ? Math.min(cap.z, anchor.z)
                                    : cap.z) - uz
                            };
                            woodmanBot.returnPos = woodmanBot.workUnderPos;
                            woodmanApplyTp(woodmanBot.workUnderPos, true);
                            woodmanHoldUnder();
                        }
                    } catch (eCap) {}
                    woodmanBot.phase = 'hitting';
                    woodmanBot.busyUntil = Date.now() + 900;
                    woodmanStatus('captured → under work');
                }
            } catch (e) {}
            return orig.apply(null, arguments);
        };
        mp.events.callRemote.__uaWoodHooked = true;
    }
)UAJS"
        R"UAJS(
    function woodmanAxeInHands() {
        try {
            if (mp.woodman && typeof mp.woodman.isAxInHands === 'function')
                return !!mp.woodman.isAxInHands();
        } catch (e) {}
        try {
            var w = mp.players.local.weapon;
            return w === mp.game.joaat('weapon_hatchet') ||
                w === mp.game.joaat('weapon_stone_hatchet');
        } catch (e2) { return false; }
    }

    // Client log HP bars: mp.woodman.logSquats[0..4] (0 = chopped).
    function woodmanGetSquats() {
        try {
            if (!mp.woodman || !mp.woodman.logSquats) return null;
            var sq = mp.woodman.logSquats;
            if (!sq || !sq.length) return null;
            return sq;
        } catch (e) { return null; }
    }

    function woodmanSlotHp(slot) {
        var sq = woodmanGetSquats();
        if (!sq) return -1;
        var h = Number(sq[slot]);
        return isNaN(h) ? 0 : h;
    }

    function woodmanFindAliveSlot(from) {
        var sq = woodmanGetSquats();
        var start = Math.max(0, from | 0);
        if (!sq) return start <= 4 ? start : -1;
        for (var i = start; i < 5; i++) {
            if (Number(sq[i]) > 0) return i;
        }
        return -1;
    }

    function woodmanEnterWaitLoot(now, inv) {
        woodmanBot.phase = 'wait_loot';
        woodmanBot.waitLootSince = now;
        woodmanBot.lootHint = false;
        woodmanBot.awaitHitSlot = -1;
        woodmanBot.lastHitHealth = -1;
        woodmanBot.missStreak = 0;
        woodmanBot.hitSlot = 0;
        woodmanBot.hitCountOnSlot = 0;
        woodmanBot.lastWood =
            inv && inv.woodCount != null ? Number(inv.woodCount) : -1;
        woodmanBot.lastWeight =
            inv && inv.weight != null ? Number(inv.weight) : -1;
        woodmanStatus('wait loot / wood');
        uaInvProbe();
    }

    function woodmanInvFull(inv) {
        if (woodmanBot.overweight) return true;
        if (!inv || inv.error) return false;
        var maxW = Number(inv.maxWeight) || 20;
        var weight = Number(inv.weight) || 0;
        return weight + 1.0 >= maxW;
    }

    function woodmanAxeBroken(inv) {
        return !!(inv &&
            !inv.error &&
            inv.axeSqlId != null &&
            inv.axeHealth != null &&
            Number(inv.axeHealth) <= 1);
    }

    function woodmanNeedAxe(inv) {
        if (!inv || inv.error) {
            return !woodmanAxeInHands();
        }
        if (woodmanAxeBroken(inv)) return true;
        if (!woodmanAxeInHands()) {
            // Have a healthy axe in inv — just equip, don't buy.
            if (inv.axeSqlId != null && Number(inv.axeHealth || 0) > 1)
                return false;
            return true;
        }
        return false;
    }

    function woodmanEquipAxe(sqlId) {
        if (!sqlId) return false;
        // Hands = bodyItems[12]; CEF callRemote JSON.stringifies the payload.
        rem('item.add', JSON.stringify({
            sqlId: Number(sqlId),
            index: 12,
            placeSqlId: null,
            pocketI: null
        }));
        return true;
    }

    function woodmanDropPos() {
        var p = woodmanGetPos() || woodmanBot.workUnderPos || woodmanBot.workPos;
        if (!p) return null;
        var heading = 0;
        try { heading = mp.players.local.getHeading(); } catch (e) {}
        try {
            var rad = (heading * Math.PI) / 180;
            return {
                x: p.x - Math.sin(rad),
                y: p.y + Math.cos(rad),
                z: p.z,
                heading: heading
            };
        } catch (e2) {
            return {
                x: p.x, y: p.y, z: p.z, heading: heading
            };
        }
    }

    // item.ground.put(sqlId, jsonPos) — throw item on ground at coords.
    function woodmanDropAxe(sqlId) {
        if (!sqlId) return false;
        var drop = woodmanDropPos();
        if (!drop) return false;
        rem('item.ground.put', sqlId, JSON.stringify(drop));
        woodmanStatus('drop axe sqlId=' + sqlId);
        return true;
    }

    function woodmanSetFrozen(freeze) {
        try {
            var p = mp.players.local;
            p.freezePosition(!!freeze);
            try { p.setCollision(!freeze, !freeze); } catch (e0) {}
            if (freeze) {
                try { p.setVelocity(0, 0, 0); } catch (e1) {}
            }
        } catch (e) {}
    }

    // Keep pinned under a point — physics otherwise drops you through the map.
    function woodmanHoldPos(pos) {
        if (!pos) return;
        try {
            var p = mp.players.local;
            try { mp.events.call('ac:refreshtp'); } catch (e0) {}
            p.freezePosition(true);
            try { p.setCollision(false, false); } catch (e1) {}
            p.setCoordsNoOffset(
                pos.x, pos.y, pos.z, false, false, false);
            try { p.setVelocity(0, 0, 0); } catch (e2) {}
        } catch (e) {}
    }

    function woodmanHoldUnder() {
        woodmanHoldPos(woodmanBot.workUnderPos);
    }

    function woodmanHoldMill() {
        woodmanHoldPos(
            woodmanBot.millUnderPos || woodmanMillUnderPos());
    }

    function woodmanRelease() {
        woodmanSetFrozen(false);
    }

    function woodmanGetPos() {
        try {
            var p = mp.players.local.position;
            return { x: p.x, y: p.y, z: p.z };
        } catch (e) {
            return null;
        }
    }

    function woodmanMillPos() {
        try {
            if (mp.locator && typeof mp.locator.getNeighborLocation === 'function') {
                var place = mp.locator.getNeighborLocation('jobs', 'woodman');
                if (place && place.position) {
                    return {
                        x: place.position.x,
                        y: place.position.y,
                        z: place.position.z
                    };
                }
            }
        } catch (e) {}
        // Fallback: job ped spawn
        return { x: -567.28, y: 5254.63, z: 70.47 };
    }

    function woodmanMillUnderPos() {
        var mill = woodmanMillPos();
        var uz = woodmanBot.millUnderZ || 1.2;
        return {
            x: mill.x,
            y: mill.y,
            z: mill.z - uz
        };
    }

    // XY-only near check (Z ignored — we sit shallow under markers).
    function woodmanNearXY(pos, maxDist) {
        var p = woodmanGetPos();
        if (!p || !pos) return false;
        var dx = p.x - pos.x;
        var dy = p.y - pos.y;
        var m = maxDist || 12;
        return dx * dx + dy * dy <= m * m;
    }

    function woodmanBuildPath(from, to) {
        var path = [];
        if (!from || !to) return path;
        var STEP = 500;
        var dx = to.x - from.x;
        var dy = to.y - from.y;
        var dist = Math.sqrt(dx * dx + dy * dy);
        if (dist <= STEP) {
            path.push({ x: to.x, y: to.y, z: to.z, final: true });
            return path;
        }
        var steps = Math.ceil(dist / STEP);
        for (var i = 1; i <= steps; i++) {
            var t = i / steps;
            var last = i === steps;
            path.push({
                x: from.x + dx * t,
                y: from.y + dy * t,
                z: last ? to.z : -50,
                final: last
            });
        }
        return path;
    }

    function woodmanApplyTp(pt, pinUnder) {
        if (!pt) return;
        try {
            try { mp.events.call('ac:refreshtp'); } catch (e0) {}
            var p = mp.players.local;
            if (pinUnder) {
                p.freezePosition(true);
                try { p.setCollision(false, false); } catch (e1) {}
            }
            p.setCoordsNoOffset(
                pt.x, pt.y, pt.z, false, false, false);
            if (pinUnder) {
                try { p.setVelocity(0, 0, 0); } catch (e2) {}
            }
        } catch (e) {}
    }

)UAJS"
        R"UAJS(
    function woodmanStartTrip(action) {
        var millUnder = woodmanMillUnderPos();
        woodmanBot.millUnderPos = millUnder;
        // Always return under the captured work point.
        woodmanBot.returnPos =
            woodmanBot.workUnderPos || woodmanGetPos();
        woodmanBot.resumePhase =
            woodmanBot.logsAddJson ? 'hitting' : 'wait_capture';
        woodmanBot.millAction = action;
        woodmanBot.millActionDone = false;
        woodmanRelease();
        // Sell/buy while pinned shallow under mill marker.
        if (woodmanNearXY(millUnder, 14)) {
            woodmanApplyTp(millUnder, true);
            woodmanHoldMill();
            woodmanBot.phase = 'at_mill';
            woodmanBot.busyUntil = Date.now() + 400;
            woodmanStatus('under mill → ' + action);
            return;
        }
        woodmanBot.tpPath = woodmanBuildPath(woodmanGetPos(), millUnder);
        woodmanBot.tpIndex = 0;
        woodmanBot.phase = 'tp_mill';
        woodmanStatus('TP under mill (' + action + ')');
    }

    function woodmanStartReturn() {
        var back =
            woodmanBot.workUnderPos ||
            woodmanBot.returnPos;
        if (!back) {
            woodmanBot.phase = woodmanBot.resumePhase || 'hitting';
            woodmanBot.hitSlot = 0;
            woodmanBot.hitCountOnSlot = 0;
            woodmanBot.awaitHitSlot = -1;
            woodmanBot.lastHitHealth = -1;
            woodmanBot.missStreak = 0;
            return;
        }
        // XY near + already under → done
        var p = woodmanGetPos();
        if (p) {
            var dx = p.x - back.x;
            var dy = p.y - back.y;
            if (dx * dx + dy * dy <= 100 && p.z <= back.z + 1.5) {
                woodmanApplyTp(back, true);
                woodmanHoldUnder();
                woodmanBot.phase = woodmanBot.resumePhase || 'hitting';
                woodmanBot.hitSlot = 0;
                woodmanBot.hitCountOnSlot = 0;
                woodmanBot.awaitHitSlot = -1;
                woodmanBot.lastHitHealth = -1;
                woodmanBot.missStreak = 0;
                woodmanStatus('back under work');
                return;
            }
        }
        woodmanRelease();
        woodmanBot.tpPath = woodmanBuildPath(woodmanGetPos(), back);
        woodmanBot.tpIndex = 0;
        woodmanBot.phase = 'tp_back';
        woodmanStatus('TP back under work');
    }
)UAJS"
        R"UAJS(
    function woodmanTick() {
        if (!woodmanBot.on) return;
        var now = Date.now();
        // Pin every tick while working under — even during busy delays.
        if (woodmanBot.workUnderPos &&
            (woodmanBot.phase === 'hitting' ||
             woodmanBot.phase === 'wait_loot')) {
            woodmanHoldUnder();
        }
        if (woodmanBot.millUnderPos &&
            (woodmanBot.phase === 'at_mill' ||
             woodmanBot.phase === 'buying_axe')) {
            woodmanHoldMill();
        }
        if (now < woodmanBot.busyUntil) return;

        var inv = global.__uaInv;
        if (!woodmanBot.probePending && now - woodmanBot.probeAt > 1200)
            uaInvProbe();

        if (woodmanBot.phase === 'wait_capture') {
            woodmanStatus('waiting first logs.add (chop once)');
            return;
        }

        if (woodmanBot.phase === 'tp_mill' ||
            woodmanBot.phase === 'tp_back') {
            var path = woodmanBot.tpPath || [];
            if (woodmanBot.tpIndex < path.length) {
                var pt = path[woodmanBot.tpIndex++];
                var pin = !!pt.final;
                if (!pin) woodmanRelease();
                woodmanApplyTp(pt, pin);
                if (pin) {
                    if (woodmanBot.phase === 'tp_mill')
                        woodmanHoldMill();
                    else
                        woodmanHoldUnder();
                }
                woodmanBot.busyUntil = now + 320;
                woodmanStatus(
                    (woodmanBot.phase === 'tp_mill'
                        ? 'TP under mill'
                        : 'TP back') +
                    ' ' + woodmanBot.tpIndex + '/' + path.length);
                return;
            }
            if (woodmanBot.phase === 'tp_mill') {
                woodmanHoldMill();
                woodmanBot.phase = 'at_mill';
                woodmanBot.busyUntil = now + 600;
                woodmanStatus('under mill');
                return;
            }
            woodmanHoldUnder();
            woodmanBot.phase = woodmanBot.resumePhase || 'hitting';
            woodmanBot.millAction = null;
            woodmanBot.millActionDone = false;
            woodmanBot.hitSlot = 0;
            woodmanBot.hitCountOnSlot = 0;
            woodmanBot.awaitHitSlot = -1;
            woodmanBot.lastHitHealth = -1;
            woodmanBot.missStreak = 0;
            woodmanBot.busyUntil = now + 800;
            woodmanStatus('back under → ' + woodmanBot.phase);
            return;
        }

        if (woodmanBot.phase === 'at_mill') {
            woodmanHoldMill();
            if (!woodmanBot.millActionDone) {
                if (woodmanBot.millAction === 'sell') {
                    if (now < woodmanBot.sellCooldown) return;
                    rem('woodman.items.sell');
                    woodmanBot.sellCooldown = now + 2500;
                    woodmanBot.millActionDone = true;
                    woodmanBot.busyUntil = now + 1600;
                    woodmanStatus('sold under mill');
                    uaInvProbe();
                    return;
                }
                if (woodmanBot.millAction === 'buy') {
                    rem('woodman.items.buy', 0);
                    woodmanBot.buyCooldown = now + 3000;
                    woodmanBot.buyAwaitUntil = now + 1400;
                    woodmanBot.millActionDone = true;
                    woodmanBot.phase = 'buying_axe';
                    woodmanBot.busyUntil = now + 800;
                    woodmanStatus('buy axe under mill');
                    uaInvProbe();
                    return;
                }
            }
            if (woodmanBot.millAction === 'sell') {
                woodmanStartReturn();
                return;
            }
            woodmanBot.busyUntil = now + 400;
            return;
        }

        if (woodmanBot.phase === 'buying_axe') {
            if (now < woodmanBot.buyAwaitUntil) {
                uaInvProbe();
                return;
            }
            inv = global.__uaInv;
            if (inv && inv.axeSqlId != null && Number(inv.axeHealth || 0) > 1) {
                woodmanEquipAxe(inv.axeSqlId);
                woodmanBot.busyUntil = now + 1200;
                woodmanStatus('axe equipped sqlId=' + inv.axeSqlId);
                woodmanStartReturn();
                return;
            }
            if (now >= woodmanBot.buyCooldown) {
                // Still at mill — retry buy without another long TP.
                rem('woodman.items.buy', 0);
                woodmanBot.buyCooldown = now + 3000;
                woodmanBot.buyAwaitUntil = now + 1400;
                woodmanStatus('retry buy axe…');
                uaInvProbe();
            } else {
                uaInvProbe();
                woodmanBot.busyUntil = now + 400;
            }
            return;
        }

        if (inv && !inv.error) {
            if (woodmanInvFull(inv) && now >= woodmanBot.sellCooldown) {
                woodmanStartTrip('sell');
                return;
            }
            // Healthy axe in bags but not in hands → equip under work.
            if (!woodmanAxeInHands() &&
                inv.axeSqlId != null &&
                Number(inv.axeHealth || 0) > 1 &&
                !woodmanAxeBroken(inv)) {
                woodmanEquipAxe(inv.axeSqlId);
                woodmanBot.busyUntil = now + 1200;
                woodmanStatus('equip axe sqlId=' + inv.axeSqlId);
                return;
            }
            if (woodmanNeedAxe(inv) && now >= woodmanBot.buyCooldown) {
                if (woodmanAxeBroken(inv) &&
                    now >= woodmanBot.dropCooldown) {
                    woodmanDropAxe(inv.axeSqlId);
                    woodmanBot.dropCooldown = now + 2000;
                    woodmanBot.busyUntil = now + 900;
                    uaInvProbe();
                    return;
                }
                woodmanStartTrip('buy');
                return;
            }
        }

        if (woodmanBot.phase === 'hitting') {
            if (!woodmanBot.logsAddJson) {
                woodmanBot.phase = 'wait_capture';
                return;
            }
            woodmanHoldUnder();

            // Resolve previous hit vs mp.woodman.logSquats HP.
            if (woodmanBot.awaitHitSlot >= 0) {
                var aSlot = woodmanBot.awaitHitSlot;
                var hpNow = woodmanSlotHp(aSlot);
                var hpBefore = woodmanBot.lastHitHealth;
                if (hpNow >= 0 && hpBefore >= 0) {
                    if (hpNow < hpBefore) {
                        woodmanBot.missStreak = 0;
                        woodmanBot.hitCountOnSlot++;
                        if (hpNow <= 0) {
                            woodmanBot.hitSlot =
                                woodmanFindAliveSlot(aSlot + 1);
                            woodmanBot.hitCountOnSlot = 0;
                            if (woodmanBot.hitSlot < 0) {
                                woodmanEnterWaitLoot(now, inv);
                                return;
                            }
                        }
                    } else {
                        woodmanBot.missStreak++;
                        woodmanStatus(
                            'miss slot ' + aSlot +
                            ' hp=' + hpNow + ' → retry (' +
                            woodmanBot.missStreak + ')');
                        if (woodmanBot.missStreak >= 6) {
                            woodmanBot.missStreak = 0;
                            woodmanBot.hitSlot =
                                woodmanFindAliveSlot(aSlot + 1);
                            woodmanBot.hitCountOnSlot = 0;
                            if (woodmanBot.hitSlot < 0) {
                                woodmanEnterWaitLoot(now, inv);
                                return;
                            }
                        }
                    }
                } else {
                    // No squat sync yet — fall back to 2 hits / slot.
                    woodmanBot.hitCountOnSlot++;
                    if (woodmanBot.hitCountOnSlot >= 2) {
                        woodmanBot.hitCountOnSlot = 0;
                        woodmanBot.hitSlot = aSlot + 1;
                        if (woodmanBot.hitSlot > 4) {
                            woodmanEnterWaitLoot(now, inv);
                            return;
                        }
                    }
                }
                woodmanBot.awaitHitSlot = -1;
            }

            var alive = woodmanFindAliveSlot(woodmanBot.hitSlot);
            if (alive < 0) {
                woodmanEnterWaitLoot(now, inv);
                return;
            }
            woodmanBot.hitSlot = alive;

            woodmanBot.lastHitHealth = woodmanSlotHp(woodmanBot.hitSlot);
            woodmanBot.awaitHitSlot = woodmanBot.hitSlot;
            rem('woodman.logs.hit', woodmanBot.hitSlot);
            woodmanBot.busyUntil = now + 3000;
            woodmanStatus(
                'hitting slot ' + woodmanBot.hitSlot +
                ' hp=' + woodmanBot.lastHitHealth +
                (woodmanBot.missStreak
                    ? (' miss=' + woodmanBot.missStreak)
                    : ''));
            return;
        }

        if (woodmanBot.phase === 'wait_loot') {
            woodmanHoldUnder();
            inv = global.__uaInv;
            var gotLoot = woodmanBot.lootHint;
            if (inv && !inv.error) {
                if (woodmanBot.lastWood >= 0 &&
                    Number(inv.woodCount) > woodmanBot.lastWood)
                    gotLoot = true;
                if (woodmanBot.lastWeight >= 0 &&
                    Number(inv.weight) > woodmanBot.lastWeight + 0.4)
                    gotLoot = true;
            }
            var timedOut = now - woodmanBot.waitLootSince > 20000;
            if (gotLoot || timedOut) {
                rem('woodman.logs.add', woodmanBot.logsAddJson);
                woodmanBot.phase = 'hitting';
                woodmanBot.hitSlot = 0;
                woodmanBot.hitCountOnSlot = 0;
                woodmanBot.awaitHitSlot = -1;
                woodmanBot.lastHitHealth = -1;
                woodmanBot.missStreak = 0;
                woodmanBot.busyUntil = now + 1000;
                woodmanBot.lootHint = false;
                woodmanStatus(
                    (gotLoot ? 'loot ok' : 'loot timeout') + ' → logs.add');
                return;
            }
            woodmanStatus('waiting loot…');
            return;
        }
    }

    function woodmanBotStart() {
        try { if (typeof jobBotsExclusive === 'function') jobBotsExclusive('woodman'); } catch (eEx) {}
        woodmanHookCallRemote();
        woodmanBot.on = true;
        woodmanBot.phase = 'wait_capture';
        woodmanBot.logsAddJson = null;
        woodmanBot.hitSlot = 0;
        woodmanBot.hitCountOnSlot = 0;
        woodmanBot.awaitHitSlot = -1;
        woodmanBot.lastHitHealth = -1;
        woodmanBot.missStreak = 0;
        woodmanBot.busyUntil = 0;
        woodmanBot.lootHint = false;
        woodmanBot.returnPos = null;
        woodmanBot.resumePhase = 'hitting';
        woodmanBot.millAction = null;
        woodmanBot.millActionDone = false;
        woodmanBot.tpPath = null;
        woodmanBot.tpIndex = 0;
        woodmanBot.workPos = null;
        woodmanBot.workUnderPos = null;
        woodmanBot.millUnderPos = null;
        woodmanBot.dropCooldown = 0;
        uaInvProbe();
        if (woodmanBot.timerId != null) {
            try { clearInterval(woodmanBot.timerId); } catch (e) {}
        }
        woodmanBot.timerId = setInterval(woodmanTick, 250);
        woodmanStatus('waiting first logs.add (chop once)');
        return 'ok:woodman_bot_start';
    }

    function woodmanBotStop() {
        woodmanBot.on = false;
        woodmanBot.phase = 'idle';
        woodmanBot.tpPath = null;
        woodmanBot.millAction = null;
        woodmanBot.workPos = null;
        woodmanBot.workUnderPos = null;
        woodmanBot.millUnderPos = null;
        woodmanRelease();
        if (woodmanBot.timerId != null) {
            try { clearInterval(woodmanBot.timerId); } catch (e) {}
            woodmanBot.timerId = null;
        }
        woodmanStatus('stopped');
        return 'ok:woodman_bot_stop';
    }
)UAJS"
        R"UAJS(
    // ---------------- Admin detector (Ukraine) ----------------
    var adminDet = {
        on: false,
        timerId: null,
        known: {},
        spect: {},
        firstScan: true,
        onlineCount: 0,
        nearbyCount: 0,
        nearestId: -1,
        nearestDist: -1,
        nearestName: '',
        nearestLvl: 0,
        spectating: 0,
        onlineList: [],
        nearbyList: [],
        lastMsg: '',
        pauseBusOnNear: true,
        nearPauseM: 80,
        streamM: 300
    };

    function adminSafeName(p, id) {
        try {
            return String(p.name || ('ID' + id))
                .replace(/[\t\r\n]/g, ' ');
        } catch (e) {
            return 'ID' + id;
        }
    }

    // Ukraine nametags use entity shared data "redNick" (NOT adminLevel/admin).
    function adminLvlOf(p) {
        try {
            var v = p.getVariable('redNick');
            if (v == null || v === undefined) {
                v = p.getVariable('adminLevel');
                if (v == null || v === undefined)
                    v = p.getVariable('admin');
            }
            if (v === true) return 1;
            var n = Number(v);
            return isFinite(n) && n > 0 ? n : 0;
        } catch (e) { return 0; }
    }

    function adminScanTick() {
        if (!adminDet.on) return;
        try {
            var me = mp.players.local;
            var myPos = me.position;
            var found = {};
            var bestD = 1e9, bestId = -1, bestName = '', bestLvl = 0;
            var nOnline = 0, nNear = 0, nSpect = 0;
            var onlineList = [];
            var nearbyList = [];
            // mp.players = all clients known on server (same as Quant)
            mp.players.forEach(function (p) {
                if (!p || p === me) return;
                var lvl = adminLvlOf(p);
                if (lvl <= 0) return;
                var id = Number(p.remoteId);
                nOnline++;
                found[id] = 1;
                var name = adminSafeName(p, id);
                onlineList.push({ id: id, name: name, lvl: lvl });
                var dist = 99999;
                var pPos = null;
                try {
                    pPos = p.position;
                    dist = Math.round(mp.game.system.vdist(
                        myPos.x, myPos.y, myPos.z,
                        pPos.x, pPos.y, pPos.z));
                } catch (e1) {}
                var invis = false;
                try {
                    // Ukraine synchronizes admin vanish through isVanished.
                    // `invisible` was an old/incorrect key.
                    invis = !!(
                        p.getVariable('isVanished') ||
                        p.variable_invisible ||
                        p.getVariable('invisible'));
                } catch (e2) {}
                if (!adminDet.spect[id])
                    adminDet.spect[id] = { ticks: 0, alerted: false };
                if (invis && dist < 50) adminDet.spect[id].ticks++;
                else {
                    adminDet.spect[id].ticks = 0;
                    adminDet.spect[id].alerted = false;
                }
                var isSpect = adminDet.spect[id].ticks >= 2;
                if (isSpect) {
                    nSpect++;
                    if (!adminDet.spect[id].alerted) {
                        adminDet.spect[id].alerted = true;
                        log('ADMIN SPECTATE: ' + name +
                            ' lvl=' + lvl + ' dist=' + dist);
                    }
                }
                if (dist < bestD) {
                    bestD = dist; bestId = id; bestLvl = lvl;
                    bestName = name;
                }
                if (pPos && dist <= adminDet.streamM) {
                    nNear++;
                    nearbyList.push({
                        id: id, name: name, lvl: lvl,
                        dist: dist, invis: invis ? 1 : 0,
                        spect: isSpect ? 1 : 0
                    });
                }
                if (!adminDet.known[id] && !adminDet.firstScan) {
                    log('ADMIN JOIN: ' + name + ' lvl=' + lvl +
                        ' dist=' + dist);
                }
                adminDet.known[id] = { name: name, lvl: lvl };
            });
            for (var kid in adminDet.known) {
                if (!found[kid]) {
                    if (!adminDet.firstScan)
                        log('ADMIN LEFT: ' + adminDet.known[kid].name);
                    delete adminDet.known[kid];
                    delete adminDet.spect[kid];
                }
            }
            onlineList.sort(function (a, b) {
                return (b.lvl - a.lvl) || (a.id - b.id);
            });
            nearbyList.sort(function (a, b) {
                return a.dist - b.dist;
            });
            adminDet.firstScan = false;
            adminDet.onlineCount = nOnline;
            adminDet.nearbyCount = nNear;
            adminDet.nearestId = bestId;
            adminDet.nearestDist = bestId >= 0 ? bestD : -1;
            adminDet.nearestName = bestName;
            adminDet.nearestLvl = bestLvl;
            adminDet.spectating = nSpect;
            adminDet.onlineList = onlineList;
            adminDet.nearbyList = nearbyList;
            if (adminDet.pauseBusOnNear && busBot.on &&
                bestId >= 0 && bestD <= adminDet.nearPauseM) {
                if (busBot.phase !== 'paused_admin') {
                    busBot.prevPhase = busBot.phase;
                    busBot.phase = 'paused_admin';
                    busStatus('paused: admin ' + bestName + ' ' + bestD + 'm');
                }
            } else if (busBot.on && busBot.phase === 'paused_admin') {
                busBot.phase = busBot.prevPhase || 'farm';
                busStatus('resume after admin left range');
                if (busBot.style === 'segment_skip' &&
                    !busBot.burstTimerId)
                    busSegmentSkipKick();
            }
        } catch (e) {}
    }

    function adminDetectorStart() {
        adminDet.on = true;
        adminDet.firstScan = true;
        adminDet.known = {};
        adminDet.spect = {};
        adminDet.onlineList = [];
        adminDet.nearbyList = [];
        if (adminDet.timerId != null) {
            try { clearInterval(adminDet.timerId); } catch (e) {}
        }
        adminDet.timerId = setInterval(adminScanTick, 3000);
        adminScanTick();
        log('admin detector ON');
        return 'ok:admin_detector_start';
    }

    function adminDetectorStop() {
        adminDet.on = false;
        if (adminDet.timerId != null) {
            try { clearInterval(adminDet.timerId); } catch (e) {}
            adminDet.timerId = null;
        }
        log('admin detector OFF');
        return 'ok:admin_detector_stop';
    }
)UAJS"
        R"UAJS(
    // ---------------- Bus bot (Okruzhnyi id=6) ----------------
    var BUS_OKRUG_ID = 6;
    var BUS_OKRUG_FARE = 0;
    // segment_skip cycle: lead 18 → stop bursts 10 → 15 → 13 → repeat
    var BUS_OKRUG_LEAD = 18;
    var BUS_OKRUG_STOP_BURSTS = [10, 15, 13];
    var BUS_OKRUG_STOPS = 3;
    var BUS_OKRUG_STOP_IDX = [18, 27, 41, 53];
    var BUS_MARKERS_PER_LAP = 53;
    var BUS_LAP_EPS = 15.0;
    var BUS_LAP_MIN = 4;
    var BUS_UNDER_DEPTH = 50;
    var BUS_SPEED_MS = [200, 100, 50, 25];
    var BUS_SPEED_NAMES = ['slow', 'normal', 'fast', 'turbo'];
    var BUS_STYLE_NAMES = ['per_point', 'segment_skip', 'remain_window'];
    var busBot = {
        on: false,
        phase: 'idle',
        prevPhase: 'farm',
        routeId: BUS_OKRUG_ID,
        fare: BUS_OKRUG_FARE,
        routeName: 'Okruzhnyi',
        style: 'segment_skip',
        speedId: 1,
        segIndex: 0,
        skipPhase: 'lead',
        stopBursts: BUS_OKRUG_STOP_BURSTS.slice(),
        lapIndex: 0,
        markersSeenLap: 0,
        stopsSeenLap: 0,
        markersPerLap: BUS_MARKERS_PER_LAP,
        stopsPerLap: BUS_OKRUG_STOPS,
        calibrated: true,
        lapPoints: [],
        firstPoint: null,
        stopIndices: BUS_OKRUG_STOP_IDX.slice(),
        currentIsStop: false,
        spamLeft: 0,
        spamTimerId: null,
        burstTimerId: null,
        burstLeft: 0,
        burstDone: null,
        spamCount: 0,
        earnedSession: 0,
        earnedLap: 0,
        lastLapEarned: 0,
        perHour: 0,
        startedAt: 0,
        cashPrev: null,
        earningActive: false,
        underground: false,
        parkX: 0,
        parkY: 0,
        surfaceZ: 0,
        underZ: 0,
        holdTimerId: null,
        skipLoopArmed: false,
        lastMsg: '',
        lastCloseReason: '',
        hooksReady: false,
        moneyHooksReady: false
    };
    global.__uaBus = busBot;

    function busStatus(msg) {
        busBot.lastMsg = String(msg || '');
        log('bus: ' + busBot.lastMsg);
    }

    function busSpeedMs() {
        var id = busBot.speedId | 0;
        if (id < 0) id = 0;
        if (id > 3) id = 3;
        return BUS_SPEED_MS[id];
    }

    function busApplyHardcode() {
        busBot.calibrated = true;
        busBot.markersPerLap = BUS_MARKERS_PER_LAP;
        busBot.stopBursts = BUS_OKRUG_STOP_BURSTS.slice();
        busBot.stopsPerLap = BUS_OKRUG_STOP_BURSTS.length;
        busBot.stopIndices = BUS_OKRUG_STOP_IDX.slice();
    }

    function busUpdatePerHour() {
        if (!busBot.startedAt) {
            busBot.perHour = 0;
            return;
        }
        var hrs = (Date.now() - busBot.startedAt) / 3600000;
        busBot.perHour = hrs > 0.0001
            ? Math.round(busBot.earnedSession / hrs)
            : 0;
    }

    function busOnMoney(payload) {
        try {
            var cash = null;
            if (payload && typeof payload === 'object')
                cash = Number(payload.cash);
            else if (arguments.length >= 1)
                cash = Number(arguments[0]);
            if (!isFinite(cash)) return;
            if (busBot.cashPrev != null && busBot.earningActive &&
                cash > busBot.cashPrev) {
                var d = Math.round(cash - busBot.cashPrev);
                if (d > 0) {
                    busBot.earnedSession += d;
                    busBot.earnedLap += d;
                    busUpdatePerHour();
                    busStatus('+$' + d + ' (sess $' +
                        busBot.earnedSession + ')');
                }
            }
            busBot.cashPrev = cash;
        } catch (e) {}
    }

    function busEnsureMoneyHooks() {
        if (busBot.moneyHooksReady) return;
        busBot.moneyHooksReady = true;
        try {
            mp.events.add('money.change', function (a, b) {
                if (a && typeof a === 'object') busOnMoney(a);
                else busOnMoney({ cash: a, bank: b });
            });
            mp.events.add('money.change.hidden', function (a, b) {
                if (a && typeof a === 'object') {
                    var c = Number(a.cash);
                    if (isFinite(c)) busBot.cashPrev = c;
                } else {
                    var c2 = Number(a);
                    if (isFinite(c2)) busBot.cashPrev = c2;
                }
            });
        } catch (e) {
            log('bus money hooks err: ' + e);
        }
    }

    function busDist(a, b) {
        if (!a || !b) return 1e9;
        var dx = Number(a.x) - Number(b.x);
        var dy = Number(a.y) - Number(b.y);
        var dz = Number(a.z || 0) - Number(b.z || 0);
        return Math.sqrt(dx * dx + dy * dy + dz * dz);
    }

    function busNear(a, b) {
        return busDist(a, b) < BUS_LAP_EPS;
    }

    function busGetVeh() {
        try {
            var p = mp.players.local;
            if (p && p.vehicle && mp.vehicles.exists(p.vehicle))
                return p.vehicle;
        } catch (e) {}
        return null;
    }

    function busClearBurst() {
        if (busBot.burstTimerId != null) {
            try { clearInterval(busBot.burstTimerId); } catch (e) {}
            busBot.burstTimerId = null;
        }
        if (busBot.spamTimerId != null) {
            try { clearInterval(busBot.spamTimerId); } catch (e) {}
            busBot.spamTimerId = null;
        }
        busBot.burstLeft = 0;
        busBot.spamLeft = 0;
        busBot.burstDone = null;
    }

    function busFireEntered() {
        try {
            mp.events.callRemote('busdriver.checkpoint.entered');
            busBot.spamCount++;
        } catch (e) {}
    }

    function busFireBurst(count, intervalMs, doneCb, phaseName) {
        busClearBurst();
        var n = Math.max(0, count | 0);
        if (n <= 0) {
            if (doneCb) doneCb();
            return;
        }
        var iv = Math.max(15, intervalMs | 0);
        busBot.burstLeft = n;
        busBot.spamLeft = n;
        busBot.burstDone = doneCb || null;
        busBot.phase = phaseName || 'burst';
        busStatus(busBot.phase + ' x' + n + ' @' + iv + 'ms');
        busBot.burstTimerId = setInterval(function () {
            if (!busBot.on) return;
            if (busBot.phase === 'paused_admin') return;
            if (busBot.burstLeft <= 0) {
                var cb = busBot.burstDone;
                busClearBurst();
                if (cb) cb();
                return;
            }
            busFireEntered();
            busBot.burstLeft--;
            busBot.spamLeft = busBot.burstLeft;
        }, iv);
    }
)UAJS"
        R"UAJS(
    function busHoldUnder() {
        if (!busBot.underground) return;
        try {
            var p = mp.players.local;
            var veh = busGetVeh();
            try { mp.events.call('ac:refreshtp'); } catch (e0) {}
            if (veh) {
                try { veh.setCollision(false, false); } catch (e1) {}
                try { veh.freezePosition(false); } catch (e2) {}
                veh.setCoordsNoOffset(
                    busBot.parkX, busBot.parkY, busBot.underZ,
                    false, false, false);
                try { veh.freezePosition(true); } catch (e3) {}
                try { veh.setVelocity(0, 0, 0); } catch (e4) {}
            }
            if (p) {
                try { p.setCollision(false, false); } catch (e5) {}
                try { p.freezePosition(true); } catch (e6) {}
                if (!veh) {
                    p.setCoordsNoOffset(
                        busBot.parkX, busBot.parkY, busBot.underZ,
                        false, false, false);
                }
                try { p.setVelocity(0, 0, 0); } catch (e7) {}
            }
        } catch (e) {}
    }

    function busParkUnderground() {
        try {
            var p = mp.players.local;
            var veh = busGetVeh();
            var pos = veh ? veh.position : (p ? p.position : null);
            if (!pos) {
                busStatus('park fail: no pos');
                return false;
            }
            busBot.parkX = Number(pos.x);
            busBot.parkY = Number(pos.y);
            busBot.surfaceZ = Number(pos.z);
            busBot.underZ = busBot.surfaceZ - BUS_UNDER_DEPTH;
            busBot.underground = true;
            try {
                if (p) p.setInvincible(true);
            } catch (e0) {}
            busHoldUnder();
            if (busBot.holdTimerId != null) {
                try { clearInterval(busBot.holdTimerId); } catch (e1) {}
            }
            busBot.holdTimerId = setInterval(busHoldUnder, 150);
            busStatus('UNDER park z=' + busBot.underZ.toFixed(1));
            return true;
        } catch (e) {
            busStatus('park err: ' + e);
            return false;
        }
    }

    function busUnpark() {
        busBot.underground = false;
        if (busBot.holdTimerId != null) {
            try { clearInterval(busBot.holdTimerId); } catch (e) {}
            busBot.holdTimerId = null;
        }
        try {
            var p = mp.players.local;
            var veh = busGetVeh();
            if (veh) {
                try { veh.freezePosition(false); } catch (e1) {}
                try { veh.setCollision(true, true); } catch (e2) {}
                try {
                    veh.setCoordsNoOffset(
                        busBot.parkX, busBot.parkY, busBot.surfaceZ,
                        false, false, false);
                } catch (e3) {}
            }
            if (p) {
                try { p.freezePosition(false); } catch (e4) {}
                try { p.setCollision(true, true); } catch (e5) {}
                try { p.setInvincible(false); } catch (e6) {}
            }
        } catch (e) {}
    }

    function busRemainForIdx(idx) {
        var rem = BUS_MARKERS_PER_LAP - (idx | 0);
        return rem < 0 ? 0 : rem;
    }

    function busStartSpamRemain(n) {
        busClearBurst();
        var left = Math.max(0, n | 0);
        if (left <= 0) {
            busStatus('stop: remain=0');
            busBot.phase = 'farm';
            return;
        }
        var windowMs = 15000;
        var interval = Math.max(80, Math.floor(windowMs / left));
        busBot.phase = 'stop_wait';
        busBot.spamLeft = left;
        busBot.burstLeft = left;
        busStatus('STOP remain x' + left + ' /' + interval + 'ms');
        busBot.spamTimerId = setInterval(function () {
            if (!busBot.on || busBot.phase === 'paused_admin') return;
            if (busBot.spamLeft <= 0) {
                busClearBurst();
                busBot.phase = 'farm';
                busStatus('farm');
                return;
            }
            busFireEntered();
            busBot.spamLeft--;
            busBot.burstLeft = busBot.spamLeft;
        }, interval);
    }

    function busResetLapTrack(keepLap) {
        busBot.markersSeenLap = 0;
        busBot.stopsSeenLap = 0;
        busBot.lapPoints = [];
        busBot.firstPoint = null;
        busBot.lastCloseReason = '';
        busBot.segIndex = 0;
        busBot.skipPhase = 'lead';
        busApplyHardcode();
        if (!keepLap) {
            busBot.lapIndex = 0;
            busBot.earnedLap = 0;
        }
    }
)UAJS"
        R"UAJS(
    function busCommitLap(reason) {
        busBot.lastCloseReason = String(reason || 'close');
        busBot.lastLapEarned = busBot.earnedLap | 0;
        busUpdatePerHour();
        busStatus('LAP#' + (busBot.lapIndex + 1) +
            ' +$' + busBot.lastLapEarned +
            ' via=' + busBot.lastCloseReason);
        busBot.lapIndex++;
        busBot.earnedLap = 0;
        busBot.markersSeenLap = 0;
        busBot.stopsSeenLap = 0;
        busBot.lapPoints = [];
        busBot.firstPoint = null;
        busBot.segIndex = 0;
        busBot.skipPhase = 'lead';
    }

    // Cycle: lead 18 → stop×10 → stop×15 → stop×13 → repeat
    function busSegmentSkipKick() {
        if (!busBot.on || busBot.style !== 'segment_skip') return;
        if (busBot.phase === 'paused_admin') return;
        if (busBot.burstTimerId || busBot.spamTimerId) return;
        var iv = busSpeedMs();
        var bursts = busBot.stopBursts || BUS_OKRUG_STOP_BURSTS;

        if (busBot.skipPhase !== 'stops') {
            busBot.skipPhase = 'lead';
            busFireBurst(BUS_OKRUG_LEAD, iv, function () {
                if (!busBot.on || busBot.style !== 'segment_skip') return;
                if (busBot.phase === 'paused_admin') return;
                busBot.skipPhase = 'stops';
                busBot.segIndex = 0;
                busSegmentSkipKick();
            }, 'seg_burst');
            return;
        }

        var si = busBot.segIndex | 0;
        if (si < 0) si = 0;
        if (si >= bursts.length) {
            busCommitLap('segment_cycle');
            busSegmentSkipKick();
            return;
        }
        var m = (bursts[si] | 0) || 10;
        busFireBurst(m, iv, function () {
            if (!busBot.on || busBot.style !== 'segment_skip') return;
            busBot.stopsSeenLap++;
            busBot.segIndex = si + 1;
            if (busBot.segIndex >= bursts.length) {
                busCommitLap('segment_cycle');
            }
            busSegmentSkipKick();
        }, 'stop_burst');
    }

    function busOnCheckpoint(data, nextPoint) {
        if (!data) return;
        var isStop = !!data.isStop;
        var pt = {
            x: Number(data.x), y: Number(data.y), z: Number(data.z),
            isStop: isStop
        };
        busBot.currentIsStop = isStop;

        if (busBot.firstPoint && busBot.markersSeenLap >= BUS_LAP_MIN &&
            busNear(pt, busBot.firstPoint)) {
            if (busBot.style !== 'segment_skip')
                busCommitLap('return_to_start');
        }

        if (busBot.markersSeenLap === 0)
            busBot.firstPoint = pt;

        busBot.markersSeenLap++;
        busBot.lapPoints.push(pt);
        if (isStop && busBot.style !== 'segment_skip')
            busBot.stopsSeenLap++;

        var idx = busBot.markersSeenLap;
        var nextIsStart = !!(nextPoint && busBot.firstPoint &&
            busNear(nextPoint, busBot.firstPoint) &&
            busBot.markersSeenLap >= BUS_LAP_MIN);

        if (!busBot.on) {
            if (nextIsStart && busBot.style !== 'segment_skip')
                busCommitLap('next_is_start');
            return;
        }
        if (busBot.phase === 'paused_admin') return;

        if (busBot.style === 'segment_skip') {
            // Autonomous bursts — only track markers from server.
            busStatus('seg ' + ((busBot.segIndex | 0) + 1) + '/4 · #' +
                idx + (isStop ? ' STOP' : ''));
            return;
        }

        if (busBot.style === 'per_point') {
            busBot.phase = isStop ? 'stop_wait' : 'farm';
            setTimeout(function () {
                if (!busBot.on || busBot.phase === 'paused_admin') return;
                if (busBot.style !== 'per_point') return;
                busFireEntered();
                busStatus('per_point #' + idx +
                    (isStop ? ' STOP' : ''));
            }, busSpeedMs());
            if (nextIsStart) busCommitLap('next_is_start');
            return;
        }

        // remain_window
        if (isStop) {
            busStartSpamRemain(busRemainForIdx(idx));
        } else {
            busBot.phase = 'farm';
            setTimeout(function () {
                if (!busBot.on || busBot.phase === 'paused_admin') return;
                if (busBot.style !== 'remain_window') return;
                busFireEntered();
            }, busSpeedMs());
            busStatus('farm #' + idx + '/' + BUS_MARKERS_PER_LAP);
        }
        if (nextIsStart) busCommitLap('next_is_start');
    }
)UAJS"
        R"UAJS(
    function busEnsureHooks() {
        if (busBot.hooksReady) return;
        busBot.hooksReady = true;
        busEnsureMoneyHooks();
        try {
            mp.events.add('busdriver.checkpoint.create',
                function (data, nextPoint) {
                    busOnCheckpoint(data, nextPoint);
                });
            mp.events.add('busdriver.route.start.ans',
                function (ans, data, nextPoint) {
                    if (Number(ans) === 1) {
                        busResetLapTrack(true);
                        busApplyHardcode();
                        if (busBot.on) {
                            busBot.earningActive = true;
                            busParkUnderground();
                            if (busBot.style === 'segment_skip') {
                                busBot.phase = 'farm';
                                busStatus('route ok · segment_skip');
                                setTimeout(busSegmentSkipKick, 400);
                            } else {
                                busBot.phase = 'farm';
                                busStatus('route ok · ' + busBot.style);
                            }
                        }
                        if (data) busOnCheckpoint(data, nextPoint);
                    } else {
                        busStatus('route start failed ans=' + ans);
                    }
                });
            mp.events.add('busdriver.route.end', function () {
                busClearBurst();
                busBot.earningActive = false;
                if (busBot.on) {
                    busBot.phase = 'idle';
                    busStatus('route ended (workday)');
                }
            });
        } catch (e) {
            log('bus hooks err: ' + e);
        }
    }

    function busSetStyle(name) {
        var s = String(name || '').toLowerCase().replace(/[\s-]+/g, '_');
        if (s === 'perpoint' || s === 'per_point' || s === '0')
            busBot.style = 'per_point';
        else if (s === 'remain' || s === 'remain_window' || s === '2')
            busBot.style = 'remain_window';
        else
            busBot.style = 'segment_skip';
        busStatus('style=' + busBot.style);
        return 'ok:bus_style_' + busBot.style;
    }

    function busSetSpeed(arg) {
        var a = String(arg || '').toLowerCase();
        var id = parseInt(a, 10);
        if (a === 'slow') id = 0;
        else if (a === 'normal') id = 1;
        else if (a === 'fast') id = 2;
        else if (a === 'turbo') id = 3;
        if (isNaN(id) || id < 0) id = 1;
        if (id > 3) id = 3;
        busBot.speedId = id;
        busStatus('speed=' + BUS_SPEED_NAMES[id] +
            ' (' + BUS_SPEED_MS[id] + 'ms)');
        return 'ok:bus_speed_' + BUS_SPEED_NAMES[id];
    }

    function busSetStopBursts(csv) {
        var parts = String(csv || '').split(/[,\s]+/).filter(function (p) {
            return p.length > 0;
        });
        var out = [];
        var nWant = Math.max(1, Math.min(8, parts.length || 3));
        for (var i = 0; i < nWant; i++) {
            var n = parseInt(parts[i], 10);
            if (isNaN(n) || n < 1) {
                n = BUS_OKRUG_STOP_BURSTS[i] != null
                    ? BUS_OKRUG_STOP_BURSTS[i] : 10;
            }
            if (n > 80) n = 80;
            out.push(n);
        }
        if (out.length < 1) out = BUS_OKRUG_STOP_BURSTS.slice();
        busBot.stopBursts = out;
        busBot.stopsPerLap = out.length;
        busStatus('stop_bursts=' + out.join(',') +
            ' (lead=' + BUS_OKRUG_LEAD + ')');
        return 'ok:bus_stop_bursts';
    }

    function busRouteOkrugStart() {
        busEnsureHooks();
        busApplyHardcode();
        busBot.routeId = BUS_OKRUG_ID;
        busBot.fare = BUS_OKRUG_FARE;
        busResetLapTrack(busBot.on);
        busClearBurst();
        try {
            mp.events.callRemote(
                'busdriver.route.start', BUS_OKRUG_ID, BUS_OKRUG_FARE);
            busStatus('sent route.start 6,0 (Okruzhnyi)');
            return 'ok:bus_route_okrug';
        } catch (e) {
            return 'err:' + e;
        }
    }

    function busBotStart() {
        try { if (typeof jobBotsExclusive === 'function') jobBotsExclusive('bus'); } catch (eEx) {}
        busEnsureHooks();
        busApplyHardcode();
        busBot.on = true;
        busBot.earningActive = true;
        if (!busBot.startedAt) busBot.startedAt = Date.now();
        busParkUnderground();
        busBot.phase = 'farm';
        busStatus('ON ' + busBot.style + ' @' +
            BUS_SPEED_NAMES[busBot.speedId | 0]);
        if (busBot.style === 'segment_skip')
            setTimeout(busSegmentSkipKick, 300);
        return 'ok:bus_bot_start';
    }

    function busBotStop() {
        busBot.on = false;
        busBot.earningActive = false;
        busClearBurst();
        busUnpark();
        busBot.phase = 'idle';
        busUpdatePerHour();
        busStatus('stopped · $' + busBot.earnedSession);
        return 'ok:bus_bot_stop';
    }
)UAJS"
        R"UAJS(
    // ---------------- Handyman bot ----------------
    var HM_SKIMMER = 245, HM_CHLORINE = 244, HM_TOWEL = 246;
    var handymanBot = {
        on: false,
        phase: 'idle',
        lastMsg: '',
        timerId: null,
        holdTimerId: null,
        holdPos: null,
        tasksRaw: null,
        queue: [],
        qi: 0,
        busyUntil: 0,
        waitProcess: false,
        waitKind: '',
        probePending: false,
        probeAt: 0,
        buyCooldown: 0,
        dropCooldown: 0,
        pauseUntil: 0,
        hooksReady: false,
        skimmerHp: -1,
        towelHp: -1,
        chlorineQty: 0,
        skimmerSqlId: -1,
        towelSqlId: -1,
        chlorineNeed: 0,
        pendingBuy: null,
        shopAt: 0,
        shopCache: [],
        shopMenuOpen: false,
        shopSub: '',
        shopOpenRetries: 0,
        inSuperShape: false,
        shopBizPos: null,
        atNpcUntil: 0,
        tasksCount: 0,
        equipSent: false,
        equipRetries: 0,
        equipAt: 0,
        inWorkShape: false,
        workShapeKey: '',
        inWindowShape: false,
        inElecShape: false,
        jobStarted: false,
        jobEnded: false,
        shapeWaitAt: 0,
        shapeStanding: false,
        jobStartAt: 0,
        jobAttempts: 0,
        jobRetries: 0,
        washDonePairs: {},
        awaitingDone: false,
        stepDoneToken: 0,
        blipSeen: false,
        holdSoft: false,
        taskSummary: '-',
        workStartAt: 0,
        workStartRetries: 0
    };
    global.__uaHandyman = handymanBot;
    global.__uaHmInv = null;

    // supermarket cashiers (not biz map blip)
    var HM_SHOP_MODELS = [
        's_f_m_sweatshop_01', 's_f_y_sweatshop_01',
        's_m_m_strvend_01', 'csb_undercover', 'mp_m_shopkeep_01'
    ];
    try {
        mp.events.add('supermarket.npc.create', function (peds) {
            // append+dedupe — server may send shops in batches
            handymanCacheShopPeds(peds);
        });
    } catch (eShopHook) {}

    function handymanStatus(msg) {
        handymanBot.lastMsg = String(msg || '');
        log('handyman: ' + handymanBot.lastMsg);
    }

    function jobBotsExclusive(except) {
        if (except !== 'woodman' && woodmanBot.on) {
            try { woodmanBotStop(); } catch (e0) {}
        }
        if (except !== 'bus' && busBot.on) {
            try { busBotStop(); } catch (e1) {}
        }
        if (except !== 'handyman' && handymanBot.on) {
            try { handymanBotStop(); } catch (e2) {}
        }
        if (except !== 'carrier' && typeof carrierBotStop === 'function' &&
            carrierBot && carrierBot.on) {
            try { carrierBotStop(); } catch (e3) {}
        }
    }

    function handymanNormPos(p) {
        if (!p) return null;
        if (typeof p.x === 'number' && typeof p.y === 'number') {
            return {
                x: Number(p.x), y: Number(p.y),
                z: Number(p.z != null ? p.z : 0)
            };
        }
        if (Array.isArray(p) && p.length >= 2) {
            return {
                x: Number(p[0]), y: Number(p[1]),
                z: Number(p[2] != null ? p[2] : 0)
            };
        }
        if (p.position) return handymanNormPos(p.position);
        return null;
    }

    function handymanNormalizeTasks(data) {
        if (data == null) return [];
        if (typeof data === 'string') {
            try { data = JSON.parse(data); } catch (e) { return []; }
        }
        if (Array.isArray(data)) return data;
        if (data.tasks && Array.isArray(data.tasks)) return data.tasks;
        if (data.data && Array.isArray(data.data)) return data.data;
        if (data.list && Array.isArray(data.list)) return data.list;
        if (typeof data === 'object') {
            var out = [];
            for (var k in data) {
                var v = data[k];
                if (v && typeof v === 'object' &&
                    (v.type || v.poses || v.pos))
                    out.push(v);
            }
            return out;
        }
        return [];
    }

    function handymanExtractWorkData(args) {
        if (!args || !args.length) return [];
        var a0 = args[0];
        if (typeof a0 === 'string') {
            try { a0 = JSON.parse(a0); } catch (eJ) {}
        }
        // Single payload: array/object of tasks
        if (args.length === 1)
            return handymanNormalizeTasks(a0);
        // Spread tasks as separate args
        if (a0 && typeof a0 === 'object' &&
            (a0.type || a0.poses || a0.pos || a0.jobType))
            return handymanNormalizeTasks(args);
        return handymanNormalizeTasks(a0);
    }

    function handymanBuildQueue(data) {
        var q = [];
        var tasks = handymanNormalizeTasks(data);
        for (var i = 0; i < tasks.length; i++) {
            var t = tasks[i];
            if (!t) continue;
            var type = String(t.type || t.jobType || '')
                .toLowerCase();
            // Exact client types: pool | electrician | window
            if (type === 'window' || type.indexOf('window') >= 0)
                type = 'window';
            else if (type === 'electrician' ||
                type.indexOf('electric') >= 0)
                type = 'electrician';
            else if (type === 'pool' || type.indexOf('pool') >= 0)
                type = 'pool';
            else
                continue;
            var rawPoses = t.poses || t.positions || t.points || [];
            if ((!rawPoses || !rawPoses.length) && t.pos)
                rawPoses = [t.pos];
            var poses = [];
            for (var pi = 0; pi < rawPoses.length; pi++) {
                var np = handymanNormPos(rawPoses[pi]);
                if (np) poses.push(np);
            }
            var idx = t.itemIndex != null ? t.itemIndex : i;
            var stepsLeft = t.stepsLeft != null
                ? Number(t.stepsLeft)
                : (poses.length >= 2 ? 2 : 1);
            if (isNaN(stepsLeft)) stepsLeft = poses.length >= 2 ? 2 : 1;

            // Mirror client initWorkBlips exactly:
            // pool: always chlorine @poses[1] (or [0]); wash @poses[0] if stepsLeft>1
            // electrician/window: single blip @poses[0]
            if (type === 'window' && poses[0]) {
                q.push({
                    kind: 'window', type: 'window', itemIndex: idx,
                    pos: poses[0], itemId: HM_TOWEL
                });
            } else if (type === 'electrician' && poses[0]) {
                q.push({
                    kind: 'electrician', type: 'electrician',
                    itemIndex: idx, pos: poses[0], itemId: 0
                });
            } else if (type === 'pool') {
                // Server: chlorine colshape exists ONLY after wash done
                if (stepsLeft > 1 && poses[0]) {
                    q.push({
                        kind: 'wash', type: 'pool', itemIndex: idx,
                        pos: poses[0], itemId: HM_SKIMMER,
                        poolPair: idx
                    });
                }
                var clPos = poses[1] || poses[0];
                if (clPos) {
                    q.push({
                        kind: 'tablet', type: 'pool', itemIndex: idx,
                        pos: clPos, itemId: HM_CHLORINE,
                        poolPair: idx,
                        needsWashFirst: stepsLeft > 1
                    });
                }
            }
        }
        return handymanOrderPoolQueue(q);
    }

    // Force wash → chlorine pairs (chlorine shape locked until wash)
    function handymanOrderPoolQueue(q) {
        if (!q || !q.length) return q || [];
        var washes = [], tablets = [], other = [];
        for (var i = 0; i < q.length; i++) {
            var s = q[i];
            if (!s) continue;
            if (s.kind === 'wash') washes.push(s);
            else if (s.kind === 'tablet') tablets.push(s);
            else other.push(s);
        }
        var out = [], used = {};
        function dist2(a, b) {
            if (!a || !b || !a.pos || !b.pos) return 1e15;
            var dx = a.pos.x - b.pos.x, dy = a.pos.y - b.pos.y;
            return dx * dx + dy * dy;
        }
        for (var w = 0; w < washes.length; w++) {
            var wash = washes[w];
            out.push(wash);
            var best = -1, bestD = 1e15;
            for (var t = 0; t < tablets.length; t++) {
                if (used[t]) continue;
                var tab = tablets[t];
                if (wash.poolPair != null && tab.poolPair != null &&
                    wash.poolPair === tab.poolPair) {
                    best = t;
                    bestD = 0;
                    break;
                }
                if (wash.itemIndex != null && tab.itemIndex != null &&
                    wash.itemIndex === tab.itemIndex &&
                    !wash.fromBlip) {
                    best = t;
                    bestD = 0;
                    break;
                }
                var d = dist2(wash, tab);
                if (d < bestD && d < 50 * 50) {
                    bestD = d;
                    best = t;
                }
            }
            if (best >= 0) {
                used[best] = true;
                // Cl colshape unlocks only after this wash
                tablets[best].needsWashFirst = true;
                tablets[best].poolPair = wash.poolPair != null
                    ? wash.poolPair : wash.itemIndex;
                out.push(tablets[best]);
            }
        }
        for (var t2 = 0; t2 < tablets.length; t2++) {
            if (used[t2]) continue;
            // No wash in queue for this Cl → already unlocked on server
            tablets[t2].needsWashFirst = false;
            out.push(tablets[t2]);
        }
        for (var o = 0; o < other.length; o++) out.push(other[o]);
        return out;
    }

    function handymanCountKinds(q) {
        var c = { window: 0, wash: 0, tablet: 0, electrician: 0 };
        for (var i = 0; i < q.length; i++) {
            var k = q[i] && q[i].kind;
            if (k && c[k] != null) c[k]++;
        }
        return c;
    }

    function handymanCountChlorineNeed(q) {
        return handymanCountKinds(q).tablet;
    }

    function handymanSummary(q, tasksCount) {
        var c = handymanCountKinds(q);
        var tc = tasksCount != null ? tasksCount : '?';
        return 'T' + tc +
            ' W' + c.window + '/wash' + c.wash +
            '/Cl' + c.tablet + '/E' + c.electrician +
            ' =' + q.length;
    }

    // Fallback ONLY: handyman blips (sprite 1, scale~0.7)
    // 3=wash, 4=chlorine, 27=electrician, 63=window
    function handymanQueueFromBlips() {
        var q = [];
        try {
            mp.blips.forEach(function (blip) {
                if (!blip || !mp.blips.exists(blip)) return;
                var color = -1;
                try {
                    if (typeof blip.getColour === 'function')
                        color = blip.getColour();
                    else if (typeof blip.getColor === 'function')
                        color = blip.getColor();
                    else if (blip.color != null) color = blip.color;
                } catch (eC) {}
                if (color !== 3 && color !== 4 &&
                    color !== 27 && color !== 63)
                    return;
                // Prefer sprite=1 (initWorkBlips uses blips.new(1, ...))
                try {
                    var spr = -1;
                    if (typeof blip.getSprite === 'function')
                        spr = blip.getSprite();
                    else if (blip.sprite != null) spr = blip.sprite;
                    if (spr !== -1 && spr !== 1) return;
                } catch (eS) {}
                try {
                    var sc = -1;
                    if (typeof blip.getScale === 'function')
                        sc = blip.getScale();
                    else if (blip.scale != null) sc = blip.scale;
                    if (sc > 0 && Math.abs(sc - 0.7) > 0.15) return;
                } catch (eSc) {}
                var pos = null;
                try {
                    if (typeof blip.getCoords === 'function')
                        pos = handymanNormPos(blip.getCoords());
                    else if (blip.position)
                        pos = handymanNormPos(blip.position);
                } catch (eP) {}
                if (!pos) return;
                if (color === 63)
                    q.push({
                        kind: 'window', type: 'window',
                        itemIndex: q.length, pos: pos, itemId: HM_TOWEL,
                        fromBlip: true
                    });
                else if (color === 3)
                    q.push({
                        kind: 'wash', type: 'pool',
                        itemIndex: q.length, pos: pos, itemId: HM_SKIMMER,
                        fromBlip: true, poolPair: 'b' + q.length
                    });
                else if (color === 4)
                    q.push({
                        kind: 'tablet', type: 'pool',
                        itemIndex: q.length, pos: pos, itemId: HM_CHLORINE,
                        fromBlip: true, needsWashFirst: true
                    });
                else if (color === 27)
                    q.push({
                        kind: 'electrician', type: 'electrician',
                        itemIndex: q.length, pos: pos, itemId: 0,
                        fromBlip: true
                    });
            });
        } catch (e) {}
        // Pair each wash with nearest chlorine — never Cl before wash
        return handymanOrderPoolQueue(q);
    }
)UAJS"
        R"UAJS(
    function handymanInvProbe() {
        handymanBot.probePending = true;
        handymanBot.probeAt = Date.now();
        var code = "(function(){try{"
            + "var send=function(p){var s;try{s=JSON.stringify(p);}catch(e){s='{\"error\":\"stringify\"}';}"
            + "try{mp['trigger']('ua:hmInvProbe',s);}catch(e1){}};"
            + "var store=null;"
            + "try{store=window.app&&window.app.config&&window.app.config.globalProperties"
            + "&&window.app.config.globalProperties.$store;}catch(eS){}"
            + "var st=store&&store.state&&store.state.inventoryState;"
            + "if(!st){send({error:'no inventoryState'});return;}"
            + "function collectMap(map,ids,out){if(!map)return;"
            + "for(var k in map){var it=map[k];if(!it)continue;"
            + "if(ids.indexOf(Number(it.itemId))>=0)out.push(it);}}"
            + "var ids=[244,245,246],all=[];"
            + "collectMap(st.mainItems,ids,all);collectMap(st.bodyItems,ids,all);"
            + "if(st.bag)collectMap(st.bag.items,ids,all);"
            + "var skimmer=null,towel=null,chlorine=null,chlorineQty=0,broken=[];"
            + "for(var i=0;i<all.length;i++){var it=all[i];if(!it)continue;"
            + "var id=Number(it.itemId),hp=100,qty=Number(it.quantity)||1;"
            + "try{if(it.params&&it.params.health!=null)hp=Number(it.params.health);}catch(eH){}"
            + "if(isNaN(hp))hp=100;"
            + "if(id===244){chlorineQty+=qty;if(!chlorine)chlorine={sqlId:it.sqlId};}"
            + "else if(id===245){if(hp<=1)broken.push(it.sqlId);"
            + "else if(!skimmer||hp>(skimmer.hp||0))skimmer={sqlId:it.sqlId,hp:hp};}"
            + "else if(id===246){if(hp<=1)broken.push(it.sqlId);"
            + "else if(!towel||hp>(towel.hp||0))towel={sqlId:it.sqlId,hp:hp};}}"
            + "var hands=null;try{hands=st.bodyItems&&st.bodyItems[12];}catch(e5){}"
            + "send({via:'vuex',skimmerSqlId:skimmer?skimmer.sqlId:null,"
            + "skimmerHp:skimmer?skimmer.hp:-1,towelSqlId:towel?towel.sqlId:null,"
            + "towelHp:towel?towel.hp:-1,chlorineQty:chlorineQty,"
            + "chlorineSqlId:chlorine?chlorine.sqlId:null,"
            + "broken:broken,handsItemId:hands?Number(hands.itemId):-1,"
            + "handsSqlId:hands?hands.sqlId:null});"
            + "}catch(e){try{mp['trigger']('ua:hmInvProbe',JSON.stringify({error:String(e)}));}catch(e2){}}})();";
        var ok = false;
        try {
            if (typeof uaCefExecuteCallCefv === 'function')
                ok = uaCefExecuteCallCefv(code);
        } catch (e0) {}
        if (!ok) {
            try {
                var browsers = uaCefListBrowsers();
                if (browsers[0])
                    ok = uaCefExecuteOnBrowser(browsers[0].browser, code);
            } catch (e1) {}
        }
        if (!ok) {
            handymanBot.probePending = false;
            global.__uaHmInv = { error: 'cef execute unavailable' };
        }
        return ok;
    }

    try {
        mp.events.add('ua:hmInvProbe', function (json) {
            handymanBot.probePending = false;
            try {
                var parsed = typeof json === 'string' ? JSON.parse(json) : json;
                global.__uaHmInv = parsed;
                if (parsed && !parsed.error) {
                    handymanBot.skimmerHp = parsed.skimmerHp != null ? Number(parsed.skimmerHp) : -1;
                    handymanBot.towelHp = parsed.towelHp != null ? Number(parsed.towelHp) : -1;
                    handymanBot.chlorineQty = Number(parsed.chlorineQty) || 0;
                    handymanBot.skimmerSqlId = parsed.skimmerSqlId != null ? Number(parsed.skimmerSqlId) : -1;
                    handymanBot.towelSqlId = parsed.towelSqlId != null ? Number(parsed.towelSqlId) : -1;
                }
            } catch (e) {
                global.__uaHmInv = { error: String(json) };
            }
        });
    } catch (eHmEv) {}

    function handymanDropPos() {
        var p = localPos();
        var heading = 0;
        try { heading = mp.players.local.getHeading(); } catch (e) {}
        try {
            var rad = (heading * Math.PI) / 180;
            return {
                x: p.x - Math.sin(rad), y: p.y + Math.cos(rad),
                z: p.z, heading: heading
            };
        } catch (e2) {
            return { x: p.x, y: p.y, z: p.z, heading: heading };
        }
    }

    function handymanDropBroken(sqlIds) {
        if (!sqlIds || !sqlIds.length) return;
        if (Date.now() < handymanBot.dropCooldown) return;
        var drop = handymanDropPos();
        for (var i = 0; i < sqlIds.length && i < 6; i++) {
            rem('item.ground.put', sqlIds[i], JSON.stringify(drop));
        }
        handymanBot.dropCooldown = Date.now() + 800;
        handymanStatus('dropped broken tools x' + sqlIds.length);
    }

    function handymanStandNear(pos, heading, dist) {
        if (!pos) return null;
        dist = dist == null ? 1.15 : dist;
        var h = (Number(heading) || 0) * Math.PI / 180;
        return {
            x: pos.x - Math.sin(h) * dist,
            y: pos.y + Math.cos(h) * dist,
            z: pos.z
        };
    }

    function handymanDist2(a, b) {
        if (!a || !b) return 1e15;
        var dx = a.x - b.x, dy = a.y - b.y;
        var dz = (a.z || 0) - (b.z || 0);
        return dx * dx + dy * dy + dz * dz;
    }

    // biz type 1 = supermarket (NOT fishing / bags / etc.)
    function handymanSupermarketBiz() {
        try {
            if (mp.locator &&
                typeof mp.locator.getNeighborLocation === 'function') {
                var place = mp.locator.getNeighborLocation('biz', 1);
                if (place && place.position) {
                    return {
                        x: Number(place.position.x),
                        y: Number(place.position.y),
                        z: Number(place.position.z)
                    };
                }
            }
        } catch (e0) {}
        return null;
    }

    function handymanCacheShopPeds(peds) {
        if (!peds) return;
        var arr = Array.isArray(peds) ? peds : [peds];
        if (!handymanBot.shopCache) handymanBot.shopCache = [];
        for (var i = 0; i < arr.length; i++) {
            var el = arr[i];
            if (!el) continue;
            var pedPos = el.position;
            var marker = el.marker;
            var src = marker || pedPos;
            if (!src || src.x == null) continue;
            var entry = {
                x: Number(src.x), y: Number(src.y), z: Number(src.z),
                pedX: pedPos ? Number(pedPos.x) : Number(src.x),
                pedY: pedPos ? Number(pedPos.y) : Number(src.y),
                pedZ: pedPos ? Number(pedPos.z) : Number(src.z),
                heading: Number(el.heading) || 0,
                fromMarker: !!marker,
                source: 'supermarket'
            };
            var key = entry.pedX.toFixed(1) + ',' +
                entry.pedY.toFixed(1);
            var exists = false;
            for (var j = 0; j < handymanBot.shopCache.length; j++) {
                var c = handymanBot.shopCache[j];
                if (!c) continue;
                if (c.pedX.toFixed(1) + ',' + c.pedY.toFixed(1) === key) {
                    handymanBot.shopCache[j] = entry;
                    exists = true;
                    break;
                }
            }
            if (!exists) handymanBot.shopCache.push(entry);
        }
    }

    function handymanNpcPos() {
        try {
            var pd = mp.handyman && mp.handyman.pedData;
            if (pd && pd.position) {
                if (pd.marker && pd.marker.x != null) {
                    return {
                        x: Number(pd.marker.x),
                        y: Number(pd.marker.y),
                        z: Number(pd.marker.z)
                    };
                }
                return handymanStandNear(pd.position, pd.heading, 1.25);
            }
        } catch (e0) {}
        return null;
    }

    function handymanShopModelSet() {
        var want = {};
        for (var i = 0; i < HM_SHOP_MODELS.length; i++) {
            try {
                want[mp.game.joaat(HM_SHOP_MODELS[i])] = 1;
            } catch (eH) {}
        }
        return want;
    }

    // NPC only near supermarket biz blip — never nearest random shopkeep
    function handymanScanSuperPeds(bizPos, maxFromBiz) {
        if (!bizPos) return null;
        var want = handymanShopModelSet();
        var maxD = (maxFromBiz || 45) * (maxFromBiz || 45);
        var best = null, bestD = 1e15;
        try {
            mp.peds.forEach(function (ped) {
                if (!ped) return;
                try {
                    if (typeof mp.peds.exists === 'function' &&
                        !mp.peds.exists(ped))
                        return;
                } catch (eEx) {}
                if (!want[ped.model]) return;
                var pp = ped.position;
                if (!pp) return;
                var dBiz = handymanDist2(pp, bizPos);
                if (dBiz > maxD) return;
                if (dBiz < bestD) {
                    bestD = dBiz;
                    var heading = 0;
                    try {
                        if (typeof ped.getHeading === 'function')
                            heading = ped.getHeading();
                    } catch (eHd) {}
                    best = handymanStandNear(
                        { x: pp.x, y: pp.y, z: pp.z }, heading, 0.85);
                }
            });
        } catch (eF) {}
        return best;
    }

    // Prefer supermarket.npc cache / ped near biz type 1
    function handymanShopPos() {
        var biz = handymanSupermarketBiz();
        handymanBot.shopBizPos = biz;
        if (!biz) return null;

        var maxD = 45 * 45;
        var best = null, bestD = 1e15;
        var cache = handymanBot.shopCache || [];
        for (var i = 0; i < cache.length; i++) {
            var c = cache[i];
            if (!c || c.source !== 'supermarket') continue;
            var pedP = { x: c.pedX, y: c.pedY, z: c.pedZ };
            var dBiz = handymanDist2(pedP, biz);
            if (dBiz > maxD) continue;
            var stand = c.fromMarker
                ? { x: c.x, y: c.y, z: c.z }
                : handymanStandNear(pedP, c.heading, 0.85);
            if (!stand) continue;
            if (dBiz < bestD) {
                bestD = dBiz;
                best = stand;
            }
        }
        if (best) return best;

        var scanned = handymanScanSuperPeds(biz, 45);
        if (scanned) return scanned;

        // Stream anchor: biz marker until cashier ped loads
        return {
            x: biz.x, y: biz.y, z: biz.z,
            streamAnchor: true
        };
    }

    function handymanNearPos(pos, maxDist) {
        var p = localPos();
        if (!pos || !p) return false;
        var dx = p.x - pos.x, dy = p.y - pos.y, dz = p.z - pos.z;
        var m = maxDist || 4;
        return dx * dx + dy * dy + dz * dz <= m * m;
    }

    function handymanNearShop(maxDist) {
        var shop = handymanShopPos();
        if (!shop || shop.streamAnchor) return false;
        return handymanNearPos(shop, maxDist || 4);
    }

    function handymanEnterShopLocal() {
        try {
            if (mp.shop && typeof mp.shop.enterColshape === 'function')
                mp.shop.enterColshape('supermarket');
        } catch (e0) {}
    }

    function handymanBuyCart(items) {
        if (!items || !items.length) return false;
        if (Date.now() < handymanBot.buyCooldown) return false;
        rem('supermarket.buy', JSON.stringify(items), 0);
        handymanBot.buyCooldown = Date.now() + 1500;
        handymanStatus('buy @shop x' + items.length);
        return true;
    }

    function handymanServerHandsId() {
        try {
            return Number(mp.players.local.getVariable('hands')) || 0;
        } catch (e0) { return 0; }
    }

    // Equip to hands (body index 12) — CEF moveItemToHands + rem fallback
    function handymanEquip(sqlId) {
        sqlId = Number(sqlId);
        if (!sqlId) return false;
        try {
            var code = "(function(){try{var S=window.app.config.globalProperties.$store;"
                + "var st=S.state.inventoryState;if(!st||st.handsBlock)return;"
                + "var id=" + sqlId + ",cr=window.callRemote;"
                + "var h=st.bodyItems&&st.bodyItems[12];"
                + "if(h&&Number(h.sqlId)===id)return;"
                + "if(h){var f=S.getters['inventoryState/findFreeSlot'](h.itemId);"
                + "if(f){var c=S.getters['inventoryState/clientToServerType'](f.place);"
                + "cr('item.add',{sqlId:h.sqlId,index:f.index,placeSqlId:c.parentId,pocketI:c.pocketI});}}"
                + "cr('item.add',{sqlId:id,index:12,placeSqlId:null,pocketI:null});"
                + "}catch(e){}})();";
            if (typeof uaCefExecuteCallCefv === 'function')
                uaCefExecuteCallCefv(code);
        } catch (eCef) {}
        rem('item.add', JSON.stringify({
            sqlId: sqlId,
            index: 12,
            placeSqlId: null,
            pocketI: null
        }));
        return true;
    }

    function handymanHandsReady(itemId) {
        itemId = Number(itemId) || 0;
        if (!itemId) return true; // electrician — no tool
        var srv = handymanServerHandsId();
        if (srv === itemId) return true;
        var inv = global.__uaHmInv || {};
        if (Number(inv.handsItemId) === itemId &&
            Number(inv.handsSqlId) > 0)
            return true;
        return false;
    }

    function handymanSqlForStep(step, inv) {
        inv = inv || global.__uaHmInv || {};
        if (!step) return 0;
        if (step.itemId === HM_SKIMMER)
            return Number(inv.skimmerSqlId || handymanBot.skimmerSqlId) || 0;
        if (step.itemId === HM_TOWEL)
            return Number(inv.towelSqlId || handymanBot.towelSqlId) || 0;
        if (step.itemId === HM_CHLORINE)
            return Number(inv.chlorineSqlId) || 0;
        return 0;
    }
)UAJS"
        R"UAJS(
    function handymanHoldClear() {
        if (handymanBot.holdTimerId != null) {
            try { clearInterval(handymanBot.holdTimerId); } catch (e) {}
            handymanBot.holdTimerId = null;
        }
        handymanBot.holdPos = null;
        try {
            var p = mp.players.local;
            p.freezePosition(false);
            try { p.setCollision(true, true); } catch (e0) {}
        } catch (e1) {}
    }

    // Clear leftover wash/tablet busy so poolwasher.start is accepted
    function handymanClearPoolBusy() {
        try {
            mp.callVUEX('minigames/updatePoolSkimmer', false);
            mp.callVUEX('minigames/updatePoolTabletShow', false);
            mp.callVUEX('audioState/stopById', 'minigame');
        } catch (e0) {}
        try {
            if (mp.busy) {
                try { mp.busy.remove('washGame'); } catch (e1) {}
                try { mp.busy.remove('tabletGame'); } catch (e2) {}
            }
        } catch (e3) {}
        try { mp.inventory.enable(true); } catch (e4) {}
        try { mp.callCEFV('playerMenu.enable = true'); } catch (e5) {}
    }

    // keepCol=true at job markers so server colshapes can fire
    // soft=true: freeze in place, do NOT spam setCoords (kills colshapes)
    function handymanHoldAt(pos, keepCol, soft) {
        if (!pos) return;
        soft = !!soft;
        var same = handymanBot.holdPos &&
            Math.abs(handymanBot.holdPos.x - pos.x) < 0.05 &&
            Math.abs(handymanBot.holdPos.y - pos.y) < 0.05 &&
            Math.abs(handymanBot.holdPos.z - pos.z) < 0.05 &&
            !!handymanBot.holdKeepCol === !!keepCol &&
            !!handymanBot.holdSoft === soft &&
            handymanBot.holdTimerId != null;
        handymanBot.holdPos = { x: pos.x, y: pos.y, z: pos.z };
        handymanBot.holdKeepCol = !!keepCol;
        handymanBot.holdSoft = soft;
        if (same) return;
        try {
            var p = mp.players.local;
            var needTp = !soft;
            if (soft) {
                try {
                    var c = p.position;
                    var dx = c.x - pos.x, dy = c.y - pos.y, dz = c.z - pos.z;
                    needTp = (dx * dx + dy * dy + dz * dz) > 2.2 * 2.2;
                } catch (eD) { needTp = true; }
            }
            if (needTp) {
                try { mp.events.call('ac:refreshtp'); } catch (e0) {}
                p.setCoordsNoOffset(pos.x, pos.y, pos.z, false, false, false);
            }
            p.freezePosition(true);
            try {
                if (keepCol) p.setCollision(true, true);
                else p.setCollision(false, false);
            } catch (e1) {}
            try { p.setVelocity(0, 0, 0); } catch (e2) {}
        } catch (e) {}
        if (handymanBot.holdTimerId != null) {
            try { clearInterval(handymanBot.holdTimerId); } catch (e3) {}
        }
        handymanBot.holdTimerId = setInterval(function () {
            if (!handymanBot.on || !handymanBot.holdPos) return;
            try {
                var hp = mp.players.local;
                // Inside server shape: never re-TP — exits the colshape
                var inShape = !!handymanBot.inWorkShape ||
                    !!handymanBot.inWindowShape;
                if (handymanBot.holdSoft || inShape) {
                    try { hp.freezePosition(true); } catch (eF) {}
                    try { hp.setVelocity(0, 0, 0); } catch (e4) {}
                    if (handymanBot.holdKeepCol)
                        try { hp.setCollision(true, true); } catch (e5) {}
                    return;
                }
                hp.setCoordsNoOffset(
                    handymanBot.holdPos.x, handymanBot.holdPos.y,
                    handymanBot.holdPos.z, false, false, false);
                try { hp.setVelocity(0, 0, 0); } catch (e4b) {}
                if (handymanBot.holdKeepCol)
                    try { hp.setCollision(true, true); } catch (e5b) {}
            } catch (e6) {}
        }, 200);
    }

    function handymanInJobShape(kind) {
        if (kind === 'window') return !!handymanBot.inWindowShape;
        if (kind === 'wash' || kind === 'tablet')
            return !!handymanBot.inWorkShape &&
                (!handymanBot.workShapeKey ||
                    handymanBot.workShapeKey === kind);
        if (kind === 'electrician') return true;
        return false;
    }

    function handymanJobStart(kind) {
        handymanBot.jobStarted = false;
        handymanBot.jobEnded = false;
        handymanBot.awaitingDone = false;
        handymanBot.blipSeen = false;
        handymanBot.jobStartAt = Date.now();
        handymanBot.stepDoneToken = (handymanBot.stepDoneToken || 0) + 1;
        handymanClearPoolBusy();
        if (kind === 'window') rem('handyman.window.start');
        else if (kind === 'wash')
            rem('handyman.poolwasher.start', 'wash');
        else if (kind === 'tablet')
            rem('handyman.poolwasher.start', 'tablet');
        else if (kind === 'electrician')
            rem('handyman.electrician.start');
    }

    function handymanJobEnd(kind) {
        if (handymanBot.jobEnded) return;
        handymanBot.jobEnded = true;
        handymanBot.awaitingDone = true;
        handymanBot.jobEndAt = Date.now();
        var t = String(kind || '');
        // Prefer client cef path (closes Vuex minigame + remotes)
        try {
            mp.events.call('handyman.cef.poolwasher.game.end', t, true);
        } catch (eCef) {
            if (t === 'window')
                rem('handyman.window.end', true);
            rem('handyman.poolwasher.game.end', t, true);
        }
        if (t === 'electrician')
            rem('handyman.electrician.end', true);
    }

    function handymanNudgeAt(pos, now) {
        if (!pos) return;
        var step = Math.floor((now - (handymanBot.shapeWaitAt || now)) / 700) % 5;
        var offs = [
            { x: 0, y: 0 }, { x: 0.7, y: 0 }, { x: -0.7, y: 0 },
            { x: 0, y: 0.7 }, { x: 0, y: -0.7 }
        ];
        var o = offs[step];
        handymanHoldAt({
            x: pos.x + o.x, y: pos.y + o.y, z: pos.z
        }, true);
    }

    function handymanStepColor(kind) {
        if (kind === 'wash') return 3;
        if (kind === 'tablet') return 4;
        if (kind === 'electrician') return 27;
        if (kind === 'window') return 63;
        return -1;
    }

    // True if handyman map blip for this step still exists near marker
    function handymanStepBlipAlive(step) {
        if (!step || !step.pos) return false;
        var want = handymanStepColor(step.kind);
        if (want < 0) return false;
        var alive = false;
        try {
            mp.blips.forEach(function (blip) {
                if (alive || !blip || !mp.blips.exists(blip)) return;
                var color = -1;
                try {
                    if (typeof blip.getColour === 'function')
                        color = blip.getColour();
                    else if (typeof blip.getColor === 'function')
                        color = blip.getColor();
                    else if (blip.color != null) color = blip.color;
                } catch (eC) {}
                if (color !== want) return;
                var bp = null;
                try {
                    if (typeof blip.getCoords === 'function')
                        bp = blip.getCoords();
                    else bp = blip.position;
                } catch (eP) {}
                if (!bp) return;
                var dx = bp.x - step.pos.x, dy = bp.y - step.pos.y;
                if (dx * dx + dy * dy < 12 * 12) alive = true;
            });
        } catch (e) {}
        return alive;
    }

    function handymanStepMatchesTask(step, type, index) {
        if (!step) return false;
        var t = String(type || '');
        var idx = index != null ? Number(index) : NaN;
        if (t === 'pool' || t === 'wash' || t === 'tablet') {
            if (step.type !== 'pool' && step.kind !== 'wash' &&
                step.kind !== 'tablet')
                return false;
            if (t === 'wash' && step.kind !== 'wash') return false;
            if (t === 'tablet' && step.kind !== 'tablet') return false;
            if (!isNaN(idx)) {
                if (Number(step.itemIndex) === idx) return true;
                if (step.poolPair != null && Number(step.poolPair) === idx)
                    return true;
                return false;
            }
            return step.kind === 'wash' || step.kind === 'tablet';
        }
        if (t === 'window') return step.kind === 'window' &&
            (isNaN(idx) || Number(step.itemIndex) === idx);
        if (t === 'electrician') return step.kind === 'electrician' &&
            (isNaN(idx) || Number(step.itemIndex) === idx);
        return step.type === t &&
            (isNaN(idx) || Number(step.itemIndex) === idx);
    }

    // Server-proven wash → unlocks chlorine colshape
    function handymanUnlockWash(step, reason) {
        if (!step || step.kind !== 'wash') return;
        var r = String(reason || '');
        var ok = r.indexOf('pool.process') === 0 ||
            r.indexOf('blip') === 0 ||
            r === 'ans.washEnded' ||
            r.indexOf('work.process') === 0 ||
            r.indexOf('TASK') === 0;
        if (!ok) return;
        var pair = step.poolPair != null
            ? step.poolPair : step.itemIndex;
        if (!handymanBot.washDonePairs)
            handymanBot.washDonePairs = {};
        handymanBot.washDonePairs[String(pair)] = true;
        handymanBot.clReadyAt = Date.now() + 700;
    }

    // Single place that leaves a marker — never double-advance
    function handymanStepDone(reason) {
        if (!handymanBot.on) return false;
        if (handymanBot.phase !== 'wait_ack' &&
            handymanBot.phase !== 'do_job')
            return false;
        if (handymanBot.stepFinishing) return false;
        var step = handymanBot.queue[handymanBot.qi];
        if (!step) return false;
        handymanBot.stepFinishing = true;
        handymanUnlockWash(step, reason);
        handymanStatus('DONE ' + step.kind + ' · ' + reason);
        handymanAdvance();
        handymanBot.stepFinishing = false;
        return true;
    }

    // Full task paid (work.process) / all blips removed — skip all steps
    function handymanCompleteTask(type, posIndex, reason) {
        if (!handymanBot.on) return;
        var guard = 0;
        while (guard++ < 6) {
            var step = handymanBot.queue[handymanBot.qi];
            if (!handymanStepMatchesTask(step, type, posIndex))
                break;
            if (step.kind === 'wash' || step.type === 'pool') {
                var p = step.poolPair != null
                    ? step.poolPair : step.itemIndex;
                if (!handymanBot.washDonePairs)
                    handymanBot.washDonePairs = {};
                handymanBot.washDonePairs[String(p)] = true;
            }
            handymanStatus(
                'TASK ' + type + '#' + posIndex +
                ' skip ' + step.kind + ' · ' + reason);
            handymanAdvance();
        }
    }

    function handymanOnBlipRemove(type, index, removedStep) {
        if (!handymanBot.on) return;
        var t = String(type || '');
        var rs = removedStep == null ? null : Number(removedStep);
        var step = handymanBot.queue[handymanBot.qi];
        if (!step) return;
        // removedStep null → whole task
        if (rs == null || isNaN(rs)) {
            handymanCompleteTask(t, index, 'blip.remove');
            return;
        }
        // pool: 0=wash, 1=tablet
        if (t === 'pool') {
            var needKind = rs === 0 ? 'wash' : (rs === 1 ? 'tablet' : '');
            if (needKind && step.kind === needKind &&
                handymanStepMatchesTask(step, 'pool', index)) {
                handymanStepDone('blip.' + needKind);
                return;
            }
            if (needKind === 'wash') {
                var p = index;
                if (!handymanBot.washDonePairs)
                    handymanBot.washDonePairs = {};
                handymanBot.washDonePairs[String(p)] = true;
            }
            return;
        }
        if (handymanStepMatchesTask(step, t, index))
            handymanStepDone('blip.remove');
    }

    function handymanAdvance() {
        handymanBot.qi++;
        handymanBot.waitProcess = false;
        handymanBot.waitKind = '';
        handymanBot.jobStarted = false;
        handymanBot.jobEnded = false;
        handymanBot.awaitingDone = false;
        handymanBot.blipSeen = false;
        handymanBot.jobRetries = 0;
        handymanBot.shapeSettledAt = 0;
        handymanBot.inWorkShape = false;
        handymanBot.workShapeKey = '';
        handymanBot.inWindowShape = false;
        if (handymanBot.qi >= handymanBot.queue.length) {
            handymanHoldClear();
            handymanBot.phase = 'done';
            handymanStatus('queue done');
            return;
        }
        handymanBot.phase = 'travel';
        handymanBot.busyUntil = Date.now() + 200;
    }

    function handymanApplyTasks(data, source) {
        var q = [];
        var tasksCount = 0;
        // blips scrape already returns flat steps with .kind
        if (source === 'blips' && Array.isArray(data) &&
            data.length && data[0] && data[0].kind) {
            q = handymanOrderPoolQueue(data.slice());
            tasksCount = q.length;
        } else {
            var tasks = handymanNormalizeTasks(data);
            tasksCount = tasks.length;
            q = handymanBuildQueue(tasks);
            // Do NOT silently mix in world blips here — wrong counts
        }
        handymanBot.tasksRaw = data;
        handymanBot.tasksCount = tasksCount;
        handymanBot.queue = q;
        handymanBot.washDonePairs = {};
        handymanBot.qi = 0;
        handymanBot.chlorineNeed = handymanCountChlorineNeed(q);
        handymanBot.taskSummary = handymanSummary(q, tasksCount);
        handymanStatus(
            (source || 'tasks') + ' · ' + handymanBot.taskSummary +
            ' · Cl ' + handymanBot.chlorineNeed);
        if (handymanBot.on && q.length) {
            // stay at handyman NPC until at_npc window ends
            if (handymanBot.phase === 'at_npc') {
                handymanStatus(
                    'queued @NPC · ' + handymanBot.taskSummary);
                return;
            }
            handymanBot.phase = 'prep_inv';
            handymanBot.busyUntil = Date.now() + 100;
            handymanInvProbe();
        } else if (handymanBot.on && !q.length) {
            if (handymanBot.phase !== 'at_npc')
                handymanBot.phase = 'wait_tasks';
            handymanStatus('no markers parsed · waiting…');
        }
    }
)UAJS"
        R"UAJS(
    function handymanEnsureHooks() {
        if (handymanBot.hooksReady) return;
        handymanBot.hooksReady = true;
        try {
            mp.events.add('handyman.work.start', function () {
                var args = Array.prototype.slice.call(arguments);
                var data = handymanExtractWorkData(args);
                handymanApplyTasks(data, 'work.start');
            });
        } catch (e0) {}
        try {
            // Full task paid — args: type, posIndex, curPay, totalPay, done, total
            mp.events.add('handyman.work.process', function (type, posIndex) {
                if (!handymanBot.on) return;
                handymanCompleteTask(
                    String(type || ''), posIndex, 'work.process');
            });
        } catch (e1) {}
        try {
            // Step finished (wash/tablet minigame accepted)
            mp.events.add('handyman.poolwasher.process', function (type) {
                if (!handymanBot.on) return;
                var t = String(type || '');
                var step = handymanBot.queue[handymanBot.qi];
                // Strict kind match only (wash process must not finish Cl)
                if (step && step.kind === t)
                    handymanStepDone('pool.process:' + t);
            });
        } catch (e1b) {}
        try {
            mp.events.add('handyman.blip.remove',
                function (type, index, removedStep) {
                    if (!handymanBot.on) return;
                    handymanOnBlipRemove(type, index, removedStep);
                });
        } catch (e1b2) {}
        try {
            mp.events.add('handyman.poolwasher.workShapeEnter',
                function (key) {
                    handymanBot.inWorkShape = true;
                    handymanBot.workShapeKey = String(key || '');
                    // Soft-hold so setCoords spam does not exit shape
                    if (handymanBot.on && handymanBot.holdPos)
                        handymanHoldAt(handymanBot.holdPos, true, true);
                    if (handymanBot.on)
                        handymanStatus('shape ' + handymanBot.workShapeKey);
                });
        } catch (e1c) {}
        try {
            mp.events.add('handyman.poolwasher.workShapeExit',
                function () {
                    handymanBot.inWorkShape = false;
                    handymanBot.workShapeKey = '';
                    // Only after real action+end (not fake timeout end)
                    if (handymanBot.on && handymanBot.awaitingDone &&
                        handymanBot.jobEnded && handymanBot.jobStarted &&
                        handymanBot.phase === 'wait_ack') {
                        var since = Date.now() -
                            (handymanBot.jobEndAt || 0);
                        if (since > 150)
                            handymanStepDone('blip.shape.exit');
                    }
                });
        } catch (e1d) {}
        try {
            mp.events.add('handyman.window.shape', function (isIn) {
                var was = handymanBot.inWindowShape;
                handymanBot.inWindowShape = !!isIn;
                if (handymanBot.on && isIn)
                    handymanStatus('window shape');
                if (handymanBot.on && was && !isIn &&
                    handymanBot.awaitingDone && handymanBot.jobEnded &&
                    handymanBot.jobStarted &&
                    handymanBot.phase === 'wait_ack')
                    handymanStepDone('blip.window.exit');
            });
        } catch (e1e) {}
        try {
            // Server accepted pool start → open minigame → we finish it
            mp.events.add('handyman.poolwasher.action', function (type) {
                if (!handymanBot.on) return;
                if (handymanBot.phase !== 'do_job' &&
                    handymanBot.phase !== 'wait_ack')
                    return;
                handymanBot.jobStarted = true;
                var t = String(type || '');
                handymanStatus('action ' + t);
                // tablet CEF needs a beat before game.end
                var delay = t === 'tablet' ? 900 : 350;
                setTimeout(function () {
                    if (handymanBot.on) handymanJobEnd(t);
                }, delay);
            });
        } catch (e1f) {}
        try {
            // Server accepted window.start → client minigame begins
            mp.events.add('handyman.window.start', function () {
                if (!handymanBot.on) return;
                if (handymanBot.phase !== 'do_job' &&
                    handymanBot.phase !== 'wait_ack')
                    return;
                handymanBot.jobStarted = true;
                handymanStatus('window action');
                setTimeout(function () {
                    if (handymanBot.on) handymanJobEnd('window');
                }, 250);
            });
        } catch (e1g) {}
        try {
            mp.events.add('handyman.work.end', function () {
                if (!handymanBot.on) return;
                handymanStatus('work.end');
                handymanBotStop();
            });
        } catch (e2) {}
        try {
            mp.events.add('handyman.ans', function (type) {
                if (!handymanBot.on) return;
                var t = String(type || '');
                if (t.indexOf('Hands') >= 0 || t.indexOf('hands') >= 0) {
                    handymanBot.phase = 'equip';
                    handymanBot.equipSent = false;
                    handymanBot.equipRetries = 0;
                    handymanBot.pauseUntil = Date.now() + 400;
                    handymanStatus('ans ' + t + ' → re-equip');
                    handymanInvProbe();
                    return;
                }
                if (t.indexOf('Shape') >= 0 || t.indexOf('shape') >= 0 ||
                    t === 'notInShape') {
                    handymanBot.phase = 'wait_shape';
                    handymanBot.shapeWaitAt = Date.now();
                    handymanBot.pauseUntil = Date.now() + 300;
                    handymanStatus('ans ' + t + ' → reshapes');
                    return;
                }
                if (t.indexOf('noHave') === 0) {
                    handymanBot.phase = 'prep_inv';
                    handymanBot.pauseUntil = Date.now() + 1500;
                    handymanStatus('ans ' + t);
                    handymanInvProbe();
                }
            });
        } catch (e3) {}
        try {
            mp.events.add('handyman.poolwasher.ans', function (type) {
                if (!handymanBot.on) return;
                var t = String(type || '');
                // Server notify after wash accepted
                if (t === 'washEnded' || t === 'washended' ||
                    t.indexOf('washEnd') === 0) {
                    handymanStepDone('ans.washEnded');
                    return;
                }
                if (t.indexOf('Hands') >= 0 || t.indexOf('hands') >= 0) {
                    handymanBot.phase = 'equip';
                    handymanBot.equipSent = false;
                    handymanBot.equipRetries = 0;
                    handymanBot.pauseUntil = Date.now() + 400;
                    handymanStatus('pool ' + t + ' → re-equip');
                    handymanInvProbe();
                    return;
                }
                if (t.indexOf('Shape') >= 0 || t.indexOf('shape') >= 0 ||
                    t === 'notInShape') {
                    handymanBot.phase = 'wait_shape';
                    handymanBot.shapeWaitAt = Date.now();
                    handymanStatus('pool ' + t + ' → reshape');
                    return;
                }
                if (t.indexOf('noHave') === 0) {
                    handymanBot.phase = 'prep_inv';
                    handymanBot.pauseUntil = Date.now() + 1500;
                    handymanStatus('pool ans ' + t);
                    handymanInvProbe();
                }
            });
        } catch (e4) {}
        try {
            mp.events.add('shop.colshape.enter', function (type) {
                var t = String(type || '');
                if (t === 'supermarket')
                    handymanBot.inSuperShape = true;
                else if (handymanBot.on &&
                    (handymanBot.phase === 'shop_tp' ||
                        handymanBot.phase === 'shop_buy'))
                    handymanStatus('wrong shop shape: ' + t);
            });
        } catch (e5) {}
        try {
            mp.events.add('shop.colshape.exit', function () {
                handymanBot.inSuperShape = false;
            });
        } catch (e6) {}
        try {
            mp.events.add('shop.menu.open', function () {
                handymanBot.shopMenuOpen = true;
                if (handymanBot.on &&
                    (handymanBot.phase === 'shop_buy' ||
                        handymanBot.shopSub === 'wait_menu'))
                    handymanStatus('shop menu open');
            });
        } catch (e7) {}
        try {
            mp.events.add('shop.buy.ans', function (type) {
                if (!handymanBot.on) return;
                if (type === 5 || type === '5')
                    handymanStatus('shop buy ok');
                else
                    handymanStatus('shop buy ans ' + type);
            });
        } catch (e8) {}
    }
)UAJS"
        R"UAJS(
    function handymanPrepInvTick(now) {
        if (handymanBot.probePending && now - handymanBot.probeAt < 2500)
            return;
        if (handymanBot.probePending && now - handymanBot.probeAt >= 2500) {
            handymanBot.probePending = false;
            handymanStatus('inv probe timeout');
        }
        var inv = global.__uaHmInv;
        if (!inv || inv.error) {
            if (now > handymanBot.busyUntil) {
                handymanBot.busyUntil = now + 800;
                handymanInvProbe();
            }
            return;
        }
        if (inv.broken && inv.broken.length)
            handymanDropBroken(inv.broken);

        var needSkimmer = false, needTowel = false, needCl = 0;
        for (var i = handymanBot.qi; i < handymanBot.queue.length; i++) {
            var s = handymanBot.queue[i];
            if (!s) continue;
            if (s.kind === 'wash') needSkimmer = true;
            if (s.kind === 'window') needTowel = true;
            if (s.kind === 'tablet') needCl++;
        }
        var haveCl = Number(inv.chlorineQty) || 0;
        var cart = [];
        if (needSkimmer && !(inv.skimmerSqlId > 0 && Number(inv.skimmerHp) > 1))
            cart.push({ id: HM_SKIMMER, quantity: 1 });
        if (needTowel && !(inv.towelSqlId > 0 && Number(inv.towelHp) > 1))
            cart.push({ id: HM_TOWEL, quantity: 1 });
        if (needCl > haveCl)
            cart.push({ id: HM_CHLORINE, quantity: needCl - haveCl });

        if (cart.length) {
            handymanBot.pendingBuy = cart;
            if (!handymanSupermarketBiz()) {
                handymanStatus('no supermarket biz (locator)');
                handymanBot.pauseUntil = now + 1200;
                return;
            }
            handymanBot.phase = 'shop_tp';
            handymanBot.shopSub = 'seek';
            handymanBot.shopMenuOpen = false;
            handymanBot.inSuperShape = false;
            handymanBot.shopAt = now;
            handymanBot.busyUntil = now + 100;
            handymanStatus('go supermarket (biz type 1)');
            return;
        }

        handymanBot.pendingBuy = null;
        handymanBot.phase = 'travel';
        handymanBot.busyUntil = now + 100;
        handymanStatus('inv ok → travel');
    }

    function handymanCloseShopMenu() {
        try {
            if (mp.shop && typeof mp.shop.closeMenu === 'function')
                mp.shop.closeMenu();
        } catch (e0) {}
        try { mp.events.call('shop.close'); } catch (e1) {}
        try { mp.events.call('shop.colshape.exit'); } catch (e2) {}
        handymanBot.shopMenuOpen = false;
    }

    function handymanShopTpTick(now) {
        var shopPos = handymanShopPos();
        if (!shopPos) {
            handymanStatus('no supermarket biz');
            handymanBot.pauseUntil = now + 1000;
            return;
        }
        // 1) stream anchor at biz if cashier not loaded yet
        if (shopPos.streamAnchor) {
            handymanHoldAt(shopPos);
            handymanBot.busyUntil = now + 700;
            handymanStatus('stream supermarket…');
            return;
        }
        // 2) TP to cashier NPC of supermarket only
        if (!handymanNearPos(shopPos, 3.5)) {
            handymanHoldAt(shopPos);
            handymanBot.shopAt = now; // timer starts on arrival
            handymanBot.busyUntil = now + 500;
            handymanStatus('TP supermarket cashier');
            return;
        }
        handymanHoldAt(shopPos);
        // Wait for SERVER colshape (not fake fishing/other)
        if (!handymanBot.inSuperShape) {
            var waitedShape = now - (handymanBot.shopAt || now);
            if (waitedShape > 2500) {
                // nudge around cashier to hit server shape
                var step = Math.floor(waitedShape / 800) % 4;
                var off = [
                    { x: 0.6, y: 0 }, { x: -0.6, y: 0 },
                    { x: 0, y: 0.6 }, { x: 0, y: -0.6 }
                ][step];
                handymanHoldAt({
                    x: shopPos.x + off.x,
                    y: shopPos.y + off.y,
                    z: shopPos.z
                });
                handymanStatus('seek supermarket colshape…');
            }
            handymanBot.busyUntil = now + 400;
            return;
        }
        handymanBot.phase = 'shop_buy';
        handymanBot.shopSub = 'open';
        handymanBot.shopMenuOpen = false;
        handymanBot.busyUntil = now + 200;
        handymanStatus('in supermarket shape');
    }

    function handymanShopBuyTick(now) {
        var shopPos = handymanShopPos();
        if (!shopPos || shopPos.streamAnchor ||
            !handymanNearPos(shopPos, 5)) {
            handymanBot.phase = 'shop_tp';
            handymanBot.shopSub = 'seek';
            handymanBot.shopMenuOpen = false;
            handymanBot.busyUntil = now + 200;
            return;
        }
        handymanHoldAt(shopPos);
        var cart = handymanBot.pendingBuy || [];
        if (!cart.length) {
            handymanCloseShopMenu();
            handymanBot.phase = 'prep_inv';
            handymanBot.shopSub = '';
            handymanBot.busyUntil = now + 400;
            handymanInvProbe();
            return;
        }

        if (!handymanBot.inSuperShape) {
            handymanBot.phase = 'shop_tp';
            handymanBot.shopSub = 'seek';
            handymanBot.busyUntil = now + 300;
            handymanStatus('lost supermarket shape');
            return;
        }

        var sub = handymanBot.shopSub || 'open';
        if (sub === 'open') {
            handymanBot.shopMenuOpen = false;
            handymanBot.shopOpenRetries = 0;
            handymanEnterShopLocal();
            try { rem('supermarket.menu.open'); } catch (e0) {}
            handymanBot.shopSub = 'wait_menu';
            handymanBot.shopAt = now;
            handymanBot.busyUntil = now + 500;
            handymanStatus('open supermarket menu…');
            return;
        }
        if (sub === 'wait_menu') {
            if (!handymanBot.shopMenuOpen &&
                now - (handymanBot.shopAt || now) < 2200) {
                handymanBot.busyUntil = now + 350;
                return;
            }
            if (!handymanBot.shopMenuOpen &&
                handymanBot.shopOpenRetries < 2) {
                handymanBot.shopOpenRetries++;
                handymanEnterShopLocal();
                try { rem('supermarket.menu.open'); } catch (e1) {}
                handymanBot.shopAt = now;
                handymanBot.busyUntil = now + 700;
                handymanStatus(
                    'retry menu #' + handymanBot.shopOpenRetries);
                return;
            }
            if (!handymanBot.shopMenuOpen) {
                handymanStatus('menu failed — still in shape?');
                handymanBot.phase = 'shop_tp';
                handymanBot.shopSub = 'seek';
                handymanBot.inSuperShape = false;
                handymanBot.busyUntil = now + 500;
                return;
            }
            handymanBot.shopSub = 'buy';
            handymanBot.busyUntil = now + 250;
            return;
        }
        if (sub === 'buy') {
            if (Date.now() < handymanBot.buyCooldown) {
                handymanBot.pauseUntil = handymanBot.buyCooldown;
                return;
            }
            var bought = false;
            try {
                if (mp.shop && typeof mp.shop.buy === 'function') {
                    mp.shop.buy(JSON.stringify(cart), 0);
                    bought = true;
                }
            } catch (eBuy) {}
            if (!bought) {
                try {
                    mp.events.call(
                        'shop.buy', JSON.stringify(cart), 0);
                    bought = true;
                } catch (eBuy2) {}
            }
            if (!bought) bought = handymanBuyCart(cart);
            if (!bought) {
                handymanBot.pauseUntil = now + 1200;
                return;
            }
            handymanBot.buyCooldown = Date.now() + 1500;
            handymanBot.pendingBuy = null;
            handymanBot.shopAt = now;
            handymanBot.shopSub = 'close';
            handymanBot.busyUntil = now + 900;
            handymanStatus('bought @supermarket');
            return;
        }
        handymanCloseShopMenu();
        handymanBot.shopSub = '';
        handymanBot.phase = 'prep_inv';
        handymanBot.busyUntil = now + 600;
        setTimeout(function () {
            if (handymanBot.on) handymanInvProbe();
        }, 700);
    }

)UAJS"
        R"UAJS(
    function handymanTick() {
        if (!handymanBot.on) return;
        var now = Date.now();
        if (now < handymanBot.pauseUntil) return;
        if (now < handymanBot.busyUntil) return;

        if (handymanBot.phase === 'wait_tasks') {
            if (handymanBot.queue && handymanBot.queue.length) {
                handymanBot.phase = 'prep_inv';
                handymanInvProbe();
                return;
            }
            var waited = now - (handymanBot.workStartAt || now);
            if (waited > 2000 && handymanBot.workStartRetries < 2) {
                handymanBot.workStartRetries++;
                try { rem('handyman.work.start'); } catch (eR) {}
                handymanStatus(
                    'retry work.start #' + handymanBot.workStartRetries);
                handymanBot.busyUntil = now + 1500;
                return;
            }
            // Last resort after retries: filtered handyman blips only
            if (waited > 4500) {
                var fromBlips = handymanQueueFromBlips();
                if (fromBlips.length) {
                    handymanApplyTasks(fromBlips, 'blips');
                    return;
                }
            }
            handymanStatus('waiting markers… ' + handymanBot.taskSummary);
            handymanBot.busyUntil = now + 800;
            return;
        }

        if (handymanBot.phase === 'at_npc') {
            var npcPos = handymanNpcPos();
            if (!npcPos) {
                handymanStatus('no handyman NPC (pedData)');
                handymanBot.busyUntil = now + 1000;
                return;
            }
            handymanHoldAt(npcPos);
            if (now < handymanBot.atNpcUntil) {
                handymanBot.busyUntil = now + 200;
                return;
            }
            // Prefer work.start payload — never scrape blips here
            handymanBot.phase = 'wait_tasks';
            if (handymanBot.queue && handymanBot.queue.length) {
                handymanBot.phase = 'prep_inv';
                handymanBot.busyUntil = now + 100;
                handymanInvProbe();
            } else {
                handymanBot.workStartAt = now;
                try {
                    rem('handyman.work.start');
                    handymanStatus('at NPC · sent work.start');
                } catch (eStartNpc) {
                    handymanStatus('work.start err: ' + eStartNpc);
                }
                handymanBot.busyUntil = now + 800;
            }
            return;
        }

        if (handymanBot.phase === 'shop_tp') {
            handymanShopTpTick(now);
            return;
        }

        if (handymanBot.phase === 'shop_buy') {
            handymanShopBuyTick(now);
            return;
        }

        if (handymanBot.phase === 'prep_inv') {
            handymanPrepInvTick(now);
            return;
        }

        if (handymanBot.phase === 'done') {
            handymanBot.busyUntil = now + 2000;
            return;
        }

        var step = handymanBot.queue[handymanBot.qi];
        if (!step) {
            handymanBot.phase = 'done';
            handymanHoldClear();
            handymanStatus('queue empty');
            return;
        }

        if (handymanBot.phase === 'travel') {
            handymanHoldAt(step.pos, true);
            handymanBot.phase = 'equip';
            handymanBot.equipSent = false;
            handymanBot.equipRetries = 0;
            handymanBot.busyUntil = now + 350;
            handymanStatus('TP ' + step.kind + ' #' + handymanBot.qi);
            return;
        }

        if (handymanBot.phase === 'equip') {
            handymanHoldAt(step.pos, true);
            // No tool needed (electrician)
            if (!step.itemId) {
                handymanBot.phase = 'wait_shape';
                handymanBot.shapeWaitAt = now;
                handymanBot.busyUntil = now + 200;
                return;
            }
            if (handymanHandsReady(step.itemId)) {
                handymanBot.phase = 'wait_shape';
                handymanBot.shapeWaitAt = now;
                handymanBot.equipSent = false;
                handymanBot.busyUntil = now + 200;
                handymanStatus('hands ok → wait shape');
                return;
            }
            if (!handymanBot.equipSent ||
                (now - (handymanBot.equipAt || 0) > 1400 &&
                    handymanBot.equipRetries < 4)) {
                var invE = global.__uaHmInv || {};
                if (!invE || invE.error ||
                    !handymanSqlForStep(step, invE)) {
                    if (!handymanBot.probePending)
                        handymanInvProbe();
                    handymanBot.busyUntil = now + 400;
                    if (handymanBot.equipRetries >= 3) {
                        handymanBot.phase = 'prep_inv';
                        handymanStatus('no tool sqlId → shop/inv');
                    }
                    return;
                }
                var sql = handymanSqlForStep(step, invE);
                handymanEquip(sql);
                handymanBot.equipSent = true;
                handymanBot.equipAt = now;
                handymanBot.equipRetries++;
                handymanBot.busyUntil = now + 900;
                handymanStatus(
                    'equip id=' + step.itemId +
                    ' sql=' + sql +
                    ' #' + handymanBot.equipRetries);
                setTimeout(function () {
                    if (handymanBot.on) handymanInvProbe();
                }, 500);
                return;
            }
            // Waiting for server hands sync
            if (now - (handymanBot.equipAt || 0) < 1600) {
                handymanBot.busyUntil = now + 300;
                return;
            }
            if (handymanBot.equipRetries >= 4) {
                handymanStatus(
                    'equip fail hands=' + handymanServerHandsId() +
                    ' need=' + step.itemId);
                handymanBot.phase = 'prep_inv';
                handymanBot.pauseUntil = now + 1000;
                handymanInvProbe();
                return;
            }
            handymanBot.equipSent = false;
            handymanBot.busyUntil = now + 200;
            return;
        }

        if (handymanBot.phase === 'wait_shape') {
            var inSh = handymanInJobShape(step.kind);
            handymanHoldAt(step.pos, true, inSh);
            if (step.itemId && !handymanHandsReady(step.itemId)) {
                handymanBot.phase = 'equip';
                handymanBot.equipSent = false;
                handymanBot.busyUntil = now + 200;
                return;
            }
            if (inSh || step.kind === 'electrician') {
                handymanBot.phase = 'do_job';
                handymanBot.busyUntil = now + 150;
                handymanStatus(
                    'in shape · ' + step.kind +
                    ' hands=' + handymanServerHandsId());
                return;
            }
            // Nudge to hit server colshape
            if (now - (handymanBot.shapeWaitAt || now) > 600)
                handymanNudgeAt(step.pos, now);
            var shapeLim = step.kind === 'tablet' ? 12000 : 5000;
            if (now - (handymanBot.shapeWaitAt || now) > shapeLim) {
                // tablet: never force outside shape (Cl colshape gated)
                if (step.kind === 'tablet') {
                    handymanNudgeAt(step.pos, now);
                    handymanBot.shapeWaitAt = now;
                    handymanStatus('Cl shape missing — keep hunting');
                    handymanBot.busyUntil = now + 400;
                    return;
                }
                handymanBot.phase = 'do_job';
                handymanStatus('shape timeout → try job');
                handymanBot.busyUntil = now + 150;
                return;
            }
            handymanBot.busyUntil = now + 350;
            handymanStatus(
                'wait shape ' + step.kind +
                (handymanBot.workShapeKey
                    ? (' (' + handymanBot.workShapeKey + ')')
                    : ''));
            return;
        }

        if (handymanBot.phase === 'do_job') {
            if (step.itemId && !handymanHandsReady(step.itemId)) {
                handymanBot.phase = 'equip';
                handymanBot.equipSent = false;
                handymanBot.busyUntil = now + 200;
                handymanStatus('block job — hands empty');
                return;
            }
            // Chlorine colshape is server-locked until wash of this pool is done
            if (step.kind === 'tablet') {
                var pk = String(
                    step.poolPair != null ? step.poolPair : step.itemIndex);
                var needWash = step.needsWashFirst !== false;
                var washed = handymanBot.washDonePairs &&
                    handymanBot.washDonePairs[pk];
                if (needWash && pk && pk !== 'undefined' && !washed) {
                    var redo = null;
                    for (var ri = 0; ri < handymanBot.queue.length; ri++) {
                        var rs = handymanBot.queue[ri];
                        if (rs && rs.kind === 'wash' &&
                            String(rs.poolPair != null
                                ? rs.poolPair : rs.itemIndex) === pk) {
                            redo = rs;
                            break;
                        }
                    }
                    if (redo && redo.pos) {
                        handymanBot.queue.splice(handymanBot.qi, 0, {
                            kind: 'wash', type: 'pool',
                            itemIndex: redo.itemIndex,
                            pos: redo.pos, itemId: HM_SKIMMER,
                            poolPair: redo.poolPair != null
                                ? redo.poolPair : pk
                        });
                        handymanBot.phase = 'travel';
                        handymanBot.busyUntil = now + 200;
                        handymanStatus('Cl locked — wash first');
                        return;
                    }
                    handymanStatus('Cl locked — no wash record');
                    handymanBot.busyUntil = now + 800;
                    return;
                }
                // Let server spawn tablet colshape after wash
                if (handymanBot.clReadyAt && now < handymanBot.clReadyAt) {
                    handymanHoldAt(step.pos, true);
                    handymanStatus('Cl wait shape spawn…');
                    handymanBot.busyUntil = now + 200;
                    return;
                }
                // Must be in tablet bind — don't spam start outside shape
                if (!handymanInJobShape('tablet')) {
                    handymanBot.phase = 'wait_shape';
                    handymanBot.shapeWaitAt = now;
                    handymanStatus('Cl → wait tablet shape');
                    handymanBot.busyUntil = now + 200;
                    return;
                }
            }
            handymanHoldAt(step.pos, true, true);
            handymanBot.jobRetries = 0;
            // Small settle so server sees us still inside colshape
            if (!handymanBot.shapeSettledAt) {
                handymanBot.shapeSettledAt = now;
                handymanStatus('shape settle ' + step.kind);
                handymanBot.busyUntil = now + 350;
                return;
            }
            if (now - handymanBot.shapeSettledAt < 300) {
                handymanBot.busyUntil = now + 100;
                return;
            }
            handymanBot.shapeSettledAt = 0;
            handymanJobStart(step.kind);
            handymanBot.waitKind = step.kind;
            handymanBot.waitProcess = true;
            handymanBot.phase = 'wait_ack';
            handymanBot.busyUntil = now + 800;
            handymanStatus(
                'job start ' + step.kind +
                ' hands=' + handymanServerHandsId() +
                ' key=' + (handymanBot.workShapeKey || '-'));
            return;
        }

        if (handymanBot.phase === 'wait_ack') {
            handymanHoldAt(step.pos, true, true);
            var waited = now - (handymanBot.jobStartAt || now);
            var endWait = handymanBot.jobEnded
                ? (now - (handymanBot.jobEndAt || now)) : 0;

            // Track blip: seen → gone = server removed marker = DONE
            var blipAlive = handymanStepBlipAlive(step);
            if (blipAlive) handymanBot.blipSeen = true;
            if (handymanBot.awaitingDone && handymanBot.jobStarted &&
                handymanBot.blipSeen && !blipAlive) {
                handymanStepDone('blip.gone');
                return;
            }

            // ONLY end minigame after server opened it (action)
            if (!handymanBot.jobEnded && handymanBot.jobStarted &&
                waited > 400)
                handymanJobEnd(step.kind);

            // Fake end without action — reshape / retry, never fake-DONE
            if (handymanBot.jobEnded && !handymanBot.jobStarted) {
                handymanBot.jobEnded = false;
                handymanBot.awaitingDone = false;
                handymanBot.shapeSettledAt = 0;
                handymanBot.phase = 'wait_shape';
                handymanBot.shapeWaitAt = now;
                handymanStatus('no action → reshape ' + step.kind);
                handymanBot.busyUntil = now + 300;
                return;
            }

            // After real game.end — wait for server process/blip
            if (handymanBot.awaitingDone && handymanBot.jobEnded &&
                handymanBot.jobStarted) {
                if (endWait > 700 && handymanBot.blipSeen && !blipAlive) {
                    handymanStepDone('blip.gone');
                    return;
                }
                // Settled only if action was real
                // wash unlock still needs process/blip (see handymanUnlockWash)
                if (endWait > 2200) {
                    handymanStepDone('end.settled');
                    return;
                }
                handymanStatus(
                    'await done ' + step.kind +
                    ' t=' + Math.floor(endWait / 100) / 10 +
                    (blipAlive ? ' blip' : ' noblip'));
                handymanBot.busyUntil = now + 250;
                return;
            }

            // No action yet — retry start / reshape (esp. tablet shape)
            if (!handymanBot.jobStarted && waited > 1800) {
                if (!handymanInJobShape(step.kind) &&
                    step.kind !== 'electrician') {
                    handymanBot.shapeSettledAt = 0;
                    handymanBot.phase = 'wait_shape';
                    handymanBot.shapeWaitAt = now;
                    handymanNudgeAt(step.pos, now);
                    handymanStatus('no shape → nudge ' + step.kind);
                    handymanBot.busyUntil = now + 400;
                    return;
                }
                if ((handymanBot.jobRetries || 0) < 5) {
                    handymanBot.jobRetries =
                        (handymanBot.jobRetries || 0) + 1;
                    handymanJobStart(step.kind);
                    handymanStatus(
                        'retry start #' + handymanBot.jobRetries +
                        ' ' + step.kind +
                        ' hands=' + handymanServerHandsId());
                    handymanBot.busyUntil = now + 900;
                    return;
                }
                handymanStatus('start fail ' + step.kind);
                handymanBot.shapeSettledAt = 0;
                handymanBot.phase = 'wait_shape';
                handymanBot.shapeWaitAt = now;
                handymanBot.jobRetries = 0;
                handymanBot.busyUntil = now + 500;
                return;
            }
            handymanBot.busyUntil = now + 400;
            return;
        }
    }
)UAJS"
        R"UAJS(
    function handymanBotStart() {
        jobBotsExclusive('handyman');
        handymanEnsureHooks();
        handymanBot.on = true;
        handymanBot.waitProcess = false;
        handymanBot.pauseUntil = 0;
        handymanBot.busyUntil = 0;
        handymanBot.qi = 0;
        handymanBot.queue = [];
        handymanBot.tasksRaw = null;
        handymanBot.taskSummary = '-';
        handymanBot.workStartRetries = 0;
        handymanBot.workStartAt = Date.now();
        handymanBot.pendingBuy = null;
        handymanBot.shopSub = '';
        handymanBot.shopMenuOpen = false;
        handymanBot.shopOpenRetries = 0;
        handymanBot.inSuperShape = false;
        handymanBot.shopBizPos = null;
        handymanBot.tasksCount = 0;
        handymanBot.equipSent = false;
        handymanBot.equipRetries = 0;
        handymanBot.equipAt = 0;
        handymanBot.inWorkShape = false;
        handymanBot.workShapeKey = '';
        handymanBot.inWindowShape = false;
        handymanBot.jobStarted = false;
        handymanBot.jobEnded = false;
        handymanBot.jobRetries = 0;
        handymanBot.awaitingDone = false;
        handymanBot.blipSeen = false;
        handymanBot.stepFinishing = false;
        handymanBot.washDonePairs = {};
        // Always begin standing at handyman NPC
        handymanBot.phase = 'at_npc';
        handymanBot.atNpcUntil = Date.now() + 1200;
        var npc = handymanNpcPos();
        if (npc) {
            handymanHoldAt(npc);
            handymanStatus('TP handyman NPC');
        } else {
            handymanStatus('waiting handyman pedData…');
        }
        if (handymanBot.timerId != null) {
            try { clearInterval(handymanBot.timerId); } catch (e) {}
        }
        handymanBot.timerId = setInterval(handymanTick, 250);
        return 'ok:handyman_bot_start';
    }

    function handymanBotStop() {
        handymanBot.on = false;
        handymanBot.phase = 'idle';
        handymanBot.waitProcess = false;
        handymanHoldClear();
        if (handymanBot.timerId != null) {
            try { clearInterval(handymanBot.timerId); } catch (e) {}
            handymanBot.timerId = null;
        }
        handymanStatus('stopped');
        return 'ok:handyman_bot_stop';
    }
)UAJS"
        R"UAJS(
    // ---------------- Carrier (дальнобой) bot ----------------
    var carrierBot = {
        on: false,
        autoMode: false,
        phase: 'idle',
        prerequisite: 'off',
        lastMsg: '',
        lastServerAnswer: '-',
        deliveryAck: 'idle',
        timerId: null,
        spamId: null,
        spamI: 0,
        spamMs: 100,
        hooksReady: false,
        busyUntil: 0,
        phaseEnteredAt: 0,
        lastProgressAt: 0,
        watchdogAt: 0,
        phaseRetries: 0,
        retryCount: 0,
        suspended: false,
        vehRemoteId: -1,
        boundVehModel: 0,
        badVehRemoteId: -1,
        vehicleMissingAt: 0,
        knownVehWeight: -1,
        learnedVehWeight: -1,
        maxWeight: -1,
        capacitySource: 'unknown',
        capacityCache: {},
        capacityProbe: true,
        myLvl: -1,
        orders: [],
        ordersSeen: 0,
        trailerOrdersSkipped: 0,
        blacklist: {},
        orderId: 0,
        orderWeight: 0,
        orderPrice: 0,
        orderAccepted: false,
        parkSlots: [],
        triedSlots: {},
        parkPosIndex: -1,
        parkAt: 0,
        pedPos: null,
        loadYardPos: null,
        route: [],
        routeIndex: 0,
        routeTotal: 0,
        deliveredCount: 0,
        completionConfirmed: false,
        routeVehId: -1,
        ignoredRouteVehId: -1,
        deliverAt: 0,
        deliveryIndex: -1,
        deliveryExpectedIndex: -1,
        deliveryRetries: 0,
        deliveryResyncs: 0,
        deliveryLastSendAt: 0,
        deliveryLastAck: -1,
        deliveryLastAckAt: 0,
        hasTrailer: false,
        cancelOfferSeen: false,
        skipIllegal: true,
        takeAt: 0,
        fetchAt: 0,
        seatIssue: '',
        seatIssueAt: 0,
        resumePhase: 'fetch_orders',
        protectId: null,
        dmgBlockReady: false,
        underLoad: false,
        underX: 0,
        underY: 0,
        surfaceX: 0,
        surfaceY: 0,
        surfaceZ: 0,
        surfaceIsRoute: false,
        underZ: 0,
        underHoldId: null,
        truckParkPos: null,
        truckUnderX: 0,
        truckUnderY: 0,
        truckUnderZ: 0,
        underStarted: false,
        underWaitAt: 0,
        underOffset: 0,
        truckUnderOffset: 0,
        avoidPlayers: true,
        clearDistance: 35,
        pauseDistance: 20,
        maxUnderOffset: 25,
        nearestPlayerId: -1,
        nearestPlayerDist: -1,
        nearestPlayerName: '',
        waitingClear: false,
        waitClearAt: 0,
        waitClearPhase: '',
        waitSlotIndex: -1,
        waitSlotRotations: 0,
        playerBypassOnce: false,
        playerBypassUsed: false,
        parkEndDueAt: 0,
        parkEndSent: false,
        parkingRetries: 0,
        parkingRounds: 0,
        noSlotAt: 0,
        employmentKnown: false,
        isEmployed: false,
        employShapeInside: false,
        employPosKnown: false,
        employX: 0,
        employY: 0,
        employZ: 0,
        employRemote: '',
        employRemoteArgc: 0,
        employRemoteArg0: null,
        employRemoteArg1: null,
        employRemoteArg2: null,
        companyKnown: false,
        inGarageKnown: false,
        inGarage: false,
        garagePosKnown: false,
        garageX: 0,
        garageY: 0,
        garageZ: 0,
        garageParkingKnown: false,
        garageParkingX: 0,
        garageParkingY: 0,
        garageParkingZ: 0,
        garageExitKnown: false,
        garageExitX: 0,
        garageExitY: 0,
        garageExitZ: 0,
        garageEnterInside: false,
        garageVehicleInside: false,
        garageAction: '',
        prereqActionAt: 0,
        prereqActionRetries: 0
    };
    global.__uaCarrier = carrierBot;
    var CARRIER_UNDER_DEPTH = 50;
    var CARRIER_PLAYER_SCAN_MS = 500;
    var CARRIER_PLAYER_SCAN_MAX = 256;
    var CARRIER_UNDER_COS = [
        1, 0.8660254, 0.5, 0, -0.5, -0.8660254,
        -1, -0.8660254, -0.5, 0, 0.5, 0.8660254
    ];
    var CARRIER_UNDER_SIN = [
        0, 0.5, 0.8660254, 1, 0.8660254, 0.5,
        0, -0.5, -0.8660254, -1, -0.8660254, -0.5
    ];
    var carrierPlayerX = [];
    var carrierPlayerY = [];
    var carrierPlayerId = [];
    var carrierPlayerName = [];
    var carrierPlayerCount = 0;
    var carrierScanMe = null;
    var carrierScanMeId = -1;
    var carrierScanDimension = null;
    var carrierNextPlayerScanAt = 0;
    var carrierPickUnderX = 0;
    var carrierPickUnderY = 0;
    var carrierPickUnderOffset = 0;
    var carrierUnderTarget = { x: 0, y: 0, z: 0 };
    var carrierRouteTarget = { x: 0, y: 0, z: 0, h: null };
    var carrierFallbackSlot = { posIndex: -1 };
    var carrierGarageCandidate = null;
    var carrierGarageCandidateD2 = 1e30;
    var carrierGaragePlayer = null;
    var carrierGarageDimension = null;
    var carrierSlotScanX = 0;
    var carrierSlotScanY = 0;
    var carrierSlotScanMine = -1;
    var carrierSlotScanDimension = null;
    var carrierSlotScanBusy = false;
    var carrierSlotNearestVehicleD2 = 1e30;
    var carrierVehicleBusyExceptions = ['vehicle'];
    var CARRIER_DELIVERY_RETRY_MAX = 24;
    var CARRIER_DELIVERY_RESYNC_MAX = 2;
    var CARRIER_PLAYER_SLOT_WAIT_MS = 60000;
    var CARRIER_FORKLIFT_HASH = 0;
    try { CARRIER_FORKLIFT_HASH = mp.game.joaat('forklift'); }
    catch (eForkHash) {}

    function carrierStatus(msg) {
        carrierBot.lastMsg = String(msg || '');
        log('carrier: ' + carrierBot.lastMsg);
    }

    function carrierSetPhase(phase, msg) {
        var now = Date.now();
        var next = String(phase || 'idle');
        var changed = carrierBot.phase !== next;
        if (changed) {
            carrierBot.phase = next;
            carrierBot.phaseEnteredAt = now;
            carrierBot.phaseRetries = 0;
            carrierBot.lastProgressAt = now;
            carrierBot.watchdogAt = 0;
        }
        if (msg) {
            carrierBot.lastProgressAt = now;
            carrierStatus(msg);
        }
    }

    function carrierProgress(msg) {
        carrierBot.lastProgressAt = Date.now();
        if (msg) carrierStatus(msg);
    }

    function carrierRetry(msg) {
        carrierBot.phaseRetries++;
        carrierBot.retryCount++;
        if (msg) carrierStatus(msg);
    }

    function carrierSetPrerequisite(value, msg) {
        var next = String(value || 'checking');
        if (carrierBot.prerequisite === next) return;
        carrierBot.prerequisite = next;
        if (msg) carrierStatus(msg);
    }

    function carrierRememberAnswer(type) {
        carrierBot.lastServerAnswer =
            String(type == null ? '-' : type).replace(/[\t\r\n]/g, ' ');
    }

    function carrierReadPosInto(value, out) {
        if (!value || !out) return false;
        var p = value.position || value;
        if (Array.isArray(p)) {
            if (p.length < 2) return false;
            out.x = Number(p[0]);
            out.y = Number(p[1]);
            out.z = Number(p[2] != null ? p[2] : 0);
            out.h = value.heading != null ? Number(value.heading) : null;
        } else {
            if (p.x == null || p.y == null) return false;
            out.x = Number(p.x);
            out.y = Number(p.y);
            out.z = Number(p.z != null ? p.z : 0);
            out.h = value.heading != null ? Number(value.heading)
                : (value.h != null ? Number(value.h)
                    : (p.heading != null ? Number(p.heading)
                        : (p.h != null ? Number(p.h) : null)));
        }
        return isFinite(out.x) && isFinite(out.y) && isFinite(out.z);
    }
)UAJS"
        R"UAJS(

    function carrierCollectPlayer(other) {
        if (!other || other === carrierScanMe ||
            carrierPlayerCount >= CARRIER_PLAYER_SCAN_MAX)
            return;
        try {
            if (mp.players.exists && !mp.players.exists(other)) return;
            if (carrierScanMeId >= 0 &&
                Number(other.remoteId) === carrierScanMeId)
                return;
            if (carrierScanDimension != null &&
                other.dimension != null &&
                Number(other.dimension) !== carrierScanDimension)
                return;
            var pos = other.position;
            if (!pos) return;
            var x = Number(pos.x), y = Number(pos.y);
            if (!isFinite(x) || !isFinite(y)) return;
            var i = carrierPlayerCount++;
            carrierPlayerX[i] = x;
            carrierPlayerY[i] = y;
            carrierPlayerId[i] = Number(other.remoteId);
            carrierPlayerName[i] = String(other.name || '');
        } catch (e) {}
    }

    function carrierRefreshPlayers(now) {
        now = Number(now) || Date.now();
        if (now < carrierNextPlayerScanAt) return;
        carrierNextPlayerScanAt = now + CARRIER_PLAYER_SCAN_MS;
        carrierPlayerCount = 0;
        try {
            carrierScanMe = mp.players.local;
            carrierScanMeId =
                carrierScanMe && carrierScanMe.remoteId != null
                    ? Number(carrierScanMe.remoteId) : -1;
            carrierScanDimension =
                carrierScanMe && carrierScanMe.dimension != null
                    ? Number(carrierScanMe.dimension) : null;
            mp.players.forEach(carrierCollectPlayer);
        } catch (e) {}
        carrierScanMe = null;
        carrierScanMeId = -1;
        carrierScanDimension = null;
    }

    function carrierNearestD2At(x, y) {
        var bestD2 = 1e30;
        for (var i = 0; i < carrierPlayerCount; i++) {
            var dx = carrierPlayerX[i] - x;
            var dy = carrierPlayerY[i] - y;
            var d2 = dx * dx + dy * dy;
            if (d2 < bestD2) bestD2 = d2;
        }
        return bestD2;
    }

    function carrierUpdateNearestAt(x, y) {
        var bestD2 = 1e30, best = -1;
        for (var i = 0; i < carrierPlayerCount; i++) {
            var dx = carrierPlayerX[i] - x;
            var dy = carrierPlayerY[i] - y;
            var d2 = dx * dx + dy * dy;
            if (d2 < bestD2) {
                bestD2 = d2;
                best = i;
            }
        }
        if (best < 0) {
            carrierBot.nearestPlayerId = -1;
            carrierBot.nearestPlayerDist = -1;
            carrierBot.nearestPlayerName = '';
            return -1;
        }
        carrierBot.nearestPlayerId = carrierPlayerId[best];
        carrierBot.nearestPlayerDist = Math.sqrt(bestD2);
        carrierBot.nearestPlayerName = carrierPlayerName[best] || '';
        return carrierBot.nearestPlayerDist;
    }

    function carrierUpdateNearestForCurrent(now) {
        carrierRefreshPlayers(now);
        try {
            if (carrierBot.underLoad &&
                isFinite(carrierBot.surfaceX) &&
                isFinite(carrierBot.surfaceY)) {
                carrierUpdateNearestAt(
                    carrierBot.surfaceX, carrierBot.surfaceY);
                return;
            }
            var me = mp.players.local;
            var pos = me && me.position;
            if (pos)
                carrierUpdateNearestAt(Number(pos.x), Number(pos.y));
            else
                carrierUpdateNearestAt(0, 0);
        } catch (e) {
            carrierBot.nearestPlayerId = -1;
            carrierBot.nearestPlayerDist = -1;
            carrierBot.nearestPlayerName = '';
        }
    }

    // Select one of 12 lateral directions without changing surface XY.
    function carrierPickSafeUnderXY(surfaceX, surfaceY) {
        carrierPickUnderX = Number(surfaceX);
        carrierPickUnderY = Number(surfaceY);
        carrierPickUnderOffset = 0;
        carrierUpdateNearestAt(carrierPickUnderX, carrierPickUnderY);
        if (!carrierBot.avoidPlayers || carrierPlayerCount <= 0)
            return;
        var clear = Math.max(
            0, Number(carrierBot.clearDistance) || 0);
        var maxOffset = Math.max(
            0, Math.min(25, Number(carrierBot.maxUnderOffset) || 0));
        var clearD2 = clear * clear;
        var bestD2 = carrierNearestD2At(
            carrierPickUnderX, carrierPickUnderY);
        if (bestD2 >= clearD2 || maxOffset <= 0) return;
        var bestX = carrierPickUnderX;
        var bestY = carrierPickUnderY;
        for (var i = 0; i < 12; i++) {
            var x = Number(surfaceX) +
                CARRIER_UNDER_COS[i] * maxOffset;
            var y = Number(surfaceY) +
                CARRIER_UNDER_SIN[i] * maxOffset;
            var d2 = carrierNearestD2At(x, y);
            if (d2 > bestD2) {
                bestD2 = d2;
                bestX = x;
                bestY = y;
            }
        }
        carrierPickUnderX = bestX;
        carrierPickUnderY = bestY;
        if (bestX !== Number(surfaceX) || bestY !== Number(surfaceY))
            carrierPickUnderOffset = maxOffset;
    }

    function carrierResetPlayerWait(now, announce) {
        if (!carrierBot.waitingClear) return;
        now = Number(now) || Date.now();
        var paused = Math.max(
            0, now - (carrierBot.waitClearAt || now));
        if (carrierBot.waitClearPhase === carrierBot.phase &&
            (carrierBot.phase === 'park_settle' ||
             carrierBot.phase === 'wait_park_ack')) {
            if (carrierBot.parkAt) carrierBot.parkAt += paused;
            if (carrierBot.parkEndDueAt)
                carrierBot.parkEndDueAt += paused;
        }
        carrierBot.waitingClear = false;
        carrierBot.waitClearAt = 0;
        carrierBot.waitClearPhase = '';
        if (announce)
            carrierStatus('clear → resume ' + carrierBot.phase);
    }

    function carrierSurfaceBlocked(x, y, now) {
        now = Number(now) || Date.now();
        if (!carrierBot.avoidPlayers) {
            if (carrierBot.waitingClear)
                carrierResetPlayerWait(now, false);
            if (String(carrierBot.lastMsg || '')
                    .indexOf('waiting clear:') === 0)
                carrierStatus(
                    'avoid players OFF · continue ' +
                    carrierBot.phase);
            return false;
        }
        carrierRefreshPlayers(now);
        var nearest = carrierUpdateNearestAt(Number(x), Number(y));
        var pause = Math.max(
            0, Number(carrierBot.pauseDistance) || 0);
        var blocked = carrierBot.avoidPlayers && pause > 0 &&
            nearest >= 0 && nearest <= pause;
        if (blocked) {
            if (!carrierBot.waitingClear ||
                carrierBot.waitClearPhase !== carrierBot.phase) {
                carrierBot.waitingClear = true;
                carrierBot.waitClearAt = now;
                carrierBot.waitClearPhase = carrierBot.phase;
                carrierStatus(
                    'waiting clear: ' +
                    (carrierBot.nearestPlayerName ||
                        ('ID ' + carrierBot.nearestPlayerId)) +
                    ' ' + nearest.toFixed(1) + 'm');
            }
            carrierBot.busyUntil = now + 250;
            return true;
        }
        if (carrierBot.waitingClear) {
            carrierResetPlayerWait(now, true);
            carrierBot.busyUntil = now + 250;
            return true;
        }
        return false;
    }

    function carrierHandleBlockedParkingSlot(slot, now) {
        if (!slot || !carrierBot.waitingClear) return true;
        var idx = Number(slot.posIndex);
        if (carrierBot.waitSlotIndex !== idx) {
            carrierBot.waitSlotIndex = idx;
            carrierBot.waitClearAt = now;
        }
        if (now - (carrierBot.waitClearAt || now) <
            CARRIER_PLAYER_SLOT_WAIT_MS)
            return true;

        carrierBot.triedSlots[String(idx)] = true;
        carrierBot.waitSlotRotations++;
        carrierBot.parkingRetries++;
        carrierRetry('player wait 60s · rotate slot');
        carrierResetPlayerWait(now, false);
        carrierBot.waitSlotIndex = -1;
        carrierBot.parkEndDueAt = 0;
        carrierBot.parkEndSent = false;
        carrierBot.phase = 'park';
        carrierBot.phaseEnteredAt = now;
        carrierBot.lastProgressAt = now;
        carrierBot.busyUntil = now + 250;

        if (carrierBot.waitSlotRotations >=
            Math.max(1, carrierBot.parkSlots.length)) {
            carrierBot.triedSlots = {};
            carrierBot.waitSlotRotations = 0;
            carrierBot.playerBypassOnce = true;
            carrierBot.playerBypassUsed = false;
            carrierStatus('all slots blocked · one-shot player bypass');
        }
        return true;
    }

    function carrierCanBypassPlayerOnce() {
        if (!carrierBot.playerBypassOnce) return false;
        carrierBot.playerBypassOnce = false;
        carrierBot.playerBypassUsed = true;
        carrierResetPlayerWait(Date.now(), false);
        carrierStatus('one-shot player bypass at exact parking XY');
        return true;
    }

)UAJS"
        R"UAJS(
    // Cargo HP tracks bodyHealth diffs → vehicleDamaged remote
    function carrierHealTruck() {
        try {
            var id = carrierBot.routeVehId >= 0
                ? carrierBot.routeVehId : carrierBot.vehRemoteId;
            var veh = carrierGetVeh(id);
            if (!veh) return;
            try { veh.setInvincible(true); } catch (e0) {}
            try { veh.setBodyHealth(1000); } catch (e1) {}
            try { veh.setEngineHealth(1000); } catch (e2) {}
            try {
                if (typeof veh.setProofs === 'function')
                    veh.setProofs(
                        true, true, true, true, true, true, true, true);
            } catch (e3) {}
            try { veh.setCanBeVisiblyDamaged(false); } catch (e4) {}
            try { veh.setDisablePetrolTankDamage(true); } catch (e5) {}
        } catch (e) {}
    }

    function carrierStartProtect() {
        if (carrierBot.protectId != null) return;
        carrierHealTruck();
        carrierBot.protectId = setInterval(function () {
            if (!carrierBot.on) return;
            // Keep bodyHealth full so client cargo checker sees no diff
            carrierHealTruck();
        }, 100);
    }

    function carrierStopProtect() {
        if (carrierBot.protectId != null) {
            try { clearInterval(carrierBot.protectId); } catch (e) {}
            carrierBot.protectId = null;
        }
        try {
            var id = carrierBot.routeVehId >= 0
                ? carrierBot.routeVehId : carrierBot.vehRemoteId;
            var veh = carrierGetVeh(id);
            if (veh) {
                try { veh.setInvincible(false); } catch (e0) {}
                try {
                    if (typeof veh.setProofs === 'function')
                        veh.setProofs(
                            false, false, false, false,
                            false, false, false, false);
                } catch (e1) {}
                try { veh.setCanBeVisiblyDamaged(true); }
                catch (e1b) {}
                try { veh.setDisablePetrolTankDamage(false); }
                catch (e1c) {}
            }
            if (Number(id) !== Number(carrierBot.vehRemoteId)) {
                var bound = carrierGetVeh(carrierBot.vehRemoteId);
                if (bound) {
                    try { bound.setInvincible(false); } catch (e2) {}
                    try {
                        if (typeof bound.setProofs === 'function')
                            bound.setProofs(
                                false, false, false, false,
                                false, false, false, false);
                    } catch (e3) {}
                    try { bound.setCanBeVisiblyDamaged(true); }
                    catch (e4) {}
                    try { bound.setDisablePetrolTankDamage(false); }
                    catch (e5) {}
                }
            }
        } catch (e) {}
    }

    function carrierEnsureDmgBlock() {
        if (carrierBot.dmgBlockReady) return;
        carrierBot.dmgBlockReady = true;
        try {
            if (!mp.events.__uaCarrierRem) {
                mp.events.__uaCarrierRem = mp.events.callRemote;
                mp.events.callRemote = function () {
                    var name = arguments[0];
                    if (typeof name === 'string' &&
                        name.indexOf('carrier.') === 0 &&
                        (name.indexOf('employ') >= 0 ||
                         name.indexOf('employment') >= 0) &&
                        arguments.length <= 4) {
                        carrierBot.employRemote = name;
                        carrierBot.employRemoteArgc =
                            Math.max(0, arguments.length - 1);
                        carrierBot.employRemoteArg0 =
                            arguments.length > 1 ? arguments[1] : null;
                        carrierBot.employRemoteArg1 =
                            arguments.length > 2 ? arguments[2] : null;
                        carrierBot.employRemoteArg2 =
                            arguments.length > 3 ? arguments[3] : null;
                    }
                    if (carrierBot.on &&
                        name === 'carrier.order.route.vehicleDamaged')
                        return;
                    return mp.events.__uaCarrierRem.apply(
                        mp.events, arguments);
                };
            }
        } catch (e) {}
    }

    function carrierNormPos(p) {
        if (!p) return null;
        if (typeof p.x === 'number')
            return {
                x: Number(p.x), y: Number(p.y),
                z: Number(p.z != null ? p.z : 0),
                h: p.heading != null ? Number(p.heading)
                    : (p.h != null ? Number(p.h) : null)
            };
        if (Array.isArray(p) && p.length >= 2)
            return {
                x: Number(p[0]), y: Number(p[1]),
                z: Number(p[2] != null ? p[2] : 0), h: null
            };
        if (p.position) {
            var n = carrierNormPos(p.position);
            if (n && p.heading != null) n.h = Number(p.heading);
            return n;
        }
        return null;
    }

    function carrierGetVeh(id) {
        try {
            if (id != null && id >= 0)
                return mp.vehicles.atRemoteId(id) || null;
        } catch (e0) {}
        if (id != null && id >= 0) return null;
        try {
            var p = mp.players.local;
            if (p && p.vehicle) return p.vehicle;
        } catch (e1) {}
        return null;
    }

    function carrierCapacity() {
        var k = Number(carrierBot.knownVehWeight);
        var l = Number(carrierBot.learnedVehWeight);
        var m = Number(carrierBot.maxWeight);
        if (m > 0) {
            carrierBot.capacitySource = 'override';
            return k > 0 ? Math.min(k, m) : m;
        }
        if (k > 0) {
            if (carrierBot.capacitySource !== 'server' &&
                carrierBot.capacitySource !== 'model-cache')
                carrierBot.capacitySource = 'server';
            return k;
        }
        if (l > 0) {
            if (carrierBot.capacitySource !== 'probing')
                carrierBot.capacitySource = 'learned';
            return l;
        }
        if (carrierBot.capacitySource === 'probing' &&
            carrierBot.orderId)
            return -1;
        carrierBot.capacitySource = 'unknown';
        return -1;
    }

    function carrierVehicleLooksTrailer(vehicle) {
        if (!vehicle) return true;
        try {
            if (CARRIER_FORKLIFT_HASH &&
                Number(vehicle.model) ===
                    Number(CARRIER_FORKLIFT_HASH))
                return true;
        } catch (eFork) {}
        try {
            if (mp.game.vehicle &&
                typeof mp.game.vehicle.isThisModelATrailer === 'function' &&
                mp.game.vehicle.isThisModelATrailer(vehicle.model))
                return true;
        } catch (e0) {}
        try {
            if (vehicle.truckId || vehicle.variable_trailer ||
                vehicle.getVariable('trailer'))
                return true;
        } catch (e1) {}
        return false;
    }

    function carrierVehicleDriverIsLocal(vehicle) {
        if (!vehicle) return false;
        try {
            var player = mp.players.local;
            if (!player || player.vehicle !== vehicle) return false;
            if (typeof vehicle.getPedInSeat !== 'function') return true;
            return Number(vehicle.getPedInSeat(-1)) ===
                Number(player.handle);
        } catch (e) {
            return false;
        }
    }

    function carrierBindVehicle(vehicle, source) {
        if (!vehicle || carrierVehicleLooksTrailer(vehicle)) return false;
        var id = Number(vehicle.remoteId);
        if (!isFinite(id) || id < 0) return false;
        var model = Number(vehicle.model) || 0;
        var changed =
            id !== Number(carrierBot.vehRemoteId) ||
            model !== Number(carrierBot.boundVehModel);
        carrierBot.vehRemoteId = id;
        carrierBot.boundVehModel = model;
        carrierBot.badVehRemoteId = -1;
        carrierBot.vehicleMissingAt = 0;
        if (changed) {
            carrierBot.knownVehWeight = -1;
            carrierBot.learnedVehWeight = -1;
            carrierBot.capacitySource = 'unknown';
            carrierBot.capacityProbe = true;
            var cached = Number(
                carrierBot.capacityCache[String(model)] || -1);
            if (cached > 0) {
                carrierBot.knownVehWeight = cached;
                carrierBot.capacitySource = 'model-cache';
                carrierBot.capacityProbe = false;
            }
        }
        if (source)
            carrierProgress('truck #' + id + ' · ' + source);
        return true;
    }

    function carrierLearnAcceptedCapacity() {
        var weight = Number(carrierBot.orderWeight);
        if (!(weight > 0)) return;
        if (weight > Number(carrierBot.learnedVehWeight || -1))
            carrierBot.learnedVehWeight = weight;
        carrierBot.capacityProbe =
            !(Number(carrierBot.knownVehWeight) > 0);
        if (!(Number(carrierBot.knownVehWeight) > 0) &&
            !(Number(carrierBot.maxWeight) > 0))
            carrierBot.capacitySource = 'learned';
        carrierCapacity();
    }

    function carrierLearnServerCapacity(value) {
        var cap = Number(value);
        if (!(cap > 0)) return;
        carrierBot.knownVehWeight = cap;
        carrierBot.learnedVehWeight = Math.max(
            Number(carrierBot.learnedVehWeight) || -1, cap);
        carrierBot.capacitySource = 'server';
        carrierBot.capacityProbe = false;
        var model = Number(carrierBot.boundVehModel) || 0;
        if (model)
            carrierBot.capacityCache[String(model)] = cap;
    }

    function carrierHasEmployApi() {
        if (carrierBot.employRemote) return true;
        try {
            var api = mp.carrier;
            return !!(api &&
                (typeof api.employ === 'function' ||
                 typeof api.requestEmployment === 'function'));
        } catch (e) {
            return false;
        }
    }

    function carrierReplayEmployRemote() {
        if (!carrierBot.employShapeInside)
            return false;
        try {
            var api = mp.carrier;
            if (api && typeof api.employ === 'function') {
                api.employ();
                return true;
            }
            if (api &&
                typeof api.requestEmployment === 'function') {
                api.requestEmployment();
                return true;
            }
            if (!carrierBot.employRemote) return false;
            var name = carrierBot.employRemote;
            var argc = carrierBot.employRemoteArgc | 0;
            if (argc <= 0) mp.events.callRemote(name);
            else if (argc === 1)
                mp.events.callRemote(name, carrierBot.employRemoteArg0);
            else if (argc === 2)
                mp.events.callRemote(
                    name,
                    carrierBot.employRemoteArg0,
                    carrierBot.employRemoteArg1);
            else
                mp.events.callRemote(
                    name,
                    carrierBot.employRemoteArg0,
                    carrierBot.employRemoteArg1,
                    carrierBot.employRemoteArg2);
            return true;
        } catch (e) {
            return false;
        }
    }
)UAJS"
        R"UAJS(

    function carrierRememberGaragePosition(data) {
        if (!data) return;
        if (carrierReadPosInto(data.garagePosition, carrierRouteTarget)) {
            carrierBot.garageX = carrierRouteTarget.x;
            carrierBot.garageY = carrierRouteTarget.y;
            carrierBot.garageZ = carrierRouteTarget.z;
            carrierBot.garagePosKnown = true;
        }
        if (carrierReadPosInto(
                data.garageParkingPosition, carrierRouteTarget)) {
            carrierBot.garageParkingX = carrierRouteTarget.x;
            carrierBot.garageParkingY = carrierRouteTarget.y;
            carrierBot.garageParkingZ = carrierRouteTarget.z;
            carrierBot.garageParkingKnown = true;
        }
    }

    function carrierProbeModuleEvidence() {
        try {
            var carrier = mp.carrier;
            var carrierState = carrier && carrier.state;
            if (carrierState) {
                var employed = carrierState.isEmployed;
                var employPed = carrierState.employNpcPed;
                if (employed === true || employPed) {
                    carrierBot.employmentKnown = true;
                    carrierBot.isEmployed = !!employed;
                }
                if (employPed &&
                    carrierReadPosInto(employPed, carrierRouteTarget)) {
                    carrierBot.employX = carrierRouteTarget.x;
                    carrierBot.employY = carrierRouteTarget.y;
                    carrierBot.employZ = carrierRouteTarget.z;
                    carrierBot.employPosKnown = true;
                }
            }
        } catch (e0) {}
        try {
            var companies = mp.carrierCompanies;
            var companyState = companies && companies.state;
            if (companyState) {
                if (companyState.enterBlip &&
                    carrierReadPosInto(
                        companyState.enterBlip, carrierRouteTarget)) {
                    carrierBot.garageX = carrierRouteTarget.x;
                    carrierBot.garageY = carrierRouteTarget.y;
                    carrierBot.garageZ = carrierRouteTarget.z;
                    carrierBot.garagePosKnown = true;
                    carrierBot.companyKnown = true;
                }
                if (companyState.vehEnterBlip &&
                    carrierReadPosInto(
                        companyState.vehEnterBlip, carrierRouteTarget)) {
                    carrierBot.garageParkingX = carrierRouteTarget.x;
                    carrierBot.garageParkingY = carrierRouteTarget.y;
                    carrierBot.garageParkingZ = carrierRouteTarget.z;
                    carrierBot.garageParkingKnown = true;
                    carrierBot.companyKnown = true;
                }
                if (companyState.inGarage === true ||
                    carrierBot.companyKnown) {
                    carrierBot.inGarageKnown = true;
                    carrierBot.inGarage = !!companyState.inGarage;
                    if (carrierBot.inGarage)
                        carrierBot.companyKnown = true;
                }
            }
        } catch (e1) {}
        try {
            var orderState =
                mp.carrierOrder && mp.carrierOrder.state;
            var recoveredRoute =
                orderState && orderState.route;
            var recoveredVehRaw =
                orderState ? orderState.routeVehId : null;
            var recoveredVehId =
                recoveredVehRaw == null
                    ? -1 : Number(recoveredVehRaw);
            if (!isFinite(recoveredVehId) ||
                recoveredVehId < 0)
                recoveredVehId = -1;
            if (carrierBot.on &&
                !carrierBot.route.length &&
                Array.isArray(recoveredRoute) &&
                recoveredRoute.length &&
                recoveredVehId >= 0 &&
                recoveredVehId !==
                    Number(carrierBot.ignoredRouteVehId)) {
                var loadingState =
                    mp.carrierCarLoading &&
                    mp.carrierCarLoading.state;
                if (loadingState &&
                    loadingState.truckId != null) {
                    carrierBot.routeVehId = recoveredVehId;
                    carrierLateTrailer('recovered route');
                    return;
                }
                carrierBot.route = recoveredRoute;
                var recoveredIndex =
                    Number(orderState.routePosIndex);
                if (!isFinite(recoveredIndex) ||
                    recoveredIndex < 0)
                    recoveredIndex = 0;
                recoveredIndex = Math.floor(recoveredIndex);
                carrierBot.routeIndex =
                    Math.min(
                        recoveredIndex,
                        recoveredRoute.length);
                carrierBot.routeVehId = recoveredVehId;
                carrierBot.orderAccepted = true;
                carrierResetDeliveryState();
                carrierBot.routeTotal = recoveredRoute.length;
                carrierBot.deliveredCount =
                    carrierBot.routeIndex;
                carrierBot.deliveryAck = 'recovered';
                carrierStartProtect();
                carrierHealTruck();
                carrierSetPrerequisite('active_order');
                carrierSetPhase(
                    'enter_truck',
                    'recovered active carrier route');
            }
        } catch (e2) {}
    }

    function carrierTpPlayerXYZ(x, y, z) {
        try {
            var player = mp.players.local;
            if (!player) return false;
            try { mp.events.call('ac:refreshtp'); } catch (e0) {}
            try { player.freezePosition(false); } catch (e1) {}
            player.setCoordsNoOffset(
                Number(x), Number(y), Number(z),
                false, false, false);
            return true;
        } catch (e) {
            return false;
        }
    }

    function carrierCollectGarageVehicle(vehicle) {
        if (!vehicle || !carrierGaragePlayer) return;
        try {
            if (mp.vehicles.exists && !mp.vehicles.exists(vehicle)) return;
            if (carrierGarageDimension != null &&
                vehicle.dimension != null &&
                Number(vehicle.dimension) !== carrierGarageDimension)
                return;
            if (Number(vehicle.remoteId) ===
                Number(carrierBot.badVehRemoteId))
                return;
            if (carrierVehicleLooksTrailer(vehicle)) return;
            if (typeof vehicle.getPedInSeat === 'function') {
                var driver = Number(vehicle.getPedInSeat(-1));
                if (driver && driver !== Number(carrierGaragePlayer.handle))
                    return;
            }
            var vp = vehicle.position;
            var pp = carrierGaragePlayer.position;
            if (!vp || !pp) return;
            var dx = Number(vp.x) - Number(pp.x);
            var dy = Number(vp.y) - Number(pp.y);
            var dz = Number(vp.z) - Number(pp.z);
            var d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < carrierGarageCandidateD2) {
                carrierGarageCandidateD2 = d2;
                carrierGarageCandidate = vehicle;
            }
        } catch (e) {}
    }

    function carrierFindGarageVehicle() {
        carrierGarageCandidate = null;
        carrierGarageCandidateD2 = 1e30;
        carrierGaragePlayer = null;
        carrierGarageDimension = null;
        try {
            carrierGaragePlayer = mp.players.local;
            if (!carrierGaragePlayer) return null;
            carrierGarageDimension =
                carrierGaragePlayer.dimension != null
                    ? Number(carrierGaragePlayer.dimension) : null;
            mp.vehicles.forEach(carrierCollectGarageVehicle);
        } catch (e) {}
        carrierGaragePlayer = null;
        carrierGarageDimension = null;
        return carrierGarageCandidate;
    }

    function carrierTryGarageAction(kind, now) {
        if (carrierBot.garageAction !== kind) return false;
        if (now - (carrierBot.prereqActionAt || 0) < 2500)
            return false;
        if (carrierBot.prereqActionRetries >= 3)
            return false;
        try {
            if (typeof mp.playerHasAnyBusy === 'function') {
                if (kind === 'take') {
                    if (mp.playerHasAnyBusy(
                            carrierVehicleBusyExceptions))
                        return false;
                } else if (mp.playerHasAnyBusy()) {
                    return false;
                }
            }
            var companies = mp.carrierCompanies;
            var state = companies && companies.state;
            if (!state || typeof state.binedFunction !== 'function')
                return false;
            state.binedFunction();
            carrierBot.prereqActionAt = now;
            carrierBot.prereqActionRetries++;
            carrierRetry('garage ' + kind + ' requested');
            return true;
        } catch (e) {
            return false;
        }
    }

)UAJS"
        R"UAJS(
    function carrierTryPrerequisites(now) {
        carrierProbeModuleEvidence();
        if (carrierBot.suspended) {
            carrierSetPrerequisite('reconnect');
            if (carrierBot.phase !== 'preflight')
                carrierSetPhase('preflight', 'waiting reconnect…');
            return false;
        }
        if (carrierBot.orderAccepted &&
            carrierBot.route.length) {
            carrierSetPrerequisite('active_order');
            return false;
        }
        if (!carrierBot.employmentKnown) {
            carrierSetPrerequisite('employment_unknown');
            if (carrierBot.phase !== 'need_employment')
                carrierSetPhase(
                    'need_employment',
                    'waiting carrier employment evidence…');
            return false;
        }
        if (!carrierBot.isEmployed) {
            carrierSetPrerequisite('need_employment');
            if (carrierBot.phase !== 'need_employment')
                carrierSetPhase(
                    'need_employment',
                    'employment required · Auto stays enabled');
            try {
                var employPlayer = mp.players.local;
                if (employPlayer && employPlayer.vehicle) {
                    if (now - (carrierBot.prereqActionAt || 0) >= 1000) {
                        try {
                            employPlayer.vehicle.freezePosition(false);
                        } catch (eEmpFreeze) {}
                        if (typeof employPlayer.taskLeaveAnyVehicle ===
                            'function')
                            employPlayer.taskLeaveAnyVehicle(0, 16);
                        carrierBot.prereqActionAt = now;
                    }
                    return false;
                }
            } catch (eEmpLeave) {}
            if (carrierBot.employPosKnown &&
                !carrierBot.employShapeInside &&
                now - (carrierBot.prereqActionAt || 0) >= 4000) {
                if (!carrierSurfaceBlocked(
                        carrierBot.employX,
                        carrierBot.employY,
                        now)) {
                    carrierTpPlayerXYZ(
                        carrierBot.employX,
                        carrierBot.employY,
                        carrierBot.employZ);
                    carrierBot.prereqActionAt = now;
                }
            }
            if (carrierBot.employShapeInside &&
                carrierHasEmployApi() &&
                now - (carrierBot.prereqActionAt || 0) >= 3000 &&
                carrierBot.prereqActionRetries < 3) {
                if (carrierReplayEmployRemote()) {
                    carrierBot.prereqActionAt = now;
                    carrierBot.prereqActionRetries++;
                    carrierRetry('employment API requested');
                }
            }
            return false;
        }

        var player = null;
        var current = null;
        try {
            player = mp.players.local;
            current = player && player.vehicle;
        } catch (eP) {}
        if (!current &&
            Number(carrierBot.vehRemoteId) >= 0 &&
            Number(carrierBot.vehRemoteId) !==
                Number(carrierBot.badVehRemoteId) &&
            carrierGetVeh(carrierBot.vehRemoteId)) {
            if (now - (carrierBot.prereqActionAt || 0) >= 750) {
                carrierTryReseat();
                carrierBot.prereqActionAt = now;
            }
            carrierSetPrerequisite('need_driver_seat');
            if (carrierBot.phase !== 'need_truck')
                carrierSetPhase(
                    'need_truck',
                    'reseating known truck');
            return false;
        }
        if (current &&
            Number(current.remoteId) !==
                Number(carrierBot.badVehRemoteId) &&
            !carrierVehicleLooksTrailer(current)) {
            carrierBindVehicle(current, '');
            if (carrierBot.inGarage) {
                carrierSetPrerequisite('need_garage_take');
                if (!carrierVehicleDriverIsLocal(current)) {
                    try {
                        if (player &&
                            typeof player.setIntoVehicle === 'function')
                            player.setIntoVehicle(current.handle, -1);
                    } catch (eGarageSeat) {}
                    return false;
                }
                carrierBot.garageAction = 'take';
                carrierTryGarageAction('take', now);
                return false;
            }
            if (!carrierVehicleDriverIsLocal(current)) {
                try {
                    if (player &&
                        typeof player.setIntoVehicle === 'function')
                        player.setIntoVehicle(current.handle, -1);
                } catch (eSeat) {}
                carrierSetPrerequisite('need_driver_seat');
                if (carrierBot.phase !== 'need_truck')
                    carrierSetPhase(
                        'need_truck',
                        'reseating in truck driver seat');
                return false;
            }
            carrierSetPrerequisite('ready');
            return true;
        }

        carrierSetPrerequisite('need_truck');
        if (carrierBot.phase !== 'need_truck')
            carrierSetPhase(
                'need_truck',
                'truck required · waiting safe garage evidence');
        if (current &&
            Number(current.remoteId) ===
                Number(carrierBot.badVehRemoteId)) {
            if (now - (carrierBot.prereqActionAt || 0) >= 1000) {
                try {
                    try { current.freezePosition(false); }
                    catch (eBadFreeze) {}
                    if (player &&
                        typeof player.taskLeaveAnyVehicle === 'function')
                        player.taskLeaveAnyVehicle(0, 16);
                } catch (eBadLeave) {}
                carrierBot.prereqActionAt = now;
            }
            return false;
        }

        if (carrierBot.inGarage) {
            if (current &&
                Number(current.remoteId) !==
                    Number(carrierBot.badVehRemoteId)) {
                carrierBindVehicle(current, 'garage');
                carrierBot.garageAction = 'take';
                carrierTryGarageAction('take', now);
                return false;
            }
            if (now - (carrierBot.prereqActionAt || 0) < 1000)
                return false;
            var candidate = carrierFindGarageVehicle();
            if (candidate && player) {
                try {
                    if (typeof player.setIntoVehicle === 'function')
                        player.setIntoVehicle(candidate.handle, -1);
                    else if (typeof player.taskEnterVehicle === 'function')
                        player.taskEnterVehicle(
                            candidate.handle, 10, -1, 2, 1, 0);
                    carrierBot.prereqActionAt = now;
                    carrierProgress(
                        'garage truck #' + candidate.remoteId +
                        ' · seating');
                } catch (eS) {}
            }
            return false;
        }

        if (carrierBot.companyKnown &&
            carrierBot.garagePosKnown) {
            carrierSetPrerequisite('need_garage');
            if (!carrierBot.garageEnterInside &&
                now - (carrierBot.prereqActionAt || 0) >= 3000) {
                if (!carrierSurfaceBlocked(
                        carrierBot.garageX,
                        carrierBot.garageY,
                        now)) {
                    carrierTpPlayerXYZ(
                        carrierBot.garageX,
                        carrierBot.garageY,
                        carrierBot.garageZ);
                    carrierBot.prereqActionAt = now;
                    carrierProgress('to company garage');
                }
            }
            if (carrierBot.garageEnterInside)
                carrierTryGarageAction('enter', now);
            return false;
        }

        if (carrierBot.lastMsg.indexOf('truck required') < 0)
            carrierStatus(
                'truck required · no proven garage API; Auto will resume');
        return false;
    }

)UAJS"
        R"UAJS(
    function carrierStopSpam() {
        if (carrierBot.spamId != null) {
            try { clearInterval(carrierBot.spamId); } catch (e) {}
            carrierBot.spamId = null;
        }
        carrierBot.spamI = 0;
    }

    function carrierStartSpam() {
        carrierStopSpam();
        carrierBot.spamI = 0;
        var ms = Math.max(50, Number(carrierBot.spamMs) || 100);
        carrierBot.spamId = setInterval(function () {
            if (!carrierBot.on) return;
            try {
                if ((carrierBot.spamI % 2) === 0)
                    mp.events.callRemote('carrier.loading.attach');
                else
                    mp.events.callRemote('carrier.loading.detach');
                carrierBot.spamI++;
            } catch (eS) {}
        }, ms);
        carrierStatus('forklift spam ON');
    }

    function carrierHoldUnderLoad() {
        if (!carrierBot.underLoad) return;
        try {
            var p = mp.players.local;
            var vehId = carrierBot.routeVehId >= 0
                ? carrierBot.routeVehId : carrierBot.vehRemoteId;
            var veh = p && p.vehicle
                ? p.vehicle : carrierGetVeh(vehId);
            try { mp.events.call('ac:refreshtp'); } catch (e0) {}
            // Force current veh (forklift OR truck) under — like bus
            if (veh) {
                veh.setCoordsNoOffset(
                    carrierBot.underX, carrierBot.underY,
                    carrierBot.underZ, false, false, false);
                try { veh.setVelocity(0, 0, 0); } catch (e4) {}
            } else if (p) {
                try { p.setCollision(false, false); } catch (e5) {}
                try { p.freezePosition(true); } catch (e6) {}
                p.setCoordsNoOffset(
                    carrierBot.underX, carrierBot.underY,
                    carrierBot.underZ, false, false, false);
                try { p.setVelocity(0, 0, 0); } catch (e7) {}
            }
            // Parked truck under bay only while NOT driving it
            var truck = carrierGetVeh(carrierBot.vehRemoteId);
            var tp = carrierBot.truckParkPos;
            var inTruck = !!(veh && truck &&
                Number(veh.remoteId) === Number(truck.remoteId));
            if (truck && tp && !inTruck) {
                truck.setCoordsNoOffset(
                    carrierBot.truckUnderX,
                    carrierBot.truckUnderY,
                    carrierBot.truckUnderZ,
                    false, false, false);
                try { truck.setVelocity(0, 0, 0); } catch (e11) {}
            }
            if (p) {
                try { p.setCollision(false, false); } catch (e12) {}
                try { p.freezePosition(true); } catch (e13) {}
                try { p.setInvincible(true); } catch (e14) {}
                try { p.setVelocity(0, 0, 0); } catch (e15) {}
            }
            try {
                mp.game.cam.setGameplayCamRelativeHeading(0.5);
            } catch (eCam) {}
        } catch (e) {}
    }

    function carrierGoUnderLoad(anchor) {
        try {
            var p = mp.players.local;
            var fork = p && p.vehicle;
            var anchorPos = carrierNormPos(anchor);
            var pos = anchorPos ||
                (fork ? fork.position : (p ? p.position : null));
            if (!pos) return false;
            carrierBot.surfaceX = Number(pos.x);
            carrierBot.surfaceY = Number(pos.y);
            carrierBot.surfaceZ = Number(pos.z);
            carrierBot.surfaceIsRoute = false;
            carrierRefreshPlayers(Date.now());
            carrierPickSafeUnderXY(
                carrierBot.surfaceX, carrierBot.surfaceY);
            carrierBot.underX = carrierPickUnderX;
            carrierBot.underY = carrierPickUnderY;
            carrierBot.underOffset = carrierPickUnderOffset;
            carrierBot.underZ = carrierBot.surfaceZ - CARRIER_UNDER_DEPTH;
            var truck = carrierGetVeh(carrierBot.vehRemoteId);
            if (truck &&
                Number(truck.remoteId) !==
                    Number(carrierBot.vehRemoteId))
                truck = null;
            if (truck && !carrierBot.truckParkPos) {
                var tpos = truck.position;
                var th = 0;
                try { th = truck.getHeading(); } catch (eH) {}
                carrierBot.truckParkPos = {
                    x: Number(tpos.x), y: Number(tpos.y),
                    z: Number(tpos.z), h: th
                };
            }
            if (carrierBot.truckParkPos) {
                carrierPickSafeUnderXY(
                    carrierBot.truckParkPos.x,
                    carrierBot.truckParkPos.y);
                carrierBot.truckUnderX = carrierPickUnderX;
                carrierBot.truckUnderY = carrierPickUnderY;
                carrierBot.truckUnderOffset = carrierPickUnderOffset;
                carrierBot.truckUnderZ =
                    Number(carrierBot.truckParkPos.z) - CARRIER_UNDER_DEPTH;
            }
            carrierBot.underLoad = true;
            carrierBot.underStarted = true;
            carrierHoldUnderLoad();
            if (carrierBot.underHoldId != null) {
                try { clearInterval(carrierBot.underHoldId); } catch (eC) {}
            }
            carrierBot.underHoldId =
                setInterval(carrierHoldUnderLoad, 150);
            carrierStatus(
                'UNDER load offset=' +
                carrierBot.underOffset.toFixed(1) +
                'm z=' + carrierBot.underZ.toFixed(1));
            return true;
        } catch (e) {
            carrierStatus('under fail: ' + e);
            return false;
        }
    }

    function carrierLeaveUnderLoad() {
        var wasUnder = !!carrierBot.underLoad;
        var routeReturn = !!carrierBot.surfaceIsRoute;
        carrierBot.underLoad = false;
        carrierBot.surfaceIsRoute = false;
        if (carrierBot.underHoldId != null) {
            try { clearInterval(carrierBot.underHoldId); } catch (e) {}
            carrierBot.underHoldId = null;
        }
        if (!wasUnder) return;
        try {
            var p = mp.players.local;
            var fork = p && p.vehicle;
            var tp = carrierBot.truckParkPos;
            try { mp.events.call('ac:refreshtp'); } catch (e0) {}
            var truck = carrierGetVeh(carrierBot.vehRemoteId);
            var inTruck = !!(fork && truck &&
                Number(fork.remoteId) === Number(truck.remoteId));
            // Delivery exit: surface truck in place, stay seated
            if (inTruck) {
                var returnX = carrierBot.surfaceX;
                var returnY = carrierBot.surfaceY;
                var returnZ = carrierBot.surfaceZ;
                if (!routeReturn && tp) {
                    returnX = tp.x;
                    returnY = tp.y;
                    returnZ = tp.z;
                }
                try { truck.freezePosition(false); } catch (e1) {}
                try { truck.setCollision(true, true); } catch (e2) {}
                truck.setCoordsNoOffset(
                    returnX, returnY, returnZ,
                    false, false, false);
                if (!routeReturn && tp && tp.h != null) {
                    try { truck.setHeading(tp.h); } catch (eH) {}
                }
                try { if (p) p.setInvincible(false); } catch (e3) {}
                return;
            }
            // If the routed truck streamed out, still restore the local
            // player/current vehicle at the exact acknowledged route XY.
            if (routeReturn && p) {
                if (fork) {
                    try { fork.freezePosition(false); } catch (e3b) {}
                    try { fork.setCollision(true, true); } catch (e3c) {}
                    fork.setCoordsNoOffset(
                        carrierBot.surfaceX,
                        carrierBot.surfaceY,
                        carrierBot.surfaceZ,
                        false, false, false);
                } else {
                    try { p.freezePosition(false); } catch (e3d) {}
                    try { p.setCollision(true, true); } catch (e3e) {}
                    p.setCoordsNoOffset(
                        carrierBot.surfaceX,
                        carrierBot.surfaceY,
                        carrierBot.surfaceZ,
                        false, false, false);
                }
                try { p.setInvincible(false); } catch (e3f) {}
                return;
            }
            // Load exit: surface truck at bay, leave forklift
            if (truck && tp) {
                try { truck.freezePosition(false); } catch (e4) {}
                try { truck.setCollision(true, true); } catch (e5) {}
                truck.setCoordsNoOffset(
                    tp.x, tp.y, tp.z, false, false, false);
                if (tp.h != null) {
                    try { truck.setHeading(tp.h); } catch (e6) {}
                }
            }
            if (fork && p) {
                try {
                    if (typeof p.taskLeaveAnyVehicle === 'function')
                        p.taskLeaveAnyVehicle(0, 16);
                } catch (eL) {}
            }
            if (p && tp) {
                try { p.freezePosition(false); } catch (e7) {}
                try { p.setCollision(true, true); } catch (e8) {}
                p.setCoordsNoOffset(
                    tp.x, tp.y, tp.z + 0.5, false, false, false);
                try { p.setInvincible(false); } catch (e9) {}
            } else if (p) {
                try { p.freezePosition(false); } catch (e10) {}
                try { p.setCollision(true, true); } catch (e11) {}
                try { p.setInvincible(false); } catch (e12) {}
                p.setCoordsNoOffset(
                    carrierBot.surfaceX, carrierBot.surfaceY,
                    carrierBot.surfaceZ, false, false, false);
            }
        } catch (e) {}
    }
)UAJS"
        R"UAJS(
    function carrierActiveVehId() {
        return carrierBot.routeVehId >= 0
            ? carrierBot.routeVehId : carrierBot.vehRemoteId;
    }

    function carrierTpVeh(pos, heading) {
        if (!pos) return false;
        try {
            var veh = carrierGetVeh(carrierActiveVehId());
            if (!veh) veh = carrierGetVeh(carrierBot.vehRemoteId);
            var p = mp.players.local;
            try { mp.events.call('ac:refreshtp'); } catch (e0) {}
            if (veh) {
                if (!carrierBot.underLoad) {
                    try { veh.freezePosition(false); } catch (e1) {}
                }
                veh.setCoordsNoOffset(
                    pos.x, pos.y, pos.z, false, false, false);
                if (heading != null && !isNaN(heading)) {
                    try { veh.setHeading(Number(heading)); } catch (e2) {}
                }
                try { veh.setVelocity(0, 0, 0); } catch (e3) {}
                if (!carrierBot.underLoad) {
                    try { veh.freezePosition(true); } catch (e4) {}
                }
            } else if (p) {
                p.setCoordsNoOffset(
                    pos.x, pos.y, pos.z, false, false, false);
            }
            return true;
        } catch (e) {
            return false;
        }
    }

    // Keep exact route XY for surfacing; only underground XY is offset.
    function carrierBuryAt(pos, heading) {
        if (!pos) return false;
        carrierBot.surfaceX = Number(pos.x);
        carrierBot.surfaceY = Number(pos.y);
        carrierBot.surfaceZ = Number(pos.z);
        carrierBot.surfaceIsRoute = true;
        carrierRefreshPlayers(Date.now());
        carrierPickSafeUnderXY(
            carrierBot.surfaceX, carrierBot.surfaceY);
        carrierBot.underX = carrierPickUnderX;
        carrierBot.underY = carrierPickUnderY;
        carrierBot.underOffset = carrierPickUnderOffset;
        carrierBot.underZ =
            carrierBot.surfaceZ - CARRIER_UNDER_DEPTH;
        carrierBot.underLoad = true;
        carrierUnderTarget.x = carrierBot.underX;
        carrierUnderTarget.y = carrierBot.underY;
        carrierUnderTarget.z = carrierBot.underZ;
        carrierTpVeh(carrierUnderTarget, heading);
        try { carrierHoldUnderLoad(); } catch (eH) {}
        if (carrierBot.underHoldId == null) {
            carrierBot.underHoldId =
                setInterval(carrierHoldUnderLoad, 150);
        }
        return true;
    }

    function carrierUnfreezeVeh() {
        try {
            var veh = carrierGetVeh(carrierActiveVehId());
            if (!veh) veh = carrierGetVeh(carrierBot.vehRemoteId);
            if (veh) {
                veh.freezePosition(false);
                try { veh.setCollision(true, true); } catch (e0) {}
            }
            if (veh &&
                Number(veh.remoteId) !==
                    Number(carrierBot.vehRemoteId)) {
                var bound = carrierGetVeh(carrierBot.vehRemoteId);
                if (bound) {
                    try { bound.freezePosition(false); } catch (e0b) {}
                    try { bound.setCollision(true, true); }
                    catch (e0c) {}
                }
            }
            var p = mp.players.local;
            if (p) {
                p.freezePosition(false);
                try { p.setCollision(true, true); } catch (e1) {}
                try { p.setInvincible(false); } catch (e2) {}
            }
        } catch (e) {}
    }

    function carrierLeaveVeh() {
        try {
            var p = mp.players.local;
            var veh = p && p.vehicle;
            carrierUnfreezeVeh();
            if (veh && typeof p.taskLeaveVehicle === 'function')
                p.taskLeaveVehicle(veh.handle, 16);
            else if (typeof p.taskLeaveAnyVehicle === 'function')
                p.taskLeaveAnyVehicle(0, 16);
            return true;
        } catch (e) {
            return false;
        }
    }

    // Seat in truck WITHOUT unfreeze (unfreeze pops to surface)
    function carrierEnsureInTruck() {
        try {
            var id = carrierActiveVehId();
            var veh = carrierGetVeh(id);
            var p = mp.players.local;
            if (!veh || !p) return false;
            if (p.vehicle &&
                Number(p.vehicle.remoteId) === Number(id))
                return true;
            try { mp.events.call('ac:refreshtp'); } catch (e0) {}
            if (typeof p.setIntoVehicle === 'function')
                p.setIntoVehicle(veh.handle, -1);
            else if (typeof p.taskEnterVehicle === 'function')
                p.taskEnterVehicle(veh.handle, 10, -1, 2, 1, 0);
            return true;
        } catch (e) {
            return false;
        }
    }

    function carrierEnterTruck() {
        return carrierEnsureInTruck();
    }

    function carrierCollectSlotVehicle(vehicle) {
        if (!vehicle) return;
        try {
            if (mp.vehicles.exists && !mp.vehicles.exists(vehicle)) return;
            if (carrierSlotScanMine >= 0 &&
                Number(vehicle.remoteId) === carrierSlotScanMine)
                return;
            if (carrierSlotScanDimension != null &&
                vehicle.dimension != null &&
                Number(vehicle.dimension) !==
                    carrierSlotScanDimension)
                return;
            var pos = vehicle.position;
            if (!pos) return;
            var dx = Number(pos.x) - carrierSlotScanX;
            var dy = Number(pos.y) - carrierSlotScanY;
            var d2 = dx * dx + dy * dy;
            if (d2 < carrierSlotNearestVehicleD2)
                carrierSlotNearestVehicleD2 = d2;
            if (d2 < 7 * 7)
                carrierSlotScanBusy = true;
        } catch (e) {}
    }

    function carrierSlotVehicleClearanceD2(slot) {
        if (!slot || !slot.pos) return 0;
        carrierSlotScanX = Number(slot.pos.x);
        carrierSlotScanY = Number(slot.pos.y);
        carrierSlotScanMine = Number(
            carrierBot.routeVehId >= 0
                ? carrierBot.routeVehId
                : carrierBot.vehRemoteId);
        try {
            var local = mp.players.local;
            carrierSlotScanDimension =
                local && local.dimension != null
                    ? Number(local.dimension) : null;
        } catch (eD) {
            carrierSlotScanDimension = null;
        }
        carrierSlotScanBusy = false;
        carrierSlotNearestVehicleD2 = 1e30;
        try {
            mp.vehicles.forEach(carrierCollectSlotVehicle);
        } catch (e) {}
        return carrierSlotScanBusy
            ? 0 : carrierSlotNearestVehicleD2;
    }

    function carrierSlotOccupied(slot) {
        return carrierSlotVehicleClearanceD2(slot) <= 0;
    }

    function carrierPickFreeSlot() {
        var best = null;
        var bestClearD2 = -1;
        var bestCombinedD2 = -1;
        carrierRefreshPlayers(Date.now());
        for (var i = 0; i < carrierBot.parkSlots.length; i++) {
            var s = carrierBot.parkSlots[i];
            if (!s) continue;
            var idx = s.posIndex;
            if (carrierBot.triedSlots[String(idx)]) continue;
            var vehicleD2 =
                carrierSlotVehicleClearanceD2(s);
            if (vehicleD2 <= 0) continue;
            var playerD2 = carrierNearestD2At(
                Number(s.pos.x),
                Number(s.pos.y));
            var clearD2 = Math.min(
                vehicleD2,
                playerD2);
            var combinedD2 =
                Math.min(vehicleD2, 1e8) +
                Math.min(playerD2, 1e8);
            if (!best ||
                clearD2 > bestClearD2 ||
                (clearD2 === bestClearD2 &&
                 combinedD2 > bestCombinedD2)) {
                best = s;
                bestClearD2 = clearD2;
                bestCombinedD2 = combinedD2;
            }
        }
        if (best) {
            best.clearance =
                bestClearD2 >= 0 &&
                bestClearD2 < 1e29
                    ? Math.sqrt(bestClearD2)
                    : -1;
        }
        return best;
    }

    function carrierParseSlots(data) {
        var out = [];
        if (!data) return out;
        var arr = Array.isArray(data) ? data : [data];
        for (var i = 0; i < arr.length; i++) {
            var p = arr[i];
            if (!p) continue;
            var pos = carrierNormPos(p);
            if (!pos) continue;
            var idx = p.posIndex != null ? Number(p.posIndex)
                : (p.index != null ? Number(p.index) : i);
            out.push({
                pos: pos,
                heading: pos.h != null ? pos.h
                    : (p.heading != null ? Number(p.heading) : 0),
                posIndex: idx
            });
        }
        return out;
    }

)UAJS"
        R"UAJS(
    function carrierBlacklistOrder(id, ttl, reason) {
        var orderId = Number(id);
        if (!(orderId > 0)) return;
        carrierBot.blacklist[String(orderId)] = {
            until: Date.now() + Math.max(1000, Number(ttl) || 15000),
            reason: String(reason || 'retry')
        };
    }

    function carrierOrderBlacklisted(id, now) {
        var key = String(id);
        var entry = carrierBot.blacklist[key];
        if (!entry) return false;
        if (entry === true) return true;
        if (Number(entry.until) > now) return true;
        delete carrierBot.blacklist[key];
        return false;
    }

    function carrierOrderNeedsTrailer(order) {
        if (!order) return false;
        if (order.hasTrailer === true ||
            order.needTrailer === true ||
            order.needsTrailer === true ||
            order.trailerRequired === true ||
            order.isTrailer === true ||
            Number(order.hasTrailer) === 1 ||
            Number(order.needTrailer) === 1 ||
            Number(order.trailerRequired) === 1 ||
            String(order.hasTrailer).toLowerCase() === 'true' ||
            String(order.needTrailer).toLowerCase() === 'true')
            return true;
        if (order.trailer != null && order.trailer !== false &&
            Number(order.trailer) !== 0)
            return true;
        var type = order.vehicleType != null ? order.vehicleType
            : (order.transportType != null ? order.transportType
                : (order.truckType != null ? order.truckType : ''));
        return String(type).toLowerCase().indexOf('trailer') >= 0;
    }

    function carrierOrderIllegal(order) {
        if (!order) return false;
        return order.isIllegal === true ||
            order.illegal === true ||
            Number(order.isIllegal) === 1 ||
            String(order.isIllegal).toLowerCase() === 'true';
    }

    function carrierAppendOrders(list) {
        if (!Array.isArray(list)) return;
        for (var i = 0; i < list.length; i++) {
            var order = list[i];
            if (!order || order.id == null) continue;
            carrierBot.ordersSeen++;
            if (carrierOrderNeedsTrailer(order)) {
                carrierBot.trailerOrdersSkipped++;
                continue;
            }
            carrierBot.orders.push(order);
        }
    }

    function carrierLoadOrders(data) {
        carrierBot.orders.length = 0;
        carrierBot.ordersSeen = 0;
        carrierBot.trailerOrdersSkipped = 0;
        var payload = data;
        if (typeof payload === 'string') {
            try { payload = JSON.parse(payload); } catch (eP) {}
        }
        if (Array.isArray(payload)) {
            carrierAppendOrders(payload);
            return payload;
        }
        var orders = payload && payload.ordersData != null
            ? payload.ordersData
            : (payload && payload.orders != null
                ? payload.orders : null);
        if (Array.isArray(orders)) {
            carrierAppendOrders(orders);
            return payload;
        }
        if (orders && typeof orders === 'object') {
            for (var key in orders) {
                if (Object.prototype.hasOwnProperty.call(orders, key))
                    carrierAppendOrders(orders[key]);
            }
        }
        return payload;
    }

    function carrierPickOrder() {
        var cap = carrierCapacity();
        var list = carrierBot.orders || [];
        var now = Date.now();
        var exactCap =
            Number(carrierBot.knownVehWeight) > 0 ||
            Number(carrierBot.maxWeight) > 0;
        var best = null;
        var bestPrice = -1;
        var lightest = null;
        var lightestWeight = 1e30;
        var probe = null;
        var probeWeight = 1e30;
        var learned = Number(carrierBot.learnedVehWeight);
        for (var i = 0; i < list.length; i++) {
            var o = list[i];
            if (!o || o.id == null) continue;
            if (carrierOrderBlacklisted(o.id, now)) continue;
            if (carrierBot.skipIllegal && carrierOrderIllegal(o)) continue;
            if (carrierOrderNeedsTrailer(o)) continue;
            if (o.executingDriver || o.inProgress || o.taken) continue;
            var w = Number(o.weight);
            if (isNaN(w)) continue;
            if (w < lightestWeight) {
                lightestWeight = w;
                lightest = o;
            }
            if (!exactCap && learned > 0 && w > learned &&
                w < probeWeight) {
                probeWeight = w;
                probe = o;
            }
            if (cap > 0 && w > cap) continue;
            var price = Number(
                o.orderPrice != null ? o.orderPrice : o.price) || 0;
            if (!best || price > bestPrice ||
                (price === bestPrice &&
                 w < Number(best.weight))) {
                best = o;
                bestPrice = price;
            }
        }
        if (!exactCap && !(learned > 0) && lightest) {
            carrierBot.capacitySource = 'probing';
            carrierBot.capacityProbe = false;
            return lightest;
        }
        if (!exactCap && carrierBot.capacityProbe && probe) {
            carrierBot.capacitySource = 'probing';
            carrierBot.capacityProbe = false;
            return probe;
        }
        if (best) return best;
        if (!exactCap && lightest) {
            carrierBot.capacitySource = 'probing';
            carrierBot.capacityProbe = false;
            return lightest;
        }
        return null;
    }

    function carrierResetDeliveryState() {
        carrierBot.deliveryAck = 'idle';
        carrierBot.routeTotal = 0;
        carrierBot.deliveredCount = 0;
        carrierBot.completionConfirmed = false;
        carrierBot.deliveryIndex = -1;
        carrierBot.deliveryExpectedIndex = -1;
        carrierBot.deliveryRetries = 0;
        carrierBot.deliveryResyncs = 0;
        carrierBot.deliveryLastSendAt = 0;
        carrierBot.deliveryLastAck = -1;
        carrierBot.deliveryLastAckAt = 0;
        carrierBot.deliverAt = 0;
    }

    function carrierResetOrderState() {
        carrierStopSpam();
        carrierResetPlayerWait(Date.now(), false);
        var restorePark =
            !carrierBot.underLoad &&
            carrierBot.truckParkPos
                ? carrierBot.truckParkPos : null;
        carrierLeaveUnderLoad();
        if (restorePark) {
            try {
                var parkedTruck =
                    carrierGetVeh(carrierBot.vehRemoteId);
                if (parkedTruck) {
                    try { parkedTruck.freezePosition(false); }
                    catch (ePf) {}
                    try { parkedTruck.setCollision(true, true); }
                    catch (ePc) {}
                    parkedTruck.setCoordsNoOffset(
                        restorePark.x,
                        restorePark.y,
                        restorePark.z,
                        false, false, false);
                    if (restorePark.h != null) {
                        try {
                            parkedTruck.setHeading(restorePark.h);
                        } catch (ePh) {}
                    }
                }
            } catch (eParkRestore) {}
        }
        carrierStopProtect();
        carrierUnfreezeVeh();
        carrierBot.orderAccepted = false;
        carrierBot.orderId = 0;
        carrierBot.orderWeight = 0;
        carrierBot.orderPrice = 0;
        carrierBot.busyUntil = 0;
        carrierBot.watchdogAt = 0;
        carrierBot.takeAt = 0;
        carrierBot.fetchAt = 0;
        carrierBot.orders.length = 0;
        carrierBot.parkSlots = [];
        carrierBot.triedSlots = {};
        carrierBot.parkPosIndex = -1;
        carrierBot.parkAt = 0;
        carrierBot.parkEndDueAt = 0;
        carrierBot.parkEndSent = false;
        carrierBot.parkingRetries = 0;
        carrierBot.parkingRounds = 0;
        carrierBot.noSlotAt = 0;
        carrierBot.pedPos = null;
        carrierBot.loadYardPos = null;
        carrierBot.route = [];
        carrierBot.routeIndex = 0;
        carrierBot.routeVehId = -1;
        carrierBot.hasTrailer = false;
        carrierBot.cancelOfferSeen = false;
        carrierBot.truckParkPos = null;
        carrierBot.underStarted = false;
        carrierBot.underWaitAt = 0;
        carrierBot.waitSlotIndex = -1;
        carrierBot.waitSlotRotations = 0;
        carrierBot.playerBypassOnce = false;
        carrierBot.playerBypassUsed = false;
        carrierBot.seatIssue = '';
        carrierBot.seatIssueAt = 0;
        carrierBot.resumePhase = 'fetch_orders';
        carrierBot.vehicleMissingAt = 0;
        carrierResetDeliveryState();
        try { mp.events.call('carrier.order.modal.close', true); }
        catch (eC) {}
    }

    function carrierReturnToFetch(reason, delay) {
        carrierResetOrderState();
        carrierBot.prerequisite = 'ready';
        carrierSetPhase(
            'fetch_orders',
            String(reason || 'reset') + ' → fetch');
        carrierBot.busyUntil =
            Date.now() + Math.max(250, Number(delay) || 800);
    }

    function carrierCompleteOrderCycle(reason) {
        var completedVehId =
            Number(carrierBot.routeVehId);
        carrierBot.ignoredRouteVehId =
            isFinite(completedVehId) &&
            completedVehId >= 0
                ? completedVehId : -1;
        carrierBot.completionConfirmed = true;
        carrierBot.deliveredCount = Math.max(
            Number(carrierBot.deliveredCount) || 0,
            Number(carrierBot.routeTotal) ||
                carrierBot.route.length);
        carrierRememberAnswer(
            reason || 'order.end');
        carrierLearnAcceptedCapacity();
        carrierReturnToFetch(
            reason || 'order end confirmed',
            200);
        setTimeout(function () {
            if (!carrierBot.on) return;
            try {
                mp.events.call(
                    'carrier.order.modal.close',
                    true);
            } catch (eClose) {}
        }, 300);
    }

    function carrierLateTrailer(reason) {
        if (!carrierBot.on) return;
        var ignoredVehId =
            Number(carrierBot.routeVehId);
        if (carrierBot.orderId)
            carrierBlacklistOrder(
                carrierBot.orderId, 120000, 'trailer');
        if (carrierBot.cancelOfferSeen) {
            try {
                mp.events.callRemote(
                    'carrier.loading.cancel.accept');
            } catch (eCancel) {}
        }
        carrierRememberAnswer('trailer');
        carrierReturnToFetch(
            'trailer skipped' +
            (reason ? ' (' + String(reason) + ')' : ''),
            1000);
        if (ignoredVehId >= 0)
            carrierBot.ignoredRouteVehId = ignoredVehId;
    }

    function carrierReconnectCleanup(suspended) {
        var keepAuto = !!carrierBot.autoMode;
        carrierResetOrderState();
        carrierBot.suspended = !!suspended;
        carrierBot.employmentKnown = false;
        carrierBot.isEmployed = false;
        carrierBot.inGarageKnown = false;
        carrierBot.inGarage = false;
        carrierBot.garageEnterInside = false;
        carrierBot.garageVehicleInside = false;
        carrierBot.garageAction = '';
        carrierBot.prereqActionAt = 0;
        carrierBot.prereqActionRetries = 0;
        carrierBot.badVehRemoteId = -1;
        carrierBot.ignoredRouteVehId = -1;
        carrierBot.vehRemoteId = -1;
        carrierBot.boundVehModel = 0;
        carrierBot.on = keepAuto;
        if (keepAuto) {
            carrierBot.prerequisite =
                suspended ? 'reconnect' : 'checking';
            carrierBot.phase = 'idle';
            carrierSetPhase(
                'preflight',
                suspended
                    ? 'connection lost · Auto waiting'
                    : 'reconnected · checking prerequisites');
        } else {
            carrierBot.prerequisite = 'off';
            carrierSetPhase('idle');
        }
    }
)UAJS"
        R"UAJS(
    function carrierEnsureHooks() {
        if (carrierBot.hooksReady) return;
        carrierBot.hooksReady = true;
        try {
            mp.events.add('carrier.init', function (pedData, isEmployed) {
                carrierBot.employmentKnown = true;
                carrierBot.isEmployed = !!isEmployed;
                if (pedData &&
                    carrierReadPosInto(pedData, carrierRouteTarget)) {
                    carrierBot.employX = carrierRouteTarget.x;
                    carrierBot.employY = carrierRouteTarget.y;
                    carrierBot.employZ = carrierRouteTarget.z;
                    carrierBot.employPosKnown = true;
                }
                if (carrierBot.on)
                    carrierProgress(
                        carrierBot.isEmployed
                            ? 'employment detected'
                            : 'employment required');
            });
            mp.events.add(
                'carrier.employ.shape',
                function (isEnter, isEmployed) {
                    carrierBot.employmentKnown = true;
                    carrierBot.isEmployed = !!isEmployed;
                    carrierBot.employShapeInside = !!isEnter;
                    carrierBot.prereqActionRetries = 0;
                    carrierBot.prereqActionAt = 0;
                    if (carrierBot.on && isEnter &&
                        !carrierBot.isEmployed)
                        carrierStatus(
                            carrierHasEmployApi()
                                ? 'employment API ready'
                                : 'use employment NPC once; Auto resumes');
                });
            mp.events.add(
                'carrier.employ.changed',
                function (isEmployed) {
                    carrierBot.employmentKnown = true;
                    carrierBot.isEmployed = !!isEmployed;
                    carrierBot.prereqActionRetries = 0;
                    carrierBot.prereqActionAt = 0;
                    if (carrierBot.on) {
                        if (carrierBot.isEmployed) {
                            carrierSetPrerequisite('checking');
                            carrierSetPhase(
                                'preflight',
                                'employed · Auto resuming');
                        } else {
                            carrierReturnToFetch('employment ended', 500);
                            carrierSetPrerequisite('need_employment');
                            carrierSetPhase(
                                'need_employment',
                                'employment ended · Auto waiting');
                        }
                    }
                });
            mp.events.add('carrier.ans', function (type) {
                if (!carrierBot.on) return;
                carrierRememberAnswer(type);
                if (String(type || '') === 'needHigherLvl') {
                    carrierBot.employmentKnown = true;
                    carrierBot.isEmployed = false;
                    carrierSetPrerequisite('need_employment');
                    carrierSetPhase(
                        'need_employment',
                        'employment level requirement not met');
                }
            });
        } catch (ePre0) {}
        try {
            mp.events.add(
                'carrier.company.init',
                function (data) {
                    carrierBot.companyKnown = true;
                    carrierRememberGaragePosition(data);
                    carrierBot.prereqActionRetries = 0;
                    carrierBot.prereqActionAt = 0;
                    if (carrierBot.on)
                        carrierProgress('company garage detected');
                });
            mp.events.add('carrier.company.clear', function () {
                carrierBot.companyKnown = false;
                carrierBot.inGarageKnown = false;
                carrierBot.inGarage = false;
                carrierBot.garagePosKnown = false;
                carrierBot.garageParkingKnown = false;
                carrierBot.garageExitKnown = false;
                carrierBot.garageAction = '';
            });
            mp.events.add(
                'carrier.company.inGarage',
                function (inside, pos) {
                    carrierBot.companyKnown = true;
                    carrierBot.inGarageKnown = true;
                    carrierBot.inGarage = !!inside;
                    carrierBot.prereqActionRetries = 0;
                    carrierBot.prereqActionAt = 0;
                    if (pos &&
                        carrierReadPosInto(pos, carrierRouteTarget)) {
                        carrierBot.garageExitX = carrierRouteTarget.x;
                        carrierBot.garageExitY = carrierRouteTarget.y;
                        carrierBot.garageExitZ = carrierRouteTarget.z;
                        carrierBot.garageExitKnown = true;
                    }
                    if (!inside) {
                        carrierBot.garageAction = '';
                        carrierBot.garageEnterInside = false;
                        carrierBot.garageVehicleInside = false;
                    }
                    if (carrierBot.on)
                        carrierProgress(
                            inside ? 'inside company garage'
                                : 'left company garage');
                });
            mp.events.add(
                'carrier.company.garage.shape.enter',
                function (inside) {
                    carrierBot.garageEnterInside = !!inside;
                    carrierBot.garageAction = inside ? 'enter' : '';
                    carrierBot.prereqActionRetries = 0;
                    carrierBot.prereqActionAt = 0;
                });
            mp.events.add(
                'carrier.company.garage.shape.enterVehicle',
                function (inside) {
                    carrierBot.garageVehicleInside = !!inside;
                });
            mp.events.add('playerEnterVehicle', function (vehicle) {
                if (!carrierBot.on || !carrierBot.inGarage) return;
                if (vehicle) carrierBindVehicle(vehicle, 'garage seat');
                carrierBot.garageAction = 'take';
                carrierBot.prereqActionRetries = 0;
                carrierBot.prereqActionAt = 0;
            });
            mp.events.add('playerLeaveVehicle', function (vehicle) {
                if (!carrierBot.on) return;
                if (vehicle &&
                    Number(vehicle.remoteId) ===
                        Number(carrierBot.vehRemoteId) &&
                    carrierBot.phase !== 'park_settle' &&
                    carrierBot.phase !== 'wait_park_ack' &&
                    carrierBot.phase !== 'forklift_spam') {
                    carrierBot.vehicleMissingAt = Date.now();
                }
            });
            mp.events.add(
                'carrier.company.ans',
                function (type) {
                    if (!carrierBot.on) return;
                    carrierRememberAnswer(type);
                    if (String(type || '') === 'noVehicle') {
                        carrierBot.garageAction = '';
                        carrierBot.prereqActionRetries = 3;
                        carrierStatus(
                            'garage has no proven truck · Auto waiting');
                    }
                });
            mp.events.add(
                'carrier.company.app.ans',
                function (type) {
                    if (carrierBot.on)
                        carrierRememberAnswer(type);
                });
        } catch (ePre1) {}
        try {
            mp.events.add('connectionClosed', function () {
                if (carrierBot.autoMode)
                    carrierReconnectCleanup(true);
            });
            mp.events.add('serverShutdown', function () {
                if (carrierBot.autoMode)
                    carrierReconnectCleanup(true);
            });
            mp.events.add('auth.init', function () {
                if (carrierBot.autoMode)
                    carrierReconnectCleanup(true);
            });
            mp.events.add('playerReady', function () {
                if (carrierBot.autoMode && carrierBot.suspended)
                    carrierReconnectCleanup(false);
            });
            mp.events.add('characterInit.done', function () {
                if (carrierBot.autoMode && carrierBot.suspended)
                    carrierReconnectCleanup(false);
            });
        } catch (ePre2) {}
)UAJS"
        R"UAJS(
        try {
            mp.events.add('carrier.order.app.connect', function (payload) {
                var data = carrierLoadOrders(payload);
                try {
                    if (data && data.lvlData && data.lvlData.myOrders != null)
                        carrierBot.myLvl = Number(data.lvlData.myOrders);
                } catch (eL) {}
                if (carrierBot.on) {
                    carrierProgress(
                        'orders ' + carrierBot.orders.length +
                        '/' + carrierBot.ordersSeen +
                        ' cap=' + carrierCapacity() +
                        (carrierBot.trailerOrdersSkipped
                            ? ' trailer-' +
                                carrierBot.trailerOrdersSkipped
                            : ''));
                    if (carrierBot.phase === 'wait_orders')
                        carrierSetPhase('pick_take');
                }
            });
        } catch (e0) {}
        try {
            mp.events.add('carrier.order.app.ans', function (type, data) {
                if (!carrierBot.on) return;
                var t = String(type || '');
                carrierRememberAnswer(t);
                if (t === 'orderAccepted') {
                    carrierBot.orderAccepted = true;
                    carrierLearnAcceptedCapacity();
                    carrierStartProtect();
                    carrierSetPhase(
                        'wait_load_yard',
                        'orderAccepted #' + carrierBot.orderId);
                    carrierBot.busyUntil = Date.now() + 400;
                    return;
                }
                if (t === 'vehicleNoHaveWeightForOrder') {
                    try {
                        var vw = data && data.vehWeight != null
                            ? Number(data.vehWeight) : -1;
                        carrierLearnServerCapacity(vw);
                    } catch (eW) {}
                    if (carrierBot.orderId)
                        carrierBlacklistOrder(
                            carrierBot.orderId, 30000, 'overweight');
                    carrierBot.orderId = 0;
                    carrierBot.orderAccepted = false;
                    carrierSetPhase(
                        'pick_take',
                        'weight reject cap=' +
                            carrierCapacity());
                    carrierBot.busyUntil = Date.now() + 300;
                    return;
                }
                if (t === 'youHaveActiveOrder') {
                    if (carrierBot.ignoredRouteVehId >= 0) {
                        carrierReturnToFetch(
                            'server still clearing trailer order',
                            5000);
                        return;
                    }
                    carrierBot.orderAccepted = true;
                    carrierStartProtect();
                    carrierSetPhase(
                        'wait_load_yard',
                        'server reports active order · resync');
                    return;
                }
                if (t === 'needSeatInVehicle' || t === 'invalidVehicle' ||
                    t === 'notPrivateVehicle') {
                    carrierBot.seatIssue = t;
                    carrierBot.seatIssueAt = Date.now();
                    carrierBot.resumePhase =
                        carrierBot.orderAccepted
                            ? 'wait_load_yard' : 'wait_take';
                    if (t !== 'needSeatInVehicle')
                        carrierBot.badVehRemoteId =
                            carrierBot.vehRemoteId;
                    carrierSetPhase(
                        'rebind_vehicle',
                        t + ' · rebind/reseat');
                    carrierBot.busyUntil = Date.now() + 250;
                    return;
                }
                if (t === 'youNotCarrier') {
                    carrierBot.employmentKnown = true;
                    carrierBot.isEmployed = false;
                    carrierResetOrderState();
                    carrierSetPrerequisite('need_employment');
                    carrierSetPhase(
                        'need_employment',
                        'server: not employed · Auto waiting');
                    return;
                }
                if (carrierBot.phase === 'wait_take' ||
                    carrierBot.phase === 'rebind_vehicle') {
                    if (carrierBot.orderId)
                        carrierBlacklistOrder(
                            carrierBot.orderId, 20000, t);
                    carrierBot.orderId = 0;
                    carrierBot.orderAccepted = false;
                    carrierBot.capacityProbe = true;
                    carrierSetPhase('pick_take', 'ans ' + t);
                    carrierBot.busyUntil = Date.now() + 400;
                }
            });
        } catch (e1) {}
        try {
            mp.events.add('carrier.loading.route', function (pos) {
                if (!carrierBot.on) return;
                carrierBot.loadYardPos = carrierNormPos(pos);
                if (carrierBot.orderAccepted &&
                    carrierBot.phase === 'wait_load_yard') {
                    carrierSetPhase('to_yard', 'yard route');
                }
            });
        } catch (e2) {}
        try {
            mp.events.add('carrier.loading.points.show', function (data) {
                if (!carrierBot.on || !carrierBot.orderAccepted) return;
                carrierBot.parkSlots = carrierParseSlots(data);
                carrierBot.triedSlots = {};
                carrierBot.parkEndDueAt = 0;
                carrierBot.parkEndSent = false;
                carrierBot.parkingRetries = 0;
                carrierBot.parkingRounds = 0;
                carrierBot.noSlotAt = 0;
                carrierBot.waitSlotIndex = -1;
                carrierBot.waitSlotRotations = 0;
                carrierBot.playerBypassOnce = false;
                carrierBot.playerBypassUsed = false;
                carrierSetPhase(
                    'park',
                    'park slots ' + carrierBot.parkSlots.length);
                carrierBot.busyUntil = Date.now() + 200;
            });
        } catch (e3) {}
        try {
            mp.events.add('carrier.loading.point.show', function (data) {
                if (!carrierBot.on) return;
                var one = carrierParseSlots([data]);
                if (one.length)
                    carrierBot.parkSlots = carrierBot.parkSlots.concat(one);
            });
        } catch (e3b) {}
        try {
            mp.events.add('carrier.loading.ans', function (type, data) {
                if (!carrierBot.on) return;
                var t = String(type || '');
                carrierRememberAnswer(t);
                if (t === 'parkingOccupied') {
                    if (carrierBot.parkPosIndex >= 0)
                        carrierBot.triedSlots[
                            String(carrierBot.parkPosIndex)] = true;
                    carrierResetPlayerWait(Date.now(), false);
                    carrierBot.parkEndDueAt = 0;
                    carrierBot.parkEndSent = false;
                    carrierBot.parkingRetries++;
                    carrierRetry('parkingOccupied → next');
                    if (carrierBot.parkingRetries >= 12) {
                        if (carrierBot.orderId)
                            carrierBlacklistOrder(
                                carrierBot.orderId,
                                30000,
                                'parking');
                        carrierReturnToFetch(
                            'parking retry cap', 1200);
                        return;
                    }
                    carrierSetPhase('park');
                    carrierBot.busyUntil = Date.now() + 250;
                    return;
                }
                if (t === 'loadingStarted' || t === 'loadingInProgress') {
                    if (carrierBot.phase === 'npc_manual' ||
                        carrierBot.phase === 'forklift_spam') {
                        carrierSetPhase(
                            'forklift_spam',
                            t);
                        if (!carrierBot.spamId) carrierStartSpam();
                    }
                    return;
                }
                if (t === 'noMoreLoadNeeded') {
                    carrierStopSpam();
                    // Stay under; retain exact park return until route bury.
                    carrierSetPhase('enter_truck', 'load done');
                    return;
                }
                if (t === 'needParkVehicle') {
                    carrierResetPlayerWait(Date.now(), false);
                    carrierSetPhase('park', 'needParkVehicle');
                    return;
                }
                if (t === 'needSeatInVehicle' ||
                    t === 'wrongVehicle' ||
                    t === 'notYourOrderVehicle') {
                    carrierBot.seatIssue = t;
                    carrierBot.seatIssueAt = Date.now();
                    carrierBot.resumePhase =
                        carrierBot.parkSlots.length
                            ? 'park' : 'wait_load_yard';
                    if (t !== 'needSeatInVehicle')
                        carrierBot.badVehRemoteId =
                            carrierBot.vehRemoteId;
                    carrierSetPhase(
                        'rebind_vehicle',
                        t + ' · rebind/reseat');
                    return;
                }
                if (t === 'vehicleNotFound' ||
                    t === 'forkliftNotFound' ||
                    t === 'loadingCancelled') {
                    if (carrierBot.orderId)
                        carrierBlacklistOrder(
                            carrierBot.orderId, 30000, t);
                    carrierReturnToFetch(t, 1000);
                    return;
                }
            });
        } catch (e4) {}
)UAJS"
        R"UAJS(
        try {
            mp.events.add('carrier.loading.ped.show', function (pos) {
                if (!carrierBot.on || !carrierBot.orderAccepted) return;
                carrierBot.pedPos = carrierNormPos(pos);
                carrierSetPhase('npc_manual', 'logistic ped');
                carrierBot.busyUntil = Date.now() + 300;
            });
        } catch (e5) {}
        try {
            mp.events.add('carrier.loading.npcShape', function (isIn) {
                if (!carrierBot.on) return;
                if (isIn && carrierBot.phase === 'npc_manual')
                    carrierStatus('npc shape');
            });
        } catch (e5b) {}
        try {
            mp.events.add('carrier.loading.forklift.enter', function () {
                if (!carrierBot.on) return;
                carrierSetPhase(
                    'forklift_spam',
                    'forklift entered');
                if (!carrierBot.underStarted && !carrierBot.underLoad)
                    carrierGoUnderLoad(
                        carrierBot.pedPos);
                if (!carrierBot.spamId) carrierStartSpam();
            });
        } catch (e6) {}
        try {
            mp.events.add('carrier.loading.start', function () {
                if (!carrierBot.on) return;
                carrierSetPhase(
                    'forklift_spam',
                    'loading start');
                if (!carrierBot.spamId) carrierStartSpam();
            });
        } catch (e6b) {}
        try {
            mp.events.add('carrier.order.route.start', function (payload) {
                if (!carrierBot.on) return;
                carrierStopSpam();
                // Do not surface; retain exact park return for trailer abort.
                var d = payload;
                if (typeof payload === 'string') {
                    try { d = JSON.parse(payload); } catch (eP) {}
                }
                // args may be spread: routeIndex, route, vehId, ...
                if (!d || !d.route) {
                    d = {
                        routeIndex: arguments[0],
                        route: arguments[1],
                        vehId: arguments[2],
                        hasTrailer: arguments[6]
                    };
                    if (arguments[0] && arguments[0].route)
                        d = arguments[0];
                }
                carrierBot.route = Array.isArray(d.route) ? d.route : [];
                var startIndex =
                    d.routeIndex != null
                        ? Number(d.routeIndex) : 0;
                if (!isFinite(startIndex) || startIndex < 0)
                    startIndex = 0;
                carrierBot.routeIndex = Math.min(
                    Math.floor(startIndex),
                    carrierBot.route.length);
                carrierBot.routeVehId =
                    d.vehId != null ? Number(d.vehId)
                        : carrierBot.vehRemoteId;
                carrierBot.hasTrailer = !!d.hasTrailer;
                if (carrierBot.hasTrailer) {
                    carrierLateTrailer('route.start');
                    return;
                }
                carrierBot.ignoredRouteVehId = -1;
                carrierBot.orderAccepted = true;
                carrierLearnAcceptedCapacity();
                carrierResetDeliveryState();
                carrierBot.routeTotal =
                    carrierBot.route.length;
                carrierBot.deliveredCount =
                    carrierBot.routeIndex;
                carrierBot.deliveryAck = 'ready';
                carrierStartProtect();
                carrierHealTruck();
                carrierSetPhase(
                    'enter_truck',
                    'route pts=' + carrierBot.route.length +
                    ' i=' + carrierBot.routeIndex +
                    ' · protect ON');
                carrierBot.busyUntil = Date.now() + 400;
            });
        } catch (e7) {}
        try {
            mp.events.add('carrier.order.route.next',
                function (taked, total) {
                    if (!carrierBot.on) return;
                    var ack = Number(taked);
                    if (!isFinite(ack) || ack < 0) {
                        carrierBot.deliveryAck = 'bad ack';
                        carrierRememberAnswer('route.next:bad');
                        return;
                    }
                    ack = Math.floor(ack);
                    var totalCount = Number(total);
                    if (isFinite(totalCount) &&
                        totalCount > 0) {
                        carrierBot.routeTotal =
                            Math.floor(totalCount);
                    } else if (!(carrierBot.routeTotal > 0)) {
                        carrierBot.routeTotal =
                            carrierBot.route.length;
                    }
                    carrierBot.deliveryLastAck = ack;
                    carrierBot.deliveryLastAckAt = Date.now();
                    if (ack <= Number(carrierBot.routeIndex)) {
                        carrierBot.deliveryAck =
                            'late/dup ' + ack;
                        return;
                    }
                    carrierBot.deliveredCount = Math.max(
                        Number(carrierBot.deliveredCount) || 0,
                        ack);
                    carrierBot.deliveryRetries = 0;
                    carrierBot.deliveryResyncs = 0;
                    carrierBot.deliveryIndex = -1;
                    carrierBot.deliveryExpectedIndex = -1;
                    carrierBot.deliveryLastSendAt = 0;
                    if (carrierBot.route.length &&
                        ack >= carrierBot.route.length) {
                        carrierBot.routeIndex =
                            carrierBot.route.length;
                        carrierBot.deliveryAck =
                            'terminal ack ' + ack + '/' +
                            carrierBot.routeTotal +
                            ' · wait order.end';
                        carrierSetPhase(
                            'delivery_complete_wait',
                            'route exhausted · authoritative order.end pending');
                    } else {
                        carrierBot.routeIndex = ack;
                        carrierBot.deliveryAck =
                            'ack ' + ack + '/' +
                            carrierBot.routeTotal;
                        carrierSetPhase(
                            'deliver',
                            'next ' + ack + '/' +
                            carrierBot.routeTotal);
                    }
                });
        } catch (e8) {}
        try {
            mp.events.add('carrier.order.end', function () {
                if (!carrierBot.on) return;
                carrierCompleteOrderCycle(
                    'order.end');
            });
        } catch (e9) {}
        try {
            mp.events.add('carrier.order.stop', function (reason) {
                if (!carrierBot.on) return;
                carrierBot.ignoredRouteVehId = -1;
                var stopReason =
                    reason != null ? String(reason) : '';
                carrierRememberAnswer(
                    'order.stop' +
                    (stopReason ? ':' + stopReason : ''));
                if (carrierBot.orderId)
                    carrierBlacklistOrder(
                        carrierBot.orderId, 30000, 'order.stop');
                carrierReturnToFetch(
                    'order.stop' +
                    (stopReason ? ' ' + stopReason : ''),
                    1000);
                if (stopReason === 'jobChange' ||
                    stopReason === 'kickedFromCompany') {
                    carrierBot.employmentKnown = true;
                    carrierBot.isEmployed = false;
                    carrierSetPrerequisite('need_employment');
                    carrierSetPhase(
                        'need_employment',
                        'carrier employment ended · Auto waiting');
                }
            });
        } catch (e10) {}
        try {
            mp.events.add('carrier.loading.clear', function () {
                if (!carrierBot.on) return;
                if (carrierBot.phase === 'forklift_spam') {
                    carrierStopSpam();
                    carrierSetPhase(
                        'enter_truck',
                        'loading cleared');
                }
            });
        } catch (e11) {}
        try {
            mp.events.add('carrier.order.ans', function (type) {
                if (!carrierBot.on) return;
                var answer = String(type || '');
                carrierRememberAnswer(answer);
                if (answer === 'notAtLocation' &&
                    (carrierBot.phase === 'deliver' ||
                     carrierBot.phase === 'delivery_resync')) {
                    carrierBot.deliveryAck = 'notAtLocation';
                    carrierSetPhase(
                        'delivery_resync',
                        'server location reject · safe resync');
                    carrierBot.watchdogAt = Date.now();
                }
            });
        } catch (e12) {}
        try {
            mp.events.add(
                'carrier.loading.cancel.show',
                function () {
                    if (carrierBot.on)
                        carrierBot.cancelOfferSeen = true;
                });
            mp.events.add(
                'carrier.loading.trailer.route',
                function () {
                    carrierLateTrailer('loading.route');
                });
            mp.events.add(
                'carrier.loading.trailer.spawned',
                function () {
                    carrierLateTrailer('trailer.spawned');
                });
            mp.events.add(
                'carrier.loading.trailer.show',
                function () {
                    carrierLateTrailer('trailer.show');
                });
        } catch (e13) {}
    }
)UAJS"
        R"UAJS(

    function carrierTryReseat() {
        var target = null;
        var routeId = Number(carrierBot.routeVehId);
        var boundId = Number(carrierBot.vehRemoteId);
        if (routeId >= 0 &&
            routeId !== Number(carrierBot.badVehRemoteId))
            target = carrierGetVeh(routeId);
        if (!target && boundId >= 0 &&
            boundId !== Number(carrierBot.badVehRemoteId))
            target = carrierGetVeh(boundId);
        try {
            var player = mp.players.local;
            var current = player && player.vehicle;
            if (!target && current &&
                Number(current.remoteId) !==
                    Number(carrierBot.badVehRemoteId) &&
                !carrierVehicleLooksTrailer(current)) {
                var currentId = Number(current.remoteId);
                var canRebindCurrent =
                    currentId === routeId ||
                    currentId === boundId ||
                    (!carrierBot.orderAccepted &&
                     carrierBot.phase !== 'forklift_spam');
                if (canRebindCurrent) target = current;
            }
            if (!target || !player) return false;
            if (carrierVehicleDriverIsLocal(target)) {
                if (Number(target.remoteId) !==
                        Number(carrierBot.vehRemoteId) ||
                    Number(target.model) !==
                        Number(carrierBot.boundVehModel))
                    carrierBindVehicle(target, '');
                else
                    carrierBot.vehicleMissingAt = 0;
                return true;
            }
            try { mp.events.call('ac:refreshtp'); } catch (e0) {}
            if (typeof player.setIntoVehicle === 'function')
                player.setIntoVehicle(target.handle, -1);
            else if (typeof player.taskEnterVehicle === 'function')
                player.taskEnterVehicle(
                    target.handle, 10, -1, 2, 1, 0);
            return carrierVehicleDriverIsLocal(target);
        } catch (e) {
            return false;
        }
    }

    function carrierProbeOrderEndFromModule(now) {
        try {
            if (!carrierBot.deliveryLastAckAt ||
                now - carrierBot.deliveryLastAckAt < 250)
                return false;
            var state =
                mp.carrierOrder && mp.carrierOrder.state;
            if (!state ||
                state.routeVehId != null ||
                state.routePointData != null ||
                state.routePoint != null)
                return false;
            carrierCompleteOrderCycle(
                'order.end module clear');
            return true;
        } catch (e) {
            return false;
        }
    }

    function carrierProbeDeliveryModuleAck() {
        try {
            var state = mp.carrierOrder && mp.carrierOrder.state;
            if (!state) return false;
            var ack = Number(state.routePosIndex);
            if (!isFinite(ack) ||
                ack <= Number(carrierBot.routeIndex))
                return false;
            ack = Math.floor(ack);
            carrierBot.deliveryLastAck = ack;
            carrierBot.deliveryLastAckAt = Date.now();
            carrierBot.deliveredCount = Math.max(
                Number(carrierBot.deliveredCount) || 0,
                ack);
            carrierBot.deliveryRetries = 0;
            carrierBot.deliveryIndex = -1;
            carrierBot.deliveryExpectedIndex = -1;
            carrierBot.deliveryLastSendAt = 0;
            if (carrierBot.route.length &&
                ack >= carrierBot.route.length) {
                carrierBot.routeIndex =
                    carrierBot.route.length;
                carrierBot.deliveryAck =
                    'resynced terminal ack ' + ack +
                    ' · wait order.end';
                carrierSetPhase(
                    'delivery_complete_wait',
                    'module route exhausted · order.end pending');
            } else {
                carrierBot.routeIndex = ack;
                carrierBot.deliveryAck =
                    'resynced ack ' + ack;
                carrierSetPhase(
                    'deliver',
                    'resynced route ack ' +
                        carrierBot.routeIndex);
            }
            return true;
        } catch (e) {
            return false;
        }
    }

    function carrierDeliveryTick(now) {
        var route = carrierBot.route;
        var index = carrierBot.routeIndex | 0;
        if (!route || !route.length) {
            carrierBot.deliveryAck = 'route missing';
            carrierSetPhase(
                'delivery_resync',
                'route missing · safe resync');
            carrierBot.watchdogAt = now;
            return;
        }
        if (index >= route.length) {
            carrierBot.deliveryAck =
                'route exhausted · wait order.end';
            carrierSetPhase(
                'delivery_complete_wait',
                'no local point · authoritative order.end pending');
            return;
        }
        if (!carrierReadPosInto(
                route[index], carrierRouteTarget)) {
            carrierBot.deliveryAck =
                'invalid point ' + index;
            carrierSetPhase(
                'delivery_resync',
                'invalid route point · no local skip');
            carrierBot.watchdogAt = now;
            return;
        }
        if (!carrierTryReseat()) {
            carrierBot.seatIssue = 'deliverySeat';
            carrierBot.seatIssueAt = now;
            carrierBot.resumePhase = 'deliver';
            carrierSetPhase(
                'rebind_vehicle',
                'delivery truck missing/seat · rebind');
            return;
        }
        if (carrierBot.deliveryIndex !== index) {
            carrierBuryAt(
                carrierRouteTarget, carrierRouteTarget.h);
            carrierBot.deliveryIndex = index;
            carrierBot.deliveryExpectedIndex = index + 1;
            carrierBot.deliveryRetries = 0;
            carrierBot.deliveryLastSendAt = 0;
            carrierBot.deliveryAck =
                'send ' + index + ' · waiting';
        } else if (carrierBot.underLoad) {
            carrierHoldUnderLoad();
        }
        if (carrierBot.deliveryRetries >=
            CARRIER_DELIVERY_RETRY_MAX) {
            carrierBot.deliveryAck =
                'retry cap ' + index;
            carrierSetPhase(
                'delivery_resync',
                'delivery retry cap · safe resync');
            carrierBot.watchdogAt = now;
            return;
        }
        try {
            mp.events.callRemote(
                'carrier.order.route.nextPointReached');
            carrierBot.deliveryRetries++;
            carrierBot.deliveryLastSendAt = now;
            carrierBot.deliveryAck = 'waiting';
        } catch (e) {
            carrierRetry('delivery send error');
        }
    }

    function carrierDeliveryResyncTick(now) {
        if (carrierBot.underLoad) carrierHoldUnderLoad();
        if (carrierProbeDeliveryModuleAck()) return;
        if (!carrierBot.watchdogAt)
            carrierBot.watchdogAt = now;
        if (now - carrierBot.watchdogAt < 1000) return;
        if (carrierBot.deliveryResyncs >=
            CARRIER_DELIVERY_RESYNC_MAX) {
            carrierBot.deliveryAck = 'stalled · capped';
            carrierSetPhase(
                'delivery_stalled',
                'delivery stalled after safe resync cap');
            return;
        }
        try {
            mp.events.callRemote('carrier.app.connect');
        } catch (e) {}
        carrierBot.deliveryResyncs++;
        carrierBot.deliveryRetries = 0;
        carrierBot.deliveryLastSendAt = 0;
        carrierBot.deliveryIndex = -1;
        carrierBot.deliveryAck =
            'resync ' + carrierBot.deliveryResyncs + '/' +
            CARRIER_DELIVERY_RESYNC_MAX;
        carrierRetry(carrierBot.deliveryAck);
        carrierSetPhase('deliver');
    }

    function carrierMonitorOrderTruck(now) {
        if (!carrierBot.orderAccepted &&
            !carrierBot.route.length)
            return false;
        var id = Number(
            carrierBot.routeVehId >= 0
                ? carrierBot.routeVehId
                : carrierBot.vehRemoteId);
        if (id >= 0 && carrierGetVeh(id)) {
            carrierBot.vehicleMissingAt = 0;
            return false;
        }
        if (!carrierBot.vehicleMissingAt) {
            carrierBot.vehicleMissingAt = now;
            carrierStatus('order truck missing · wait stream/rebind');
        }
        if (now - carrierBot.vehicleMissingAt < 5000)
            return true;
        var phase = carrierBot.phase;
        if (phase === 'park' ||
            phase === 'park_settle' ||
            phase === 'wait_park_ack' ||
            phase === 'npc_manual' ||
            phase === 'forklift_spam') {
            if (carrierBot.orderId)
                carrierBlacklistOrder(
                    carrierBot.orderId, 30000, 'vehicle missing');
            carrierReturnToFetch(
                'order truck missing', 1200);
            return true;
        }
        carrierBot.seatIssue = 'vehicleMissing';
        carrierBot.seatIssueAt = carrierBot.vehicleMissingAt;
        carrierBot.resumePhase = phase;
        carrierSetPhase(
            'rebind_vehicle',
            'order truck missing · rebind');
        return true;
    }

    function carrierWatchdog(now) {
        var phase = carrierBot.phase;
        var stalled =
            now - (carrierBot.lastProgressAt ||
                carrierBot.phaseEnteredAt || now);
        if ((phase === 'wait_load_yard' ||
             phase === 'to_yard') &&
            stalled >= 12000 &&
            now - (carrierBot.watchdogAt || 0) >= 8000 &&
            carrierBot.phaseRetries < 4) {
            try { mp.events.callRemote('carrier.app.connect'); }
            catch (e0) {}
            carrierBot.watchdogAt = now;
            carrierRetry(phase + ' · resync');
        }
        if ((phase === 'wait_load_yard' ||
             phase === 'to_yard') &&
            stalled >= 45000) {
            if (carrierBot.orderId)
                carrierBlacklistOrder(
                    carrierBot.orderId, 30000, phase);
            carrierReturnToFetch(phase + ' watchdog', 1200);
            return true;
        }
        if ((phase === 'npc_manual' ||
             phase === 'forklift_spam') &&
            stalled >= 75000) {
            if (carrierBot.orderId)
                carrierBlacklistOrder(
                    carrierBot.orderId, 30000, phase);
            carrierReturnToFetch(
                'loading watchdog', 1200);
            return true;
        }
        if (phase === 'enter_truck' && stalled >= 45000) {
            if (carrierBot.orderId)
                carrierBlacklistOrder(
                    carrierBot.orderId, 30000, phase);
            carrierReturnToFetch(
                'route/seat watchdog', 1200);
            return true;
        }
        if (phase === 'delivery_complete_wait' &&
            carrierProbeOrderEndFromModule(now))
            return true;
        if (phase === 'delivery_complete_wait' &&
            stalled >= 12000 &&
            now - (carrierBot.watchdogAt || 0) >= 8000 &&
            carrierBot.phaseRetries < 4) {
            try { mp.events.callRemote('carrier.app.connect'); }
            catch (e1) {}
            carrierBot.watchdogAt = now;
            carrierRetry('final ack · resync order state');
        }
        if (phase === 'delivery_complete_wait' &&
            stalled >= 45000) {
            var moduleRouteActive = false;
            try {
                var orderState =
                    mp.carrierOrder &&
                    mp.carrierOrder.state;
                moduleRouteActive =
                    !!orderState &&
                    orderState.routeVehId != null;
            } catch (eState) {}
            if (moduleRouteActive) {
                carrierBot.phaseRetries = 0;
                carrierBot.lastProgressAt = now;
                carrierBot.watchdogAt = now;
                carrierStatus(
                    'order.end pending · module route still active');
                return false;
            }
            carrierReturnToFetch(
                'order.end watchdog · no active module',
                1200);
            return true;
        }
        return false;
    }
)UAJS"
        R"UAJS(
    function carrierTick() {
        if (!carrierBot.on) return;
        var now = Date.now();
        carrierUpdateNearestForCurrent(now);
        var surfacePhase =
            carrierBot.phase === 'preflight' ||
            carrierBot.phase === 'need_employment' ||
            carrierBot.phase === 'need_truck' ||
            carrierBot.phase === 'park' ||
            carrierBot.phase === 'park_settle' ||
            carrierBot.phase === 'wait_park_ack' ||
            carrierBot.phase === 'npc_manual';
        if (carrierBot.waitingClear &&
            (!carrierBot.avoidPlayers || !surfacePhase))
            carrierResetPlayerWait(now, false);
        if (carrierWatchdog(now)) return;
        if (now < (carrierBot.busyUntil || 0)) return;

        if (carrierBot.phase === 'preflight' ||
            carrierBot.phase === 'need_employment' ||
            carrierBot.phase === 'need_truck') {
            if (carrierTryPrerequisites(now)) {
                carrierBot.prereqActionAt = 0;
                carrierBot.prereqActionRetries = 0;
                carrierSetPhase(
                    'fetch_orders',
                    'prerequisites ready · fetch orders');
                carrierBot.busyUntil = now + 250;
            }
            return;
        }

        if (carrierBot.phase === 'rebind_vehicle') {
            if (carrierTryReseat()) {
                carrierBot.seatIssue = '';
                carrierBot.seatIssueAt = 0;
                var resume =
                    carrierBot.resumePhase || 'fetch_orders';
                if (resume === 'wait_take' &&
                    carrierBot.orderId) {
                    try {
                        mp.events.callRemote(
                            'carrier.app.order.take',
                            carrierBot.orderId);
                    } catch (eRetryTake) {}
                    carrierBot.takeAt = now;
                    carrierSetPhase(
                        'wait_take',
                        'truck rebound · retry order take');
                } else {
                    carrierSetPhase(
                        resume,
                        'truck rebound/reseated');
                }
                carrierBot.busyUntil = now + 300;
                return;
            }
            if (now - (carrierBot.seatIssueAt || now) >= 12000) {
                if (carrierBot.orderId)
                    carrierBlacklistOrder(
                        carrierBot.orderId,
                        20000,
                        carrierBot.seatIssue || 'rebind');
                carrierResetOrderState();
                carrierSetPrerequisite('checking');
                carrierSetPhase(
                    'preflight',
                    'rebind timeout · Auto waiting for truck');
                carrierBot.busyUntil = now + 500;
                return;
            }
            carrierBot.busyUntil = now + 250;
            return;
        }

        if (carrierMonitorOrderTruck(now)) return;

        if (carrierBot.phase === 'fetch_orders') {
            if (!carrierTryPrerequisites(now)) return;
            carrierBot.orders.length = 0;
            carrierBot.fetchAt = now;
            try { mp.events.callRemote('carrier.app.connect'); }
            catch (e0) {}
            carrierSetPhase('wait_orders', 'fetch orders…');
            carrierBot.busyUntil = now + 250;
            return;
        }

        if (carrierBot.phase === 'wait_orders') {
            if (carrierBot.orders && carrierBot.orders.length) {
                carrierSetPhase('pick_take');
                return;
            }
            if (now - (carrierBot.fetchAt || now) > 5000) {
                carrierRetry('orders empty · retry');
                carrierSetPhase('fetch_orders');
                carrierBot.busyUntil = now + 800;
                return;
            }
            carrierBot.busyUntil = now + 500;
            return;
        }

        if (carrierBot.phase === 'pick_take') {
            if (!carrierTryReseat()) {
                carrierBot.seatIssue = 'pickSeat';
                carrierBot.seatIssueAt = now;
                carrierBot.resumePhase = 'pick_take';
                carrierSetPhase(
                    'rebind_vehicle',
                    'seat required before order');
                return;
            }
            var ord = carrierPickOrder();
            if (!ord) {
                carrierRetry(
                    'no fit orders cap=' + carrierCapacity());
                carrierSetPhase('fetch_orders');
                carrierBot.busyUntil = now + 1000;
                return;
            }
            carrierBot.orderId = Number(ord.id);
            carrierBot.orderWeight = Number(ord.weight) || 0;
            carrierBot.orderPrice = Number(
                ord.orderPrice != null
                    ? ord.orderPrice : ord.price) || 0;
            carrierBot.orderAccepted = false;
            carrierBot.takeAt = now;
            try {
                mp.events.callRemote(
                    'carrier.app.order.take',
                    carrierBot.orderId);
            } catch (eTake) {}
            carrierSetPhase(
                'wait_take',
                'take #' + carrierBot.orderId +
                ' w=' + carrierBot.orderWeight +
                ' $' + carrierBot.orderPrice);
            carrierBot.busyUntil = now + 600;
            return;
        }

        if (carrierBot.phase === 'wait_take') {
            if (carrierBot.orderAccepted) {
                carrierSetPhase('wait_load_yard');
                return;
            }
            if (now - (carrierBot.takeAt || now) > 6000) {
                if (carrierBot.orderId)
                    carrierBlacklistOrder(
                        carrierBot.orderId, 15000, 'take timeout');
                carrierBot.orderId = 0;
                carrierBot.capacityProbe = true;
                carrierRetry('take timeout');
                carrierSetPhase('pick_take');
                carrierBot.busyUntil = now + 500;
                return;
            }
            carrierBot.busyUntil = now + 200;
            return;
        }

        if (carrierBot.phase === 'wait_load_yard') {
            if (carrierBot.parkSlots && carrierBot.parkSlots.length) {
                carrierSetPhase('park');
                return;
            }
            if (carrierBot.loadYardPos) {
                carrierSetPhase('to_yard', 'yard position ready');
                return;
            }
            carrierBot.busyUntil = now + 200;
            return;
        }

        if (carrierBot.phase === 'to_yard') {
            if (!carrierTryReseat()) {
                carrierBot.seatIssue = 'yardSeat';
                carrierBot.seatIssueAt = now;
                carrierBot.resumePhase = 'to_yard';
                carrierSetPhase(
                    'rebind_vehicle',
                    'yard delivery seat · rebind');
                return;
            }
            if (carrierBot.loadYardPos) {
                if (!carrierTpVeh(carrierBot.loadYardPos, null)) {
                    carrierBot.seatIssue = 'yardVehicle';
                    carrierBot.seatIssueAt = now;
                    carrierBot.resumePhase = 'to_yard';
                    carrierSetPhase(
                        'rebind_vehicle',
                        'yard truck missing · rebind');
                    return;
                }
            }
            carrierBot.busyUntil = now + 300;
            if (carrierBot.parkSlots && carrierBot.parkSlots.length)
                carrierSetPhase('park', 'at yard · parking');
            return;
        }

)UAJS"
        R"UAJS(
        if (carrierBot.phase === 'park') {
            if (!carrierTryReseat()) {
                carrierBot.seatIssue = 'parkingSeat';
                carrierBot.seatIssueAt = now;
                carrierBot.resumePhase = 'park';
                carrierSetPhase(
                    'rebind_vehicle',
                    'parking seat · rebind');
                return;
            }
            var slot = carrierPickFreeSlot();
            if (!slot) {
                if (!carrierBot.noSlotAt) {
                    carrierBot.noSlotAt = now;
                    carrierStatus('wait free slot…');
                }
                if (now - carrierBot.noSlotAt >= 10000) {
                    carrierBot.triedSlots = {};
                    carrierBot.parkingRounds++;
                    carrierBot.parkingRetries++;
                    carrierBot.noSlotAt = now;
                    carrierRetry('parking slot round retry');
                    if (carrierBot.parkingRounds >= 3 ||
                        carrierBot.parkingRetries >= 12) {
                        if (carrierBot.orderId)
                            carrierBlacklistOrder(
                                carrierBot.orderId,
                                30000,
                                'no parking slot');
                        carrierReturnToFetch(
                            'parking slots watchdog',
                            1200);
                        return;
                    }
                }
                carrierBot.busyUntil = now + 1000;
                return;
            }
            carrierBot.noSlotAt = 0;
            if (carrierSurfaceBlocked(
                    slot.pos.x, slot.pos.y, now)) {
                if (!carrierCanBypassPlayerOnce()) {
                    carrierHandleBlockedParkingSlot(
                        slot, now);
                    return;
                }
            }
            carrierBot.parkPosIndex = slot.posIndex;
            if (!carrierTpVeh(slot.pos, slot.heading)) {
                carrierBot.seatIssue = 'parkingVehicle';
                carrierBot.seatIssueAt = now;
                carrierBot.resumePhase = 'park';
                carrierSetPhase(
                    'rebind_vehicle',
                    'parking truck missing · rebind');
                return;
            }
            carrierBot.parkAt = now;
            var slotClearance =
                Number(slot.clearance);
            carrierSetPhase(
                'park_settle',
                'park #' + slot.posIndex +
                (isFinite(slotClearance) &&
                 slotClearance >= 0
                    ? ' clear=' +
                        slotClearance.toFixed(1) + 'm'
                    : ' clear=stream-free'));
            carrierBot.busyUntil = now + 200;
            return;
        }

        if (carrierBot.phase === 'park_settle') {
            var slot2 = null;
            for (var si = 0; si < carrierBot.parkSlots.length; si++) {
                if (carrierBot.parkSlots[si].posIndex ===
                    carrierBot.parkPosIndex) {
                    slot2 = carrierBot.parkSlots[si];
                    break;
                }
            }
            if (!slot2) {
                carrierBot.parkingRetries++;
                carrierRetry('parking slot disappeared');
                carrierSetPhase('park');
                carrierBot.busyUntil = now + 250;
                return;
            }
            if (carrierSurfaceBlocked(
                    slot2.pos.x, slot2.pos.y, now)) {
                if (!carrierCanBypassPlayerOnce()) {
                    carrierHandleBlockedParkingSlot(
                        slot2, now);
                    return;
                }
            }
            if (slot2) {
                if (!carrierTpVeh(slot2.pos, slot2.heading)) {
                    carrierBot.seatIssue = 'parkingSettleVehicle';
                    carrierBot.seatIssueAt = now;
                    carrierBot.resumePhase = 'park';
                    carrierSetPhase(
                        'rebind_vehicle',
                        'parking settle truck missing');
                    return;
                }
                if (!carrierBot.truckParkPos)
                    carrierBot.truckParkPos =
                        { x: 0, y: 0, z: 0, h: 0 };
                carrierBot.truckParkPos.x = slot2.pos.x;
                carrierBot.truckParkPos.y = slot2.pos.y;
                carrierBot.truckParkPos.z = slot2.pos.z;
                carrierBot.truckParkPos.h = slot2.heading;
                carrierBot.truckUnderX = slot2.pos.x;
                carrierBot.truckUnderY = slot2.pos.y;
                carrierBot.truckUnderZ =
                    slot2.pos.z - CARRIER_UNDER_DEPTH;
            }
            if (now - (carrierBot.parkAt || now) < 400) {
                carrierBot.busyUntil = now + 200;
                return;
            }
            carrierLeaveVeh();
            // Guarded fallback if the normal vehicle-exit handler misses.
            carrierBot.parkEndDueAt = now + 300;
            carrierBot.parkEndSent = false;
            carrierSetPhase(
                'wait_park_ack',
                'left veh · wait parkingEnd');
            carrierBot.busyUntil = now + 400;
            return;
        }

        if (carrierBot.phase === 'wait_park_ack') {
            var parkRef = carrierBot.truckParkPos;
            var slot3 = null;
            for (var sj = 0;
                sj < carrierBot.parkSlots.length;
                sj++) {
                if (carrierBot.parkSlots[sj].posIndex ===
                    carrierBot.parkPosIndex) {
                    slot3 = carrierBot.parkSlots[sj];
                    break;
                }
            }
            if (parkRef && carrierSurfaceBlocked(
                    parkRef.x, parkRef.y, now)) {
                if (!carrierCanBypassPlayerOnce()) {
                    carrierFallbackSlot.posIndex =
                        carrierBot.parkPosIndex;
                    carrierHandleBlockedParkingSlot(
                        slot3 || carrierFallbackSlot,
                        now);
                    return;
                }
            }
            if (carrierBot.pedPos) {
                carrierSetPhase('npc_manual', 'logistic ped ready');
                return;
            }
            if (!carrierBot.parkEndSent &&
                now >= (carrierBot.parkEndDueAt || 0)) {
                try {
                    mp.events.callRemote(
                        'carrier.loading.parkingEnd',
                        carrierBot.parkPosIndex,
                        carrierBot.vehRemoteId);
                    carrierBot.parkEndSent = true;
                    carrierStatus('parkingEnd sent at exact slot');
                } catch (ePe) {}
            }
            if (now - (carrierBot.parkAt || now) > 8000) {
                carrierBot.triedSlots[
                    String(carrierBot.parkPosIndex)] = true;
                carrierBot.parkEndDueAt = 0;
                carrierBot.parkEndSent = false;
                carrierBot.parkingRetries++;
                carrierRetry('park ack timeout · retry');
                if (carrierBot.parkingRetries >= 12) {
                    if (carrierBot.orderId)
                        carrierBlacklistOrder(
                            carrierBot.orderId,
                            30000,
                            'park ack');
                    carrierReturnToFetch(
                        'parking ack retry cap',
                        1200);
                    return;
                }
                carrierSetPhase('park');
                carrierBot.busyUntil = now + 300;
                return;
            }
            carrierBot.busyUntil = now + 200;
            return;
        }

)UAJS"
        R"UAJS(
        if (carrierBot.phase === 'npc_manual') {
            if (carrierBot.pedPos && carrierSurfaceBlocked(
                    carrierBot.pedPos.x,
                    carrierBot.pedPos.y,
                    now))
                return;
            if (carrierBot.pedPos)
                carrierTpVeh(carrierBot.pedPos, carrierBot.pedPos.h);
            // walk TP player to ped
            try {
                var pp = mp.players.local;
                if (pp && carrierBot.pedPos) {
                    try { mp.events.call('ac:refreshtp'); } catch (eA) {}
                    pp.setCoordsNoOffset(
                        carrierBot.pedPos.x, carrierBot.pedPos.y,
                        carrierBot.pedPos.z, false, false, false);
                }
            } catch (eP) {}
            try {
                mp.events.callRemote(
                    'carrier.loading.startLoading');
            } catch (eStart) {}
            carrierBot.underStarted = false;
            carrierBot.underWaitAt = now;
            carrierSetPhase(
                'forklift_spam',
                'startLoading · bury under logistic bot');
            if (carrierBot.pedPos &&
                carrierGoUnderLoad(carrierBot.pedPos)) {
                carrierBot.underStarted = true;
                if (!carrierBot.spamId)
                    carrierStartSpam();
                carrierStatus(
                    'UNDER logistic bot · loading spam ON');
            }
            carrierBot.busyUntil = now + 150;
            return;
        }

        if (carrierBot.phase === 'forklift_spam') {
            // Wait for forklift seat (or timeout), then bury load
            if (!carrierBot.underStarted) {
                if (!carrierBot.underWaitAt)
                    carrierBot.underWaitAt = now;
                var inFork = false;
                try {
                    var lf = mp.players.local;
                    inFork = !!(lf && lf.vehicle &&
                        Number(lf.vehicle.remoteId) !==
                            Number(carrierBot.vehRemoteId));
                } catch (eF) {}
                if (inFork ||
                    now - carrierBot.underWaitAt > 3500) {
                    if (!carrierBot.underLoad)
                        carrierGoUnderLoad(
                            carrierBot.pedPos);
                    carrierBot.underStarted = true;
                } else {
                    carrierBot.busyUntil = now + 250;
                    return;
                }
            } else if (carrierBot.underLoad) {
                carrierHoldUnderLoad();
            }
            if (!carrierBot.spamId) carrierStartSpam();
            // safety timeout
            if (carrierBot.spamI > 600) {
                if (carrierBot.orderId)
                    carrierBlacklistOrder(
                        carrierBot.orderId,
                        30000,
                        'loading spam cap');
                carrierReturnToFetch(
                    'loading spam cap',
                    1200);
                return;
            }
            carrierBot.busyUntil = now + 400;
            return;
        }

        if (carrierBot.phase === 'enter_truck') {
            carrierStopSpam();
            if (!carrierTryReseat()) {
                if (now - (carrierBot.phaseEnteredAt || now) >=
                    12000) {
                    carrierBot.seatIssue = 'enterTruck';
                    carrierBot.seatIssueAt = now;
                    carrierBot.resumePhase = 'enter_truck';
                    carrierSetPhase(
                        'rebind_vehicle',
                        'truck seat timeout · rebind');
                } else {
                    carrierBot.busyUntil = now + 250;
                }
                return;
            }
            if (carrierBot.route && carrierBot.route.length) {
                carrierBot.deliveryAck = 'ready';
                carrierBot.deliveryIndex = -1;
                carrierSetPhase('deliver', 'deliver UNDER');
                carrierBot.busyUntil = now + 100;
                return;
            } else {
                // wait for route — stay buried where we are
                if (carrierBot.underLoad) {
                    carrierHoldUnderLoad();
                } else {
                    try {
                        var wv = carrierGetVeh(carrierActiveVehId());
                        var wp = wv ? wv.position
                            : (mp.players.local &&
                                mp.players.local.position);
                        if (wp) {
                            carrierRouteTarget.x = Number(wp.x);
                            carrierRouteTarget.y = Number(wp.y);
                            carrierRouteTarget.z = Number(wp.z);
                            carrierRouteTarget.h = null;
                            carrierBuryAt(
                                carrierRouteTarget, null);
                        }
                    } catch (eW) {}
                }
            }
            carrierBot.busyUntil = now + 400;
            return;
        }

        if (carrierBot.phase === 'deliver') {
            carrierDeliveryTick(now);
            return;
        }

        if (carrierBot.phase === 'delivery_resync') {
            carrierDeliveryResyncTick(now);
            return;
        }

        if (carrierBot.phase === 'delivery_stalled') {
            if (carrierBot.underLoad)
                carrierHoldUnderLoad();
            carrierProbeDeliveryModuleAck();
            carrierBot.busyUntil = now + 500;
            return;
        }

        if (carrierBot.phase === 'delivery_complete_wait') {
            if (carrierProbeOrderEndFromModule(now))
                return;
            if (carrierBot.underLoad)
                carrierHoldUnderLoad();
            carrierProbeDeliveryModuleAck();
            carrierBot.busyUntil = now + 200;
            return;
        }
    }

    function carrierBotStart() {
        jobBotsExclusive('carrier');
        carrierEnsureHooks();
        carrierEnsureDmgBlock();
        carrierResetOrderState();
        carrierBot.on = true;
        carrierBot.autoMode = true;
        carrierBot.suspended = false;
        carrierBot.spamMs = 100;
        carrierBot.clearDistance = 35;
        carrierBot.pauseDistance = 20;
        carrierBot.maxUnderOffset = 25;
        carrierBot.skipIllegal = true;
        carrierBot.maxWeight = -1;
        carrierBot.blacklist = {};
        carrierBot.orders.length = 0;
        carrierBot.retryCount = 0;
        carrierBot.phaseRetries = 0;
        carrierBot.lastServerAnswer = '-';
        carrierBot.deliveryAck = 'idle';
        carrierBot.busyUntil = 0;
        carrierBot.waitingClear = false;
        carrierBot.waitClearAt = 0;
        carrierBot.waitClearPhase = '';
        carrierBot.nearestPlayerId = -1;
        carrierBot.nearestPlayerDist = -1;
        carrierBot.nearestPlayerName = '';
        carrierBot.prereqActionAt = 0;
        carrierBot.prereqActionRetries = 0;
        carrierBot.badVehRemoteId = -1;
        carrierBot.ignoredRouteVehId = -1;
        carrierBot.phase = 'idle';
        carrierBot.phaseEnteredAt = 0;
        carrierBot.lastProgressAt = 0;
        carrierPlayerCount = 0;
        carrierNextPlayerScanAt = 0;
        carrierProbeModuleEvidence();
        carrierBot.prerequisite = 'checking';
        carrierSetPhase(
            'preflight',
            'AUTO ON · checking employment/truck');
        if (carrierBot.timerId != null) {
            try { clearInterval(carrierBot.timerId); } catch (eT) {}
        }
        carrierBot.timerId = setInterval(carrierTick, 250);
        try { carrierTick(); } catch (eTick) {}
        return 'ok:carrier_bot_start';
    }

    function carrierBotStop() {
        carrierBot.autoMode = false;
        carrierResetOrderState();
        carrierBot.on = false;
        carrierBot.suspended = false;
        carrierBot.prerequisite = 'off';
        carrierBot.nearestPlayerId = -1;
        carrierBot.nearestPlayerDist = -1;
        carrierBot.nearestPlayerName = '';
        carrierBot.badVehRemoteId = -1;
        carrierBot.vehicleMissingAt = 0;
        carrierBot.vehRemoteId = -1;
        carrierBot.boundVehModel = 0;
        if (carrierBot.timerId != null) {
            try { clearInterval(carrierBot.timerId); } catch (e) {}
            carrierBot.timerId = null;
        }
        carrierUnfreezeVeh();
        carrierSetPhase('idle', 'stopped · cleanup complete');
        return 'ok:carrier_bot_stop';
    }
)UAJS"
        R"UAJS(
    global.__uaGetStatus = function () {
        var dump = '';
        if (cefDumpInFlight > 0 || (cefDumpFiles && cefDumpFiles.length))
            dump = ' cef=' + cefDumpInFlight + '/' + cefDumpFiles.length;
        var inv = global.__uaInv || {};
        function numOr(v, d) {
            var n = Number(v);
            return isNaN(n) ? d : n;
        }
        var safeLast = String(woodmanBot.lastMsg || global.__uaLast || '')
            .replace(/[\t\r\n]/g, ' ');
        var busLast = String(busBot.lastMsg || '')
            .replace(/[\t\r\n]/g, ' ');
        var hmLast = String(handymanBot.lastMsg || '')
            .replace(/[\t\r\n]/g, ' ');
        var carLast = String(
            (typeof carrierBot !== 'undefined' && carrierBot
                ? carrierBot.lastMsg : '') || '')
            .replace(/[\t\r\n]/g, ' ');
        var carDeliveryAck = String(
            (typeof carrierBot !== 'undefined' && carrierBot
                ? carrierBot.deliveryAck : '') || 'idle');
        if (carDeliveryAck === 'waiting') {
            carDeliveryAck =
                'waiting #' + (carrierBot.deliveryIndex | 0) +
                ' r' + (carrierBot.deliveryRetries | 0) +
                '/' + CARRIER_DELIVERY_RETRY_MAX;
        }
        var step = handymanBot.queue[handymanBot.qi];
        var lines = [];
        lines.push(
            'S\t' + (global.__uaStatus || 'ready') + '\t' +
            String(global.__uaLast || '').replace(/[\t\r\n]/g, ' ') + dump);
        lines.push([
            'W',
            woodmanBot.on ? 1 : 0,
            String(woodmanBot.phase || 'idle').replace(/[\t\r\n]/g, '_'),
            woodmanBot.hitSlot | 0,
            woodmanBot.hitCountOnSlot | 0,
            numOr(inv.weight, -1),
            numOr(inv.maxWeight, -1),
            inv.axeHealth != null ? numOr(inv.axeHealth, -1) : -1,
            inv.axeSqlId != null ? numOr(inv.axeSqlId, -1) : -1,
            inv.woodCount != null ? numOr(inv.woodCount, -1) : -1,
            woodmanBot.overweight ? 1 : 0,
            safeLast
        ].join('\t'));
        busUpdatePerHour();
        lines.push([
            'B',
            busBot.on ? 1 : 0,
            String(busBot.phase || 'idle').replace(/[\t\r\n]/g, '_'),
            busBot.routeId | 0,
            busBot.markersSeenLap | 0,
            busBot.markersPerLap | 0,
            busBot.stopsSeenLap | 0,
            busBot.stopsPerLap | 0,
            busBot.calibrated ? 1 : 0,
            busBot.lapIndex | 0,
            busBot.spamCount | 0,
            busBot.spamLeft | 0,
            busBot.earnedSession | 0,
            busBot.currentIsStop ? 1 : 0,
            String(busBot.style || 'segment_skip').replace(/[\t\r\n]/g, '_'),
            busBot.speedId | 0,
            busBot.segIndex | 0,
            busBot.earnedLap | 0,
            busBot.lastLapEarned | 0,
            busBot.perHour | 0,
            busBot.underground ? 1 : 0,
            busBot.stopBursts.join(','),
            busLast
        ].join('\t'));
        lines.push([
            'H',
            handymanBot.on ? 1 : 0,
            String(handymanBot.phase || 'idle').replace(/[\t\r\n]/g, '_'),
            step ? String(step.kind).replace(/[\t\r\n]/g, '_') : '-',
            handymanBot.qi | 0,
            handymanBot.queue.length | 0,
            handymanBot.skimmerHp | 0,
            handymanBot.towelHp | 0,
            handymanBot.chlorineQty | 0,
            handymanBot.chlorineNeed | 0,
            String(handymanBot.taskSummary || '-').replace(/[\t\r\n]/g, ' '),
            hmLast
        ].join('\t'));
        try {
            carrierCapacity();
            lines.push([
                'C',
                (carrierBot && carrierBot.on) ? 1 : 0,
                String((carrierBot && carrierBot.phase) || 'idle')
                    .replace(/[\t\r\n]/g, '_'),
                (carrierBot && carrierBot.orderId) | 0,
                (carrierBot && carrierBot.orderWeight) | 0,
                (carrierBot && carrierBot.orderPrice) | 0,
                numOr(carrierCapacity(), -1),
                (carrierBot && carrierBot.maxWeight) | 0,
                (carrierBot && carrierBot.routeIndex) | 0,
                (carrierBot && carrierBot.route)
                    ? carrierBot.route.length : 0,
                (carrierBot && carrierBot.vehRemoteId) | 0,
                (carrierBot && carrierBot.parkPosIndex) | 0,
                carLast,
                (carrierBot && carrierBot.avoidPlayers) ? 1 : 0,
                numOr(carrierBot && carrierBot.clearDistance, 35),
                numOr(carrierBot && carrierBot.pauseDistance, 20),
                numOr(carrierBot && carrierBot.maxUnderOffset, 25),
                (carrierBot && carrierBot.nearestPlayerId) | 0,
                numOr(carrierBot && carrierBot.nearestPlayerDist, -1),
                String(
                    (carrierBot && carrierBot.nearestPlayerName) || '')
                    .replace(/[\t\r\n]/g, ' '),
                (carrierBot && carrierBot.waitingClear) ? 1 : 0,
                (carrierBot && carrierBot.autoMode) ? 1 : 0,
                String(
                    (carrierBot && carrierBot.prerequisite) || 'off')
                    .replace(/[\t\r\n]/g, '_'),
                carrierBot && carrierBot.on
                    ? Math.max(
                        0,
                        ((Date.now() -
                            (carrierBot.phaseEnteredAt ||
                                Date.now())) / 1000) | 0)
                    : 0,
                (carrierBot && carrierBot.retryCount) | 0,
                String(
                    (carrierBot && carrierBot.capacitySource) ||
                    'unknown').replace(/[\t\r\n]/g, '_'),
                String(
                    (carrierBot && carrierBot.lastServerAnswer) || '-')
                    .replace(/[\t\r\n]/g, ' '),
                String(
                    carDeliveryAck)
                    .replace(/[\t\r\n]/g, ' ')
            ].join('\t'));
        } catch (eC) {}
        lines.push([
            'A',
            adminDet.on ? 1 : 0,
            adminDet.onlineCount | 0,
            adminDet.nearbyCount | 0,
            adminDet.nearestId | 0,
            adminDet.nearestDist | 0,
            String(adminDet.nearestName || '').replace(/[\t\r\n]/g, ' '),
            adminDet.nearestLvl | 0,
            adminDet.spectating | 0
        ].join('\t'));
        var oList = adminDet.onlineList || [];
        for (var oi = 0; oi < oList.length && oi < 64; oi++) {
            var oa = oList[oi];
            lines.push([
                'O',
                oa.id | 0,
                String(oa.name || '').replace(/[\t\r\n]/g, ' '),
                oa.lvl | 0
            ].join('\t'));
        }
        var nList = adminDet.nearbyList || [];
        for (var ni = 0; ni < nList.length && ni < 32; ni++) {
            var na = nList[ni];
            lines.push([
                'N',
                na.id | 0,
                String(na.name || '').replace(/[\t\r\n]/g, ' '),
                na.lvl | 0,
                na.dist | 0,
                na.invis | 0,
                na.spect | 0
            ].join('\t'));
        }
        return lines.join('\n');
    };
)UAJS"
        R"UAJS(
    global.__uaCommand = function (command) {
        var cmd = String(command || '').trim();
        if (!cmd) return 'empty';

        // Crime
        if (cmd === 'crime_biz_money') return rem('bizStealing.get.money');
        if (cmd === 'crime_biz_npc_death') return rem('bizStealing.npc.death');
        if (cmd === 'crime_biz_aim_start') return rem('bizStealing.aiming.start');
        if (cmd.indexOf('crime_biz_aim_end:') === 0) {
            var nid = parseInt(cmd.substring('crime_biz_aim_end:'.length), 10);
            if (isNaN(nid) || nid < 0) nid = nearestPlayerId();
            if (nid < 0) return 'no target';
            return rem('bizStealing.aiming.end', nid);
        }

        // Jobs
        if (cmd === 'job_builder_finish') return rem('builder.point.finish');
        if (cmd === 'job_bus_checkpoint') return rem('busdriver.checkpoint.entered');
        if (cmd === 'job_incasation_finish') return rem('incasation.route.finish');
        if (cmd === 'job_steal_finish') return rem('stealing.route.finish');
        if (cmd === 'job_school_finish') return rem('driverSchool.routeFinish');
        if (cmd === 'job_graffiti_finish') return rem('graffitiWar.paint.finish');
        if (cmd === 'job_treasure_finish') return rem('treasureHunter.treasure.get.finish', localPos());
        if (cmd === 'job_handyman_ok') return rem('handyman.window.end', true);
        if (cmd === 'job_wcf_cleanup') return rem('server.wcf.tasks.action.finish', 'cleanUp');
        if (cmd === 'job_wcf_prepare') return rem('server.wcf.tasks.action.finish', 'prepareForMatch');
        if (cmd === 'woodman_bot_start') return woodmanBotStart();
        if (cmd === 'woodman_bot_stop') return woodmanBotStop();
        if (cmd === 'handyman_bot_start') return handymanBotStart();
        if (cmd === 'handyman_bot_stop') return handymanBotStop();
        if (cmd === 'handyman_inv_probe') {
            handymanEnsureHooks();
            handymanInvProbe();
            return 'ok:handyman_inv_probe';
        }
        if (cmd === 'bus_route_okrug') return busRouteOkrugStart();
        if (cmd === 'bus_bot_start') return busBotStart();
        if (cmd === 'bus_bot_stop') return busBotStop();
        if (cmd === 'bus_employ') return rem('busdriver.employment');
        if (cmd === 'bus_rent_accept') return rem('busdriver.rent.accept', 1);
        if (cmd.indexOf('bus_style:') === 0)
            return busSetStyle(cmd.substring('bus_style:'.length));
        if (cmd.indexOf('bus_speed:') === 0)
            return busSetSpeed(cmd.substring('bus_speed:'.length));
        if (cmd.indexOf('bus_stop_bursts:') === 0)
            return busSetStopBursts(cmd.substring('bus_stop_bursts:'.length));
        if (cmd === 'bus_park_under') {
            busEnsureHooks();
            return busParkUnderground() ? 'ok:bus_park' : 'err:park';
        }
        if (cmd === 'bus_unpark') {
            busUnpark();
            return 'ok:bus_unpark';
        }
        if (cmd.indexOf('bus_checkpoint_spam:') === 0) {
            var bn = parseInt(cmd.substring('bus_checkpoint_spam:'.length), 10) || 1;
            busEnsureHooks();
            for (var bi = 0; bi < bn && bi < 80; bi++) busFireEntered();
            return 'ok:bus_spam_' + bn;
        }
        if (cmd === 'admin_detector_start') return adminDetectorStart();
        if (cmd === 'admin_detector_stop') return adminDetectorStop();

        // Appearance (donate change-view flow)
        if (cmd === 'char_view_change') {
            try {
                mp.events.call('character.view.change.init');
                log('char view: init → remote character.view.change');
                return 'ok:char_view_change_init';
            } catch (e) {
                return rem('character.view.change');
            }
        }
        if (cmd === 'char_view_change_remote')
            return rem('character.view.change');
        if (cmd === 'char_view_cancel') {
            try {
                mp.events.call('character.view.change.cancel');
                return 'ok:char_view_cancel';
            } catch (e) {
                return rem('character.view.change.cancel');
            }
        }
        if (cmd === 'woodman_inv_probe') {
            uaInvProbe();
            var inv = global.__uaInv;
            if (!inv) {
                log('woodman inv: probing…');
                return 'ok:woodman_inv_probe pending';
            }
            var msg = 'w=' + inv.weight + '/' + inv.maxWeight +
                ' axe=' + inv.axeSqlId + ' hp=' + inv.axeHealth +
                ' wood=' + inv.woodCount +
                (inv.error ? ' err=' + inv.error : '');
            log('woodman inv: ' + msg);
            return 'ok:woodman_inv_probe ' + msg;
        }

        // Carrier
        if (cmd === 'carrier_auto_toggle')
            return carrierBot.autoMode
                ? carrierBotStop()
                : carrierBotStart();
        if (cmd.indexOf('carrier_auto_toggle:') === 0) {
            var autoValue = String(
                cmd.substring('carrier_auto_toggle:'.length))
                .toLowerCase();
            var autoEnabled =
                autoValue === '1' ||
                autoValue === 'true' ||
                autoValue === 'on';
            return autoEnabled
                ? carrierBotStart()
                : carrierBotStop();
        }
        if (cmd === 'carrier_bot_start') return carrierBotStart();
        if (cmd === 'carrier_bot_stop') return carrierBotStop();
        if (cmd === 'carrier_next') {
            if (!carrierBot.route.length ||
                carrierBot.routeIndex < 0 ||
                carrierBot.routeIndex >= carrierBot.route.length)
                return 'err:no_route_evidence';
            return rem('carrier.order.route.nextPointReached');
        }
        if (cmd === 'carrier_take_veh') {
            carrierProbeModuleEvidence();
            if (!carrierBot.inGarage)
                return 'err:no_garage_evidence';
            carrierBot.garageAction = 'take';
            carrierBot.prereqActionAt = 0;
            carrierBot.prereqActionRetries = 0;
            return carrierTryGarageAction('take', Date.now())
                ? 'ok:garage_take_evidence'
                : 'err:no_take_api_evidence';
        }
        if (cmd === 'carrier_attach') {
            if (carrierBot.phase !== 'forklift_spam')
                return 'err:no_loading_evidence';
            return rem('carrier.loading.attach');
        }
        if (cmd === 'carrier_detach') {
            if (carrierBot.phase !== 'forklift_spam')
                return 'err:no_loading_evidence';
            return rem('carrier.loading.detach');
        }
        if (cmd === 'carrier_start_loading') {
            if (!carrierBot.pedPos ||
                carrierBot.phase !== 'npc_manual')
                return 'err:no_logistic_evidence';
            return rem('carrier.loading.startLoading');
        }
        if (cmd.indexOf('carrier_max_weight:') === 0) {
            var mw = parseFloat(
                cmd.substring('carrier_max_weight:'.length));
            carrierBot.maxWeight =
                isNaN(mw) || mw <= 0 ? -1 : mw;
            carrierCapacity();
            return 'ok:max_weight=' + carrierBot.maxWeight;
        }
        if (cmd.indexOf('carrier_spam_ms:') === 0) {
            var sms = parseInt(
                cmd.substring('carrier_spam_ms:'.length), 10);
            carrierBot.spamMs = sms > 0 ? sms : 100;
            return 'ok:spam_ms=' + carrierBot.spamMs;
        }
        if (cmd.indexOf('carrier_skip_illegal:') === 0) {
            var sil = String(
                cmd.substring('carrier_skip_illegal:'.length))
                .toLowerCase();
            carrierBot.skipIllegal =
                sil === '1' || sil === 'true' || sil === 'on';
            return 'ok:skip_illegal=' +
                (carrierBot.skipIllegal ? 1 : 0);
        }
        if (cmd.indexOf('carrier_avoid_players:') === 0) {
            var av = String(
                cmd.substring('carrier_avoid_players:'.length))
                .toLowerCase();
            carrierBot.avoidPlayers =
                av === '1' || av === 'true' || av === 'on';
            if (!carrierBot.avoidPlayers) {
                var wasWaiting =
                    carrierBot.waitingClear;
                carrierResetPlayerWait(Date.now(), false);
                if (wasWaiting)
                    carrierBot.busyUntil = 0;
                carrierStatus(
                    'avoid players OFF · no clear waits');
            }
            return 'ok:avoid_players=' +
                (carrierBot.avoidPlayers ? 1 : 0);
        }
        if (cmd.indexOf('carrier_clear_distance:') === 0) {
            var cd = parseFloat(
                cmd.substring('carrier_clear_distance:'.length));
            if (isNaN(cd)) return 'err:clear_distance';
            carrierBot.clearDistance = Math.max(0, Math.min(100, cd));
            return 'ok:clear_distance=' + carrierBot.clearDistance;
        }
        if (cmd.indexOf('carrier_pause_distance:') === 0) {
            var pd = parseFloat(
                cmd.substring('carrier_pause_distance:'.length));
            if (isNaN(pd)) return 'err:pause_distance';
            carrierBot.pauseDistance = Math.max(0, Math.min(100, pd));
            return 'ok:pause_distance=' + carrierBot.pauseDistance;
        }
        if (cmd.indexOf('carrier_max_under_offset:') === 0) {
            var uo = parseFloat(
                cmd.substring('carrier_max_under_offset:'.length));
            if (isNaN(uo)) return 'err:max_under_offset';
            carrierBot.maxUnderOffset = Math.max(0, Math.min(25, uo));
            return 'ok:max_under_offset=' + carrierBot.maxUnderOffset;
        }
        if (cmd.indexOf('carrier_parking:') === 0) {
            var rest = cmd.substring('carrier_parking:'.length).split(':');
            var place = parseInt(rest[0], 10) || 0;
            var vehId = parseInt(rest[1], 10) || 0;
            var knownPlace = false;
            for (var cpi = 0;
                cpi < carrierBot.parkSlots.length;
                cpi++) {
                if (Number(carrierBot.parkSlots[cpi].posIndex) ===
                    Number(place)) {
                    knownPlace = true;
                    break;
                }
            }
            if (!knownPlace)
                return 'err:no_parking_evidence';
            if (Number(vehId) !==
                Number(carrierBot.vehRemoteId))
                return 'err:parking_vehicle_mismatch';
            return rem('carrier.loading.parkingEnd', place, vehId);
        }

        // Prizes
        if (cmd.indexOf('prize_container:') === 0) {
            var parts = cmd.substring('prize_container:'.length).split(':');
            var idx = parseInt(parts[0], 10) || 0;
            var sell = parts[1] === '1' || parts[1] === 'true';
            return rem('containers.prize.receive', idx, sell);
        }
        if (cmd === 'prize_container_sell_all') return rem('containers.prize.sellAll');
        if (cmd.indexOf('prize_bpass:') === 0) {
            var bp = cmd.substring('prize_bpass:'.length).split(':');
            var isDonate = bp[0] === '1' || bp[0] === 'true';
            var action = bp[1] || 'get';
            var prizeLvl = parseInt(bp[2], 10) || 0;
            var index = parseInt(bp[3], 10) || 0;
            return rem('bpass.prize.get', isDonate, action, prizeLvl, index);
        }
        if (cmd === 'prize_package') return rem('packageGame.prize.get');
        if (cmd === 'prize_lucky_finish') return rem('luckywheel.finishspin');
        if (cmd === 'prize_lucky_spin') return rem('luckywheel.spin');
        if (cmd === 'prize_wof') return rem('wheelOfFortune.getPrize');

        // AirDrop
        if (cmd === 'airdrop_interact') return rem('airDrop.interaction');
        if (cmd === 'airdrop_close') return rem('airDrop.closeMenu');
        if (cmd.indexOf('airdrop_get:') === 0) {
            var itemId = parseInt(cmd.substring('airdrop_get:'.length), 10);
            if (isNaN(itemId)) return 'bad itemId';
            return rem('airDrop.items.get', itemId);
        }

        // CEF dump
        if (cmd === 'dump_cef') return uaCefBeginDump('html');
        if (cmd === 'dump_cef_scripts') return uaCefBeginDump('scripts');
        if (cmd === 'dump_cef_all') return uaCefBeginDump('all');
        if (cmd === 'dump_cef_logic') return uaCefBeginDump('logic');
        if (cmd.indexOf('dump_cef_search:') === 0) {
            var term = cmd.substring('dump_cef_search:'.length);
            if (!term) return 'empty search';
            cefDumpBuffers = {};
            cefDumpFiles = [];
            cefDumpCursor = null;
            cefDumpInFlight = 0;
            cefDumpForceReady = false;
            var browsers = uaCefListBrowsers();
            uaCefStartNamedDump('search_result.json', function () {
                var ok = uaCefExecuteCallCefv(uaCefMakeSearchScript(term));
                if (!ok && browsers[0])
                    ok = uaCefExecuteOnBrowser(browsers[0].browser, uaCefMakeSearchScript(term));
                if (!ok) {
                    cefDumpInFlight = Math.max(0, cefDumpInFlight - 1);
                    uaCefStoreFile('search_result.json.error.txt', 'no CEF execute path');
                }
            });
            log('CEF search: ' + term);
            return 'ok:dump_cef_search';
        }

        // Raw: raw:eventName:jsonArgsArray
        if (cmd.indexOf('raw:') === 0) {
            var body = cmd.substring(4);
            var colon = body.indexOf(':');
            var ev = colon >= 0 ? body.substring(0, colon) : body;
            var argStr = colon >= 0 ? body.substring(colon + 1) : '[]';
            if (!ev) return 'no event';
            var a = parseArgsJson(argStr);
            var call = [ev].concat(a);
            return rem.apply(null, call);
        }

        return 'unknown:' + cmd;
    };

    try { setTimeout(adminDetectorStart, 1000); } catch (eAd) {}
    log('Ukraine Tools loaded');
    return 'UA_LOADED';
})();)UAJS";
}