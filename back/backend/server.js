require('dotenv').config();
const express = require('express');
const cors = require('cors');
const jwt = require('jsonwebtoken');
const crypto = require('crypto');
const bcrypt = require('bcrypt');
const helmet = require('helmet');
const rateLimit = require('express-rate-limit');
const Database = require('better-sqlite3');
const path = require('path');
const fs = require('fs');
const productDelivery = require('./product_delivery');

// ─── 9.1: session_key encryption at rest ───
// Master key из env — если не задан, сервер не стартует.
// Генерация: openssl rand -hex 32
const SESSION_KEY_MASTER = process.env.SESSION_KEY_MASTER_SECRET;
if (!SESSION_KEY_MASTER || SESSION_KEY_MASTER.length < 32) {
    console.error('[FATAL] SESSION_KEY_MASTER_SECRET not set or too short! Set at least 32 chars in .env');
    process.exit(1);
}

function encryptSessionKey(hexKey) {
    const iv = crypto.randomBytes(12);
    const key = crypto.scryptSync(SESSION_KEY_MASTER, 'shadow_skey_v1', 32);
    const cipher = crypto.createCipheriv('aes-256-gcm', key, iv);
    const ct = Buffer.concat([cipher.update(hexKey, 'utf8'), cipher.final()]);
    const tag = cipher.getAuthTag();
    // iv(12) + tag(16) + ct → base64
    return Buffer.concat([iv, tag, ct]).toString('base64');
}

function decryptSessionKey(blob) {
    try {
        const buf = Buffer.from(blob, 'base64');
        const iv = buf.slice(0, 12);
        const tag = buf.slice(12, 28);
        const ct = buf.slice(28);
        const key = crypto.scryptSync(SESSION_KEY_MASTER, 'shadow_skey_v1', 32);
        const decipher = crypto.createDecipheriv('aes-256-gcm', key, iv);
        decipher.setAuthTag(tag);
        return decipher.update(ct) + decipher.final('utf8');
    } catch { return null; }
}

// 8.1: security startup check
if (!process.env.JWT_SECRET || process.env.JWT_SECRET === 'REPLACE_WITH_RANDOM_64BYTES_HEX') {
    console.error('[FATAL] JWT_SECRET is not configured! Set a real secret in .env');
    process.exit(1);
}
if (!process.env.CLIENT_SHARED_SECRET || process.env.CLIENT_SHARED_SECRET === 'REPLACE_WITH_RANDOM_32BYTES_HEX') {
    console.error('[FATAL] CLIENT_SHARED_SECRET is not configured! Set a real secret in .env');
    process.exit(1);
}
if (!process.env.WATERMARK_SECRET || process.env.WATERMARK_SECRET.length < 32) {
    console.error('[FATAL] WATERMARK_SECRET must contain at least 32 characters');
    process.exit(1);
}
const RAGEMP_VARIANT_COUNT = Number.parseInt(
    process.env.RAGEMP_VARIANT_COUNT || '32',
    10);
if (!Number.isInteger(RAGEMP_VARIANT_COUNT) ||
    RAGEMP_VARIANT_COUNT < 2 ||
    RAGEMP_VARIANT_COUNT > 64) {
    console.error('[FATAL] RAGEMP_VARIANT_COUNT must be between 2 and 64');
    process.exit(1);
}

const app = express();
app.set('trust proxy', true); // Allow Express to read all X-Forwarded-For IPs (Cloudflare + Nginx)

// ─── Security Middleware ───
app.use(helmet({
    contentSecurityPolicy: {
        directives: {
            defaultSrc: ["'self'"],
            scriptSrc: ["'self'", "'unsafe-inline'"],
            styleSrc: ["'self'", "'unsafe-inline'", "https://fonts.googleapis.com"],
            fontSrc: ["'self'", "https://fonts.gstatic.com"],
            imgSrc: ["'self'", "data:"],
            connectSrc: ["'self'"],
            frameSrc: ["'none'"],
        }
    }
}));
app.use(cors({
    origin: (origin, callback) => {
        // Разрешаем: без origin (мобильные/Postman), shadowcheats.cc, www.shadowcheats.cc
        const allowed = [
            'https://shadowcheats.cc',
            'https://www.shadowcheats.cc',
            process.env.ALLOWED_ORIGIN
        ].filter(Boolean);
        if (!origin || allowed.includes(origin)) return callback(null, true);
        return callback(new Error('Not allowed by CORS'));
    },
    credentials: true,
    exposedHeaders: ['Content-Disposition']
}));
app.use(express.json({
    limit: '2mb',
    verify: (req, res, buf) => {
        req.rawBody = buf;
    }
}));

// ─── Serve Admin UI ───
app.use('/admin', express.static(path.join(__dirname, 'admin_static')));

// ─── SQLite Database ───
const databasePath = process.env.SHADOW_DB_PATH ||
    path.join(__dirname, 'shadow.db');
const db = new Database(databasePath);
db.pragma('journal_mode = DELETE');
db.pragma('foreign_keys = ON');

db.exec(`
CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE,
    password_hash TEXT    NOT NULL,
    hwid          TEXT,
    hwid_locked   INTEGER DEFAULT 0,
    banned        INTEGER DEFAULT 0,
    ban_reason    TEXT,
    banned_at     TEXT,
    is_admin      INTEGER DEFAULT 0,
    media_access  INTEGER DEFAULT 0,
    promocode     TEXT    DEFAULT '',
    promo_percent INTEGER DEFAULT 0,
    created_at    TEXT    DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS subscriptions (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES users(id),
    product_id TEXT    NOT NULL,
    days_left  INTEGER DEFAULT 0,
    revoked_at TEXT,
    revoke_reason TEXT,
    created_at TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS hwid_banlist (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    hwid       TEXT NOT NULL UNIQUE,
    reason     INTEGER,
    user_id    INTEGER,
    banned_at  TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS ip_banlist (
    ip         TEXT PRIMARY KEY,
    reason     TEXT,
    created_at TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS download_log (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER,
    product_id      TEXT,
    hwid            TEXT,
    loader_filename TEXT,
    integrity_hash  TEXT,
    customer_tag    TEXT,
    launch_id       TEXT,
    variant_id      INTEGER,
    ts              TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS audit_log (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    admin_id   INTEGER,
    action     TEXT,
    target_id  INTEGER,
    ip         TEXT,
    ts         TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS activation_keys (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    key_str    TEXT NOT NULL UNIQUE,
    product_id TEXT,
    days       INTEGER,
    used       INTEGER DEFAULT 0,
    used_by    INTEGER,
    used_at    TEXT,
    created_at TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS security_events (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER,
    hwid       TEXT,
    event_type TEXT,
    detail     TEXT,
    ip         TEXT,
    ts         TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS loader_sessions (
    user_id           INTEGER PRIMARY KEY,
    integrity_hash    TEXT,
    session_key_hex   TEXT,
    current_challenge TEXT,
    product_id        TEXT,
    customer_tag      TEXT,
    launch_id         TEXT,
    variant_id        INTEGER,
    last_heartbeat    TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS payments (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    order_id      TEXT    NOT NULL UNIQUE,
    user_id       INTEGER NOT NULL,
    amount_rub    INTEGER NOT NULL,
    method        TEXT    NOT NULL,
    status        TEXT    DEFAULT 'pending',
    invoice_id    TEXT,
    amount_usdt   REAL,
    rate_used     REAL,
    created_at    TEXT    DEFAULT (datetime('now')),
    completed_at  TEXT
);

CREATE TABLE IF NOT EXISTS user_configs (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id),
    name          TEXT    NOT NULL,
    data          TEXT    NOT NULL,
    created_at    TEXT    DEFAULT (datetime('now')),
    updated_at    TEXT    DEFAULT (datetime('now')),
    UNIQUE(user_id, name)
);

-- ─── LOADER VERSIONS: каждая сборка имеет уникальный numeric ID ───
-- version_token — уникальный случайный токен, вшитый в бинарник при сборке
-- revoked=1 означает что все лоадеры этой версии заблокированы
CREATE TABLE IF NOT EXISTS loader_versions (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    version_id    TEXT    NOT NULL UNIQUE,
    version_token TEXT    NOT NULL UNIQUE,
    label         TEXT    DEFAULT '',
    revoked       INTEGER DEFAULT 0,
    revoked_at    TEXT,
    revoked_by    INTEGER,
    created_at    TEXT    DEFAULT (datetime('now'))
);

-- ─── LOADER BINDINGS: привязка скачанного лоадера к HWID пользователя ───
-- session_uuid — вшитый в бинарник UUID (уникален для каждого скачивания)
-- При попытке войти с чужого PC (другой HWID) — блокируем и логируем
CREATE TABLE IF NOT EXISTS loader_bindings (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id),
    hwid          TEXT    NOT NULL,
    session_uuid  TEXT    NOT NULL UNIQUE,
    version_id    TEXT,
    revoked       INTEGER DEFAULT 0,
    created_at    TEXT    DEFAULT (datetime('now'))
);

-- ─── ACTIVITY LOG: все важные действия пользователей (расширенный аудит) ───
CREATE TABLE IF NOT EXISTS activity_log (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER,
    username        TEXT,
    action          TEXT    NOT NULL,
    detail          TEXT,
    hwid            TEXT,
    ip              TEXT,
    loader_version  TEXT,
    session_uuid    TEXT,
    ts              TEXT    DEFAULT (datetime('now'))
);

-- ─── REQUEST LOG: все HTTP запросы к серверу ───
CREATE TABLE IF NOT EXISTS request_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    method      TEXT,
    path        TEXT,
    ip          TEXT,
    user_id     INTEGER,
    username    TEXT,
    status      INTEGER,
    body_keys   TEXT,
    user_agent  TEXT,
    ts          TEXT DEFAULT (datetime('now'))
);

-- ─── LOADER ERROR LOG: ошибки лоадера отображаются в админ панели ───
CREATE TABLE IF NOT EXISTS loader_error_log (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER,
    username   TEXT,
    hwid       TEXT,
    phase      TEXT,
    message    TEXT    NOT NULL,
    loader_ver TEXT,
    ip         TEXT,
    ts         TEXT    DEFAULT (datetime('now'))
);

-- ─── CLOUD CONFIGS: пользовательские конфиги чита ───
CREATE TABLE IF NOT EXISTS user_configs (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id     INTEGER NOT NULL REFERENCES users(id),
    name        TEXT    NOT NULL,
    data        TEXT    NOT NULL,
    share_code  TEXT    UNIQUE,
    created_at  TEXT    DEFAULT (datetime('now')),
    updated_at  TEXT    DEFAULT (datetime('now')),
    UNIQUE(user_id, name)
);
`);

// ─── Migrate legacy tables: add new columns if missing ───
{
    const cols = db.prepare("PRAGMA table_info(loader_sessions)").all();
    const have = new Set(cols.map(c => c.name));
    if (!have.has('session_key_hex'))
        db.exec("ALTER TABLE loader_sessions ADD COLUMN session_key_hex TEXT");
    if (!have.has('current_challenge'))
        db.exec("ALTER TABLE loader_sessions ADD COLUMN current_challenge TEXT");
    if (!have.has('loader_session_uuid'))
        db.exec("ALTER TABLE loader_sessions ADD COLUMN loader_session_uuid TEXT");
    if (!have.has('product_id'))
        db.exec("ALTER TABLE loader_sessions ADD COLUMN product_id TEXT");
    if (!have.has('customer_tag'))
        db.exec("ALTER TABLE loader_sessions ADD COLUMN customer_tag TEXT");
    if (!have.has('launch_id'))
        db.exec("ALTER TABLE loader_sessions ADD COLUMN launch_id TEXT");
    if (!have.has('variant_id'))
        db.exec("ALTER TABLE loader_sessions ADD COLUMN variant_id INTEGER");
}
try { db.exec("ALTER TABLE download_log ADD COLUMN customer_tag TEXT"); } catch (e) { }
try { db.exec("ALTER TABLE download_log ADD COLUMN launch_id TEXT"); } catch (e) { }
try { db.exec("ALTER TABLE download_log ADD COLUMN variant_id INTEGER"); } catch (e) { }
try {
    db.exec(`CREATE TABLE IF NOT EXISTS feature_bundle_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id INTEGER NOT NULL,
        launch_id TEXT NOT NULL,
        variant_id INTEGER NOT NULL,
        feature_id TEXT NOT NULL,
        canary TEXT NOT NULL,
        ts TEXT DEFAULT (datetime('now'))
    )`);
} catch (e) {
    console.warn('[MIGRATION] feature_bundle_log:', e.message);
}
try { db.exec("ALTER TABLE loader_bindings ADD COLUMN ip TEXT"); } catch (e) { }
try { db.exec("ALTER TABLE activity_log ADD COLUMN loader_version TEXT"); } catch (e) { }
try { db.exec("ALTER TABLE activity_log ADD COLUMN session_uuid TEXT"); } catch (e) { }
try { db.exec("CREATE TABLE IF NOT EXISTS session_banlist (session_uuid TEXT PRIMARY KEY, reason TEXT, user_id INTEGER, created_at TEXT DEFAULT (datetime('now')))"); } catch (e) { }
try { db.exec("ALTER TABLE subscriptions ADD COLUMN is_frozen INTEGER DEFAULT 0"); } catch (e) { }

// ─── Loader Version helpers ───
// Версия хардкодится в лоадере, сервер не патчит бинарник.
// patchLoaderBinary больше не нужен, оставляем для обратной совместимости как no-op.
function patchLoaderBinary(raw) {
    return Buffer.from(raw);
}

// ─── Activity logging helper ───
function logActivity(user_id, username, action, detail, hwid, ip, loader_version, session_uuid) {
    try {
        db.prepare(`INSERT INTO activity_log (user_id, username, action, detail, hwid, ip, loader_version, session_uuid) VALUES (?,?,?,?,?,?,?,?)`)
            .run(user_id || null, username || null, action, detail || null, hwid || null, ip || null, loader_version || null, session_uuid || null);
    } catch (e) { /* non-critical */ }
}

// ─── Loader version middleware helper ───
// Проверяет X-Loader-Version заголовок: версия в базе, не отозвана.
// Сессии убраны — защита через HWID binding.
function checkLoaderVersion(req, res) {
    const verHeader = req.headers['x-loader-version'] || req.body?.loader_version;
    const requireHeaders = (process.env.REQUIRE_LOADER_HEADERS || '').toLowerCase() === 'true';

    // ── Режим жёсткой блокировки старых лоадеров ──
    if (requireHeaders && !verHeader) {
        const hwid = req.body?.hwid || null;
        try {
            db.prepare(`INSERT INTO security_events (hwid, event_type, detail, ip) VALUES (?,?,?,?)`)
                .run(hwid, 'OLD_LOADER_BLOCKED', `ver=none`, req.ip);
        } catch (e) { }
        console.log('[DEBUG] Blocked OLD_LOADER. Headers received:', req.headers);
        return { blocked: true, reason: 'Loader version not supported. Please download the new version.' };
    }

    // Проверяем версию если заголовок присутствует
    if (verHeader) {
        const verRow = db.prepare('SELECT * FROM loader_versions WHERE version_id=?').get(verHeader);
        if (!verRow) {
            db.prepare(`INSERT INTO security_events (hwid, event_type, detail, ip) VALUES (?,?,?,?)`)
                .run(req.body?.hwid || null, 'UNKNOWN_LOADER_VERSION', `ver=${verHeader}`, req.ip);
            return { blocked: true, reason: 'Outdated loader. Please download the new version.' };
        }
        if (verRow.revoked) {
            db.prepare(`INSERT INTO security_events (hwid, event_type, detail, ip) VALUES (?,?,?,?)`)
                .run(req.body?.hwid || null, 'REVOKED_LOADER', `ver=${verHeader}`, req.ip);
            return { blocked: true, reason: 'This loader version has been revoked. Please download the new version.' };
        }
    }

    return { blocked: false };
}

// ─── Loader integrity (anti-crack): сверка хэша .text лоадера ───
//   Клиент шлёт loader_hash (CRC .text) в /api/loader/download и /driver.
//   Ожидаемый хэш текущего релиза — в env LOADER_RELEASE_HASH (обновляется на
//   каждый билд). Не настроен → пропускаем (rollout). Не совпал → патченый/
//   крякнутый лоадер: security_events + бан, пейлоад не отдаём.
function checkLoaderHash(req) {
    const expected = (process.env.LOADER_RELEASE_HASH || '').trim();
    if (!expected) return { blocked: false }; // не настроено — пропускаем
    const got = (req.body && req.body.loader_hash) ? String(req.body.loader_hash).trim() : '';
    if (!got || got.toLowerCase() !== expected.toLowerCase()) {
        try {
            db.prepare(`INSERT INTO security_events (hwid, event_type, detail, ip) VALUES (?,?,?,?)`)
                .run(req.body?.hwid || null, 'LOADER_HASH_MISMATCH',
                     `got=${got || 'none'}`, req.ip);
        } catch (e) { }
        return { blocked: true, reason: 'Loader integrity check failed. Please download the official loader.' };
    }
    return { blocked: false };
}

// ─── HWID server-side derivation (#2) ───
//   Клиент присылает raw components separately; сервер хэширует в SHA-256.
//   Фейк-клиент не может подобрать HWID (например для обхода ban'а), потому что
//   не контролирует строку, по которой считается хэш.
//
//   Компоненты — уже pre-hashed SHA-256 на клиенте (см. hwid.hpp GetComponents),
//   так что индивидуальныеingerprint'ы в открытом виде не идут по проводу.
//   Сервер просто соединяет их канонично (fixed order, ':' separator).
function deriveHwid(components) {
    if (!components) return null;
    const canon = [
        components.cpu, components.disk, components.guid,
        components.mac, components.bios, components.profile
    ].join(':');
    return crypto.createHash('sha256').update(canon).digest('hex');
}

// Возвращает канон. строку компонентов — для HMAC signature.
function componentsCanonical(components) {
    return [
        components.cpu, components.disk, components.guid,
        components.mac, components.bios, components.profile
    ].join(':');
}

try { db.exec("ALTER TABLE users ADD COLUMN email TEXT DEFAULT ''"); } catch (e) { }
try { db.exec("ALTER TABLE users ADD COLUMN last_freeze_at TEXT DEFAULT NULL"); } catch (e) { }
try { db.exec("ALTER TABLE users ADD COLUMN balance INTEGER DEFAULT 0"); } catch (e) { }
try { db.exec("ALTER TABLE users ADD COLUMN last_freeze_at TEXT"); } catch (e) { }
try { db.exec("ALTER TABLE users ADD COLUMN is_distributor INTEGER DEFAULT 0"); } catch (e) { }
try { db.exec("ALTER TABLE users ADD COLUMN promo_earned INTEGER DEFAULT 0"); } catch (e) { }
try { db.exec("ALTER TABLE activation_keys ADD COLUMN distributor_id INTEGER"); } catch (e) { }

db.exec(`
CREATE TABLE IF NOT EXISTS promo_uses (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER,
    promocode  TEXT,
    amount_rub INTEGER,
    earned_rub INTEGER,
    ts         TEXT DEFAULT (datetime('now'))
);
`);

// ─── MIGRATION: Merge duplicate subscriptions ───
try {
    db.exec(`UPDATE subscriptions SET product_id = LOWER(product_id);`);
    const dupes = db.prepare(`
        SELECT user_id, product_id, COUNT(*) as c, SUM(days_left) as total_days, MIN(id) as keep_id
        FROM subscriptions
        GROUP BY user_id, product_id
        HAVING c > 1
    `).all();
    for (const d of dupes) {
        db.prepare(`UPDATE subscriptions SET days_left = ? WHERE id = ?`).run(d.total_days, d.keep_id);
        db.prepare(`DELETE FROM subscriptions WHERE user_id = ? AND product_id = ? AND id != ?`).run(d.user_id, d.product_id, d.keep_id);
        console.log(`[MIGRATION] Merged duplicate subs for user_id=${d.user_id}, product=${d.product_id} -> total ${d.total_days} days`);
    }
} catch (e) {
    console.error('[MIGRATION] Subscription merge error:', e.message);
}


// ─── MIGRATION: add product_id to user_configs ───
try { db.exec("ALTER TABLE user_configs ADD COLUMN product_id TEXT DEFAULT 'external'"); } catch (e) { }
try { db.exec("UPDATE user_configs SET product_id = 'external' WHERE product_id IS NULL OR product_id = ''"); } catch (e) { }

// ─── MIGRATION: drop old UNIQUE(user_id, name) and recreate with product_id ───
// SQLite не поддерживает DROP CONSTRAINT, поэтому пересоздаём таблицу если нет нового индекса
try {
    const configIdxInfo = db.prepare("SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='user_configs' AND name='idx_user_configs_user_product_name'").get();
    if (!configIdxInfo) {
        db.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_user_configs_user_product_name ON user_configs(user_id, product_id, name)");
    }
} catch (e) { console.warn('[MIGRATION] user_configs index:', e.message); }

// ─── CREATE TABLE: user_scripts (cloud scripts for ragemp) ───
try {
    db.exec(`CREATE TABLE IF NOT EXISTS user_scripts (
        id         INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id    INTEGER NOT NULL REFERENCES users(id),
        name       TEXT    NOT NULL,
        data       TEXT    NOT NULL,
        created_at TEXT    DEFAULT (datetime('now')),
        updated_at TEXT    DEFAULT (datetime('now')),
        UNIQUE(user_id, name)
    )`);
} catch (e) { }
// ─── INIT ADMIN USER ───
const ADMIN_USERNAME = process.env.ADMIN_USERNAME;
const ADMIN_PASSWORD = process.env.ADMIN_PASSWORD;
if (ADMIN_USERNAME && ADMIN_PASSWORD) {
    const hash = bcrypt.hashSync(ADMIN_PASSWORD, 12);
    const adminExists = db.prepare('SELECT id FROM users WHERE username = ?').get(ADMIN_USERNAME);
    if (!adminExists) {
        db.prepare(`INSERT INTO users (username, password_hash, is_admin, media_access) VALUES (?, ?, 1, 1)`).run(ADMIN_USERNAME, hash);
        console.log(`[SETUP] Admin user '${ADMIN_USERNAME}' created.`);
    } else {
        db.prepare(`UPDATE users SET password_hash = ? WHERE username = ?`).run(hash, ADMIN_USERNAME);
        console.log(`[SETUP] Admin user '${ADMIN_USERNAME}' password synced with .env`);
    }
} else {
    console.warn('[WARN] ADMIN_USERNAME/ADMIN_PASSWORD not set — skipping admin auto-creation.');
}

// ─── Helpers ───

function banUserAndRevoke(userId, reason, hwid) {
    const tx = db.transaction(() => {
        db.prepare(`UPDATE users SET banned=1, ban_reason=?, banned_at=datetime('now') WHERE id=?`).run(reason, userId);
        db.prepare(`UPDATE subscriptions SET days_left=0, revoked_at=datetime('now'), revoke_reason='SECURITY_BAN' WHERE user_id=?`).run(userId);
        if (hwid) {
            try {
                db.prepare(`INSERT OR IGNORE INTO hwid_banlist (hwid, reason, user_id) VALUES (?,?,?)`).run(hwid, reason, userId);
            } catch (e) { }
        }
        db.prepare(`INSERT INTO security_events (user_id, hwid, event_type, detail) VALUES (?,?,?,?)`).run(userId, hwid, 'BAN', reason);
        console.log(`[BAN] user_id=${userId} reason=${reason} hwid=${hwid}`);
    });
    tx();
}

function hmacSign(data, secret) {
    return crypto.createHmac('sha256', secret).update(data).digest('hex');
}

function verifyClientSignature(canonical, timestamp, signature) {
    const secret = process.env.CLIENT_SHARED_SECRET;
    // V6 fix: если секрет не задан — жёстко блокируем (раньше: return true без проверки).
    // Это позволяло выпустить сервер без CLIENT_SHARED_SECRET и любой клиент проходил.
    if (!secret) {
        console.error('[SECURITY] CLIENT_SHARED_SECRET not set! All requests blocked.');
        return false;
    }
    const expected = hmacSign(canonical + timestamp, secret);
    console.log(`[SIG_DEBUG] canon: ${canonical}`);
    console.log(`[SIG_DEBUG] ts: ${timestamp}`);
    console.log(`[SIG_DEBUG] expected: ${expected}`);
    console.log(`[SIG_DEBUG] received: ${signature}`);
    try {
        if (!signature) return false;
        return crypto.timingSafeEqual(Buffer.from(signature, 'hex'), Buffer.from(expected, 'hex'));
    } catch { return false; }
}

function generateSessionKey() {
    return crypto.randomBytes(32).toString('hex');
}

function loadPersonalizedProduct(productId, user, hwid) {
    if (!/^[a-z0-9_-]{1,32}$/.test(productId))
        throw new Error('Invalid product id');
    const variantId = productDelivery.deriveVariantId({
        userId: user.id,
        hwid,
        variantSecret: process.env.WATERMARK_SECRET,
        variantCount: RAGEMP_VARIANT_COUNT
    });
    const variantDirectory = variantId.toString().padStart(2, '0');
    const templatePath = path.join(
        __dirname, 'products', productId, 'variants',
        variantDirectory, 'cheat.dll');
    if (!fs.existsSync(templatePath))
        throw new Error('Protected product template not found');
    const customerTag = productDelivery.deriveCustomerTag(
        user.id, hwid, process.env.WATERMARK_SECRET);
    const template = fs.readFileSync(templatePath);
    return {
        artifact: productDelivery.personalizeArtifact(template, customerTag),
        customerTag,
        variantId
    };
}

function validateRagempVariantCatalog() {
    for (let variantId = 0;
         variantId < RAGEMP_VARIANT_COUNT;
         ++variantId) {
        const directory = variantId.toString().padStart(2, '0');
        const artifactPath = path.join(
            __dirname, 'products', 'ragemp', 'variants',
            directory, 'cheat.dll');
        if (!fs.existsSync(artifactPath))
            return false;
        const artifact = fs.readFileSync(artifactPath);
        if (artifact.indexOf(
                productDelivery.CUSTOMER_TAG_PLACEHOLDER) < 0)
            return false;
    }
    return true;
}

if (process.env.NODE_ENV === 'production' &&
    !validateRagempVariantCatalog()) {
    console.error('[FATAL] protected RAGE:MP variant catalog is incomplete');
    process.exit(1);
}

// Генерация уникального имени лоадера для пользователя
function generateLoaderFilename(userId, hwid) {
    return `VoiceChanger64(2.02).exe`;
}

// ─── IP blacklist ───
const bannedIPs = new Set();
app.use((req, res, next) => {
    if (bannedIPs.has(req.ip)) return res.status(403).json({ error: 'Forbidden' });
    next();
});

// ─── Global Request Logger ───
// Логируем каждый запрос к API в БД (кроме /api/ping чтобы не спамить)
try { db.exec(`CREATE TABLE IF NOT EXISTS request_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT, method TEXT, path TEXT, ip TEXT,
    user_id INTEGER, username TEXT, status INTEGER, body_keys TEXT, user_agent TEXT,
    ts TEXT DEFAULT (datetime('now')))`); } catch(e) {}

app.use((req, res, next) => {
    // Пропускаем ping и статику
    if (req.path === '/api/ping' || !req.path.startsWith('/api/')) return next();
    const startTime = Date.now();
    const origJson = res.json.bind(res);
    res.json = function(body) {
        try {
            const bodyKeys = req.body && typeof req.body === 'object' ? Object.keys(req.body).join(',') : '';
            const userId = req.user?.id || null;
            const username = req.user?.username || null;
            db.prepare(`INSERT INTO request_log (method, path, ip, user_id, username, status, body_keys, user_agent)
                VALUES (?,?,?,?,?,?,?,?)`).run(
                req.method, req.path, req.ip, userId, username, res.statusCode,
                bodyKeys.slice(0, 200),
                (req.headers['user-agent'] || '').slice(0, 150)
            );
        } catch(e) { /* non-critical */ }
        return origJson(body);
    };
    next();
});

// ─── Auth Middleware ───
// ИСПРАВЛЕНИЕ: убрали HWID-fallback — он позволял войти зная чужой HWID без пароля
function authenticateToken(req, res, next) {
    const auth = req.headers['authorization'];
    const token = auth && auth.startsWith('Bearer ') ? auth.slice(7) : (req.body?.token || req.query?.token || null);
    if (token) {
        try {
            const decoded = jwt.verify(token, process.env.JWT_SECRET);
            const user = db.prepare('SELECT * FROM users WHERE id=?').get(decoded.uid);
            if (user && !user.banned) {
                req.user = user;
                return next();
            }
        } catch { }
    }
    return res.status(401).json({ error: 'Unauthorized' });
}

// ─── Server-side product catalog (единственный источник правды) ───
// Цены берутся ТОЛЬКО отсюда, клиентский price игнорируется
const ALLOWED_PRODUCTS = {
    'external':       { 7: 500,  14: 800,  30: 1200 },
    'trigger':        { 7: 300,  14: 400,  30: 500  },
    'trigger_silent': { 7: 600,  14: 700,  30: 800  },
    'ragemp':         { 7: 499,  30: 999,  90: 1999 },
};

function requireAdmin(req, res, next) {
    if (!req.user.is_admin) return res.status(403).json({ error: 'Admin only' });
    next();
}

// Rate limiters
const loginLimiter = rateLimit({
    validate: { trustProxy: false },
    windowMs: 60_000, max: 10,
    validate: { trustProxy: false },
    handler: (req, res) => {
        // Логируем попытку брутфорса но НЕ баним IP навсегда
        // (постоянный бан вызывает проблемы с легитимными пользователями за NAT/VPN)
        db.prepare(`INSERT INTO security_events (hwid, event_type, detail, ip) VALUES (?,?,?,?)`)
            .run(null, 'BRUTE_FORCE', 'login_ratelimit', req.ip);
        console.log(`[RATELIMIT] IP ${req.ip} rate limited (brute force attempt)`);
        res.status(429).json({ error: 'Too many attempts. Please wait 1 minute.' });
    }
});

// downloadLimiter и heartbeatLimiter убраны — мешали повторному инжекту без ребута
const banLimiter = rateLimit({ validate: { trustProxy: false }, windowMs: 60_000, max: 10 });
const registerLimiter = rateLimit({
    validate: { trustProxy: false }, windowMs: 60_000, max: 5,
    message: { error: 'Too many registrations from this IP. Wait 1 minute.' }
});

const apiLimiter = rateLimit({
    validate: { trustProxy: false },
    windowMs: 60_000, // 1 minute
    max: 120, // 120 requests per IP per minute
    skip: (req) => String(req.originalUrl || '').startsWith('/api/radar/'),
    message: { error: 'Too many requests. Please try again later.' }
});
const radarPushLimiter = rateLimit({
    validate: { trustProxy: false },
    windowMs: 60_000,
    max: 400
});
const { createRadarStore } = require('./radar_store');
const radarStore = createRadarStore(15_000);

// Apply rate limiter to all /api/ routes
app.use('/api/', apiLimiter);

 
//  AUTH ROUTES
 
// ─── /api/ping — проверка соединения (нет авторизации) ───
app.get('/api/ping', (req, res) => res.json({ ok: true, ts: Date.now() }));

app.post('/api/auth/login', loginLimiter, async (req, res) => {
    const { username, password, components, timestamp, signature, isWeb } = req.body;

    if (isWeb) {
        if (!username || !password) return res.status(400).json({ error: 'Missing fields' });
        const requestIp = req.headers['x-forwarded-for']?.toString().split(',')[0] || req.ip || req.socket?.remoteAddress || '0.0.0.0';
        const ip_ban = db.prepare('SELECT * FROM ip_banlist WHERE ip=?').get(requestIp);
        if (ip_ban) return res.status(401).json({ error: 'Invalid credentials' });

        const user = db.prepare('SELECT * FROM users WHERE username=?').get(username);
        if (!user) return res.status(401).json({ error: 'Invalid credentials' });
        const valid = await bcrypt.compare(password, user.password_hash);
        if (!valid) return res.status(401).json({ error: 'Invalid credentials' });
        if (user.banned) return res.status(401).json({ error: 'Invalid credentials' });
        const token = jwt.sign({ uid: user.id }, process.env.JWT_SECRET, { expiresIn: '2h' });
        logActivity(user.id, user.username, 'WEB_LOGIN', null, null, req.ip);
        return res.json({
            token,
            user: { id: user.id, username: user.username, media: !!user.media_access }
        });
    }

    // ── Версия/сессия лоадера ──
    const loaderCheck = checkLoaderVersion(req, res);
    if (loaderCheck.blocked) {
        return res.status(403).json({ error: loaderCheck.reason });
    }

    if (!username || !password || !components || !timestamp || !signature)
        return res.status(400).json({ error: 'Missing fields' });
    if (!components.cpu || !components.disk || !components.guid ||
        !components.mac || !components.bios || !components.profile)
        return res.status(400).json({ error: 'Incomplete HWID components' });

    if (Date.now() - Number(timestamp) > 60_000)
        return res.status(400).json({ error: 'Request expired' });

    const hwid = deriveHwid(components);



    if (process.env.CLIENT_SHARED_SECRET) {
        const canon = componentsCanonical(components);
        if (!verifyClientSignature(canon, timestamp, signature)) {
            console.log(`[LOGIN_DEBUG] Client signature verification failed for ${username}`);
            return res.status(401).json({ error: 'Invalid credentials' });
        }
    }

    const ip_ban = db.prepare('SELECT * FROM ip_banlist WHERE ip=?').get(req.ip);
    if (ip_ban) {
        console.log(`[LOGIN_DEBUG] IP banned: ${req.ip}`);
        return res.status(401).json({ error: 'Invalid credentials' });
    }

    const hwid_ban = db.prepare('SELECT * FROM hwid_banlist WHERE hwid=?').get(hwid);
    if (hwid_ban) {
        console.log(`[LOGIN_DEBUG] HWID banned for ${username}, hwid: ${hwid}`);
        return res.status(401).json({ error: 'Invalid credentials' });
    }

    const user = db.prepare('SELECT * FROM users WHERE username=?').get(username);
    if (!user) {
        console.log(`[LOGIN_DEBUG] User not found: ${username}`);
        return res.status(401).json({ error: 'Invalid credentials' });
    }

    const valid = await bcrypt.compare(password, user.password_hash);
    if (!valid) {
        console.log(`[LOGIN_DEBUG] Invalid password for ${username}`);
        return res.status(401).json({ error: 'Invalid credentials' });
    }

    if (user.banned) {
        console.log(`[LOGIN_DEBUG] User is banned: ${username}`);
        db.prepare(`INSERT INTO security_events (user_id, hwid, event_type, detail, ip) VALUES (?,?,?,?,?)`)
            .run(user.id, hwid, 'LOGIN_BANNED', user.ban_reason, req.ip);
        return res.status(401).json({ error: 'Invalid credentials' });
    }

    if (user.hwid && user.hwid !== hwid) {
        console.log(`[LOGIN_DEBUG] HWID mismatch for ${username}. DB: ${user.hwid}, Req: ${hwid}`);
        return res.status(403).json({ error: 'HWID mismatch. Contact support.' });
    }
    if (!user.hwid)
        db.prepare('UPDATE users SET hwid=?, hwid_locked=1 WHERE id=?').run(hwid, user.id);

    // ── ИСПРАВЛЕНИЕ: дни только незамороженных подписок ──
    const allActiveSubs = db.prepare(
        `SELECT product_id, days_left FROM subscriptions
         WHERE user_id=? AND days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)`
    ).all(user.id);
    const days = allActiveSubs.reduce((acc, s) => acc + s.days_left, 0);

    // ── ИСПРАВЛЕНИЕ: блокировать вход при 0 днях ──
    if (days <= 0) {
        db.prepare(`INSERT INTO security_events (user_id, hwid, event_type, detail, ip) VALUES (?,?,?,?,?)`)
            .run(user.id, hwid, 'LOGIN_NO_SUB', `days=${days}`, req.ip);
        return res.status(403).json({ error: 'No active subscription' });
    }

    const token = jwt.sign({ uid: user.id }, process.env.JWT_SECRET, { expiresIn: '2h' });
    const session_key = generateSessionKey();
    const loader_filename = generateLoaderFilename(user.id, hwid);
    const verHeader = req.headers['x-loader-version'] || null;

    // Сохраняем session_key в loader_sessions — нужен для /api/loader/driver (trigger)
    try {
        const initial_challenge = crypto.randomBytes(16).toString('hex');
        db.prepare(`INSERT OR REPLACE INTO loader_sessions
            (user_id, integrity_hash, session_key_hex, current_challenge, last_heartbeat)
            VALUES (?,?,?,?,datetime('now'))`)
            .run(user.id, 'login_session', encryptSessionKey(session_key), initial_challenge);
    } catch (e) { console.warn('[LOGIN] loader_sessions save warn:', e.message); }

    logActivity(user.id, user.username, 'LOADER_LOGIN',
        `hwid=${hwid.slice(0, 16)} ver=${verHeader || 'unknown'}`,
        hwid, req.ip, verHeader, null);

    res.json({
        token,
        session_key,
        hwid,
        loader_filename,
        days,
        subscriptions: allActiveSubs.map(s => ({
            product_id: (s.product_id || '').toLowerCase(),
            days_left: s.days_left
        })),
        user: {
            id: user.id,
            username: user.username,
            media: !!user.media_access,
        }
    });
});

app.post('/api/auth/register', registerLimiter, async (req, res) => {
    const { username, password, email } = req.body;
    if (!username || !password || password.length < 8)
        return res.status(400).json({ error: 'Invalid data' });
    const usernameRegex = /^[a-zA-Z0-9_-]{3,20}$/;
    if (!usernameRegex.test(username)) {
        return res.status(400).json({ error: 'Имя пользователя должно состоять из 3-20 латинских букв, цифр или _ -' });
    }
    
    let safeEmail = '';
    if (email) {
        // XSS & Format Protection
        const emailRegex = /^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$/;
        if (!emailRegex.test(email) || email.length > 255) {
            return res.status(400).json({ error: 'Неверный формат email адреса.' });
        }
        safeEmail = email;
    }

    const hash = await bcrypt.hash(password, 12);
    try {
        db.prepare('INSERT INTO users (username, password_hash, email) VALUES (?,?,?)').run(username, hash, safeEmail);
        res.json({ ok: true });
    } catch (e) {
        res.status(409).json({ error: 'Username already exists' });
    }
});

// Активация ключа
app.post('/api/auth/activate', authenticateToken, (req, res) => {
    const { key } = req.body;
    if (!key) return res.status(400).json({ error: 'No key' });

    try {
        const k = db.prepare('SELECT * FROM activation_keys WHERE key_str=? AND used=0').get(key);
        if (!k) return res.status(400).json({ error: 'Invalid or already used key' });

        const tx = db.transaction(() => {
            db.prepare('UPDATE activation_keys SET used=1, used_by=?, used_at=datetime(\'now\') WHERE id=?').run(req.user.id, k.id);
            const pidLower = (k.product_id || '').toLowerCase();
            const hoursToAdd = k.days; // k.days is already in hours in the DB
            const existing = db.prepare('SELECT * FROM subscriptions WHERE user_id=? AND product_id COLLATE NOCASE=?').get(req.user.id, pidLower);
            if (existing) {
                db.prepare('UPDATE subscriptions SET days_left=days_left+? WHERE id=?').run(hoursToAdd, existing.id);
            } else {
                db.prepare('INSERT INTO subscriptions (user_id, product_id, days_left) VALUES (?,?,?)').run(req.user.id, pidLower, hoursToAdd);
            }
        });
        tx();
        logActivity(req.user.id, req.user.username, 'KEY_ACTIVATE', `key=${key} product=${k.product_id} days=${k.days}`, req.user.hwid, req.ip);
        res.json({ ok: true, product_id: k.product_id, days: k.days });
    } catch (e) {
        console.error('Activate error:', e);
        res.status(500).json({ error: 'Server error during activation' });
    }
});

 
//  USER ROUTES
 
app.get('/api/user/me', authenticateToken, (req, res) => {
    const rawSubs = db.prepare('SELECT * FROM subscriptions WHERE user_id=? AND days_left > 0').all(req.user.id);
    const subMap = {};
    for (const s of rawSubs) {
        const pid = (s.product_id || 'unknown').toLowerCase();
        if (!subMap[pid]) {
            subMap[pid] = { ...s, product_id: pid };
        } else {
            subMap[pid].days_left += s.days_left;
        }
    }
    const { password_hash, ...user } = req.user;
    const subsArray = Object.values(subMap).map(s => ({
        ...s,
        days_left: s.days_left
    }));
    res.json({ ...user, subscriptions: subsArray });
});

app.post('/api/user/me/password', authenticateToken, async (req, res) => {
    const { oldPassword, newPassword } = req.body;
    if (!oldPassword || !newPassword || newPassword.length < 8) return res.status(400).json({ error: 'Invalid data' });

    const valid = await bcrypt.compare(oldPassword, req.user.password_hash);
    if (!valid) return res.status(401).json({ error: 'Incorrect old password' });

    const hash = await bcrypt.hash(newPassword, 12);
    db.prepare('UPDATE users SET password_hash=? WHERE id=?').run(hash, req.user.id);
    logActivity(req.user.id, req.user.username, 'PASSWORD_CHANGE', 'User changed password', req.user.hwid, req.ip);
    res.json({ ok: true });
});

app.post('/api/user/me/freeze', authenticateToken, (req, res) => {
    const { product_id } = req.body;
    const pidLower = (product_id || '').toLowerCase();
    const sub = db.prepare('SELECT * FROM subscriptions WHERE user_id=? AND product_id COLLATE NOCASE=? AND days_left > 0').get(req.user.id, pidLower);
    if (!sub) return res.status(404).json({ error: 'Subscription not found or expired' });

    if (sub.frozen_at || sub.is_frozen) {
        return res.status(400).json({ error: 'Subscription is already frozen. Unfreeze it first.' });
    }

    if (req.user.last_freeze_at) {
        const lastFreeze = new Date(req.user.last_freeze_at).getTime();
        const now = Date.now();
        if (now - lastFreeze < 7 * 24 * 60 * 60 * 1000) {
            return res.status(400).json({ error: 'You can freeze a subscription only once a week' });
        }
    }

    db.prepare("UPDATE subscriptions SET frozen_at=datetime('now'), is_frozen=1 WHERE id=?").run(sub.id);
    db.prepare("UPDATE users SET last_freeze_at=datetime('now') WHERE id=?").run(req.user.id);
    logActivity(req.user.id, req.user.username, 'FREEZE_SUB', `product=${pidLower}`, req.user.hwid, req.ip);
    res.json({ ok: true, frozen: true });
});

app.post('/api/user/me/unfreeze', authenticateToken, (req, res) => {
    const { product_id } = req.body;
    const pidLower = (product_id || '').toLowerCase();
    const sub = db.prepare('SELECT * FROM subscriptions WHERE user_id=? AND product_id COLLATE NOCASE=?').get(req.user.id, pidLower);
    if (!sub) return res.status(404).json({ error: 'Subscription not found' });
    if (!sub.frozen_at && !sub.is_frozen) return res.status(400).json({ error: 'Subscription is not frozen' });
    if (sub.is_frozen === 2) return res.status(403).json({ error: 'Идет глобальное обновление. Разморозка недоступна.' });

    db.prepare('UPDATE subscriptions SET frozen_at=NULL, is_frozen=0 WHERE id=?').run(sub.id);
    logActivity(req.user.id, req.user.username, 'UNFREEZE_SUB', `product=${pidLower}`, req.user.hwid, req.ip);
    res.json({ ok: true, frozen: false });
});

app.post('/api/user/me/hwid-reset', authenticateToken, (req, res) => {
    if (req.user.balance < 100) return res.status(400).json({ error: 'Not enough balance. 100 rubles required.' });

    db.prepare('UPDATE users SET balance=balance-100, hwid=NULL, hwid_locked=0 WHERE id=?').run(req.user.id);
    if (req.user.hwid) db.prepare('DELETE FROM hwid_banlist WHERE hwid=?').run(req.user.hwid);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'USER_HWID_RESET', req.user.id, req.ip);
    logActivity(req.user.id, req.user.username, 'HWID_RESET', 'user self-reset', req.user.hwid, req.ip);
    res.json({ ok: true });
});

app.post('/api/user/me/topup', authenticateToken, async (req, res) => {
    const { amount, method } = req.body;
    const amt = parseInt(amount);
    // ИСПРАВЛЕНИЕ: отрицательные суммы и превышение лимита заблокированы
    if (!amt || amt < 100) return res.status(400).json({ error: 'Minimum amount is 100 rubles' });
    if (amt > 100000) return res.status(400).json({ error: 'Maximum topup is 100,000 rubles' });

    const orderId = `ORDER_${req.user.id}_${Date.now()}`;

    try {
        if (method === 'СБП') {
            const MERCHANT_ID = process.env.PLATEGA_MERCHANT_ID;
            const SECRET_KEY = process.env.PLATEGA_SECRET_KEY;
            if (!MERCHANT_ID || !SECRET_KEY)
                return res.status(500).json({ error: 'Platega not configured' });

            const response = await fetch('https://app.platega.io/transaction/process', {
                method: 'POST',
                headers: {
                    'X-MerchantId': MERCHANT_ID,
                    'X-Secret': SECRET_KEY,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    paymentMethod: 2,
                    paymentDetails: { amount: parseFloat(amt), currency: 'RUB' },
                    description: 'Пополнение баланса shadowcheats.cc',
                    return: `${process.env.SITE_URL || 'https://shadowcheats.cc'}/dashboard`,
                    failedUrl: `${process.env.SITE_URL || 'https://shadowcheats.cc'}/dashboard`,
                    payload: `${req.user.id}_${orderId}`
                })
            });
            const data = await response.json();
            if (!response.ok || (!data.redirect && !data.transactionId))
                return res.status(502).json({ error: data.error || 'Platega error' });

            // Сохраняем pending платёж
            db.prepare(`INSERT INTO payments (order_id, user_id, amount_rub, method, invoice_id)
                        VALUES (?,?,?,?,?)`)
                .run(orderId, req.user.id, amt, 'platega', data.transactionId || null);

            return res.json({ ok: true, url: data.redirect, invoice_id: data.transactionId });
        }
        else if (method === 'CRYPTOBOT') {
            const CRYPTO_TOKEN = process.env.CRYPTOBOT_API_KEY;
            if (!CRYPTO_TOKEN)
                return res.status(500).json({ error: 'CryptoBot not configured' });

            // Получаем актуальный курс USDT/RUB
            let rate = 77.19;
            try {
                const rateRes = await fetch('https://pay.crypt.bot/api/getExchangeRates', {
                    headers: { 'Crypto-Pay-API-Token': CRYPTO_TOKEN }
                });
                const rateData = await rateRes.json();
                if (rateData.ok) {
                    const r = rateData.result.find(x => x.source === 'USDT' && x.target === 'RUB');
                    if (r) rate = parseFloat(r.rate);
                }
            } catch (e) { /* используем дефолтный курс */ }

            const amountUsdt = (amt / rate).toFixed(2);

            const response = await fetch('https://pay.crypt.bot/api/createInvoice', {
                method: 'POST',
                headers: {
                    'Crypto-Pay-API-Token': CRYPTO_TOKEN,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    asset: 'USDT',
                    amount: amountUsdt,
                    description: `Пополнение баланса shadowcheats.cc на ${amt} ₽`,
                    hidden_message: 'Спасибо! Баланс будет зачислен автоматически.',
                    paid_btn_name: 'callback',
                    paid_btn_url: `${process.env.SITE_URL || 'https://shadowcheats.cc'}/dashboard`,
                    payload: `${req.user.id}_${orderId}`
                })
            });
            const data = await response.json();
            if (!data.ok)
                return res.status(502).json({ error: data.error?.name || 'CryptoBot error' });

            const invoice = data.result;
            db.prepare(`INSERT INTO payments (order_id, user_id, amount_rub, method, invoice_id, amount_usdt, rate_used)
                        VALUES (?,?,?,?,?,?,?)`)
                .run(orderId, req.user.id, amt, 'cryptobot', String(invoice.invoice_id), parseFloat(amountUsdt), rate);

            return res.json({ ok: true, url: invoice.pay_url, invoice_id: invoice.invoice_id });
        }
        else {
            return res.status(400).json({ error: 'Unknown payment method' });
        }
    } catch (e) {
        console.error('Topup error:', e);
        res.status(500).json({ error: 'Ошибка создания платежа' });
    }
});

app.post('/api/user/me/promo', authenticateToken, (req, res) => {
    const { code } = req.body;
    if (!code) return res.status(400).json({ error: 'Empty promo code' });

    const mediaUser = db.prepare('SELECT id, promo_percent FROM users WHERE promocode COLLATE NOCASE = ? AND media_access = 1').get(code);
    if (mediaUser && mediaUser.promo_percent > 0) {
        res.json({ ok: true, message: `Скидка ${mediaUser.promo_percent}% применена` });
    } else {
        res.status(400).json({ error: 'Неверный или неактивный промокод' });
    }
});

app.post('/api/admin/users/:id/balance', authenticateToken, requireAdmin, (req, res) => {
    const { balance } = req.body;
    db.prepare('UPDATE users SET balance=? WHERE id=?').run(parseInt(balance) || 0, req.params.id);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'UPDATE_BALANCE', req.params.id, req.ip);
    res.json({ ok: true });
});

app.post('/api/user/me/buy', authenticateToken, (req, res) => {
    const { product_id, days, promocode } = req.body;
    if (!product_id || !days) return res.status(400).json({ error: 'Missing fields' });

    const daysInt = parseInt(days);
    if (!daysInt || daysInt <= 0) return res.status(400).json({ error: 'Invalid days' });

    // ─── ИСПРАВЛЕНИЕ: цена берётся ТОЛЬКО с сервера, клиентский price игнорируется ───
    // Это закрывает уязвимость с отрицательной ценой и подменой product_id
    const pidLower = (product_id || '').toLowerCase();

    let actualPrice;
    if (pidLower === 'hwid_reset') {
        actualPrice = 100; // HWID reset всегда стоит 100р
    } else {
        const productPrices = ALLOWED_PRODUCTS[pidLower];
        if (!productPrices) {
            // Попытка купить несуществующий продукт — логируем как подозрительное
            db.prepare(`INSERT INTO security_events (user_id, hwid, event_type, detail, ip) VALUES (?,?,?,?,?)`)
                .run(req.user.id, req.user.hwid, 'INVALID_PRODUCT_BUY', `product=${product_id} days=${days}`, req.ip);
            return res.status(400).json({ error: 'Invalid product' });
        }
        actualPrice = productPrices[daysInt];
        if (!actualPrice) {
            return res.status(400).json({ error: 'Invalid duration for this product' });
        }
    }

    const user = db.prepare('SELECT * FROM users WHERE id = ?').get(req.user.id);
    if (user.balance < actualPrice) {
        return res.status(400).json({ error: 'INSUFFICIENT_FUNDS' });
    }

    try {
        db.prepare('BEGIN').run();

        // 1. Списываем баланс по СЕРВЕРНОЙ цене
        db.prepare('UPDATE users SET balance = balance - ? WHERE id = ?').run(actualPrice, req.user.id);

        // 2. Начисляем % медиа-партнеру если есть промокод
        if (promocode) {
            const mediaUser = db.prepare('SELECT id, promo_percent FROM users WHERE promocode COLLATE NOCASE = ? AND media_access = 1').get(promocode);
            if (mediaUser && mediaUser.promo_percent > 0) {
                const earned = Math.floor((actualPrice * mediaUser.promo_percent) / 100);
                if (earned > 0) {
                    db.prepare('UPDATE users SET promo_earned = promo_earned + ? WHERE id = ?').run(earned, mediaUser.id);
                    db.prepare('INSERT INTO promo_uses (user_id, promocode, amount_rub, earned_rub) VALUES (?, ?, ?, ?)')
                        .run(req.user.id, promocode.toUpperCase(), actualPrice, earned);
                }
            }
        }

        // 3. HWID reset
        if (pidLower === 'hwid_reset') {
            if (!user.hwid || user.hwid === 'NOT_BOUND') {
                db.prepare('ROLLBACK').run();
                return res.status(400).json({ error: 'Ваш HWID не привязан, сброс не требуется' });
            }
            db.prepare('UPDATE users SET hwid=NULL, hwid_locked=0 WHERE id=?').run(req.user.id);
            if (user.hwid) db.prepare('DELETE FROM hwid_banlist WHERE hwid=?').run(user.hwid);
            db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'USER_HWID_RESET_BOUGHT', req.user.id, req.ip);
        } else {
            const hoursToAdd = daysInt * 24;
            const existingSub = db.prepare('SELECT id, days_left FROM subscriptions WHERE user_id = ? AND product_id COLLATE NOCASE = ?').get(req.user.id, pidLower);
            if (existingSub) {
                db.prepare('UPDATE subscriptions SET days_left = days_left + ? WHERE id = ?').run(hoursToAdd, existingSub.id);
            } else {
                db.prepare('INSERT INTO subscriptions (user_id, product_id, days_left) VALUES (?, ?, ?)').run(req.user.id, pidLower, hoursToAdd);
            }
        }

        db.prepare('COMMIT').run();
        logActivity(req.user.id, req.user.username, 'BUY', `product=${pidLower} days=${daysInt} price=${actualPrice}`, req.user.hwid, req.ip);
        res.json({ ok: true });
    } catch (e) {
        db.prepare('ROLLBACK').run();
        console.error('Buy error:', e);
        res.status(500).json({ error: 'Ошибка покупки' });
    }
});

 
//  WEBHOOKS ДЛЯ ОПЛАТЫ
 

// Вебхук от Платеги
app.post('/api/payments/platega/webhook', (req, res) => {
    try {
        const rawBuf = req.rawBody || Buffer.from(JSON.stringify(req.body || {}));
        const data = typeof req.body === 'object' && req.body !== null ? req.body : JSON.parse(rawBuf.toString('utf8'));
        const merchantId = req.headers['x-merchantid'] || data.merchantId;
        const secret = req.headers['x-secret'] || data.secret;

        // Проверяем что вебхук пришёл от Platega
        if (merchantId !== process.env.PLATEGA_MERCHANT_ID ||
            secret !== process.env.PLATEGA_SECRET_KEY) {
            console.warn('[PLATEGA WEBHOOK] Invalid credentials');
            return res.sendStatus(403);
        }

        const transactionId = data.id || data.transactionId;
        const status = data.status;

        console.log(`[PLATEGA WEBHOOK] txId=${transactionId} status=${status}`);

        if ((status === 'CONFIRMED' || status === 'SUCCESS' || status === 'PAID') && transactionId) {
            // Ищем pending платёж по invoice_id
            const payment = db.prepare(`SELECT * FROM payments WHERE invoice_id=? AND status='pending' AND method='platega'`)
                .get(String(transactionId));
            if (payment) {
                const tx = db.transaction(() => {
                    db.prepare(`UPDATE payments SET status='completed', completed_at=datetime('now') WHERE id=?`).run(payment.id);
                    db.prepare(`UPDATE users SET balance=balance+? WHERE id=?`).run(payment.amount_rub, payment.user_id);
                });
                tx();
                console.log(`[PLATEGA] Credited ${payment.amount_rub} RUB to user_id=${payment.user_id}`);
            }
        }
        res.send('OK');
    } catch (e) {
        console.error('[PLATEGA WEBHOOK] Error:', e);
        res.send('OK'); // Всегда OK чтобы Platega не ретраила
    }
});

// Вебхук от CryptoBot
app.post('/api/payments/cryptobot/webhook', (req, res) => {
    try {
        const rawBuf = req.rawBody || Buffer.from(JSON.stringify(req.body || {}));
        const update = typeof req.body === 'object' && req.body !== null ? req.body : JSON.parse(rawBuf.toString('utf8'));

        // Проверяем подпись CryptoBot
        const signature = req.headers['crypto-pay-api-signature'];
        const token = process.env.CRYPTOBOT_API_KEY || '';
        const secretKey = crypto.createHash('sha256').update(token).digest();
        const expected = crypto.createHmac('sha256', secretKey).update(rawBuf).digest('hex');
        if (signature !== expected) {
            console.warn('[CRYPTOBOT WEBHOOK] Invalid signature');
            return res.sendStatus(403);
        }

        console.log(`[CRYPTOBOT WEBHOOK] update_type=${update.update_type}`);

        if (update.update_type === 'invoice_paid') {
            const invoice = update.payload;
            const invoiceId = String(invoice.invoice_id);

            const payment = db.prepare(`SELECT * FROM payments WHERE invoice_id=? AND status='pending' AND method='cryptobot'`)
                .get(invoiceId);
            if (payment) {
                const tx = db.transaction(() => {
                    db.prepare(`UPDATE payments SET status='completed', completed_at=datetime('now') WHERE id=?`).run(payment.id);
                    db.prepare(`UPDATE users SET balance=balance+? WHERE id=?`).run(payment.amount_rub, payment.user_id);
                });
                tx();
                console.log(`[CRYPTOBOT] Credited ${payment.amount_rub} RUB to user_id=${payment.user_id}`);
            } else {
                console.warn(`[CRYPTOBOT] Payment not found for invoice_id=${invoiceId}`);
            }
        }
        res.send('OK');
    } catch (e) {
        console.error('[CRYPTOBOT WEBHOOK] Error:', e);
        res.send('OK');
    }
});

// Проверка статуса платежа Platega (для фронта — polling)
app.get('/api/payments/platega/status/:invoiceId', authenticateToken, async (req, res) => {
    try {
        const { invoiceId } = req.params;
        const payment = db.prepare(`SELECT * FROM payments WHERE invoice_id=? AND user_id=?`)
            .get(invoiceId, req.user.id);
        if (!payment) return res.status(404).json({ error: 'Not found' });
        if (payment.status === 'completed')
            return res.json({ status: 'completed', balance: db.prepare('SELECT balance FROM users WHERE id=?').get(req.user.id).balance });

        // Активно проверяем у Platega
        const r = await fetch(`https://app.platega.io/transaction/${invoiceId}`, {
            headers: {
                'X-MerchantId': process.env.PLATEGA_MERCHANT_ID,
                'X-Secret': process.env.PLATEGA_SECRET_KEY
            }
        });
        const data = await r.json();
        if (data.status === 'CONFIRMED' && payment.status === 'pending') {
            const tx = db.transaction(() => {
                db.prepare(`UPDATE payments SET status='completed', completed_at=datetime('now') WHERE id=?`).run(payment.id);
                db.prepare(`UPDATE users SET balance=balance+? WHERE id=?`).run(payment.amount_rub, payment.user_id);
            });
            tx();
            const newBalance = db.prepare('SELECT balance FROM users WHERE id=?').get(req.user.id).balance;
            return res.json({ status: 'completed', balance: newBalance });
        }
        res.json({ status: payment.status, platega_status: data.status });
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});


 
//  LOADER ROUTES
 

// ─── Инициализация сессии без скачивания чита (для trigger) ───
app.post('/api/loader/session', authenticateToken, async (req, res) => {
    const { product_id, hwid, timestamp } = req.body;

    const loaderCheck = checkLoaderVersion(req, res);
    if (loaderCheck.blocked) return res.status(403).json({ error: loaderCheck.reason });

    if (!product_id || !hwid || !timestamp)
        return res.status(400).json({ error: 'Missing fields' });
    if (Date.now() - Number(timestamp) > 120_000)
        return res.status(400).json({ error: 'Request expired' });
    if (req.user.hwid !== hwid)
        return res.status(403).json({ error: 'HWID mismatch' });

    let check_product_id = product_id;
    if (product_id === 'trigger') {
        const hasSilent = db.prepare(`SELECT id FROM subscriptions WHERE user_id=? AND product_id='trigger_silent' AND days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)`).get(req.user.id);
        if (hasSilent) check_product_id = 'trigger_silent';
    }

    const sub = db.prepare(`SELECT * FROM subscriptions WHERE user_id=? AND product_id=? AND days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)`).get(req.user.id, check_product_id);
    if (!sub) return res.status(403).json({ error: 'No active subscription or subscription is frozen' });

    const key = crypto.randomBytes(32);
    const initial_challenge = crypto.randomBytes(16).toString('hex');

    const tx = db.transaction(() => {
        db.prepare(`INSERT OR REPLACE INTO loader_sessions
            (user_id, integrity_hash, session_key_hex, current_challenge, last_heartbeat)
            VALUES (?,?,?,?,datetime('now'))`).run(
            req.user.id, 'trigger_no_hash', encryptSessionKey(key.toString('hex')), initial_challenge
        );
    });
    tx();

    res.json({ session_key: key.toString('hex') });
});

// POST /api/loader/download — скачать зашифрованный payload (per-user мутации)
app.post('/api/loader/download', authenticateToken, async (req, res) => {
    const { product_id, hwid, components, timestamp, signature } = req.body;
    if (!product_id || !hwid) return res.status(400).json({ error: 'Missing fields' });

    // ── Версия лоадера ──
    const loaderCheck = checkLoaderVersion(req, res);
    if (loaderCheck.blocked) return res.status(403).json({ error: loaderCheck.reason });

    // ── Целостность лоадера (anti-crack): патченый/крякнутый лоадер не получает пейлоад ──
    const hashCheck = checkLoaderHash(req);
    if (hashCheck.blocked) {
        banUserAndRevoke(req.user.id, 'LOADER_HASH_MISMATCH', hwid);
        return res.status(403).json({ error: hashCheck.reason });
    }

    if (Date.now() - Number(timestamp) > 60_000) return res.status(400).json({ error: 'Expired' });

    if (!components || deriveHwid(components) !== hwid) {
        banUserAndRevoke(req.user.id, 'HWID_DERIVATION_FAIL', hwid);
        return res.status(403).json({ error: 'HWID components mismatch' });
    }

    if (process.env.CLIENT_SHARED_SECRET) {
        const canon = componentsCanonical(components);
        if (!verifyClientSignature(canon, timestamp, signature))
            return res.status(401).json({ error: 'Invalid signature' });
    }

    if (req.user.hwid !== hwid) {
        banUserAndRevoke(req.user.id, 'HWID_MISMATCH_DOWNLOAD', hwid);
        return res.status(403).json({ error: 'HWID mismatch' });
    }

    // ── ИСПРАВЛЕНИЕ: заморозка блокирует inject ──
    let check_product_id = product_id;
    if (product_id === 'trigger') {
        const hasSilent = db.prepare(`SELECT id FROM subscriptions WHERE user_id=? AND product_id='trigger_silent' AND days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)`).get(req.user.id);
        if (hasSilent) check_product_id = 'trigger_silent';
    }

    const sub = db.prepare(`SELECT * FROM subscriptions WHERE user_id=? AND product_id=? AND days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)`).get(req.user.id, check_product_id);
    if (!sub) return res.status(403).json({ error: 'No active subscription or subscription is frozen' });

    let raw;
    let customerTag = null;
    let variantId = null;
    if (product_id === 'trigger' || product_id === 'trigger_silent') {
        // Для триггера DLL не нужна, выдаем пустышку чисто для session_key
        raw = Buffer.from('trigger_no_dll_required_payload');
    } else if (product_id === 'ragemp') {
        try {
            const personalized = loadPersonalizedProduct(
                product_id, req.user, hwid);
            raw = personalized.artifact;
            customerTag = personalized.customerTag;
            variantId = personalized.variantId;
        } catch (error) {
            console.error('[DELIVERY]', error.message);
            return res.status(404).json({ error: 'Protected product not found' });
        }
    } else {
        const binaryPath = path.join(
            __dirname, 'products', product_id, 'cheat.dll');
        if (!fs.existsSync(binaryPath))
            return res.status(404).json({ error: 'Product not found' });
        raw = fs.readFileSync(binaryPath);
    }

    const integrity_hash = productDelivery.hashArtifact(raw);

    const key = crypto.randomBytes(32);
    const iv = crypto.randomBytes(12);
    const cipher = crypto.createCipheriv('aes-256-gcm', key, iv);
    const ct = Buffer.concat([cipher.update(raw), cipher.final()]);
    const tag = cipher.getAuthTag();
    const payload = Buffer.concat([iv, ct, tag]);

    const loader_filename = generateLoaderFilename(req.user.id, hwid);

    // #1: первичный challenge для heartbeat challenge-response.
    //   Сервер сохраняет session_key (АES-GCM ключ шифрования payload — клиент
    //   использует его же для HMAC) и random 16-байт nonce. Клиент при первом
    //   heartbeat шлёт challenge_response = HMAC-SHA256(session_key, nonce).
    const initial_challenge = crypto.randomBytes(16).toString('hex');
    const launchId = crypto.randomUUID();
    const launchTicket = customerTag
        ? productDelivery.issueLaunchTicket({
            userId: req.user.id,
            productId: product_id,
            hwid,
            customerTag,
            integrityHash: integrity_hash,
            launchId,
            variantId,
            jwtSecret: process.env.JWT_SECRET
        })
        : null;

    const tx = db.transaction(() => {
        db.prepare(`INSERT INTO download_log
            (user_id, product_id, hwid, loader_filename, integrity_hash,
             customer_tag, launch_id, variant_id) VALUES (?,?,?,?,?,?,?,?)`).run(
            req.user.id, product_id, hwid, loader_filename, integrity_hash,
            customerTag, launchId, variantId
        );
        // 9.1 fix: session_key_hex хранится зашифрованным через AES-256-GCM
        db.prepare(`INSERT OR REPLACE INTO loader_sessions
            (user_id, integrity_hash, session_key_hex, current_challenge,
             product_id, customer_tag, launch_id, variant_id, last_heartbeat)
            VALUES (?,?,?,?,?,?,?,?,datetime('now'))`).run(
            req.user.id, integrity_hash, encryptSessionKey(key.toString('hex')),
            initial_challenge, product_id, customerTag, launchId, variantId
        );
    });
    tx();

    res.json({
        payload: payload.toString('base64'),
        session_key: key.toString('hex'),
        integrity_hash,
        loader_filename,
        customer_tag: customerTag,
        launch_ticket: launchTicket,
        variant_id: variantId === null ? null : String(variantId),
    });
});

// Временные токены для нативного скачивания лоадера браузером
// Map: token -> { userId, username, hwid, filename, expiresAt }
const loaderDownloadTokens = new Map();

// POST /api/loader/request_download — выдать одноразовый токен (60 сек)
app.post('/api/loader/request_download', authenticateToken, (req, res) => {
    const hasActiveUnfrozen = db.prepare(
        'SELECT id FROM subscriptions WHERE user_id = ? AND days_left > 0 AND frozen_at IS NULL AND is_frozen = 0 LIMIT 1'
    ).get(req.user.id);
    if (!hasActiveUnfrozen) return res.status(403).json({ error: 'Нет активных подписок или подписка заморожена' });

    const loader_path = path.join(__dirname, 'products', 'loader.exe');
    if (!fs.existsSync(loader_path)) return res.status(404).json({ error: 'Loader not found' });

    const currentVer = db.prepare('SELECT * FROM loader_versions WHERE revoked=0 ORDER BY id DESC LIMIT 1').get();
    if (!currentVer) return res.status(500).json({ error: 'No active loader version configured. Contact admin.' });

    const token = crypto.randomBytes(32).toString('hex');
    const filename = generateLoaderFilename(req.user.id, req.user.hwid);
    loaderDownloadTokens.set(token, {
        userId: req.user.id,
        username: req.user.username,
        hwid: req.user.hwid,
        filename,
        versionId: currentVer.version_id,
        expiresAt: Date.now() + 60_000 // 60 секунд
    });

    // Логируем заранее
    logActivity(req.user.id, req.user.username, 'LOADER_DOWNLOAD',
        `ver=${currentVer.version_id} hwid=${req.user.hwid ? req.user.hwid.slice(0, 16) : 'unknown'}`,
        req.user.hwid, req.ip, currentVer.version_id, null);

    res.json({ token });
});

// GET /api/loader/download_loader/:token — нативное скачивание браузером без JS fetch
app.get('/api/loader/download_loader/:token', (req, res) => {
    const info = loaderDownloadTokens.get(req.params.token);
    if (!info) return res.status(403).json({ error: 'Invalid or expired token' });
    if (Date.now() > info.expiresAt) {
        loaderDownloadTokens.delete(req.params.token);
        return res.status(403).json({ error: 'Download token expired' });
    }
    // Одноразовый — удаляем сразу
    loaderDownloadTokens.delete(req.params.token);

    const loader_path = path.join(__dirname, 'products', 'loader.exe');
    if (!fs.existsSync(loader_path)) return res.status(404).send('Loader not found');

    const stat = fs.statSync(loader_path);
    res.setHeader('Content-Disposition', `attachment; filename="${info.filename}"`);
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Length', stat.size);

    const fileStream = fs.createReadStream(loader_path);
    fileStream.on('error', (err) => {
        console.error('[LOADER_DOWNLOAD] Stream error:', err);
        if (!res.headersSent) res.status(500).send('Read error');
    });
    fileStream.pipe(res);
});

// POST /api/loader/download_loader — старый эндпоинт, оставлен для обратной совместимости
app.post('/api/loader/download_loader', authenticateToken, (req, res) => {
    const hasActiveUnfrozen = db.prepare(
        'SELECT id FROM subscriptions WHERE user_id = ? AND days_left > 0 AND frozen_at IS NULL AND is_frozen = 0 LIMIT 1'
    ).get(req.user.id);
    if (!hasActiveUnfrozen) return res.status(403).json({ error: 'Нет активных подписок или подписка заморожена' });

    const loader_path = path.join(__dirname, 'products', 'loader.exe');
    if (!fs.existsSync(loader_path)) return res.status(404).json({ error: 'Loader not found' });

    const currentVer = db.prepare('SELECT * FROM loader_versions WHERE revoked=0 ORDER BY id DESC LIMIT 1').get();
    if (!currentVer) return res.status(500).json({ error: 'No active loader version configured. Contact admin.' });

    logActivity(req.user.id, req.user.username, 'LOADER_DOWNLOAD',
        `ver=${currentVer.version_id} hwid=${req.user.hwid ? req.user.hwid.slice(0, 16) : 'unknown'}`,
        req.user.hwid, req.ip, currentVer.version_id, null);

    const filename = generateLoaderFilename(req.user.id, req.user.hwid);
    const stat = fs.statSync(loader_path);
    res.setHeader('Content-Disposition', `attachment; filename="${filename}"`);
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Length', stat.size);

    const fileStream = fs.createReadStream(loader_path);
    fileStream.on('error', (err) => {
        if (!res.headersSent) res.status(500).json({ error: 'Loader read error' });
    });
    fileStream.pipe(res);
});

// POST /api/loader/heartbeat — challenge-response + integrity hash
// #1: heartbeat теперь подписан HMAC-SHA256(session_key, current_challenge).
//   Фейк-серверу на localhost нечем подписать ответ: он не знает session_key
//   (генерируется на /download только один раз и не возвращается повторно).
//   Протокол:
//     1) Download отдаёт клиенту session_key + записывает в БД начальный
//        current_challenge.
//     2) Клиент при первом heartbeat шлёт пустой challenge_response —
//        сервер выдаёт ему текущий challenge в ответе, next_check_in.
//     3) На следующийheartbeat клиент sh Lloyd шлёт
//        challenge_response = HMAC-SHA256(session_key, prev_challenge).hex
//     4) Сервер сравнивает (timingSafe), если честно — генерит новый
//        challenge и отдаёт его в этом же ответе. Если отличное — бан INTEGRITY_FAIL.
//     5) Без успешной ack продвижения heartbeat'ы остаются на одном значении
//        → фейк-сервер не может "жить" вечно, ни разу не ответив правильно.
app.post('/api/loader/heartbeat', authenticateToken, (req, res) => {
    const { hwid, product_id, integrity_hash, challenge_response } = req.body;

    // ── Версия/сессия лоадера ──
    const loaderCheck = checkLoaderVersion(req, res);
    if (loaderCheck.blocked) return res.status(403).json({ error: loaderCheck.reason });

    if (req.user.hwid !== hwid) {
        banUserAndRevoke(req.user.id, 'HWID_MISMATCH_HEARTBEAT', hwid);
        return res.status(403).json({ error: 'HWID changed' });
    }

    // ── ИСПРАВЛЕНИЕ: заморозка блокирует heartbeat ──
    if (product_id) {
        let check_product_id = product_id;
        if (product_id === 'trigger') {
            const hasSilent = db.prepare(
                `SELECT id FROM subscriptions WHERE user_id=? AND product_id='trigger_silent' AND days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)`
            ).get(req.user.id);
            if (hasSilent) check_product_id = 'trigger_silent';
        }
        const sub = db.prepare(
            `SELECT * FROM subscriptions WHERE user_id=? AND product_id=? AND days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)`
        ).get(req.user.id, check_product_id);
        if (!sub) {
            db.prepare(`INSERT INTO security_events (user_id, hwid, event_type, detail, ip) VALUES (?,?,?,?,?)`)
                .run(req.user.id, hwid, 'SUB_EXPIRED_OR_FROZEN_HEARTBEAT', product_id, req.ip);
            return res.status(403).json({ error: 'Subscription expired or frozen' });
        }
    }

    const session = db.prepare('SELECT * FROM loader_sessions WHERE user_id=?').get(req.user.id);
    if (!session) return res.status(403).json({ error: 'No active session' });

    // 9.2 fix: проверяем что сессия не устарела (макс 5 минут без heartbeat)
    const SESSION_EXPIRE_MS = 5 * 60 * 1000;
    const last_hb = new Date(session.last_heartbeat + ' UTC').getTime();
    if (Date.now() - last_hb > SESSION_EXPIRE_MS) {
        db.prepare(`INSERT INTO security_events (user_id, hwid, event_type, detail, ip) VALUES (?,?,?,?,?)`)
            .run(req.user.id, hwid, 'SESSION_TIMEOUT', `last_hb=${session.last_heartbeat}`, req.ip);
        db.prepare('DELETE FROM loader_sessions WHERE user_id=?').run(req.user.id);
        return res.status(403).json({ error: 'Session expired. Please re-launch.' });
    }

    if (!integrity_hash || integrity_hash !== session.integrity_hash) {
        banUserAndRevoke(req.user.id, 'INTEGRITY_FAIL', hwid);
        db.prepare(`INSERT INTO security_events (user_id, hwid, event_type, detail) VALUES (?,?,?,?)`).run(
            req.user.id, hwid, 'INTEGRITY_FAIL',
            `expected=${session.integrity_hash?.substring(0, 16)}... got=${integrity_hash?.substring(0, 16)}...`
        );
        return res.status(403).json({ error: 'Integrity check failed' });
    }

    // 9.1 fix: расшифровываем session_key_hex из БД перед использованием
    const session_key_hex = session.session_key_hex ? decryptSessionKey(session.session_key_hex) : null;
    if (!session_key_hex) {
        console.error(`[SECURITY] Failed to decrypt session_key for user_id=${req.user.id}`);
        return res.status(500).json({ error: 'Session error' });
    }

    // ── #1 Challenge-Response verification ──
    const first_heartbeat = !challenge_response;
    if (!first_heartbeat && session_key_hex && session.current_challenge) {
        const expected = crypto.createHmac('sha256', session_key_hex)
            .update(session.current_challenge).digest('hex');
        let ok = false;
        try {
            ok = challenge_response.length === expected.length &&
                crypto.timingSafeEqual(Buffer.from(challenge_response, 'hex'),
                    Buffer.from(expected, 'hex'));
        } catch { ok = false; }
        if (!ok) {
            banUserAndRevoke(req.user.id, 'HEARTBEAT_CHALLENGE_FAIL', hwid);
            db.prepare(`INSERT INTO security_events (user_id, hwid, event_type, detail) VALUES (?,?,?,?)`).run(
                req.user.id, hwid, 'CHALLENGE_FAIL', 'heartbeat hmac mismatch'
            );
            return res.status(403).json({ error: 'Challenge failed' });
        }
    }

    // ── Ротация challenge на каждый heartbeat ──
    const new_challenge = crypto.randomBytes(16).toString('hex');
    db.prepare(`UPDATE loader_sessions
                SET last_heartbeat=datetime('now'), current_challenge=? WHERE user_id=?`)
        .run(new_challenge, req.user.id);

    // Подписываем новый challenge с помощью session_key, чтобы защитить от WinHTTP MITM (фейковых ответов "ok":true)
    const signature = crypto.createHmac('sha256', session_key_hex).update(new_challenge).digest('hex');

    res.json({
        ok: true,
        next_check_in: 30,
        challenge: new_challenge,
        signature: signature
    });
});

 
//  /api/loader/driver — скачать зашифрованный kernel driver (fix 2.1)
//
//  Драйвер больше НЕ вшивается в лоадер (IDR_DRIVER убран).
//  Клиент скачивает зашифрованный driver.sys напрямую в память
//  и маппит через in-memory KDMapper (без записи на диск).
//
//  Шифрование: тот же AES-256-GCM + session_key что и для чита.
//  Доступ: только авторизованный пользователь с активной подпиской.
 
app.post('/api/loader/driver', authenticateToken, async (req, res) => {
    const { product_id, hwid, timestamp } = req.body;

    // ── Версия лоадера ──
    const loaderCheck = checkLoaderVersion(req, res);
    if (loaderCheck.blocked) return res.status(403).json({ error: loaderCheck.reason });

    // ── Целостность лоадера (anti-crack) ──
    const hashCheck = checkLoaderHash(req);
    if (hashCheck.blocked) {
        banUserAndRevoke(req.user.id, 'LOADER_HASH_MISMATCH', hwid);
        return res.status(403).json({ error: hashCheck.reason });
    }

    if (!product_id || !hwid || !timestamp)
        return res.status(400).json({ error: 'Missing fields' });
    if (Date.now() - Number(timestamp) > 120_000)
        return res.status(400).json({ error: 'Request expired' });
    if (req.user.hwid !== hwid)
        return res.status(403).json({ error: 'HWID mismatch' });

    // ── ИСПРАВЛЕНИЕ: заморозка блокирует скачивание драйвера ──
    // Для trigger: принимаем как trigger так и trigger_silent подписку
    let check_product_id = product_id;
    if (product_id === 'trigger') {
        const hasSilent = db.prepare(
            `SELECT id FROM subscriptions WHERE user_id=? AND product_id='trigger_silent' AND days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)`
        ).get(req.user.id);
        if (hasSilent) check_product_id = 'trigger_silent';
    }
    const sub = db.prepare(
        `SELECT * FROM subscriptions WHERE user_id=? AND product_id=? AND days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)`
    ).get(req.user.id, check_product_id);
    if (!sub)
        return res.status(403).json({ error: 'No active subscription or subscription is frozen' });

    // Получаем session_key из активной сессии (расшифровываем из БД)
    // Сессия создаётся при логине — всегда должна быть
    const session = db.prepare('SELECT * FROM loader_sessions WHERE user_id=?').get(req.user.id);
    if (!session)
        return res.status(403).json({ error: 'No active loader session. Please re-login.' });

    const session_key_hex = session.session_key_hex ? decryptSessionKey(session.session_key_hex) : null;
    if (!session_key_hex)
        return res.status(500).json({ error: 'Session error' });

    // Читаем зашифрованный driver с диска (хранится pre-encrypted AES-256-GCM)
    // Путь: ./drivers/<product_id>/driver.enc
    // Формат файла: IV(12) + ciphertext + TAG(16) (то же что и payload чита)
    // И trigger и external используют один и тот же драйвер, берем его из папки external
    const fetch_product_id = product_id === 'trigger' ? 'external' : product_id;
    const driver_dir = path.join(__dirname, 'drivers', fetch_product_id);
    const driver_path = path.join(driver_dir, 'driver.enc');

    if (!fs.existsSync(driver_path))
        return res.status(404).json({ error: 'Driver not found for this product' });

    // Читаем сырой зашифрованный файл (он зашифрован мастер-ключом при деплое)
    const driver_master_enc = fs.readFileSync(driver_path);

    // Перешифровываем driver мастер-ключом → session_key пользователя
    // Шаг 1: расшифровываем мастер-ключом (SERVER_DRIVER_MASTER_KEY из env)
    const DRIVER_MASTER_KEY = process.env.DRIVER_MASTER_KEY;
    if (!DRIVER_MASTER_KEY || DRIVER_MASTER_KEY.length < 32)
        return res.status(500).json({ error: 'Server configuration error' });

    let driver_bytes;
    try {
        const m_iv = driver_master_enc.slice(0, 12);
        const m_tag = driver_master_enc.slice(12, 28);
        const m_ct = driver_master_enc.slice(28);
        const m_key = crypto.scryptSync(DRIVER_MASTER_KEY, 'shadow_drv_v1', 32);
        const decipher = crypto.createDecipheriv('aes-256-gcm', m_key, m_iv);
        decipher.setAuthTag(m_tag);
        driver_bytes = Buffer.concat([decipher.update(m_ct), decipher.final()]);
    } catch (e) {
        console.error('[DRIVER] Master decrypt error:', e.message);
        return res.status(500).json({ error: 'Driver decrypt error' });
    }

    // Шаг 2: перешифровываем session_key пользователя
    // формат = IV(12) + ciphertext + TAG(16) (tag в КОНЦЕ, как в /download чита)
    //   Раньше здесь было [s_iv, s_tag, s_ct] → loader DecryptPayload не находил tag
    //   в конце, driver decrypt fail → "error:Driver load failed".
    const session_key_buf = Buffer.from(session_key_hex, 'hex');
    const s_iv = crypto.randomBytes(12);
    const s_cipher = crypto.createCipheriv('aes-256-gcm', session_key_buf, s_iv);
    const s_ct = Buffer.concat([s_cipher.update(driver_bytes), s_cipher.final()]);
    const s_tag = s_cipher.getAuthTag();
    const driver_payload = Buffer.concat([s_iv, s_ct, s_tag]); // ✓ matches DecryptPayload

    // Очищаем чувствительные данные
    driver_bytes.fill(0);
    session_key_buf.fill(0);

    // Логируем
    db.prepare(`INSERT INTO download_log (user_id, product_id, hwid, loader_filename, integrity_hash) VALUES (?,?,?,?,?)`)
        .run(req.user.id, product_id + '_driver', hwid, 'driver.sys', crypto.createHash('sha256').update(driver_payload).digest('hex').slice(0, 16));

    res.json({
        driver: driver_payload.toString('base64'),
    });
});
function bearerToken(req) {
    const header = req.headers['authorization'];
    return header && header.startsWith('Bearer ')
        ? header.slice(7).trim()
        : null;
}

function requestProduct(req) {
    if (req.path.startsWith('/api/scripts/'))
        return 'ragemp';
    if (req.query && req.query.product)
        return String(req.query.product).toLowerCase();
    if (!req.body)
        return null;
    try {
        const body = typeof req.body === 'string'
            ? JSON.parse(req.body)
            : req.body;
        return body.product ? String(body.product).toLowerCase() : null;
    } catch {
        return null;
    }
}

function activeLaunchSession(userId) {
    return db.prepare(`SELECT integrity_hash, product_id, customer_tag,
        launch_id, variant_id
        FROM loader_sessions WHERE user_id=?`).get(userId);
}

function resolveConfigUser(req) {
    const token = bearerToken(req);
    if (!token)
        return null;
    try {
        const decoded = jwt.verify(token, process.env.JWT_SECRET);
        const user = db.prepare(
            'SELECT id, username, hwid, banned FROM users WHERE id=?'
        ).get(decoded.uid);
        if (!user || user.banned)
            return null;
        const product = requestProduct(req) || decoded.product_id;
        if (decoded.scope !== 'product')
            return product === 'ragemp' ? null : user;
        const session = activeLaunchSession(user.id);
        if (!session || product !== session.product_id)
            return null;
        productDelivery.verifyLaunchTicket(token, {
            productId: session.product_id,
            hwid: user.hwid,
            customerTag: session.customer_tag,
            integrityHash: session.integrity_hash,
            launchId: session.launch_id,
            variantId: session.variant_id
        }, process.env.JWT_SECRET);
        return user;
    } catch {
        return null;
    }
}

const FEATURE_FILES = Object.freeze({
    quant: 'quant.js',
    five_rp: 'five_rp.js',
    ukraine: 'ukraine.js',
    policy: null
});

function readFeatureSource(featureId) {
    if (!Object.prototype.hasOwnProperty.call(FEATURE_FILES, featureId))
        throw new Error('Unknown feature bundle');
    const fileName = FEATURE_FILES[featureId];
    if (fileName === null)
        return productDelivery.combatPolicyBuffer();
    const sourcePath = path.join(
        __dirname, 'products', 'ragemp', 'features', fileName);
    if (!fs.existsSync(sourcePath))
        throw new Error('Feature bundle is unavailable');
    return fs.readFileSync(sourcePath);
}

function createFeatureBundle(user, session, featureId) {
    const canary = productDelivery.deriveFeatureCanary({
        userId: user.id,
        launchId: session.launch_id,
        featureId,
        watermarkSecret: process.env.WATERMARK_SECRET
    });
    const rawSource = readFeatureSource(featureId);
    const source = featureId === 'policy'
        ? productDelivery.watermarkPolicy(rawSource, canary)
        : productDelivery.watermarkFeatureSource(rawSource, canary);
    return {
        ...productDelivery.encryptFeatureBundle(source),
        canary
    };
}

function hasActiveProductSubscription(userId, productId) {
    return Boolean(db.prepare(`SELECT id FROM subscriptions
        WHERE user_id=? AND product_id=? AND days_left > 0
        AND (frozen_at IS NULL AND is_frozen = 0)`).get(
        userId,
        productId));
}

app.post('/api/cheat/renew', express.text({ type: '*/*' }), (req, res) => {
    const user = resolveConfigUser(req);
    if (!user)
        return res.status(401).send('Unauthorized');
    const session = activeLaunchSession(user.id);
    if (!session ||
        !hasActiveProductSubscription(user.id, session.product_id))
        return res.status(403).send('Subscription unavailable');
    const token = productDelivery.issueLaunchTicket({
        userId: user.id,
        productId: session.product_id,
        hwid: user.hwid,
        customerTag: session.customer_tag,
        integrityHash: session.integrity_hash,
        launchId: session.launch_id,
        variantId: session.variant_id,
        jwtSecret: process.env.JWT_SECRET
    });
    return res.type('text/plain').send(token);
});

app.post('/api/product/blob', (req, res) => {
    const user = resolveConfigUser(req);
    const featureId = String(req.body?.feature || '').toLowerCase();
    if (!user)
        return res.status(401).json({ error: 'Unauthorized' });
    const session = activeLaunchSession(user.id);
    if (!session ||
        session.product_id !== 'ragemp' ||
        !hasActiveProductSubscription(user.id, session.product_id))
        return res.status(403).json({ error: 'Invalid product session' });
    try {
        const bundle = createFeatureBundle(user, session, featureId);
        db.prepare(`INSERT INTO feature_bundle_log
            (user_id, launch_id, variant_id, feature_id, canary)
            VALUES (?,?,?,?,?)`).run(
            user.id, session.launch_id, session.variant_id,
            featureId, bundle.canary);
        return res.json({
            ...bundle,
            feature_id: featureId,
            variant_id: String(session.variant_id)
        });
    } catch (error) {
        return res.status(404).json({ error: error.message });
    }
});

app.all('/api/configs/list', express.text({ type: '*/*' }), (req, res) => {
    try {
        const user = resolveConfigUser(req);
        if (!user) return res.send('');

        // product обязателен — без него ошибка
        let product = null;
        try {
            const bodyObj = req.body && typeof req.body === 'string' ? JSON.parse(req.body) : req.body;
            product = (bodyObj && bodyObj.product) || req.query.product || null;
        } catch (e) { product = req.query.product || null; }
        if (!product) return res.status(400).send('Missing product');
        product = product.toLowerCase();

        const configs = db.prepare('SELECT name FROM user_configs WHERE user_id=? AND product_id=? ORDER BY updated_at DESC').all(user.id, product);
        res.send(configs.map(c => c.name).join(','));
    } catch (e) {
        res.status(500).send('Error');
    }
});

app.post('/api/configs/save', express.text({ type: '*/*' }), (req, res) => {
    try {
        const user = resolveConfigUser(req);
        if (!user) return res.status(401).send('Unauthorized');

        let payload = req.body;
        if (typeof payload === 'string') {
            try { payload = JSON.parse(payload); } catch (e) { return res.status(400).send('Invalid JSON'); }
        }

        const name = payload.name;
        const data = payload.data;
        const product = (payload.product || '').toLowerCase();

        if (!name || !data) return res.status(400).send('Missing name or data');
        if (!product) return res.status(400).send('Missing product');

        const existing = db.prepare('SELECT id FROM user_configs WHERE user_id=? AND product_id=? AND name=?').get(user.id, product, name);
        if (existing) {
            db.prepare('UPDATE user_configs SET data=?, updated_at=datetime("now") WHERE id=?').run(data, existing.id);
        } else {
            db.prepare('INSERT INTO user_configs (user_id, product_id, name, data) VALUES (?,?,?,?)').run(user.id, product, name, data);
        }
        res.send('OK');
    } catch (e) {
        res.status(500).send('Error');
    }
});

app.post('/api/configs/load', express.text({ type: '*/*' }), (req, res) => {
    try {
        const user = resolveConfigUser(req);
        if (!user) return res.status(401).send('Unauthorized');

        let payload = req.body;
        if (typeof payload === 'string') {
            try { payload = JSON.parse(payload); } catch (e) { return res.status(400).send('Invalid JSON'); }
        }

        const name = payload.name;
        const product = (payload.product || '').toLowerCase();
        if (!name) return res.status(400).send('Missing name');
        if (!product) return res.status(400).send('Missing product');

        const config = db.prepare('SELECT data FROM user_configs WHERE user_id=? AND product_id=? AND name=?').get(user.id, product, name);
        if (!config) return res.status(404).send('Not found');

        res.send(JSON.stringify({ data: config.data }));
    } catch (e) {
        res.status(500).send('Error');
    }
});

app.post('/api/configs/delete', express.text({ type: '*/*' }), (req, res) => {
    try {
        const user = resolveConfigUser(req);
        if (!user) return res.status(401).send('Unauthorized');

        let payload = req.body;
        if (typeof payload === 'string') {
            try { payload = JSON.parse(payload); } catch (e) { return res.status(400).send('Invalid JSON'); }
        }

        const name = payload.name;
        const product = (payload.product || '').toLowerCase();
        if (!name) return res.status(400).send('Missing name');
        if (!product) return res.status(400).send('Missing product');

        db.prepare('DELETE FROM user_configs WHERE user_id=? AND product_id=? AND name=?').run(user.id, product, name);
        res.send('OK');
    } catch (e) {
        res.status(500).send('Error');
    }
});

 
//  CLOUD SCRIPTS (RAGE:MP internal)
//  Используют ту же resolveConfigUser авторизацию (HWID/JWT)
 

app.all('/api/scripts/list', express.text({ type: '*/*' }), (req, res) => {
    try {
        const user = resolveConfigUser(req);
        if (!user) return res.send('');
        const scripts = db.prepare('SELECT name FROM user_scripts WHERE user_id=? ORDER BY updated_at DESC').all(user.id);
        res.send(scripts.map(s => s.name).join(','));
    } catch (e) {
        res.status(500).send('Error');
    }
});

app.post('/api/scripts/save', express.text({ type: '*/*' }), (req, res) => {
    try {
        const user = resolveConfigUser(req);
        if (!user) return res.status(401).send('Unauthorized');

        let payload = req.body;
        if (typeof payload === 'string') {
            try { payload = JSON.parse(payload); } catch (e) { return res.status(400).send('Invalid JSON'); }
        }

        const name = payload.name;
        const data = payload.data;
        if (!name || !data) return res.status(400).send('Missing name or data');

        const existing = db.prepare('SELECT id FROM user_scripts WHERE user_id=? AND name=?').get(user.id, name);
        if (existing) {
            db.prepare('UPDATE user_scripts SET data=?, updated_at=datetime("now") WHERE id=?').run(data, existing.id);
        } else {
            db.prepare('INSERT INTO user_scripts (user_id, name, data) VALUES (?,?,?)').run(user.id, name, data);
        }
        res.send('OK');
    } catch (e) {
        res.status(500).send('Error');
    }
});

app.post('/api/scripts/load', express.text({ type: '*/*' }), (req, res) => {
    try {
        const user = resolveConfigUser(req);
        if (!user) return res.status(401).send('Unauthorized');

        let payload = req.body;
        if (typeof payload === 'string') {
            try { payload = JSON.parse(payload); } catch (e) { return res.status(400).send('Invalid JSON'); }
        }

        const name = payload.name;
        if (!name) return res.status(400).send('Missing name');

        const script = db.prepare('SELECT data FROM user_scripts WHERE user_id=? AND name=?').get(user.id, name);
        if (!script) return res.status(404).send('Not found');

        res.send(JSON.stringify({ data: script.data }));
    } catch (e) {
        res.status(500).send('Error');
    }
});

app.post('/api/scripts/delete', express.text({ type: '*/*' }), (req, res) => {
    try {
        const user = resolveConfigUser(req);
        if (!user) return res.status(401).send('Unauthorized');

        let payload = req.body;
        if (typeof payload === 'string') {
            try { payload = JSON.parse(payload); } catch (e) { return res.status(400).send('Invalid JSON'); }
        }

        const name = payload.name;
        if (!name) return res.status(400).send('Missing name');

        db.prepare('DELETE FROM user_scripts WHERE user_id=? AND name=?').run(user.id, name);
        res.send('OK');
    } catch (e) {
        res.status(500).send('Error');
    }
});

app.post('/api/configs/delete', express.text({ type: '*/*' }), (req, res) => {
    try {
        const user = resolveConfigUser(req);
        if (!user) return res.status(401).send('Unauthorized');

        let payload = req.body;
        if (typeof payload === 'string') {
            try { payload = JSON.parse(payload); } catch (e) { return res.status(400).send('Invalid JSON'); }
        }

        const name = payload.name;
        const product = (payload.product || '').toLowerCase();
        if (!name) return res.status(400).send('Missing name');
        if (!product) return res.status(400).send('Missing product');

        db.prepare('DELETE FROM user_configs WHERE user_id=? AND product_id=? AND name=?').run(user.id, product, name);
        res.send('OK');
    } catch (e) {
        res.status(500).send('Error');
    }
});

 
//  CHEAT VERIFY — чит посылает сигнал для проверки подписки
 
const cheatVerifyLimiter = rateLimit({ validate: { trustProxy: false }, windowMs: 10_000, max: 10 });
app.post('/api/cheat/verify', cheatVerifyLimiter, (req, res) => {
    const { token, hwid, loader_session_uuid, ts, sig } = req.body;
    if (!token || !hwid) return res.json({ ok: false, action: 'crash' });

    // Проверяем подпись
    if (process.env.CLIENT_SHARED_SECRET) {
        if (!sig || !ts || !verifyClientSignature(hwid, ts, sig))
            return res.json({ ok: false, action: 'crash' });
    }

    let user;
    try {
        const decoded = jwt.verify(token, process.env.JWT_SECRET);
        user = db.prepare('SELECT * FROM users WHERE id=?').get(decoded.uid);
    } catch { return res.json({ ok: false, action: 'crash' }); }

    if (!user || user.banned || user.hwid !== hwid)
        return res.json({ ok: false, action: 'crash' });

    // Проверяем активную незамороженную подписку
    const sub = db.prepare(
        `SELECT * FROM subscriptions WHERE user_id=? AND days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)`
    ).get(user.id);
    if (!sub) {
        db.prepare(`INSERT INTO security_events (user_id, hwid, event_type, detail, ip) VALUES (?,?,?,?,?)`)
            .run(user.id, hwid, 'CHEAT_NO_SUB', 'cheat verify failed', req.ip);
        return res.json({ ok: false, action: 'crash' });
    }

    // Проверяем loader_session_uuid если передан
    if (loader_session_uuid) {
        const binding = db.prepare('SELECT * FROM loader_bindings WHERE session_uuid=?').get(loader_session_uuid);
        if (!binding || binding.user_id !== user.id || binding.hwid !== hwid || binding.revoked) {
            db.prepare(`INSERT INTO security_events (user_id, hwid, event_type, detail, ip) VALUES (?,?,?,?,?)`)
                .run(user.id, hwid, 'CHEAT_INVALID_SESSION', `uuid=${loader_session_uuid?.slice(0, 16)}`, req.ip);
            return res.json({ ok: false, action: 'crash' });
        }
    }

    res.json({ ok: true, days: sub.days_left });
});


 
//  SECURITY ROUTES
 

// POST /api/security/ban — теперь с HMAC подписью (клиент должен подписывать)
app.post('/api/security/ban', banLimiter, (req, res) => {
    const { hwid, reason, token, ts, signature } = req.body;

    // Проверяем подпись (тот же CLIENT_SHARED_SECRET что и при логине)
    if (process.env.CLIENT_SHARED_SECRET) {
        if (!signature || !ts || !verifyClientSignature(hwid || '', ts, signature)) {
            // Логируем попытку без подписи (возможно кто-то пытается злоупотребить endpoint'ом)
            db.prepare(`INSERT INTO security_events (hwid, event_type, detail, ip) VALUES (?,?,?,?)`).run(
                hwid, 'BAN_UNSIGNED', reason, req.ip
            );
            // НЕ баним — просто тихо игнорируем без ответа об ошибке
            return res.json({ ok: true }); // fake OK
        }
    }

    let userId = null;
    if (token) {
        try {
            const decoded = jwt.verify(token, process.env.JWT_SECRET);
            userId = decoded.uid;
        } catch { }
    }

    let user = hwid ? db.prepare('SELECT * FROM users WHERE hwid=?').get(hwid) : null;
    if (!user && userId) user = db.prepare('SELECT * FROM users WHERE id=?').get(userId);

    const reason_str = `CLIENT_REPORT_${reason}`;

    if (user) {
        banUserAndRevoke(user.id, reason_str, hwid || user.hwid);
    } else if (hwid) {
        try {
            db.prepare('INSERT OR IGNORE INTO hwid_banlist (hwid, reason) VALUES (?,?)').run(hwid, reason);
        } catch { }
    }

    console.log(`[SECURITY_BAN] hwid=${hwid} reason=${reason} userId=${userId}`);
    res.json({ ok: true });
});

 
//  HONEYPOT ENDPOINTS
 
app.post('/api/auth/bypass', (req, res) => {
    console.warn(`[HONEYPOT HIT] IP=${req.ip} body=${JSON.stringify(req.body)}`);
    bannedIPs.add(req.ip);
    db.prepare(`INSERT INTO security_events (hwid, event_type, detail, ip) VALUES (?,?,?,?)`).run(null, 'HONEYPOT', 'auth_bypass_hit', req.ip);
    res.json({ token: 'fake_bypass_token_invalid', status: 'ok', user: { id: 1, premium: true } });
});

app.get('/api/admin/config', (req, res) => {
    console.warn(`[HONEYPOT ADMIN_CONFIG] IP=${req.ip}`);
    bannedIPs.add(req.ip);
    db.prepare(`INSERT INTO security_events (hwid, event_type, detail, ip) VALUES (?,?,?,?)`).run(null, 'HONEYPOT', 'admin_config_hit', req.ip);
    res.json({ secret_key: 'fake_key_lmao', admin: true });
});

app.get('/api/v1/crack', (req, res) => {
    bannedIPs.add(req.ip);
    db.prepare(`INSERT INTO security_events (hwid, event_type, detail, ip) VALUES (?,?,?,?)`).run(null, 'HONEYPOT', 'crack_endpoint_hit', req.ip);
    res.json({ status: 'cracked', license: 'unlimited', expires: 'never' });
});

 
//  ADMIN ROUTES (API)
 

app.get('/api/admin/stats', authenticateToken, requireAdmin, (req, res) => {
    const stats = {
        total_users: db.prepare('SELECT COUNT(*) as c FROM users').get().c,
        banned_users: db.prepare('SELECT COUNT(*) as c FROM users WHERE banned=1').get().c,
        active_subs: db.prepare('SELECT COUNT(*) as c FROM subscriptions WHERE days_left > 0 AND (frozen_at IS NULL AND is_frozen = 0)').get().c,
        frozen_subs: db.prepare('SELECT COUNT(*) as c FROM subscriptions WHERE is_frozen=1 OR frozen_at IS NOT NULL').get().c,
        active_sessions: db.prepare("SELECT COUNT(*) as c FROM loader_sessions WHERE last_heartbeat > datetime('now', '-10 minutes')").get().c,
        downloads_24h: db.prepare("SELECT COUNT(*) as c FROM download_log WHERE ts > datetime('now', '-1 day')").get().c,
        loader_downloads_24h: db.prepare("SELECT COUNT(*) as c FROM activity_log WHERE action='LOADER_DOWNLOAD' AND ts > datetime('now', '-1 day')").get().c,
        security_events_24h: db.prepare("SELECT COUNT(*) as c FROM security_events WHERE ts > datetime('now', '-1 day')").get().c,
        hwid_bans: db.prepare('SELECT COUNT(*) as c FROM hwid_banlist').get().c,
        loader_versions: db.prepare('SELECT COUNT(*) as c FROM loader_versions WHERE revoked=0').get().c,
    };
    res.json(stats);
});

// ─── Loader Versions ───
app.get('/api/admin/loader-versions', authenticateToken, requireAdmin, (req, res) => {
    const versions = db.prepare(`
        SELECT lv.*, u.username as revoked_by_name,
               (SELECT COUNT(*) FROM loader_bindings lb WHERE lb.version_id=lv.version_id) as download_count
        FROM loader_versions lv
        LEFT JOIN users u ON lv.revoked_by=u.id
        ORDER BY lv.id DESC
    `).all();
    res.json(versions);
});

app.post('/api/admin/loader-versions', authenticateToken, requireAdmin, (req, res) => {
    const { version_id, label } = req.body;
    if (!version_id) return res.status(400).json({ error: 'Missing version_id' });
    // Генерируем уникальный токен версии
    const version_token = crypto.randomBytes(32).toString('hex');
    try {
        db.prepare('INSERT INTO loader_versions (version_id, version_token, label) VALUES (?,?,?)').run(
            String(version_id), version_token, label || ''
        );
        db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(
            req.user.id, `LOADER_VERSION_ADD_${version_id}`, 0, req.ip
        );
        res.json({ ok: true, version_token });
    } catch (e) { res.status(409).json({ error: 'Version ID already exists' }); }
});

app.post('/api/admin/loader-versions/:id/revoke', authenticateToken, requireAdmin, (req, res) => {
    const ver = db.prepare('SELECT * FROM loader_versions WHERE id=?').get(req.params.id);
    if (!ver) return res.status(404).json({ error: 'Not found' });
    db.prepare("UPDATE loader_versions SET revoked=1, revoked_at=datetime('now'), revoked_by=? WHERE id=?").run(req.user.id, ver.id);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(
        req.user.id, `LOADER_VERSION_REVOKE_${ver.version_id}`, 0, req.ip
    );
    res.json({ ok: true });
});

app.post('/api/admin/loader-versions/:id/restore', authenticateToken, requireAdmin, (req, res) => {
    db.prepare('UPDATE loader_versions SET revoked=0, revoked_at=NULL, revoked_by=NULL WHERE id=?').run(req.params.id);
    res.json({ ok: true });
});

// ─── Upload Loader Binary ───
app.post('/api/admin/upload-loader', authenticateToken, requireAdmin, express.raw({ type: 'application/octet-stream', limit: '100mb' }), (req, res) => {
    try {
        if (!req.body || !req.body.length) {
            return res.status(400).json({ error: 'No file data received' });
        }
        const targetPath = path.join(__dirname, 'products', 'loader.exe');
        fs.writeFileSync(targetPath, req.body);

        db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(
            req.user.id, `LOADER_BINARY_UPLOADED`, 0, req.ip
        );
        res.json({ ok: true, message: 'Loader file uploaded successfully' });
    } catch (e) {
        console.error('Loader upload error:', e);
        res.status(500).json({ error: 'File save error' });
    }
});

// ─── Loader Bindings (sessions) ───
app.get('/api/admin/loader-sessions', authenticateToken, requireAdmin, (req, res) => {
    const sessions = db.prepare(`
        SELECT lb.*, u.username
        FROM loader_bindings lb
        LEFT JOIN users u ON lb.user_id=u.id
        ORDER BY lb.id DESC LIMIT 200
    `).all();
    res.json(sessions);
});

app.post('/api/admin/loader-sessions/:id/revoke', authenticateToken, requireAdmin, (req, res) => {
    db.prepare('UPDATE loader_bindings SET revoked=1 WHERE id=?').run(req.params.id);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(
        req.user.id, 'LOADER_SESSION_REVOKE', req.params.id, req.ip
    );
    res.json({ ok: true });
});

// ─── Activity Log ───
app.get('/api/admin/activity-log', authenticateToken, requireAdmin, (req, res) => {
    const { action_filter, search, limit: limitQ } = req.query;
    const lim = Math.min(parseInt(limitQ) || 300, 1000);
    let q = 'SELECT * FROM activity_log WHERE 1=1';
    const params = [];
    if (action_filter) { q += ' AND action=?'; params.push(action_filter); }
    if (search) { q += ' AND (username LIKE ? OR hwid LIKE ? OR ip LIKE ? OR session_uuid LIKE ? OR action LIKE ?)'; params.push(`%${search}%`, `%${search}%`, `%${search}%`, `%${search}%`, `%${search}%`); }
    q += ` ORDER BY id DESC LIMIT ${lim}`;
    res.json(db.prepare(q).all(...params));
});

// ─── Request Log (все HTTP запросы) ───
app.get('/api/admin/request-log', authenticateToken, requireAdmin, (req, res) => {
    const { search, limit: limitQ } = req.query;
    const lim = Math.min(parseInt(limitQ) || 500, 2000);
    let q = 'SELECT * FROM request_log WHERE 1=1';
    const params = [];
    if (search) { q += ' AND (ip LIKE ? OR path LIKE ? OR username LIKE ? OR body_keys LIKE ?)'; params.push(`%${search}%`, `%${search}%`, `%${search}%`, `%${search}%`); }
    q += ` ORDER BY id DESC LIMIT ${lim}`;
    res.json(db.prepare(q).all(...params));
});

// ─── User Full Info ───
app.get('/api/admin/users/:id/info', authenticateToken, requireAdmin, (req, res) => {
    const user = db.prepare('SELECT id,username,hwid,hwid_locked,banned,ban_reason,banned_at,is_admin,media_access,is_distributor,balance,created_at,email FROM users WHERE id=?').get(req.params.id);
    if (!user) return res.status(404).json({ error: 'Not found' });

    const subscriptions = db.prepare('SELECT * FROM subscriptions WHERE user_id=?').all(user.id);
    const loaderSession = db.prepare('SELECT last_heartbeat, loader_session_uuid FROM loader_sessions WHERE user_id=?').get(user.id);
    const loaderBinding = db.prepare('SELECT session_uuid, version_id, created_at, ip FROM loader_bindings WHERE user_id=? AND revoked=0 ORDER BY id DESC LIMIT 1').get(user.id);
    const lastLogin = db.prepare("SELECT ip, ts, session_uuid, loader_version FROM activity_log WHERE user_id=? AND action='LOADER_LOGIN' ORDER BY id DESC LIMIT 1").get(user.id);
    const lastWebLogin = db.prepare("SELECT ip, ts FROM activity_log WHERE user_id=? AND action='WEB_LOGIN' ORDER BY id DESC LIMIT 1").get(user.id);
    const recentActivity = db.prepare('SELECT action, detail, ip, ts, session_uuid, loader_version FROM activity_log WHERE user_id=? ORDER BY id DESC LIMIT 10').all(user.id);
    const securityEvents = db.prepare('SELECT event_type, detail, ip, ts FROM security_events WHERE user_id=? ORDER BY id DESC LIMIT 5').all(user.id);

    res.json({
        ...user,
        subscriptions,
        loader_session: loaderSession,
        loader_binding: loaderBinding,
        last_loader_login: lastLogin,
        last_web_login: lastWebLogin,
        recent_activity: recentActivity,
        security_events: securityEvents,
    });
});

// ─── IP Blacklist ───
app.get('/api/admin/blacklist/ip', authenticateToken, requireAdmin, (req, res) => {
    const bans = db.prepare('SELECT * FROM ip_banlist ORDER BY created_at DESC LIMIT 200').all();
    res.json(bans);
});

app.post('/api/admin/blacklist/ip', authenticateToken, requireAdmin, (req, res) => {
    const { ip, reason } = req.body;
    if (!ip) return res.status(400).json({ error: 'Missing ip' });
    db.prepare('INSERT OR IGNORE INTO ip_banlist (ip, reason) VALUES (?,?)').run(ip, reason || 'MANUAL_ADMIN_BAN');
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'ADD_IP_BAN', 0, req.ip);
    logActivity(req.user.id, req.user.username, 'ADMIN_IP_BAN', `ip=${ip} reason=${reason}`, null, req.ip);
    res.json({ ok: true });
});

app.delete('/api/admin/blacklist/ip/:ip', authenticateToken, requireAdmin, (req, res) => {
    db.prepare('DELETE FROM ip_banlist WHERE ip=?').run(req.params.ip);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'REMOVE_IP_BAN', 0, req.ip);
    res.json({ ok: true });
});

// ─── Session Blacklist ───
app.get('/api/admin/blacklist/session', authenticateToken, requireAdmin, (req, res) => {
    const bans = db.prepare('SELECT * FROM session_banlist ORDER BY created_at DESC LIMIT 200').all();
    res.json(bans);
});

app.post('/api/admin/blacklist/session', authenticateToken, requireAdmin, (req, res) => {
    const { session_uuid, reason } = req.body;
    if (!session_uuid) return res.status(400).json({ error: 'Missing session_uuid' });
    db.prepare('INSERT OR IGNORE INTO session_banlist (session_uuid, reason) VALUES (?,?)').run(session_uuid, reason || 'MANUAL_ADMIN_BAN');
    // Also revoke the loader binding
    db.prepare('UPDATE loader_bindings SET revoked=1 WHERE session_uuid=?').run(session_uuid);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'ADD_SESSION_BAN', 0, req.ip);
    logActivity(req.user.id, req.user.username, 'ADMIN_SESSION_BAN', `uuid=${session_uuid.slice(0, 16)} reason=${reason}`, null, req.ip);
    res.json({ ok: true });
});

app.delete('/api/admin/blacklist/session/:uuid', authenticateToken, requireAdmin, (req, res) => {
    db.prepare('DELETE FROM session_banlist WHERE session_uuid=?').run(req.params.uuid);
    res.json({ ok: true });
});

// ─── Bulk Freeze / Unfreeze ───
app.post('/api/admin/subscriptions/freeze-all', authenticateToken, requireAdmin, (req, res) => {
    const result = db.prepare(
        `UPDATE subscriptions SET frozen_at=datetime('now'), is_frozen=2 WHERE days_left > 0 AND (frozen_at IS NULL OR is_frozen=0)`
    ).run();
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(
        req.user.id, 'BULK_FREEZE_ALL', 0, req.ip
    );
    logActivity(req.user.id, req.user.username, 'ADMIN_BULK_FREEZE', `affected=${result.changes}`, null, req.ip);
    res.json({ ok: true, affected: result.changes });
});

app.post('/api/admin/subscriptions/add-all', authenticateToken, requireAdmin, (req, res) => {
    const { product_id, days, mode } = req.body;
    if (!product_id || !days || days <= 0) return res.status(400).json({ error: 'Invalid parameters' });
    
    let affected = 0;
    if (mode === 'all') {
        const users = db.prepare('SELECT id FROM users').all();
        const existingSubs = db.prepare('SELECT user_id, id FROM subscriptions WHERE product_id COLLATE NOCASE=?').all(product_id);
        const subMap = new Set(existingSubs.map(s => s.user_id));
        
        db.prepare('UPDATE subscriptions SET days_left = days_left + ? WHERE product_id COLLATE NOCASE=?').run(days * 24, product_id);
        affected += existingSubs.length;

        const insertStmt = db.prepare('INSERT INTO subscriptions (user_id, product_id, days_left) VALUES (?,?,?)');
        const tx = db.transaction(() => {
            for (const u of users) {
                if (!subMap.has(u.id)) {
                    insertStmt.run(u.id, product_id, days * 24);
                    affected++;
                }
            }
        });
        tx();
    } else {
        const result = db.prepare('UPDATE subscriptions SET days_left = days_left + ? WHERE product_id COLLATE NOCASE=? AND days_left > 0').run(days * 24, product_id);
        affected = result.changes;
    }

    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(
        req.user.id, 'BULK_ADD_DAYS', 0, req.ip
    );
    logActivity(req.user.id, req.user.username, 'ADMIN_BULK_ADD_DAYS', `product=${product_id} days=${days} mode=${mode} affected=${affected}`, null, req.ip);
    res.json({ ok: true, affected });
});

app.post('/api/admin/subscriptions/unfreeze-all', authenticateToken, requireAdmin, (req, res) => {
    const result = db.prepare(
        `UPDATE subscriptions SET frozen_at=NULL, is_frozen=0 WHERE is_frozen=2`
    ).run();
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(
        req.user.id, 'BULK_UNFREEZE_ALL', 0, req.ip
    );
    logActivity(req.user.id, req.user.username, 'ADMIN_BULK_UNFREEZE', `affected=${result.changes}`, null, req.ip);
    res.json({ ok: true, affected: result.changes });
});

app.post('/api/admin/users/:id/freeze-sub', authenticateToken, requireAdmin, (req, res) => {
    const { product_id } = req.body;
    const q = product_id
        ? `UPDATE subscriptions SET frozen_at=datetime('now'), is_frozen=1 WHERE user_id=? AND product_id=? AND days_left > 0`
        : `UPDATE subscriptions SET frozen_at=datetime('now'), is_frozen=1 WHERE user_id=? AND days_left > 0`;
    const params = product_id ? [req.params.id, product_id] : [req.params.id];
    const r = db.prepare(q).run(...params);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'ADMIN_FREEZE_SUB', req.params.id, req.ip);
    res.json({ ok: true, affected: r.changes });
});

app.post('/api/admin/users/:id/unfreeze-sub', authenticateToken, requireAdmin, (req, res) => {
    const { product_id } = req.body;
    const q = product_id
        ? `UPDATE subscriptions SET frozen_at=NULL, is_frozen=0 WHERE user_id=? AND product_id=?`
        : `UPDATE subscriptions SET frozen_at=NULL, is_frozen=0 WHERE user_id=?`;
    const params = product_id ? [req.params.id, product_id] : [req.params.id];
    const r = db.prepare(q).run(...params);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'ADMIN_UNFREEZE_SUB', req.params.id, req.ip);
    res.json({ ok: true, affected: r.changes });
});

app.get('/api/admin/users', authenticateToken, requireAdmin, (req, res) => {
    const search = req.query.search || '';
    const page = Math.max(1, parseInt(req.query.page) || 1);
    const limit = 50;
    const offset = (page - 1) * limit;

    let query = 'SELECT id,username,hwid,banned,ban_reason,is_admin,media_access,is_distributor,promocode,promo_percent,promo_earned,balance,created_at FROM users';
    let params = [];
    if (search) {
        query += ' WHERE username LIKE ? OR hwid LIKE ?';
        params = [`%${search}%`, `%${search}%`];
    }
    query += ` ORDER BY id DESC LIMIT ${limit} OFFSET ${offset}`;

    const users = db.prepare(query).all(...params);
    const result = users.map(u => ({
        ...u,
        subscriptions: db.prepare('SELECT * FROM subscriptions WHERE user_id=?').all(u.id).map(s => ({
            ...s,
            days_left: Math.ceil(s.days_left / 24)
        }))
    }));
    const total = db.prepare(search
        ? 'SELECT COUNT(*) as c FROM users WHERE username LIKE ? OR hwid LIKE ?'
        : 'SELECT COUNT(*) as c FROM users').get(...(search ? [`%${search}%`, `%${search}%`] : [])).c;

    res.json({ users: result, total, page, pages: Math.ceil(total / limit) });
});

function banUserAndRevoke(userId, reason, hwid, ip) {
    db.prepare('UPDATE users SET banned=1, ban_reason=?, banned_at=datetime("now") WHERE id=?').run(reason, userId);
    db.prepare('UPDATE subscriptions SET is_frozen=1, frozen_at=datetime("now") WHERE user_id=?').run(userId);
    // Delete active sessions
    db.prepare('DELETE FROM loader_sessions WHERE user_id=?').run(userId);
    if (hwid) {
        db.prepare('INSERT OR IGNORE INTO hwid_banlist (hwid, reason, user_id) VALUES (?,?,?)').run(hwid, reason, userId);
    }
    if (ip) {
        db.prepare('CREATE TABLE IF NOT EXISTS ip_banlist (ip TEXT PRIMARY KEY, reason TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP)').run();
        db.prepare('INSERT OR IGNORE INTO ip_banlist (ip, reason) VALUES (?,?)').run(ip, reason);
    }
}

app.post('/api/admin/users/:id/ban', authenticateToken, requireAdmin, (req, res) => {
    const { id } = req.params;
    const user = db.prepare('SELECT * FROM users WHERE id=?').get(id);
    if (!user) return res.sendStatus(404);

    const { reason } = req.body;
    const newBanned = !user.banned;
    if (newBanned) {
        banUserAndRevoke(user.id, reason || 'ADMIN_BAN', user.hwid, req.ip);
    } else {
        db.prepare('UPDATE users SET banned=0, ban_reason=NULL, banned_at=NULL WHERE id=?').run(id);
    }
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'TOGGLE_BAN', id, req.ip);
    res.json({ ok: true, banned: newBanned });
});

app.post('/api/admin/users/:id/hwid-reset', authenticateToken, requireAdmin, (req, res) => {
    db.prepare('UPDATE users SET hwid=NULL, hwid_locked=0 WHERE id=?').run(req.params.id);
    // Убираем из HWID banlist если был там
    const user = db.prepare('SELECT hwid FROM users WHERE id=?').get(req.params.id);
    if (user?.hwid) db.prepare('DELETE FROM hwid_banlist WHERE hwid=?').run(user.hwid);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'HWID_RESET', req.params.id, req.ip);
    res.json({ ok: true });
});

app.post('/api/admin/users/:id/subscription', authenticateToken, requireAdmin, (req, res) => {
    const { product_id, days } = req.body;
    const existing = db.prepare('SELECT * FROM subscriptions WHERE user_id=? AND product_id=?').get(req.params.id, product_id);
    if (days > 0) {
        if (existing) {
            db.prepare('UPDATE subscriptions SET days_left=? WHERE id=?').run(days * 24, existing.id);
        } else {
            db.prepare('INSERT INTO subscriptions (user_id, product_id, days_left) VALUES (?,?,?)').run(req.params.id, product_id, days * 24);
        }
    } else {
        db.prepare('UPDATE subscriptions SET days_left=0 WHERE user_id=? AND product_id=?').run(req.params.id, product_id);
    }
    const targetUser = db.prepare('SELECT username FROM users WHERE id=?').get(req.params.id);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'SUB_CHANGE', req.params.id, req.ip);
    logActivity(req.params.id, targetUser?.username, 'ADMIN_SUB_CHANGE', `product=${product_id} days=${days} by=${req.user.username}`, null, req.ip);
    res.json({ ok: true });
});

app.post('/api/admin/keys/generate', authenticateToken, requireAdmin, (req, res) => {
    // Support both camelCase (frontend) and snake_case (API)
    const product_id = req.body.product_id || req.body.productId;
    const days = parseInt(req.body.days) || 30;
    const amount = parseInt(req.body.amount) || 1;
    if (!product_id) return res.status(400).json({ error: 'Missing product_id' });
    const keys = [];
    for (let i = 0; i < Math.min(amount, 100); i++) {
        const genChars = (len) => {
            const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789';
            return Array.from({ length: len }, () => chars[Math.floor(Math.random() * chars.length)]).join('');
        };
        const key = `${genChars(5)}-${genChars(8)}-${genChars(9)}-${genChars(8)}`;
        db.prepare('INSERT INTO activation_keys (key_str, product_id, days) VALUES (?,?,?)').run(key, product_id, days * 24);
        keys.push(key);
    }
    res.json({ keys, days, product_id });
});

app.get('/api/admin/keys', authenticateToken, requireAdmin, (req, res) => {
    const keys = db.prepare('SELECT ak.*, u.username as used_by_name FROM activation_keys ak LEFT JOIN users u ON ak.used_by=u.id ORDER BY ak.id DESC LIMIT 200').all();
    keys.forEach(k => k.days = Math.ceil(k.days / 24));
    res.json(keys);
});

app.get('/api/admin/security-events', authenticateToken, requireAdmin, (req, res) => {
    const { search } = req.query;
    let q = `SELECT se.*, u.username FROM security_events se LEFT JOIN users u ON se.user_id=u.id WHERE 1=1`;
    const params = [];
    if (search) { q += ' AND (u.username LIKE ? OR se.hwid LIKE ? OR se.ip LIKE ? OR se.event_type LIKE ?)'; params.push(`%${search}%`, `%${search}%`, `%${search}%`, `%${search}%`); }
    q += ' ORDER BY se.id DESC LIMIT 300';
    res.json(db.prepare(q).all(...params));
});

app.get('/api/admin/download-log', authenticateToken, requireAdmin, (req, res) => {
    const { search } = req.query;
    let q = `SELECT dl.*, u.username FROM download_log dl LEFT JOIN users u ON dl.user_id=u.id WHERE 1=1`;
    const params = [];
    if (search) { q += ' AND (u.username LIKE ? OR dl.hwid LIKE ? OR dl.product_id LIKE ?)'; params.push(`%${search}%`, `%${search}%`, `%${search}%`); }
    q += ' ORDER BY dl.id DESC LIMIT 300';
    res.json(db.prepare(q).all(...params));
});

app.get('/api/admin/audit-log', authenticateToken, requireAdmin, (req, res) => {
    const { search } = req.query;
    let q = `SELECT al.*, u.username as admin_name FROM audit_log al LEFT JOIN users u ON al.admin_id=u.id WHERE 1=1`;
    const params = [];
    if (search) { q += ' AND (u.username LIKE ? OR al.action LIKE ?)'; params.push(`%${search}%`, `%${search}%`); }
    q += ' ORDER BY al.id DESC LIMIT 300';
    res.json(db.prepare(q).all(...params));
});

// ─── Loader/Cheat Error Log ───
// POST /api/loader/error — принимает краш-репорт от лоадера (БЕЗ авторизации).
// Авторизация опциональна: токен передаётся в теле запроса, не в заголовке.
const errorLogLimiter = rateLimit({ validate: { trustProxy: false }, windowMs: 60_000, max: 30 });
app.post('/api/loader/error', errorLogLimiter, (req, res) => {
    try {
        const { token, hwid, phase, message, loader_ver } = req.body;
        if (!message) return res.json({ ok: false });

        let user_id = null, username = null;
        if (token) {
            try {
                const decoded = jwt.verify(token, process.env.JWT_SECRET);
                const u = db.prepare('SELECT id, username FROM users WHERE id=?').get(decoded.uid);
                if (u) { user_id = u.id; username = u.username; }
            } catch { }
        }

        const safe_msg = String(message || '').slice(0, 4096);
        const safe_phase = String(phase || 'UNKNOWN').slice(0, 64);
        const safe_ver = String(loader_ver || req.headers['x-loader-version'] || '').slice(0, 32);
        const safe_hwid = String(hwid || '').slice(0, 128);

        db.prepare(`INSERT INTO loader_error_log (user_id, username, hwid, phase, message, loader_ver, ip)
                    VALUES (?,?,?,?,?,?,?)`).run(
            user_id, username, safe_hwid, safe_phase, safe_msg, safe_ver, req.ip
        );

        console.log(`[CRASH_REPORT] user=${username || 'anon'} phase=${safe_phase} hwid=${safe_hwid.slice(0, 16)} msg=${safe_msg.slice(0, 80)}`);
        res.json({ ok: true });
    } catch (e) {
        res.json({ ok: false });
    }
});

app.get('/api/admin/loader-errors', authenticateToken, requireAdmin, (req, res) => {
    const { search } = req.query;
    let q = 'SELECT * FROM loader_error_log WHERE 1=1';
    const params = [];
    if (search) { q += ' AND (username LIKE ? OR hwid LIKE ? OR phase LIKE ? OR message LIKE ?)'; params.push(`%${search}%`, `%${search}%`, `%${search}%`, `%${search}%`); }
    q += ' ORDER BY id DESC LIMIT 300';
    res.json(db.prepare(q).all(...params));
});

app.delete('/api/admin/loader-errors/:id', authenticateToken, requireAdmin, (req, res) => {
    db.prepare('DELETE FROM loader_error_log WHERE id=?').run(req.params.id);
    res.json({ ok: true });
});

app.delete('/api/admin/hwid-ban/:hwid', authenticateToken, requireAdmin, (req, res) => {
    db.prepare('DELETE FROM hwid_banlist WHERE hwid=?').run(req.params.hwid);
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'HWID_UNBAN', 0, req.ip);
    res.json({ ok: true });
});

app.get('/api/admin/hwid-bans', authenticateToken, requireAdmin, (req, res) => {
    const bans = db.prepare('SELECT * FROM hwid_banlist ORDER BY id DESC LIMIT 200').all();
    res.json(bans);
});

app.post('/api/admin/blacklist/hwid', authenticateToken, requireAdmin, (req, res) => {
    const { hwid, reason } = req.body;
    if (!hwid) return res.status(400).json({ error: 'Missing hwid' });
    db.prepare('INSERT OR IGNORE INTO hwid_banlist (hwid, reason, user_id) VALUES (?,?,NULL)').run(hwid, reason || 'MANUAL_ADMIN_BAN');
    db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'ADD_HWID_BAN', 0, req.ip);
    res.json({ ok: true });
});

app.get('/api/admin/payments', authenticateToken, requireAdmin, (req, res) => {
    const payments = db.prepare(`
        SELECT p.*, u.username FROM payments p
        LEFT JOIN users u ON p.user_id=u.id
        ORDER BY p.id DESC LIMIT 200
    `).all();
    res.json(payments);
});

 
//  ПЕРИОДИЧЕСКАЯ ОЧИСТКА (9.5: download_log retention + old sessions)
//  + ЕЖЕДНЕВНЫЙ ДЕКРЕМЕНТ ДНЕЙ ПОДПИСКИ
 

// Добавляем колонку frozen_at если её нет
try { db.exec("ALTER TABLE subscriptions ADD COLUMN frozen_at TEXT DEFAULT NULL"); } catch (e) { }

function decrementSubscriptionDays() {
    try {
        const hourStr = new Date().toISOString().substring(0, 13);
        try { db.exec('CREATE TABLE IF NOT EXISTS system_state ("key" TEXT PRIMARY KEY, "value" TEXT)'); } catch (e) { }

        try {
            const mig = db.prepare('SELECT "value" FROM system_state WHERE "key"=\'hours_migration_v2\'').get();
            if (!mig) {
                db.prepare('UPDATE subscriptions SET days_left = days_left * 24').run();
                db.prepare('UPDATE activation_keys SET days = days * 24').run();
                db.prepare('INSERT INTO system_state ("key", "value") VALUES (\'hours_migration_v2\', \'done\')').run();
                console.log("[MIGRATION] Converted DB to hours (v2)");
            }
        } catch (e) { }

        let lastDecRow = null;
        try {
            lastDecRow = db.prepare('SELECT "value" FROM system_state WHERE "key"=\'last_dec_hour\'').get();
        } catch (e) {
            console.error('[SUBSCRIPTION] Could not read system_state:', e.message);
        }
        
        if (lastDecRow && lastDecRow.value === hourStr) return;

        const result = db.prepare(
            `UPDATE subscriptions SET days_left = days_left - 1
             WHERE days_left > 0 AND (is_frozen = 0 OR is_frozen IS NULL)`
        ).run();
        if (result.changes > 0)
            console.log(`[SUBSCRIPTION] Decremented 1 hour for ${result.changes} subscriptions`);

        try {
            db.prepare('INSERT OR REPLACE INTO system_state ("key", "value") VALUES (\'last_dec_hour\', ?)').run(hourStr);
        } catch (e) {
            console.error('[SUBSCRIPTION] Could not update system_state:', e.message);
        }
    } catch (e) {
        console.error('[SUBSCRIPTION] Decrement error:', e.message);
    }
}

function runMaintenance() {
    try {
        // Декремент дней подписки
        decrementSubscriptionDays();

        // 9.5: удаляем download_log старше 30 дней
        const dl_deleted = db.prepare(`DELETE FROM download_log WHERE ts < datetime('now', '-30 days')`).run();
        if (dl_deleted.changes > 0)
            console.log(`[MAINTENANCE] Deleted ${dl_deleted.changes} old download_log rows`);

        // 9.2: удаляем loader_sessions старше 24 часов (без активного heartbeat)
        const sess_deleted = db.prepare(`DELETE FROM loader_sessions WHERE last_heartbeat < datetime('now', '-24 hours')`).run();
        if (sess_deleted.changes > 0)
            console.log(`[MAINTENANCE] Deleted ${sess_deleted.changes} stale loader_sessions`);

        // Удаляем security_events старше 90 дней (не нужны для расследований)
        const ev_deleted = db.prepare(`DELETE FROM security_events WHERE ts < datetime('now', '-90 days')`).run();
        if (ev_deleted.changes > 0)
            console.log(`[MAINTENANCE] Deleted ${ev_deleted.changes} old security_events rows`);
    } catch (e) {
        console.error('[MAINTENANCE] Error:', e.message);
    }
}

// Запуск раз в час (декремент подписок + уборка)
setInterval(runMaintenance, 60 * 60 * 1000);
// И сразу при старте
runMaintenance();

 
//  START
 
const PORT = process.env.PORT || 3001;
app.listen(PORT, '0.0.0.0', () => console.log(`[shadow backend] Running on port ${PORT}`));

// ─── 9.2 + 9.3 fix: periodic retention cleanup
//   • loader_sessions старше 24 часов — удаляются (фикс стейлаHandlers-session'ов
//     которые висят после выключенной машины юзера).
//   • download_log старше 30 дней — удаляются (в нём лежит сырой HWID каждого
//     скачивания — большая поверхность для деанонимизации при утечке БД).
//   • security_events старше 90 дней — удаляются (общий объём ограничивается —
//     не потребляет тайлы неограниченно).
setInterval(() => {
    try {
        const d = new Date();
        const cutoff_sessions = new Date(d.getTime() - 24 * 60 * 60 * 1000).toISOString();
        const cutoff_dl = new Date(d.getTime() - 30 * 24 * 60 * 60 * 1000).toISOString();
        const cutoff_events = new Date(d.getTime() - 90 * 24 * 60 * 60 * 1000).toISOString();
        const s3 = db.prepare(`DELETE FROM security_events  WHERE ts < ?`).run(cutoff_events);
        if (s3.changes) {
            console.log(`[cleanup] security_events=${s3.changes}`);
        }
    } catch (e) {
        console.error('[cleanup] failed:', e.message);
    }
}, 60 * 60 * 1000); // каждый час

 
//  MEDIA PARTNERS
 

app.get('/api/media/stats', authenticateToken, (req, res) => {
    if (!req.user.media_access) return res.status(403).json({ error: 'Доступ запрещен' });

    // Получаем статистику по использованию промокода
    const stats = db.prepare(`SELECT count(*) as total_uses FROM promo_uses WHERE promocode = ? COLLATE NOCASE`).get(req.user.promocode);

    res.json({
        ok: true,
        promocode: req.user.promocode,
        promo_percent: req.user.promo_percent,
        promo_earned: req.user.promo_earned,
        total_uses: stats.total_uses || 0
    });
});

 
//  DISTRIBUTORS
 

app.get('/api/distributor/keys', authenticateToken, (req, res) => {
    if (!req.user.is_distributor) return res.status(403).json({ error: 'Доступ запрещен' });

    const keys = db.prepare('SELECT key_str, product_id, days, used, used_by, used_at, created_at FROM activation_keys WHERE distributor_id = ? ORDER BY id DESC').all(req.user.id);
    keys.forEach(k => k.days = Math.ceil(k.days / 24));
    res.json({ ok: true, keys });
});

 
//  ADMIN: DISTRIBUTOR & MEDIA MANAGEMENT
 

app.post('/api/admin/distributor/issue', authenticateToken, requireAdmin, (req, res) => {
    const { distributor_id, product_id, days, count } = req.body;
    if (!distributor_id || !product_id || !days || !count) return res.status(400).json({ error: 'Missing fields' });

    const keys = [];
    const genChars = (len) => {
        const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789';
        return Array.from({ length: len }, () => chars[Math.floor(Math.random() * chars.length)]).join('');
    };

    try {
        db.prepare('BEGIN').run();
        const stmt = db.prepare('INSERT INTO activation_keys (key_str, product_id, days, distributor_id) VALUES (?, ?, ?, ?)');
        for (let i = 0; i < count; i++) {
            const keyStr = `${genChars(5)}-${genChars(8)}-${genChars(9)}-${genChars(8)}`;
            stmt.run(keyStr, product_id, days * 24, distributor_id);
            keys.push(keyStr);
        }
        db.prepare('COMMIT').run();
        db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, `ISSUE_${count}_KEYS_TO_DISTRIBUTOR`, distributor_id, req.ip);
        res.json({ ok: true, keys });
    } catch (e) {
        db.prepare('ROLLBACK').run();
        res.status(500).json({ error: 'Ошибка генерации' });
    }
});

app.put('/api/admin/users/:id', authenticateToken, requireAdmin, (req, res) => {
    const userId = req.params.id;
    const { media_access, is_distributor, promocode, promo_percent, promo_earned } = req.body;

    const updates = [];
    const params = [];

    if (media_access !== undefined) { updates.push('media_access=?'); params.push(media_access); }
    if (is_distributor !== undefined) { updates.push('is_distributor=?'); params.push(is_distributor); }
    if (promocode !== undefined) { updates.push('promocode=?'); params.push(promocode); }
    if (promo_percent !== undefined) { updates.push('promo_percent=?'); params.push(promo_percent); }
    if (promo_earned !== undefined) { updates.push('promo_earned=?'); params.push(promo_earned); }

    if (updates.length > 0) {
        params.push(userId);
        db.prepare(`UPDATE users SET ${updates.join(', ')} WHERE id=?`).run(...params);
        db.prepare('INSERT INTO audit_log (admin_id, action, target_id, ip) VALUES (?,?,?,?)').run(req.user.id, 'UPDATE_USER_ROLES', userId, req.ip);
    }
    res.json({ ok: true });
});
// End of admin block

function writeRadarEvent(res, name, payload) {
    res.write(`event: ${name}\n`);
    res.write(`data: ${JSON.stringify(payload)}\n\n`);
}

app.post('/api/radar/push', radarPushLimiter, (req, res) => {
    const user = resolveConfigUser(req);
    if (!user) return res.status(401).json({ error: 'Unauthorized' });
    const now = Date.now();
    radarStore.prune(now);
    radarStore.put(user.id, req.body && typeof req.body === 'object' ? req.body : {}, now);
    res.json({ ok: true });
});

app.get('/api/radar/latest', authenticateToken, (req, res) => {
    const now = Date.now();
    radarStore.prune(now);
    res.json({ frame: radarStore.get(req.user.id, now) });
});

app.get('/api/radar/stream', authenticateToken, (req, res) => {
    res.setHeader('Content-Type', 'text/event-stream');
    res.setHeader('Cache-Control', 'no-cache, no-transform');
    res.setHeader('Connection', 'keep-alive');
    res.setHeader('X-Accel-Buffering', 'no');
    if (typeof res.flushHeaders === 'function')
        res.flushHeaders();
    const now = Date.now();
    const latest = radarStore.get(req.user.id, now);
    writeRadarEvent(res, latest ? 'frame' : 'idle', latest || { empty: true });
    const ping = setInterval(() => writeRadarEvent(res, 'ping', { t: Date.now() }), 15_000);
    const unsub = radarStore.subscribe(req.user.id, (frame) => {
        writeRadarEvent(res, 'frame', frame);
    });
    req.on('close', () => {
        clearInterval(ping);
        unsub();
    });
});

// ─── Session ban check in checkLoaderVersion



