#!/usr/bin/env node
/* Створює секрети й файл .env: node scripts/generate_secrets.js.
 * Сервер потребує JWT_SECRET, CLIENT_SHARED_SECRET, SESSION_KEY_MASTER_SECRET,
 * ADMIN_USERNAME, ADMIN_PASSWORD і DRIVER_MASTER_KEY. Налаштування домену, порту й CORS необов’язкові. */

const crypto = require('crypto')
const fs     = require('fs')
const path   = require('path')
const readline = require('readline')

const rl = readline.createInterface({ input: process.stdin, output: process.stdout })
const ask = (q) => new Promise(r => rl.question(q, r))

async function main() {
    console.log('\n🛡️  Shadow Cheats — Release Setup\n')
    console.log('='.repeat(60))

    // ─── Генерация секретов ───
    const jwt_secret          = crypto.randomBytes(32).toString('hex')
    const session_key_master  = crypto.randomBytes(32).toString('hex')
    const driver_master_key   = crypto.randomBytes(32).toString('hex')
    const refresh_secret      = crypto.randomBytes(32).toString('hex')

    // CLIENT_SHARED_SECRET — особый: должен совпадать с SECRET_ENC в network.hpp.
    // Для dev-теста (без перегенерации SECRET_ENC) — используем дефолтный plaintext.
    console.log('\n--- CLIENT_SHARED_SECRET ---')
    console.log('   network.hpp SECRET_ENC содержит: "ShadowSecretChangeMe!"')
    console.log('   Для dev: оставить дефолт → HMAC совпадёт без правки лоадера')
    console.log('   Для prod: сгенерировать новый → но тогда нужно перегенерировать')
    console.log('              SECRET_ENC в network.hpp через tools/')
    const use_default_secret = (await ask('Использовать дефолтный секрет? (Y/n): ')).toLowerCase()
    const client_secret = (!use_default_secret || use_default_secret === 'y')
        ? 'ShadowSecretChangeMe!'
        : crypto.randomBytes(32).toString('hex')

    console.log(`\n✅ Сгенерированы секреты (скрыты в .env):`)
    console.log(`   JWT_SECRET             = ${jwt_secret.substring(0, 12)}...`)
    console.log(`   CLIENT_SHARED_SECRET   = ${client_secret.substring(0, 16)}${client_secret.length > 16 ? '...' : ''}`)
    console.log(`   SESSION_KEY_MASTER     = ${session_key_master.substring(0, 12)}...`)
    console.log(`   DRIVER_MASTER_KEY      = ${driver_master_key.substring(0, 12)}...`)

    // ─── Запросить конфигурацию ───
    console.log('\n--- Настройки ---')
    const admin_username    = await ask('\nAdmin логин (default: admin): ') || 'admin'
    const admin_password    = await ask('Admin пароль (≥ 12 символов): ')
    const allowed_origin   = await ask('CORS origin (default: http://localhost:5173): ') || 'http://localhost:5173'
    const port             = await ask('Порт сервера (default: 3001): ') || '3001'
    rl.close()

    if (!admin_password || admin_password.length < 12) {
        console.error('\n[FATAL] Пароль слишком короткий! Минимум 12 символов. Сервер не запустится.')
        process.exit(1)
    }

    // ─── Записать .env ───
    const env_content = `# Shadow Cheats Backend — GENERATED ${new Date().toISOString()}
# ⚠️ НИКОГДА не коммить этот файл в git!

# ── JWT auth ──
JWT_SECRET=${jwt_secret}
JWT_REFRESH_SECRET=${refresh_secret}
JWT_EXPIRES_IN=2h
JWT_REFRESH_EXPIRES_IN=30d

# ── Client HMAC secret (должен совпадать с SECRET_ENC в network.hpp!) ──
CLIENT_SHARED_SECRET=${client_secret}

# ── Session key encryption at rest (AES-256-GCM in SQLite) ──
SESSION_KEY_MASTER_SECRET=${session_key_master}

# ── Driver encryption at rest (AES-256-GCM for driver.enc) ──
DRIVER_MASTER_KEY=${driver_master_key}

# ── Admin account ──
ADMIN_USERNAME=${admin_username}
ADMIN_PASSWORD=${admin_password}

# ── CORS ──
ALLOWED_ORIGIN=${allowed_origin}

# ── Server ──
PORT=${port}
NODE_ENV=development

# ── Telegram bot (optional, см. ban_reporter.hpp) ──
# TELEGRAM_BOT_TOKEN=
`
    const env_path = path.join(__dirname, '..', '.env')
    fs.writeFileSync(env_path, env_content, 'utf8')
    console.log(`\n✅ .env создан: ${env_path}`)

    // ─── Вывод для C++ лоадера ───
    console.log('\n' + '='.repeat(60))
    console.log('📋 CLIENT_SHARED_SECRET для .env сервера совпадает с')
    console.log('   SECRET_ENC в network.hpp. Дефолтный dev-секрет лоадера:')
    console.log('   "ShadowSecretChangeMe!" (21 chars)')
    console.log('')
    console.log('   Для dev-теста: оставь server CLIENT_SHARED_SECRET = текстовая строка,')
    console.log('   и network.hpp SECRET_ENC как есть (XOR-обфусцированный plaintext).')
    console.log('   Для прода: пересгенерируй SECRET_ENC через инструменты генерации.')
    console.log('='.repeat(60))

    // ─── Создать папки для продуктов если нет ───
    const products_dir = path.join(__dirname, '..', 'products')
    if (!fs.existsSync(products_dir)) {
        fs.mkdirSync(path.join(products_dir, 'rage'), { recursive: true })
        fs.mkdirSync(path.join(products_dir, 'altv'), { recursive: true })
        console.log('✅ Созданы папки products/rage и products/altv')
    }
    const drivers_dir = path.join(__dirname, '..', 'drivers')
    if (!fs.existsSync(drivers_dir)) {
        fs.mkdirSync(path.join(drivers_dir, 'rage'), { recursive: true })
        fs.mkdirSync(path.join(drivers_dir, 'altv'), { recursive: true })
        console.log('✅ Созданы папки drivers/rage и drivers/altv')
    }

    console.log('\n' + '='.repeat(60))
    console.log('✅ .env готов! Следующие шаги:')
    console.log('   1. npm install')
    console.log('   2. npm start')
    console.log('   3. Скомпилируй читы и лоадер, положи в products/')
    console.log('   4. Зашифруй driver.sys: node scripts/encrypt_driver.js <path> <product_id>')
    console.log('='.repeat(60) + '\n')
}

main().catch(e => { console.error(e); process.exit(1) })