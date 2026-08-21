#!/usr/bin/env node
/**
 * encrypt_driver.js — зашифровать driver.sys мастер-ключом перед деплоем
 *
 * Использование:
 *   node scripts/encrypt_driver.js <путь к driver.sys> <product_id>
 *
 * Пример:
 *   node scripts/encrypt_driver.js ./driver.sys rage
 *   node scripts/encrypt_driver.js ./driver_altv.sys altv
 *
 * Результат:
 *   backend/drivers/<product_id>/driver.enc
 *
 * Формат driver.enc: IV(12) + GCM_TAG(16) + ciphertext
 * (тот же формат что использует /api/loader/driver для расшифровки)
 *
 * Требует: DRIVER_MASTER_KEY в .env или переменной окружения
 */

'use strict';
require('dotenv').config({ path: require('path').join(__dirname, '..', '.env') });
const crypto = require('crypto');
const fs     = require('fs');
const path   = require('path');

const [,, driver_path, product_id] = process.argv;

if (!driver_path || !product_id) {
    console.error('Usage: node encrypt_driver.js <driver.sys path> <product_id>');
    process.exit(1);
}

const DRIVER_MASTER_KEY = process.env.DRIVER_MASTER_KEY;
if (!DRIVER_MASTER_KEY || DRIVER_MASTER_KEY.length < 32) {
    console.error('[ERROR] DRIVER_MASTER_KEY not set or too short in .env');
    console.error('        Generate: openssl rand -hex 32');
    process.exit(1);
}

if (!fs.existsSync(driver_path)) {
    console.error(`[ERROR] File not found: ${driver_path}`);
    process.exit(1);
}

const driver_bytes = fs.readFileSync(driver_path);
console.log(`[INFO] Driver size: ${driver_bytes.length} bytes`);

// Шифруем AES-256-GCM мастер-ключом
const iv     = crypto.randomBytes(12);
const m_key  = crypto.scryptSync(DRIVER_MASTER_KEY, 'shadow_drv_v1', 32);
const cipher = crypto.createCipheriv('aes-256-gcm', m_key, iv);
const ct     = Buffer.concat([cipher.update(driver_bytes), cipher.final()]);
const tag    = cipher.getAuthTag();

// Формат: IV(12) + TAG(16) + ciphertext
const enc_buf = Buffer.concat([iv, tag, ct]);

// Создаём директорию для продукта
const out_dir  = path.join(__dirname, '..', 'drivers', product_id);
const out_path = path.join(out_dir, 'driver.enc');
fs.mkdirSync(out_dir, { recursive: true });
fs.writeFileSync(out_path, enc_buf);

console.log(`[OK] Encrypted driver saved: ${out_path}`);
console.log(`     Size: ${enc_buf.length} bytes (${driver_bytes.length} + 28 overhead)`);
console.log(`     Product: ${product_id}`);
console.log('');
console.log('[IMPORTANT] Add DRIVER_MASTER_KEY to your server .env before deploying!');
