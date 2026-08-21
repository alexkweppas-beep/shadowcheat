const crypto = require('crypto');
const fs = require('fs');

// simulate decryptSessionKey
function decryptSessionKey(blob) {
    const buf = Buffer.from(blob, 'base64');
    const iv = buf.slice(0, 12);
    const tag = buf.slice(12, 28);
    const ct = buf.slice(28);
    // Use the actual master key from .env
    require('dotenv').config();
    const SESSION_KEY_MASTER = process.env.SESSION_KEY_MASTER_SECRET || 'daf96293621aa55f8cc40e393abb52949975119c5e68dd80a24f309d31975c47';
    const key = crypto.scryptSync(SESSION_KEY_MASTER, 'shadow_skey_v1', 32);
    const decipher = crypto.createDecipheriv('aes-256-gcm', key, iv);
    decipher.setAuthTag(tag);
    return decipher.update(ct, null, 'utf8') + decipher.final('utf8');
}

const db = require('better-sqlite3')('shadow.db');

// Get the latest loader session
const session = db.prepare('SELECT * FROM loader_sessions ORDER BY last_heartbeat DESC LIMIT 1').get();
if (!session) {
    console.log("No loader session found.");
    process.exit(1);
}

console.log("Session User ID:", session.user_id);
const session_key_hex = decryptSessionKey(session.session_key_hex);
console.log("Session Key Hex:", session_key_hex);

const session_key_buf = Buffer.from(session_key_hex, 'hex');

require('dotenv').config();
const DRIVER_MASTER_KEY = process.env.DRIVER_MASTER_KEY;
console.log("Driver master key length:", DRIVER_MASTER_KEY ? DRIVER_MASTER_KEY.length : 0);

try {
    const driver_master_enc = fs.readFileSync('./drivers/external/driver.enc');
    console.log("Driver.enc read, size:", driver_master_enc.length);

    const m_iv = driver_master_enc.slice(0, 12);
    const m_tag = driver_master_enc.slice(12, 28);
    const m_ct = driver_master_enc.slice(28);
    const m_key = crypto.scryptSync(DRIVER_MASTER_KEY, 'shadow_drv_v1', 32);
    const decipher = crypto.createDecipheriv('aes-256-gcm', m_key, m_iv);
    decipher.setAuthTag(m_tag);
    const driver_bytes = Buffer.concat([decipher.update(m_ct), decipher.final()]);
    console.log("Master decrypt success. Driver raw size:", driver_bytes.length);

    // Re-encrypt with session key
    const s_iv = crypto.randomBytes(12);
    const s_cipher = crypto.createCipheriv('aes-256-gcm', session_key_buf, s_iv);
    const s_ct = Buffer.concat([s_cipher.update(driver_bytes), s_cipher.final()]);
    const s_tag = s_cipher.getAuthTag();
    const driver_payload = Buffer.concat([s_iv, s_ct, s_tag]);

    console.log("Re-encrypt success. Payload size:", driver_payload.length);
    
    // Now simulate loader decrypt
    const l_iv = driver_payload.slice(0, 12);
    const l_ct = driver_payload.slice(12, driver_payload.length - 16);
    const l_tag = driver_payload.slice(driver_payload.length - 16);
    
    const l_decipher = crypto.createDecipheriv('aes-256-gcm', session_key_buf, l_iv);
    l_decipher.setAuthTag(l_tag);
    const l_driver_bytes = Buffer.concat([l_decipher.update(l_ct), l_decipher.final()]);
    
    console.log("Loader decrypt simulation success! Output size:", l_driver_bytes.length);

} catch (e) {
    console.error("Error:", e);
}
